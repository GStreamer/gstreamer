/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 * Copyright (C) 2020 Igalia S.L
 *     Author: 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:gestestclip
 * @title: GESTestClip
 * @short_description: Render video and audio test patterns in a GESLayer
 *
 * Useful for testing purposes.
 *
 * ## Asset
 *
 * The default asset ID is GESTestClip, but the framerate and video
 * size can be overridden using an ID of the form:
 *
 * ```
 * framerate=60/1, width=1920, height=1080, max-duration=5.0
 * ```
 * Note: `max-duration` can be provided in seconds as float, or as GstClockTime
 * as guint64 or gint.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-test-clip.h"
#include "ges-source-clip.h"
#include "ges-track-element.h"
#include "ges-video-test-source.h"
#include "ges-audio-test-source.h"
#include <string.h>

#define DEFAULT_VOLUME 1.0
#define DEFAULT_VPATTERN GES_VIDEO_TEST_PATTERN_SMPTE

G_DECLARE_FINAL_TYPE (GESTestClipAsset, ges_test_clip_asset, GES,
    TEST_CLIP_ASSET, GESSourceClipAsset);

struct _GESTestClipAsset
{
  GESSourceClipAsset parent;

  gint natural_framerate_n;
  gint natural_framerate_d;
  gint natural_width;
  gint natural_height;
  GstClockTime max_duration;
};

#define GES_TYPE_TEST_CLIP_ASSET (ges_test_clip_asset_get_type())
G_DEFINE_TYPE (GESTestClipAsset, ges_test_clip_asset,
    GES_TYPE_SOURCE_CLIP_ASSET);

static gboolean
_get_natural_framerate (GESClipAsset * asset, gint * framerate_n,
    gint * framerate_d)
{
  GESTestClipAsset *self = GES_TEST_CLIP_ASSET (asset);

  *framerate_n = self->natural_framerate_n;
  *framerate_d = self->natural_framerate_d;
  return TRUE;
}

static GstClockTime
ges_test_clip_asset_get_max_duration (GESAsset * asset)
{
  GESTestClipAsset *self = GES_TEST_CLIP_ASSET (asset);

  return GES_TEST_CLIP_ASSET (self)->max_duration;
}

gboolean
ges_test_clip_asset_get_natural_size (GESAsset * asset, gint * width,
    gint * height)
{
  GESTestClipAsset *self = GES_TEST_CLIP_ASSET (asset);

  *width = self->natural_width;
  *height = self->natural_height;

  return TRUE;
}

static void
ges_test_clip_asset_constructed (GObject * gobject)
{
  GESFrameNumber fmax_dur = GES_FRAME_NUMBER_NONE;
  GESTestClipAsset *self = GES_TEST_CLIP_ASSET (gobject);
  GstStructure *structure =
      gst_structure_from_string (ges_asset_get_id (GES_ASSET (self)), NULL);

  g_assert (structure);

  gst_structure_get_int (structure, "width", &self->natural_width);
  gst_structure_get_int (structure, "height", &self->natural_height);
  gst_structure_get_fraction (structure, "framerate",
      &self->natural_framerate_n, &self->natural_framerate_d);
  ges_util_structure_get_clocktime (structure, "max-duration",
      &self->max_duration, &fmax_dur);
  if (GES_FRAME_NUMBER_IS_VALID (fmax_dur))
    self->max_duration =
        gst_util_uint64_scale (fmax_dur, self->natural_framerate_d * GST_SECOND,
        self->natural_framerate_n);
  gst_structure_free (structure);

  G_OBJECT_CLASS (ges_test_clip_asset_parent_class)->constructed (gobject);
}

static void
ges_test_clip_asset_class_init (GESTestClipAssetClass * klass)
{
  GESClipAssetClass *clip_asset_class = GES_CLIP_ASSET_CLASS (klass);

  clip_asset_class->get_natural_framerate = _get_natural_framerate;
  G_OBJECT_CLASS (klass)->constructed = ges_test_clip_asset_constructed;
}

static void
ges_test_clip_asset_init (GESTestClipAsset * self)
{
  self->natural_width = DEFAULT_WIDTH;
  self->natural_height = DEFAULT_HEIGHT;
  self->natural_framerate_n = DEFAULT_FRAMERATE_N;
  self->natural_framerate_d = DEFAULT_FRAMERATE_D;
  self->max_duration = GST_CLOCK_TIME_NONE;
}

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

typedef struct
{
  const gchar *name;
  GType type;
} ValidField;

gchar *
ges_test_source_asset_check_id (GType type, const gchar * id, GError ** error)
{
  if (id && g_strcmp0 (id, g_type_name (type))) {
    gchar *res = NULL;
    GstStructure *structure = gst_structure_from_string (id, NULL);

    if (!structure) {
      gchar *struct_str = g_strdup_printf ("%s,%s", g_type_name (type), id);

      structure = gst_structure_from_string (struct_str, NULL);
      g_free (struct_str);
    }

    GST_INFO ("Test source ID: %" GST_PTR_FORMAT, structure);
    if (!structure) {
      g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
          "GESTestClipAsset ID should be in the form: `framerate=30/1, "
          "width=1920, height=1080, got %s", id);
    } else {
      static ValidField valid_fields[] = {
        {"width", G_TYPE_INT},
        {"height", G_TYPE_INT},
        {"framerate", G_TYPE_NONE},     /* GST_TYPE_FRACTION is not constant */
        {"max-duration", GST_TYPE_CLOCK_TIME},
        {"disable-timecodestamper", G_TYPE_BOOLEAN},
      };
      gint i;

      for (i = 0; i < G_N_ELEMENTS (valid_fields); i++) {
        if (gst_structure_has_field (structure, valid_fields[i].name)) {
          GstClockTime ts;
          GESFrameNumber fn;
          ValidField field = valid_fields[i];
          GType type =
              field.type == G_TYPE_NONE ? GST_TYPE_FRACTION : field.type;

          if (!(gst_structure_has_field_typed (structure, field.name,
                      type) ||
                  (type == GST_TYPE_CLOCK_TIME &&
                      ges_util_structure_get_clocktime (structure, field.name,
                          &ts, &fn)))) {

            g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
                "Field %s has wrong type, %s, expected %s", field.name,
                g_type_name (gst_structure_get_field_type (structure,
                        field.name)), g_type_name (type));

            gst_structure_free (structure);

            return FALSE;
          }
        }
      }
      res = gst_structure_to_string (structure);
      gst_structure_free (structure);
    }

    return res;
  }

  return g_strdup (g_type_name (type));
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_TEST_CLIP_ASSET;
  iface->check_id = ges_test_source_asset_check_id;
}

G_DEFINE_TYPE_WITH_CODE (GESTestClip, ges_test_clip, GES_TYPE_SOURCE_CLIP,
    G_ADD_PRIVATE (GESTestClip)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));


static GESTrackElement
    * ges_test_clip_create_track_element (GESClip * clip, GESTrackType type);

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
  GESClipClass *clip_class = GES_CLIP_CLASS (klass);

  object_class->get_property = ges_test_clip_get_property;
  object_class->set_property = ges_test_clip_set_property;

  /**
   * GESTestClip:vpattern:
   *
   * Video pattern to display in video track elements.
   */
  g_object_class_install_property (object_class, PROP_VPATTERN,
      g_param_spec_enum ("vpattern", "VPattern",
          "Which video pattern to display. See videotestsrc element",
          GES_VIDEO_TEST_PATTERN_TYPE,
          DEFAULT_VPATTERN, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTestClip:freq:
   *
   * The frequency to generate for audio track elements.
   */
  g_object_class_install_property (object_class, PROP_FREQ,
      g_param_spec_double ("freq", "Audio Frequency",
          "The frequency to generate. See audiotestsrc element",
          0, 20000, 440, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTestClip:volume:
   *
   * The volume for the audio track elements.
   */
  g_object_class_install_property (object_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Audio Volume",
          "The volume of the test audio signal.",
          0, 1, DEFAULT_VOLUME, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


  /**
   * GESTestClip:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  clip_class->create_track_element = ges_test_clip_create_track_element;
}

static void
ges_test_clip_init (GESTestClip * self)
{
  SUPRESS_UNUSED_WARNING (GES_IS_TEST_CLIP_ASSET);
  self->priv = ges_test_clip_get_instance_private (self);

  self->priv->freq = 0;
  self->priv->volume = 0;
  GES_TIMELINE_ELEMENT (self)->duration = 0;
}

/**
 * ges_test_clip_set_mute:
 * @self: the #GESTestClip on which to mute or unmute the audio track
 * @mute: %TRUE to mute the audio track, %FALSE to unmute it
 *
 * Sets whether the audio track of this clip is muted or not.
 *
 */
void
ges_test_clip_set_mute (GESTestClip * self, gboolean mute)
{
  GList *tmp;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->priv->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_AUDIO)
      ges_track_element_set_active (trackelement, !mute);
  }
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
  GList *tmp;

  self->priv->vpattern = vpattern;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_VIDEO_TEST_SOURCE (trackelement))
      ges_video_test_source_set_pattern (
          (GESVideoTestSource *) trackelement, vpattern);
  }
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
  GList *tmp;

  self->priv->freq = freq;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_AUDIO_TEST_SOURCE (trackelement))
      ges_audio_test_source_set_freq (
          (GESAudioTestSource *) trackelement, freq);
  }
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
  GList *tmp;

  self->priv->volume = volume;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;
    if (GES_IS_AUDIO_TEST_SOURCE (trackelement))
      ges_audio_test_source_set_volume (
          (GESAudioTestSource *) trackelement, volume);
  }
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
ges_test_clip_create_track_element (GESClip * clip, GESTrackType type)
{
  GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (clip));
  GESTestClipPrivate *priv = GES_TEST_CLIP (clip)->priv;
  GESTrackElement *res = NULL;

  GST_DEBUG ("Creating a GESTrackTestSource for type: %s",
      ges_track_type_name (type));

  if (type == GES_TRACK_TYPE_VIDEO) {
    gchar *id = NULL;
    GESAsset *videoasset;

    if (asset) {
      GstStructure *structure =
          gst_structure_from_string (ges_asset_get_id (asset), NULL);

      if (structure) {
        id = gst_structure_to_string (structure);
        gst_structure_free (structure);
      }
    }
    /* Our asset ID has been verified and thus this should not fail ever */
    videoasset = ges_asset_request (GES_TYPE_VIDEO_TEST_SOURCE, id, NULL);
    g_assert (videoasset);
    g_free (id);

    res = (GESTrackElement *) ges_asset_extract (videoasset, NULL);
    gst_object_unref (videoasset);
    ges_video_test_source_set_pattern (
        (GESVideoTestSource *) res, priv->vpattern);
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    res = (GESTrackElement *) ges_audio_test_source_new ();

    if (priv->mute)
      ges_track_element_set_active (res, FALSE);

    ges_audio_test_source_set_freq ((GESAudioTestSource *) res, priv->freq);
    ges_audio_test_source_set_volume ((GESAudioTestSource *) res, priv->volume);
  }

  if (asset)
    ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (res),
        ges_test_clip_asset_get_max_duration (asset));

  return res;
}

/**
 * ges_test_clip_new:
 *
 * Creates a new #GESTestClip.
 *
 * Returns: (transfer floating) (nullable): The newly created #GESTestClip,
 * or %NULL if there was an error.
 */
GESTestClip *
ges_test_clip_new (void)
{
  GESTestClip *new_clip;
  GESAsset *asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  new_clip = GES_TEST_CLIP (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return new_clip;
}

/**
 * ges_test_clip_new_for_nick:
 * @nick: the nickname for which to create the #GESTestClip
 *
 * Creates a new #GESTestClip for the provided @nick.
 *
 * Returns: (transfer floating) (nullable): The newly created #GESTestClip,
 * or %NULL if there was an error.
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
    ret = ges_test_clip_new ();
    ges_test_clip_set_vpattern (ret, value->value);
  }

  g_type_class_unref (klass);
  return ret;
}
