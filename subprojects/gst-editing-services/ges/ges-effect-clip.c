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
 * SECTION: geseffectclip
 * @title: GESEffectClip
 * @short_description: An effect created by parse-launch style bin descriptions
 * in a GESLayer
 *
 * The effect will be applied on the sources that have lower priorities
 * (higher number) between the inpoint and the end of it.
 *
 * The asset ID of an effect clip is in the form:
 *
 * ```
 *   "audio ! bin ! description || video ! bin ! description"
 * ```
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ges/ges.h>
#include "ges-internal.h"
#include "ges-types.h"

struct _GESEffectClipPrivate
{
  gchar *video_bin_description;
  gchar *audio_bin_description;
};

static void ges_extractable_interface_init (GESExtractableInterface * iface);
G_DEFINE_TYPE_WITH_CODE (GESEffectClip, ges_effect_clip,
    GES_TYPE_BASE_EFFECT_CLIP, G_ADD_PRIVATE (GESEffectClip)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

enum
{
  PROP_0,
  PROP_VIDEO_BIN_DESCRIPTION,
  PROP_AUDIO_BIN_DESCRIPTION,
};

static void ges_effect_clip_finalize (GObject * object);
static GESTrackElement *_create_track_element (GESClip * self,
    GESTrackType type);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS;       /* Start ignoring GParameter deprecation */
static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  gchar *bin_desc;
  GESTrackType ttype;
  GParameter *params = g_new0 (GParameter, 2);
  gchar **effects_desc = g_strsplit (id, "||", -1);
  gint i;

  *n_params = 0;

  if (g_strv_length (effects_desc) > 2)
    GST_ERROR ("EffectClip id %s contains too many effect descriptions", id);

  for (i = 0; effects_desc[i] && i < 2; i++) {
    bin_desc =
        ges_effect_asset_id_get_type_and_bindesc (effects_desc[i], &ttype,
        NULL);

    if (ttype == GES_TRACK_TYPE_AUDIO) {
      *n_params = *n_params + 1;
      params[*n_params - 1].name = "audio-bin-description";
    } else if (ttype == GES_TRACK_TYPE_VIDEO) {
      *n_params = *n_params + 1;
      params[i].name = "video-bin-description";
    } else {
      g_free (bin_desc);
      GST_ERROR ("Could not find effect type for %s", effects_desc[i]);
      continue;
    }

    g_value_init (&params[*n_params - 1].value, G_TYPE_STRING);
    g_value_set_string (&params[*n_params - 1].value, bin_desc);
    g_free (bin_desc);
  }

  g_strfreev (effects_desc);
  return params;
}

G_GNUC_END_IGNORE_DEPRECATIONS; /* End ignoring GParameter deprecation */

static gchar *
extractable_check_id (GType type, const gchar * id, GError ** error)
{
  return g_strdup (id);
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  GString *id = g_string_new (NULL);
  GESEffectClipPrivate *priv = GES_EFFECT_CLIP (self)->priv;

  if (priv->audio_bin_description)
    g_string_append_printf (id, "audio %s ||", priv->audio_bin_description);
  if (priv->video_bin_description)
    g_string_append_printf (id, "video %s", priv->video_bin_description);

  return g_string_free (id, FALSE);
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_ASSET;
  iface->check_id = extractable_check_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->get_id = extractable_get_id;
}


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

  timobj_class->create_track_element = _create_track_element;
}

static void
ges_effect_clip_init (GESEffectClip * self)
{
  self->priv = ges_effect_clip_get_instance_private (self);
}

static GESTrackElement *
_create_track_element (GESClip * self, GESTrackType type)
{
  const gchar *bin_description = NULL;
  GESEffectClip *effect = GES_EFFECT_CLIP (self);

  if (type == GES_TRACK_TYPE_VIDEO) {
    bin_description = effect->priv->video_bin_description;
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    bin_description = effect->priv->audio_bin_description;
  }

  if (bin_description)
    return GES_TRACK_ELEMENT (ges_effect_new (bin_description));

  GST_WARNING ("Effect doesn't handle this track type");
  return NULL;
}

/**
 * ges_effect_clip_new:
 * @video_bin_description: (nullable): The gst-launch like bin description of the effect
 * @audio_bin_description: (nullable): The gst-launch like bin description of the effect
 *
 * Creates a new #GESEffectClip from the description of the bin.
 *
 * Returns: (transfer floating) (nullable): a newly created #GESEffectClip, or
 * %NULL if something went wrong.
 */
GESEffectClip *
ges_effect_clip_new (const gchar * video_bin_description,
    const gchar * audio_bin_description)
{
  GESAsset *asset;
  GESEffectClip *res;
  GString *id = g_string_new (NULL);

  if (audio_bin_description)
    g_string_append_printf (id, "audio %s ||", audio_bin_description);
  if (video_bin_description)
    g_string_append_printf (id, "video %s", video_bin_description);

  asset = ges_asset_request (GES_TYPE_EFFECT_CLIP, id->str, NULL);
  res = GES_EFFECT_CLIP (ges_asset_extract (asset, NULL));
  g_string_free (id, TRUE);
  gst_object_unref (asset);

  return res;
}
