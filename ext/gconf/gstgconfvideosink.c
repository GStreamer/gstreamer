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

GST_BOILERPLATE (GstGConfVideoSink, gst_gconf_video_sink, GstSwitchSink,
    GST_TYPE_SWITCH_SINK);

static void
gst_gconf_video_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

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

static void
gst_gconf_video_sink_reset (GstGConfVideoSink * sink)
{
  gst_switch_sink_set_child (GST_SWITCH_SINK (sink), NULL);

  g_free (sink->gconf_str);
  sink->gconf_str = NULL;
}

static void
gst_gconf_video_sink_init (GstGConfVideoSink * sink,
    GstGConfVideoSinkClass * g_class)
{
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

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_gconf_video_sink_finalize (GstGConfVideoSink * sink)
{
  g_free (sink->gconf_str);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (sink)));
}

static gboolean
do_change_child (GstGConfVideoSink * sink)
{
  gchar *new_gconf_str;
  GstElement *new_kid;

  new_gconf_str = gst_gconf_get_string (GST_GCONF_VIDEOSINK_KEY);

  GST_LOG_OBJECT (sink, "old gconf string: %s", GST_STR_NULL (sink->gconf_str));
  GST_LOG_OBJECT (sink, "new gconf string: %s", GST_STR_NULL (new_gconf_str));

  if (new_gconf_str != NULL && sink->gconf_str != NULL &&
      (strlen (new_gconf_str) == 0 ||
          strcmp (sink->gconf_str, new_gconf_str) == 0)) {
    g_free (new_gconf_str);
    GST_DEBUG_OBJECT (sink,
        "GConf key was updated, but it didn't change. Ignoring");
    return TRUE;
  }

  GST_DEBUG_OBJECT (sink, "GConf key changed: '%s' to '%s'",
      GST_STR_NULL (sink->gconf_str), GST_STR_NULL (new_gconf_str));

  GST_DEBUG_OBJECT (sink, "Creating new kid");
  if (!(new_kid = gst_gconf_get_default_video_sink ())) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render video sink from GConf"));
    return FALSE;
  }

  if (!gst_switch_sink_set_child (GST_SWITCH_SINK (sink), new_kid)) {
    GST_WARNING_OBJECT (sink, "Failed to update child element");
    goto fail;
  }

  g_free (sink->gconf_str);
  sink->gconf_str = new_gconf_str;

  GST_DEBUG_OBJECT (sink, "done changing gconf video sink");

  return TRUE;

fail:
  g_free (new_gconf_str);
  return FALSE;
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_change_child (GST_GCONF_VIDEO_SINK (data));
}

static GstStateChangeReturn
gst_gconf_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfVideoSink *sink = GST_GCONF_VIDEO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_change_child (sink)) {
        gst_gconf_video_sink_reset (sink);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_gconf_video_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}
