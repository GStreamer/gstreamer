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


#ifndef GES_ASSET_CLIP_H
#define GES_ASSET_CLIP_H

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>

G_BEGIN_DECLS

#define GES_TYPE_ASSET_CLIP (ges_asset_clip_get_type ())
#define GES_ASSET_CLIP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_ASSET_CLIP, GESAssetClip))
#define GES_ASSET_CLIP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_ASSET_CLIP, GESAssetClipClass))
#define GES_IS_ASSET_CLIP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_ASSET_CLIP))
#define GES_IS_ASSET_CLIP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_ASSET_CLIP))
#define GES_ASSET_CLIP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_ASSET_CLIP, GESAssetClipClass))

typedef struct _GESAssetClipPrivate GESAssetClipPrivate;

struct _GESAssetClip
{
  GESAsset parent;

  /* <private> */
  GESAssetClipPrivate *priv;

  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAssetClipClass
{
  GESAssetClass parent;

  gpointer _ges_reserved[GES_PADDING];
};

GType ges_asset_clip_get_type (void);
void ges_asset_clip_set_supported_formats         (GESAssetClip *self,
                                                              GESTrackType supportedformats);
GESTrackType ges_asset_clip_get_supported_formats (GESAssetClip *self);

G_END_DECLS
#endif /* _GES_ASSET_CLIP_H */
