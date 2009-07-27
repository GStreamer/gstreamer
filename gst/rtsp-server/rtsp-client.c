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

#include <sys/ioctl.h>

#include "rtsp-client.h"
#include "rtsp-sdp.h"
#include "rtsp-params.h"

#define DEBUG

static GMutex *tunnels_lock;
static GHashTable *tunnels;

enum
{
  PROP_0,
  PROP_SESSION_POOL,
  PROP_MEDIA_MAPPING,
  PROP_LAST
};

static void gst_rtsp_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_finalize (GObject * obj);

static void client_session_finalized (GstRTSPClient * client,
    GstRTSPSession * session);

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

  tunnels =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  tunnels_lock = g_mutex_new ();
}

static void
gst_rtsp_client_init (GstRTSPClient * client)
{
}

/* A client is finalized when the connection is broken */
static void
gst_rtsp_client_finalize (GObject * obj)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (obj);

  g_message ("finalize client %p", client);

  g_list_free (client->sessions);

  gst_rtsp_connection_free (client->connection);
  if (client->session_pool)
    g_object_unref (client->session_pool);
  if (client->media_mapping)
    g_object_unref (client->media_mapping);

  if (client->uri)
    gst_rtsp_url_free (client->uri);
  if (client->media)
    g_object_unref (client->media);

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
#ifdef DEBUG
  gst_rtsp_message_dump (response);
#endif

  gst_rtsp_watch_queue_message (client->watch, response);
  gst_rtsp_message_unset (response);
}

static void
send_generic_response (GstRTSPClient * client, GstRTSPStatusCode code,
    GstRTSPMessage * request)
{
  GstRTSPMessage response = { 0 };

  gst_rtsp_message_init_response (&response, code,
      gst_rtsp_status_as_text (code), request);

  send_response (client, NULL, &response);
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
find_media (GstRTSPClient * client, GstRTSPUrl * uri, GstRTSPMessage * request)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;

  if (!compare_uri (client->uri, uri)) {
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
            gst_rtsp_media_mapping_find_factory (client->media_mapping, uri)))
      goto no_factory;

    /* prepare the media and add it to the pipeline */
    if (!(media = gst_rtsp_media_factory_construct (factory, uri)))
      goto no_media;

    /* prepare the media */
    if (!(gst_rtsp_media_prepare (media)))
      goto no_prepare;

    /* now keep track of the uri and the media */
    client->uri = gst_rtsp_url_copy (uri);
    client->media = media;
  } else {
    /* we have seen this uri before, used cached media */
    media = client->media;
    g_message ("reusing cached media %p", media);
  }

  if (media)
    g_object_ref (media);

  return media;

  /* ERRORS */
no_mapping:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return NULL;
  }
no_factory:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return NULL;
  }
no_media:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    g_object_unref (factory);
    return NULL;
  }
no_prepare:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    g_object_unref (media);
    g_object_unref (factory);
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

  gst_rtsp_watch_queue_message (client->watch, &message);

  gst_rtsp_message_steal_body (&message, &data, &size);
  gst_rtsp_message_unset (&message);

  return TRUE;
}

static void
link_stream (GstRTSPClient * client, GstRTSPSessionStream * stream)
{
  gst_rtsp_session_stream_set_callbacks (stream, (GstRTSPSendFunc) do_send_data,
      (GstRTSPSendFunc) do_send_data, client, NULL);
  client->streams = g_list_prepend (client->streams, stream);
}

static void
unlink_stream (GstRTSPClient * client, GstRTSPSessionStream * stream)
{
  gst_rtsp_session_stream_set_callbacks (stream, NULL, NULL, NULL, NULL);
  client->streams = g_list_remove (client->streams, stream);
}

static void
unlink_streams (GstRTSPClient * client)
{
  GList *walk;

  for (walk = client->streams; walk; walk = g_list_next (walk)) {
    GstRTSPSessionStream *stream = (GstRTSPSessionStream *) walk->data;

    gst_rtsp_session_stream_set_callbacks (stream, NULL, NULL, NULL, NULL);
  }
  g_list_free (client->streams);
  client->streams = NULL;
}

static void
unlink_session_streams (GstRTSPClient * client, GstRTSPSessionMedia * media)
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
      unlink_stream (client, sstream);
    }
  }
}

static gboolean
handle_teardown_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPSessionMedia *media;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  if (!session)
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  /* unlink the all TCP callbacks */
  unlink_session_streams (client, media);

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
  gst_rtsp_message_init_response (&response, code,
      gst_rtsp_status_as_text (code), request);

  send_response (client, session, &response);

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
}

static gboolean
handle_get_param_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;

  res = gst_rtsp_message_get_body (request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0) {
    /* no body, keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, request);
  } else {
    /* there is a body */
    GstRTSPMessage response = { 0 };

    /* there is a body, handle the params */
    res = gst_rtsp_params_get (client, uri, session, request, &response);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_response (client, session, &response);
  }
  return TRUE;

  /* ERRORS */
bad_request:
  {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
}

static gboolean
handle_set_param_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;

  res = gst_rtsp_message_get_body (request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0) {
    /* no body, keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, request);
  } else {
    GstRTSPMessage response = { 0 };

    /* there is a body, handle the params */
    res = gst_rtsp_params_set (client, uri, session, request, &response);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_response (client, session, &response);
  }
  return TRUE;

  /* ERRORS */
bad_request:
  {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
}

static gboolean
handle_pause_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPSessionMedia *media;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  if (!session)
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  /* the session state must be playing or recording */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_RECORDING)
    goto invalid_state;

  /* unlink the all TCP callbacks */
  unlink_session_streams (client, media);

  /* then pause sending */
  gst_rtsp_session_media_set_state (media, GST_STATE_PAUSED);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code,
      gst_rtsp_status_as_text (code), request);

  send_response (client, session, &response);

  /* the state is now READY */
  media->state = GST_RTSP_STATE_READY;

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        request);
    return FALSE;
  }
}

static gboolean
handle_play_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPSessionMedia *media;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;
  GString *rtpinfo;
  guint n_streams, i, infocount;
  guint timestamp, seqnum;
  gchar *str;
  GstRTSPTimeRange *range;
  GstRTSPResult res;

  if (!session)
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  /* the session state must be playing or ready */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_READY)
    goto invalid_state;

  /* parse the range header if we have one */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_RANGE, &str, 0);
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
      g_message ("stream %d is not configured", i);
      continue;
    }

    if (tr->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
      /* for TCP, link the stream to the TCP connection of the client */
      link_stream (client, sstream);
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

      uristr = gst_rtsp_url_get_request_uri (uri);
      g_string_append_printf (rtpinfo, "url=%s/stream=%d;seq=%u;rtptime=%u",
          uristr, i, seqnum, timestamp);
      g_free (uristr);

      infocount++;
    } else {
      g_warning ("RTP-Info cannot be determined for stream %d", i);
    }
  }

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code,
      gst_rtsp_status_as_text (code), request);

  /* add the RTP-Info header */
  if (infocount > 0) {
    str = g_string_free (rtpinfo, FALSE);
    gst_rtsp_message_take_header (&response, GST_RTSP_HDR_RTP_INFO, str);
  } else {
    g_string_free (rtpinfo, TRUE);
  }

  /* add the range */
  str = gst_rtsp_range_to_string (&media->media->range);
  gst_rtsp_message_take_header (&response, GST_RTSP_HDR_RANGE, str);

  send_response (client, session, &response);

  /* start playing after sending the request */
  gst_rtsp_session_media_set_state (media, GST_STATE_PLAYING);

  media->state = GST_RTSP_STATE_PLAYING;

  return TRUE;

  /* ERRORS */
no_session:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        request);
    return FALSE;
  }
}

static void
do_keepalive (GstRTSPSession * session)
{
  g_message ("keep session %p alive", session);
  gst_rtsp_session_touch (session);
}

static gboolean
handle_setup_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPResult res;
  gchar *transport;
  gchar **transports;
  gboolean have_transport;
  GstRTSPTransport *ct, *st;
  gint i;
  GstRTSPLowerTrans supported;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;
  GstRTSPSessionStream *stream;
  gchar *trans_str, *pos;
  guint streamid;
  GstRTSPSessionMedia *media;
  gboolean need_session;
  GstRTSPUrl *url;

  /* the uri contains the stream number we added in the SDP config, which is
   * always /stream=%d so we need to strip that off 
   * parse the stream we need to configure, look for the stream in the abspath
   * first and then in the query. */
  if (!(pos = strstr (uri->abspath, "/stream="))) {
    if (!(pos = strstr (uri->query, "/stream=")))
      goto bad_request;
  }

  /* we can mofify the parse uri in place */
  *pos = '\0';

  pos += strlen ("/stream=");
  if (sscanf (pos, "%u", &streamid) != 1)
    goto bad_request;

  /* parse the transport */
  res =
      gst_rtsp_message_get_header (request, GST_RTSP_HDR_TRANSPORT, &transport,
      0);
  if (res != GST_RTSP_OK)
    goto no_transport;

  transports = g_strsplit (transport, ",", 0);
  gst_rtsp_transport_new (&ct);

  /* loop through the transports, try to parse */
  have_transport = FALSE;
  for (i = 0; transports[i]; i++) {

    gst_rtsp_transport_init (ct);
    res = gst_rtsp_transport_parse (transports[i], ct);
    if (res == GST_RTSP_OK) {
      have_transport = TRUE;
      break;
    }
  }
  g_strfreev (transports);

  /* we have not found anything usable, error out */
  if (!have_transport)
    goto unsupported_transports;

  /* we have a valid transport, check if we can handle it */
  if (ct->trans != GST_RTSP_TRANS_RTP)
    goto unsupported_transports;
  if (ct->profile != GST_RTSP_PROFILE_AVP)
    goto unsupported_transports;

  supported = GST_RTSP_LOWER_TRANS_UDP |
      GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP;
  if (!(ct->lower_transport & supported))
    goto unsupported_transports;

  if (client->session_pool == NULL)
    goto no_pool;

  /* we have a valid transport now, set the destination of the client. */
  g_free (ct->destination);
  url = gst_rtsp_connection_get_url (client->connection);
  ct->destination = g_strdup (url->host);

  if (session) {
    g_object_ref (session);
    /* get a handle to the configuration of the media in the session, this can
     * return NULL if this is a new url to manage in this session. */
    media = gst_rtsp_session_get_media (session, uri);

    need_session = FALSE;
  } else {
    /* create a session if this fails we probably reached our session limit or
     * something. */
    if (!(session = gst_rtsp_session_pool_create (client->session_pool)))
      goto service_unavailable;

    /* we need a new media configuration in this session */
    media = NULL;

    need_session = TRUE;
  }

  /* we have no media, find one and manage it */
  if (media == NULL) {
    GstRTSPMedia *m;

    /* get a handle to the configuration of the media in the session */
    if ((m = find_media (client, uri, request))) {
      /* manage the media in our session now */
      media = gst_rtsp_session_manage_media (session, uri, m);
    }
  }

  /* if we stil have no media, error */
  if (media == NULL)
    goto not_found;

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
  gst_rtsp_message_init_response (&response, code,
      gst_rtsp_status_as_text (code), request);

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_TRANSPORT, trans_str);
  g_free (trans_str);

  send_response (client, session, &response);

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
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    g_object_unref (session);
    return FALSE;
  }
no_stream:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    g_object_unref (media);
    g_object_unref (session);
    return FALSE;
  }
no_transport:
  {
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, request);
    return FALSE;
  }
unsupported_transports:
  {
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, request);
    gst_rtsp_transport_free (ct);
    return FALSE;
  }
no_pool:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return FALSE;
  }
service_unavailable:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return FALSE;
  }
}

/* for the describe we must generate an SDP */
static gboolean
handle_describe_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;
  GstSDPMessage *sdp;
  guint i;
  gchar *str;
  GstRTSPMedia *media;

  /* check what kind of format is accepted, we don't really do anything with it
   * and always return SDP for now. */
  for (i = 0; i++;) {
    gchar *accept;

    res =
        gst_rtsp_message_get_header (request, GST_RTSP_HDR_ACCEPT, &accept, i);
    if (res == GST_RTSP_ENOTIMPL)
      break;

    if (g_ascii_strcasecmp (accept, "application/sdp") == 0)
      break;
  }

  /* find the media object for the uri */
  if (!(media = find_media (client, uri, request)))
    goto no_media;

  /* create an SDP for the media object */
  if (!(sdp = gst_rtsp_sdp_from_media (media)))
    goto no_sdp;

  g_object_unref (media);

  gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), request);

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  /* content base for some clients that might screw up creating the setup uri */
  str = g_strdup_printf ("rtsp://%s:%u%s/", uri->host, uri->port, uri->abspath);
  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_CONTENT_BASE, str);
  g_free (str);

  /* add SDP to the response body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (&response, (guint8 *) str, strlen (str));
  gst_sdp_message_free (sdp);

  send_response (client, session, &response);

  return TRUE;

  /* ERRORS */
no_media:
  {
    /* error reply is already sent */
    return FALSE;
  }
no_sdp:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    g_object_unref (media);
    return FALSE;
  }
}

static gboolean
handle_options_request (GstRTSPClient * client, GstRTSPUrl * uri,
    GstRTSPSession * session, GstRTSPMessage * request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPMethod options;
  gchar *str;

  options = GST_RTSP_DESCRIBE |
      GST_RTSP_OPTIONS |
      GST_RTSP_PAUSE |
      GST_RTSP_PLAY |
      GST_RTSP_SETUP |
      GST_RTSP_GET_PARAMETER | GST_RTSP_SET_PARAMETER | GST_RTSP_TEARDOWN;

  str = gst_rtsp_options_as_text (options);

  gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), request);

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_PUBLIC, str);
  g_free (str);

  send_response (client, session, &response);

  return TRUE;
}

/* remove duplicate and trailing '/' */
static void
santize_uri (GstRTSPUrl * uri)
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
  if (!(client->sessions = g_list_remove (client->sessions, session))) {
    g_message ("all sessions finalized, close the connection");
    g_source_destroy ((GSource *) client->watch);
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

  g_message ("watching session %p", session);

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
  gchar *sessid;

#ifdef DEBUG
  gst_rtsp_message_dump (request);
#endif

  g_message ("client %p: received a request", client);

  gst_rtsp_message_parse_request (request, &method, &uristr, &version);

  if (version != GST_RTSP_VERSION_1_0) {
    /* we can only handle 1.0 requests */
    send_generic_response (client, GST_RTSP_STS_RTSP_VERSION_NOT_SUPPORTED,
        request);
    return;
  }

  /* we always try to parse the url first */
  if ((res = gst_rtsp_url_parse (uristr, &uri)) != GST_RTSP_OK) {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return;
  }

  /* sanitize the uri */
  santize_uri (uri);

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

  /* now see what is asked and dispatch to a dedicated handler */
  switch (method) {
    case GST_RTSP_OPTIONS:
      handle_options_request (client, uri, session, request);
      break;
    case GST_RTSP_DESCRIBE:
      handle_describe_request (client, uri, session, request);
      break;
    case GST_RTSP_SETUP:
      handle_setup_request (client, uri, session, request);
      break;
    case GST_RTSP_PLAY:
      handle_play_request (client, uri, session, request);
      break;
    case GST_RTSP_PAUSE:
      handle_pause_request (client, uri, session, request);
      break;
    case GST_RTSP_TEARDOWN:
      handle_teardown_request (client, uri, session, request);
      break;
    case GST_RTSP_SET_PARAMETER:
      handle_set_param_request (client, uri, session, request);
      break;
    case GST_RTSP_GET_PARAMETER:
      handle_get_param_request (client, uri, session, request);
      break;
    case GST_RTSP_ANNOUNCE:
    case GST_RTSP_RECORD:
    case GST_RTSP_REDIRECT:
      send_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, request);
      break;
    case GST_RTSP_INVALID:
    default:
      send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
      break;
  }
  if (session)
    g_object_unref (session);

  gst_rtsp_url_free (uri);
  return;

  /* ERRORS */
no_pool:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return;
  }
session_not_found:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
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
  GstRTSPClient *client;
  
  client = GST_RTSP_CLIENT (user_data);

  /* g_message ("client %p: sent a message with cseq %d", client, cseq); */

  return GST_RTSP_OK;
}

static GstRTSPResult
closed (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  const gchar *tunnelid;

  g_message ("client %p: connection closed", client);

  if ((tunnelid = gst_rtsp_connection_get_tunnelid (client->connection))) {
    g_mutex_lock (tunnels_lock);
    g_hash_table_remove (tunnels, tunnelid);
    g_mutex_unlock (tunnels_lock);
  }

  /* remove all streams that are streaming over this client connection */
  unlink_streams (client);

  return GST_RTSP_OK;
}

static GstRTSPResult
error (GstRTSPWatch * watch, GstRTSPResult result, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  gchar *str;

  str = gst_rtsp_strresult (result);
  g_message ("client %p: received an error %s", client, str);
  g_free (str);

  return GST_RTSP_OK;
}

static GstRTSPStatusCode
tunnel_start (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client;
  const gchar *tunnelid;

  client = GST_RTSP_CLIENT (user_data);

  g_message ("client %p: tunnel start", client);

  /* store client in the pending tunnels */
  tunnelid = gst_rtsp_connection_get_tunnelid (client->connection);
  if (tunnelid == NULL)
    goto no_tunnelid;

  g_message ("client %p: inserting %s", client, tunnelid);

  /* we can't have two clients connecting with the same tunnelid */
  g_mutex_lock (tunnels_lock);
  if (g_hash_table_lookup (tunnels, tunnelid))
    goto tunnel_existed;

  g_hash_table_insert (tunnels, g_strdup (tunnelid), g_object_ref (client));
  g_mutex_unlock (tunnels_lock);

  return GST_RTSP_STS_OK;

  /* ERRORS */
no_tunnelid:
  {
    g_message ("client %p: no tunnelid provided", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
tunnel_existed:
  {
    g_mutex_unlock (tunnels_lock);
    g_message ("client %p: tunnel session %s existed", client, tunnelid);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
}

static GstRTSPResult
tunnel_complete (GstRTSPWatch * watch, gpointer user_data)
{
  const gchar *tunnelid;
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  GstRTSPClient *oclient;

  g_message ("client %p: tunnel complete", client);

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
  g_mutex_unlock (tunnels_lock);

  g_message ("client %p: found tunnel %p", client, oclient);

  /* merge the tunnels into the first client */
  gst_rtsp_connection_do_tunnel (oclient->connection, client->connection);
  gst_rtsp_watch_reset (oclient->watch);
  g_object_unref (oclient);

  /* we don't need this watch anymore */
  g_source_destroy ((GSource *) client->watch);
  client->watchid = 0;

  return GST_RTSP_OK;

  /* ERRORS */
no_tunnelid:
  {
    g_message ("client %p: no tunnelid provided", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
no_tunnel:
  {
    g_mutex_unlock (tunnels_lock);
    g_message ("client %p: tunnel session %s not found", client, tunnelid);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
}

static GstRTSPWatchFuncs watch_funcs = {
  message_received,
  message_sent,
  closed,
  error,
  tunnel_start,
  tunnel_complete
};

/**
 * gst_rtsp_client_attach:
 * @client: a #GstRTSPClient
 * @channel: a #GIOChannel
 *
 * Accept a new connection for @client on the socket in @source. 
 *
 * This function should be called when the client properties and urls are fully
 * configured and the client is ready to start.
 *
 * Returns: %TRUE if the client could be accepted.
 */
gboolean
gst_rtsp_client_accept (GstRTSPClient * client, GIOChannel * channel)
{
  int sock;
  GstRTSPConnection *conn;
  GstRTSPResult res;
  GSource *source;
  GMainContext *context;
  GstRTSPUrl *url;

  /* a new client connected. */
  sock = g_io_channel_unix_get_fd (channel);

  GST_RTSP_CHECK (gst_rtsp_connection_accept (sock, &conn), accept_failed);

  url = gst_rtsp_connection_get_url (conn);
  g_message ("added new client %p ip %s:%d", client, url->host, url->port);

  client->connection = conn;

  /* create watch for the connection and attach */
  client->watch = gst_rtsp_watch_new (client->connection, &watch_funcs,
      g_object_ref (client), g_object_unref);

  /* find the context to add the watch */
  if ((source = g_main_current_source ()))
    context = g_source_get_context (source);
  else
    context = NULL;

  g_message ("attaching to context %p", context);

  client->watchid = gst_rtsp_watch_attach (client->watch, context);
  gst_rtsp_watch_unref (client->watch);

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    g_error ("Could not accept client on server socket %d: %s", sock, str);
    g_free (str);
    return FALSE;
  }
}
