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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "rtsp-session-pool.h"

#undef DEBUG

#define DEFAULT_MAX_SESSIONS 0

enum
{
  PROP_0,
  PROP_MAX_SESSIONS,
  PROP_LAST
};

static void gst_rtsp_session_pool_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_session_pool_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_session_pool_finalize (GObject * object);

static gchar * create_session_id (GstRTSPSessionPool *pool);

G_DEFINE_TYPE (GstRTSPSessionPool, gst_rtsp_session_pool, G_TYPE_OBJECT);

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

  klass->create_session_id = create_session_id;

}

static void
gst_rtsp_session_pool_init (GstRTSPSessionPool * pool)
{
  pool->lock = g_mutex_new ();
  pool->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
		  NULL, g_object_unref);
  pool->max_sessions = DEFAULT_MAX_SESSIONS;
}

static void
gst_rtsp_session_pool_finalize (GObject * object)
{
  GstRTSPSessionPool * pool = GST_RTSP_SESSION_POOL (object);

  g_mutex_free (pool->lock);
  g_hash_table_unref (pool->sessions);
  
  G_OBJECT_CLASS (gst_rtsp_session_pool_parent_class)->finalize (object);
}

static void
gst_rtsp_session_pool_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
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
gst_rtsp_session_pool_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
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
 * Returns: A new #GstRTSPSessionPool. g_object_unref() after usage.
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
gst_rtsp_session_pool_set_max_sessions (GstRTSPSessionPool *pool, guint max)
{
  g_return_if_fail (GST_IS_RTSP_SESSION_POOL (pool));

  g_mutex_lock (pool->lock);
  pool->max_sessions = max;
  g_mutex_unlock (pool->lock);
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
gst_rtsp_session_pool_get_max_sessions (GstRTSPSessionPool *pool)
{
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  g_mutex_lock (pool->lock);
  result = pool->max_sessions;
  g_mutex_unlock (pool->lock);

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
gst_rtsp_session_pool_get_n_sessions (GstRTSPSessionPool *pool)
{
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  g_mutex_lock (pool->lock);
  result = g_hash_table_size (pool->sessions);
  g_mutex_unlock (pool->lock);

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
 * Returns: the #GstRTSPSession with @sessionid or %NULL when the session did
 * not exist. g_object_unref() after usage.
 */
GstRTSPSession *
gst_rtsp_session_pool_find (GstRTSPSessionPool *pool, const gchar *sessionid)
{
  GstRTSPSession *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);
  g_return_val_if_fail (sessionid != NULL, NULL);

  g_mutex_lock (pool->lock);
  result = g_hash_table_lookup (pool->sessions, sessionid);
  if (result) {
    g_object_ref (result);
    gst_rtsp_session_touch (result);
  }
  g_mutex_unlock (pool->lock);

  return result;
}

static gchar *
create_session_id (GstRTSPSessionPool *pool)
{
  gchar id[16];
  gint i;

  for (i = 0; i < 16; i++) {
    id[i] = g_random_int_range ('a', 'z');
  }

  return g_strndup (id, 16);
}

/**
 * gst_rtsp_session_pool_create:
 * @pool: a #GstRTSPSessionPool
 *
 * Create a new #GstRTSPSession object in @pool.
 *
 * Returns: a new #GstRTSPSession.
 */
GstRTSPSession *
gst_rtsp_session_pool_create (GstRTSPSessionPool *pool)
{
  GstRTSPSession *result = NULL;
  GstRTSPSessionPoolClass *klass;
  gchar *id = NULL;
  guint retry;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

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

    g_mutex_lock (pool->lock);
    /* check session limit */
    if (pool->max_sessions > 0) {
      if (g_hash_table_size (pool->sessions) >= pool->max_sessions)
	goto too_many_sessions;
    }
    /* check if the sessionid existed */
    result = g_hash_table_lookup (pool->sessions, id);
    if (result) {
      /* found, retry with a different session id */
      result = NULL;
      retry++;
      if (retry > 100)
	goto collision;
    }
    else {
      /* not found, create session and insert it in the pool */
      result = gst_rtsp_session_new (id); 
      /* take additional ref for the pool */
      g_object_ref (result);
      g_hash_table_insert (pool->sessions, result->sessionid, result);
    }
    g_mutex_unlock (pool->lock);

    g_free (id);
  } while (result == NULL);

  return result;

  /* ERRORS */
no_function:
  {
    g_warning ("no create_session_id vmethod in GstRTSPSessionPool %p", pool);
    return NULL;
  }
no_session:
  {
    g_warning ("can't create session id with GstRTSPSessionPool %p", pool);
    return NULL;
  }
collision:
  {
    g_warning ("can't find unique sessionid for GstRTSPSessionPool %p", pool);
    g_mutex_unlock (pool->lock);
    g_free (id);
    return NULL;
  }
too_many_sessions:
  {
    g_warning ("session pool reached max sessions of %d", pool->max_sessions);
    g_mutex_unlock (pool->lock);
    g_free (id);
    return NULL;
  }
}

/**
 * gst_rtsp_session_pool_remove:
 * @pool: a #GstRTSPSessionPool
 * @sess: a #GstRTSPSession
 *
 * Remove @sess from @pool, releasing the ref that the pool has on @sess.
 *
 * Returns: %TRUE if the session was found and removed.
 */
gboolean
gst_rtsp_session_pool_remove (GstRTSPSessionPool *pool, GstRTSPSession *sess)
{
  gboolean found;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), FALSE);
  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), FALSE);

  g_mutex_lock (pool->lock);
  found = g_hash_table_remove (pool->sessions, sess->sessionid);
  g_mutex_unlock (pool->lock);

  return found;
}

static gboolean
cleanup_func (gchar *sessionid, GstRTSPSession *sess, GTimeVal *now)
{
  return gst_rtsp_session_is_expired (sess, now);
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
gst_rtsp_session_pool_cleanup (GstRTSPSessionPool *pool)
{
  guint result;
  GTimeVal now;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), 0);

  g_get_current_time (&now);

  g_mutex_lock (pool->lock);
  result = g_hash_table_foreach_remove (pool->sessions, (GHRFunc) cleanup_func, &now);
  g_mutex_unlock (pool->lock);

  return result;
}

typedef struct
{
  GstRTSPSessionPool *pool;
  GstRTSPSessionFilterFunc func;
  gpointer user_data;
  GList *list;
} FilterData;

static gboolean
filter_func (gchar *sessionid, GstRTSPSession *sess, FilterData *data)
{
  switch (data->func (data->pool, sess, data->user_data)) {
    case GST_RTSP_FILTER_REMOVE:
      return TRUE;
    case GST_RTSP_FILTER_REF:
      /* keep ref */
      data->list = g_list_prepend (data->list, g_object_ref (sess));
      /* fallthrough */
    default:
    case GST_RTSP_FILTER_KEEP:
      return FALSE;
  }
}

/**
 * gst_rtsp_session_pool_filter:
 * @pool: a #GstRTSPSessionPool
 * @func: a callback
 * @user_data: user data passed to @func
 *
 * Call @func for each session in @pool. The result value of @func determines
 * what happens to the session. @func will be called with the session pool
 * locked so no further actions on @pool can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the session will be removed from
 * @pool.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the session will remain in @pool.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the session will remain in @pool but
 * will also be added with an additional ref to the result GList of this
 * function..
 *
 * Returns: a GList with all sessions for which @func returned
 * #GST_RTSP_FILTER_REF. After usage, each element in the GList should be unreffed
 * before the list is freed.
 */
GList *
gst_rtsp_session_pool_filter (GstRTSPSessionPool *pool,
    GstRTSPSessionFilterFunc func, gpointer user_data)
{
  FilterData data;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  data.pool = pool;
  data.func = func;
  data.user_data = user_data;
  data.list = NULL;

  g_mutex_lock (pool->lock);
  g_hash_table_foreach_remove (pool->sessions, (GHRFunc) filter_func, &data);
  g_mutex_unlock (pool->lock);

  return data.list;
}

typedef struct
{
  GSource source;
  GstRTSPSessionPool *pool;
  gint timeout;
} GstPoolSource;

static void
collect_timeout (gchar *sessionid, GstRTSPSession *sess, GstPoolSource *psrc)
{
  gint timeout;
  GTimeVal now;

  g_source_get_current_time ((GSource*)psrc, &now);

  timeout = gst_rtsp_session_next_timeout (sess, &now);
  g_message ("%p: next timeout: %d", sess, timeout);
  if (psrc->timeout == -1 || timeout < psrc->timeout)
    psrc->timeout = timeout;
}

static gboolean
gst_pool_source_prepare (GSource * source, gint * timeout)
{
  GstPoolSource *psrc;
  gboolean result;

  psrc = (GstPoolSource *) source;
  psrc->timeout = -1;

  g_mutex_lock (psrc->pool->lock);
  g_hash_table_foreach (psrc->pool->sessions, (GHFunc) collect_timeout, psrc);
  g_mutex_unlock (psrc->pool->lock);

  if (timeout)
    *timeout = psrc->timeout;

  result = psrc->timeout == 0;

  g_message ("prepare %d, %d", psrc->timeout, result);

  return result;
}

static gboolean
gst_pool_source_check (GSource * source)
{
  g_message ("check");

  return gst_pool_source_prepare (source, NULL);
}

static gboolean
gst_pool_source_dispatch (GSource * source, GSourceFunc callback,
    gpointer user_data)
{
  gboolean res;
  GstPoolSource *psrc = (GstPoolSource *) source;
  GstRTSPSessionPoolFunc func = (GstRTSPSessionPoolFunc) callback;

  g_message ("dispatch");

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

  g_message ("finalize %p", psrc);

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
 * A GSource that will be dispatched when the session should be cleaned up.
 */
GSource *
gst_rtsp_session_pool_create_watch (GstRTSPSessionPool *pool)
{
  GstPoolSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

  source = (GstPoolSource *) g_source_new (&gst_pool_source_funcs,
      sizeof (GstPoolSource));
  source->pool = g_object_ref (pool);

  return (GSource *) source;
}

