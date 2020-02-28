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

typedef struct _GESExtractable GESExtractable;

#include <glib-object.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>

G_BEGIN_DECLS

/* GESExtractable interface declarations */
#define GES_TYPE_EXTRACTABLE                (ges_extractable_get_type ())
#define GES_EXTRACTABLE_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GES_TYPE_EXTRACTABLE, GESExtractableInterface))
GES_API
G_DECLARE_INTERFACE(GESExtractable, ges_extractable, GES, EXTRACTABLE, GInitiallyUnowned);

/**
 * GESExtractableCheckId:
 * @type: The #GESExtractable type to check @id for
 * @id: The ID to check
 * @error: An error that can be set if needed
 *
 * Method for checking that an ID is valid for the given #GESExtractable
 * type. If the given ID is considered valid, it can be adjusted into some
 * standard and returned to prevent the creation of separate #GESAsset-s,
 * with different #GESAsset:id, that would otherwise act the same.
 *
 * Returns (transfer full) (nullable): The actual #GESAsset:id to set on
 * any corresponding assets, based on @id, or %NULL if @id is not valid.
 */

typedef gchar* (*GESExtractableCheckId) (GType type, const gchar *id,
    GError **error);

/**
 * GESExtractableInterface:
 * @asset_type: The subclass type of #GESAsset that should be created when
 * an asset with the corresponding #GESAsset:extractable-type is
 * requested.
 * @check_id: The method to call to check whether a given ID is valid as
 * an asset #GESAsset:id for the given #GESAsset:extractable-type. The
 * returned ID is the actual #GESAsset:id that is set on the asset. The
 * default implementation will simply always return the type name of the
 * #GESAsset:extractable-type, even if the received ID is %NULL. As such,
 * any given ID is considered valid (or is ignored), but only one is
 * actually ever set on an asset, which means the given
 * #GESAsset:extractable-type can only have one associated asset.
 * @can_update_asset: Whether an object of this class can have its
 * #GESAsset change over its lifetime. This should be set to %TRUE if one
 * of the object's parameters that is associated with its ID can change
 * after construction, which would require an asset with a new ID. Note
 * that the subclass is required to handle the requesting and setting of
 * the new asset on the object.
 * @set_asset: This method is called after the #GESAsset of an object is
 * set. If your class supports the asset of an object changing, then you
 * can use this method to change the parameters of the object to match the
 * new asset #GESAsset:id. If setting the asset should be able to fail,
 * you should implement @set_asset_full instead.
 * @set_asset_full: Like @set_asset, but also allows you to return %FALSE
 * to indicate a failure to change the object in response to a change in
 * its asset.
 * @get_parameters_from_id: The method to call to get the object
 * properties corresponding to a given asset #GESAsset:id. The default
 * implementation will simply return no parameters. The default #GESAsset
 * will call this to set the returned properties on the extracted object,
 * but other subclasses may ignore this method.
 * @get_id: The method to fetch the #GESAsset:id of some associated asset.
 * Note that it may be the case that the object does not have its asset
 * set, or even that an asset with such an #GESAsset:id does not exist in
 * the GES cache. Instead, this should return the #GESAsset:id that is
 * _compatible_ with the current state of the object. The default
 * implementation simply returns the currently set asset ID, or the type name
 * of the object, which is what is used as the #GESAsset:id by default,
 * if no asset is set.
 * @get_real_extractable_type: The method to call to get the actual
 * #GESAsset:extractable-type an asset should have set, given the
 * requested #GESAsset:id. The default implementation simply returns the
 * same type as given. You can overwrite this if it is more appropriate
 * to extract the object from a subclass, depending on the requested
 * #GESAsset:id. Note that when an asset is requested, this method will be
 * called before the other class methods. In particular, this means that
 * the @check_id and @get_parameters_from_id class methods of the returned
 * type will be used (instead of our own).
 * @register_metas: The method to set metadata on an asset. This is called
 * on initiation of the asset, but before it begins to load its state.
 */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
struct _GESExtractableInterface
{
  GTypeInterface parent;

  GType asset_type;

  GESExtractableCheckId check_id;
  gboolean can_update_asset;

  void (*set_asset)                  (GESExtractable *self,
                                         GESAsset *asset);

  gboolean (*set_asset_full)         (GESExtractable *self,
                                      GESAsset *asset);

  GParameter *(*get_parameters_from_id) (const gchar *id,
                                         guint *n_params);

  gchar * (*get_id)                     (GESExtractable *self);

  GType (*get_real_extractable_type)    (GType wanted_type,
                                         const gchar *id);

  gboolean (*register_metas)            (GESExtractableInterface *self,
                                         GObjectClass *klass,
                                         GESAsset *asset);

  gpointer _ges_reserved[GES_PADDING];
};
G_GNUC_END_IGNORE_DEPRECATIONS

GES_API
GESAsset* ges_extractable_get_asset      (GESExtractable *self);
GES_API
gboolean ges_extractable_set_asset              (GESExtractable *self,
                                                GESAsset *asset);

GES_API
gchar * ges_extractable_get_id                 (GESExtractable *self);

G_END_DECLS