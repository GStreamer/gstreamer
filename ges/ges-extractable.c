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
/**
 * SECTION: gesextractable
 * @title: GESExtractable Interface
 * @short_description: An interface for objects which can be extracted
 * from a #GESAsset
 *
 * A #GObject that implements the #GESExtractable interface can be
 * extracted from a #GESAsset using ges_asset_extract().
 *
 * Each extractable type will have its own way of interpreting the
 * #GESAsset:id of an asset (or, if it is associated with a specific
 * subclass of #GESAsset, the asset subclass may handle the
 * interpretation of the #GESAsset:id). By default, the requested asset
 * #GESAsset:id will be ignored by a #GESExtractable and will be set to
 * the type name of the extractable instead. Also by default, when the
 * requested asset is extracted, the returned object will simply be a
 * newly created default object of that extractable type. You should check
 * the documentation for each extractable type to see if they differ from
 * the default.
 *
 * After the object is extracted, it will have a reference to the asset it
 * came from, which you can retrieve using ges_extractable_get_asset().
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-asset.h"
#include "ges-internal.h"
#include "ges-extractable.h"
#include "ges-uri-clip.h"

static GQuark ges_asset_key;

G_DEFINE_INTERFACE_WITH_CODE (GESExtractable, ges_extractable,
    G_TYPE_INITIALLY_UNOWNED,
    ges_asset_key = g_quark_from_static_string ("ges-extractable-data"));

static gchar *
ges_extractable_check_id_default (GType type, const gchar * id, GError ** error)
{
  return g_strdup (g_type_name (type));
}

static GType
ges_extractable_get_real_extractable_type_default (GType type, const gchar * id)
{
  return type;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS;       /* Start ignoring GParameter deprecation */
static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  *n_params = 0;

  return NULL;
}

G_GNUC_END_IGNORE_DEPRECATIONS; /* End ignoring GParameter deprecation */
static gchar *
extractable_get_id (GESExtractable * self)
{
  GESAsset *asset;

  if ((asset = ges_extractable_get_asset (self)))
    return g_strdup (ges_asset_get_id (asset));

  return g_strdup (g_type_name (G_OBJECT_TYPE (self)));

}

static void
ges_extractable_default_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_ASSET;
  iface->check_id = ges_extractable_check_id_default;
  iface->get_real_extractable_type =
      ges_extractable_get_real_extractable_type_default;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->set_asset = NULL;
  iface->set_asset_full = NULL;
  iface->get_id = extractable_get_id;
  iface->register_metas = NULL;
  iface->can_update_asset = FALSE;
}

/**
 * ges_extractable_get_asset:
 * @self: A #GESExtractable
 *
 * Get the asset that has been set on the extractable object.
 *
 * Returns: (transfer none) (nullable): The asset set on @self, or %NULL
 * if no asset has been set.
 */
GESAsset *
ges_extractable_get_asset (GESExtractable * self)
{
  g_return_val_if_fail (GES_IS_EXTRACTABLE (self), NULL);

  return g_object_get_qdata (G_OBJECT (self), ges_asset_key);;
}

/**
 * ges_extractable_set_asset:
 * @self: A #GESExtractable
 * @asset: (transfer none): The asset to set
 *
 * Sets the asset for this extractable object.
 *
 * When an object is extracted from an asset using ges_asset_extract() its
 * asset will be automatically set. Note that many classes that implement
 * #GESExtractable will automatically create their objects using assets
 * when you call their @new methods. However, you can use this method to
 * associate an object with a compatible asset if it was created by other
 * means and does not yet have an asset. Or, for some implementations of
 * #GESExtractable, you can use this to change the asset of the given
 * extractable object, which will lead to a change in its state to
 * match the new asset #GESAsset:id.
 *
 * Returns: %TRUE if @asset could be successfully set on @self.
 */
gboolean
ges_extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  GESExtractableInterface *iface;
  GType extract_type;

  g_return_val_if_fail (GES_IS_EXTRACTABLE (self), FALSE);

  iface = GES_EXTRACTABLE_GET_INTERFACE (self);
  GST_DEBUG_OBJECT (self, "Setting asset to %" GST_PTR_FORMAT, asset);

  if (iface->can_update_asset == FALSE &&
      g_object_get_qdata (G_OBJECT (self), ges_asset_key)) {
    GST_WARNING_OBJECT (self, "Can not reset asset on object");
    /* FIXME: do not fail if the same asset */

    return FALSE;
  }

  extract_type = ges_asset_get_extractable_type (asset);
  if (G_OBJECT_TYPE (self) != extract_type) {
    GST_WARNING_OBJECT (self, "Can not set the asset to %" GST_PTR_FORMAT
        " because its extractable-type is %s, rather than %s",
        asset, g_type_name (extract_type), G_OBJECT_TYPE_NAME (self));

    return FALSE;
  }

  g_object_set_qdata_full (G_OBJECT (self), ges_asset_key,
      gst_object_ref (asset), gst_object_unref);

  /* Let classes that implement the interface know that a asset has been set */
  if (iface->set_asset_full)
    /* FIXME: return to the previous asset if the setting fails */
    return iface->set_asset_full (self, asset);

  if (iface->set_asset)
    iface->set_asset (self, asset);

  return TRUE;
}

/**
 * ges_extractable_get_id:
 * @self: A #GESExtractable
 *
 * Gets the #GESAsset:id of some associated asset. It may be the case
 * that the object has no set asset, or even that such an asset does not
 * yet exist in the GES cache. Instead, this will return the asset
 * #GESAsset:id that is _compatible_ with the current state of the object,
 * as determined by the #GESExtractable implementer. If it was indeed
 * extracted from an asset, this should return the same as its
 * corresponding asset #GESAsset:id.
 *
 * Returns: (transfer full): The #GESAsset:id of some associated #GESAsset
 * that is compatible with @self's current state.
 */
gchar *
ges_extractable_get_id (GESExtractable * self)
{
  g_return_val_if_fail (GES_IS_EXTRACTABLE (self), NULL);

  return GES_EXTRACTABLE_GET_INTERFACE (self)->get_id (self);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS;       /* Start ignoring GParameter deprecation */
/**
 * ges_extractable_type_get_parameters_for_id:
 * @type: The #GType implementing #GESExtractable
 * @id: The ID of the Extractable
 * @n_params: (out): Return location for the returned array
 *
 * Returns: (transfer full) (array length=n_params): an array of #GParameter
 * needed to extract the #GESExtractable from a #GESAsset of @id
 */
GParameter *
ges_extractable_type_get_parameters_from_id (GType type, const gchar * id,
    guint * n_params)
{
  GObjectClass *klass;
  GESExtractableInterface *iface;

  GParameter *ret = NULL;

  g_return_val_if_fail (g_type_is_a (type, G_TYPE_OBJECT), NULL);
  g_return_val_if_fail (g_type_is_a (type, GES_TYPE_EXTRACTABLE), NULL);

  klass = g_type_class_ref (type);
  iface = g_type_interface_peek (klass, GES_TYPE_EXTRACTABLE);

  ret = iface->get_parameters_from_id (id, n_params);

  g_type_class_unref (klass);

  return ret;
}

G_GNUC_END_IGNORE_DEPRECATIONS; /* End ignoring GParameter deprecation */

/**
 * ges_extractable_type_get_asset_type:
 * @type: The #GType implementing #GESExtractable
 *
 * Get the #GType, subclass of #GES_TYPE_ASSET to instanciate
 * to be able to extract a @type
 *
 * Returns: the #GType to use to create a asset to extract @type
 */
GType
ges_extractable_type_get_asset_type (GType type)
{
  GObjectClass *klass;
  GESExtractableInterface *iface;

  g_return_val_if_fail (g_type_is_a (type, G_TYPE_OBJECT), G_TYPE_INVALID);
  g_return_val_if_fail (g_type_is_a (type, GES_TYPE_EXTRACTABLE),
      G_TYPE_INVALID);

  klass = g_type_class_ref (type);

  iface = g_type_interface_peek (klass, GES_TYPE_EXTRACTABLE);

  g_type_class_unref (klass);

  return iface->asset_type;
}

/**
 * ges_extractable_type_check_id:
 * @type: The #GType implementing #GESExtractable
 * @id: The ID to check
 *
 * Check if @id is valid for @type
 *
 * Returns: (transfer full) (nullable): A newly allocated string containing
 * the actual ID (after some processing) or %NULL if the ID is wrong.
 */
gchar *
ges_extractable_type_check_id (GType type, const gchar * id, GError ** error)
{
  GObjectClass *klass;
  GESExtractableInterface *iface;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (g_type_is_a (type, G_TYPE_OBJECT), NULL);
  g_return_val_if_fail (g_type_is_a (type, GES_TYPE_EXTRACTABLE), NULL);

  klass = g_type_class_ref (type);

  iface = g_type_interface_peek (klass, GES_TYPE_EXTRACTABLE);

  g_type_class_unref (klass);

  return iface->check_id (type, id, error);
}

/**
 * ges_extractable_get_real_extractable_type:
 * @type: The #GType implementing #GESExtractable
 * @id: The ID to check
 *
 * Get the #GType that should be used as extractable_type for @type and
 * @id. Usually this will be the same as @type but in some cases they can
 * be some subclasses of @type. For example, in the case of #GESFormatter,
 * the returned #GType will be a subclass of #GESFormatter that can be used
 * to load the file pointed by @id.
 *
 * Returns: Return the #GESExtractable type that should be used for @id
 */
GType
ges_extractable_get_real_extractable_type_for_id (GType type, const gchar * id)
{
  GType ret;
  GObjectClass *klass;
  GESExtractableInterface *iface;

  klass = g_type_class_ref (type);
  iface = g_type_interface_peek (klass, GES_TYPE_EXTRACTABLE);
  g_type_class_unref (klass);

  ret = iface->get_real_extractable_type (type, id);

  GST_DEBUG ("Extractable type for id %s and wanted type %s is: %s",
      id, g_type_name (type), g_type_name (ret));

  return ret;
}

/**
 * ges_extractable_register_metas:
 * @self: A #GESExtractable
 * @asset: The #GESAsset on which metadatas should be registered
 *
 * Lets you register standard method for @extractable_type on @asset
 *
 * Returns: %TRUE if metas could be register %FALSE otherwize
 */
gboolean
ges_extractable_register_metas (GType extractable_type, GESAsset * asset)
{
  GObjectClass *klass;
  gboolean ret = FALSE;
  GESExtractableInterface *iface;

  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      FALSE);

  klass = g_type_class_ref (extractable_type);
  iface = g_type_interface_peek (klass, GES_TYPE_EXTRACTABLE);

  if (iface->register_metas)
    ret = iface->register_metas (iface, klass, asset);

  g_type_class_unref (klass);
  return ret;
}
