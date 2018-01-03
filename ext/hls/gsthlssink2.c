/* GStreamer
 * Copyright (C) 2011 Alessandro Decina <alessandro.d@gmail.com>
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-hlssink
 * @title: hlssink
 *
 * HTTP Live Streaming sink/server
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc is-live=true ! x264enc ! hlssink max-files=5
 * ]|
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthlssink2.h"
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <glib/gstdio.h>
#include <memory.h>


GST_DEBUG_CATEGORY_STATIC (gst_hls_sink2_debug);
#define GST_CAT_DEFAULT gst_hls_sink2_debug

#define DEFAULT_LOCATION "segment%05d.ts"
#define DEFAULT_PLAYLIST_LOCATION "playlist.m3u8"
#define DEFAULT_PLAYLIST_ROOT NULL
#define DEFAULT_MAX_FILES 10
#define DEFAULT_TARGET_DURATION 15
#define DEFAULT_PLAYLIST_LENGTH 5

#define GST_M3U8_PLAYLIST_VERSION 3

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PLAYLIST_LOCATION,
  PROP_PLAYLIST_ROOT,
  PROP_MAX_FILES,
  PROP_TARGET_DURATION,
  PROP_PLAYLIST_LENGTH
};

static GstStaticPadTemplate video_template = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate audio_template = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define gst_hls_sink2_parent_class parent_class
G_DEFINE_TYPE (GstHlsSink2, gst_hls_sink2, GST_TYPE_BIN);

static void gst_hls_sink2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_hls_sink2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static void gst_hls_sink2_handle_message (GstBin * bin, GstMessage * message);
static void gst_hls_sink2_reset (GstHlsSink2 * sink);
static GstStateChangeReturn
gst_hls_sink2_change_state (GstElement * element, GstStateChange trans);
static GstPad *gst_hls_sink2_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_hls_sink2_release_pad (GstElement * element, GstPad * pad);

static void
gst_hls_sink2_dispose (GObject * object)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (object);

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) sink);
}

static void
gst_hls_sink2_finalize (GObject * object)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (object);

  g_free (sink->location);
  g_free (sink->playlist_location);
  g_free (sink->playlist_root);
  if (sink->playlist)
    gst_m3u8_playlist_free (sink->playlist);

  g_queue_foreach (&sink->old_locations, (GFunc) g_free, NULL);
  g_queue_clear (&sink->old_locations);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) sink);
}

static void
gst_hls_sink2_class_init (GstHlsSink2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBinClass *bin_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  bin_class = GST_BIN_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &video_template);
  gst_element_class_add_static_pad_template (element_class, &audio_template);

  gst_element_class_set_static_metadata (element_class,
      "HTTP Live Streaming sink", "Sink", "HTTP Live Streaming sink",
      "Alessandro Decina <alessandro.d@gmail.com>, "
      "Sebastian Dröge <sebastian@centricular.com>");

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_hls_sink2_change_state);
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_hls_sink2_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_hls_sink2_release_pad);

  bin_class->handle_message = gst_hls_sink2_handle_message;

  gobject_class->dispose = gst_hls_sink2_dispose;
  gobject_class->finalize = gst_hls_sink2_finalize;
  gobject_class->set_property = gst_hls_sink2_set_property;
  gobject_class->get_property = gst_hls_sink2_get_property;

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
  g_object_class_install_property (gobject_class, PROP_TARGET_DURATION,
      g_param_spec_uint ("target-duration", "Target duration",
          "The target duration in seconds of a segment/file. "
          "(0 - disabled, useful for management of segment duration by the "
          "streaming server)",
          0, G_MAXUINT, DEFAULT_TARGET_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PLAYLIST_LENGTH,
      g_param_spec_uint ("playlist-length", "Playlist length",
          "Length of HLS playlist. To allow players to conform to section 6.3.3 "
          "of the HLS specification, this should be at least 3. If set to 0, "
          "the playlist will be infinite.",
          0, G_MAXUINT, DEFAULT_PLAYLIST_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_hls_sink2_init (GstHlsSink2 * sink)
{
  GstElement *mux;

  sink->location = g_strdup (DEFAULT_LOCATION);
  sink->playlist_location = g_strdup (DEFAULT_PLAYLIST_LOCATION);
  sink->playlist_root = g_strdup (DEFAULT_PLAYLIST_ROOT);
  sink->playlist_length = DEFAULT_PLAYLIST_LENGTH;
  sink->max_files = DEFAULT_MAX_FILES;
  sink->target_duration = DEFAULT_TARGET_DURATION;
  g_queue_init (&sink->old_locations);

  sink->splitmuxsink = gst_element_factory_make ("splitmuxsink", NULL);
  gst_bin_add (GST_BIN (sink), sink->splitmuxsink);

  mux = gst_element_factory_make ("mpegtsmux", NULL);
  g_object_set (sink->splitmuxsink, "location", sink->location, "max-size-time",
      ((GstClockTime) sink->target_duration * GST_SECOND),
      "send-keyframe-requests", TRUE, "muxer", mux, NULL);

  GST_OBJECT_FLAG_SET (sink, GST_ELEMENT_FLAG_SINK);

  gst_hls_sink2_reset (sink);
}

static void
gst_hls_sink2_reset (GstHlsSink2 * sink)
{
  sink->index = 0;

  if (sink->playlist)
    gst_m3u8_playlist_free (sink->playlist);
  sink->playlist =
      gst_m3u8_playlist_new (GST_M3U8_PLAYLIST_VERSION, sink->playlist_length,
      FALSE);

  g_queue_foreach (&sink->old_locations, (GFunc) g_free, NULL);
  g_queue_clear (&sink->old_locations);
}

static void
gst_hls_sink2_write_playlist (GstHlsSink2 * sink)
{
  char *playlist_content;
  GError *error = NULL;

  playlist_content = gst_m3u8_playlist_render (sink->playlist);
  if (!g_file_set_contents (sink->playlist_location,
          playlist_content, -1, &error)) {
    GST_ERROR ("Failed to write playlist: %s", error->message);
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        (("Failed to write playlist '%s'."), error->message), (NULL));
    g_error_free (error);
    error = NULL;
  }
  g_free (playlist_content);

}

static void
gst_hls_sink2_handle_message (GstBin * bin, GstMessage * message)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (bin);

  switch (message->type) {
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);
      if (message->src == GST_OBJECT_CAST (sink->splitmuxsink)) {
        if (gst_structure_has_name (s, "splitmuxsink-fragment-opened")) {
          g_free (sink->current_location);
          sink->current_location =
              g_strdup (gst_structure_get_string (s, "location"));
          gst_structure_get_clock_time (s, "running-time",
              &sink->current_running_time_start);
        } else if (gst_structure_has_name (s, "splitmuxsink-fragment-closed")) {
          GstClockTime running_time;
          gchar *entry_location;

          g_assert (strcmp (sink->current_location, gst_structure_get_string (s,
                      "location")) == 0);

          gst_structure_get_clock_time (s, "running-time", &running_time);

          GST_INFO_OBJECT (sink, "COUNT %d", sink->index);
          if (sink->playlist_root == NULL) {
            entry_location = g_path_get_basename (sink->current_location);
          } else {
            gchar *name = g_path_get_basename (sink->current_location);
            entry_location = g_build_filename (sink->playlist_root, name, NULL);
            g_free (name);
          }

          gst_m3u8_playlist_add_entry (sink->playlist, entry_location,
              NULL, running_time - sink->current_running_time_start,
              sink->index++, FALSE);
          g_free (entry_location);

          gst_hls_sink2_write_playlist (sink);

          g_queue_push_tail (&sink->old_locations,
              g_strdup (sink->current_location));

          while (g_queue_get_length (&sink->old_locations) >
              g_queue_get_length (sink->playlist->entries)) {
            gchar *old_location = g_queue_pop_head (&sink->old_locations);
            g_remove (old_location);
            g_free (old_location);
          }
        }
      }
      break;
    }
    case GST_MESSAGE_EOS:{
      sink->playlist->end_list = TRUE;
      gst_hls_sink2_write_playlist (sink);
      break;
    }
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static GstPad *
gst_hls_sink2_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (element);
  GstPad *pad, *peer;
  gboolean is_audio;

  g_return_val_if_fail (strcmp (templ->name_template, "audio") == 0
      || strcmp (templ->name_template, "video") == 0, NULL);
  g_return_val_if_fail (strcmp (templ->name_template, "audio") != 0
      || !sink->audio_sink, NULL);
  g_return_val_if_fail (strcmp (templ->name_template, "video") != 0
      || !sink->video_sink, NULL);

  is_audio = strcmp (templ->name_template, "audio") == 0;

  peer =
      gst_element_get_request_pad (sink->splitmuxsink,
      is_audio ? "audio_0" : "video");
  if (!peer)
    return NULL;

  pad = gst_ghost_pad_new_from_template (templ->name_template, peer, templ);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);
  gst_object_unref (peer);

  if (is_audio)
    sink->audio_sink = pad;
  else
    sink->video_sink = pad;

  return pad;
}

static void
gst_hls_sink2_release_pad (GstElement * element, GstPad * pad)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (element);
  GstPad *peer;

  g_return_if_fail (pad == sink->audio_sink || pad == sink->video_sink);

  peer = gst_pad_get_peer (pad);
  if (peer) {
    gst_element_release_request_pad (sink->splitmuxsink, pad);
    gst_object_unref (peer);
  }

  gst_object_ref (pad);
  gst_element_remove_pad (element, pad);
  gst_pad_set_active (pad, FALSE);
  if (pad == sink->audio_sink)
    sink->audio_sink = NULL;
  else
    sink->video_sink = NULL;

  gst_object_unref (pad);
}

static GstStateChangeReturn
gst_hls_sink2_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!sink->splitmuxsink) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_hls_sink2_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_hls_sink2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (sink->location);
      sink->location = g_value_dup_string (value);
      if (sink->splitmuxsink)
        g_object_set (sink->splitmuxsink, "location", sink->location, NULL);
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
      break;
    case PROP_TARGET_DURATION:
      sink->target_duration = g_value_get_uint (value);
      if (sink->splitmuxsink) {
        g_object_set (sink->splitmuxsink, "max-size-time",
            ((GstClockTime) sink->target_duration * GST_SECOND), NULL);
      }
      break;
    case PROP_PLAYLIST_LENGTH:
      sink->playlist_length = g_value_get_uint (value);
      sink->playlist->window_size = sink->playlist_length;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_sink2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHlsSink2 *sink = GST_HLS_SINK2_CAST (object);

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
    case PROP_TARGET_DURATION:
      g_value_set_uint (value, sink->target_duration);
      break;
    case PROP_PLAYLIST_LENGTH:
      g_value_set_uint (value, sink->playlist_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_hls_sink2_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_hls_sink2_debug, "hlssink2", 0, "HlsSink2");
  return gst_element_register (plugin, "hlssink2", GST_RANK_NONE,
      gst_hls_sink2_get_type ());
}
