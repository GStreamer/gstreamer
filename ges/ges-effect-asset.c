/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

/* SECTION: geseffectasset
 * @short_description: A GESAsset subclass specialized in GESEffect extraction
 *
 * This is internal, and implementation details, so we are not showing it in the
 * documentation
 */

#include "ges-effect-asset.h"
#include "ges-track-element.h"
#include "ges-internal.h"

G_DEFINE_TYPE (GESEffectAsset, ges_effect_asset, GES_TYPE_TRACK_ELEMENT_ASSET);

struct _GESEffectAssetPrivate
{
  GESTrackType track_type;
};

/* GESAsset virtual methods implementation */
static void
_fill_track_type (GESAsset * asset)
{
  GList *tmp;
  GstElement *effect = gst_parse_bin_from_description (ges_asset_get_id (asset),
      TRUE, NULL);

  if (effect == NULL)
    return;

  for (tmp = GST_BIN_CHILDREN (effect); tmp; tmp = tmp->next) {
    GstElementFactory *factory =
        gst_element_get_factory (GST_ELEMENT (tmp->data));
    const gchar *klass =
        gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);

    if (g_strrstr (klass, "Effect")) {
      if (g_strrstr (klass, "Audio")) {
        GES_EFFECT_ASSET (asset)->priv->track_type = GES_TRACK_TYPE_AUDIO;
        break;
      } else if (g_strrstr (klass, "Video")) {
        GES_EFFECT_ASSET (asset)->priv->track_type = GES_TRACK_TYPE_VIDEO;
        break;
      }
    }
  }

  gst_object_unref (effect);
  return;
}

static GESExtractable *
_extract (GESAsset * asset, GError ** error)
{
  GESExtractable *effect;

  if (GES_EFFECT_ASSET (asset)->priv->track_type == GES_TRACK_TYPE_UNKNOWN)
    _fill_track_type (asset);

  effect = GES_ASSET_CLASS (ges_effect_asset_parent_class)->extract (asset,
      error);

  if (effect == NULL || (error && *error)) {
    effect = NULL;

    return NULL;
  }

  ges_track_element_set_track_type (GES_TRACK_ELEMENT (effect),
      GES_EFFECT_ASSET (asset)->priv->track_type);

  return effect;
}

static void
ges_effect_asset_init (GESEffectAsset * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_EFFECT_ASSET, GESEffectAssetPrivate);

  self->priv->track_type = GES_TRACK_TYPE_UNKNOWN;
}

static void
ges_effect_asset_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_effect_asset_parent_class)->finalize (object);
}

static void
ges_effect_asset_class_init (GESEffectAssetClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESAssetClass *asset_class = GES_ASSET_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESEffectAssetPrivate));

  object_class->finalize = ges_effect_asset_finalize;
  asset_class->extract = _extract;
}
