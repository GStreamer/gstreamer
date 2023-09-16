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
 * @short_description: Represents usable resources inside the GStreamer
 * Editing Services
 *
 * A #GESAsset in the GStreamer Editing Services represents a resources
 * that can be used. In particular, any class that implements the
 * #GESExtractable interface may have some associated assets with a
 * corresponding #GESAsset:extractable-type, from which its objects can be
 * extracted using ges_asset_extract(). Some examples would be
 * #GESClip, #GESFormatter and #GESTrackElement.
 *
 * All assets that are created within GES are stored in a cache; one per
 * each #GESAsset:id and #GESAsset:extractable-type pair. These assets can
 * be fetched, and initialized if they do not yet exist in the cache,
 * using ges_asset_request().
 *
 * ``` c
 * GESAsset *effect_asset;
 * GESEffect *effect;
 *
 * // You create an asset for an effect
 * effect_asset = ges_asset_request (GES_TYPE_EFFECT, "agingtv", NULL);
 *
 * // And now you can extract an instance of GESEffect from that asset
 * effect = GES_EFFECT (ges_asset_extract (effect_asset));
 *
 * ```
 *
 * The advantage of using assets, rather than simply creating the object
 * directly, is that the currently loaded resources can be listed with
 * ges_list_assets() and displayed to an end user. For example, to show
 * which media files have been loaded, and a standard list of effects. In
 * fact, the GES library already creates assets for #GESTransitionClip and
 * #GESFormatter, which you can use to list all the available transition
 * types and supported formats.
 *
 * The other advantage is that #GESAsset implements #GESMetaContainer, so
 * metadata can be set on the asset, with some subclasses automatically
 * creating this metadata on initiation.
 *
 * For example, to display information about the supported formats, you
 * could do the following:
 * |[
 *    GList *formatter_assets, *tmp;
 *
 *    //  List all  the transitions
 *    formatter_assets = ges_list_assets (GES_TYPE_FORMATTER);
 *
 *    // Print some infos about the formatter GESAsset
 *    for (tmp = formatter_assets; tmp; tmp = tmp->next) {
 *      gst_print ("Name of the formatter: %s, file extension it produces: %s",
 *        ges_meta_container_get_string (
 *          GES_META_CONTAINER (tmp->data), GES_META_FORMATTER_NAME),
 *        ges_meta_container_get_string (
 *          GES_META_CONTAINER (tmp->data), GES_META_FORMATTER_EXTENSION));
 *    }
 *
 *    g_list_free (transition_assets);
 *
 * ]|
 *
 * ## ID
 *
 * Each asset is uniquely defined in the cache by its
 * #GESAsset:extractable-type and #GESAsset:id. Depending on the
 * #GESAsset:extractable-type, the #GESAsset:id can be used to parametrise
 * the creation of the object upon extraction. By default, a class that
 * implements #GESExtractable will only have a single associated asset,
 * with an #GESAsset:id set to the type name of its objects. However, this
 * is overwritten by some implementations, which allow a class to have
 * multiple associated assets. For example, for #GESTransitionClip the
 * #GESAsset:id will be a nickname of the #GESTransitionClip:vtype. You
 * should check the documentation for each extractable type to see if they
 * differ from the default.
 *
 * Moreover, each #GESAsset:extractable-type may also associate itself
 * with a specific asset subclass. In such cases, when their asset is
 * requested, an asset of this subclass will be returned instead.
 *
 * ## Managing
 *
 * You can use a #GESProject to easily manage the assets of a
 * #GESTimeline.
 *
 * ## Proxies
 *
 * Some assets can (temporarily) act as the #GESAsset:proxy of another
 * asset. When the original asset is requested from the cache, the proxy
 * will be returned in its place. This can be useful if, say, you want
 * to substitute a #GESUriClipAsset corresponding to a high resolution
 * media file with the asset of a lower resolution stand in.
 *
 * An asset may even have several proxies, the first of which will act as
 * its default and be returned on requests, but the others will be ordered
 * to take its place once it is removed. You can add a proxy to an asset,
 * or set its default, using ges_asset_set_proxy(), and you can remove
 * them with ges_asset_unproxy().
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

  /* used internally by try_proxy to pre-set a proxy whilst an asset is
   * still loading. It can be used later to set the proxy for the asset
   * once it has finished loading */
  char *proxied_asset_id;

  /* actual list of proxies */
  GList *proxies;
  /* the asset whose proxies list we belong to */
  GESAsset *proxy_target;

  /* The error that occurred when an asset has been initialized with error */
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
/* Protect all the entries in the cache */
static GRecMutex asset_cache_lock;
#define LOCK_CACHE   (g_rec_mutex_lock (&asset_cache_lock))
#define UNLOCK_CACHE (g_rec_mutex_unlock (&asset_cache_lock))

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

/* FIXME: why are we not accepting a GError ** error argument, which we
 * could pass to ges_asset_cache_set_loaded ()? Which would allow the
 * error to be set for the GInitable init method below */
static gboolean
start_loading (GESAsset * asset)
{
  GInitableIface *iface;

  iface = g_type_interface_peek (GES_ASSET_GET_CLASS (asset), G_TYPE_INITABLE);

  if (!iface->init) {
    GST_INFO_OBJECT (asset, "Can not start loading sync, as no ->init vmethod");
    return FALSE;
  }

  ges_asset_cache_put (gst_object_ref (asset), NULL);
  return ges_asset_cache_set_loaded (asset->priv->extractable_type,
      asset->priv->id, NULL);
}

static gboolean
initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  /* FIXME: Is there actually a reason to be freeing the GError that
   * error points to? */
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

  ges_asset_cache_put (gst_object_ref (asset), task);
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
      /* FIXME: how are user subclasses that implement ->start_loading
       * to return GES_ASSET_LOADING_ASYNC meant to invoke the private
       * method ges_asset_cache_set_loaded once they finish initializing?
       */
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

G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
static GESExtractable *
ges_asset_extract_default (GESAsset * asset, GError ** error)
{
  guint n_params;
  GParameter *params;
  GESAssetPrivate *priv = asset->priv;
  GESExtractable *n_extractable;
  gint i;
  GValue *values;
  const gchar **names;

  params = ges_extractable_type_get_parameters_from_id (priv->extractable_type,
      priv->id, &n_params);


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

  while (n_params--)
    g_value_unset (&params[n_params].value);

  g_free (params);

  return n_extractable;
}

G_GNUC_END_IGNORE_DEPRECATIONS;

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
      /* NOTE: we calling this in the setter so metadata is set on the
       * asset upon initiation, but before it has been loaded. */
      ges_extractable_register_metas (asset->priv->extractable_type, asset);
      break;
    case PROP_ID:
      asset->priv->id = g_value_dup_string (value);
      break;
    case PROP_PROXY:
      ges_asset_set_proxy (asset, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_finalize (GObject * object)
{
  GESAssetPrivate *priv = GES_ASSET (object)->priv;

  GST_LOG_OBJECT (object, "finalizing");

  if (priv->id)
    g_free (priv->id);

  if (priv->proxied_asset_id)
    g_free (priv->proxied_asset_id);

  if (priv->error)
    g_error_free (priv->error);

  if (priv->proxies)
    g_list_free (priv->proxies);

  G_OBJECT_CLASS (ges_asset_parent_class)->finalize (object);
}

void
ges_asset_class_init (GESAssetClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_asset_get_property;
  object_class->set_property = ges_asset_set_property;
  object_class->finalize = ges_asset_finalize;

  /**
   * GESAsset:extractable-type:
   *
   * The #GESExtractable object type that can be extracted from the asset.
   */
  _properties[PROP_TYPE] =
      g_param_spec_gtype ("extractable-type", "Extractable type",
      "The type of the Object that can be extracted out of the asset",
      G_TYPE_OBJECT, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  /**
   * GESAsset:id:
   *
   * The ID of the asset. This should be unique amongst all assets with
   * the same #GESAsset:extractable-type. Depending on the associated
   * #GESExtractable implementation, this id may convey some information
   * about the #GObject that should be extracted. Note that, as such, the
   * ID will have an expected format, and you can not choose this value
   * arbitrarily. By default, this will be set to the type name of the
   * #GESAsset:extractable-type, but you should check the documentation
   * of the extractable type to see whether they differ from the
   * default behaviour.
   */
  _properties[PROP_ID] =
      g_param_spec_string ("id", "Identifier",
      "The unique identifier of the asset", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  /**
   * GESAsset:proxy:
   *
   * The default proxy for this asset, or %NULL if it has no proxy. A
   * proxy will act as a substitute for the original asset when the
   * original is requested (see ges_asset_request()).
   *
   * Setting this property will not usually remove the existing proxy, but
   * will replace it as the default (see ges_asset_set_proxy()).
   */
  _properties[PROP_PROXY] =
      g_param_spec_object ("proxy", "Proxy",
      "The asset default proxy.", GES_TYPE_ASSET, G_PARAM_READWRITE);

  /**
   * GESAsset:proxy-target:
   *
   * The asset that this asset is a proxy for, or %NULL if it is not a
   * proxy for another asset.
   *
   * Note that even if this asset is acting as a proxy for another asset,
   * but this asset is not the default #GESAsset:proxy, then @proxy-target
   * will *still* point to this other asset. So you should check the
   * #GESAsset:proxy property of @target-proxy before assuming it is the
   * current default proxy for the target.
   *
   * Note that the #GObject::notify for this property is emitted after
   * the #GESAsset:proxy #GObject::notify for the corresponding (if any)
   * asset it is now the proxy of/no longer the proxy of.
   */
  _properties[PROP_PROXY_TARGET] =
      g_param_spec_object ("proxy-target", "Proxy target",
      "The target of a proxy asset.", GES_TYPE_ASSET, G_PARAM_READABLE);

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

static inline const gchar *
_extractable_type_name (GType type)
{
  /* We can use `ges_asset_request (GES_TYPE_FORMATTER);` */
  if (g_type_is_a (type, GES_TYPE_FORMATTER))
    return g_type_name (GES_TYPE_FORMATTER);

  return g_type_name (type);
}

static void
ges_asset_cache_init_unlocked (void)
{
  if (type_entries_table)
    return;

  type_entries_table = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_hash_table_unref);

  _init_formatter_assets ();
  _init_standard_transition_assets ();
}


/* WITH LOCK_CACHE */
static GHashTable *
_get_type_entries (void)
{
  if (type_entries_table)
    return type_entries_table;

  ges_asset_cache_init_unlocked ();

  return type_entries_table;
}

/* WITH LOCK_CACHE */
static inline GESAssetCacheEntry *
_lookup_entry (GType extractable_type, const gchar * id)
{
  GHashTable *entries_table;

  entries_table = g_hash_table_lookup (_get_type_entries (),
      _extractable_type_name (extractable_type));
  if (entries_table)
    return g_hash_table_lookup (entries_table, id);

  return NULL;
}

static void
_free_entries (gpointer entry)
{
  GESAssetCacheEntry *data = (GESAssetCacheEntry *) entry;
  if (data->asset)
    gst_object_unref (data->asset);
  g_free (entry);
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

/* transfer full for both @asset and @task */
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

    entries_table = g_hash_table_lookup (_get_type_entries (),
        _extractable_type_name (extractable_type));
    if (entries_table == NULL) {
      entries_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          _free_entries);

      g_hash_table_insert (_get_type_entries (),
          g_strdup (_extractable_type_name (extractable_type)), entries_table);
    }

    entry = g_new0 (GESAssetCacheEntry, 1);

    /* transfer asset to entry */
    entry->asset = asset;
    if (task)
      entry->results = g_list_prepend (entry->results, task);
    g_hash_table_insert (entries_table, (gpointer) g_strdup (asset_id),
        (gpointer) entry);
  } else {
    /* give up the reference we were given */
    gst_object_unref (asset);
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
  LOCK_CACHE;
  ges_asset_cache_init_unlocked ();
  UNLOCK_CACHE;
}

void
ges_asset_cache_deinit (void)
{
  _deinit_formatter_assets ();

  LOCK_CACHE;
  g_hash_table_destroy (type_entries_table);
  type_entries_table = NULL;
  UNLOCK_CACHE;
}

gboolean
ges_asset_request_id_update (GESAsset * asset, gchar ** proposed_id,
    GError * error)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);

  return GES_ASSET_GET_CLASS (asset)->request_id_update (asset, proposed_id,
      error);
}

/* pre-set a proxy id whilst the asset is still loading. Once the proxy
 * is loaded, call ges_asset_finish_proxy (proxy) */
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

  /* FIXME: inform_proxy is not used consistently. For example, it is
   * not called in set_proxy. However, it is still used by GESUriAsset.
   * We should find some other method */
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

/* find the assets that called try_proxy for the asset id of @proxy
 * and set @proxy as their proxy */
gboolean
ges_asset_finish_proxy (GESAsset * proxy)
{
  GESAsset *proxied_asset;
  GHashTable *entries_table;
  GESAssetCacheEntry *entry;

  LOCK_CACHE;
  entries_table = g_hash_table_lookup (_get_type_entries (),
      _extractable_type_name (proxy->priv->extractable_type));
  entry = g_hash_table_find (entries_table, (GHRFunc) _lookup_proxied_asset,
      (gpointer) ges_asset_get_id (proxy));

  if (!entry) {
    UNLOCK_CACHE;
    GST_DEBUG_OBJECT (proxy, "Not proxying any asset %s", proxy->priv->id);
    return FALSE;
  }

  proxied_asset = entry->asset;
  UNLOCK_CACHE;

  /* If the asset with the matching ->proxied_asset_id is already proxied
   * by another asset, we actually want @proxy to proxy this instead */
  while (proxied_asset->priv->proxies)
    proxied_asset = proxied_asset->priv->proxies->data;

  /* unless it is ourselves. I.e. it is already proxied by us */
  if (proxied_asset == proxy)
    return FALSE;

  GST_INFO_OBJECT (proxied_asset,
      "%s Making sure the proxy chain is fully set.",
      ges_asset_get_id (entry->asset));
  if (g_strcmp0 (proxied_asset->priv->proxied_asset_id, proxy->priv->id) ||
      g_strcmp0 (proxied_asset->priv->id, proxy->priv->proxied_asset_id))
    ges_asset_finish_proxy (proxied_asset);
  return ges_asset_set_proxy (proxied_asset, proxy);
}

static gboolean
_contained_in_proxy_tree (GESAsset * node, GESAsset * search)
{
  GList *tmp;
  if (node == search)
    return TRUE;
  for (tmp = node->priv->proxies; tmp; tmp = tmp->next) {
    if (_contained_in_proxy_tree (tmp->data, search))
      return TRUE;
  }
  return FALSE;
}

/**
 * ges_asset_set_proxy:
 * @asset: The #GESAsset to proxy
 * @proxy: (allow-none): A new default proxy for @asset
 *
 * Sets the #GESAsset:proxy for the asset.
 *
 * If @proxy is among the existing proxies of the asset (see
 * ges_asset_list_proxies()) it will be moved to become the default
 * proxy. Otherwise, if @proxy is not %NULL, it will be added to the list
 * of proxies, as the new default. The previous default proxy will become
 * 'next in line' for if the new one is removed, and so on. As such, this
 * will **not** actually remove the previous default proxy (use
 * ges_asset_unproxy() for that).
 *
 * Note that an asset can only act as a proxy for one other asset.
 *
 * As a special case, if @proxy is %NULL, then this method will actually
 * remove **all** proxies from the asset.
 *
 * Returns: %TRUE if @proxy was successfully set as the default for
 * @asset.
 */
gboolean
ges_asset_set_proxy (GESAsset * asset, GESAsset * proxy)
{
  GESAsset *current_target;
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);
  g_return_val_if_fail (proxy == NULL || GES_IS_ASSET (proxy), FALSE);
  g_return_val_if_fail (asset != proxy, FALSE);

  if (!proxy) {
    GList *tmp, *proxies;
    if (asset->priv->error) {
      GST_ERROR_OBJECT (asset,
          "Asset was loaded with error (%s), it should not be 'unproxied'",
          asset->priv->error->message);

      return FALSE;
    }

    GST_DEBUG_OBJECT (asset, "Removing all proxies");
    proxies = asset->priv->proxies;
    asset->priv->proxies = NULL;

    for (tmp = proxies; tmp; tmp = tmp->next) {
      GESAsset *proxy = tmp->data;
      proxy->priv->proxy_target = NULL;
    }
    asset->priv->state = ASSET_INITIALIZED;

    g_object_notify_by_pspec (G_OBJECT (asset), _properties[PROP_PROXY]);
    for (tmp = proxies; tmp; tmp = tmp->next)
      g_object_notify_by_pspec (G_OBJECT (tmp->data),
          _properties[PROP_PROXY_TARGET]);

    g_list_free (proxies);
    return TRUE;
  }
  current_target = proxy->priv->proxy_target;

  if (current_target && current_target != asset) {
    GST_ERROR_OBJECT (asset,
        "Trying to use '%s' as a proxy, but it is already proxying '%s'",
        proxy->priv->id, current_target->priv->id);

    return FALSE;
  }

  if (_contained_in_proxy_tree (proxy, asset)) {
    GST_ERROR_OBJECT (asset, "Trying to setup a circular proxying dependency!");

    return FALSE;
  }

  if (g_list_find (asset->priv->proxies, proxy)) {
    GST_INFO_OBJECT (asset,
        "%" GST_PTR_FORMAT " already marked as proxy, moving to first", proxy);
    asset->priv->proxies = g_list_remove (asset->priv->proxies, proxy);
  }

  GST_INFO ("%s is now proxied by %s", asset->priv->id, proxy->priv->id);
  asset->priv->proxies = g_list_prepend (asset->priv->proxies, proxy);

  proxy->priv->proxy_target = asset;
  asset->priv->state = ASSET_PROXIED;

  g_object_notify_by_pspec (G_OBJECT (asset), _properties[PROP_PROXY]);
  if (current_target != asset)
    g_object_notify_by_pspec (G_OBJECT (proxy), _properties[PROP_PROXY_TARGET]);

  /* FIXME: ->inform_proxy is not called. We should figure out what the
   * purpose of ->inform_proxy should be generically. Currently, it is
   * only called in ges_asset_try_proxy! */

  return TRUE;
}

/**
 * ges_asset_unproxy:
 * @asset: The #GESAsset to no longer proxy with @proxy
 * @proxy: An existing proxy of @asset
 *
 * Removes the proxy from the available list of proxies for the asset. If
 * the given proxy is the default proxy of the list, then the next proxy
 * in the available list (see ges_asset_list_proxies()) will become the
 * default. If there are no other proxies, then the asset will no longer
 * have a default #GESAsset:proxy.
 *
 * Returns: %TRUE if @proxy was successfully removed from @asset's proxy
 * list.
 */
gboolean
ges_asset_unproxy (GESAsset * asset, GESAsset * proxy)
{
  gboolean removing_default;
  gboolean last_proxy;
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);
  g_return_val_if_fail (GES_IS_ASSET (proxy), FALSE);
  g_return_val_if_fail (asset != proxy, FALSE);

  /* also tests if the list is NULL */
  if (!g_list_find (asset->priv->proxies, proxy)) {
    GST_INFO_OBJECT (asset, "'%s' is not a proxy.", proxy->priv->id);

    return FALSE;
  }

  last_proxy = (asset->priv->proxies->next == NULL);
  if (last_proxy && asset->priv->error) {
    GST_ERROR_OBJECT (asset,
        "Asset was loaded with error (%s), its last proxy '%s' should "
        "not be removed", asset->priv->error->message, proxy->priv->id);
    return FALSE;
  }

  removing_default = (asset->priv->proxies->data == proxy);

  asset->priv->proxies = g_list_remove (asset->priv->proxies, proxy);

  if (last_proxy)
    asset->priv->state = ASSET_INITIALIZED;
  proxy->priv->proxy_target = NULL;

  if (removing_default)
    g_object_notify_by_pspec (G_OBJECT (asset), _properties[PROP_PROXY]);
  g_object_notify_by_pspec (G_OBJECT (proxy), _properties[PROP_PROXY_TARGET]);

  return TRUE;
}

/**
 * ges_asset_list_proxies:
 * @asset: A #GESAsset
 *
 * Get all the proxies that the asset has. The first item of the list will
 * be the default #GESAsset:proxy. The second will be the proxy that is
 * 'next in line' to be default, and so on.
 *
 * Returns: (element-type GESAsset) (transfer none): The list of proxies
 * that @asset has.
 */
GList *
ges_asset_list_proxies (GESAsset * asset)
{
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);

  return asset->priv->proxies;
}

/**
 * ges_asset_get_proxy:
 * @asset: A #GESAsset
 *
 * Gets the default #GESAsset:proxy of the asset.
 *
 * Returns: (transfer none) (nullable): The default proxy of @asset.
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
 * @proxy: A #GESAsset
 *
 * Gets the #GESAsset:proxy-target of the asset.
 *
 * Note that the proxy target may have loaded with an error, so you should
 * call ges_asset_get_error() on the returned target.
 *
 * Returns: (transfer none) (nullable): The asset that @proxy is a proxy
 * of.
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
  entries = g_hash_table_lookup (_get_type_entries (),
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
_ensure_asset_for_wrong_id (const gchar * wrong_id, GType extractable_type,
    GError * error)
{
  GESAsset *asset;

  if ((asset = ges_asset_cache_lookup (extractable_type, wrong_id)))
    return asset;

  /* It is a dummy GESAsset, we just bruteforce its creation */
  asset = g_object_new (GES_TYPE_ASSET, "id", wrong_id,
      "extractable-type", extractable_type, NULL);

  /* transfer ownership to the cache */
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
 * Gets the #GESAsset:extractable-type of the asset.
 *
 * Returns: The extractable type of @self.
 */
GType
ges_asset_get_extractable_type (GESAsset * self)
{
  g_return_val_if_fail (GES_IS_ASSET (self), G_TYPE_INVALID);

  return self->priv->extractable_type;
}

/**
 * ges_asset_request:
 * @extractable_type: The #GESAsset:extractable-type of the asset
 * @id: (allow-none): The #GESAsset:id of the asset
 * @error: (allow-none): An error to be set if the requested asset has
 * loaded with an error, or %NULL to ignore
 *
 * Returns an asset with the given properties. If such an asset already
 * exists in the cache (it has been previously created in GES), then a
 * reference to the existing asset is returned. Otherwise, a newly created
 * asset is returned, and also added to the cache.
 *
 * If the requested asset has been loaded with an error, then @error is
 * set, if given, and %NULL will be returned instead.
 *
 * Note that the given @id may not be exactly the #GESAsset:id that is
 * set on the returned asset. For instance, it may be adjusted into a
 * standard format. Or, if a #GESExtractable type does not have its
 * extraction parametrised, as is the case by default, then the given @id
 * may be ignored entirely and the #GESAsset:id set to some standard, in
 * which case a %NULL @id can be given.
 *
 * Similarly, the given @extractable_type may not be exactly the
 * #GESAsset:extractable-type that is set on the returned asset. Instead,
 * the actual extractable type may correspond to a subclass of the given
 * @extractable_type, depending on the given @id.
 *
 * Moreover, depending on the given @extractable_type, the returned asset
 * may belong to a subclass of #GESAsset.
 *
 * Finally, if the requested asset has a #GESAsset:proxy, then the proxy
 * that is found at the end of the chain of proxies is returned (a proxy's
 * proxy will take its place, and so on, unless it has no proxy).
 *
 * Some asset subclasses only support asynchronous construction of its
 * assets, such as #GESUriClip. For such assets this method will fail, and
 * you should use ges_asset_request_async() instead. In the case of
 * #GESUriClip, you can use ges_uri_clip_asset_request_sync() if you only
 * want to wait for the request to finish.
 *
 * Returns: (transfer full) (allow-none): A reference to the requested
 * asset, or %NULL if an error occurred.
 */
GESAsset *
ges_asset_request (GType extractable_type, const gchar * id, GError ** error)
{
  gchar *real_id;

  GError *lerr = NULL;
  GESAsset *asset = NULL, *proxy;
  gboolean proxied = TRUE;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, G_TYPE_OBJECT), NULL);
  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      NULL);

  real_id = _check_and_update_parameters (&extractable_type, id, &lerr);
  if (real_id == NULL) {
    /* We create an asset for that wrong ID so we have a reference that the
     * user requested it */
    _ensure_asset_for_wrong_id (id, extractable_type, lerr);
    real_id = g_strdup (id);
  }
  if (lerr)
    g_error_free (lerr);

  /* asset owned by cache */
  asset = ges_asset_cache_lookup (extractable_type, real_id);
  if (asset) {
    while (proxied) {
      proxied = FALSE;
      switch (asset->priv->state) {
        case ASSET_INITIALIZED:
          break;
        case ASSET_INITIALIZING:
          asset = NULL;
          break;
        case ASSET_PROXIED:
          proxy = ges_asset_get_proxy (asset);
          if (proxy == NULL) {
            GST_ERROR ("Proxied against an asset we do not"
                " have in cache, something massively screwed");
            asset = NULL;
          } else {
            proxied = TRUE;
            do {
              asset = proxy;
            } while ((proxy = ges_asset_get_proxy (asset)));
          }
          break;
        case ASSET_NEEDS_RELOAD:
          GST_DEBUG_OBJECT (asset, "Asset in cache and needs reload");
          if (!start_loading (asset)) {
            GST_ERROR ("Failed to reload the asset for id %s", id);
            asset = NULL;
          }
          break;
        case ASSET_INITIALIZED_WITH_ERROR:
          GST_WARNING_OBJECT (asset, "Initialized with error, not returning");
          if (asset->priv->error && error)
            *error = g_error_copy (asset->priv->error);
          asset = NULL;
          break;
        default:
          GST_WARNING ("Case %i not handle, returning", asset->priv->state);
          asset = NULL;
          break;
      }
    }
    if (asset)
      gst_object_ref (asset);
  } else {
    GObjectClass *klass;
    GInitableIface *iface;
    GType asset_type = ges_extractable_type_get_asset_type (extractable_type);

    klass = g_type_class_ref (asset_type);
    iface = g_type_interface_peek (klass, G_TYPE_INITABLE);

    if (iface->init) {
      /* FIXME: allow the error to be set, which GInitable is designed
       * for! */
      asset = g_initable_new (asset_type,
          NULL, NULL, "id", real_id, "extractable-type",
          extractable_type, NULL);
    } else {
      GST_INFO ("Tried to create an Asset for type %s but no ->init method",
          g_type_name (extractable_type));
    }
    g_type_class_unref (klass);
  }

  if (real_id)
    g_free (real_id);

  GST_DEBUG ("New asset created synchronously: %p", asset);
  return asset;
}

/**
 * ges_asset_request_async:
 * @extractable_type: The #GESAsset:extractable-type of the asset
 * @id: (allow-none): The #GESAsset:id of the asset
 * @cancellable: (allow-none): An object to allow cancellation of the
 * asset request, or %NULL to ignore
 * @callback: A function to call when the initialization is finished
 * @user_data: Data to be passed to @callback
 *
 * Requests an asset with the given properties asynchronously (see
 * ges_asset_request()). When the asset has been initialized or fetched
 * from the cache, the given callback function will be called. The
 * asset can then be retrieved in the callback using the
 * ges_asset_request_finish() method on the given #GAsyncResult.
 *
 * Note that the source object passed to the callback will be the
 * #GESAsset corresponding to the request, but it may not have loaded
 * correctly and therefore can not be used as is. Instead,
 * ges_asset_request_finish() should be used to fetch a usable asset, or
 * indicate that an error occurred in the asset's creation.
 *
 * Note that the callback will be called in the #GMainLoop running under
 * the same #GMainContext that ges_init() was called in. So, if you wish
 * the callback to be invoked outside the default #GMainContext, you can
 * call g_main_context_push_thread_default() in a new thread before
 * calling ges_init().
 *
 * Example of an asynchronous asset request:
 * ``` c
 * // The request callback
 * static void
 * asset_loaded_cb (GESAsset * source, GAsyncResult * res, gpointer user_data)
 * {
 *   GESAsset *asset;
 *   GError *error = NULL;
 *
 *   asset = ges_asset_request_finish (res, &error);
 *   if (asset) {
 *    gst_print ("The file: %s is usable as a GESUriClip",
 *        ges_asset_get_id (asset));
 *   } else {
 *    gst_print ("The file: %s is *not* usable as a GESUriClip because: %s",
 *        ges_asset_get_id (source), error->message);
 *   }
 *
 *   gst_object_unref (asset);
 * }
 *
 * // The request:
 * ges_asset_request_async (GES_TYPE_URI_CLIP, some_uri, NULL,
 *    (GAsyncReadyCallback) asset_loaded_cb, user_data);
 * ```
 */
void
ges_asset_request_async (GType extractable_type,
    const gchar * id, GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  gchar *real_id;
  GESAsset *asset;
  GError *error = NULL;
  GTask *task = NULL;

  g_return_if_fail (g_type_is_a (extractable_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE));
  g_return_if_fail (callback);

  GST_DEBUG ("Creating asset with extractable type %s and ID=%s",
      g_type_name (extractable_type), id);

  real_id = _check_and_update_parameters (&extractable_type, id, &error);
  if (error) {
    _ensure_asset_for_wrong_id (id, extractable_type, error);
    real_id = g_strdup (id);
  }

  /* Check if we already have an asset for this ID */
  asset = ges_asset_cache_lookup (extractable_type, real_id);
  if (asset) {
    task = g_task_new (asset, NULL, callback, user_data);

    /* In the case of proxied asset, we will loop until we find the
     * last asset of the chain of proxied asset */
    while (TRUE) {
      switch (asset->priv->state) {
        case ASSET_INITIALIZED:
          GST_DEBUG_OBJECT (asset, "Asset in cache and initialized, "
              "using it");

          /* Takes its own references to @asset */
          g_task_return_boolean (task, TRUE);

          goto done;
        case ASSET_INITIALIZING:
          GST_DEBUG_OBJECT (asset, "Asset in cache and but not "
              "initialized, setting a new callback");
          ges_asset_cache_append_task (extractable_type, real_id, task);
          task = NULL;

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
          task = NULL;
          GES_ASSET_GET_CLASS (asset)->start_loading (asset, &error);

          goto done;
        case ASSET_INITIALIZED_WITH_ERROR:
          g_task_return_error (task,
              error ? g_error_copy (error) : g_error_copy (asset->priv->error));

          g_clear_error (&error);

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
  if (task)
    gst_object_unref (task);
  if (real_id)
    g_free (real_id);
}

/**
 * ges_asset_needs_reload
 * @extractable_type: The #GESAsset:extractable-type of the asset that
 * needs reloading
 * @id: (allow-none): The #GESAsset:id of the asset asset that needs
 * reloading
 *
 * Indicate that an existing #GESAsset in the cache should be reloaded
 * upon the next request. This can be used when some condition has
 * changed, which may require that an existing asset should be updated.
 * For example, if an external resource has changed or now become
 * available.
 *
 * Note, the asset is not immediately changed, but will only actually
 * reload on the next call to ges_asset_request() or
 * ges_asset_request_async().
 *
 * Returns: %TRUE if the specified asset exists in the cache and could be
 * marked for reloading.
 */
gboolean
ges_asset_needs_reload (GType extractable_type, const gchar * id)
{
  gchar *real_id;
  GESAsset *asset;
  GError *error = NULL;

  g_return_val_if_fail (g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE),
      FALSE);

  real_id = _check_and_update_parameters (&extractable_type, id, &error);
  if (error) {
    _ensure_asset_for_wrong_id (id, extractable_type, error);
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
 * @self: A #GESAsset
 *
 * Gets the #GESAsset:id of the asset.
 *
 * Returns: The ID of @self.
 */
const gchar *
ges_asset_get_id (GESAsset * self)
{
  g_return_val_if_fail (GES_IS_ASSET (self), NULL);

  return self->priv->id;
}

/**
 * ges_asset_extract:
 * @self: The #GESAsset to extract an object from
 * @error: An error to be set in case something goes wrong,
 * or %NULL to ignore
 *
 * Extracts a new #GESAsset:extractable-type object from the asset. The
 * #GESAsset:id of the asset may determine the properties and state of the
 * newly created object.
 *
 * Returns: (transfer floating): A newly created object, or %NULL if an
 * error occurred.
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
 * @res: The task result to fetch the asset from
 * @error: An error to be set in case something goes wrong, or %NULL to ignore
 *
 * Fetches an asset requested by ges_asset_request_async(), which
 * finalises the request.
 *
 * Returns: (transfer full): The requested asset, or %NULL if an error
 * occurred.
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
 * @filter: The type of object that can be extracted from the asset
 *
 * List all the assets in the current cache whose
 * #GESAsset:extractable-type are of the given type (including
 * subclasses).
 *
 * Note that, since only a #GESExtractable can be extracted from an asset,
 * using `GES_TYPE_EXTRACTABLE` as @filter will return all the assets in
 * the current cache.
 *
 * Returns: (transfer container) (element-type GESAsset): A list of all
 * #GESAsset-s currently in the cache whose #GESAsset:extractable-type is
 * of the @filter type.
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
  g_hash_table_iter_init (&types_iter, _get_type_entries ());
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
 * @self: A #GESAsset
 *
 * Retrieve the error that was set on the asset when it was loaded.
 *
 * Returns: (transfer none) (nullable): The error set on @asset, or
 * %NULL if no error occurred when @asset was loaded.
 *
 * Since: 1.8
 */
GError *
ges_asset_get_error (GESAsset * self)
{
  g_return_val_if_fail (GES_IS_ASSET (self), NULL);

  return self->priv->error;
}
