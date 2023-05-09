#include "ges-internal.h"
#include "ges-discoverer-manager.h"

/**
 * GESDiscovererManager:
 *
 * Since: 1.24
 */

struct _GESDiscovererManager
{
  GObject parent;

  GHashTable *discoverers;
  GMutex lock;
  GstClockTime timeout;

  gboolean use_cache;
};

G_DEFINE_TYPE (GESDiscovererManager, ges_discoverer_manager, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_TIMEOUT,
  PROP_USE_CACHE,
  N_PROPERTIES
};

enum
{
  LOAD_SERIALIZED_INFO_SIGNAL,
  DISCOVERER_SIGNAL,
  N_SIGNALS
};

#define DEFAULT_USE_CACHE FALSE
#define DEFAULT_TIMEOUT (60 * GST_SECOND)
static GParamSpec *properties[N_PROPERTIES] = { NULL, };
static guint signals[N_SIGNALS] = { 0, };

G_LOCK_DEFINE_STATIC (singleton_lock);
static GESDiscovererManager *self = NULL;

static void
ges_discoverer_manager_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESDiscovererManager *self = GES_DISCOVERER_MANAGER (object);

  switch (property_id) {
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, ges_discoverer_manager_get_timeout (self));
      break;
    case PROP_USE_CACHE:
      g_value_set_boolean (value, ges_discoverer_manager_get_use_cache (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ges_discoverer_manager_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESDiscovererManager *self = GES_DISCOVERER_MANAGER (object);

  switch (property_id) {
    case PROP_TIMEOUT:
      ges_discoverer_manager_set_timeout (self, g_value_get_uint64 (value));
      break;
    case PROP_USE_CACHE:
      ges_discoverer_manager_set_use_cache (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ges_discoverer_manager_finalize (GObject * object)
{
  GESDiscovererManager *self = GES_DISCOVERER_MANAGER (object);

  g_hash_table_unref (self->discoverers);

  G_OBJECT_CLASS (ges_discoverer_manager_parent_class)->finalize (object);
}

static void
ges_discoverer_manager_class_init (GESDiscovererManagerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ges_discoverer_manager_finalize;
  object_class->set_property = ges_discoverer_manager_set_property;
  object_class->get_property = ges_discoverer_manager_get_property;

  /**
   * GESDiscovererManager:timeout:
   *
   * The timeout (in milliseconds) for the #GstDiscoverer operations
   *
   * Since: 1.24
   */
  properties[PROP_TIMEOUT] =
      g_param_spec_uint64 ("timeout", "Timeout",
      "The timeout for the discoverer", 0, GST_CLOCK_TIME_NONE, DEFAULT_TIMEOUT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  /**
   * GESDiscovererManager::use-cache:
   *
   * Whether to use a serialized version of the discoverer info from our own
   * cache if accessible. This allows the discovery to be much faster as when
   * using this option, we do not need to create a #GstPipeline and run it, but
   * instead, just reload the #GstDiscovererInfo in its serialized form.
   *
   * The cache files are saved in `$XDG_CACHE_DIR/gstreamer-1.0/discoverer/`.
   *
   * For more granularity or to use your own cache, using the
   * #GESDiscovererManager::load-serialized-info signal is recommended.
   *
   * Since: 1.24
   */
  properties[PROP_USE_CACHE] =
      g_param_spec_boolean ("use-cache", "Use cache",
      "Whether to use a serialized version of the discoverer info from our own cache if accessible",
      DEFAULT_USE_CACHE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);

  /**
   * GESDiscovererManager::load-serialized-info:
   * @manager: the #GESDiscovererManager
   * @uri: The URI to load the serialized info for
   *
   * Retrieves information about a URI from and external source of information,
   * like a cache file. This is used by the discoverer to speed up the
   * discovery.
   *
   * Returns: (nullable) (transfer full): The #GstDiscovererInfo representing
   * @uri, or %NULL if no information
   *
   * Since: 1.24
   */
  signals[LOAD_SERIALIZED_INFO_SIGNAL] =
      g_signal_new ("load-serialized-info", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, GST_TYPE_DISCOVERER_INFO, 1, G_TYPE_STRING);

  /**
   * GESDiscovererManager::discovered: (attributes doc.skip=true)
   * @manager: the #GESDiscovererManager
   * @info: The #GstDiscovererInfo representing the discovered URI
   * @error: (nullable): The #GError that occurred, or %NULL
   *
   * Since: 1.24
   */
  signals[DISCOVERER_SIGNAL] =
      g_signal_new ("discovered", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 2, GST_TYPE_DISCOVERER_INFO,
      G_TYPE_ERROR);
}

void
ges_discoverer_manager_init (GESDiscovererManager * self)
{
  self->discoverers = g_hash_table_new_full (g_direct_hash, g_str_equal,
      NULL, g_object_unref);
}


/**
 * ges_discoverer_manager_get_default:
 *
 * Returns: (transfer full): The default #GESDiscovererManager
 *
 * Since: 1.24
 */
GESDiscovererManager *
ges_discoverer_manager_get_default (void)
{
  G_LOCK (singleton_lock);
  if (G_UNLIKELY (self == NULL)) {
    self = g_object_new (GES_TYPE_DISCOVERER_MANAGER, NULL);
  }
  G_UNLOCK (singleton_lock);

  return g_object_ref (self);
}

/**
 * ges_discoverer_manager_get_use_cache:
  * @self: The #GESDiscovererManager
  *
  * Returns: Whether to use the cache or not
  *
  * Since: 1.24
  */
gboolean
ges_discoverer_manager_get_use_cache (GESDiscovererManager * self)
{
  g_return_val_if_fail (GES_IS_DISCOVERER_MANAGER (self), FALSE);

  return self->use_cache;

}

/**
 * ges_discoverer_manager_set_use_cache:
  * @self: The #GESDiscovererManager
  * @use_cache: Whether to use the cache
  *
  * Sets whether to use the cache or not
  *
  * Since: 1.24
  */
void
ges_discoverer_manager_set_use_cache (GESDiscovererManager * self,
    gboolean use_cache)
{
  g_return_if_fail (GES_IS_DISCOVERER_MANAGER (self));

  self->use_cache = use_cache;

}

/**
 * ges_discoverer_manager_get_timeout:
  * @self: The #GESDiscovererManager
  *
  * Returns: The timeout to use for the discoverer
  *
  * Since: 1.24
  */
GstClockTime
ges_discoverer_manager_get_timeout (GESDiscovererManager * self)
{
  g_return_val_if_fail (GES_IS_DISCOVERER_MANAGER (self), 0);

  return self->timeout;
}

/**
 * ges_discoverer_manager_set_timeout:
  * @self: The #GESDiscovererManager
  * @timeout: The timeout to set
  *
  * Sets the timeout to use for the discoverer
  *
  * Since: 1.24
  */
void
ges_discoverer_manager_set_timeout (GESDiscovererManager * self,
    GstClockTime timeout)
{
  GHashTableIter iter;
  GstDiscoverer *discoverer;

  g_return_if_fail (GES_IS_DISCOVERER_MANAGER (self));

  self->timeout = timeout;

  g_mutex_lock (&self->lock);
  g_hash_table_iter_init (&iter, self->discoverers);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) & discoverer))
    g_object_set (discoverer, "timeout", timeout, NULL);
  g_mutex_unlock (&self->lock);
}

static GstDiscovererInfo *
proxy_load_serialized_info_cb (GESDiscovererManager * self, const gchar * uri)
{
  GstDiscovererInfo *info;

  g_signal_emit (self, signals[LOAD_SERIALIZED_INFO_SIGNAL], 0, uri, &info);

  return info;
}

static void
proxy_discovered_cb (GESDiscovererManager * self,
    GstDiscovererInfo * info, GError * err, gpointer user_data)
{
  g_signal_emit (self, signals[DISCOVERER_SIGNAL], 0, info, err);
}


static GstDiscoverer *
create_discoverer (GESDiscovererManager * self)
{
  GstDiscoverer *discoverer;

  discoverer = gst_discoverer_new (self->timeout, NULL);
  g_signal_connect_swapped (discoverer, "load-serialized-info",
      G_CALLBACK (proxy_load_serialized_info_cb), self);
  g_signal_connect_swapped (discoverer, "discovered",
      G_CALLBACK (proxy_discovered_cb), self);
  g_object_set (discoverer, "use-cache", self->use_cache, NULL);

  gst_discoverer_start (discoverer);

  return discoverer;
}

static GstDiscoverer *
ges_discoverer_manager_get_discoverer (GESDiscovererManager * self)
{
  GstDiscoverer *ret;

  g_return_val_if_fail (GES_IS_DISCOVERER_MANAGER (self), NULL);

  g_mutex_lock (&self->lock);
  ret = g_hash_table_lookup (self->discoverers, g_thread_self ());
  if (!ret) {
    ret = create_discoverer (self);
    g_hash_table_insert (self->discoverers, g_thread_self (), ret);
  }
  g_mutex_unlock (&self->lock);

  return gst_object_ref (ret);
}

gboolean
ges_discoverer_manager_start_discovery (GESDiscovererManager * self,
    const gchar * uri)
{
  GstDiscoverer *discoverer;

  g_return_val_if_fail (uri != NULL, FALSE);

  discoverer = ges_discoverer_manager_get_discoverer (self);

  gboolean res = gst_discoverer_discover_uri_async (discoverer, uri);
  gst_object_unref (discoverer);

  return res;
}

void
ges_discoverer_manager_cleanup (void)
{
  G_LOCK (singleton_lock);
  gst_clear_object (&self);
  G_UNLOCK (singleton_lock);
}
