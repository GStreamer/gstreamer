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
#include <string.h>

#include "rtsp-session.h"

#undef DEBUG

#define DEFAULT_TIMEOUT	60

enum
{
  PROP_0,
  PROP_SESSIONID,
  PROP_TIMEOUT,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_session_debug);
#define GST_CAT_DEFAULT rtsp_session_debug

static void gst_rtsp_session_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_session_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPSession, gst_rtsp_session, G_TYPE_OBJECT);

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

  GST_DEBUG_CATEGORY_INIT (rtsp_session_debug, "rtspsession", 0,
      "GstRTSPSession");
}

static void
gst_rtsp_session_init (GstRTSPSession * session)
{
  session->timeout = DEFAULT_TIMEOUT;
  g_get_current_time (&session->create_time);
  gst_rtsp_session_touch (session);
}

static void
gst_rtsp_session_finalize (GObject * obj)
{
  GstRTSPSession *session;

  session = GST_RTSP_SESSION (obj);

  GST_INFO ("finalize session %p", session);

  /* free all media */
  g_list_free_full (session->medias, g_object_unref);

  /* free session id */
  g_free (session->sessionid);

  G_OBJECT_CLASS (gst_rtsp_session_parent_class)->finalize (obj);
}

static void
gst_rtsp_session_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPSession *session = GST_RTSP_SESSION (object);

  switch (propid) {
    case PROP_SESSIONID:
      g_value_set_string (value, session->sessionid);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, gst_rtsp_session_get_timeout (session));
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

  switch (propid) {
    case PROP_SESSIONID:
      g_free (session->sessionid);
      session->sessionid = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      gst_rtsp_session_set_timeout (session, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_session_manage_media:
 * @sess: a #GstRTSPSession
 * @uri: the uri for the media
 * @media: (transfer full): a #GstRTSPMedia
 *
 * Manage the media object @obj in @sess. @uri will be used to retrieve this
 * media from the session with gst_rtsp_session_get_media().
 *
 * Ownership is taken from @media.
 *
 * Returns: a new @GstRTSPSessionMedia object.
 */
GstRTSPSessionMedia *
gst_rtsp_session_manage_media (GstRTSPSession * sess, const GstRTSPUrl * uri,
    GstRTSPMedia * media)
{
  GstRTSPSessionMedia *result;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (uri != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (media->status == GST_RTSP_MEDIA_STATUS_PREPARED, NULL);

  result = gst_rtsp_session_media_new (uri, media);

  sess->medias = g_list_prepend (sess->medias, result);

  GST_INFO ("manage new media %p in session %p", media, result);

  return result;
}

/**
 * gst_rtsp_session_release_media:
 * @sess: a #GstRTSPSession
 * @media: a #GstRTSPMedia
 *
 * Release the managed @media in @sess, freeing the memory allocated by it.
 *
 * Returns: %TRUE if there are more media session left in @sess.
 */
gboolean
gst_rtsp_session_release_media (GstRTSPSession * sess,
    GstRTSPSessionMedia * media)
{
  GList *find;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), FALSE);
  g_return_val_if_fail (media != NULL, FALSE);

  find = g_list_find (sess->medias, media);
  if (find) {
    g_object_unref (find->data);
    sess->medias = g_list_delete_link (sess->medias, find);
  }
  return (sess->medias != NULL);
}

/**
 * gst_rtsp_session_get_media:
 * @sess: a #GstRTSPSession
 * @url: the url for the media
 *
 * Get the session media of the @url.
 *
 * Returns: the configuration for @url in @sess.
 */
GstRTSPSessionMedia *
gst_rtsp_session_get_media (GstRTSPSession * sess, const GstRTSPUrl * url)
{
  GstRTSPSessionMedia *result;
  GList *walk;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  result = NULL;

  for (walk = sess->medias; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionMedia *) walk->data;

    if (g_str_equal (result->url->abspath, url->abspath))
      break;

    result = NULL;
  }
  return result;
}

/**
 * gst_rtsp_session_new:
 *
 * Create a new #GstRTSPSession instance.
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
 * Returns: the sessionid of @session. The value remains valid as long as
 * @session is alive.
 */
const gchar *
gst_rtsp_session_get_sessionid (GstRTSPSession * session)
{
  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), NULL);

  return session->sessionid;
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
  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  session->timeout = timeout;
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
  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), 0);

  return session->timeout;
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
  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  g_get_current_time (&session->last_access);
}

void
gst_rtsp_session_prevent_expire (GstRTSPSession * session)
{
  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  g_atomic_int_add (&session->expire_count, 1);
}

void
gst_rtsp_session_allow_expire (GstRTSPSession * session)
{
  g_atomic_int_add (&session->expire_count, -1);
}

/**
 * gst_rtsp_session_next_timeout:
 * @session: a #GstRTSPSession
 * @now: the current system time
 *
 * Get the amount of milliseconds till the session will expire.
 *
 * Returns: the amount of milliseconds since the session will time out.
 */
gint
gst_rtsp_session_next_timeout (GstRTSPSession * session, GTimeVal * now)
{
  gint res;
  GstClockTime last_access, now_ns;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), -1);
  g_return_val_if_fail (now != NULL, -1);

  if (g_atomic_int_get (&session->expire_count) != 0) {
    /* touch session when the expire count is not 0 */
    g_get_current_time (&session->last_access);
  }

  last_access = GST_TIMEVAL_TO_TIME (session->last_access);
  /* add timeout allow for 5 seconds of extra time */
  last_access += session->timeout * GST_SECOND + (5 * GST_SECOND);

  now_ns = GST_TIMEVAL_TO_TIME (*now);

  if (last_access > now_ns)
    res = GST_TIME_AS_MSECONDS (last_access - now_ns);
  else
    res = 0;

  return res;
}

/**
 * gst_rtsp_session_is_expired:
 * @session: a #GstRTSPSession
 * @now: the current system time
 *
 * Check if @session timeout out.
 *
 * Returns: %TRUE if @session timed out
 */
gboolean
gst_rtsp_session_is_expired (GstRTSPSession * session, GTimeVal * now)
{
  gboolean res;

  res = (gst_rtsp_session_next_timeout (session, now) == 0);

  return res;
}
