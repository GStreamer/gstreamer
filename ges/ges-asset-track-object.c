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

/**
 * SECTION: ges-asset-track-object
 * @short_description: A GESAsset subclass specialized in GESTrackObject extraction
 */

#include "ges-asset-track-object.h"

enum
{
  PROP_0,
  PROP_TRACK_TYPE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

G_DEFINE_TYPE (GESAssetTrackObject, ges_asset_track_object, GES_TYPE_ASSET);

struct _GESAssetTrackObjectPrivate
{
  GESTrackType type;
};

static void
_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESAssetTrackObject *asset = GES_ASSET_TRACK_OBJECT (object);

  switch (property_id) {
    case PROP_TRACK_TYPE:
      g_value_set_flags (value, asset->priv->type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESAssetTrackObject *asset = GES_ASSET_TRACK_OBJECT (object);

  switch (property_id) {
    case PROP_TRACK_TYPE:
      asset->priv->type = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_track_object_class_init (GESAssetTrackObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESAssetTrackObjectPrivate));

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

  /**
   * GESClip:track-type:
   *
   * The formats supported by the object.
   */
  properties[PROP_TRACK_TYPE] = g_param_spec_flags ("track-type",
      "Track type", "The GESTrackType in which the object is",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_UNKNOWN,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_TRACK_TYPE,
      properties[PROP_TRACK_TYPE]);
}

static void
ges_asset_track_object_init (GESAssetTrackObject * self)
{
  GESAssetTrackObjectPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_ASSET_TRACK_OBJECT, GESAssetTrackObjectPrivate);

  priv->type = GES_TRACK_TYPE_UNKNOWN;

}

/**
 * ges_asset_track_object_set_track_type:
 * @asset: A #GESAssetObject
 * @type: A #GESTrackType
 *
 * Set the #GESAssetTrackType the #GESTrackObject extracted from @self
 * should get into
 */
void
ges_asset_track_object_set_track_type (GESAssetTrackObject * asset,
    GESTrackType type)
{
  g_return_if_fail (GES_IS_ASSET_TRACK_OBJECT (asset));

  asset->priv->type = type;
}

/**
 * ges_asset_track_object_get_track_type:
 * @asset: A #GESAssetObject
 *
 * Get the GESAssetTrackType the #GESTrackObject extracted from @self
 * should get into
 *
 * Returns: a #GESTrackType
 */
const GESTrackType
ges_asset_track_object_get_track_type (GESAssetTrackObject * asset)
{
  g_return_val_if_fail (GES_IS_ASSET_TRACK_OBJECT (asset),
      GES_TRACK_TYPE_UNKNOWN);

  return asset->priv->type;
}
