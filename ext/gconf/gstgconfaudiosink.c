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
/**
 * SECTION:element-gconfaudiosink
 *
 * This element outputs sound to the audiosink that has been configured in
 * GConf by the user.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=foo.ogg ! decodebin ! audioconvert ! audioresample ! gconfaudiosink
 * ]| Play on configured audiosink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstgconfelements.h"
#include "gstgconfaudiosink.h"

static void gst_gconf_audio_sink_dispose (GObject * object);
static void gst_gconf_audio_sink_finalize (GstGConfAudioSink * sink);
static void cb_change_child (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);
static GstStateChangeReturn
gst_gconf_audio_sink_change_state (GstElement * element,
    GstStateChange transition);
static void gst_gconf_switch_profile (GstGConfAudioSink * sink,
    GstGConfProfile profile);

enum
{
  PROP_0,
  PROP_PROFILE
};

GST_BOILERPLATE (GstGConfAudioSink, gst_gconf_audio_sink, GstSwitchSink,
    GST_TYPE_SWITCH_SINK);

static void gst_gconf_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gconf_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_gconf_audio_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (eklass, "GConf audio sink",
      "Sink/Audio",
      "Audio sink embedding the GConf-settings for audio output",
      "Jan Schmidt <thaytan@mad.scientist.com>");
}

#define GST_TYPE_GCONF_PROFILE (gst_gconf_profile_get_type())
static GType
gst_gconf_profile_get_type (void)
{
  static GType gconf_profile_type = 0;
  static const GEnumValue gconf_profiles[] = {
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
  oklass->finalize = (GObjectFinalizeFunc) gst_gconf_audio_sink_finalize;
  eklass->change_state = gst_gconf_audio_sink_change_state;

  g_object_class_install_property (oklass, PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile", "Profile",
          GST_TYPE_GCONF_PROFILE, GCONF_PROFILE_SOUNDS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gconf_audio_sink_reset (GstGConfAudioSink * sink)
{
  gst_switch_sink_set_child (GST_SWITCH_SINK (sink), NULL);

  g_free (sink->gconf_str);
  sink->gconf_str = NULL;
}

static void
gst_gconf_audio_sink_init (GstGConfAudioSink * sink,
    GstGConfAudioSinkClass * g_class)
{
  gst_gconf_audio_sink_reset (sink);

  sink->client = gconf_client_get_default ();
  gconf_client_add_dir (sink->client, GST_GCONF_DIR "/default",
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);

  gst_gconf_switch_profile (sink, GCONF_PROFILE_SOUNDS);
}

static void
gst_gconf_audio_sink_dispose (GObject * object)
{
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (object);

  if (sink->client) {
    gst_gconf_switch_profile (sink, GCONF_PROFILE_NONE);
    g_object_unref (G_OBJECT (sink->client));
    sink->client = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_gconf_audio_sink_finalize (GstGConfAudioSink * sink)
{
  g_free (sink->gconf_str);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, ((GObject *) (sink)));
}

static gboolean
do_change_child (GstGConfAudioSink * sink)
{
  const gchar *key;
  gchar *new_gconf_str;
  GstElement *new_kid;

  if (sink->profile == GCONF_PROFILE_NONE)
    return FALSE;               /* Can't switch to a 'NONE' sink */

  key = gst_gconf_get_key_for_sink_profile (sink->profile);
  new_gconf_str = gst_gconf_get_string (key);

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

  GST_DEBUG_OBJECT (sink, "Creating new child for profile %d", sink->profile);
  new_kid =
      gst_gconf_render_bin_with_default (new_gconf_str, DEFAULT_AUDIOSINK);

  if (new_kid == NULL) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio sink from GConf"));
    goto fail;
  }

  if (!gst_switch_sink_set_child (GST_SWITCH_SINK (sink), new_kid)) {
    GST_WARNING_OBJECT (sink, "Failed to update child element");
    goto fail;
  }

  g_free (sink->gconf_str);
  sink->gconf_str = new_gconf_str;

  GST_DEBUG_OBJECT (sink, "done changing gconf audio sink");

  return TRUE;

fail:
  g_free (new_gconf_str);
  return FALSE;
}

static void
gst_gconf_switch_profile (GstGConfAudioSink * sink, GstGConfProfile profile)
{
  if (sink->client == NULL)
    return;

  if (sink->notify_id) {
    GST_DEBUG_OBJECT (sink, "Unsubscribing old key %s for profile %d",
        gst_gconf_get_key_for_sink_profile (sink->profile), sink->profile);
    gconf_client_notify_remove (sink->client, sink->notify_id);
    sink->notify_id = 0;
  }

  sink->profile = profile;
  if (profile != GCONF_PROFILE_NONE) {
    const gchar *key = gst_gconf_get_key_for_sink_profile (sink->profile);

    GST_DEBUG_OBJECT (sink, "Subscribing to key %s for profile %d",
        key, profile);
    sink->notify_id = gconf_client_notify_add (sink->client, key,
        cb_change_child, sink, NULL, NULL);
  }
}

static void
gst_gconf_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGConfAudioSink *sink;

  sink = GST_GCONF_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_PROFILE:
      gst_gconf_switch_profile (sink, g_value_get_enum (value));
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
cb_change_child (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  do_change_child (GST_GCONF_AUDIO_SINK (data));
}

static GstStateChangeReturn
gst_gconf_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_change_child (sink)) {
        gst_gconf_audio_sink_reset (sink);
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
      gst_gconf_audio_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}
