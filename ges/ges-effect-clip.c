/* GStreamer Editing Services
 * Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
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
 * SECTION: ges-effect-clip
 * @short_description: An effect created by parse-launch style bin descriptions
 * in a #GESTimelineLayer
 *
 * Should be used mainly for testing purposes.
 *
 * The effect will be applied on the sources that have lower priorities
 * (higher number) between the inpoint and the end of it.
 *
 * In a #GESSimpleTimelineLayer, the priorities will be set for you but if
 * you use another type of #GESTimelineLayer, you will have to handle it
 * yourself.
 */

#include <ges/ges.h>
#include "ges-internal.h"
#include "ges-types.h"

G_DEFINE_TYPE (GESEffectClip, ges_effect_clip, GES_TYPE_BASE_EFFECT_CLIP);

struct _GESEffectClipPrivate
{
  gchar *video_bin_description;
  gchar *audio_bin_description;
};

enum
{
  PROP_0,
  PROP_VIDEO_BIN_DESCRIPTION,
  PROP_AUDIO_BIN_DESCRIPTION,
};

static void ges_effect_clip_finalize (GObject * object);
static GESTrackElement
    * ges_tl_parse_launch_effect_create_track_obj (GESClip * self,
    GESTrackType type);

static void
ges_effect_clip_finalize (GObject * object)
{
  GESEffectClipPrivate *priv = GES_EFFECT_CLIP (object)->priv;

  g_free (priv->audio_bin_description);
  g_free (priv->video_bin_description);

  G_OBJECT_CLASS (ges_effect_clip_parent_class)->finalize (object);
}

static void
ges_effect_clip_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESEffectClipPrivate *priv = GES_EFFECT_CLIP (object)->priv;

  switch (property_id) {
    case PROP_VIDEO_BIN_DESCRIPTION:
      g_value_set_string (value, priv->video_bin_description);
      break;
    case PROP_AUDIO_BIN_DESCRIPTION:
      g_value_set_string (value, priv->audio_bin_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_effect_clip_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESEffectClip *self = GES_EFFECT_CLIP (object);

  switch (property_id) {
    case PROP_VIDEO_BIN_DESCRIPTION:
      self->priv->video_bin_description = g_value_dup_string (value);
      break;
    case PROP_AUDIO_BIN_DESCRIPTION:
      self->priv->audio_bin_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_effect_clip_class_init (GESEffectClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESEffectClipPrivate));

  object_class->get_property = ges_effect_clip_get_property;
  object_class->set_property = ges_effect_clip_set_property;
  object_class->finalize = ges_effect_clip_finalize;

  /**
   * GESEffectClip:video-bin-description:
   *
   * The description of the video track of the effect bin with a gst-launch-style
   * pipeline description. This should be used for test purposes.
   *
   * Example: "videobalance saturation=1.5 hue=+0.5"
   */
  g_object_class_install_property (object_class, PROP_VIDEO_BIN_DESCRIPTION,
      g_param_spec_string ("video-bin-description",
          "Video bin description",
          "Description of the video track of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GESEffectClip:audio-bin-description:
   *
   * The description of the audio track of the effect bin with a gst-launch-style
   * pipeline description. This should be used for test purposes.
   *
   * Example: "audiopanorama panorama=1.0"
   */
  g_object_class_install_property (object_class, PROP_AUDIO_BIN_DESCRIPTION,
      g_param_spec_string ("audio-bin-description",
          "bin description",
          "Bin description of the audio track of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  timobj_class->create_track_element =
      ges_tl_parse_launch_effect_create_track_obj;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_effect_clip_init (GESEffectClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_EFFECT_CLIP, GESEffectClipPrivate);

}

static GESTrackElement *
ges_tl_parse_launch_effect_create_track_obj (GESClip * self, GESTrackType type)
{
  const gchar *bin_description = NULL;
  GESEffectClip *effect = GES_EFFECT_CLIP (self);

  if (type == GES_TRACK_TYPE_VIDEO) {
    bin_description = effect->priv->video_bin_description;
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    bin_description = effect->priv->audio_bin_description;
  }

  if (bin_description) {
    /* FIXME Work with a GESAsset here! */
    return g_object_new (GES_TYPE_EFFECT, "bin-description",
        bin_description, "track-type", type, NULL);
  }

  GST_WARNING ("Effect doesn't handle this track type");
  return NULL;
}

/**
 * ges_effect_clip_new:
 * @video_bin_description: The gst-launch like bin description of the effect
 * @audio_bin_description: The gst-launch like bin description of the effect
 *
 * Creates a new #GESEffectClip from the description of the bin.
 *
 * Returns: a newly created #GESEffectClip, or
 * %NULL if something went wrong.
 *
 * Since: 0.10.2
 */
GESEffectClip *
ges_effect_clip_new (const gchar * video_bin_description,
    const gchar * audio_bin_description)
{
  return g_object_new (GES_TYPE_EFFECT_CLIP,
      "video-bin-description", video_bin_description,
      "audio-bin-description", audio_bin_description, NULL);
}
