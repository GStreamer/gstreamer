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
 * SECTION:rtsp-session
 * @short_description: An object to manage media
 * @see_also: #GstRTSPSessionPool, #GstRTSPSessionMedia, #GstRTSPMedia
 *
 * The #GstRTSPSession is identified by an id, unique in the
 * #GstRTSPSessionPool that created the session and manages media and its
 * configuration.
 *
 * A #GstRTSPSession has a timeout that can be retrieved with
 * gst_rtsp_session_get_timeout(). You can check if the sessions is expired with
 * gst_rtsp_session_is_expired(). gst_rtsp_session_touch() will reset the
 * expiration counter of the session.
 *
 * When a client configures a media with SETUP, a session will be created to
 * keep track of the configuration of that media. With
 * gst_rtsp_session_manage_media(), the media is added to the managed media
 * in the session. With gst_rtsp_session_release_media() the media can be
 * released again from the session. Managed media is identified in the sessions
 * with a url. Use gst_rtsp_session_get_media() to get the media that matches
 * (part of) the given url.
 *
 * The media in a session can be iterated with gst_rtsp_session_filter().
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-session.h"

struct _GstRTSPSessionPrivate
{
  GMutex lock;                  /* protects everything but sessionid and create_time */
  gchar *sessionid;

  guint timeout;
  gboolean timeout_always_visible;
  GMutex last_access_lock;
  gint64 last_access_monotonic_time;
  gint64 last_access_real_time;
  gint expire_count;

  GList *medias;
  guint medias_cookie;
  guint extra_time_timeout;
};

#undef DEBUG

#define DEFAULT_TIMEOUT	       60
#define NO_TIMEOUT              -1
#define DEFAULT_ALWAYS_VISIBLE  FALSE
#define DEFAULT_EXTRA_TIMEOUT 5

enum
{
  PROP_0,
  PROP_SESSIONID,
  PROP_TIMEOUT,
  PROP_TIMEOUT_ALWAYS_VISIBLE,
  PROP_EXTRA_TIME_TIMEOUT,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_session_debug);
#define GST_CAT_DEFAULT rtsp_session_debug

static void gst_rtsp_session_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_finalize (GObject * obj);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPSession, gst_rtsp_session, G_TYPE_OBJECT);

static void
gst_rtsp_session_class_init (GstRTSPSessionClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_session_get_property;
  gobject_class->set_property = gst_rtsp_session_set_property;
  gobject_class->finalize = gst_rtsp_session_finalize;

  g_object_class_install_property (gobject_class, PROP_SESSIONID,
      g_param_spec_string ("sessionid", "Sessionid", "the session id",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "the timeout of the session (0 = never)", 0, G_MAXUINT,
          DEFAULT_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT_ALWAYS_VISIBLE,
      g_param_spec_boolean ("timeout-always-visible", "Timeout Always Visible ",
          "timeout always visible in header",
          DEFAULT_ALWAYS_VISIBLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPSession::extra-timeout:
   *
   * Extra time to add to the timeout, in seconds. This only affects the
   * time until a session is considered timed out and is not signalled
   * in the RTSP request responses. Only the value of the timeout
   * property is signalled in the request responses.
   *
   * Default value is 5 seconds.
   * If the application is using a buffer that is configured to hold
   * amount of data equal to the sessiontimeout, extra-timeout can be
   * set to zero to prevent loss of data
   *
   * Since: 1.18
   */
  g_object_class_install_property (gobject_class, PROP_EXTRA_TIME_TIMEOUT,
      g_param_spec_uint ("extra-timeout",
          "Add extra time to timeout ", "Add extra time to timeout", 0,
          G_MAXUINT, DEFAULT_EXTRA_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  GST_DEBUG_CATEGORY_INIT (rtsp_session_debug, "rtspsession", 0,
      "GstRTSPSession");
}

static void
gst_rtsp_session_init (GstRTSPSession * session)
{
  GstRTSPSessionPrivate *priv;

  session->priv = priv = gst_rtsp_session_get_instance_private (session);

  GST_INFO ("init session %p", session);

  g_mutex_init (&priv->lock);
  g_mutex_init (&priv->last_access_lock);
  priv->timeout = DEFAULT_TIMEOUT;
  priv->extra_time_timeout = DEFAULT_EXTRA_TIMEOUT;

  gst_rtsp_session_touch (session);
}

static void
gst_rtsp_session_finalize (GObject * obj)
{
  GstRTSPSession *session;
  GstRTSPSessionPrivate *priv;

  session = GST_RTSP_SESSION (obj);
  priv = session->priv;

  GST_INFO ("finalize session %p", session);

  /* free all media */
  g_list_free_full (priv->medias, g_object_unref);

  /* free session id */
  g_free (priv->sessionid);
  g_mutex_clear (&priv->last_access_lock);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_session_parent_class)->finalize (obj);
}

static void
gst_rtsp_session_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPSession *session = GST_RTSP_SESSION (object);
  GstRTSPSessionPrivate *priv = session->priv;

  switch (propid) {
    case PROP_SESSIONID:
      g_value_set_string (value, priv->sessionid);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, gst_rtsp_session_get_timeout (session));
      break;
    case PROP_TIMEOUT_ALWAYS_VISIBLE:
      g_value_set_boolean (value, priv->timeout_always_visible);
      break;
    case PROP_EXTRA_TIME_TIMEOUT:
      g_value_set_uint (value, priv->extra_time_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_session_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPSession *session = GST_RTSP_SESSION (object);
  GstRTSPSessionPrivate *priv = session->priv;

  switch (propid) {
    case PROP_SESSIONID:
      g_free (priv->sessionid);
      priv->sessionid = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      gst_rtsp_session_set_timeout (session, g_value_get_uint (value));
      break;
    case PROP_TIMEOUT_ALWAYS_VISIBLE:
      g_mutex_lock (&priv->lock);
      priv->timeout_always_visible = g_value_get_boolean (value);
      g_mutex_unlock (&priv->lock);
      break;
    case PROP_EXTRA_TIME_TIMEOUT:
      g_mutex_lock (&priv->lock);
      priv->extra_time_timeout = g_value_get_uint (value);
      g_mutex_unlock (&priv->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_session_manage_media:
 * @sess: a #GstRTSPSession
 * @path: the path for the media
 * @media: (transfer full): a #GstRTSPMedia
 *
 * Manage the media object @obj in @sess. @path will be used to retrieve this
 * media from the session with gst_rtsp_session_get_media().
 *
 * Ownership is taken from @media.
 *
 * Returns: (transfer none): a new @GstRTSPSessionMedia object.
 */
GstRTSPSessionMedia *
gst_rtsp_session_manage_media (GstRTSPSession * sess, const gchar * path,
    GstRTSPMedia * media)
{
  GstRTSPSessionPrivate *priv;
  GstRTSPSessionMedia *result;
  GstRTSPMediaStatus status;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  status = gst_rtsp_media_get_status (media);
  g_return_val_if_fail (status == GST_RTSP_MEDIA_STATUS_PREPARED || status ==
      GST_RTSP_MEDIA_STATUS_SUSPENDED, NULL);

  priv = sess->priv;

  result = gst_rtsp_session_media_new (path, media);

  g_mutex_lock (&priv->lock);
  priv->medias = g_list_prepend (priv->medias, result);
  priv->medias_cookie++;
  g_mutex_unlock (&priv->lock);

  GST_INFO ("manage new media %p in session %p", media, result);

  return result;
}

static void
gst_rtsp_session_unset_transport_keepalive (GstRTSPSessionMedia * sessmedia)
{
  GstRTSPMedia *media;
  guint i, n_streams;

  media = gst_rtsp_session_media_get_media (sessmedia);
  n_streams = gst_rtsp_media_n_streams (media);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStreamTransport *transport =
        gst_rtsp_session_media_get_transport (sessmedia, i);

    if (!transport)
      continue;

    gst_rtsp_stream_transport_set_keepalive (transport, NULL, NULL, NULL);
  }
}

/**
 * gst_rtsp_session_release_media:
 * @sess: a #GstRTSPSession
 * @media: (transfer none): a #GstRTSPMedia
 *
 * Release the managed @media in @sess, freeing the memory allocated by it.
 *
 * Returns: %TRUE if there are more media session left in @sess.
 */
gboolean
gst_rtsp_session_release_media (GstRTSPSession * sess,
    GstRTSPSessionMedia * media)
{
  GstRTSPSessionPrivate *priv;
  GList *find;
  gboolean more;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), FALSE);
  g_return_val_if_fail (media != NULL, FALSE);

  priv = sess->priv;

  g_mutex_lock (&priv->lock);
  find = g_list_find (priv->medias, media);
  if (find) {
    priv->medias = g_list_delete_link (priv->medias, find);
    priv->medias_cookie++;
  }
  more = (priv->medias != NULL);
  g_mutex_unlock (&priv->lock);

  if (find && !more)
    gst_rtsp_session_unset_transport_keepalive (media);

  if (find)
    g_object_unref (media);

  return more;
}

static GstRTSPSessionMedia *
_gst_rtsp_session_get_media (GstRTSPSession * sess, const gchar * path,
    gint * matched, gboolean dup)
{
  GstRTSPSessionPrivate *priv;
  GstRTSPSessionMedia *result;
  GList *walk;
  gint best;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  priv = sess->priv;
  result = NULL;
  best = 0;

  g_mutex_lock (&priv->lock);
  for (walk = priv->medias; walk; walk = g_list_next (walk)) {
    GstRTSPSessionMedia *test;

    test = (GstRTSPSessionMedia *) walk->data;

    /* find largest match */
    if (gst_rtsp_session_media_matches (test, path, matched)) {
      if (best < *matched) {
        result = test;
        best = *matched;
      }
    }
  }

  if (result && dup)
    result = g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  *matched = best;

  return result;
}

/**
 * gst_rtsp_session_get_media:
 * @sess: a #GstRTSPSession
 * @path: the path for the media
 * @matched: (out): the amount of matched characters
 *
 * Gets the session media for @path. @matched will contain the number of matched
 * characters of @path.
 *
 * Returns: (transfer none) (nullable): the configuration for @path in @sess.
 */
GstRTSPSessionMedia *
gst_rtsp_session_get_media (GstRTSPSession * sess, const gchar * path,
    gint * matched)
{
  return _gst_rtsp_session_get_media (sess, path, matched, FALSE);
}

/**
 * gst_rtsp_session_dup_media:
 * @sess: a #GstRTSPSession
 * @path: the path for the media
 * @matched: (out): the amount of matched characters
 *
 * Gets the session media for @path, increasing its reference count. @matched
 * will contain the number of matched characters of @path.
 *
 * Returns: (transfer full) (nullable): the configuration for @path in @sess,
 * should be unreferenced when no longer needed.
 *
 * Since: 1.20
 */
GstRTSPSessionMedia *
gst_rtsp_session_dup_media (GstRTSPSession * sess, const gchar * path,
    gint * matched)
{
  return _gst_rtsp_session_get_media (sess, path, matched, TRUE);
}

/**
 * gst_rtsp_session_filter:
 * @sess: a #GstRTSPSession
 * @func: (scope call) (allow-none): a callback
 * @user_data: (closure): user data passed to @func
 *
 * Call @func for each media in @sess. The result value of @func determines
 * what happens to the media. @func will be called with @sess
 * locked so no further actions on @sess can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the media will be removed from
 * @sess.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the media will remain in @sess.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the media will remain in @sess but
 * will also be added with an additional ref to the result #GList of this
 * function..
 *
 * When @func is %NULL, #GST_RTSP_FILTER_REF will be assumed for all media.
 *
 * Returns: (element-type GstRTSPSessionMedia) (transfer full): a GList with all
 * media for which @func returned #GST_RTSP_FILTER_REF. After usage, each
 * element in the #GList should be unreffed before the list is freed.
 */
GList *
gst_rtsp_session_filter (GstRTSPSession * sess,
    GstRTSPSessionFilterFunc func, gpointer user_data)
{
  GstRTSPSessionPrivate *priv;
  GList *result, *walk, *next;
  GHashTable *visited;
  guint cookie;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);

  priv = sess->priv;

  result = NULL;
  if (func)
    visited = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  g_mutex_lock (&priv->lock);
restart:
  cookie = priv->medias_cookie;
  for (walk = priv->medias; walk; walk = next) {
    GstRTSPSessionMedia *media = walk->data;
    GstRTSPFilterResult res;
    gboolean changed;

    next = g_list_next (walk);

    if (func) {
      /* only visit each media once */
      if (g_hash_table_contains (visited, media))
        continue;

      g_hash_table_add (visited, g_object_ref (media));
      g_mutex_unlock (&priv->lock);

      res = func (sess, media, user_data);

      g_mutex_lock (&priv->lock);
    } else {
      res = GST_RTSP_FILTER_REF;
    }

    changed = (cookie != priv->medias_cookie);

    switch (res) {
      case GST_RTSP_FILTER_REMOVE:
        if (changed) {
          GList *l;

          walk = NULL;

          for (l = priv->medias; l; l = l->next) {
            if (l->data == media) {
              walk = l;
              break;
            }
          }
        }

        /* The media might have been removed from the list while the mutex was
         * unlocked above. In that case there's nothing else to do here as the
         * only reference to the media owned by this function is in the
         * visited hash table and that is released in the end
         */
        if (walk) {
          priv->medias = g_list_delete_link (priv->medias, walk);
          g_object_unref (media);
        }

        cookie = ++priv->medias_cookie;
        break;
      case GST_RTSP_FILTER_REF:
        result = g_list_prepend (result, g_object_ref (media));
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

/**
 * gst_rtsp_session_new:
 * @sessionid: a session id
 *
 * Create a new #GstRTSPSession instance with @sessionid.
 *
 * Returns: (transfer full): a new #GstRTSPSession
 */
GstRTSPSession *
gst_rtsp_session_new (const gchar * sessionid)
{
  GstRTSPSession *result;

  g_return_val_if_fail (sessionid != NULL, NULL);

  result = g_object_new (GST_TYPE_RTSP_SESSION, "sessionid", sessionid, NULL);

  return result;
}

/**
 * gst_rtsp_session_get_sessionid:
 * @session: a #GstRTSPSession
 *
 * Get the sessionid of @session.
 *
 * Returns: (transfer none) (nullable): the sessionid of @session.
 * The value remains valid as long as @session is alive.
 */
const gchar *
gst_rtsp_session_get_sessionid (GstRTSPSession * session)
{
  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), NULL);

  return session->priv->sessionid;
}

/**
 * gst_rtsp_session_get_header:
 * @session: a #GstRTSPSession
 *
 * Get the string that can be placed in the Session header field.
 *
 * Returns: (transfer full) (nullable): the Session header of @session.
 * g_free() after usage.
 */
gchar *
gst_rtsp_session_get_header (GstRTSPSession * session)
{
  GstRTSPSessionPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), NULL);

  priv = session->priv;


  g_mutex_lock (&priv->lock);
  if (priv->timeout_always_visible || priv->timeout != 60)
    result = g_strdup_printf ("%s;timeout=%d", priv->sessionid, priv->timeout);
  else
    result = g_strdup (priv->sessionid);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_session_set_timeout:
 * @session: a #GstRTSPSession
 * @timeout: the new timeout
 *
 * Configure @session for a timeout of @timeout seconds. The session will be
 * cleaned up when there is no activity for @timeout seconds.
 */
void
gst_rtsp_session_set_timeout (GstRTSPSession * session, guint timeout)
{
  GstRTSPSessionPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  priv = session->priv;

  g_mutex_lock (&priv->lock);
  priv->timeout = timeout;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_session_get_timeout:
 * @session: a #GstRTSPSession
 *
 * Get the timeout value of @session.
 *
 * Returns: the timeout of @session in seconds.
 */
guint
gst_rtsp_session_get_timeout (GstRTSPSession * session)
{
  GstRTSPSessionPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), 0);

  priv = session->priv;

  g_mutex_lock (&priv->lock);
  res = priv->timeout;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_session_touch:
 * @session: a #GstRTSPSession
 *
 * Update the last_access time of the session to the current time.
 */
void
gst_rtsp_session_touch (GstRTSPSession * session)
{
  GstRTSPSessionPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  priv = session->priv;

  g_mutex_lock (&priv->last_access_lock);
  priv->last_access_monotonic_time = g_get_monotonic_time ();
  priv->last_access_real_time = g_get_real_time ();
  g_mutex_unlock (&priv->last_access_lock);
}

/**
 * gst_rtsp_session_prevent_expire:
 * @session: a #GstRTSPSession
 *
 * Prevent @session from expiring.
 */
void
gst_rtsp_session_prevent_expire (GstRTSPSession * session)
{
  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  g_atomic_int_add (&session->priv->expire_count, 1);
}

/**
 * gst_rtsp_session_allow_expire:
 * @session: a #GstRTSPSession
 *
 * Allow @session to expire. This method must be called an equal
 * amount of time as gst_rtsp_session_prevent_expire().
 */
void
gst_rtsp_session_allow_expire (GstRTSPSession * session)
{
  g_atomic_int_add (&session->priv->expire_count, -1);
}

/**
 * gst_rtsp_session_next_timeout_usec:
 * @session: a #GstRTSPSession
 * @now: the current monotonic time
 *
 * Get the amount of milliseconds till the session will expire.
 *
 * Returns: the amount of milliseconds since the session will time out.
 */
gint
gst_rtsp_session_next_timeout_usec (GstRTSPSession * session, gint64 now)
{
  GstRTSPSessionPrivate *priv;
  gint res;
  GstClockTime last_access, now_ns;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), -1);

  priv = session->priv;

  g_mutex_lock (&priv->lock);
  /* If timeout is set to 0, we never timeout */
  if (priv->timeout == 0) {
    g_mutex_unlock (&priv->lock);
    return NO_TIMEOUT;
  }
  g_mutex_unlock (&priv->lock);

  g_mutex_lock (&priv->last_access_lock);
  if (g_atomic_int_get (&priv->expire_count) != 0) {
    /* touch session when the expire count is not 0 */
    priv->last_access_monotonic_time = g_get_monotonic_time ();
    priv->last_access_real_time = g_get_real_time ();
  }

  last_access = GST_USECOND * (priv->last_access_monotonic_time);

  /* add timeout allow for priv->extra_time_timeout
   * seconds of extra time */
  last_access += priv->timeout * GST_SECOND +
      (priv->extra_time_timeout * GST_SECOND);

  g_mutex_unlock (&priv->last_access_lock);

  now_ns = GST_USECOND * now;

  if (last_access > now_ns) {
    res = GST_TIME_AS_MSECONDS (last_access - now_ns);
  } else {
    res = 0;
  }

  return res;
}

/****** Deprecated API *******/

/**
 * gst_rtsp_session_next_timeout:
 * @session: a #GstRTSPSession
 * @now: (transfer none): the current system time
 *
 * Get the amount of milliseconds till the session will expire.
 *
 * Returns: the amount of milliseconds since the session will time out.
 *
 * Deprecated: Use gst_rtsp_session_next_timeout_usec() instead.
 */
#ifndef GST_REMOVE_DEPRECATED
G_GNUC_BEGIN_IGNORE_DEPRECATIONS gint
gst_rtsp_session_next_timeout (GstRTSPSession * session, GTimeVal * now)
{
  GstRTSPSessionPrivate *priv;
  gint res;
  GstClockTime last_access, now_ns;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), -1);
  g_return_val_if_fail (now != NULL, -1);

  priv = session->priv;

  g_mutex_lock (&priv->last_access_lock);
  if (g_atomic_int_get (&priv->expire_count) != 0) {
    /* touch session when the expire count is not 0 */
    priv->last_access_monotonic_time = g_get_monotonic_time ();
    priv->last_access_real_time = g_get_real_time ();
  }

  last_access = GST_USECOND * (priv->last_access_real_time);

  /* add timeout allow for priv->extra_time_timeout
   * seconds of extra time */
  last_access += priv->timeout * GST_SECOND +
      (priv->extra_time_timeout * GST_SECOND);

  g_mutex_unlock (&priv->last_access_lock);

  now_ns = GST_TIMEVAL_TO_TIME (*now);

  if (last_access > now_ns) {
    res = GST_TIME_AS_MSECONDS (last_access - now_ns);
  } else {
    res = 0;
  }

  return res;
}

G_GNUC_END_IGNORE_DEPRECATIONS
#endif
/**
 * gst_rtsp_session_is_expired_usec:
 * @session: a #GstRTSPSession
 * @now: the current monotonic time
 *
 * Check if @session timeout out.
 *
 * Returns: %TRUE if @session timed out
 */
    gboolean
gst_rtsp_session_is_expired_usec (GstRTSPSession * session, gint64 now)
{
  gboolean res;

  res = (gst_rtsp_session_next_timeout_usec (session, now) == 0);

  return res;
}


/****** Deprecated API *******/

/**
 * gst_rtsp_session_is_expired:
 * @session: a #GstRTSPSession
 * @now: (transfer none): the current system time
 *
 * Check if @session timeout out.
 *
 * Returns: %TRUE if @session timed out
 *
 * Deprecated: Use gst_rtsp_session_is_expired_usec() instead.
 */
#ifndef GST_REMOVE_DEPRECATED
G_GNUC_BEGIN_IGNORE_DEPRECATIONS gboolean
gst_rtsp_session_is_expired (GstRTSPSession * session, GTimeVal * now)
{
  gboolean res;

  res = gst_rtsp_session_next_timeout_usec (session,
      (now->tv_sec * G_USEC_PER_SEC) + (now->tv_usec));

  return res;
}

G_GNUC_END_IGNORE_DEPRECATIONS
#endif
