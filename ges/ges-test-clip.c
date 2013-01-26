/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:ges-test-clip
 * @short_description: Render video and audio test patterns in a
 * #GESTimelineLayer
 *
 * Useful for testing purposes.
 *
 * You can use the ges_asset_request_simple API to create a Asset
 * capable of extractinf GESTestClip-s
 */

#include "ges-internal.h"
#include "ges-test-clip.h"
#include "ges-source-clip.h"
#include "ges-track-element.h"
#include "ges-video-test-source.h"
#include "ges-audio-test-source.h"
#include <string.h>

G_DEFINE_TYPE (GESTestClip, ges_test_clip, GES_TYPE_SOURCE_CLIP);

struct _GESTestClipPrivate
{
  gboolean mute;
  GESVideoTestPattern vpattern;
  gdouble freq;
  gdouble volume;
};

enum
{
  PROP_0,
  PROP_MUTE,
  PROP_VPATTERN,
  PROP_FREQ,
  PROP_VOLUME,
};

static GESTrackElement
    * ges_test_clip_create_track_element (GESClip * obj, GESTrackType type);

static void
ges_test_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTestClipPrivate *priv = GES_TEST_CLIP (object)->priv;

  switch (property_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, priv->mute);
      break;
    case PROP_VPATTERN:
      g_value_set_enum (value, priv->vpattern);
      break;
    case PROP_FREQ:
      g_value_set_double (value, priv->freq);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, priv->volume);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_test_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTestClip *uriclip = GES_TEST_CLIP (object);

  switch (property_id) {
    case PROP_MUTE:
      ges_test_clip_set_mute (uriclip, g_value_get_boolean (value));
      break;
    case PROP_VPATTERN:
      ges_test_clip_set_vpattern (uriclip, g_value_get_enum (value));
      break;
    case PROP_FREQ:
      ges_test_clip_set_frequency (uriclip, g_value_get_double (value));
      break;
    case PROP_VOLUME:
      ges_test_clip_set_volume (uriclip, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_test_clip_class_init (GESTestClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTestClipPrivate));

  object_class->get_property = ges_test_clip_get_property;
  object_class->set_property = ges_test_clip_set_property;

  /**
   * GESTestClip:vpattern:
   *
   * Video pattern to display in video track objects.
   */
  g_object_class_install_property (object_class, PROP_VPATTERN,
      g_param_spec_enum ("vpattern", "VPattern",
          "Which video pattern to display. See videotestsrc element",
          GES_VIDEO_TEST_PATTERN_TYPE,
          GES_VIDEO_TEST_PATTERN_BLACK, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTestClip:freq:
   *
   * The frequency to generate for audio track objects.
   */
  g_object_class_install_property (object_class, PROP_FREQ,
      g_param_spec_double ("freq", "Audio Frequency",
          "The frequency to generate. See audiotestsrc element",
          0, 20000, 440, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTestClip:volume:
   *
   * The volume for the audio track objects.
   */
  g_object_class_install_property (object_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Audio Volume",
          "The volume of the test audio signal.",
          0, 1, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


  /**
   * GESTestClip:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  timobj_class->create_track_element = ges_test_clip_create_track_element;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_test_clip_init (GESTestClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TEST_CLIP, GESTestClipPrivate);

  self->priv->freq = 0;
  self->priv->volume = 0;
  GES_TIMELINE_ELEMENT (self)->duration = 0;
}

/**
 * ges_test_clip_set_mute:
 * @self: the #GESTestClip on which to mute or unmute the audio track
 * @mute: %TRUE to mute the audio track, %FALSE to unmute it
 *
 * Sets whether the audio track of this timeline object is muted or not.
 *
 */
void
ges_test_clip_set_mute (GESTestClip * self, gboolean mute)
{
  GList *tmp, *trackelements;
  GESClip *object = (GESClip *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->priv->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_AUDIO)
      ges_track_element_set_active (trackelement, !mute);

    g_object_unref (GES_TRACK_ELEMENT (tmp->data));
  }
  g_list_free (trackelements);
}

/**
 * ges_test_clip_set_vpattern:
 * @self: the #GESTestClip to set the pattern on
 * @vpattern: the #GESVideoTestPattern to use on @self
 *
 * Sets which video pattern to display on @self.
 *
 */
void
ges_test_clip_set_vpattern (GESTestClip * self, GESVideoTestPattern vpattern)
{
  GList *tmp, *trackelements;
  GESClip *object = (GESClip *) self;

  self->priv->vpattern = vpattern;

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_VIDEO_TEST_SOURCE (trackelement))
      ges_video_test_source_set_pattern (
          (GESVideoTestSource *) trackelement, vpattern);

    g_object_unref (GES_TRACK_ELEMENT (tmp->data));
  }
  g_list_free (trackelements);
}

/**
 * ges_test_clip_set_frequency:
 * @self: the #GESTestClip to set the frequency on
 * @freq: the frequency you want to use on @self
 *
 * Sets the frequency to generate. See audiotestsrc element.
 *
 */
void
ges_test_clip_set_frequency (GESTestClip * self, gdouble freq)
{
  GList *tmp, *trackelements;
  GESClip *object = (GESClip *) self;

  self->priv->freq = freq;

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_AUDIO_TEST_SOURCE (trackelement))
      ges_audio_test_source_set_freq (
          (GESAudioTestSource *) trackelement, freq);

    g_object_unref (GES_TRACK_ELEMENT (tmp->data));
  }
  g_list_free (trackelements);
}

/**
 * ges_test_clip_set_volume:
 * @self: the #GESTestClip to set the volume on
 * @volume: the volume of the audio signal you want to use on @self
 *
 * Sets the volume of the test audio signal.
 *
 */
void
ges_test_clip_set_volume (GESTestClip * self, gdouble volume)
{
  GList *tmp, *trackelements;
  GESClip *object = (GESClip *) self;

  self->priv->volume = volume;

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_AUDIO_TEST_SOURCE (trackelement))
      ges_audio_test_source_set_volume (
          (GESAudioTestSource *) trackelement, volume);

    g_object_unref (GES_TRACK_ELEMENT (tmp->data));
  }
  g_list_free (trackelements);
}

/**
 * ges_test_clip_get_vpattern:
 * @self: a #GESTestClip
 *
 * Get the #GESVideoTestPattern which is applied on @self.
 *
 * Returns: The #GESVideoTestPattern which is applied on @self.
 */
GESVideoTestPattern
ges_test_clip_get_vpattern (GESTestClip * self)
{
  return self->priv->vpattern;
}

/**
 * ges_test_clip_is_muted:
 * @self: a #GESTestClip
 *
 * Let you know if the audio track of @self is muted or not.
 *
 * Returns: Whether the audio track of @self is muted or not.
 */
gboolean
ges_test_clip_is_muted (GESTestClip * self)
{
  return self->priv->mute;
}

/**
 * ges_test_clip_get_frequency:
 * @self: a #GESTestClip
 *
 * Get the frequency @self generates.
 *
 * Returns: The frequency @self generates. See audiotestsrc element.
 */
gdouble
ges_test_clip_get_frequency (GESTestClip * self)
{
  return self->priv->freq;
}

/**
 * ges_test_clip_get_volume:
 * @self: a #GESTestClip
 *
 * Get the volume of the test audio signal applied on @self.
 *
 * Returns: The volume of the test audio signal applied on @self.
 */
gdouble
ges_test_clip_get_volume (GESTestClip * self)
{
  return self->priv->volume;
}

static GESTrackElement *
ges_test_clip_create_track_element (GESClip * obj, GESTrackType type)
{
  GESTestClipPrivate *priv = GES_TEST_CLIP (obj)->priv;
  GESTrackElement *res = NULL;

  GST_DEBUG ("Creating a GESTrackTestSource for type: %s",
      ges_track_type_name (type));

  if (type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackElement *) ges_video_test_source_new ();
    ges_video_test_source_set_pattern (
        (GESVideoTestSource *) res, priv->vpattern);
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    res = (GESTrackElement *) ges_audio_test_source_new ();

    if (priv->mute)
      ges_track_element_set_active (res, FALSE);

    ges_audio_test_source_set_freq ((GESAudioTestSource *) res, priv->freq);
    ges_audio_test_source_set_volume ((GESAudioTestSource *) res, priv->volume);
  }

  return res;
}

/**
 * ges_test_clip_new:
 *
 * Creates a new #GESTestClip.
 *
 * Returns: The newly created #GESTestClip, or NULL if there was an
 * error.
 */
GESTestClip *
ges_test_clip_new (void)
{
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TEST_CLIP, NULL);
}

/**
 * ges_test_clip_new_for_nick:
 * @nick: the nickname for which to create the #GESTestClip
 *
 * Creates a new #GESTestClip for the provided @nick.
 *
 * Returns: The newly created #GESTestClip, or NULL if there was an
 * error.
 */
GESTestClip *
ges_test_clip_new_for_nick (gchar * nick)
{
  GEnumValue *value;
  GEnumClass *klass;
  GESTestClip *ret = NULL;

  klass = G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_TEST_PATTERN_TYPE));
  if (!klass)
    return NULL;

  value = g_enum_get_value_by_nick (klass, nick);
  if (value) {
    ret = g_object_new (GES_TYPE_TEST_CLIP, "vpattern",
        (gint) value->value, NULL);
  }

  g_type_class_unref (klass);
  return ret;
}
