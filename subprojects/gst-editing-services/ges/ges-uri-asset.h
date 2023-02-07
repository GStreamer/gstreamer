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

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>
#include <ges/ges-source-clip-asset.h>
#include <ges/ges-track-element-asset.h>

G_BEGIN_DECLS
#define GES_TYPE_URI_CLIP_ASSET ges_uri_clip_asset_get_type()
GES_DECLARE_TYPE(UriClipAsset, uri_clip_asset, URI_CLIP_ASSET);

struct _GESUriClipAsset
{
  GESSourceClipAsset parent;

  /* <private> */
  GESUriClipAssetPrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESUriClipAssetClass
{
  GESSourceClipAssetClass parent_class;

  /* <private> */
  GstDiscoverer *discoverer; /* Unused */
  GstDiscoverer *sync_discoverer; /* Unused */

  void (*discovered) (GstDiscoverer * discoverer, /* Unused */
                      GstDiscovererInfo * info,
                      GError * err,
                      gpointer user_data);

  gpointer _ges_reserved[GES_PADDING -1];
};

GES_API
GstDiscovererInfo *ges_uri_clip_asset_get_info      (const GESUriClipAsset * self);
GES_API
GstClockTime ges_uri_clip_asset_get_duration        (GESUriClipAsset *self);
GES_API
GstClockTime ges_uri_clip_asset_get_max_duration    (GESUriClipAsset *self);
GES_API
gboolean ges_uri_clip_asset_is_image                (GESUriClipAsset *self);
GES_API
void ges_uri_clip_asset_new                         (const gchar *uri,
                                                     GCancellable *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
GES_API
GESUriClipAsset * ges_uri_clip_asset_finish (GAsyncResult * res, GError ** error);
GES_API
GESUriClipAsset* ges_uri_clip_asset_request_sync    (const gchar *uri, GError **error);
GES_API
void ges_uri_clip_asset_class_set_timeout           (GESUriClipAssetClass *klass,
                                                     GstClockTime timeout);
GES_API
const GList * ges_uri_clip_asset_get_stream_assets  (GESUriClipAsset *self);

#define GES_TYPE_URI_SOURCE_ASSET ges_uri_source_asset_get_type()
GES_DECLARE_TYPE(UriSourceAsset, uri_source_asset, URI_SOURCE_ASSET);

/**
 * GESUriSourceAsset:
 *
 * Asset to create a stream specific #GESSource for a media file.
 *
 * NOTE: You should never request such a #GESAsset as they will be created automatically
 * by #GESUriClipAsset-s.
 */
struct _GESUriSourceAsset
{
  GESTrackElementAsset parent;

  /* <private> */
  GESUriSourceAssetPrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESUriSourceAssetClass
{
  GESTrackElementAssetClass parent_class;

  gpointer _ges_reserved[GES_PADDING];
};
GES_API
GstDiscovererStreamInfo * ges_uri_source_asset_get_stream_info     (GESUriSourceAsset *asset);
GES_API
const gchar * ges_uri_source_asset_get_stream_uri                  (GESUriSourceAsset *asset);
GES_API
const GESUriClipAsset *ges_uri_source_asset_get_filesource_asset   (GESUriSourceAsset *asset);
GES_API
gboolean ges_uri_source_asset_is_image                             (GESUriSourceAsset *asset);
G_END_DECLS
