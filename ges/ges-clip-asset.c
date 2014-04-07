/* Gstreamer Editing Services
 *
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION: gesclipasset
 * @short_description: A GESAsset subclass specialized in GESClip extraction
 *
 * The #GESUriClipAsset is a special #GESAsset specilized in #GESClip.
 * it is mostly used to get information about the #GESTrackType-s the objects extracted
 * from it can potentialy create #GESTrackElement for.
 */

#include "ges-clip-asset.h"

G_DEFINE_TYPE (GESClipAsset, ges_clip_asset, GES_TYPE_ASSET);
#define GES_CLIP_ASSET_GET_PRIVATE(o)\
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_CLIP_ASSET, \
   GESClipAssetPrivate))

#define parent_class ges_clip_asset_parent_class

struct _GESClipAssetPrivate
{
  GESTrackType supportedformats;
};


enum
{
  PROP_0,
  PROP_SUPPORTED_FORMATS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

/***********************************************
 *                                             *
 *      GObject vmetods implemenation          *
 *                                             *
 ***********************************************/

static void
_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESClipAssetPrivate *priv = GES_CLIP_ASSET (object)->priv;
  switch (property_id) {
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value, priv->supportedformats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESClipAssetPrivate *priv = GES_CLIP_ASSET (object)->priv;

  switch (property_id) {
    case PROP_SUPPORTED_FORMATS:
      priv->supportedformats = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_clip_asset_init (GESClipAsset * self)
{
  self->priv = GES_CLIP_ASSET_GET_PRIVATE (self);
}

static void
_constructed (GObject * object)
{
  GType extractable_type = ges_asset_get_extractable_type (GES_ASSET (object));
  GObjectClass *class = g_type_class_ref (extractable_type);
  GParamSpecFlags *pspec;

  pspec = G_PARAM_SPEC_FLAGS (g_object_class_find_property (class,
          "supported-formats"));

  GES_CLIP_ASSET (object)->priv->supportedformats = pspec->default_value;
  g_type_class_unref (class);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
ges_clip_asset_class_init (GESClipAssetClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  g_type_class_add_private (self_class, sizeof (GESClipAssetPrivate));
  object_class->constructed = _constructed;
  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

  /**
   * GESClipAsset:supported-formats:
   *
   * The formats supported by the asset.
   */
  properties[PROP_SUPPORTED_FORMATS] = g_param_spec_flags ("supported-formats",
      "Supported formats", "Formats supported by the file",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      properties[PROP_SUPPORTED_FORMATS]);
}

/***********************************************
 *                                             *
 *              Public methods                 *
 *                                             *
 ***********************************************/
/**
 * ges_clip_asset_set_supported_formats:
 * @self: a #GESClipAsset
 * @supportedformats: The track types supported by the GESClipAsset
 *
 * Sets track types for which objects extracted from @self can create #GESTrackElement
 */
void
ges_clip_asset_set_supported_formats (GESClipAsset * self,
    GESTrackType supportedformats)
{
  g_return_if_fail (GES_IS_CLIP_ASSET (self));

  self->priv->supportedformats = supportedformats;
}

/**
 * ges_clip_asset_get_supported_formats:
 * @self: a #GESClipAsset
 *
 * Gets track types for which objects extracted from @self can create #GESTrackElement
 *
 * Returns: The track types on which @self will create TrackElement when added to
 * a layer
 */
GESTrackType
ges_clip_asset_get_supported_formats (GESClipAsset * self)
{
  g_return_val_if_fail (GES_IS_CLIP_ASSET (self), GES_TRACK_TYPE_UNKNOWN);

  return self->priv->supportedformats;
}
