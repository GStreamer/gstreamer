/* GStreamer
 * Copyright (C) 2011 Alessandro Decina <alessandro.d@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthlssink.h"
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <memory.h>


GST_DEBUG_CATEGORY_STATIC (gst_hls_sink_debug);
#define GST_CAT_DEFAULT gst_hls_sink_debug

#define DEFAULT_LOCATION "segment%05d.ts"
#define DEFAULT_PLAYLIST_LOCATION "playlist.m3u8"
#define DEFAULT_PLAYLIST_ROOT NULL
#define DEFAULT_MAX_FILES 10

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PLAYLIST_LOCATION,
  PROP_PLAYLIST_ROOT,
  PROP_MAX_FILES
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstHlsSink, gst_hls_sink, GstBin, GST_TYPE_BIN);

static void gst_hls_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_hls_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static void gst_hls_sink_handle_message (GstBin * bin, GstMessage * message);
static gboolean gst_hls_sink_ghost_event_probe (GstPad * pad,
    GstEvent * event, gpointer data);

static GstStateChangeReturn
gst_hls_sink_change_state (GstElement * element, GstStateChange trans);

static void
gst_hls_sink_dispose (GObject * object)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (object);

  if (sink->multifilesink)
    g_object_unref (sink->multifilesink);

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) sink);
}

static void
gst_hls_sink_finalize (GObject * object)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (object);

  gst_event_replace (&sink->force_key_unit_event, NULL);
  g_free (sink->location);
  g_free (sink->playlist_location);
  g_free (sink->playlist_root);
  gst_m3u8_playlist_free (sink->playlist);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) sink);
}

static void
gst_hls_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class,
      "HTTP Live Streaming sink", "Sink", "HTTP Live Streaming sink",
      "Alessandro Decina <alessandro.decina@gmail.com>");
}

static void
gst_hls_sink_class_init (GstHlsSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBinClass *bin_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  bin_class = GST_BIN_CLASS (klass);

  bin_class->handle_message = gst_hls_sink_handle_message;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_sink_change_state);

  gobject_class->dispose = gst_hls_sink_dispose;
  gobject_class->finalize = gst_hls_sink_finalize;
  gobject_class->set_property = gst_hls_sink_set_property;
  gobject_class->get_property = gst_hls_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", DEFAULT_LOCATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PLAYLIST_LOCATION,
      g_param_spec_string ("playlist-location", "Playlist Location",
          "Location of the playlist to write", DEFAULT_PLAYLIST_LOCATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PLAYLIST_ROOT,
      g_param_spec_string ("playlist-root", "Playlist Root",
          "Location of the playlist to write", DEFAULT_PLAYLIST_ROOT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_FILES,
      g_param_spec_uint ("max-files", "Max files",
          "Maximum number of files to keep on disk. Once the maximum is reached,"
          "old files start to be deleted to make room for new ones.",
          0, G_MAXUINT, DEFAULT_MAX_FILES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_hls_sink_init (GstHlsSink * sink, GstHlsSinkClass * sink_class)
{
  GstPadTemplate *templ = gst_static_pad_template_get (&sink_template);
  sink->ghostpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_object_unref (templ);
  gst_element_add_pad (GST_ELEMENT_CAST (sink), sink->ghostpad);
  gst_pad_add_event_probe (sink->ghostpad,
      G_CALLBACK (gst_hls_sink_ghost_event_probe), sink);

  sink->index = 0;
  sink->multifilesink = NULL;
  sink->last_stream_time = 0;
  sink->location = g_strdup (DEFAULT_LOCATION);
  sink->playlist_location = g_strdup (DEFAULT_PLAYLIST_LOCATION);
  sink->playlist_root = g_strdup (DEFAULT_PLAYLIST_ROOT);
  sink->playlist = gst_m3u8_playlist_new (6, 5, FALSE);
  sink->max_files = DEFAULT_MAX_FILES;
}

static gboolean
gst_hls_sink_create_elements (GstHlsSink * sink)
{
  GstPad *pad = NULL;

  GST_DEBUG_OBJECT (sink, "Creating internal elements");

  if (sink->elements_created)
    return TRUE;

  sink->multifilesink = gst_element_factory_make ("multifilesink", NULL);
  if (sink->multifilesink == NULL)
    goto missing_element;

  g_object_set (sink->multifilesink, "location", sink->location,
      "next-file", 3, "post-messages", TRUE, "max-files", sink->max_files,
      NULL);

  gst_bin_add (GST_BIN_CAST (sink), sink->multifilesink);

  pad = gst_element_get_static_pad (sink->multifilesink, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->ghostpad), pad);
  gst_object_unref (pad);

  sink->elements_created = TRUE;
  return TRUE;

missing_element:
  gst_element_post_message (GST_ELEMENT_CAST (sink),
      gst_missing_element_message_new (GST_ELEMENT_CAST (sink),
          "multifilesink"));
  GST_ELEMENT_ERROR (sink, CORE, MISSING_PLUGIN,
      (("Missing element '%s' - check your GStreamer installation."),
          "multifilesink"), (NULL));
  return FALSE;
}

static void
gst_hls_sink_handle_message (GstBin * bin, GstMessage * message)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (bin);

  switch (message->type) {
    case GST_MESSAGE_ELEMENT:
    {
      GFile *file;
      const char *filename, *title;
      char *playlist_content;
      GstClockTime stream_time, duration;
      gboolean discont = FALSE;
      GError *error = NULL;
      gchar *entry_location;

      if (strcmp (gst_structure_get_name (message->structure),
              "GstMultiFileSink"))
        break;

      filename = gst_structure_get_string (message->structure, "filename");
      gst_structure_get_clock_time (message->structure, "stream-time",
          &stream_time);
      duration = stream_time - sink->last_stream_time;
      sink->last_stream_time = stream_time;
      file = g_file_new_for_path (filename);
      title = "ciao";
      GST_INFO_OBJECT (sink, "COUNT %d", sink->index);
      if (sink->playlist_root == NULL)
        entry_location = g_strdup (filename);
      else {
        gchar *name = g_path_get_basename (filename);
        entry_location = g_build_filename (sink->playlist_root, name, NULL);
        g_free (name);
      }
      gst_m3u8_playlist_add_entry (sink->playlist, entry_location, file,
          title, duration, sink->index, discont);
      g_free (entry_location);
      playlist_content = gst_m3u8_playlist_render (sink->playlist);
      g_file_set_contents (sink->playlist_location,
          playlist_content, -1, &error);
      g_free (playlist_content);
      break;
    }
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static GstStateChangeReturn
gst_hls_sink_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstHlsSink *sink = GST_HLS_SINK_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_hls_sink_create_elements (sink)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_hls_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (sink->location);
      sink->location = g_value_dup_string (value);
      if (sink->multifilesink)
        g_object_set (sink->multifilesink, "location", sink->location, NULL);
      break;
    case PROP_PLAYLIST_LOCATION:
      g_free (sink->playlist_location);
      sink->playlist_location = g_value_dup_string (value);
      break;
    case PROP_PLAYLIST_ROOT:
      g_free (sink->playlist_root);
      sink->playlist_root = g_value_dup_string (value);
      break;
    case PROP_MAX_FILES:
      sink->max_files = g_value_get_uint (value);
      if (sink->multifilesink) {
        g_object_set (sink->multifilesink, "location", sink->location,
            "next-file", 3, "post-messages", TRUE, "max-files", sink->max_files,
            NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->location);
      break;
    case PROP_PLAYLIST_LOCATION:
      g_value_set_string (value, sink->playlist_location);
      break;
    case PROP_PLAYLIST_ROOT:
      g_value_set_string (value, sink->playlist_root);
      break;
    case PROP_MAX_FILES:
      g_value_set_uint (value, sink->max_files);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_hls_sink_ghost_event_probe (GstPad * pad, GstEvent * event, gpointer data)
{
  GstHlsSink *sink = GST_HLS_SINK_CAST (data);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstClockTime timestamp;
      GstClockTime running_time, stream_time;
      gboolean all_headers;
      guint count;

      if (!gst_video_event_is_force_key_unit (event))
        break;

      gst_event_replace (&sink->force_key_unit_event, event);
      gst_video_event_parse_downstream_force_key_unit (event,
          &timestamp, &stream_time, &running_time, &all_headers, &count);
      GST_INFO_OBJECT (sink, "setting index %d", count);
      sink->index = count;
      break;
    }
    default:
      break;
  }

  return TRUE;
}


gboolean
gst_hls_sink_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_hls_sink_debug, "hlssink", 0, "HlsSink");
  return gst_element_register (plugin, "hlssink", GST_RANK_NONE,
      gst_hls_sink_get_type ());
}
