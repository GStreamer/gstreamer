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
#include <ges/ges-extractable.h>
#include <ges/ges-types.h>
#include <ges/ges-enums.h>
#include <gio/gio.h>
#include <gst/gst.h>

G_BEGIN_DECLS
#define GES_TYPE_ASSET ges_asset_get_type()
GES_DECLARE_TYPE(Asset, asset, ASSET);

/**
 * GESAssetLoadingReturn:
 * @GES_ASSET_LOADING_ERROR: Indicates that an error occurred
 * @GES_ASSET_LOADING_ASYNC: Indicates that the loading is being performed
 * asynchronously
 * @GES_ASSET_LOADING_OK: Indicates that the loading is complete, without
 * error
 */
typedef enum
{
  GES_ASSET_LOADING_ERROR,
  GES_ASSET_LOADING_ASYNC,
  GES_ASSET_LOADING_OK
} GESAssetLoadingReturn;

struct _GESAsset
{
  GObject parent;

  /* <private> */
  GESAssetPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESAssetClass:
 * @start_loading: A method to be called when an asset is being requested
 * asynchronously. This will be after the properties of the asset have
 * been set, so it is tasked with (re)loading the 'state' of the asset.
 * The return value should indicated whether the loading is complete, is
 * carrying on asynchronously, or an error occurred. The default
 * implementation will simply return that loading is already complete (the
 * asset is already in a usable state after the properties have been set).
 * @extract: A method that returns a new object of the asset's
 * #GESAsset:extractable-type, or %NULL if an error occurs. The default
 * implementation will fetch the properties of the #GESExtractable from
 * its get_parameters_from_id() class method and set them on a new
 * #GESAsset:extractable-type #GObject, which is returned.
 * @request_id_update: A method called by a #GESProject when an asset has
 * failed to load. @error is the error given by
 * ges_asset_request_finish (). Returns: %TRUE if a new id for @self was
 * passed to @proposed_new_id.
 * @proxied: Deprecated: 1.18: This vmethod is no longer called.
 */
/* FIXME: add documentation for inform_proxy when it is used properly */
struct _GESAssetClass
{
  GObjectClass parent;

  GESAssetLoadingReturn    (*start_loading)     (GESAsset *self,
                                                 GError **error);
  GESExtractable*          (*extract)           (GESAsset *self,
                                                 GError **error);
  /* Let subclasses know that we proxied an asset */
  void                     (*inform_proxy)      (GESAsset *self,
                                                 const gchar *proxy_id);

  void                     (*proxied)      (GESAsset *self,
                                            GESAsset *proxy);

  /* Ask subclasses for a new ID for @self when the asset failed loading
   * This function returns %FALSE when the ID could be updated or %TRUE
   * otherwize */
  gboolean                 (*request_id_update) (GESAsset *self,
                                                 gchar **proposed_new_id,
                                                 GError *error) ;
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GType ges_asset_get_extractable_type (GESAsset * self);
GES_API
void ges_asset_request_async         (GType extractable_type,
                                      const gchar * id,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
GES_API
GESAsset * ges_asset_request         (GType extractable_type,
                                      const gchar * id,
                                      GError **error);
GES_API
const gchar * ges_asset_get_id       (GESAsset* self);
GES_API
GESAsset * ges_asset_request_finish  (GAsyncResult *res,
                                      GError **error);
GES_API
GError * ges_asset_get_error         (GESAsset * self);
GES_API
GESExtractable * ges_asset_extract   (GESAsset * self,
                                      GError **error);
GES_API
GList * ges_list_assets              (GType filter);


GES_API
gboolean ges_asset_set_proxy         (GESAsset *asset, GESAsset *proxy);
GES_API
gboolean ges_asset_unproxy           (GESAsset *asset, GESAsset * proxy);
GES_API
GList * ges_asset_list_proxies       (GESAsset *asset);
GES_API
GESAsset * ges_asset_get_proxy_target(GESAsset *proxy);
GES_API
GESAsset * ges_asset_get_proxy       (GESAsset *asset);
GES_API
gboolean ges_asset_needs_reload 	 (GType extractable_type,
									  const gchar * id);

G_END_DECLS
