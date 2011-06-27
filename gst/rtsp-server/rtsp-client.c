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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "rtsp-client.h"
#include "rtsp-sdp.h"
#include "rtsp-params.h"

/* temporary multicast address until it's configurable somewhere */
#define MCAST_ADDRESS "224.2.0.1"

static GMutex *tunnels_lock;
static GHashTable *tunnels;

enum
{
  PROP_0,
  PROP_SESSION_POOL,
  PROP_MEDIA_MAPPING,
  PROP_LAST
};

enum
{
  SIGNAL_CLOSED,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_client_debug);
#define GST_CAT_DEFAULT rtsp_client_debug

static guint gst_rtsp_client_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_finalize (GObject * obj);

static void client_session_finalized (GstRTSPClient * client,
    GstRTSPSession * session);
static void unlink_session_streams (GstRTSPClient * client,
    GstRTSPSession * session, GstRTSPSessionMedia * media);

G_DEFINE_TYPE (GstRTSPClient, gst_rtsp_client, G_TYPE_OBJECT);

static void
gst_rtsp_client_class_init (GstRTSPClientClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_client_get_property;
  gobject_class->set_property = gst_rtsp_client_set_property;
  gobject_class->finalize = gst_rtsp_client_finalize;

  g_object_class_install_property (gobject_class, PROP_SESSION_POOL,
      g_param_spec_object ("session-pool", "Session Pool",
          "The session pool to use for client session",
          GST_TYPE_RTSP_SESSION_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MEDIA_MAPPING,
      g_param_spec_object ("media-mapping", "Media Mapping",
          "The media mapping to use for client session",
          GST_TYPE_RTSP_MEDIA_MAPPING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_client_signals[SIGNAL_CLOSED] =
      g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPClientClass, closed), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  tunnels =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  tunnels_lock = g_mutex_new ();

  GST_DEBUG_CATEGORY_INIT (rtsp_client_debug, "rtspclient", 0, "GstRTSPClient");
}

static void
gst_rtsp_client_init (GstRTSPClient * client)
{
}

static void
client_unlink_session (GstRTSPClient * client, GstRTSPSession * session)
{
  GList *medias;

  /* unlink all media managed in this session */
  for (medias = session->medias; medias; medias = g_list_next (medias)) {
    unlink_session_streams (client, session,
        (GstRTSPSessionMedia *) medias->data);
  }
}

static void
client_cleanup_sessions (GstRTSPClient * client)
{
  GList *sessions;

  /* remove weak-ref from sessions */
  for (sessions = client->sessions; sessions; sessions = g_list_next (sessions)) {
    GstRTSPSession *session = (GstRTSPSession *) sessions->data;
    g_object_weak_unref (G_OBJECT (session),
        (GWeakNotify) client_session_finalized, client);
    client_unlink_session (client, session);
  }
  g_list_free (client->sessions);
  client->sessions = NULL;
}

/* A client is finalized when the connection is broken */
static void
gst_rtsp_client_finalize (GObject * obj)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (obj);

  GST_INFO ("finalize client %p", client);

  client_cleanup_sessions (client);

  gst_rtsp_connection_free (client->connection);
  if (client->session_pool)
    g_object_unref (client->session_pool);
  if (client->media_mapping)
    g_object_unref (client->media_mapping);
  if (client->auth)
    g_object_unref (client->auth);

  if (client->uri)
    gst_rtsp_url_free (client->uri);
  if (client->media)
    g_object_unref (client->media);

  g_free (client->server_ip);

  G_OBJECT_CLASS (gst_rtsp_client_parent_class)->finalize (obj);
}

static void
gst_rtsp_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);

  switch (propid) {
    case PROP_SESSION_POOL:
      g_value_take_object (value, gst_rtsp_client_get_session_pool (client));
      break;
    case PROP_MEDIA_MAPPING:
      g_value_take_object (value, gst_rtsp_client_get_media_mapping (client));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);

  switch (propid) {
    case PROP_SESSION_POOL:
      gst_rtsp_client_set_session_pool (client, g_value_get_object (value));
      break;
    case PROP_MEDIA_MAPPING:
      gst_rtsp_client_set_media_mapping (client, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_client_new:
 *
 * Create a new #GstRTSPClient instance.
 *
 * Returns: a new #GstRTSPClient
 */
GstRTSPClient *
gst_rtsp_client_new (void)
{
  GstRTSPClient *result;

  result = g_object_new (GST_TYPE_RTSP_CLIENT, NULL);

  return result;
}

static void
send_response (GstRTSPClient * client, GstRTSPSession * session,
    GstRTSPMessage * response)
{
  gst_rtsp_message_add_header (response, GST_RTSP_HDR_SERVER,
      "GStreamer RTSP server");

  /* remove any previous header */
  gst_rtsp_message_remove_header (response, GST_RTSP_HDR_SESSION, -1);

  /* add the new session header for new session ids */
  if (session) {
    gchar *str;

    if (session->timeout != 60)
      str =
          g_strdup_printf ("%s; timeout=%d", session->sessionid,
          session->timeout);
    else
      str = g_strdup (session->sessionid);

    gst_rtsp_message_take_header (response, GST_RTSP_HDR_SESSION, str);
  }

  if (gst_debug_category_get_threshold (rtsp_client_debug) >= GST_LEVEL_LOG) {
    gst_rtsp_message_dump (response);
  }

  gst_rtsp_watch_send_message (client->watch, response, NULL);
  gst_rtsp_message_unset (response);
}

static void
send_generic_response (GstRTSPClient * client, GstRTSPStatusCode code,
    GstRTSPClientState * state)
{
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  send_response (client, NULL, state->response);
}

static void
handle_unauthorized_request (GstRTSPClient * client, GstRTSPAuth * auth,
    GstRTSPClientState * state)
{
  gst_rtsp_message_init_response (state->response, GST_RTSP_STS_UNAUTHORIZED,
      gst_rtsp_status_as_text (GST_RTSP_STS_UNAUTHORIZED), state->request);

  if (auth) {
    /* and let the authentication manager setup the auth tokens */
    gst_rtsp_auth_setup_auth (auth, client, 0, state);
  }

  send_response (client, state->session, state->response);
}


static gboolean
compare_uri (const GstRTSPUrl * uri1, const GstRTSPUrl * uri2)
{
  if (uri1 == NULL || uri2 == NULL)
    return FALSE;

  if (strcmp (uri1->abspath, uri2->abspath))
    return FALSE;

  return TRUE;
}

/* this function is called to initially find the media for the DESCRIBE request
 * but is cached for when the same client (without breaking the connection) is
 * doing a setup for the exact same url. */
static GstRTSPMedia *
find_media (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPAuth *auth;

  if (!compare_uri (client->uri, state->uri)) {
    /* remove any previously cached values before we try to construct a new
     * media for uri */
    if (client->uri)
      gst_rtsp_url_free (client->uri);
    client->uri = NULL;
    if (client->media)
      g_object_unref (client->media);
    client->media = NULL;

    if (!client->media_mapping)
      goto no_mapping;

    /* find the factory for the uri first */
    if (!(factory =
            gst_rtsp_media_mapping_find_factory (client->media_mapping,
                state->uri)))
      goto no_factory;

    state->factory = factory;

    /* check if we have access to the factory */
    if ((auth = gst_rtsp_media_factory_get_auth (factory))) {
      if (!gst_rtsp_auth_check (auth, client, 0, state))
        goto not_allowed;

      g_object_unref (auth);
    }

    /* prepare the media and add it to the pipeline */
    if (!(media = gst_rtsp_media_factory_construct (factory, state->uri)))
      goto no_media;

    g_object_unref (factory);
    factory = NULL;
    state->factory = NULL;

    /* set ipv6 on the media before preparing */
    media->is_ipv6 = client->is_ipv6;
    state->media = media;

    /* prepare the media */
    if (!(gst_rtsp_media_prepare (media)))
      goto no_prepare;

    /* now keep track of the uri and the media */
    client->uri = gst_rtsp_url_copy (state->uri);
    client->media = media;
  } else {
    /* we have seen this uri before, used cached media */
    media = client->media;
    state->media = media;
    GST_INFO ("reusing cached media %p", media);
  }

  if (media)
    g_object_ref (media);

  return media;

  /* ERRORS */
no_mapping:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    return NULL;
  }
no_factory:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    return NULL;
  }
not_allowed:
  {
    handle_unauthorized_request (client, auth, state);
    g_object_unref (factory);
    g_object_unref (auth);
    return NULL;
  }
no_media:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, state);
    g_object_unref (factory);
    return NULL;
  }
no_prepare:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, state);
    g_object_unref (media);
    return NULL;
  }
}

static gboolean
do_send_data (GstBuffer * buffer, guint8 channel, GstRTSPClient * client)
{
  GstRTSPMessage message = { 0 };
  guint8 *data;
  guint size;

  gst_rtsp_message_init_data (&message, channel);

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  gst_rtsp_message_take_body (&message, data, size);

  /* FIXME, client->watch could have been finalized here, we need to keep an
   * extra refcount to the watch.  */
  gst_rtsp_watch_send_message (client->watch, &message, NULL);

  gst_rtsp_message_steal_body (&message, &data, &size);
  gst_rtsp_message_unset (&message);

  return TRUE;
}

static gboolean
do_send_data_list (GstBufferList * blist, guint8 channel,
    GstRTSPClient * client)
{
  GstBufferListIterator *it;

  it = gst_buffer_list_iterate (blist);
  while (gst_buffer_list_iterator_next_group (it)) {
    GstBuffer *group = gst_buffer_list_iterator_merge_group (it);

    if (group == NULL)
      continue;

    do_send_data (group, channel, client);
  }
  gst_buffer_list_iterator_free (it);

  return TRUE;
}

static void
link_stream (GstRTSPClient * client, GstRTSPSession * session,
    GstRTSPSessionStream * stream)
{
  GST_DEBUG ("client %p: linking stream %p", client, stream);
  gst_rtsp_session_stream_set_callbacks (stream, (GstRTSPSendFunc) do_send_data,
      (GstRTSPSendFunc) do_send_data, (GstRTSPSendListFunc) do_send_data_list,
      (GstRTSPSendListFunc) do_send_data_list, client, NULL);
  client->streams = g_list_prepend (client->streams, stream);
  /* make sure our session can't expire */
  gst_rtsp_session_prevent_expire (session);
}

static void
unlink_stream (GstRTSPClient * client, GstRTSPSession * session,
    GstRTSPSessionStream * stream)
{
  GST_DEBUG ("client %p: unlinking stream %p", client, stream);
  gst_rtsp_session_stream_set_callbacks (stream, NULL, NULL, NULL, NULL, NULL,
      NULL);
  client->streams = g_list_remove (client->streams, stream);
  /* our session can now expire */
  gst_rtsp_session_allow_expire (session);
}

static void
unlink_session_streams (GstRTSPClient * client, GstRTSPSession * session,
    GstRTSPSessionMedia * media)
{
  guint n_streams, i;

  n_streams = gst_rtsp_media_n_streams (media->media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPSessionStream *sstream;
    GstRTSPTransport *tr;

    /* get the stream as configured in the session */
    sstream = gst_rtsp_session_media_get_stream (media, i);
    /* get the transport, if there is no transport configured, skip this stream */
    if (!(tr = sstream->trans.transport))
      continue;

    if (tr->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
      /* for TCP, unlink the stream from the TCP connection of the client */
      unlink_stream (client, session, sstream);
    }
  }
}

static void
close_connection (GstRTSPClient * client)
{
  const gchar *tunnelid;

  GST_DEBUG ("client %p: closing connection", client);

  if ((tunnelid = gst_rtsp_connection_get_tunnelid (client->connection))) {
    g_mutex_lock (tunnels_lock);
    /* remove from tunnelids */
    g_hash_table_remove (tunnels, tunnelid);
    g_mutex_unlock (tunnels_lock);
  }

  gst_rtsp_connection_close (client->connection);
  if (client->watchid) {
    g_source_destroy ((GSource *) client->watch);
    client->watchid = 0;
    client->watch = NULL;
  }
}

static gboolean
handle_teardown_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPSession *session;
  GstRTSPSessionMedia *media;
  GstRTSPStatusCode code;

  if (!state->session)
    goto no_session;

  session = state->session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, state->uri);
  if (!media)
    goto not_found;

  state->sessmedia = media;

  /* unlink the all TCP callbacks */
  unlink_session_streams (client, session, media);

  /* remove the session from the watched sessions */
  g_object_weak_unref (G_OBJECT (session),
      (GWeakNotify) client_session_finalized, client);
  client->sessions = g_list_remove (client->sessions, session);

  gst_rtsp_session_media_set_state (media, GST_STATE_NULL);

  /* unmanage the media in the session, returns false if all media session
   * are torn down. */
  if (!gst_rtsp_session_release_media (session, media)) {
    /* remove the session */
    gst_rtsp_session_pool_remove (client->session_pool, session);
  }
  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_CONNECTION,
      "close");

  send_response (client, session, state->response);

  close_connection (client);

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, state);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    return FALSE;
  }
}

static gboolean
handle_get_param_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;

  res = gst_rtsp_message_get_body (state->request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0) {
    /* no body, keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, state);
  } else {
    /* there is a body, handle the params */
    res = gst_rtsp_params_get (client, state);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_response (client, state->session, state->response);
  }
  return TRUE;

  /* ERRORS */
bad_request:
  {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, state);
    return FALSE;
  }
}

static gboolean
handle_set_param_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;

  res = gst_rtsp_message_get_body (state->request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0) {
    /* no body, keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, state);
  } else {
    /* there is a body, handle the params */
    res = gst_rtsp_params_set (client, state);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_response (client, state->session, state->response);
  }
  return TRUE;

  /* ERRORS */
bad_request:
  {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, state);
    return FALSE;
  }
}

static gboolean
handle_pause_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPSession *session;
  GstRTSPSessionMedia *media;
  GstRTSPStatusCode code;

  if (!(session = state->session))
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, state->uri);
  if (!media)
    goto not_found;

  state->sessmedia = media;

  /* the session state must be playing or recording */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_RECORDING)
    goto invalid_state;

  /* unlink the all TCP callbacks */
  unlink_session_streams (client, session, media);

  /* then pause sending */
  gst_rtsp_session_media_set_state (media, GST_STATE_PAUSED);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  send_response (client, session, state->response);

  /* the state is now READY */
  media->state = GST_RTSP_STATE_READY;

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, state);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        state);
    return FALSE;
  }
}

static gboolean
handle_play_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPSession *session;
  GstRTSPSessionMedia *media;
  GstRTSPStatusCode code;
  GString *rtpinfo;
  guint n_streams, i, infocount;
  guint timestamp, seqnum;
  gchar *str;
  GstRTSPTimeRange *range;
  GstRTSPResult res;

  if (!(session = state->session))
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, state->uri);
  if (!media)
    goto not_found;

  state->sessmedia = media;

  /* the session state must be playing or ready */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_READY)
    goto invalid_state;

  /* parse the range header if we have one */
  res =
      gst_rtsp_message_get_header (state->request, GST_RTSP_HDR_RANGE, &str, 0);
  if (res == GST_RTSP_OK) {
    if (gst_rtsp_range_parse (str, &range) == GST_RTSP_OK) {
      /* we have a range, seek to the position */
      gst_rtsp_media_seek (media->media, range);
      gst_rtsp_range_free (range);
    }
  }

  /* grab RTPInfo from the payloaders now */
  rtpinfo = g_string_new ("");

  n_streams = gst_rtsp_media_n_streams (media->media);
  for (i = 0, infocount = 0; i < n_streams; i++) {
    GstRTSPSessionStream *sstream;
    GstRTSPMediaStream *stream;
    GstRTSPTransport *tr;
    GObjectClass *payobjclass;
    gchar *uristr;

    /* get the stream as configured in the session */
    sstream = gst_rtsp_session_media_get_stream (media, i);
    /* get the transport, if there is no transport configured, skip this stream */
    if (!(tr = sstream->trans.transport)) {
      GST_INFO ("stream %d is not configured", i);
      continue;
    }

    if (tr->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
      /* for TCP, link the stream to the TCP connection of the client */
      link_stream (client, session, sstream);
    }

    stream = sstream->media_stream;

    payobjclass = G_OBJECT_GET_CLASS (stream->payloader);

    if (g_object_class_find_property (payobjclass, "seqnum") &&
        g_object_class_find_property (payobjclass, "timestamp")) {
      GObject *payobj;

      payobj = G_OBJECT (stream->payloader);

      /* only add RTP-Info for streams with seqnum and timestamp */
      g_object_get (payobj, "seqnum", &seqnum, "timestamp", &timestamp, NULL);

      if (infocount > 0)
        g_string_append (rtpinfo, ", ");

      uristr = gst_rtsp_url_get_request_uri (state->uri);
      g_string_append_printf (rtpinfo, "url=%s/stream=%d;seq=%u;rtptime=%u",
          uristr, i, seqnum, timestamp);
      g_free (uristr);

      infocount++;
    } else {
      GST_WARNING ("RTP-Info cannot be determined for stream %d", i);
    }
  }

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  /* add the RTP-Info header */
  if (infocount > 0) {
    str = g_string_free (rtpinfo, FALSE);
    gst_rtsp_message_take_header (state->response, GST_RTSP_HDR_RTP_INFO, str);
  } else {
    g_string_free (rtpinfo, TRUE);
  }

  /* add the range */
  str = gst_rtsp_media_get_range_string (media->media, TRUE);
  gst_rtsp_message_take_header (state->response, GST_RTSP_HDR_RANGE, str);

  send_response (client, session, state->response);

  /* start playing after sending the request */
  gst_rtsp_session_media_set_state (media, GST_STATE_PLAYING);

  media->state = GST_RTSP_STATE_PLAYING;

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, state);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        state);
    return FALSE;
  }
}

static void
do_keepalive (GstRTSPSession * session)
{
  GST_INFO ("keep session %p alive", session);
  gst_rtsp_session_touch (session);
}

static gboolean
handle_setup_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPResult res;
  GstRTSPUrl *uri;
  gchar *transport;
  gchar **transports;
  gboolean have_transport;
  GstRTSPTransport *ct, *st;
  gint i;
  GstRTSPLowerTrans supported;
  GstRTSPStatusCode code;
  GstRTSPSession *session;
  GstRTSPSessionStream *stream;
  gchar *trans_str, *pos;
  guint streamid;
  GstRTSPSessionMedia *media;
  GstRTSPUrl *url;

  uri = state->uri;

  /* the uri contains the stream number we added in the SDP config, which is
   * always /stream=%d so we need to strip that off 
   * parse the stream we need to configure, look for the stream in the abspath
   * first and then in the query. */
  if (uri->abspath == NULL || !(pos = strstr (uri->abspath, "/stream="))) {
    if (uri->query == NULL || !(pos = strstr (uri->query, "/stream=")))
      goto bad_request;
  }

  /* we can mofify the parse uri in place */
  *pos = '\0';

  pos += strlen ("/stream=");
  if (sscanf (pos, "%u", &streamid) != 1)
    goto bad_request;

  /* parse the transport */
  res =
      gst_rtsp_message_get_header (state->request, GST_RTSP_HDR_TRANSPORT,
      &transport, 0);
  if (res != GST_RTSP_OK)
    goto no_transport;

  transports = g_strsplit (transport, ",", 0);
  gst_rtsp_transport_new (&ct);

  /* init transports */
  have_transport = FALSE;
  gst_rtsp_transport_init (ct);

  /* our supported transports */
  supported = GST_RTSP_LOWER_TRANS_UDP |
      GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP;

  /* loop through the transports, try to parse */
  for (i = 0; transports[i]; i++) {
    res = gst_rtsp_transport_parse (transports[i], ct);
    if (res != GST_RTSP_OK) {
      /* no valid transport, search some more */
      GST_WARNING ("could not parse transport %s", transports[i]);
      goto next;
    }

    /* we have a transport, see if it's RTP/AVP */
    if (ct->trans != GST_RTSP_TRANS_RTP || ct->profile != GST_RTSP_PROFILE_AVP) {
      GST_WARNING ("invalid transport %s", transports[i]);
      goto next;
    }

    if (!(ct->lower_transport & supported)) {
      GST_WARNING ("unsupported transport %s", transports[i]);
      goto next;
    }

    /* we have a valid transport */
    GST_INFO ("found valid transport %s", transports[i]);
    have_transport = TRUE;
    break;

  next:
    gst_rtsp_transport_init (ct);
  }
  g_strfreev (transports);

  /* we have not found anything usable, error out */
  if (!have_transport)
    goto unsupported_transports;

  if (client->session_pool == NULL)
    goto no_pool;

  /* we have a valid transport now, set the destination of the client. */
  g_free (ct->destination);
  if (ct->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    ct->destination = g_strdup (MCAST_ADDRESS);
  } else {
    url = gst_rtsp_connection_get_url (client->connection);
    ct->destination = g_strdup (url->host);
  }

  session = state->session;

  if (session) {
    g_object_ref (session);
    /* get a handle to the configuration of the media in the session, this can
     * return NULL if this is a new url to manage in this session. */
    media = gst_rtsp_session_get_media (session, uri);
  } else {
    /* create a session if this fails we probably reached our session limit or
     * something. */
    if (!(session = gst_rtsp_session_pool_create (client->session_pool)))
      goto service_unavailable;

    state->session = session;

    /* we need a new media configuration in this session */
    media = NULL;
  }

  /* we have no media, find one and manage it */
  if (media == NULL) {
    GstRTSPMedia *m;

    /* get a handle to the configuration of the media in the session */
    if ((m = find_media (client, state))) {
      /* manage the media in our session now */
      media = gst_rtsp_session_manage_media (session, uri, m);
    }
  }

  /* if we stil have no media, error */
  if (media == NULL)
    goto not_found;

  state->sessmedia = media;

  /* fix the transports */
  if (ct->lower_transport & GST_RTSP_LOWER_TRANS_TCP) {
    /* check if the client selected channels for TCP */
    if (ct->interleaved.min == -1 || ct->interleaved.max == -1) {
      gst_rtsp_session_media_alloc_channels (media, &ct->interleaved);
    }
  }

  /* get a handle to the stream in the media */
  if (!(stream = gst_rtsp_session_media_get_stream (media, streamid)))
    goto no_stream;

  st = gst_rtsp_session_stream_set_transport (stream, ct);

  /* configure keepalive for this transport */
  gst_rtsp_session_stream_set_keepalive (stream,
      (GstRTSPKeepAliveFunc) do_keepalive, session, NULL);

  /* serialize the server transport */
  trans_str = gst_rtsp_transport_as_text (st);
  gst_rtsp_transport_free (st);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (state->response, code,
      gst_rtsp_status_as_text (code), state->request);

  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_TRANSPORT,
      trans_str);
  g_free (trans_str);

  send_response (client, session, state->response);

  /* update the state */
  switch (media->state) {
    case GST_RTSP_STATE_PLAYING:
    case GST_RTSP_STATE_RECORDING:
    case GST_RTSP_STATE_READY:
      /* no state change */
      break;
    default:
      media->state = GST_RTSP_STATE_READY;
      break;
  }
  g_object_unref (session);

  return TRUE;

  /* ERRORS */
bad_request:
  {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, state);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    g_object_unref (session);
    return FALSE;
  }
no_stream:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, state);
    g_object_unref (media);
    g_object_unref (session);
    return FALSE;
  }
no_transport:
  {
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, state);
    return FALSE;
  }
unsupported_transports:
  {
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, state);
    gst_rtsp_transport_free (ct);
    return FALSE;
  }
no_pool:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, state);
    return FALSE;
  }
service_unavailable:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, state);
    return FALSE;
  }
}

static GstSDPMessage *
create_sdp (GstRTSPClient * client, GstRTSPMedia * media)
{
  GstSDPMessage *sdp;
  GstSDPInfo info;
  const gchar *proto;

  gst_sdp_message_new (&sdp);

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");

  if (client->is_ipv6)
    proto = "IP6";
  else
    proto = "IP4";

  gst_sdp_message_set_origin (sdp, "-", "1188340656180883", "1", "IN", proto,
      client->server_ip);

  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtsp-server");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");
  gst_sdp_message_add_attribute (sdp, "type", "broadcast");
  gst_sdp_message_add_attribute (sdp, "control", "*");

  info.server_proto = proto;
  if (media->protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST)
    info.server_ip = MCAST_ADDRESS;
  else
    info.server_ip = client->server_ip;

  /* create an SDP for the media object */
  if (!gst_rtsp_sdp_from_media (sdp, &info, media))
    goto no_sdp;

  return sdp;

  /* ERRORS */
no_sdp:
  {
    gst_sdp_message_free (sdp);
    return NULL;
  }
}

/* for the describe we must generate an SDP */
static gboolean
handle_describe_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPResult res;
  GstSDPMessage *sdp;
  guint i, str_len;
  gchar *str, *content_base;
  GstRTSPMedia *media;

  /* check what kind of format is accepted, we don't really do anything with it
   * and always return SDP for now. */
  for (i = 0; i++;) {
    gchar *accept;

    res =
        gst_rtsp_message_get_header (state->request, GST_RTSP_HDR_ACCEPT,
        &accept, i);
    if (res == GST_RTSP_ENOTIMPL)
      break;

    if (g_ascii_strcasecmp (accept, "application/sdp") == 0)
      break;
  }

  /* find the media object for the uri */
  if (!(media = find_media (client, state)))
    goto no_media;

  /* create an SDP for the media object on this client */
  if (!(sdp = create_sdp (client, media)))
    goto no_sdp;

  g_object_unref (media);

  gst_rtsp_message_init_response (state->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), state->request);

  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  /* content base for some clients that might screw up creating the setup uri */
  str = gst_rtsp_url_get_request_uri (state->uri);
  str_len = strlen (str);

  /* check for trailing '/' and append one */
  if (str[str_len - 1] != '/') {
    content_base = g_malloc (str_len + 2);
    memcpy (content_base, str, str_len);
    content_base[str_len] = '/';
    content_base[str_len + 1] = '\0';
    g_free (str);
  } else {
    content_base = str;
  }

  GST_INFO ("adding content-base: %s", content_base);

  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_CONTENT_BASE,
      content_base);
  g_free (content_base);

  /* add SDP to the response body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (state->response, (guint8 *) str, strlen (str));
  gst_sdp_message_free (sdp);

  send_response (client, state->session, state->response);

  return TRUE;

  /* ERRORS */
no_media:
  {
    /* error reply is already sent */
    return FALSE;
  }
no_sdp:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, state);
    g_object_unref (media);
    return FALSE;
  }
}

static gboolean
handle_options_request (GstRTSPClient * client, GstRTSPClientState * state)
{
  GstRTSPMethod options;
  gchar *str;

  options = GST_RTSP_DESCRIBE |
      GST_RTSP_OPTIONS |
      GST_RTSP_PAUSE |
      GST_RTSP_PLAY |
      GST_RTSP_SETUP |
      GST_RTSP_GET_PARAMETER | GST_RTSP_SET_PARAMETER | GST_RTSP_TEARDOWN;

  str = gst_rtsp_options_as_text (options);

  gst_rtsp_message_init_response (state->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), state->request);

  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_PUBLIC, str);
  g_free (str);

  send_response (client, state->session, state->response);

  return TRUE;
}

/* remove duplicate and trailing '/' */
static void
sanitize_uri (GstRTSPUrl * uri)
{
  gint i, len;
  gchar *s, *d;
  gboolean have_slash, prev_slash;

  s = d = uri->abspath;
  len = strlen (uri->abspath);

  prev_slash = FALSE;

  for (i = 0; i < len; i++) {
    have_slash = s[i] == '/';
    *d = s[i];
    if (!have_slash || !prev_slash)
      d++;
    prev_slash = have_slash;
  }
  len = d - uri->abspath;
  /* don't remove the first slash if that's the only thing left */
  if (len > 1 && *(d - 1) == '/')
    d--;
  *d = '\0';
}

static void
client_session_finalized (GstRTSPClient * client, GstRTSPSession * session)
{
  GST_INFO ("client %p: session %p finished", client, session);

  /* unlink all media managed in this session */
  client_unlink_session (client, session);

  /* remove the session */
  if (!(client->sessions = g_list_remove (client->sessions, session))) {
    GST_INFO ("client %p: all sessions finalized, close the connection",
        client);
    close_connection (client);
  }
}

static void
client_watch_session (GstRTSPClient * client, GstRTSPSession * session)
{
  GList *walk;

  for (walk = client->sessions; walk; walk = g_list_next (walk)) {
    GstRTSPSession *msession = (GstRTSPSession *) walk->data;

    /* we already know about this session */
    if (msession == session)
      return;
  }

  GST_INFO ("watching session %p", session);

  g_object_weak_ref (G_OBJECT (session), (GWeakNotify) client_session_finalized,
      client);
  client->sessions = g_list_prepend (client->sessions, session);
}

static void
handle_request (GstRTSPClient * client, GstRTSPMessage * request)
{
  GstRTSPMethod method;
  const gchar *uristr;
  GstRTSPUrl *uri;
  GstRTSPVersion version;
  GstRTSPResult res;
  GstRTSPSession *session;
  GstRTSPClientState state = { NULL };
  GstRTSPMessage response = { 0 };
  gchar *sessid;

  state.request = request;
  state.response = &response;

  if (gst_debug_category_get_threshold (rtsp_client_debug) >= GST_LEVEL_LOG) {
    gst_rtsp_message_dump (request);
  }

  GST_INFO ("client %p: received a request", client);

  gst_rtsp_message_parse_request (request, &method, &uristr, &version);

  if (version != GST_RTSP_VERSION_1_0) {
    /* we can only handle 1.0 requests */
    send_generic_response (client, GST_RTSP_STS_RTSP_VERSION_NOT_SUPPORTED,
        &state);
    return;
  }
  state.method = method;

  /* we always try to parse the url first */
  if (gst_rtsp_url_parse (uristr, &uri) != GST_RTSP_OK) {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, &state);
    return;
  }

  /* sanitize the uri */
  sanitize_uri (uri);
  state.uri = uri;

  /* get the session if there is any */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    if (client->session_pool == NULL)
      goto no_pool;

    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->session_pool, sessid)))
      goto session_not_found;

    /* we add the session to the client list of watched sessions. When a session
     * disappears because it times out, we will be notified. If all sessions are
     * gone, we will close the connection */
    client_watch_session (client, session);
  } else
    session = NULL;

  state.session = session;

  if (client->auth) {
    if (!gst_rtsp_auth_check (client->auth, client, &state))
      goto not_authorized;
  }

  /* now see what is asked and dispatch to a dedicated handler */
  switch (method) {
    case GST_RTSP_OPTIONS:
      handle_options_request (client, &state);
      break;
    case GST_RTSP_DESCRIBE:
      handle_describe_request (client, &state);
      break;
    case GST_RTSP_SETUP:
      handle_setup_request (client, &state);
      break;
    case GST_RTSP_PLAY:
      handle_play_request (client, &state);
      break;
    case GST_RTSP_PAUSE:
      handle_pause_request (client, &state);
      break;
    case GST_RTSP_TEARDOWN:
      handle_teardown_request (client, &state);
      break;
    case GST_RTSP_SET_PARAMETER:
      handle_set_param_request (client, &state);
      break;
    case GST_RTSP_GET_PARAMETER:
      handle_get_param_request (client, &state);
      break;
    case GST_RTSP_ANNOUNCE:
    case GST_RTSP_RECORD:
    case GST_RTSP_REDIRECT:
      send_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, &state);
      break;
    case GST_RTSP_INVALID:
    default:
      send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, &state);
      break;
  }
  if (session)
    g_object_unref (session);

  gst_rtsp_url_free (uri);
  return;

  /* ERRORS */
no_pool:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, &state);
    return;
  }
session_not_found:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, &state);
    return;
  }
not_authorized:
  {
    handle_unauthorized_request (client, client->auth, &state);
    return;
  }
}

static void
handle_data (GstRTSPClient * client, GstRTSPMessage * message)
{
  GstRTSPResult res;
  guint8 channel;
  GList *walk;
  guint8 *data;
  guint size;
  GstBuffer *buffer;
  gboolean handled;

  /* find the stream for this message */
  res = gst_rtsp_message_parse_data (message, &channel);
  if (res != GST_RTSP_OK)
    return;

  gst_rtsp_message_steal_body (message, &data, &size);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = data;
  GST_BUFFER_MALLOCDATA (buffer) = data;
  GST_BUFFER_SIZE (buffer) = size;

  handled = FALSE;
  for (walk = client->streams; walk; walk = g_list_next (walk)) {
    GstRTSPSessionStream *stream = (GstRTSPSessionStream *) walk->data;
    GstRTSPMediaStream *mstream;
    GstRTSPTransport *tr;

    /* get the transport, if there is no transport configured, skip this stream */
    if (!(tr = stream->trans.transport))
      continue;

    /* we also need a media stream */
    if (!(mstream = stream->media_stream))
      continue;

    /* check for TCP transport */
    if (tr->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
      /* dispatch to the stream based on the channel number */
      if (tr->interleaved.min == channel) {
        gst_rtsp_media_stream_rtp (mstream, buffer);
        handled = TRUE;
        break;
      } else if (tr->interleaved.max == channel) {
        gst_rtsp_media_stream_rtcp (mstream, buffer);
        handled = TRUE;
        break;
      }
    }
  }
  if (!handled)
    gst_buffer_unref (buffer);
}

/**
 * gst_rtsp_client_set_session_pool:
 * @client: a #GstRTSPClient
 * @pool: a #GstRTSPSessionPool
 *
 * Set @pool as the sessionpool for @client which it will use to find
 * or allocate sessions. the sessionpool is usually inherited from the server
 * that created the client but can be overridden later.
 */
void
gst_rtsp_client_set_session_pool (GstRTSPClient * client,
    GstRTSPSessionPool * pool)
{
  GstRTSPSessionPool *old;

  old = client->session_pool;
  if (old != pool) {
    if (pool)
      g_object_ref (pool);
    client->session_pool = pool;
    if (old)
      g_object_unref (old);
  }
}

/**
 * gst_rtsp_client_get_session_pool:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPSessionPool object that @client uses to manage its sessions.
 *
 * Returns: a #GstRTSPSessionPool, unref after usage.
 */
GstRTSPSessionPool *
gst_rtsp_client_get_session_pool (GstRTSPClient * client)
{
  GstRTSPSessionPool *result;

  if ((result = client->session_pool))
    g_object_ref (result);

  return result;
}

/**
 * gst_rtsp_client_set_server:
 * @client: a #GstRTSPClient
 * @server: a #GstRTSPServer
 *
 * Set @server as the server that created @client.
 */
void
gst_rtsp_client_set_server (GstRTSPClient * client, GstRTSPServer * server)
{
  GstRTSPServer *old;

  old = client->server;
  if (old != server) {
    if (server)
      g_object_ref (server);
    client->server = server;
    if (old)
      g_object_unref (old);
  }
}

/**
 * gst_rtsp_client_get_server:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPServer object that @client was created from.
 *
 * Returns: a #GstRTSPServer, unref after usage.
 */
GstRTSPServer *
gst_rtsp_client_get_server (GstRTSPClient * client)
{
  GstRTSPServer *result;

  if ((result = client->server))
    g_object_ref (result);

  return result;
}

/**
 * gst_rtsp_client_set_media_mapping:
 * @client: a #GstRTSPClient
 * @mapping: a #GstRTSPMediaMapping
 *
 * Set @mapping as the media mapping for @client which it will use to map urls
 * to media streams. These mapping is usually inherited from the server that
 * created the client but can be overriden later.
 */
void
gst_rtsp_client_set_media_mapping (GstRTSPClient * client,
    GstRTSPMediaMapping * mapping)
{
  GstRTSPMediaMapping *old;

  old = client->media_mapping;

  if (old != mapping) {
    if (mapping)
      g_object_ref (mapping);
    client->media_mapping = mapping;
    if (old)
      g_object_unref (old);
  }
}

/**
 * gst_rtsp_client_get_media_mapping:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPMediaMapping object that @client uses to manage its sessions.
 *
 * Returns: a #GstRTSPMediaMapping, unref after usage.
 */
GstRTSPMediaMapping *
gst_rtsp_client_get_media_mapping (GstRTSPClient * client)
{
  GstRTSPMediaMapping *result;

  if ((result = client->media_mapping))
    g_object_ref (result);

  return result;
}

/**
 * gst_rtsp_client_set_auth:
 * @client: a #GstRTSPClient
 * @auth: a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @client.
 */
void
gst_rtsp_client_set_auth (GstRTSPClient * client, GstRTSPAuth * auth)
{
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  old = client->auth;

  if (old != auth) {
    if (auth)
      g_object_ref (auth);
    client->auth = auth;
    if (old)
      g_object_unref (old);
  }
}


/**
 * gst_rtsp_client_get_auth:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPAuth used as the authentication manager of @client.
 *
 * Returns: the #GstRTSPAuth of @client. g_object_unref() after
 * usage.
 */
GstRTSPAuth *
gst_rtsp_client_get_auth (GstRTSPClient * client)
{
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  if ((result = client->auth))
    g_object_ref (result);

  return result;
}

static GstRTSPResult
message_received (GstRTSPWatch * watch, GstRTSPMessage * message,
    gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);

  switch (message->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      handle_request (client, message);
      break;
    case GST_RTSP_MESSAGE_RESPONSE:
      break;
    case GST_RTSP_MESSAGE_DATA:
      handle_data (client, message);
      break;
    default:
      break;
  }
  return GST_RTSP_OK;
}

static GstRTSPResult
message_sent (GstRTSPWatch * watch, guint cseq, gpointer user_data)
{
  /* GstRTSPClient *client; */

  /* client = GST_RTSP_CLIENT (user_data); */

  /* GST_INFO ("client %p: sent a message with cseq %d", client, cseq); */

  return GST_RTSP_OK;
}

static GstRTSPResult
closed (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  const gchar *tunnelid;

  GST_INFO ("client %p: connection closed", client);

  if ((tunnelid = gst_rtsp_connection_get_tunnelid (client->connection))) {
    g_mutex_lock (tunnels_lock);
    /* remove from tunnelids */
    g_hash_table_remove (tunnels, tunnelid);
    g_mutex_unlock (tunnels_lock);
  }

  return GST_RTSP_OK;
}

static GstRTSPResult
error (GstRTSPWatch * watch, GstRTSPResult result, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  gchar *str;

  str = gst_rtsp_strresult (result);
  GST_INFO ("client %p: received an error %s", client, str);
  g_free (str);

  return GST_RTSP_OK;
}

static GstRTSPResult
error_full (GstRTSPWatch * watch, GstRTSPResult result,
    GstRTSPMessage * message, guint id, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  gchar *str;

  str = gst_rtsp_strresult (result);
  GST_INFO
      ("client %p: received an error %s when handling message %p with id %d",
      client, str, message, id);
  g_free (str);

  return GST_RTSP_OK;
}

static gboolean
remember_tunnel (GstRTSPClient * client)
{
  const gchar *tunnelid;

  /* store client in the pending tunnels */
  tunnelid = gst_rtsp_connection_get_tunnelid (client->connection);
  if (tunnelid == NULL)
    goto no_tunnelid;

  GST_INFO ("client %p: inserting tunnel session %s", client, tunnelid);

  /* we can't have two clients connecting with the same tunnelid */
  g_mutex_lock (tunnels_lock);
  if (g_hash_table_lookup (tunnels, tunnelid))
    goto tunnel_existed;

  g_hash_table_insert (tunnels, g_strdup (tunnelid), g_object_ref (client));
  g_mutex_unlock (tunnels_lock);

  return TRUE;

  /* ERRORS */
no_tunnelid:
  {
    GST_ERROR ("client %p: no tunnelid provided", client);
    return FALSE;
  }
tunnel_existed:
  {
    g_mutex_unlock (tunnels_lock);
    GST_ERROR ("client %p: tunnel session %s already existed", client,
        tunnelid);
    return FALSE;
  }
}

static GstRTSPStatusCode
tunnel_start (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client;

  client = GST_RTSP_CLIENT (user_data);

  GST_INFO ("client %p: tunnel start (connection %p)", client,
      client->connection);

  if (!remember_tunnel (client))
    goto tunnel_error;

  return GST_RTSP_STS_OK;

  /* ERRORS */
tunnel_error:
  {
    GST_ERROR ("client %p: error starting tunnel", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
}

static GstRTSPResult
tunnel_lost (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client;

  client = GST_RTSP_CLIENT (user_data);

  GST_INFO ("client %p: tunnel lost (connection %p)", client,
      client->connection);

  /* ignore error, it'll only be a problem when the client does a POST again */
  remember_tunnel (client);

  return GST_RTSP_OK;
}

static GstRTSPResult
tunnel_complete (GstRTSPWatch * watch, gpointer user_data)
{
  const gchar *tunnelid;
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  GstRTSPClient *oclient;

  GST_INFO ("client %p: tunnel complete", client);

  /* find previous tunnel */
  tunnelid = gst_rtsp_connection_get_tunnelid (client->connection);
  if (tunnelid == NULL)
    goto no_tunnelid;

  g_mutex_lock (tunnels_lock);
  if (!(oclient = g_hash_table_lookup (tunnels, tunnelid)))
    goto no_tunnel;

  /* remove the old client from the table. ref before because removing it will
   * remove the ref to it. */
  g_object_ref (oclient);
  g_hash_table_remove (tunnels, tunnelid);

  if (oclient->watch == NULL)
    goto tunnel_closed;
  g_mutex_unlock (tunnels_lock);

  GST_INFO ("client %p: found tunnel %p (old %p, new %p)", client, oclient,
      oclient->connection, client->connection);

  /* merge the tunnels into the first client */
  gst_rtsp_connection_do_tunnel (oclient->connection, client->connection);
  gst_rtsp_watch_reset (oclient->watch);
  g_object_unref (oclient);

  /* we don't need this watch anymore */
  g_source_destroy ((GSource *) client->watch);
  client->watchid = 0;
  client->watch = NULL;

  return GST_RTSP_OK;

  /* ERRORS */
no_tunnelid:
  {
    GST_INFO ("client %p: no tunnelid provided", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
no_tunnel:
  {
    g_mutex_unlock (tunnels_lock);
    GST_INFO ("client %p: tunnel session %s not found", client, tunnelid);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
tunnel_closed:
  {
    g_mutex_unlock (tunnels_lock);
    GST_INFO ("client %p: tunnel session %s was closed", client, tunnelid);
    g_object_unref (oclient);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
}

static GstRTSPWatchFuncs watch_funcs = {
  message_received,
  message_sent,
  closed,
  error,
  tunnel_start,
  tunnel_complete,
  error_full,
  tunnel_lost
};

static void
client_watch_notify (GstRTSPClient * client)
{
  GST_INFO ("client %p: watch destroyed", client);
  client->watchid = 0;
  client->watch = NULL;
  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_CLOSED], 0, NULL);
  g_object_unref (client);
}

/**
 * gst_rtsp_client_attach:
 * @client: a #GstRTSPClient
 * @channel: a #GIOChannel
 *
 * Accept a new connection for @client on the socket in @channel. 
 *
 * This function should be called when the client properties and urls are fully
 * configured and the client is ready to start.
 *
 * Returns: %TRUE if the client could be accepted.
 */
gboolean
gst_rtsp_client_accept (GstRTSPClient * client, GIOChannel * channel)
{
  int sock, fd;
  GstRTSPConnection *conn;
  GstRTSPResult res;
  GSource *source;
  GMainContext *context;
  GstRTSPUrl *url;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  gchar ip[INET6_ADDRSTRLEN];

  /* a new client connected. */
  sock = g_io_channel_unix_get_fd (channel);

  GST_RTSP_CHECK (gst_rtsp_connection_accept (sock, &conn), accept_failed);

  fd = gst_rtsp_connection_get_readfd (conn);

  addrlen = sizeof (addr);
  if (getsockname (fd, (struct sockaddr *) &addr, &addrlen) < 0)
    goto getpeername_failed;

  client->is_ipv6 = addr.ss_family == AF_INET6;

  if (getnameinfo ((struct sockaddr *) &addr, addrlen, ip, sizeof (ip), NULL, 0,
          NI_NUMERICHOST) != 0)
    goto getnameinfo_failed;

  /* keep the original ip that the client connected to */
  g_free (client->server_ip);
  client->server_ip = g_strndup (ip, sizeof (ip));

  GST_INFO ("client %p connected to server ip %s, ipv6 = %d", client,
      client->server_ip, client->is_ipv6);

  url = gst_rtsp_connection_get_url (conn);
  GST_INFO ("added new client %p ip %s:%d", client, url->host, url->port);

  client->connection = conn;

  /* create watch for the connection and attach */
  client->watch = gst_rtsp_watch_new (client->connection, &watch_funcs,
      g_object_ref (client), (GDestroyNotify) client_watch_notify);

  /* find the context to add the watch */
  if ((source = g_main_current_source ()))
    context = g_source_get_context (source);
  else
    context = NULL;

  GST_INFO ("attaching to context %p", context);

  client->watchid = gst_rtsp_watch_attach (client->watch, context);
  gst_rtsp_watch_unref (client->watch);

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ERROR ("Could not accept client on server socket %d: %s", sock, str);
    g_free (str);
    return FALSE;
  }
getpeername_failed:
  {
    GST_ERROR ("getpeername failed: %s", g_strerror (errno));
    return FALSE;
  }
getnameinfo_failed:
  {
    GST_ERROR ("getnameinfo failed: %s", g_strerror (errno));
    return FALSE;
  }
}
