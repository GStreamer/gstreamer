/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
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
#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_ELEMENT_ASSET ges_track_element_asset_get_type()
GES_DECLARE_TYPE(TrackElementAsset, track_element_asset, TRACK_ELEMENT_ASSET);

struct _GESTrackElementAsset
{
  GESAsset parent;

  /* <private> */
  GESTrackElementAssetPrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESTrackElementAssetClass
{
  GESAssetClass parent_class;

  /**
   * GESTrackElementAssetClass::get_natural_framerate:
   * @self: A #GESTrackElementAsset
   * @framerate_n: The framerate numerator to retrieve
   * @framerate_d: The framerate denominator to retrieve
   *
   * Returns: %TRUE if @self has a natural framerate @FALSE otherwise.
   *
   * Since: 1.18
   */
  gboolean (*get_natural_framerate)        (GESTrackElementAsset *self, gint *framerate_n, gint *framerate_d);

  gpointer _ges_reserved[GES_PADDING - 1];
};

GES_API
const GESTrackType ges_track_element_asset_get_track_type (GESTrackElementAsset *asset);
GES_API
void ges_track_element_asset_set_track_type               (GESTrackElementAsset * asset, GESTrackType type);
GES_API
gboolean ges_track_element_asset_get_natural_framerate    (GESTrackElementAsset *self,
                                                           gint *framerate_n,
                                                           gint *framerate_d);

G_END_DECLS
