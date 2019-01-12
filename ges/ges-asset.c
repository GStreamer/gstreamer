/* GStreamer Editing Services
 *
 * Copyright (C) 2012-2015 Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION: gesasset
 * @title: GESAsset
 * @short_description: Represents usable resources inside the GStreamer Editing Services
 *
 * The Assets in the GStreamer Editing Services represent the resources
 * that can be used. You can create assets for any type that implements the #GESExtractable
 * interface, for example #GESClips, #GESFormatter, and #GESTrackElement do implement it.
 * This means that assets will represent for example a #GESUriClips, #GESBaseEffect etc,
 * and then you can extract objects of those types with the appropriate parameters from the asset
 * using the #ges_asset_extract method:
 *
 * |[
 * GESAsset *effect_asset;
 * GESEffect *effect;
 *
 * // You create an asset for an effect
 * effect_asset = ges_asset_request (GES_TYPE_EFFECT, "agingtv", NULL);
 *
 * // And now you can extract an instance of GESEffect from that asset
 * effect = GES_EFFECT (ges_asset_extract (effect_asset));
 *
 * ]|
 *
 * In that example, the advantages of having a #GESAsset are that you can know what effects
 * you are working with and let your user know about the avalaible ones, you can add metadata
 * to the #GESAsset through the #GESMetaContainer interface and you have a model for your
 * custom effects. Note that #GESAsset management is making easier thanks to the #GESProject class.
 *
 * Each asset is represented by a pair of @extractable_type and @id (string). Actually the @extractable_type
 * is the type that implements the #GESExtractable interface, that means that for example for a #GESUriClip,
 * the type that implements the #GESExtractable interface is #GESClip.
 * The identifier represents different things depending on the @extractable_type and you should check
 * the documentation of each type to know what the ID of #GESAsset actually represents for that type. By default,
 * we only have one #GESAsset per type, and the @id is the name of the type, but this behaviour is overriden
 * to be more useful. For example, for GESTransitionClips, the ID is the vtype of the transition
 * you will extract from it (ie crossfade, box-wipe-rc etc..) For #GESEffect the ID is the
 * @bin-description property of the extracted objects (ie the gst-launch style description of the bin that
 * will be used).
 *
 * Each and every #GESAsset is cached into GES, and you can query those with the #ges_list_assets function.
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
 * You can request the creation of #GESAssets using either ges_asset_request() or
 * ges_asset_request_async(). All the #GESAssets are cached and thus any asset that has already
 * been created can be requested again without overhead.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges.h"
#include "ges-internal.h"

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (ges_asset_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_asset_debug

enum
{
  PROP_0,
  PROP_TYPE,
  PROP_ID,
  PROP_PROXY,
  PROP_PROXY_TARGET,
  PROP_LAST
};

typedef enum
{
  ASSET_NOT_INITIALIZED,
  ASSET_INITIALIZING, ASSET_INITIALIZED_WITH_ERROR,
  ASSET_PROXIED,
  ASSET_NEEDS_RELOAD,
  ASSET_INITIALIZED
} GESAssetState;

static GParamSpec *_properties[PROP_LAST];

struct _GESAssetPrivate
{
  gchar *id;
  GESAssetState state;
  GType extractable_type;

  /* When an asset is proxied, instantiating it will
   * return the asset it points to */
  char *proxied_asset_id;

  GList *proxies;
  GESAsset *proxy_target;

  /* The error that accured when an asset has been initialized with error */
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
G_LOCK_DEFINE_STATIC (asset_cache_lock);
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
#define LOCK_CACHE   (G_LOCK (asset_cache_lock))
#define UNLOCK_CACHE (G_UNLOCK (asset_cache_lock))

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
      g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
          "Wrong ID, can not find any extractable_type");
    return NULL;
  }

  real_id = ges_extractable_type_check_id (*extractable_type, id, error);
  if (real_id == NULL) {
    GST_WARNING ("Wrong ID %s, can not create asset", id);

    g_free (real_id);
    if (error && *error == NULL)
      g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID, "Wrong ID");

    return NULL;
  }

  return real_id;
}

static gboolean
start_loading (GESAsset * asset)
{
  ges_asset_cache_put (gst_object_ref (asset), NULL);
  return ges_asset_cache_set_loaded (asset->priv->extractable_type,
      asset->priv->id, NULL);
}

static gboolean
initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  g_clear_error (error);

  return start_loading (GES_ASSET (initable));
}

static void
async_initable_init_async (GAsyncInitable * initable, gint io_priority,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  GTask *task;

  GError *error = NULL;
  GESAsset *asset = GES_ASSET (initable);

  task = g_task_new (asset, cancellable, callback, user_data);

  ges_asset_cache_put (g_object_ref (asset), task);
  switch (GES_ASSET_GET_CLASS (asset)->start_loading (asset, &error)) {
    case GES_ASSET_LOADING_ERROR:
    {
      if (error == NULL)
        g_set_error (&error, GES_ERROR, GES_ERROR_ASSET_LOADING,
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
    G_ADD_PRIVATE (GESAsset)
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

#if GLIB_CHECK_VERSION(2, 53, 1)
  {
    gint i;
    GValue *values;
    const gchar **names;

    values = g_malloc0 (sizeof (GValue) * n_params);
    names = g_malloc0 (sizeof (gchar *) * n_params);

    for (i = 0; i < n_params; i++) {
      values[i] = params[i].value;
      names[i] = params[i].name;
    }

    n_extractable =
        GES_EXTRACTABLE (g_object_new_with_properties (priv->extractable_type,
            n_params, names, values));
    g_free (names);
    g_free (values);
  }
#else
  n_extractable = g_object_newv (priv->extractable_type, n_params, params);
#endif

  while (n_params--)
    g_value_unset (&params[n_params].value);

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
    case PROP_PROXY:
      g_value_set_object (value, ges_asset_get_proxy (asset));
      break;
    case PROP_PROXY_TARGET:
      g_value_set_object (value, ges_asset_get_proxy_target (asset));
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
    case PROP_PROXY:
      ges_asset_set_proxy (asset, g_value_get_object (value));
      break;
    case PROP_PROXY_TARGET:
      ges_asset_set_proxy (g_value_get_object (value), asset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_finalize (GObject * object)
{
  GESAssetPrivate *priv = GES_ASSET (object)->priv;

  GST_DEBUG_OBJECT (object, "finalizing");

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

  _properties[PROP_PROXY] =
      g_param_spec_object ("proxy", "Proxy",
      "The asset default proxy.", GES_TYPE_ASSET, G_PARAM_READWRITE);

  _properties[PROP_PROXY_TARGET] =
      g_param_spec_object ("proxy-target", "Proxy target",
      "The target of a proxy asset.", GES_TYPE_ASSET, G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, _properties);

  klass->start_loading = ges_asset_start_loading_default;
  klass->extract = ges_asset_extract_default;
  klass->request_id_update = ges_asset_request_id_update_default;
  klass->inform_proxy = NULL;

  GST_DEBUG_CATEGORY_INIT (ges_asset_debug, "ges-asset",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GES Asset");
}

void
ges_asset_init (GESAsset * self)
{
  self->priv = ges_asset_get_instance_private (self);

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

static void
_gtask_return_error (GTask * task, GError * error)
{
  g_task_return_error (task, g_error_copy (error));
}

static void
_gtask_return_true (GTask * task, gpointer udata)
{
  g_task_return_boolean (task, TRUE);
}

/**
 * ges_asset_cache_lookup:
 *
 * @id String identifier of asset
 *
 * Looks for asset with specified id in cache and it's completely loaded.
 *
 * Returns: (transfer none) (nullable): The #GESAsset found or %NULL
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
ges_asset_cache_append_task (GType extractable_type,
    const gchar * id, GTask * task)
{
  GESAssetCacheEntry *entry = NULL;

  LOCK_CACHE;
  if ((entry = _lookup_entry (extractable_type, id)))
    entry->results = g_list_append (entry->results, task);
  UNLOCK_CACHE;
}

gboolean
ges_asset_cache_set_loaded (GType extractable_type, const gchar * id,
    GError * error)
{
  GESAsset *asset;
  GESAssetCacheEntry *entry = NULL;
  GList *results = NULL;
  GFunc user_func = NULL;
  gpointer user_data = NULL;

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

  results = entry->results;
  entry->results = NULL;

  if (error) {
    asset->priv->state = ASSET_INITIALIZED_WITH_ERROR;
    if (asset->priv->error)
      g_error_free (asset->priv->error);
    asset->priv->error = g_error_copy (error);

    /* In case of error we do not want to emit in idle as we need to recover
     * if possible */
    user_func = (GFunc) _gtask_return_error;
    user_data = error;
    GST_DEBUG_OBJECT (asset, "initialized with error");
  } else {
    asset->priv->state = ASSET_INITIALIZED;
    user_func = (GFunc) _gtask_return_true;
    GST_DEBUG_OBJECT (asset, "initialized");
  }
  UNLOCK_CACHE;

  g_list_foreach (results, user_func, user_data);
  g_list_free_full (results, g_object_unref);

  return TRUE;
}

void
ges_asset_cache_put (GESAsset * asset, GTask * task)
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
    if (task)
      entry->results = g_list_prepend (entry->results, task);
    g_hash_table_insert (entries_table, (gpointer) g_strdup (asset_id),
        (gpointer) entry);
  } else {
    if (task) {
      GST_DEBUG ("%s already in cache, adding result %p", asset_id, task);
      entry->results = g_list_prepend (entry->results, task);
    }
  }
  UNLOCK_CACHE;
}

void
ges_asset_cache_init (void)
{
  type_entries_table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_hash_table_unref);

  _init_formatter_assets ();
  _init_standard_transition_assets ();
}

gboolean
ges_asset_request_id_update (GESAsset * asset, gchar ** proposed_id,
    GError * error)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);

  return GES_ASSET_GET_CLASS (asset)->request_id_update (asset, proposed_id,
      error);
}

gboolean
ges_asset_try_proxy (GESAsset * asset, const gchar * new_id)
{
  GESAssetClass *class;

  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);

  if (g_strcmp0 (asset->priv->id, new_id) == 0) {
    GST_WARNING_OBJECT (asset, "Trying to proxy to itself (%s),"
        " NOT possible", new_id);

    return FALSE;
  } else if (g_strcmp0 (asset->priv->proxied_asset_id, new_id) == 0) {
    GST_WARNING_OBJECT (asset,
        "Trying to proxy to same currently set proxy: %s -- %s",
        asset->priv->proxied_asset_id, new_id);

    return FALSE;
  }

  g_free (asset->priv->proxied_asset_id);
  asset->priv->state = ASSET_PROXIED;
  asset->priv->proxied_asset_id = g_strdup (new_id);

  class = GES_ASSET_GET_CLASS (asset);
  if (class->inform_proxy)
    GES_ASSET_GET_CLASS (asset)->inform_proxy (asset, new_id);

  GST_DEBUG_OBJECT (asset, "Trying to proxy to %s", new_id);

  return TRUE;
}

static gboolean
_lookup_proxied_asset (const gchar * id, GESAssetCacheEntry * entry,
    const gchar * asset_id)
{
  return !g_strcmp0 (asset_id, entry->asset->priv->proxied_asset_id);
}

/**
 * ges_asset_set_proxy:
 * @asset: The #GESAsset to set proxy on
 * @proxy: (allow-none): The #GESAsset that should be used as default proxy for @asset or
 * %NULL if you want to use the currently set proxy. Note that an asset can proxy one and only
 * one other asset.
 *
 * A proxying asset is an asset that can substitue the real @asset. For example if you
 * have a full HD #GESUriClipAsset you might want to set a lower resolution (HD version
 * of the same file) as proxy. Note that when an asset is proxied, calling
 * #ges_asset_request will actually return the proxy asset.
 *
 * Returns: %TRUE if @proxy has been set on @asset, %FALSE otherwise.
 */
gboolean
ges_asset_set_proxy (GESAsset * asset, GESAsset * proxy)
{
  g_return_val_if_fail (asset == NULL || GES_IS_ASSET (asset), FALSE);
  g_return_val_if_fail (proxy == NULL || GES_IS_ASSET (proxy), FALSE);
  g_return_val_if_fail (asset != proxy, FALSE);

  if (!proxy) {
    if (asset->priv->error) {
      GST_ERROR_OBJECT (asset,
          "Proxy was loaded with error (%s), it should not be 'unproxied'",
          asset->priv->error->message);

      return FALSE;
    }

    if (asset->priv->proxies) {
      GESAsset *old_proxy = GES_ASSET (asset->priv->proxies->data);

      old_proxy->priv->proxy_target = NULL;
      g_object_notify_by_pspec (G_OBJECT (old_proxy),
          _properties[PROP_PROXY_TARGET]);
    }

    GST_DEBUG_OBJECT (asset, "%s not proxied anymore", asset->priv->id);
    asset->priv->state = ASSET_INITIALIZED;
    g_object_notify_by_pspec (G_OBJECT (asset), _properties[PROP_PROXY]);

    return TRUE;
  }

  if (asset == NULL) {
    GHashTable *entries_table;
    GESAssetCacheEntry *entry;

    entries_table = g_hash_table_lookup (type_entries_table,
        _extractable_type_name (proxy->priv->extractable_type));
    entry = g_hash_table_find (entries_table, (GHRFunc) _lookup_proxied_asset,
        (gpointer) ges_asset_get_id (proxy));

    if (!entry) {
      GST_DEBUG_OBJECT (asset, "Not proxying any asset");
      return FALSE;
    }

    asset = entry->asset;
    while (asset->priv->proxies)
      asset = asset->priv->proxies->data;
  }

  if (proxy->priv->proxy_target) {
    GST_ERROR_OBJECT (asset,
        "Trying to use %s as a proxy, but it is already proxying %s",
        proxy->priv->id, proxy->priv->proxy_target->priv->id);

    return FALSE;
  }

  if (g_list_find (proxy->priv->proxies, asset)) {
    GST_ERROR_OBJECT (asset, "Trying to setup a circular proxying dependency!");

    return FALSE;
  }

  if (g_list_find (asset->priv->proxies, proxy)) {
    GST_INFO_OBJECT (asset,
        "%" GST_PTR_FORMAT " already marked as proxy, moving to first", proxy);
    GES_ASSET (asset->priv->proxies->data)->priv->proxy_target = NULL;
    asset->priv->proxies = g_list_remove (asset->priv->proxies, proxy);
  }

  GST_INFO ("%s is now proxied by %s", asset->priv->id, proxy->priv->id);
  asset->priv->proxies = g_list_prepend (asset->priv->proxies, proxy);
  proxy->priv->proxy_target = asset;
  g_object_notify_by_pspec (G_OBJECT (proxy), _properties[PROP_PROXY_TARGET]);

  asset->priv->state = ASSET_PROXIED;
  g_object_notify_by_pspec (G_OBJECT (asset), _properties[PROP_PROXY]);

  return TRUE;
}

/**
 * ges_asset_unproxy:
 * @asset: The #GESAsset to stop proxying with @proxy
 * @proxy: The #GESAsset to stop considering as a proxy for @asset
 *
 * Removes @proxy from the list of known proxies for @asset.
 * If @proxy was the current proxy for @asset, stop using it.
 *
 * Returns: %TRUE if @proxy was a known proxy for @asset, %FALSE otherwise.
 */
gboolean
ges_asset_unproxy (GESAsset * asset, GESAsset * proxy)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);
  g_return_val_if_fail (GES_IS_ASSET (proxy), FALSE);
  g_return_val_if_fail (asset != proxy, FALSE);

  if (!g_list_find (asset->priv->proxies, proxy)) {
    GST_INFO_OBJECT (asset, "%s is not a proxy.", proxy->priv->id);

    return FALSE;
  }

  if (asset->priv->proxies->data == proxy)
    ges_asset_set_proxy (asset, NULL);

  asset->priv->proxies = g_list_remove (asset->priv->proxies, proxy);

  return TRUE;
}

/**
 * ges_asset_list_proxies:
 * @asset: The #GESAsset to get proxies from
 *
 * Returns: (element-type GESAsset) (transfer none): The list of proxies @asset has. Note that the default asset to be
 * used is always the first in that list.
 */
GList *
ges_asset_list_proxies (GESAsset * asset)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);

  return asset->priv->proxies;
}

/**
 * ges_asset_get_proxy:
 * @asset: The #GESAsset to get currenlty used proxy
 *
 * Returns: (transfer none) (nullable): The proxy in use for @asset
 */
GESAsset *
ges_asset_get_proxy (GESAsset * asset)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);

  if (asset->priv->state == ASSET_PROXIED && asset->priv->proxies) {
    return asset->priv->proxies->data;
  }

  return NULL;
}

/**
 * ges_asset_get_proxy_target:
 * @proxy: The #GESAsset from which to get the the asset it proxies.
 *
 * Returns: (transfer none) (nullable): The #GESAsset that is proxied by @proxy
 */
GESAsset *
ges_asset_get_proxy_target (GESAsset * proxy)
{
  g_return_val_if_fail (GES_IS_ASSET (proxy), NULL);

  return proxy->priv->proxy_target;
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
  GESAssetPrivate *priv = NULL;

  g_return_if_fail (GES_IS_ASSET (asset));

  priv = asset->priv;

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

  g_return_if_fail (g_hash_table_lookup_extended (entries, priv->id, &orig_id,
          (gpointer *) & entry));

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
  g_return_val_if_fail (GES_IS_ASSET (self), G_TYPE_INVALID);

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
 * As it is recommanded not to instanciate assets for GESUriClip synchronously,
 * it will not work with this method, but you can instead use the specific
 * #ges_uri_clip_asset_request_sync method if you really want to.
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
  if (lerr)
    g_error_free (lerr);

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
          if (asset->priv->proxies == NULL) {
            GST_ERROR ("Proxied against an asset we do not"
                " have in cache, something massively screwed");

            goto done;
          }

          asset = asset->priv->proxies->data;
          while (ges_asset_get_proxy (asset))
            asset = ges_asset_get_proxy (asset);

          break;
        case ASSET_NEEDS_RELOAD:
          GST_DEBUG_OBJECT (asset, "Asset in cache and needs reload");
          start_loading (asset);

          goto done;
        case ASSET_INITIALIZED_WITH_ERROR:
          GST_WARNING_OBJECT (asset, "Initialized with error, not returning");
          if (asset->priv->error && error)
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
 * GESEffect, it will be the pipeline description, for a GESUriClip it
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
 *   gst_object_unref (mfs);
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

  /* Check if we already have an asset for this ID */
  asset = ges_asset_cache_lookup (extractable_type, real_id);
  if (asset) {
    GTask *task = g_task_new (asset, NULL, callback, user_data);

    /* In the case of proxied asset, we will loop until we find the
     * last asset of the chain of proxied asset */
    while (TRUE) {
      switch (asset->priv->state) {
        case ASSET_INITIALIZED:
          gst_object_ref (asset);

          GST_DEBUG_OBJECT (asset, "Asset in cache and initialized, "
              "using it");

          /* Takes its own references to @asset */
          g_task_return_boolean (task, TRUE);

          goto done;
        case ASSET_INITIALIZING:
          GST_DEBUG_OBJECT (asset, "Asset in cache and but not "
              "initialized, setting a new callback");
          ges_asset_cache_append_task (extractable_type, real_id, task);

          goto done;
        case ASSET_PROXIED:{
          GESAsset *target = ges_asset_get_proxy (asset);

          if (target == NULL) {
            GST_ERROR ("Asset %s proxied against an asset (%s) we do not"
                " have in cache, something massively screwed",
                asset->priv->id, asset->priv->proxied_asset_id);

            goto done;
          }
          asset = target;
          break;
        }
        case ASSET_NEEDS_RELOAD:
          GST_DEBUG_OBJECT (asset, "Asset in cache and needs reload");
          ges_asset_cache_append_task (extractable_type, real_id, task);
          GES_ASSET_GET_CLASS (asset)->start_loading (asset, &error);

          goto done;
        case ASSET_INITIALIZED_WITH_ERROR:
          g_task_return_error (task,
              error ? error : g_error_copy (asset->priv->error));

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
 * ges_asset_needs_reload
 * @extractable_type: The #GType of the object that can be extracted from the
 *  asset to be reloaded.
 * @id: The identifier of the asset to mark as needing reload
 *
 * Sets an asset from the internal cache as needing reload. An asset needs reload
 * in the case where, for example, we were missing a GstPlugin to use it and that
 * plugin has been installed, or, that particular asset content as changed
 * meanwhile (in the case of the usage of proxies).
 *
 * Once an asset has been set as "needs reload", requesting that asset again
 * will lead to it being re discovered, and reloaded as if it was not in the
 * cache before.
 *
 * Returns: %TRUE if the asset was in the cache and could be set as needing reload,
 * %FALSE otherwise.
 */
gboolean
ges_asset_needs_reload (GType extractable_type, const gchar * id)
{
  gchar *real_id;
  GESAsset *asset;
  GError *error = NULL;

  real_id = _check_and_update_parameters (&extractable_type, id, &error);
  if (error) {
    _unsure_material_for_wrong_id (id, extractable_type, error);
    real_id = g_strdup (id);
  }

  asset = ges_asset_cache_lookup (extractable_type, real_id);

  if (real_id) {
    g_free (real_id);
  }

  if (asset) {
    GST_DEBUG_OBJECT (asset,
        "Asset with id %s switch state to ASSET_NEEDS_RELOAD",
        ges_asset_get_id (asset));
    asset->priv->state = ASSET_NEEDS_RELOAD;
    g_clear_error (&asset->priv->error);
    return TRUE;
  }

  GST_DEBUG ("Asset with id %s not found in cache", id);
  return FALSE;
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

  gst_object_unref (source_object);

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

/**
 * ges_asset_get_error:
 * @self: The asset to retrieve the error from
 *
 * Returns: (transfer none) (nullable): The #GError of the asset or %NULL if
 * the asset was loaded without issue
 *
 * Since: 1.8
 */
GError *
ges_asset_get_error (GESAsset * self)
{
  g_return_val_if_fail (GES_IS_ASSET (self), NULL);

  return self->priv->error;
}
