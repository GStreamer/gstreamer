/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-gsettingsvideosink
 *
 * This element outputs sound to the videosink that has been configured in
 * GSettings by the user.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! videoconvert ! videoscale ! gsettingsvideosink
 * ]| Play on configured videosink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/glib-compat-private.h>
#include <string.h>

#include "gstgsettingsvideosink.h"
#include "gstgsettings.h"

GST_BOILERPLATE (GstGSettingsVideoSink, gst_gsettings_video_sink, GstSwitchSink,
    GST_TYPE_SWITCH_SINK);

static gboolean
gst_gsettings_video_sink_change_child (GstGSettingsVideoSink * sink)
{
  gchar *new_string;
  GError *err = NULL;
  GstElement *new_kid;

  GST_OBJECT_LOCK (sink);
  new_string =
      g_settings_get_string (sink->settings, GST_GSETTINGS_KEY_VIDEOSINK);

  if (new_string != NULL && sink->gsettings_str != NULL &&
      (strlen (new_string) == 0 ||
          strcmp (sink->gsettings_str, new_string) == 0)) {
    g_free (new_string);
    GST_DEBUG_OBJECT (sink,
        "GSettings key was updated, but it didn't change. Ignoring");
    GST_OBJECT_UNLOCK (sink);
    return TRUE;
  }
  GST_OBJECT_UNLOCK (sink);

  GST_DEBUG_OBJECT (sink, "GSettings key changed from '%s' to '%s'",
      GST_STR_NULL (sink->gsettings_str), GST_STR_NULL (new_string));

  if (new_string) {
    new_kid = gst_parse_bin_from_description (new_string, TRUE, &err);
    if (err) {
      GST_ERROR_OBJECT (sink, "error creating bin '%s': %s", new_string,
          err->message);
      g_error_free (err);
    }
  } else {
    new_kid = NULL;
  }

  if (new_kid == NULL) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render video sink from GSettings"));
    goto fail;
  }

  if (!gst_switch_sink_set_child (GST_SWITCH_SINK (sink), new_kid)) {
    GST_WARNING_OBJECT (sink, "Failed to update child element");
    goto fail;
  }

  g_free (sink->gsettings_str);
  sink->gsettings_str = new_string;

  return TRUE;

fail:
  g_free (new_string);
  return FALSE;
}

static void
on_changed (GSettings * settings, gchar * key, GstGSettingsVideoSink * sink)
{
  if (!g_str_has_suffix (key, "videosink"))
    return;

  gst_gsettings_video_sink_change_child (sink);
}

static gboolean
gst_gsettings_video_sink_start (GstGSettingsVideoSink * sink)
{
  GError *err = NULL;
  GThread *thread;

  sink->loop = g_main_loop_new (sink->context, FALSE);

  thread =
      g_thread_create ((GThreadFunc) g_main_loop_run, sink->loop, FALSE, &err);
  if (!thread) {
    GST_ELEMENT_ERROR (sink, CORE, STATE_CHANGE, (NULL),
        ("Failed to create new thread: %s", err->message));
    g_error_free (err);
    g_main_loop_unref (sink->loop);
    sink->loop = NULL;
    return FALSE;
  }

  g_main_context_push_thread_default (sink->context);
  sink->settings = g_settings_new (GST_GSETTINGS_SCHEMA);
  sink->changed_id =
      g_signal_connect_data (G_OBJECT (sink->settings), "changed",
      G_CALLBACK (on_changed), gst_object_ref (sink),
      (GClosureNotify) gst_object_unref, 0);
  g_main_context_pop_thread_default (sink->context);

  return TRUE;
}

static gboolean
gst_gsettings_video_sink_reset (GstGSettingsVideoSink * sink)
{
  gst_switch_sink_set_child (GST_SWITCH_SINK (sink), NULL);

  if (sink->changed_id) {
    g_signal_handler_disconnect (sink->settings, sink->changed_id);
    sink->changed_id = 0;
  }

  if (sink->loop) {
    g_main_loop_quit (sink->loop);
    g_main_loop_unref (sink->loop);
    sink->loop = NULL;
  }

  if (sink->settings) {
    g_object_unref (sink->settings);
    sink->settings = NULL;
  }

  GST_OBJECT_LOCK (sink);
  g_free (sink->gsettings_str);
  sink->gsettings_str = NULL;
  GST_OBJECT_UNLOCK (sink);

  return TRUE;
}

static void
gst_gsettings_video_sink_finalize (GObject * object)
{
  GstGSettingsVideoSink *sink = GST_GSETTINGS_VIDEO_SINK (object);

  g_free (sink->gsettings_str);
  g_main_context_unref (sink->context);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (sink)));
}

static GstStateChangeReturn
gst_gsettings_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGSettingsVideoSink *sink = GST_GSETTINGS_VIDEO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gsettings_video_sink_start (sink))
        return GST_STATE_CHANGE_FAILURE;

      if (!gst_gsettings_video_sink_change_child (sink)) {
        gst_gsettings_video_sink_reset (sink);
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
      gst_gsettings_video_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_gsettings_video_sink_init (GstGSettingsVideoSink * sink,
    GstGSettingsVideoSinkClass * g_class)
{
  sink->context = g_main_context_new ();
  gst_gsettings_video_sink_reset (sink);
}

static void
gst_gsettings_video_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (eklass, "GSettings video sink",
      "Sink/Video",
      "Video sink embedding the GSettings preferences for video input",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_gsettings_video_sink_class_init (GstGSettingsVideoSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->finalize = gst_gsettings_video_sink_finalize;

  eklass->change_state = gst_gsettings_video_sink_change_state;
}
