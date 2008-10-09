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

G_DEFINE_TYPE (GstRTSPSessionPool, gst_rtsp_session_pool, G_TYPE_OBJECT);

static void
gst_rtsp_session_pool_class_init (GstRTSPSessionPoolClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
}

static void
gst_rtsp_session_pool_init (GstRTSPSessionPool * pool)
{
  pool->lock = g_mutex_new ();
  pool->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
		  g_free, NULL);
}

/**
 * gst_rtsp_session_pool_new:
 *
 * Create a new #GstRTSPSessionPool instance.
 */
GstRTSPSessionPool *
gst_rtsp_session_pool_new (void)
{
  GstRTSPSessionPool *result;

  result = g_object_new (GST_TYPE_RTSP_SESSION_POOL, NULL);

  return result;
}

/**
 * gst_rtsp_session_pool_find:
 * @pool: the pool to search
 * @sessionid: the session id
 *
 * Find the session with @sessionid in @pool.
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
  if (result)
    g_object_ref (result);
  g_mutex_unlock (pool->lock);

  return result;
}

static gchar *
create_session_id (void)
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
  gchar *id;

  g_return_val_if_fail (GST_IS_RTSP_SESSION_POOL (pool), NULL);

  do {
    /* start by creating a new random session id, we assume that this is random
     * enough to not cause a collision, which we will check later  */
    id = create_session_id ();

    g_mutex_lock (pool->lock);
    /* check if the sessionid existed */
    result = g_hash_table_lookup (pool->sessions, id);
    if (result) {
      /* found, retry with a different session id*/
      result = NULL;
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
}

/**
 * gst_rtsp_session_pool_remove:
 * @pool: a #GstRTSPSessionPool
 * @sess: a #GstRTSPSession
 *
 * Remove @sess from @pool and Clean it up.
 *
 * Returns: a new #GstRTSPSession.
 */
void
gst_rtsp_session_pool_remove (GstRTSPSessionPool *pool, GstRTSPSession *sess)
{
  gboolean found;

  g_return_if_fail (GST_IS_RTSP_SESSION_POOL (pool));
  g_return_if_fail (GST_IS_RTSP_SESSION (sess));

  g_mutex_lock (pool->lock);
  found = g_hash_table_remove (pool->sessions, sess);
  g_mutex_unlock (pool->lock);

  if (found) {
    g_object_unref (sess);
  }
}

