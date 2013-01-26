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
 * SECTION: ges-asset
 * @short_description: Represents usable ressources inside the GStreamer Editing Services
 *
 * The Assets in the GStreamer Editing Services represent the ressources
 * that can be used. You can create assets for any type that implements the #GESExtractable
 * interface, for example #GESClips, #GESFormatter, and #GESTrackElement do implement it.
 * This means that asssets will represent for example a #GESUriClips, #GESBaseEffect etc,
 * and then you can extract objects of those types with the appropriate parameters from the asset
 * using the #ges_asset_extract method:
 *
 * |[
 * GESAsset *effect_asset;
 * GESTrackParseLaunchEffect *effect;
 *
 * // You create an asset for an effect
 * effect_asset = ges_asset_request (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT, "agingtv", NULL);
 *
 * // And now you can extract an instance of GESTrackParseLaunchEffect from that asset
 * effect = GES_TRACK_PARSE_LAUNCH_EFFECT (ges_asset_extract (effect_asset));
 *
 * ]|
 *
 * In that example, the advantages of having a #GESAsset are that you can know what effects
 * you are working with and let your user know about the avalaible ones, you can add metadata
 * to the #GESAsset through the #GESMetaContainer interface and you have a model for your
 * custom effects. Note that #GESAsset management is making easier thanks to the #GESProject class.
 *
 * Each asset are represented by a pair of @extractable_type and @id (string). Actually the @extractable_type
 * is the type that implements the #GESExtractable interface, that means that for example for a #GESUriClip,
 * the type that implements the #GESExtractable interface is #GESClip.
 * The identifier represents different things depending on the @extractable_type and you should check
 * the documentation of each type to know what the ID of #GESAsset actually represents for that type. By default,
 * we only have one #GESAsset per type, and the @id is the name of the type, but this behaviour is overriden
 * to be more usefull. For example, for GESTransitionClips, the ID is the vtype of the transition
 * you will extract from it (ie crossfade, box-wipe-rc etc..) For #GESTrackParseLaunchEffect the id is the
 * @bin-description property of the extracted objects (ie the gst-launch style description of the bin that
 * will be used).
 *
 * Each and every #GESAsset are cached into GES, and you can query those with the #ges_list_assets function.
 * Also the system will automatically register #GESAssets for #GESFormatters and #GESTransitionClips
 * and standard effects (actually not implemented yet) and you can simply query those calling:
 * |[
 *    GList *formatter_assets, *tmp;
 *
 *    //  List all  the transitions
 *    formatter_assets = ges_list_assets (GES_TYPE_FORMATTER);
 *
 *    // Print some infos about the formatter GESAsset
 *    for (tmp = formatter_assets; tmp; tmp = tmp->next) {
 *      g_print ("Name of the formatter: %s, file extension it produces: %s",
 *        ges_meta_container_get_string (GES_META_CONTAINER (tmp->data), GES_META_FORMATTER_NAME),
 *        ges_meta_container_get_string (GES_META_CONTAINER (tmp->data), GES_META_FORMATTER_EXTENSION));
 *    }
 *
 *    g_list_free (transition_assets);
 *
 * ]|
 *
 * You can request the creation of #GESAssets using either #ges_asset_request_async or
 * #ges_asset_request_async. All the #GESAssets are cached and thus any asset that has already
 * been created can be requested again without overhead.
 */

#include "ges.h"
#include "ges-internal.h"

#include <gst/gst.h>

enum
{
  PROP_0,
  PROP_TYPE,
  PROP_ID,
  PROP_LAST
};

typedef enum
{
  ASSET_NOT_INITIALIZED,
  ASSET_INITIALIZING, ASSET_INITIALIZED_WITH_ERROR,
  ASSET_PROXIED,
  ASSET_INITIALIZED
} GESAssetState;

static GParamSpec *_properties[PROP_LAST];

struct _GESAssetPrivate
{
  gchar *id;
  GESAssetState state;
  GType extractable_type;

  /* When a asset is proxied, instanciating it will
   * return the asset it points to */
  char *proxied_asset_id;

  /* The error that accured when a asset has been initialized with error */
  GError *error;
};

/* Internal structure to help avoid full loading
 * of one asset several times
 */
typedef struct
{
  GList *results;
  GESAsset *asset;
} GESAssetCacheEntry;

/* Also protect all the entries in the cache */
static GMutex asset_cache_lock;
/* We are mapping entries by types and ID, such as:
 *
 * {
 *   first_extractable_type_name1 :
 *      {
 *        "some ID": GESAssetCacheEntry,
 *        "some other ID": GESAssetCacheEntry 2
 *      },
 *   second_extractable_type_name :
 *      {
 *        "some ID": GESAssetCacheEntry,
 *        "some other ID": GESAssetCacheEntry 2
 *      }
 * }
 *
 * (The first extractable type is the type of the class that implemented
 *  the GESExtractable interface ie: GESClip, GESTimeline,
 *  GESFomatter, etc... but not subclasses)
 *
 * This is in order to be able to have 2 Asset with the same ID but
 * different extractable types.
 **/
static GHashTable *type_entries_table = NULL;
#define LOCK_CACHE   (g_mutex_lock (&asset_cache_lock))
#define UNLOCK_CACHE (g_mutex_unlock (&asset_cache_lock))

static gchar *
_check_and_update_parameters (GType * extractable_type, const gchar * id,
    GError ** error)
{
  gchar *real_id;
  GType old_type = *extractable_type;

  *extractable_type =
      ges_extractable_get_real_extractable_type_for_id (*extractable_type, id);

  if (*extractable_type == G_TYPE_NONE) {
    GST_WARNING ("No way to create a Asset for ID: %s, type: %s", id,
        g_type_name (old_type));

    if (error && *error == NULL)
      g_set_error (error, GES_ERROR_DOMAIN, 0,
          "Wrong ID, can not find any extractable_type");
    return NULL;
  }

  real_id = ges_extractable_type_check_id (*extractable_type, id, error);
  if (real_id == NULL) {
    GST_WARNING ("Wrong ID %s, can not create asset", id);

    g_free (real_id);
    if (error && *error == NULL)
      g_set_error (error, GES_ERROR_DOMAIN, 0, "Wrong ID");

    return NULL;
  }

  return real_id;
}

static gboolean
initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  gboolean ret;
  GESAsset *asset = GES_ASSET (initable);

  ges_asset_cache_put (gst_object_ref (asset), NULL);
  ret = ges_asset_cache_set_loaded (asset->priv->extractable_type,
      asset->priv->id, NULL);

  g_clear_error (error);

  return ret;
}

static void
async_initable_init_async (GAsyncInitable * initable, gint io_priority,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;

  GError *error = NULL;
  GESAsset *asset = GES_ASSET (initable);

  simple = g_simple_async_result_new (G_OBJECT (asset),
      callback, user_data, ges_asset_request_async);

  ges_asset_cache_put (asset, simple);
  switch (GES_ASSET_GET_CLASS (asset)->start_loading (asset, &error)) {
    case GES_ASSET_LOADING_ERROR:
    {
      if (error == NULL)
        g_set_error (&error, GES_ERROR_DOMAIN, 1,
            "Could not start loading asset");

      /* FIXME Define error code */
      ges_asset_cache_set_loaded (asset->priv->extractable_type,
          asset->priv->id, error);
      g_error_free (error);
      return;
    }
    case GES_ASSET_LOADING_OK:
    {
      ges_asset_cache_set_loaded (asset->priv->extractable_type,
          asset->priv->id, error);
      return;
    }
    case GES_ASSET_LOADING_ASYNC:
      /* If Async....  let it go */
      break;
  }
}

static void
async_initable_iface_init (GAsyncInitableIface * async_initable_iface)
{
  async_initable_iface->init_async = async_initable_init_async;
}

static void
initable_iface_init (GInitableIface * initable_iface)
{
  initable_iface->init = initable_init;
}

G_DEFINE_TYPE_WITH_CODE (GESAsset, ges_asset, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
    G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));

/* GESAsset virtual methods default implementation */
static GESAssetLoadingReturn
ges_asset_start_loading_default (GESAsset * asset, GError ** error)
{
  return GES_ASSET_LOADING_OK;
}

static GESExtractable *
ges_asset_extract_default (GESAsset * asset, GError ** error)
{
  guint n_params;
  GParameter *params;
  GESAssetPrivate *priv = asset->priv;
  GESExtractable *n_extractable;

  params = ges_extractable_type_get_parameters_from_id (priv->extractable_type,
      priv->id, &n_params);

  n_extractable = g_object_newv (priv->extractable_type, n_params, params);

  while (n_params--)
    g_value_unset (&params[n_params].value);
  if (params)
    g_free (params);

  return n_extractable;
}

static gboolean
ges_asset_request_id_update_default (GESAsset * self, gchar ** proposed_new_id,
    GError * error)
{
  return FALSE;
}

/* GObject virtual methods implementation */
static void
ges_asset_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESAsset *asset = GES_ASSET (object);

  switch (property_id) {
    case PROP_TYPE:
      g_value_set_gtype (value, asset->priv->extractable_type);
      break;
    case PROP_ID:
      g_value_set_string (value, asset->priv->id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESAsset *asset = GES_ASSET (object);

  switch (property_id) {
    case PROP_TYPE:
      asset->priv->extractable_type = g_value_get_gtype (value);
      ges_extractable_register_metas (asset->priv->extractable_type, asset);
      break;
    case PROP_ID:
      asset->priv->id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_finalize (GObject * object)
{
  GESAssetPrivate *priv = GES_ASSET (object)->priv;

  if (priv->id)
    g_free (priv->id);

  if (priv->proxied_asset_id)
    g_free (priv->proxied_asset_id);

  if (priv->error)
    g_error_free (priv->error);

  G_OBJECT_CLASS (ges_asset_parent_class)->finalize (object);
}

void
ges_asset_class_init (GESAssetClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GESAssetPrivate));

  object_class->get_property = ges_asset_get_property;
  object_class->set_property = ges_asset_set_property;
  object_class->finalize = ges_asset_finalize;

  _properties[PROP_TYPE] =
      g_param_spec_gtype ("extractable-type", "Extractable type",
      "The type of the Object that can be extracted out of the asset",
      G_TYPE_OBJECT, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  _properties[PROP_ID] =
      g_param_spec_string ("id", "Identifier",
      "The unic identifier of the asset", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, _properties);

  klass->start_loading = ges_asset_start_loading_default;
  klass->extract = ges_asset_extract_default;
  klass->request_id_update = ges_asset_request_id_update_default;
  klass->inform_proxy = NULL;
}

void
ges_asset_init (GESAsset * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_ASSET, GESAssetPrivate);

  self->priv->state = ASSET_INITIALIZING;
  self->priv->proxied_asset_id = NULL;
}

/* Internal methods */

/* Find the type that implemented the GESExtractable interface */
static inline const gchar *
_extractable_type_name (GType type)
{
  while (1) {
    if (g_type_is_a (g_type_parent (type), GES_TYPE_EXTRACTABLE))
      type = g_type_parent (type);
    else
      return g_type_name (type);
  }
}

static inline GESAssetCacheEntry *
_lookup_entry (GType extractable_type, const gchar * id)
{
  GHashTable *entries_table;

  entries_table = g_hash_table_lookup (type_entries_table,
      _extractable_type_name (extractable_type));
  if (entries_table)
    return g_hash_table_lookup (entries_table, id);

  return NULL;
}

static void
_free_entries (gpointer entry)
{
  g_slice_free (GESAssetCacheEntry, entry);
}

/**
 * ges_asset_cache_lookup:
 *
 * @id String identifier of asset
 *
 * Looks for asset with specified id in cache and it's completely loaded.
 *
 * Returns: (transfer none): The #GESAsset found or %NULL
 */
GESAsset *
ges_asset_cache_lookup (GType extractable_type, const gchar * id)
{
  GESAsset *asset = NULL;
  GESAssetCacheEntry *entry = NULL;

  g_return_val_if_fail (id, NULL);

  LOCK_CACHE;
  entry = _lookup_entry (extractable_type, id);
  if (entry)
    asset = entry->asset;
  UNLOCK_CACHE;

  return asset;
}

static void
ges_asset_cache_append_result (GType extractable_type,
    const gchar * id, GSimpleAsyncResult * res)
{
  GESAssetCacheEntry *entry = NULL;

  LOCK_CACHE;
  if ((entry = _lookup_entry (extractable_type, id)))
    entry->results = g_list_append (entry->results, res);
  UNLOCK_CACHE;
}

gboolean
ges_asset_cache_set_loaded (GType extractable_type, const gchar * id,
    GError * error)
{
  GList *tmp;
  GESAsset *asset;
  GESAssetCacheEntry *entry = NULL;

  LOCK_CACHE;
  if ((entry = _lookup_entry (extractable_type, id)) == NULL) {
    UNLOCK_CACHE;
    GST_ERROR ("Calling but type %s ID: %s not in cached, "
        "something massively screwed", g_type_name (extractable_type), id);

    return FALSE;
  }

  asset = entry->asset;
  GST_DEBUG_OBJECT (entry->asset, ": (extractable type: %s) loaded, calling %i "
      "callback (Error: %s)", g_type_name (asset->priv->extractable_type),
      g_list_length (entry->results), error ? error->message : "");

  if (error) {
    GList *results = entry->results;

    asset->priv->state = ASSET_INITIALIZED_WITH_ERROR;
    if (asset->priv->error)
      g_error_free (asset->priv->error);
    asset->priv->error = g_error_copy (error);
    entry->results = NULL;
    UNLOCK_CACHE;

    /* In case of error we do not want to emit in idle as we need to recover
     * if possible */
    for (tmp = results; tmp; tmp = tmp->next) {
      g_simple_async_result_set_from_error (G_SIMPLE_ASYNC_RESULT (tmp->data),
          error);
      g_simple_async_result_complete (G_SIMPLE_ASYNC_RESULT (tmp->data));
      gst_object_unref (tmp->data);
    }

    g_list_free (results);
    return TRUE;
  } else {
    asset->priv->state = ASSET_INITIALIZED;

    g_list_foreach (entry->results,
        (GFunc) g_simple_async_result_complete_in_idle, NULL);
    g_list_free_full (entry->results, g_object_unref);
    entry->results = NULL;
    UNLOCK_CACHE;
  }

  return TRUE;
}

void
ges_asset_cache_put (GESAsset * asset, GSimpleAsyncResult * res)
{
  GType extractable_type;
  const gchar *asset_id;
  GESAssetCacheEntry *entry;

  /* Needing to work with the cache, taking the lock */
  asset_id = ges_asset_get_id (asset);
  extractable_type = asset->priv->extractable_type;

  LOCK_CACHE;
  if (!(entry = _lookup_entry (extractable_type, asset_id))) {
    GHashTable *entries_table;

    entries_table = g_hash_table_lookup (type_entries_table,
        _extractable_type_name (extractable_type));
    if (entries_table == NULL) {
      entries_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          _free_entries);

      g_hash_table_insert (type_entries_table,
          g_strdup (_extractable_type_name (extractable_type)), entries_table);
    }

    entry = g_slice_new0 (GESAssetCacheEntry);

    entry->asset = asset;
    if (res)
      entry->results = g_list_prepend (entry->results, res);
    g_hash_table_insert (entries_table, (gpointer) g_strdup (asset_id),
        (gpointer) entry);
  } else {
    if (res) {
      GST_DEBUG ("%s already in cache, adding result %p", asset_id, res);
      entry->results = g_list_prepend (entry->results, res);
    }
  }
  UNLOCK_CACHE;
}

void
ges_asset_cache_init (void)
{
  g_mutex_init (&asset_cache_lock);
  type_entries_table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_hash_table_unref);

  _init_formatter_assets ();
  _init_standard_transition_assets ();
}

gboolean
ges_asset_request_id_update (GESAsset * asset, gchar ** proposed_id,
    GError * error)
{
  return GES_ASSET_GET_CLASS (asset)->request_id_update (asset, proposed_id,
      error);
}

gboolean
ges_asset_set_proxy (GESAsset * asset, const gchar * new_id)
{
  GESAssetClass *class;
  if (g_strcmp0 (asset->priv->id, new_id) == 0) {
    GST_WARNING_OBJECT (asset, "Trying to proxy to itself (%s),"
        " NOT possible", new_id);

    return FALSE;
  } else if (g_strcmp0 (asset->priv->proxied_asset_id, new_id) == 0) {
    GST_WARNING_OBJECT (asset, "Trying to proxy to same currently set proxy");

    return FALSE;
  }

  if (asset->priv->proxied_asset_id)
    g_free (asset->priv->proxied_asset_id);

  asset->priv->state = ASSET_PROXIED;
  asset->priv->proxied_asset_id = g_strdup (new_id);

  class = GES_ASSET_GET_CLASS (asset);
  if (class->inform_proxy)
    GES_ASSET_GET_CLASS (asset)->inform_proxy (asset, new_id);

  GST_DEBUG_OBJECT (asset, "Now proxied to %s", new_id);
  return TRUE;
}

/* Caution, this method should be used in rare cases (ie: for the project
 * as we can change its ID from a useless one to a proper URI). In most
 * cases you want to update the ID creating a proxy
 */
void
ges_asset_set_id (GESAsset * asset, const gchar * id)
{
  GHashTable *entries;

  gpointer orig_id = NULL;
  GESAssetCacheEntry *entry = NULL;
  GESAssetPrivate *priv = asset->priv;

  if (priv->state != ASSET_INITIALIZED) {
    GST_WARNING_OBJECT (asset, "Trying to rest ID on an object that is"
        " not properly loaded");
    return;
  }

  if (g_strcmp0 (id, priv->id) == 0) {
    GST_DEBUG_OBJECT (asset, "ID is already %s", id);

    return;
  }

  LOCK_CACHE;
  entries = g_hash_table_lookup (type_entries_table,
      _extractable_type_name (asset->priv->extractable_type));

  g_hash_table_lookup_extended (entries, priv->id, &orig_id,
      (gpointer *) & entry);

  g_hash_table_steal (entries, priv->id);
  g_hash_table_insert (entries, g_strdup (id), entry);

  GST_DEBUG_OBJECT (asset, "Changing id from %s to %s", priv->id, id);
  g_free (priv->id);
  g_free (orig_id);
  priv->id = g_strdup (id);
  UNLOCK_CACHE;
}

static GESAsset *
_unsure_material_for_wrong_id (const gchar * wrong_id, GType extractable_type,
    GError * error)
{
  GESAsset *asset;

  if ((asset = ges_asset_cache_lookup (extractable_type, wrong_id)))
    return asset;

  /* It is a dummy GESAsset, we just bruteforce its creation */
  asset = g_object_new (GES_TYPE_ASSET, "id", wrong_id,
      "extractable-type", extractable_type, NULL);

  ges_asset_cache_put (asset, NULL);
  ges_asset_cache_set_loaded (extractable_type, wrong_id, error);

  return asset;
}

/**********************************
 *                                *
 *      API implementation        *
 *                                *
 **********************************/

/**
 * ges_asset_get_extractable_type:
 * @self: The #GESAsset
 *
 * Gets the type of object that can be extracted from @self
 *
 * Returns: the type of object that can be extracted from @self
 */
GType
ges_asset_get_extractable_type (GESAsset * self)
{
  return self->priv->extractable_type;
}

/**
 * ges_asset_request:
 * @extractable_type: The #GType of the object that can be extracted from the new asset.
 * @id: (allow-none): The Identifier or %NULL
 * @error: (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Create a #GESAsset in the most simple cases, you should look at the @extractable_type
 * documentation to see if that constructor can be called for this particular type
 *
 * Note that it won't be possible to instantiate the first %GESAsset with
 * @id depending on the @extractable_type. For example instantiate a
 * #GESAsset that extract #GESUriClip needs to be done async
 * the first time for a specific ID.
 *
 * Returns: (transfer full) (allow-none): A reference to the wanted #GESAsset or %NULL
 */
GESAsset *
ges_asset_request (GType extractable_type, const gchar * id, GError ** error)
{
  gchar *real_id;

  GError *lerr = NULL;
  GESAsset *asset = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, G_TYPE_OBJECT), NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      NULL);

  real_id = _check_and_update_parameters (&extractable_type, id, &lerr);
  if (real_id == NULL) {
    /* We create an asset for that wrong ID so we have a reference that the
     * user requested it */
    _unsure_material_for_wrong_id (id, extractable_type, lerr);
    real_id = g_strdup (id);
  }

  asset = ges_asset_cache_lookup (extractable_type, real_id);
  if (asset) {
    while (TRUE) {
      switch (asset->priv->state) {
        case ASSET_INITIALIZED:
          gst_object_ref (asset);
          goto done;
        case ASSET_INITIALIZING:
          asset = NULL;
          goto done;
        case ASSET_PROXIED:
          asset =
              ges_asset_cache_lookup (asset->priv->extractable_type,
              asset->priv->proxied_asset_id);
          if (asset == NULL) {
            GST_ERROR ("Asset against a asset we do not"
                " have in cache, something massively screwed");

            goto done;
          }
          break;
        case ASSET_INITIALIZED_WITH_ERROR:
          GST_WARNING_OBJECT (asset, "Initialized with error, not returning");
          if (error)
            *error = g_error_copy (asset->priv->error);
          asset = NULL;
          goto done;
        default:
          GST_WARNING ("Case %i not handle, returning", asset->priv->state);
          goto done;
      }
    }
  } else {
    GObjectClass *klass;
    GInitableIface *iface;
    GType asset_type = ges_extractable_type_get_asset_type (extractable_type);

    klass = g_type_class_ref (asset_type);
    iface = g_type_interface_peek (klass, G_TYPE_INITABLE);

    if (iface->init) {
      asset = g_initable_new (asset_type,
          NULL, NULL, "id", real_id, "extractable-type",
          extractable_type, NULL);
    } else {
      GST_WARNING ("Tried to create an Asset for type %s but no ->init method",
          g_type_name (extractable_type));
    }
    g_type_class_unref (klass);
  }

done:
  if (real_id)
    g_free (real_id);

  GST_DEBUG ("New asset created synchronously: %p", asset);
  return asset;
}

/**
 * ges_asset_request_async:
 * @extractable_type: The #GType of the object that can be extracted from the
 *    new asset. The class must implement the #GESExtractable interface.
 * @id: The Identifier of the asset we want to create. This identifier depends of the extractable,
 * type you want. By default it is the name of the class itself (or %NULL), but for example for a
 * GESTrackParseLaunchEffect, it will be the pipeline description, for a GESUriClip it
 * will be the name of the file, etc... You should refer to the documentation of the #GESExtractable
 * type you want to create a #GESAsset for.
 * @cancellable: (allow-none): optional %GCancellable object, %NULL to ignore.
 * @callback: a #GAsyncReadyCallback to call when the initialization is finished,
 * Note that the @source of the callback will be the #GESAsset, but you need to
 * make sure that the asset is properly loaded using the #ges_asset_request_finish
 * method. This asset can not be used as is.
 * @user_data: The user data to pass when @callback is called
 *
 * Request a new #GESAsset asyncronously, @callback will be called when the materail is
 * ready to be used or if an error occured.
 *
 * Example of request of a GESAsset async:
 * |[
 * // The request callback
 * static void
 * asset_loaded_cb (GESAsset * source, GAsyncResult * res, gpointer user_data)
 * {
 *   GESAsset *asset;
 *   GError *error = NULL;
 *
 *   asset = ges_asset_request_finish (res, &error);
 *   if (asset) {
 *    g_print ("The file: %s is usable as a FileSource",
 *        ges_asset_get_id (asset));
 *   } else {
 *    g_print ("The file: %s is *not* usable as a FileSource because: %s",
 *        ges_asset_get_id (source), error->message);
 *   }
 *
 *   g_object_unref (mfs);
 * }
 *
 * // The request:
 * ges_asset_request_async (GES_TYPE_URI_CLIP, some_uri, NULL,
 *    (GAsyncReadyCallback) asset_loaded_cb, user_data);
 * ]|
 */
void
ges_asset_request_async (GType extractable_type,
    const gchar * id, GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  gchar *real_id;
  GESAsset *asset;
  GError *error = NULL;

  g_return_if_fail (g_type_is_a (extractable_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE));
  g_return_if_fail (callback);

  GST_DEBUG ("Creating asset with extractable type %s and ID=%s",
      g_type_name (extractable_type), id);

  real_id = _check_and_update_parameters (&extractable_type, id, &error);
  if (error) {
    _unsure_material_for_wrong_id (id, extractable_type, error);
    real_id = g_strdup (id);
  }

  /* Check if we already have a asset for this ID */
  asset = ges_asset_cache_lookup (extractable_type, real_id);
  if (asset) {
    GSimpleAsyncResult *simple = g_simple_async_result_new (G_OBJECT (asset),
        callback, user_data, ges_asset_request_async);

    /* In the case of proxied asset, we will loop until we find the
     * last asset of the chain of proxied asset */
    while (TRUE) {
      switch (asset->priv->state) {
        case ASSET_INITIALIZED:
          gst_object_ref (asset);

          GST_DEBUG_OBJECT (asset, "Asset in cache and initialized, "
              "using it");

          /* Takes its own references to @asset */
          g_simple_async_result_complete_in_idle (simple);

          goto done;
        case ASSET_INITIALIZING:
          GST_DEBUG_OBJECT (asset, "Asset in cache and but not "
              "initialized, setting a new callback");
          ges_asset_cache_append_result (extractable_type, real_id, simple);

          goto done;
        case ASSET_PROXIED:
          asset =
              ges_asset_cache_lookup (asset->priv->extractable_type,
              asset->priv->proxied_asset_id);
          if (asset == NULL) {
            GST_ERROR ("Asset proxied against a asset we do not"
                " have in cache, something massively screwed");

            goto done;
          }
          break;
        case ASSET_INITIALIZED_WITH_ERROR:
          g_simple_async_report_gerror_in_idle (G_OBJECT (asset), callback,
              user_data, error ? error : asset->priv->error);

          if (error)
            g_error_free (error);
          goto done;
        default:
          GST_WARNING ("Case %i not handle, returning", asset->priv->state);
          return;
      }
    }
  }

  g_async_initable_new_async (ges_extractable_type_get_asset_type
      (extractable_type), G_PRIORITY_DEFAULT, cancellable, callback, user_data,
      "id", real_id, "extractable-type", extractable_type, NULL);
done:
  if (real_id)
    g_free (real_id);
}

/**
 * ges_asset_get_id:
 * @self: The #GESAsset to get ID from
 *
 * Gets the ID of a #GESAsset
 *
 * Returns: The ID of @self
 */
const gchar *
ges_asset_get_id (GESAsset * self)
{
  g_return_val_if_fail (GES_IS_ASSET (self), NULL);

  return self->priv->id;
}

/**
 * ges_asset_extract:
 * @self: The #GESAsset to get extract an object from
 * @error: (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Extracts a new #GObject from @asset. The type of the object is
 * defined by the extractable-type of @asset, you can check what
 * type will be extracted from @asset using
 * #ges_asset_get_extractable_type
 *
 * Returns: (transfer floating) (allow-none): A newly created #GESExtractable
 */
GESExtractable *
ges_asset_extract (GESAsset * self, GError ** error)
{
  GESExtractable *extractable;

  g_return_val_if_fail (GES_IS_ASSET (self), NULL);
  g_return_val_if_fail (GES_ASSET_GET_CLASS (self)->extract, NULL);

  GST_DEBUG_OBJECT (self, "Extracting asset of type %s",
      g_type_name (self->priv->extractable_type));
  extractable = GES_ASSET_GET_CLASS (self)->extract (self, error);

  if (extractable == NULL)
    return NULL;

  if (ges_extractable_get_asset (extractable) == NULL)
    ges_extractable_set_asset (extractable, self);

  return extractable;
}

/**
 * ges_asset_request_finish:
 * @res: The #GAsyncResult from which to get the newly created #GESAsset
 * @error: (out) (allow-none) (transfer full): An error to be set in case
 * something wrong happens or %NULL
 *
 * Finalize the request of an async #GESAsset
 *
 * Returns: (transfer full)(allow-none): The #GESAsset previously requested
 */
GESAsset *
ges_asset_request_finish (GAsyncResult * res, GError ** error)
{
  GObject *object;
  GObject *source_object;

  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);

  source_object = g_async_result_get_source_object (res);
  g_assert (source_object != NULL);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
      res, error);

  g_object_unref (source_object);

  return GES_ASSET (object);
}

/**
 * ges_list_assets:
 * @filter: Type of assets to list, #GES_TYPE_EXTRACTABLE  will list
 * all assets
 *
 * List all @asset filtering per filter as defined by @filter.
 * It copies the asset and thus will not be updated in time.
 *
 * Returns: (transfer container) (element-type GESAsset): The list of
 * #GESAsset the object contains
 */
GList *
ges_list_assets (GType filter)
{
  GList *ret = NULL;
  GESAsset *asset;
  GHashTableIter iter, types_iter;
  gpointer key, value, typename, assets;

  g_return_val_if_fail (g_type_is_a (filter, GES_TYPE_EXTRACTABLE), NULL);

  LOCK_CACHE;
  g_hash_table_iter_init (&types_iter, type_entries_table);
  while (g_hash_table_iter_next (&types_iter, &typename, &assets)) {
    if (g_type_is_a (filter, g_type_from_name ((gchar *) typename)) == FALSE)
      continue;

    g_hash_table_iter_init (&iter, (GHashTable *) assets);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      asset = ((GESAssetCacheEntry *) value)->asset;

      if (g_type_is_a (asset->priv->extractable_type, filter))
        ret = g_list_append (ret, asset);
    }
  }
  UNLOCK_CACHE;

  return ret;
}
