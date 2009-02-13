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

#undef DEBUG

#define DEFAULT_TIMEOUT  60

enum
{
  PROP_0,
  PROP_TIMEOUT,
  PROP_SESSION_POOL,
  PROP_MEDIA_MAPPING,
  PROP_LAST
};

static void gst_rtsp_client_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_client_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_client_finalize (GObject * obj);

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
      g_param_spec_uint ("timeout", "Timeout", "The client timeout",
          0, G_MAXUINT, DEFAULT_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SESSION_POOL,
      g_param_spec_object ("session-pool", "Session Pool",
          "The session pool to use for client session",
          GST_TYPE_RTSP_SESSION_POOL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MEDIA_MAPPING,
      g_param_spec_object ("media-mapping", "Media Mapping",
          "The media mapping to use for client session",
          GST_TYPE_RTSP_MEDIA_MAPPING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_rtsp_client_init (GstRTSPClient * client)
{
  client->timeout = DEFAULT_TIMEOUT;
}

/* A client is finalized when the connection is broken */
static void
gst_rtsp_client_finalize (GObject * obj)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (obj);

  g_message ("finalize client %p", client);

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
gst_rtsp_client_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);

  switch (propid) {
    case PROP_TIMEOUT:
      g_value_set_uint (value, gst_rtsp_client_get_timeout (client));
      break;
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
gst_rtsp_client_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);

  switch (propid) {
    case PROP_TIMEOUT:
      gst_rtsp_client_set_timeout (client, g_value_get_uint (value));
      break;
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
send_response (GstRTSPClient *client, GstRTSPSession *session, GstRTSPMessage *response)
{
  GTimeVal timeout;

  gst_rtsp_message_add_header (response, GST_RTSP_HDR_SERVER, "GStreamer RTSP server");

#ifdef DEBUG
  gst_rtsp_message_dump (response);
#endif

  timeout.tv_sec = client->timeout;
  timeout.tv_usec = 0;

  /* add the new session header for new session ids */
  if (session) {
    gchar *str;

    if (session->timeout != 60)
      str = g_strdup_printf ("%s; timeout=%d", session->sessionid, session->timeout);
    else
      str = g_strdup (session->sessionid);

    gst_rtsp_message_take_header (response, GST_RTSP_HDR_SESSION, str);
  }
  else {
    /* remove the session id from the response */
    gst_rtsp_message_remove_header (response, GST_RTSP_HDR_SESSION, -1);
  }

  gst_rtsp_connection_send (client->connection, response, &timeout);
  gst_rtsp_message_unset (response);
}

static void
send_generic_response (GstRTSPClient *client, GstRTSPStatusCode code, 
    GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };

  gst_rtsp_message_init_response (&response, code, 
	gst_rtsp_status_as_text (code), request);

  send_response (client, NULL, &response);
}

static gboolean
compare_uri (const GstRTSPUrl *uri1, const GstRTSPUrl *uri2)
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
find_media (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
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
    if (!(factory = gst_rtsp_media_mapping_find_factory (client->media_mapping, uri)))
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
  }
  else {
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

/* Get the session or NULL when there was no session */
static GstRTSPSession *
ensure_session (GstRTSPClient *client, GstRTSPMessage *request)
{
  GstRTSPResult res;
  GstRTSPSession *session;
  gchar *sessid;

  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    if (client->session_pool == NULL)
      goto no_pool;

    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->session_pool, sessid)))
      goto session_not_found;

    client->timeout = gst_rtsp_session_get_timeout (session);
  }
  else
    goto service_unavailable;

  return session;

  /* ERRORS */
no_pool:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return NULL;
  }
session_not_found:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return NULL;
  }
service_unavailable:
  {
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return NULL;
  }
}

static gboolean
handle_teardown_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  if (!(session = ensure_session (client, request)))
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  gst_rtsp_session_media_stop (media);

  /* unmanage the media in the session, returns false if all media session
   * are torn down. */
  if (!gst_rtsp_session_release_media (session, media)) {
    /* remove the session */
    gst_rtsp_session_pool_remove (client->session_pool, session);
  }
  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  send_response (client, session, &response);

  g_object_unref (session);

  return FALSE;

  /* ERRORS */
no_session:
  {
    /* error was sent already */
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
}

static gboolean
handle_pause_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  if (!(session = ensure_session (client, request)))
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  /* the session state must be playing or recording */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_RECORDING)
    goto invalid_state;

  gst_rtsp_session_media_pause (media);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  send_response (client, session, &response);

  /* the state is now READY */
  media->state = GST_RTSP_STATE_READY;
  g_object_unref (session);

  return FALSE;

  /* ERRORS */
no_session:
  {
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    g_object_unref (session);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE, request);
    g_object_unref (session);
    return FALSE;
  }
}

static gboolean
handle_play_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;
  GString *rtpinfo;
  guint n_streams, i;
  guint timestamp, seqnum;
  gchar *str;

  if (!(session = ensure_session (client, request)))
    goto no_session;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri);
  if (!media)
    goto not_found;

  /* the session state must be playing or ready */
  if (media->state != GST_RTSP_STATE_PLAYING &&
      media->state != GST_RTSP_STATE_READY)
    goto invalid_state;

  /* grab RTPInfo from the payloaders now */
  rtpinfo = g_string_new ("");

  n_streams = gst_rtsp_media_n_streams (media->media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;
    gchar *uristr;

    stream = gst_rtsp_media_get_stream (media->media, i);

    g_object_get (G_OBJECT (stream->payloader), "seqnum", &seqnum, NULL);
    g_object_get (G_OBJECT (stream->payloader), "timestamp", &timestamp, NULL);

    if (i > 0)
      g_string_append (rtpinfo, ", ");

    uristr = gst_rtsp_url_get_request_uri (uri);
    g_string_append_printf (rtpinfo, "url=%s/stream=%d;seq=%u;rtptime=%u", uristr, i, seqnum, timestamp);
    g_free (uristr);
  }

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  /* add the RTP-Info header */
  str = g_string_free (rtpinfo, FALSE);
  gst_rtsp_message_take_header (&response, GST_RTSP_HDR_RTP_INFO, str);

  /* add the range */
  str = gst_rtsp_range_to_string (&media->media->range);
  gst_rtsp_message_take_header (&response, GST_RTSP_HDR_RANGE, str);

  send_response (client, session, &response);

  /* start playing after sending the request */
  gst_rtsp_session_media_play (media);

  media->state = GST_RTSP_STATE_PLAYING;
  g_object_unref (session);

  return FALSE;

  /* ERRORS */
no_session:
  {
    /* error was sent */
    return FALSE;
  }
not_found:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    g_object_unref (session);
    return FALSE;
  }
invalid_state:
  {
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE, request);
    g_object_unref (session);
    return FALSE;
  }
}

static gboolean
handle_setup_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPResult res;
  gchar *sessid;
  gchar *transport;
  gchar **transports;
  gboolean have_transport;
  GstRTSPTransport *ct, *st;
  GstRTSPSession *session;
  gint i;
  GstRTSPLowerTrans supported;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;
  GstRTSPSessionStream *stream;
  gchar *trans_str, *pos;
  guint streamid;
  GstRTSPSessionMedia *media;
  gboolean need_session;

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
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_TRANSPORT, &transport, 0);
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
  ct->destination = g_strdup (inet_ntoa (client->address.sin_addr));

  /* a setup request creates a session for a client, check if the client already
   * sent a session id to us */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->session_pool, sessid)))
      goto session_not_found;

    /* get a handle to the configuration of the media in the session, this can
     * return NULL if this is a new url to manage in this session. */
    media = gst_rtsp_session_get_media (session, uri);

    need_session = FALSE;
  }
  else {
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

  /* get a handle to the stream in the media */
  if (!(stream = gst_rtsp_session_media_get_stream (media, streamid)))
    goto no_stream;

  /* setup the server transport from the client transport */
  st = gst_rtsp_session_stream_set_transport (stream, ct);

  /* serialize the server transport */
  trans_str = gst_rtsp_transport_as_text (st);
  gst_rtsp_transport_free (st);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

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
    return FALSE;
  }
no_stream:
  {
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    g_object_unref (media);
    return FALSE;
  }
session_not_found:
  {
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
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
handle_describe_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;
  GstSDPMessage *sdp;
  guint i;
  gchar *str;
  GstRTSPMedia *media;

  /* check what kind of format is accepted, we don't really do anything with it
   * and always return SDP for now. */
  for (i = 0; i++; ) {
    gchar *accept;

    res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_ACCEPT, &accept, i);
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

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_CONTENT_TYPE, "application/sdp");

  /* content base for some clients that might screw up creating the setup uri */
  str = g_strdup_printf ("rtsp://%s:%u%s/", uri->host, uri->port, uri->abspath);
  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_CONTENT_BASE, str);
  g_free (str);

  /* add SDP to the response body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (&response, (guint8 *)str, strlen (str));
  gst_sdp_message_free (sdp);

  send_response (client, NULL, &response);

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

static void
handle_options_request (GstRTSPClient *client, GstRTSPUrl *uri, GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPMethod options;
  gchar *str;

  options = GST_RTSP_DESCRIBE |
	    GST_RTSP_OPTIONS |
    //        GST_RTSP_PAUSE |
            GST_RTSP_PLAY |
            GST_RTSP_SETUP |
            GST_RTSP_TEARDOWN;

  str = gst_rtsp_options_as_text (options);

  gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, 
	gst_rtsp_status_as_text (GST_RTSP_STS_OK), request);

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_PUBLIC, str);
  g_free (str);

  send_response (client, NULL, &response);
}

/* remove duplicate and trailing '/' */
static void
santize_uri (GstRTSPUrl *uri)
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
  if (len > 1 && *(d-1) == '/')
    d--;
  *d = '\0';
}

/* this function runs in a client specific thread and handles all rtsp messages
 * with the client */
static gpointer
handle_client (GstRTSPClient *client)
{
  GstRTSPMessage request = { 0 };
  GstRTSPResult res;
  GstRTSPMethod method;
  const gchar *uristr;
  GstRTSPUrl *uri;
  GstRTSPVersion version;

  while (TRUE) {
    GTimeVal timeout;

    timeout.tv_sec = client->timeout;
    timeout.tv_usec = 0;

    /* start by waiting for a message from the client */
    res = gst_rtsp_connection_receive (client->connection, &request, &timeout);
    if (res < 0) {
      if (res == GST_RTSP_ETIMEOUT)
        goto timeout;

      goto receive_failed;
    }

#ifdef DEBUG
    gst_rtsp_message_dump (&request);
#endif

    gst_rtsp_message_parse_request (&request, &method, &uristr, &version);

    if (version != GST_RTSP_VERSION_1_0) {
      /* we can only handle 1.0 requests */
      send_generic_response (client, GST_RTSP_STS_RTSP_VERSION_NOT_SUPPORTED, &request);
      continue;
    }

    /* we always try to parse the url first */
    if ((res = gst_rtsp_url_parse (uristr, &uri)) != GST_RTSP_OK) {
      send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, &request);
      continue;
    }

    /* sanitize the uri */
    santize_uri (uri);

    /* now see what is asked and dispatch to a dedicated handler */
    switch (method) {
      case GST_RTSP_OPTIONS:
        handle_options_request (client, uri, &request);
        break;
      case GST_RTSP_DESCRIBE:
        handle_describe_request (client, uri, &request);
        break;
      case GST_RTSP_SETUP:
        handle_setup_request (client, uri, &request);
        break;
      case GST_RTSP_PLAY:
        handle_play_request (client, uri, &request);
        break;
      case GST_RTSP_PAUSE:
        handle_pause_request (client, uri, &request);
        break;
      case GST_RTSP_TEARDOWN:
        handle_teardown_request (client, uri, &request);
        break;
      case GST_RTSP_ANNOUNCE:
      case GST_RTSP_GET_PARAMETER:
      case GST_RTSP_RECORD:
      case GST_RTSP_REDIRECT:
      case GST_RTSP_SET_PARAMETER:
        send_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, &request);
        break;
      case GST_RTSP_INVALID:
      default:
        send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, &request);
        break;
    }
    gst_rtsp_url_free (uri);
  }
  g_object_unref (client);
  return NULL;

  /* ERRORS */
timeout:
  {
    g_message ("client timed out");
    if (client->session_pool)
      gst_rtsp_session_pool_cleanup (client->session_pool);
    goto cleanup;
  }
receive_failed:
  {
    gchar *str;
    str = gst_rtsp_strresult (res);
    g_message ("receive failed %d (%s), disconnect client %p", res, 
	    str, client);
    g_free (str);
    goto cleanup;
  }
cleanup:
  {
    gst_rtsp_message_unset (&request);
    gst_rtsp_connection_close (client->connection);
    g_object_unref (client);
    return NULL;
  }
}

/* called when we need to accept a new request from a client */
static gboolean
client_accept (GstRTSPClient *client, GIOChannel *channel)
{
  /* a new client connected. */
  int server_sock_fd, fd;
  unsigned int address_len;
  GstRTSPConnection *conn;

  server_sock_fd = g_io_channel_unix_get_fd (channel);

  address_len = sizeof (client->address);
  memset (&client->address, 0, address_len);

  fd = accept (server_sock_fd, (struct sockaddr *) &client->address,
      &address_len);
  if (fd == -1)
    goto accept_failed;

  /* now create the connection object */
  gst_rtsp_connection_create (NULL, &conn);
  conn->fd.fd = fd;

  /* FIXME some hackery, we need to have a connection method to accept server
   * connections */
  gst_poll_add_fd (conn->fdset, &conn->fd);

  g_message ("added new client %p ip %s with fd %d", client,
	        inet_ntoa (client->address.sin_addr), conn->fd.fd);

  client->connection = conn;

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    g_error ("Could not accept client on server socket %d: %s (%d)",
            server_sock_fd, g_strerror (errno), errno);
    return FALSE;
  }
}

/**
 * gst_rtsp_client_set_timeout:
 * @client: a #GstRTSPClient
 * @timeout: a timeout in seconds
 *
 * Set the connection timeout to @timeout seconds for @client.
 */
void
gst_rtsp_client_set_timeout (GstRTSPClient *client, guint timeout)
{
  client->timeout = timeout;
}

/**
 * gst_rtsp_client_get_timeout:
 * @client: a #GstRTSPClient
 *
 * Get the connection timeout @client.
 *
 * Returns: the connection timeout for @client in seconds.
 */
guint
gst_rtsp_client_get_timeout (GstRTSPClient *client)
{
  return client->timeout;
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
gst_rtsp_client_set_session_pool (GstRTSPClient *client, GstRTSPSessionPool *pool)
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
gst_rtsp_client_get_session_pool (GstRTSPClient *client)
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
gst_rtsp_client_set_media_mapping (GstRTSPClient *client, GstRTSPMediaMapping *mapping)
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
gst_rtsp_client_get_media_mapping (GstRTSPClient *client)
{
  GstRTSPMediaMapping *result;

  if ((result = client->media_mapping))
    g_object_ref (result);

  return result;
}

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
gst_rtsp_client_accept (GstRTSPClient *client, GIOChannel *channel)
{
  GError *error = NULL;

  if (!client_accept (client, channel))
    goto accept_failed;

  /* client accepted, spawn a thread for the client, we don't need to join the
   * thread */
  g_object_ref (client);
  client->thread = g_thread_create ((GThreadFunc)handle_client, client, FALSE, &error);
  if (client->thread == NULL)
    goto no_thread;

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    return FALSE;
  }
no_thread:
  {
    if (error) {
      g_warning ("could not create thread for client %p: %s", client, error->message);
      g_error_free (error);
    }
    g_object_unref (client);
    return FALSE;
  }
}
