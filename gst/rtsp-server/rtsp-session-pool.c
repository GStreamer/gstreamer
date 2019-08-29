/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:rtsp-session-pool
 * @short_description: An object for managing sessions
 * @see_also: #GstRTSPSession
 *
 * The #GstRTSPSessionPool object manages a list of #GstRTSPSession objects.
 *
 * The maximum number of sessions can be configured with
 * gst_rtsp_session_pool_set_max_sessions(). The current number of sessions can
 * be retrieved with gst_rtsp_session_pool_get_n_sessions().
 *
 * Use gst_rtsp_session_pool_create() to create a new #GstRTSPSession object.
 * The session object can be found again with its id and
 * gst_rtsp_session_pool_find().
 *
 * All sessions can be iterated with gst_rtsp_session_pool_filter().
 *
 * Run gst_rtsp_session_pool_cleanup() periodically to remove timed out sessions
 * or use gst_rtsp_session_pool_create_watch() to be notified when session
 * cleanup should be performed.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp-session-pool.h"

struct _GstRTSPSessionPoolPrivate
{
  GMutex lock;                  /* protects everything in this struct */
  guint max_sessions;
  GHashTable *sessions;
  guint sessions_cookie;
};

#define DEFAULT_MAX_SESSIONS 0

enum
{
  PROP_0,
  PROP_MAX_SESSIONS,
  PROP_LAST
};

static const gchar session_id_charset[] =
    { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D',
  'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', '-', '_', '.', '+'  /* '$' Live555 in VLC strips off $ chars */
};

enum
{
  SIGNAL_SESSION_REMOVED,
  SIGNAL_LAST
};

static guint gst_rtsp_session_pool_signals[SIGNAL_LAST] = { 0 };

GST_DEBUG_CATEGORY_STATIC (rtsp_session_debug);
#define GST_CAT_DEFAULT rtsp_session_debug

static void gst_rtsp_session_pool_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_pool_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_pool_finalize (GObject * object);

static gchar *create_session_id (GstRTSPSessionPool * pool);
static GstRTSPSession *create_session (GstRTSPSessionPool * pool,
    const gchar * id);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPSessionPool, gst_rtsp_session_pool,
    G_TYPE_OBJECT);

static void
gst_rtsp_session_pool_class_init (GstRTSPSessionPoolClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_session_pool_get_property;
  gobject_class->set_property = gst_rtsp_session_pool_set_property;
  gobject_class->finalize = gst_rtsp_session_pool_finalize;

  g_object_class_install_property (gobject_class, PROP_MAX_SESSIONS,
      g_param_spec_uint ("max-sessions", "Max Sessions",
          "the maximum amount of sessions (0 = unlimited)",
          0, G_MAXUINT, DEFAULT_MAX_SESSIONS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_session_pool_signals[SIGNAL_SESSION_REMOVED] =
      g_signal_new ("session-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPSessionPoolClass,
          session_removed), NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_RTSP_SESSION);

  klass->create_session_id = create_session_id;
  klass->create_session = create_session;

  GST_DEBUG_CATEGORY_INIT (rtsp_session_debug, "rtspsessionpool", 0,
      "GstRTSPSessionPool");
}

static void
gst_rtsp_session_pool_init (GstRTSPSessionPool * pool)
{
  GstRTSPSessionPoolPrivate *priv;

  pool->priv = priv = gst_rtsp_session_pool_get_instance_private (pool);

  g_mutex_init (&priv->lock);
  priv->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, g_object_unref);
  priv->max_sessions = DEFAULT_MAX_SESSIONS;
}

static GstRTSPFilterResult
remove_sessions_func (GstRTSPSessionPool * pool, GstRTSPSession * session,
    gpointer user_data)
{
  return GST_RTSP_FILTER_REMOVE;
}

static void
gst_rtsp_session_pool_finalize (GObject * object)
{
  GstRTSPSessionPool *pool = GST_RTSP_SESSION_POOL (object);
  GstRTSPSessionPoolPrivate *priv = pool->priv;

  gst_rtsp_session_pool_filter (pool, remove_sessions_func, NULL);
  g_hash_table_unref (priv->sessions);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_session_pool_parent_class)->finalize (object);
}

static void
gst_rtsp_session_pool_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPSessionPool *pool = GST_RTSP_SESSION_POOL (object);

  switch (propid) {
    case PROP_MAX_SESSIONS:
      g_value_set_uint (value, gst_rtsp_session_pool_get_max_sessions (pool));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
      break;
  }
}

static void
gst_rtsp_session_pool_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPSessionPool *pool = GST_RTSP_SESSION_POOL (object);

  switch (propid) {
    case PROP_MAX_SESSIONS:
      gst_rtsp_session_pool_set_max_sessions (pool, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
      break;
  }
}

/**
 * gst_rtsp_session_pool_new:
 *
 * Create a new #GstRTSPSessionPool instance.
 *
 * Returns: (transfer full): A new #GstRTSPSessionPool. g_object_unref() after
 * usage.
 */
GstRTSPSessionPool *
gst_rtsp_session_pool_new (void)
{
  GstRTSPSessionPool *result;

  result = g_object_new (GST_TYPE_RTSP_SESSION_POOL, NULL);

  return result;
}

/**
 * gst_rtsp_session_pool_set_max_sessions:
 * @pool: a #GstRTSPSessionPool
 * @max: the maximum number of sessions
 *
 * Configure the maximum allowed number of sessions in @pool to @max.
 * A value of 0 means an unlimited amount of sessions.
 */
void
gst_rtsp_session_pool_set_max_sessions (GstRTSPSessionPool * pool, guint max)
{
  GstRTSPSessionPoolPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SESSION_POOL (pool));

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  priv->max_sessions = max;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_session_pool_get_max_sessions:
 * @pool: a #GstRTSPSessionPool
 *
 * Get the maximum allowed number of sessions in @pool. 0 means an unlimited
 * amount of sessions.
 *
 * Returns: the maximum allowed number of sessions.
 */
guint
gst_rtsp_session_pool_get_max_sessions (GstRTSPSessionPool * pool)
{
  GstRTSPSessionPoolPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  result = priv->max_sessions;
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_session_pool_get_n_sessions:
 * @pool: a #GstRTSPSessionPool
 *
 * Get the amount of active sessions in @pool.
 *
 * Returns: the amount of active sessions in @pool.
 */
guint
gst_rtsp_session_pool_get_n_sessions (GstRTSPSessionPool * pool)
{
  GstRTSPSessionPoolPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  result = g_hash_table_size (priv->sessions);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_session_pool_find:
 * @pool: the pool to search
 * @sessionid: the session id
 *
 * Find the session with @sessionid in @pool. The access time of the session
 * will be updated with gst_rtsp_session_touch().
 *
 * Returns: (transfer full) (nullable): the #GstRTSPSession with @sessionid
 * or %NULL when the session did not exist. g_object_unref() after usage.
 */
GstRTSPSession *
gst_rtsp_session_pool_find (GstRTSPSessionPool * pool, const gchar * sessionid)
{
  GstRTSPSessionPoolPrivate *priv;
  GstRTSPSession *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);
  g_return_val_if_fail (sessionid != NULL, NULL);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  result = g_hash_table_lookup (priv->sessions, sessionid);
  if (result) {
    g_object_ref (result);
    gst_rtsp_session_touch (result);
  }
  g_mutex_unlock (&priv->lock);

  return result;
}

static gchar *
create_session_id (GstRTSPSessionPool * pool)
{
  gchar id[16];
  gint i;

  for (i = 0; i < 16; i++) {
    id[i] =
        session_id_charset[g_random_int_range (0,
            G_N_ELEMENTS (session_id_charset))];
  }

  return g_strndup (id, 16);
}

static GstRTSPSession *
create_session (GstRTSPSessionPool * pool, const gchar * id)
{
  return gst_rtsp_session_new (id);
}

/**
 * gst_rtsp_session_pool_create:
 * @pool: a #GstRTSPSessionPool
 *
 * Create a new #GstRTSPSession object in @pool.
 *
 * Returns: (transfer full) (nullable): a new #GstRTSPSession.
 */
GstRTSPSession *
gst_rtsp_session_pool_create (GstRTSPSessionPool * pool)
{
  GstRTSPSessionPoolPrivate *priv;
  GstRTSPSession *result = NULL;
  GstRTSPSessionPoolClass *klass;
  gchar *id = NULL;
  guint retry;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

  priv = pool->priv;

  klass = GST_RTSP_SESSION_POOL_GET_CLASS (pool);

  retry = 0;
  do {
    /* start by creating a new random session id, we assume that this is random
     * enough to not cause a collision, which we will check later  */
    if (klass->create_session_id)
      id = klass->create_session_id (pool);
    else
      goto no_function;

    if (id == NULL)
      goto no_session;

    g_mutex_lock (&priv->lock);
    /* check session limit */
    if (priv->max_sessions > 0) {
      if (g_hash_table_size (priv->sessions) >= priv->max_sessions)
        goto too_many_sessions;
    }
    /* check if the sessionid existed */
    result = g_hash_table_lookup (priv->sessions, id);
    if (result) {
      /* found, retry with a different session id */
      result = NULL;
      retry++;
      if (retry > 100)
        goto collision;
    } else {
      /* not found, create session and insert it in the pool */
      if (klass->create_session)
        result = klass->create_session (pool, id);
      if (result == NULL)
        goto too_many_sessions;
      /* take additional ref for the pool */
      g_object_ref (result);
      g_hash_table_insert (priv->sessions,
          (gchar *) gst_rtsp_session_get_sessionid (result), result);
      priv->sessions_cookie++;
    }
    g_mutex_unlock (&priv->lock);

    g_free (id);
  } while (result == NULL);

  return result;

  /* ERRORS */
no_function:
  {
    GST_WARNING ("no create_session_id vmethod in GstRTSPSessionPool %p", pool);
    return NULL;
  }
no_session:
  {
    GST_WARNING ("can't create session id with GstRTSPSessionPool %p", pool);
    return NULL;
  }
collision:
  {
    GST_WARNING ("can't find unique sessionid for GstRTSPSessionPool %p", pool);
    g_mutex_unlock (&priv->lock);
    g_free (id);
    return NULL;
  }
too_many_sessions:
  {
    GST_WARNING ("session pool reached max sessions of %d", priv->max_sessions);
    g_mutex_unlock (&priv->lock);
    g_free (id);
    return NULL;
  }
}

/**
 * gst_rtsp_session_pool_remove:
 * @pool: a #GstRTSPSessionPool
 * @sess: (transfer none): a #GstRTSPSession
 *
 * Remove @sess from @pool, releasing the ref that the pool has on @sess.
 *
 * Returns: %TRUE if the session was found and removed.
 */
gboolean
gst_rtsp_session_pool_remove (GstRTSPSessionPool * pool, GstRTSPSession * sess)
{
  GstRTSPSessionPoolPrivate *priv;
  gboolean found;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), FALSE);
  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), FALSE);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  g_object_ref (sess);
  found =
      g_hash_table_remove (priv->sessions,
      gst_rtsp_session_get_sessionid (sess));
  if (found)
    priv->sessions_cookie++;
  g_mutex_unlock (&priv->lock);

  if (found)
    g_signal_emit (pool, gst_rtsp_session_pool_signals[SIGNAL_SESSION_REMOVED],
        0, sess);

  g_object_unref (sess);

  return found;
}

typedef struct
{
  gint64 now_monotonic_time;
  GstRTSPSessionPool *pool;
  GList *removed;
} CleanupData;

static gboolean
cleanup_func (gchar * sessionid, GstRTSPSession * sess, CleanupData * data)
{
  gboolean expired;

  expired = gst_rtsp_session_is_expired_usec (sess, data->now_monotonic_time);

  if (expired) {
    GST_DEBUG ("session expired");
    data->removed = g_list_prepend (data->removed, g_object_ref (sess));
  }

  return expired;
}

/**
 * gst_rtsp_session_pool_cleanup:
 * @pool: a #GstRTSPSessionPool
 *
 * Inspect all the sessions in @pool and remove the sessions that are inactive
 * for more than their timeout.
 *
 * Returns: the amount of sessions that got removed.
 */
guint
gst_rtsp_session_pool_cleanup (GstRTSPSessionPool * pool)
{
  GstRTSPSessionPoolPrivate *priv;
  guint result;
  CleanupData data;
  GList *walk;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  priv = pool->priv;

  data.now_monotonic_time = g_get_monotonic_time ();

  data.pool = pool;
  data.removed = NULL;

  g_mutex_lock (&priv->lock);
  result =
      g_hash_table_foreach_remove (priv->sessions, (GHRFunc) cleanup_func,
      &data);
  if (result > 0)
    priv->sessions_cookie++;
  g_mutex_unlock (&priv->lock);

  for (walk = data.removed; walk; walk = walk->next) {
    GstRTSPSession *sess = walk->data;

    g_signal_emit (pool,
        gst_rtsp_session_pool_signals[SIGNAL_SESSION_REMOVED], 0, sess);

    g_object_unref (sess);
  }
  g_list_free (data.removed);

  return result;
}

/**
 * gst_rtsp_session_pool_filter:
 * @pool: a #GstRTSPSessionPool
 * @func: (scope call) (allow-none): a callback
 * @user_data: (closure): user data passed to @func
 *
 * Call @func for each session in @pool. The result value of @func determines
 * what happens to the session. @func will be called with the session pool
 * locked so no further actions on @pool can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the session will be set to the
 * expired state and removed from @pool.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the session will remain in @pool.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the session will remain in @pool but
 * will also be added with an additional ref to the result GList of this
 * function..
 *
 * When @func is %NULL, #GST_RTSP_FILTER_REF will be assumed for all sessions.
 *
 * Returns: (element-type GstRTSPSession) (transfer full): a GList with all
 * sessions for which @func returned #GST_RTSP_FILTER_REF. After usage, each
 * element in the GList should be unreffed before the list is freed.
 */
GList *
gst_rtsp_session_pool_filter (GstRTSPSessionPool * pool,
    GstRTSPSessionPoolFilterFunc func, gpointer user_data)
{
  GstRTSPSessionPoolPrivate *priv;
  GHashTableIter iter;
  gpointer key, value;
  GList *result;
  GHashTable *visited;
  guint cookie;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

  priv = pool->priv;

  result = NULL;
  if (func)
    visited = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  g_mutex_lock (&priv->lock);
restart:
  g_hash_table_iter_init (&iter, priv->sessions);
  cookie = priv->sessions_cookie;
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstRTSPSession *session = value;
    GstRTSPFilterResult res;
    gboolean changed;

    if (func) {
      /* only visit each session once */
      if (g_hash_table_contains (visited, session))
        continue;

      g_hash_table_add (visited, g_object_ref (session));
      g_mutex_unlock (&priv->lock);

      res = func (pool, session, user_data);

      g_mutex_lock (&priv->lock);
    } else
      res = GST_RTSP_FILTER_REF;

    changed = (cookie != priv->sessions_cookie);

    switch (res) {
      case GST_RTSP_FILTER_REMOVE:
      {
        gboolean removed = TRUE;

        if (changed)
          /* something changed, check if we still have the session */
          removed = g_hash_table_remove (priv->sessions, key);
        else
          g_hash_table_iter_remove (&iter);

        if (removed) {
          /* if we managed to remove the session, update the cookie and
           * signal */
          cookie = ++priv->sessions_cookie;
          g_mutex_unlock (&priv->lock);

          g_signal_emit (pool,
              gst_rtsp_session_pool_signals[SIGNAL_SESSION_REMOVED], 0,
              session);

          g_mutex_lock (&priv->lock);
          /* cookie could have changed again, make sure we restart */
          changed |= (cookie != priv->sessions_cookie);
        }
        break;
      }
      case GST_RTSP_FILTER_REF:
        /* keep ref */
        result = g_list_prepend (result, g_object_ref (session));
        break;
      case GST_RTSP_FILTER_KEEP:
      default:
        break;
    }
    if (changed)
      goto restart;
  }
  g_mutex_unlock (&priv->lock);

  if (func)
    g_hash_table_unref (visited);

  return result;
}

typedef struct
{
  GSource source;
  GstRTSPSessionPool *pool;
  gint timeout;
} GstPoolSource;

static void
collect_timeout (gchar * sessionid, GstRTSPSession * sess, GstPoolSource * psrc)
{
  gint timeout;
  gint64 now_monotonic_time;

  now_monotonic_time = g_get_monotonic_time ();

  timeout = gst_rtsp_session_next_timeout_usec (sess, now_monotonic_time);

  GST_INFO ("%p: next timeout: %d", sess, timeout);
  if (psrc->timeout == -1 || timeout < psrc->timeout)
    psrc->timeout = timeout;
}

static gboolean
gst_pool_source_prepare (GSource * source, gint * timeout)
{
  GstRTSPSessionPoolPrivate *priv;
  GstPoolSource *psrc;
  gboolean result;

  psrc = (GstPoolSource *) source;
  psrc->timeout = -1;
  priv = psrc->pool->priv;

  g_mutex_lock (&priv->lock);
  g_hash_table_foreach (priv->sessions, (GHFunc) collect_timeout, psrc);
  g_mutex_unlock (&priv->lock);

  if (timeout)
    *timeout = psrc->timeout;

  result = psrc->timeout == 0;

  GST_INFO ("prepare %d, %d", psrc->timeout, result);

  return result;
}

static gboolean
gst_pool_source_check (GSource * source)
{
  GST_INFO ("check");

  return gst_pool_source_prepare (source, NULL);
}

static gboolean
gst_pool_source_dispatch (GSource * source, GSourceFunc callback,
    gpointer user_data)
{
  gboolean res;
  GstPoolSource *psrc = (GstPoolSource *) source;
  GstRTSPSessionPoolFunc func = (GstRTSPSessionPoolFunc) callback;

  GST_INFO ("dispatch");

  if (func)
    res = func (psrc->pool, user_data);
  else
    res = FALSE;

  return res;
}

static void
gst_pool_source_finalize (GSource * source)
{
  GstPoolSource *psrc = (GstPoolSource *) source;

  GST_INFO ("finalize %p", psrc);

  g_object_unref (psrc->pool);
  psrc->pool = NULL;
}

static GSourceFuncs gst_pool_source_funcs = {
  gst_pool_source_prepare,
  gst_pool_source_check,
  gst_pool_source_dispatch,
  gst_pool_source_finalize
};

/**
 * gst_rtsp_session_pool_create_watch:
 * @pool: a #GstRTSPSessionPool
 *
 * Create a #GSource that will be dispatched when the session should be cleaned
 * up.
 *
 * Returns: (transfer full): a #GSource
 */
GSource *
gst_rtsp_session_pool_create_watch (GstRTSPSessionPool * pool)
{
  GstPoolSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

  source = (GstPoolSource *) g_source_new (&gst_pool_source_funcs,
      sizeof (GstPoolSource));
  source->pool = g_object_ref (pool);

  return (GSource *) source;
}
