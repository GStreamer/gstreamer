/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2012 Volodymyr Rudyi <vladimir.rudoy@gmail.com>
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


#ifndef GES_ASSET_TIMELINE_OBJECT_H
#define GES_ASSET_TIMELINE_OBJECT_H

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>

G_BEGIN_DECLS

#define GES_TYPE_ASSET_TIMELINE_OBJECT (ges_asset_timeline_object_get_type ())
#define GES_ASSET_TIMELINE_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_ASSET_TIMELINE_OBJECT, GESAssetTimelineObject))
#define GES_ASSET_TIMELINE_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_ASSET_TIMELINE_OBJECT, GESAssetTimelineObjectClass))
#define GES_IS_ASSET_TIMELINE_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_ASSET_TIMELINE_OBJECT))
#define GES_IS_ASSET_TIMELINE_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_ASSET_TIMELINE_OBJECT))
#define GES_ASSET_TIMELINE_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_ASSET_TIMELINE_OBJECT, GESAssetTimelineObjectClass))

typedef struct _GESAssetTimelineObjectPrivate GESAssetTimelineObjectPrivate;

struct _GESAssetTimelineObject
{
  GESAsset parent;

  /* <private> */
  GESAssetTimelineObjectPrivate *priv;

  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAssetTimelineObjectClass
{
  GESAssetClass parent;

  gpointer _ges_reserved[GES_PADDING];
};

GType ges_asset_timeline_object_get_type (void);
void ges_asset_timeline_object_set_supported_formats         (GESAssetTimelineObject *self,
                                                              GESTrackType supportedformats);
GESTrackType ges_asset_timeline_object_get_supported_formats (GESAssetTimelineObject *self);

G_END_DECLS
#endif /* _GES_ASSET_TIMELINE_OBJECT_H */
