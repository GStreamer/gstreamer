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

static void gst_rtsp_session_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_session_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
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
      g_param_spec_uint ("timeout", "timeout", "the timeout of the session (0 = never)",
          0, G_MAXUINT, DEFAULT_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_rtsp_session_init (GstRTSPSession * session)
{
  session->timeout = DEFAULT_TIMEOUT;
  g_get_current_time (&session->create_time);
  gst_rtsp_session_touch (session);
}

static void
gst_rtsp_session_free_stream (GstRTSPSessionStream *stream)
{
  g_message ("free session stream %p", stream);

  /* remove callbacks now */
  gst_rtsp_session_stream_set_callbacks (stream, NULL, NULL, NULL, NULL);
  gst_rtsp_session_stream_set_keepalive (stream, NULL, NULL, NULL);

  if (stream->trans.transport)
    gst_rtsp_transport_free (stream->trans.transport);

  g_free (stream);
}

static void
gst_rtsp_session_free_media (GstRTSPSessionMedia *media, GstRTSPSession *session)
{
  guint size, i;

  size = media->streams->len;

  g_message ("free session media %p", media);

  gst_rtsp_session_media_set_state (media, GST_STATE_NULL);

  for (i = 0; i < size; i++) {
    GstRTSPSessionStream *stream;

    stream = g_array_index (media->streams, GstRTSPSessionStream *, i);

    if (stream)
      gst_rtsp_session_free_stream (stream);
  }
  g_array_free (media->streams, TRUE);

  if (media->url)
    gst_rtsp_url_free (media->url);

  if (media->media)
    g_object_unref (media->media);

  g_free (media);
}

static void
gst_rtsp_session_finalize (GObject * obj)
{
  GstRTSPSession *session;

  session = GST_RTSP_SESSION (obj);

  g_message ("finalize session %p", session);

  /* free all media */
  g_list_foreach (session->medias, (GFunc) gst_rtsp_session_free_media,
		  session);
  g_list_free (session->medias);

  /* free session id */
  g_free (session->sessionid);

  G_OBJECT_CLASS (gst_rtsp_session_parent_class)->finalize (obj);
}

static void
gst_rtsp_session_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
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
gst_rtsp_session_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
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
 * @url: the url for the media
 * @media: a #GstRTSPMediaObject
 *
 * Manage the media object @obj in @sess. @url will be used to retrieve this
 * media from the session with gst_rtsp_session_get_media().
 *
 * Ownership is taken from @media.
 *
 * Returns: a new @GstRTSPSessionMedia object.
 */
GstRTSPSessionMedia *
gst_rtsp_session_manage_media (GstRTSPSession *sess, const GstRTSPUrl *uri,
    GstRTSPMedia *media)
{
  GstRTSPSessionMedia *result;
  guint n_streams;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (uri != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (media->prepared, NULL);

  result = g_new0 (GstRTSPSessionMedia, 1);
  result->media = media;
  result->url = gst_rtsp_url_copy ((GstRTSPUrl *)uri);
  result->state = GST_RTSP_STATE_INIT;

  /* prealloc the streams now, filled with NULL */
  n_streams = gst_rtsp_media_n_streams (media);
  result->streams = g_array_sized_new (FALSE, TRUE, sizeof (GstRTSPSessionStream *), n_streams);
  g_array_set_size (result->streams, n_streams);

  sess->medias = g_list_prepend (sess->medias, result);

  g_message ("manage new media %p in session %p", media, result);

  return result;
}

/**
 * gst_rtsp_session_release_media:
 * @sess: a #GstRTSPSession
 * @media: a #GstRTSPMediaObject
 *
 * Release the managed @media in @sess, freeing the memory allocated by it.
 *
 * Returns: %TRUE if there are more media session left in @sess.
 */
gboolean
gst_rtsp_session_release_media (GstRTSPSession *sess,
    GstRTSPSessionMedia *media)
{
  GList *walk, *next;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), FALSE);
  g_return_val_if_fail (media != NULL, FALSE);

  for (walk = sess->medias; walk;) {
    GstRTSPSessionMedia *find;

    find = (GstRTSPSessionMedia *) walk->data; 
    next = g_list_next (walk);

    if (find == media) {
      sess->medias = g_list_delete_link (sess->medias, walk);

      gst_rtsp_session_free_media (find, sess);
      break;
    }
    walk = next;
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
gst_rtsp_session_get_media (GstRTSPSession *sess, const GstRTSPUrl *url)
{
  GstRTSPSessionMedia *result;
  GList *walk;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  result = NULL;

  for (walk = sess->medias; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionMedia *) walk->data; 

    if (strcmp (result->url->abspath, url->abspath) == 0)
      break;

    result = NULL;
  }
  return result;
}

/**
 * gst_rtsp_session_media_get_stream:
 * @media: a #GstRTSPSessionMedia
 * @idx: the stream index
 *
 * Get a previously created or create a new #GstRTSPSessionStream at @idx.
 *
 * Returns: a #GstRTSPSessionStream that is valid until the session of @media
 * is unreffed.
 */
GstRTSPSessionStream *
gst_rtsp_session_media_get_stream (GstRTSPSessionMedia *media, guint idx)
{
  GstRTSPSessionStream *result;
  GstRTSPMediaStream *media_stream;

  g_return_val_if_fail (media != NULL, NULL);
  g_return_val_if_fail (media->media != NULL, NULL);

  if (idx >= media->streams->len)
    return NULL;

  result = g_array_index (media->streams, GstRTSPSessionStream *, idx);
  if (result == NULL) {
    media_stream = gst_rtsp_media_get_stream (media->media, idx);
    if (media_stream == NULL)
      goto no_media;

    result = g_new0 (GstRTSPSessionStream, 1);
    result->trans.idx = idx;
    result->trans.transport = NULL;
    result->media_stream = media_stream;

    g_array_index (media->streams, GstRTSPSessionStream *, idx) = result;
  }
  return result;

  /* ERRORS */
no_media:
  {
    return NULL;
  }
}

gboolean
gst_rtsp_session_media_alloc_channels (GstRTSPSessionMedia *media, GstRTSPRange *range)
{
  range->min = media->counter++;
  range->max = media->counter++;

  return TRUE;
}

/**
 * gst_rtsp_session_new:
 *
 * Create a new #GstRTSPSession instance.
 */
GstRTSPSession *
gst_rtsp_session_new (const gchar *sessionid)
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
gst_rtsp_session_get_sessionid (GstRTSPSession *session)
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
gst_rtsp_session_set_timeout (GstRTSPSession *session, guint timeout)
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
gst_rtsp_session_get_timeout (GstRTSPSession *session)
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
gst_rtsp_session_touch (GstRTSPSession *session)
{
  g_return_if_fail (GST_IS_RTSP_SESSION (session));

  g_get_current_time (&session->last_access);
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
gst_rtsp_session_next_timeout (GstRTSPSession *session, GTimeVal *now)
{
  gint res;
  GstClockTime last_access, now_ns;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (session), -1);
  g_return_val_if_fail (now != NULL, -1);

  last_access = GST_TIMEVAL_TO_TIME (session->last_access);
  /* add timeout allow for 5 seconds of extra time */
  last_access += session->timeout * GST_SECOND + (5 * GST_SECOND);

  now_ns = GST_TIMEVAL_TO_TIME (*now);

  if (last_access > now_ns) 
    res =  GST_TIME_AS_MSECONDS (last_access - now_ns);
  else
    res =  0;

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
gst_rtsp_session_is_expired (GstRTSPSession *session, GTimeVal *now)
{
  gboolean res;

  res = (gst_rtsp_session_next_timeout (session, now) == 0);

  return res;
}

/**
 * gst_rtsp_session_stream_init_udp:
 * @stream: a #GstRTSPSessionStream
 * @ct: a client #GstRTSPTransport
 *
 * Set @ct as the client transport and create and return a matching server
 * transport.
 * 
 * Returns: a server transport or NULL if something went wrong.
 */
GstRTSPTransport *
gst_rtsp_session_stream_set_transport (GstRTSPSessionStream *stream, 
    GstRTSPTransport *ct)
{
  GstRTSPTransport *st;

  g_return_val_if_fail (stream != NULL, NULL);
  g_return_val_if_fail (ct != NULL, NULL);

  /* prepare the server transport */
  gst_rtsp_transport_new (&st);

  st->trans = ct->trans;
  st->profile = ct->profile;
  st->lower_transport = ct->lower_transport;
  st->client_port = ct->client_port;
  st->interleaved = ct->interleaved;
  st->server_port = stream->media_stream->server_port;

  /* keep track of the transports in the stream. */
  if (stream->trans.transport)
    gst_rtsp_transport_free (stream->trans.transport);
  stream->trans.transport = ct;

  return st;
}

/**
 * gst_rtsp_session_stream_set_callbacks:
 * @stream: a #GstRTSPSessionStream
 * @send_rtp: a callback called when RTP should be sent
 * @send_rtcp: a callback called when RTCP should be sent
 * @user_data: user data passed to callbacks
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when data for a stream should be sent
 * to a client. This is usually used when sending RTP/RTCP over TCP.
 */
void
gst_rtsp_session_stream_set_callbacks (GstRTSPSessionStream *stream,
    GstRTSPSendFunc send_rtp, GstRTSPSendFunc send_rtcp, gpointer user_data,
    GDestroyNotify  notify)
{
  stream->trans.send_rtp = send_rtp;
  stream->trans.send_rtcp = send_rtcp;
  if (stream->trans.notify)
    stream->trans.notify (stream->trans.user_data);
  stream->trans.user_data = user_data;
  stream->trans.notify = notify;
}

/**
 * gst_rtsp_session_stream_set_keepalive:
 * @stream: a #GstRTSPSessionStream
 * @keep_alive: a callback called when the receiver is active
 * @user_data: user data passed to callback
 * @notify: called with the user_data when no longer needed.
 *
 * Install callbacks that will be called when RTCP packets are received from the
 * receiver of @stream.
 */
void
gst_rtsp_session_stream_set_keepalive (GstRTSPSessionStream *stream,
    GstRTSPKeepAliveFunc keep_alive, gpointer user_data, GDestroyNotify  notify)
{
  stream->trans.keep_alive = keep_alive;
  if (stream->trans.ka_notify)
    stream->trans.ka_notify (stream->trans.ka_user_data);
  stream->trans.ka_user_data = user_data;
  stream->trans.ka_notify = notify;
}

/**
 * gst_rtsp_session_media_set_state:
 * @media: a #GstRTSPSessionMedia
 * @state: the new state
 *
 * Tell the media object @media to change to @state.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_session_media_set_state (GstRTSPSessionMedia *media, GstState state)
{
  gboolean ret;

  g_return_val_if_fail (media != NULL, FALSE);

  ret = gst_rtsp_media_set_state (media->media, state, media->streams);

  return ret;
}

