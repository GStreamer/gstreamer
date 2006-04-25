/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Jürg Billeter <j@bitron.ch>
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

#include <string.h>

#include "gstgconfelements.h"
#include "gstgconfaudiosink.h"

static void gst_gconf_audio_sink_dispose (GObject * object);
static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstStateChangeReturn
gst_gconf_audio_sink_change_state (GstElement * element,
    GstStateChange transition);

enum
{
  PROP_0,
  PROP_PROFILE
};

GST_BOILERPLATE (GstGConfAudioSink, gst_gconf_audio_sink, GstBin, GST_TYPE_BIN);

static void gst_gconf_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gconf_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_gconf_audio_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  static const GstElementDetails gst_gconf_audio_sink_details =
      GST_ELEMENT_DETAILS ("GConf audio sink",
      "Sink/Audio",
      "Audio sink embedding the GConf-settings for audio output",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_gconf_audio_sink_details);
}

#define GST_TYPE_GCONF_PROFILE (gst_gconf_profile_get_type())
static GType
gst_gconf_profile_get_type (void)
{
  static GType gconf_profile_type = 0;
  static GEnumValue gconf_profiles[] = {
    {GCONF_PROFILE_SOUNDS, "Sound Events", "sounds"},
    {GCONF_PROFILE_MUSIC, "Music and Movies", "music"},
    {GCONF_PROFILE_CHAT, "Audio/Video Conferencing", "chat"},
    {0, NULL, NULL}
  };

  if (!gconf_profile_type) {
    gconf_profile_type =
        g_enum_register_static ("GstGConfProfile", gconf_profiles);
  }
  return gconf_profile_type;
}

static void
gst_gconf_audio_sink_class_init (GstGConfAudioSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->set_property = gst_gconf_audio_sink_set_property;
  oklass->get_property = gst_gconf_audio_sink_get_property;
  oklass->dispose = gst_gconf_audio_sink_dispose;
  eklass->change_state = gst_gconf_audio_sink_change_state;

  g_object_class_install_property (oklass, PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile", "Profile",
          GST_TYPE_GCONF_PROFILE, GCONF_PROFILE_SOUNDS, G_PARAM_READWRITE));
}

/*
 * Hack to make negotiation work.
 */

static void
gst_gconf_audio_sink_reset (GstGConfAudioSink * sink)
{
  GstPad *targetpad;

  /* fakesink */
  if (sink->kid) {
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
  }
  sink->kid = gst_element_factory_make ("fakesink", "testsink");
  gst_bin_add (GST_BIN (sink), sink->kid);

  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);

  g_free (sink->gconf_str);
  sink->gconf_str = NULL;

  if (sink->connection) {
    gconf_client_notify_remove (sink->client, sink->connection);
    sink->connection = 0;
  }
}

static const gchar *
get_gconf_key_for_profile (int profile)
{
  switch (profile) {
    case GCONF_PROFILE_SOUNDS:
      return GST_GCONF_DIR "/default/audiosink";
    case GCONF_PROFILE_MUSIC:
      return GST_GCONF_DIR "/default/musicaudiosink";
    case GCONF_PROFILE_CHAT:
      return GST_GCONF_DIR "/default/chataudiosink";
    default:
      g_return_val_if_reached (NULL);
  }
}

static void
gst_gconf_audio_sink_init (GstGConfAudioSink * sink,
    GstGConfAudioSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_gconf_audio_sink_reset (sink);

  sink->client = gconf_client_get_default ();
  gconf_client_add_dir (sink->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);

  sink->profile = GCONF_PROFILE_SOUNDS;
  sink->connection = gconf_client_notify_add (sink->client,
      get_gconf_key_for_profile (sink->profile), cb_toggle_element,
      sink, NULL, NULL);
}

static void
gst_gconf_audio_sink_dispose (GObject * object)
{
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (object);

  if (sink->client) {
    if (sink->connection) {
      gconf_client_notify_remove (sink->client, sink->connection);
      sink->connection = 0;
    }

    g_object_unref (G_OBJECT (sink->client));
    sink->client = NULL;
  }

  g_free (sink->gconf_str);
  sink->gconf_str = NULL;

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
do_toggle_element (GstGConfAudioSink * sink)
{
  GstPad *targetpad;
  gchar *new_gconf_str;
  GstState cur, next;

  new_gconf_str = gst_gconf_get_string (GST_GCONF_AUDIOSINK_KEY);
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
  if (!(sink->kid = gst_gconf_get_default_audio_sink (sink->profile))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio sink from GConf"));
    g_free (sink->gconf_str);
    sink->gconf_str = NULL;
    return FALSE;
  }
  gst_element_set_state (sink->kid, GST_STATE (sink));
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (sink, "Creating new ghostpad");
  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (sink, "done changing gconf audio sink");

  return TRUE;
}

static void
gst_gconf_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGConfAudioSink *sink;

  g_return_if_fail (GST_IS_GCONF_AUDIO_SINK (object));

  sink = GST_GCONF_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_PROFILE:
      sink->profile = g_value_get_enum (value);
      if (sink->connection) {
        gconf_client_notify_remove (sink->client, sink->connection);
      }
      sink->connection = gconf_client_notify_add (sink->client,
          get_gconf_key_for_profile (sink->profile), cb_toggle_element,
          sink, NULL, NULL);
      break;
    default:
      break;
  }
}

static void
gst_gconf_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGConfAudioSink *sink;

  g_return_if_fail (GST_IS_GCONF_AUDIO_SINK (object));

  sink = GST_GCONF_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_value_set_enum (value, sink->profile);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_toggle_element (GST_GCONF_AUDIO_SINK (data));
}

static GstStateChangeReturn
gst_gconf_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (element);

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
      gst_gconf_audio_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}
