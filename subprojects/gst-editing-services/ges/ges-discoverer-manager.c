#include "ges-internal.h"
#include "ges-discoverer-manager.h"


typedef struct
{
  GWeakRef /*<GESDiscovererManager> */ manager;
  GstDiscoverer *discoverer;
  GThread *thread;
  gint n_uri;
  gulong load_serialized_info_id;
  gulong source_setup_id;
  gulong discovered_id;
} GESDiscovererData;

static void
ges_discoverer_data_free (GESDiscovererData * data)
{
  GST_LOG ("Freeing discoverer %" GST_PTR_FORMAT, data->discoverer);
  g_assert (data->n_uri == 0);
  gst_discoverer_stop (data->discoverer);
  g_signal_handler_disconnect (data->discoverer, data->load_serialized_info_id);
  g_signal_handler_disconnect (data->discoverer, data->source_setup_id);
  g_signal_handler_disconnect (data->discoverer, data->discovered_id);
  g_weak_ref_clear (&data->manager);

  g_object_unref (data->discoverer);
}

static void
ges_discoverer_data_unref (GESDiscovererData * data)
{
  g_atomic_rc_box_release_full (data,
      (GDestroyNotify) ges_discoverer_data_free);
}

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
  DISCOVERER_SOURCE_SETUP,
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
  GHashTableIter iter;
  GESDiscovererData *discoverer_data;
  GMainContext *context = g_main_context_get_thread_default ();

  if (!context)
    context = g_main_context_default ();

  g_mutex_lock (&self->lock);
  g_hash_table_iter_init (&iter, self->discoverers);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) & discoverer_data)) {
    GSource *source;

    while ((source =
            g_main_context_find_source_by_user_data (context,
                discoverer_data))) {
      g_source_destroy (source);
    }
  }

  g_hash_table_unref (self->discoverers);
  g_mutex_unlock (&self->lock);

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
   * GESDiscovererManager::source-setup:
   * @manager: the #GESDiscovererManager
   * @source: The source #GstElement to setup
   *
   * Allows to setup the source element before the discoverer runs.
   *
   * Since: 1.24
   */
  signals[DISCOVERER_SOURCE_SETUP] =
      g_signal_new ("source-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

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
  self->discoverers = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) ges_discoverer_data_unref);
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
  GESDiscovererData *discoverer_data;

  g_return_if_fail (GES_IS_DISCOVERER_MANAGER (self));

  self->timeout = timeout;

  g_mutex_lock (&self->lock);
  g_hash_table_iter_init (&iter, self->discoverers);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) & discoverer_data))
    g_object_set (discoverer_data->discoverer, "timeout", timeout, NULL);
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
source_setup_cb (GESDiscovererManager * self, GstElement * source)
{
  g_signal_emit (self, signals[DISCOVERER_SOURCE_SETUP], 0, source);
}

static gboolean
cleanup_discoverer_cb (GESDiscovererData * discoverer_data)
{
  GESDiscovererManager *self = g_weak_ref_get (&discoverer_data->manager);
  gint res = G_SOURCE_CONTINUE;

  if (!self) {
    return G_SOURCE_REMOVE;
  }

  g_mutex_lock (&self->lock);
  if (discoverer_data->n_uri > 0) {
    GST_DEBUG_OBJECT (self, "Discoverer still has %d uris to discover",
        discoverer_data->n_uri);
    goto done;
  }

  GST_DEBUG_OBJECT (self, "Removing unused discoverer");

  // Remove the discoverer if the one is use for that thread is still the
  // one we have been asked to free, otherwise this one will be destroyed anyway
  // once this source is removed
  res = G_SOURCE_REMOVE;
  if (g_hash_table_lookup (self->discoverers,
          discoverer_data->thread) == discoverer_data) {
    g_hash_table_remove (self->discoverers, discoverer_data->thread);
  }

done:
  g_mutex_unlock (&self->lock);
  g_object_unref (self);

  return res;
}

static void
proxy_discovered_cb (GESDiscovererManager * self,
    GstDiscovererInfo * info, GError * err, GstDiscoverer * discoverer)
{
  g_signal_emit (self, signals[DISCOVERER_SIGNAL], 0, info, err);

  g_mutex_lock (&self->lock);
  GESDiscovererData *data =
      g_hash_table_lookup (self->discoverers, g_thread_self ());
  if (data) {
    data->n_uri--;
    data = g_atomic_rc_box_acquire (data);
  }
  g_mutex_unlock (&self->lock);

  if (data) {
    ges_timeout_add (1000, (GSourceFunc) cleanup_discoverer_cb, data,
        (GDestroyNotify) ges_discoverer_data_unref);
  }
}

static GESDiscovererData *
create_discoverer (GESDiscovererManager * self)
{
  GstDiscoverer *discoverer;

  GESDiscovererData *data = g_atomic_rc_box_new0 (GESDiscovererData);
  discoverer = gst_discoverer_new (self->timeout, NULL);
  data->thread = g_thread_self ();
  g_weak_ref_set (&data->manager, self);
  data->load_serialized_info_id =
      g_signal_connect_swapped (discoverer, "load-serialized-info",
      G_CALLBACK (proxy_load_serialized_info_cb), self);
  data->source_setup_id =
      g_signal_connect_swapped (discoverer, "source-setup",
      G_CALLBACK (source_setup_cb), self);
  data->discovered_id =
      g_signal_connect_swapped (discoverer, "discovered",
      G_CALLBACK (proxy_discovered_cb), self);
  g_object_set (discoverer, "use-cache", self->use_cache, NULL);

  gst_discoverer_start (discoverer);

  data->discoverer = discoverer;

  return data;
}

static GESDiscovererData *
ges_discoverer_manager_get_discoverer (GESDiscovererManager * self)
{
  GESDiscovererData *ret;

  g_return_val_if_fail (GES_IS_DISCOVERER_MANAGER (self), NULL);

  g_mutex_lock (&self->lock);
  ret = g_hash_table_lookup (self->discoverers, g_thread_self ());
  if (!ret) {
    ret = create_discoverer (self);
  } else {
    g_hash_table_steal (self->discoverers, g_thread_self ());
  }
  g_mutex_unlock (&self->lock);

  return ret;
}

gboolean
ges_discoverer_manager_start_discovery (GESDiscovererManager * self,
    const gchar * uri)
{
  GESDiscovererData *disco_data;

  g_return_val_if_fail (uri != NULL, FALSE);

  disco_data = ges_discoverer_manager_get_discoverer (self);

  g_mutex_lock (&self->lock);
  gboolean res =
      gst_discoverer_discover_uri_async (disco_data->discoverer, uri);
  disco_data->n_uri++;
  g_hash_table_insert (self->discoverers, g_thread_self (), disco_data);
  g_mutex_unlock (&self->lock);

  return res;
}

void
ges_discoverer_manager_cleanup (void)
{
  G_LOCK (singleton_lock);
  gst_clear_object (&self);
  G_UNLOCK (singleton_lock);
}
