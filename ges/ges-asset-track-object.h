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
#ifndef _GES_ASSET_TRACK_ELEMENT_
#define _GES_ASSET_TRACK_ELEMENT_

#include <glib-object.h>
#include <gio/gio.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>

G_BEGIN_DECLS

#define GES_TYPE_ASSET_TRACK_ELEMENT ges_asset_track_element_get_type()
#define GES_ASSET_TRACK_ELEMENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_ASSET_TRACK_ELEMENT, GESAssetTrackElement))
#define GES_ASSET_TRACK_ELEMENT_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_ASSET_TRACK_ELEMENT, GESAssetTrackElementClass))
#define GES_IS_ASSET_TRACK_ELEMENT(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_ASSET_TRACK_ELEMENT))
#define GES_IS_ASSET_TRACK_ELEMENT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_ASSET_TRACK_ELEMENT))
#define GES_ASSET_TRACK_ELEMENT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_ASSET_TRACK_ELEMENT, GESAssetTrackElementClass))

typedef struct _GESAssetTrackElementPrivate GESAssetTrackElementPrivate;

GType ges_asset_track_element_get_type (void);

struct _GESAssetTrackElement
{
  GESAsset parent;

  /* <private> */
  GESAssetTrackElementPrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESAssetTrackElementClass
{
  GESAssetClass parent_class;

  gpointer _ges_reserved[GES_PADDING];
};

const GESTrackType ges_asset_track_element_get_track_type (GESAssetTrackElement *asset);
void ges_asset_track_element_set_track_type               (GESAssetTrackElement * asset, GESTrackType type);

G_END_DECLS
#endif /* _GES_ASSET_TRACK_ELEMENT */

