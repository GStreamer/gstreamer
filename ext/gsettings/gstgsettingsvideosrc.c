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
 * SECTION:element-gsettingsvideosrc
 *
 * This element outputs sound to the videosrc that has been configured in
 * GSettings by the user.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch gsettingsvideosrc ! videoconvert ! videoscale ! autovideosink
 * ]| Play from configured videosrc
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/glib-compat-private.h>
#include <string.h>

#include "gstgsettingsvideosrc.h"
#include "gstgsettings.h"

GST_BOILERPLATE (GstGSettingsVideoSrc, gst_gsettings_video_src, GstSwitchSrc,
    GST_TYPE_SWITCH_SRC);

static gboolean
gst_gsettings_video_src_change_child (GstGSettingsVideoSrc * src)
{
  gchar *new_string;
  GError *err = NULL;
  GstElement *new_kid;

  GST_OBJECT_LOCK (src);
  new_string =
      g_settings_get_string (src->settings, GST_GSETTINGS_KEY_VIDEOSRC);

  if (new_string != NULL && src->gsettings_str != NULL &&
      (strlen (new_string) == 0 ||
          strcmp (src->gsettings_str, new_string) == 0)) {
    g_free (new_string);
    GST_DEBUG_OBJECT (src,
        "GSettings key was updated, but it didn't change. Ignoring");
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }
  GST_OBJECT_UNLOCK (src);

  GST_DEBUG_OBJECT (src, "GSettings key changed from '%s' to '%s'",
      GST_STR_NULL (src->gsettings_str), GST_STR_NULL (new_string));

  if (new_string) {
    new_kid = gst_parse_bin_from_description (new_string, TRUE, &err);
    if (err) {
      GST_ERROR_OBJECT (src, "error creating bin '%s': %s", new_string,
          err->message);
      g_error_free (err);
    }
  } else {
    new_kid = NULL;
  }

  if (new_kid == NULL) {
    GST_ELEMENT_ERROR (src, LIBRARY, SETTINGS, (NULL),
        ("Failed to render video src from GSettings"));
    goto fail;
  }

  if (!gst_switch_src_set_child (GST_SWITCH_SRC (src), new_kid)) {
    GST_WARNING_OBJECT (src, "Failed to update child element");
    goto fail;
  }

  g_free (src->gsettings_str);
  src->gsettings_str = new_string;

  return TRUE;

fail:
  g_free (new_string);
  return FALSE;
}

static void
on_changed (GSettings * settings, gchar * key, GstGSettingsVideoSrc * src)
{
  if (!g_str_equal (key, "videosrc"))
    return;

  gst_gsettings_video_src_change_child (src);
}

static gboolean
gst_gsettings_video_src_start (GstGSettingsVideoSrc * src)
{
  GError *err = NULL;
  GThread *thread;

  src->loop = g_main_loop_new (src->context, FALSE);

  thread =
      g_thread_create ((GThreadFunc) g_main_loop_run, src->loop, FALSE, &err);
  if (!thread) {
    GST_ELEMENT_ERROR (src, CORE, STATE_CHANGE, (NULL),
        ("Failed to create new thread: %s", err->message));
    g_error_free (err);
    g_main_loop_unref (src->loop);
    src->loop = NULL;
    return FALSE;
  }

  g_main_context_push_thread_default (src->context);
  src->settings = g_settings_new (GST_GSETTINGS_SCHEMA);
  src->changed_id =
      g_signal_connect_data (G_OBJECT (src->settings), "changed",
      G_CALLBACK (on_changed), gst_object_ref (src),
      (GClosureNotify) gst_object_unref, 0);
  g_main_context_pop_thread_default (src->context);

  return TRUE;
}

static gboolean
gst_gsettings_video_src_reset (GstGSettingsVideoSrc * src)
{
  gst_switch_src_set_child (GST_SWITCH_SRC (src), NULL);

  if (src->changed_id) {
    g_signal_handler_disconnect (src->settings, src->changed_id);
    src->changed_id = 0;
  }

  if (src->loop) {
    g_main_loop_quit (src->loop);
    g_main_loop_unref (src->loop);
    src->loop = NULL;
  }

  if (src->settings) {
    g_object_unref (src->settings);
    src->settings = NULL;
  }

  GST_OBJECT_LOCK (src);
  g_free (src->gsettings_str);
  src->gsettings_str = NULL;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static void
gst_gsettings_video_src_finalize (GObject * object)
{
  GstGSettingsVideoSrc *src = GST_GSETTINGS_VIDEO_SRC (object);

  g_free (src->gsettings_str);
  g_main_context_unref (src->context);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (src)));
}

static GstStateChangeReturn
gst_gsettings_video_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGSettingsVideoSrc *src = GST_GSETTINGS_VIDEO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gsettings_video_src_start (src))
        return GST_STATE_CHANGE_FAILURE;

      if (!gst_gsettings_video_src_change_child (src)) {
        gst_gsettings_video_src_reset (src);
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
      gst_gsettings_video_src_reset (src);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_gsettings_video_src_init (GstGSettingsVideoSrc * src,
    GstGSettingsVideoSrcClass * g_class)
{
  src->context = g_main_context_new ();
  gst_gsettings_video_src_reset (src);
}

static void
gst_gsettings_video_src_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (eklass, "GSettings video src",
      "Src/Video",
      "Video src embedding the GSettings preferences for video input",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_gsettings_video_src_class_init (GstGSettingsVideoSrcClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->finalize = gst_gsettings_video_src_finalize;

  eklass->change_state = gst_gsettings_video_src_change_state;
}
