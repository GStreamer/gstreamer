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
/**
 * SECTION:element-gconfvideosink
 *
 * This element outputs video to the videosink that has been configured in
 * GConf by the user.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=foo.ogg ! decodebin ! ffmpegcolorspace ! gconfvideosink
 * ]| Play on configured videosink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstgconfelements.h"
#include "gstgconfvideosink.h"

static void gst_gconf_video_sink_dispose (GObject * object);
static void gst_gconf_video_sink_finalize (GstGConfVideoSink * sink);
static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstStateChangeReturn
gst_gconf_video_sink_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstGConfVideoSink, gst_gconf_video_sink, GstBin, GST_TYPE_BIN);

static void
gst_gconf_video_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details_simple (eklass, "GConf video sink",
      "Sink/Video",
      "Video sink embedding the GConf-settings for video output",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");
}

static void
gst_gconf_video_sink_class_init (GstGConfVideoSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->dispose = gst_gconf_video_sink_dispose;
  oklass->finalize = (GObjectFinalizeFunc) gst_gconf_video_sink_finalize;
  eklass->change_state = gst_gconf_video_sink_change_state;
}

/*
 * Hack to make negotiation work.
 */

static gboolean
gst_gconf_video_sink_reset (GstGConfVideoSink * sink)
{
  GstPad *targetpad;

  /* fakesink */
  if (sink->kid) {
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
  }
  sink->kid = gst_element_factory_make ("fakesink", "testsink");
  if (!sink->kid) {
    GST_ERROR_OBJECT (sink, "Failed to create fakesink");
    return FALSE;
  }
  gst_bin_add (GST_BIN (sink), sink->kid);

  targetpad = gst_element_get_static_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);

  g_free (sink->gconf_str);
  sink->gconf_str = NULL;

  return TRUE;
}

static void
gst_gconf_video_sink_init (GstGConfVideoSink * sink,
    GstGConfVideoSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_gconf_video_sink_reset (sink);

  sink->client = gconf_client_get_default ();
  gconf_client_add_dir (sink->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  sink->notify_id = gconf_client_notify_add (sink->client,
      GST_GCONF_DIR "/" GST_GCONF_VIDEOSINK_KEY,
      cb_toggle_element, sink, NULL, NULL);
}

static void
gst_gconf_video_sink_dispose (GObject * object)
{
  GstGConfVideoSink *sink = GST_GCONF_VIDEO_SINK (object);

  if (sink->client) {
    if (sink->notify_id != 0)
      gconf_client_notify_remove (sink->client, sink->notify_id);

    g_object_unref (G_OBJECT (sink->client));
    sink->client = NULL;
  }
  g_free (sink->gconf_str);
  sink->gconf_str = NULL;

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_gconf_video_sink_finalize (GstGConfVideoSink * sink)
{
  g_free (sink->gconf_str);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (sink)));
}

static gboolean
do_toggle_element (GstGConfVideoSink * sink)
{
  GstPad *targetpad;
  gchar *new_gconf_str;
  GstState cur, next;

  new_gconf_str = gst_gconf_get_string (GST_GCONF_VIDEOSINK_KEY);
  if (new_gconf_str != NULL && sink->gconf_str != NULL &&
      (strlen (new_gconf_str) == 0 ||
          strcmp (sink->gconf_str, new_gconf_str) == 0)) {
    g_free (new_gconf_str);
    GST_DEBUG_OBJECT (sink, "GConf key was updated, but it didn't change");
    return TRUE;
  }

  /* Sometime, it would be lovely to allow sink changes even when
   * already running, but this involves sending an appropriate new-segment
   * and possibly prerolling etc */
  GST_OBJECT_LOCK (sink);
  cur = GST_STATE (sink);
  next = GST_STATE_PENDING (sink);
  GST_OBJECT_UNLOCK (sink);

  if (cur > GST_STATE_READY || next == GST_STATE_PAUSED) {
    GST_DEBUG_OBJECT (sink,
        "Auto-sink is already running. Ignoring GConf change");
    g_free (new_gconf_str);
    return TRUE;
  }

  GST_DEBUG_OBJECT (sink, "GConf key changed: '%s' to '%s'",
      GST_STR_NULL (sink->gconf_str), GST_STR_NULL (new_gconf_str));

  g_free (sink->gconf_str);
  sink->gconf_str = new_gconf_str;

  /* kill old element */
  if (sink->kid) {
    GST_DEBUG_OBJECT (sink, "Removing old kid");
    gst_element_set_state (sink->kid, GST_STATE_NULL);
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
  targetpad = gst_element_get_static_pad (sink->kid, "sink");
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

static GstStateChangeReturn
gst_gconf_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfVideoSink *sink = GST_GCONF_VIDEO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_toggle_element (sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_gconf_video_sink_reset (sink))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}
