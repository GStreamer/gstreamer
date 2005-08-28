/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#include "gstgconfelements.h"
#include "gstgconfvideosink.h"

static void gst_gconf_video_sink_dispose (GObject * object);
static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstElementStateReturn
gst_gconf_video_sink_change_state (GstElement * element);

GST_BOILERPLATE (GstGConfVideoSink, gst_gconf_video_sink, GstBin, GST_TYPE_BIN);

static void
gst_gconf_video_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_gconf_video_sink_details = {
    "GConf video sink",
    "Sink/Video",
    "Video sink embedding the GConf-settings for video output",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_gconf_video_sink_details);
}

static void
gst_gconf_video_sink_class_init (GstGConfVideoSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->dispose = gst_gconf_video_sink_dispose;
  eklass->change_state = gst_gconf_video_sink_change_state;
}

/*
 * Hack to make negotiation work.
 */

static void
gst_gconf_video_sink_reset (GstGConfVideoSink * sink)
{
  GstPad *targetpad;

  /* fakesink */
  if (sink->kid) {
    gst_bin_remove (GST_BIN (sink), sink->kid);
  }
  sink->kid = gst_element_factory_make ("fakesink", "testsink");
  gst_bin_add (GST_BIN (sink), sink->kid);

  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
}

static void
gst_gconf_video_sink_init (GstGConfVideoSink * sink,
    GstGConfVideoSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_notarget ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_gconf_video_sink_reset (sink);

  sink->client = gconf_client_get_default ();
  gconf_client_add_dir (sink->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  gconf_client_notify_add (sink->client, GST_GCONF_DIR "/default/videosink",
      cb_toggle_element, sink, NULL, NULL);
}

static void
gst_gconf_video_sink_dispose (GObject * object)
{
  GstGConfVideoSink *sink = GST_GCONF_VIDEO_SINK (object);

  if (sink->client) {
    g_object_unref (G_OBJECT (sink->client));
    sink->client = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
do_toggle_element (GstGConfVideoSink * sink)
{
  GstPad *targetpad;

  /* kill old element */
  if (sink->kid) {
    GST_DEBUG_OBJECT (sink, "Removing old kid");
    gst_bin_remove (GST_BIN (sink), sink->kid);
    sink->kid = NULL;
  }

  GST_DEBUG_OBJECT (sink, "Creating new kid");
  if (!(sink->kid = gst_gconf_get_default_video_sink ())) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render video sink from GConf"));
    return FALSE;
  }
  gst_element_set_state (sink->kid, GST_STATE (sink));
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (sink, "Creating new ghostpad");
  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (sink, "done changing gconf video sink");

  return TRUE;
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_toggle_element (GST_GCONF_VIDEO_SINK (data));
}

static GstElementStateReturn
gst_gconf_video_sink_change_state (GstElement * element)
{
  GstGConfVideoSink *sink = GST_GCONF_VIDEO_SINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!do_toggle_element (sink))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      gst_gconf_video_sink_reset (sink);
      break;
    default:
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element), GST_STATE_SUCCESS);
}
