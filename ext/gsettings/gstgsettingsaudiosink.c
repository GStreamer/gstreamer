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
 * SECTION:element-gsettingsaudiosink
 *
 * This element outputs sound to the audiosink that has been configured in
 * GSettings by the user.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc ! audioconvert ! audioresample ! gsettingsaudiosink
 * ]| Play on configured audiosink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/glib-compat-private.h>
#include <string.h>

#include "gstgsettingsaudiosink.h"
#include "gstgsettings.h"

#define GST_TYPE_GSETTINGS_AUDIOSINK_PROFILE (gst_gsettings_audiosink_profile_get_type())
static GType
gst_gsettings_audiosink_profile_get_type (void)
{
  static GType gsettings_profile_type = 0;
  static const GEnumValue gsettings_profiles[] = {
    {GST_GSETTINGS_AUDIOSINK_PROFILE_SOUNDS, "Sound Events", "sounds"},
    {GST_GSETTINGS_AUDIOSINK_PROFILE_MUSIC, "Music and Movies (default)",
        "music"},
    {GST_GSETTINGS_AUDIOSINK_PROFILE_CHAT, "Audio/Video Conferencing", "chat"},
    {0, NULL, NULL}
  };

  if (!gsettings_profile_type) {
    gsettings_profile_type =
        g_enum_register_static ("GstGSettingsAudioSinkProfile",
        gsettings_profiles);
  }
  return gsettings_profile_type;
}

enum
{
  PROP_0,
  PROP_PROFILE
};

GST_BOILERPLATE (GstGSettingsAudioSink, gst_gsettings_audio_sink, GstSwitchSink,
    GST_TYPE_SWITCH_SINK);

static gboolean
gst_gsettings_audio_sink_change_child (GstGSettingsAudioSink * sink)
{
  const gchar *key = NULL;
  gchar *new_string;
  GError *err = NULL;
  GstElement *new_kid;

  GST_OBJECT_LOCK (sink);
  switch (sink->profile) {
    case GST_GSETTINGS_AUDIOSINK_PROFILE_SOUNDS:
      key = GST_GSETTINGS_KEY_SOUNDS_AUDIOSINK;
      break;
    case GST_GSETTINGS_AUDIOSINK_PROFILE_MUSIC:
      key = GST_GSETTINGS_KEY_MUSIC_AUDIOSINK;
      break;
    case GST_GSETTINGS_AUDIOSINK_PROFILE_CHAT:
      key = GST_GSETTINGS_KEY_CHAT_AUDIOSINK;
      break;
    default:
      break;
  }

  new_string = g_settings_get_string (sink->settings, key);

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
        ("Failed to render audio sink from GSettings"));
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

static gboolean
gst_gsettings_audio_sink_switch_profile (GstGSettingsAudioSink * sink,
    GstGSettingsAudioSinkProfile profile)
{
  if (sink->settings == NULL)
    return TRUE;

  GST_OBJECT_LOCK (sink);
  sink->profile = profile;
  GST_OBJECT_UNLOCK (sink);

  return gst_gsettings_audio_sink_change_child (sink);
}

static void
on_changed (GSettings * settings, gchar * key, GstGSettingsAudioSink * sink)
{
  gboolean changed = FALSE;

  if (!g_str_has_suffix (key, "audiosink"))
    return;

  GST_OBJECT_LOCK (sink);
  if ((sink->profile == GST_GSETTINGS_AUDIOSINK_PROFILE_SOUNDS &&
          g_str_equal (key, GST_GSETTINGS_KEY_SOUNDS_AUDIOSINK)) ||
      (sink->profile == GST_GSETTINGS_AUDIOSINK_PROFILE_MUSIC &&
          g_str_equal (key, GST_GSETTINGS_KEY_MUSIC_AUDIOSINK)) ||
      (sink->profile == GST_GSETTINGS_AUDIOSINK_PROFILE_CHAT &&
          g_str_equal (key, GST_GSETTINGS_KEY_CHAT_AUDIOSINK)))
    changed = TRUE;
  GST_OBJECT_UNLOCK (sink);

  if (changed)
    gst_gsettings_audio_sink_change_child (sink);
}

static gboolean
gst_gsettings_audio_sink_start (GstGSettingsAudioSink * sink)
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
gst_gsettings_audio_sink_reset (GstGSettingsAudioSink * sink)
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
gst_gsettings_audio_sink_finalize (GObject * object)
{
  GstGSettingsAudioSink *sink = GST_GSETTINGS_AUDIO_SINK (object);

  g_free (sink->gsettings_str);
  g_main_context_unref (sink->context);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (sink)));
}

static void
gst_gsettings_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGSettingsAudioSink *sink = GST_GSETTINGS_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_PROFILE:
      gst_gsettings_audio_sink_switch_profile (sink, g_value_get_enum (value));
      break;
    default:
      break;
  }
}

static void
gst_gsettings_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGSettingsAudioSink *sink = GST_GSETTINGS_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_value_set_enum (value, sink->profile);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gsettings_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGSettingsAudioSink *sink = GST_GSETTINGS_AUDIO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gsettings_audio_sink_start (sink))
        return GST_STATE_CHANGE_FAILURE;

      if (!gst_gsettings_audio_sink_change_child (sink)) {
        gst_gsettings_audio_sink_reset (sink);
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
      gst_gsettings_audio_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_gsettings_audio_sink_init (GstGSettingsAudioSink * sink,
    GstGSettingsAudioSinkClass * g_class)
{
  sink->context = g_main_context_new ();
  gst_gsettings_audio_sink_reset (sink);

  sink->profile = GST_GSETTINGS_AUDIOSINK_PROFILE_MUSIC;
}

static void
gst_gsettings_audio_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (eklass, "GSettings audio sink",
      "Sink/Audio",
      "Audio sink embedding the GSettings preferences for audio output",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_gsettings_audio_sink_class_init (GstGSettingsAudioSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->finalize = gst_gsettings_audio_sink_finalize;
  oklass->set_property = gst_gsettings_audio_sink_set_property;
  oklass->get_property = gst_gsettings_audio_sink_get_property;

  g_object_class_install_property (oklass, PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile", "Profile",
          GST_TYPE_GSETTINGS_AUDIOSINK_PROFILE,
          GST_GSETTINGS_AUDIOSINK_PROFILE_SOUNDS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  eklass->change_state = gst_gsettings_audio_sink_change_state;
}
