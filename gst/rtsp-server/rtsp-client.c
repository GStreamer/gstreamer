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

#include <gst/sdp/gstsdpmessage.h>

#include "rtsp-client.h"

#undef DEBUG

static void gst_rtsp_client_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPClient, gst_rtsp_client, G_TYPE_OBJECT);

static void
gst_rtsp_client_class_init (GstRTSPClientClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_client_finalize;
}

static void
gst_rtsp_client_init (GstRTSPClient * client)
{
}

static void
gst_rtsp_client_finalize (GObject * obj)
{
  G_OBJECT_CLASS (gst_rtsp_client_parent_class)->finalize (obj);
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
handle_generic_response (GstRTSPClient *client, GstRTSPStatusCode code, 
    GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };

  gst_rtsp_message_init_response (&response, code, 
	gst_rtsp_status_as_text (code), request);

  gst_rtsp_connection_send (client->connection, &response, NULL);
}

static gboolean
handle_teardown_response (GstRTSPClient *client, const gchar *uri, GstRTSPMessage *request)
{
  GstRTSPResult res;
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  gchar *sessid;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->pool, sessid)))
      goto session_not_found;
  }
  else
    goto service_unavailable;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri, client->factory);
  if (!media)
    goto not_found;

  gst_rtsp_session_media_stop (media);

  gst_rtsp_session_pool_remove (client->pool, session);
  g_object_unref (session);

  /* remove the session id from the request, which will also remove it from the
   * response */
  gst_rtsp_message_remove_header (request, GST_RTSP_HDR_SESSION, -1);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  gst_rtsp_connection_send (client->connection, &response, NULL);

  return FALSE;

  /* ERRORS */
session_not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
service_unavailable:
  {
    handle_generic_response (client, GST_RTSP_STS_OK, request);
    return FALSE;
  }
not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
}

static gboolean
handle_pause_response (GstRTSPClient *client, const gchar *uri, GstRTSPMessage *request)
{
  GstRTSPResult res;
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  gchar *sessid;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;

  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->pool, sessid)))
      goto session_not_found;
  }
  else
    goto service_unavailable;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri, client->factory);
  if (!media)
    goto not_found;

  gst_rtsp_session_media_pause (media);
  g_object_unref (session);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  gst_rtsp_connection_send (client->connection, &response, NULL);

  return FALSE;

  /* ERRORS */
session_not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
service_unavailable:
  {
    return FALSE;
  }
not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
}

static gboolean
handle_play_response (GstRTSPClient *client, const gchar *uri, GstRTSPMessage *request)
{
  GstRTSPResult res;
  GstRTSPSessionMedia *media;
  GstRTSPSession *session;
  gchar *sessid;
  GstRTSPMessage response = { 0 };
  GstRTSPStatusCode code;
  GstStateChangeReturn ret;
  GString *rtpinfo;
  guint n_streams, i;
  guint timestamp, seqnum;

  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->pool, sessid)))
      goto session_not_found;
  }
  else
    goto service_unavailable;

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri, client->factory);
  if (!media)
    goto not_found;

  /* wait for paused to get the caps */
  ret = gst_rtsp_session_media_pause (media);
  switch (ret) {
    case GST_STATE_CHANGE_NO_PREROLL:
      break;
    case GST_STATE_CHANGE_SUCCESS:
      break;
    case GST_STATE_CHANGE_FAILURE:
      goto service_unavailable;
    case GST_STATE_CHANGE_ASYNC:
      /* wait for paused state change to complete */
      ret = gst_element_get_state (media->pipeline, NULL, NULL, -1);
      break;
  }

  /* grab RTPInfo from the payloaders now */
  rtpinfo = g_string_new ("");
  n_streams = gst_rtsp_media_bin_n_streams (media->mediabin);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;

    stream = gst_rtsp_media_bin_get_stream (media->mediabin, i);

    g_object_get (G_OBJECT (stream->payloader), "seqnum", &seqnum, NULL);
    g_object_get (G_OBJECT (stream->payloader), "timestamp", &timestamp, NULL);

    if (i > 0)
      g_string_append (rtpinfo, ", ");
    g_string_append_printf (rtpinfo, "url=%s/stream=%d;seq=%u;rtptime=%u", uri, i, seqnum, timestamp);
  }

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  /* add the RTP-Info header */
  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_RTP_INFO, rtpinfo->str);
  g_string_free (rtpinfo, TRUE);

  gst_rtsp_connection_send (client->connection, &response, NULL);

  /* start playing after sending the request */
  gst_rtsp_session_media_play (media);
  g_object_unref (session);

  return FALSE;

  /* ERRORS */
session_not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
service_unavailable:
  {
    handle_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return FALSE;
  }
not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
}

static gboolean
handle_setup_response (GstRTSPClient *client, const gchar *location, GstRTSPMessage *request)
{
  GstRTSPResult res;
  gchar *sessid;
  gchar *transport;
  gchar **transports;
  gboolean have_transport;
  GstRTSPTransport *ct, *st;
  GstRTSPUrl *uri;
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
   * always /stream=%d so we need to strip that off */
  if ((res = gst_rtsp_url_parse (location, &uri)) != GST_RTSP_OK)
    goto bad_url;

  /* parse the stream we need to configure, look for the stream in the abspath
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

  /* find the media associated with the uri */
  if (client->factory == NULL) {
    if ((client->factory = gst_rtsp_media_mapping_find_factory (client->mapping, uri)) == NULL)
      goto not_found;
  }

  /* parse the transport */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_TRANSPORT, &transport, 0);
  if (res != GST_RTSP_OK)
    goto unsupported_transports;

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
  if (!have_transport) {
    gst_rtsp_transport_free (ct);  
    goto unsupported_transports;
  }

  /* we have a valid transport, check if we can handle it */
  if (ct->trans != GST_RTSP_TRANS_RTP)
    goto unsupported_transports;
  if (ct->profile != GST_RTSP_PROFILE_AVP)
    goto unsupported_transports;
  supported = GST_RTSP_LOWER_TRANS_UDP |
	GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP;
  if (!(ct->lower_transport & supported))
    goto unsupported_transports;

  /* a setup request creates a session for a client, check if the client already
   * sent a session id to us */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (client->pool, sessid)))
      goto session_not_found;
    need_session = FALSE;
  }
  else {
    /* create a session if this fails we probably reached our session limit or
     * something. */
    if (!(session = gst_rtsp_session_pool_create (client->pool)))
      goto service_unavailable;
    need_session = TRUE;
  }

  /* get a handle to the configuration of the media in the session */
  media = gst_rtsp_session_get_media (session, uri->abspath, client->factory);
  if (!media)
    goto not_found;

  /* get a handle to the stream in the media */
  stream = gst_rtsp_session_media_get_stream (media, streamid);

  /* setup the server transport from the client transport */
  st = gst_rtsp_session_stream_set_transport (stream, inet_ntoa (client->address.sin_addr), ct);

  /* serialize the server transport */
  trans_str = gst_rtsp_transport_as_text (st);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (&response, code, gst_rtsp_status_as_text (code), request);

  if (need_session)
    gst_rtsp_message_add_header (&response, GST_RTSP_HDR_SESSION, session->sessionid);
  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_TRANSPORT, trans_str);
  g_free (trans_str);
  g_object_unref (session);

  gst_rtsp_connection_send (client->connection, &response, NULL);

  return TRUE;

  /* ERRORS */
bad_url:
  {
    handle_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
bad_request:
  {
    handle_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
session_not_found:
  {
    handle_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, request);
    return FALSE;
  }
unsupported_transports:
  {
    handle_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, request);
    return FALSE;
  }
service_unavailable:
  {
    handle_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    return FALSE;
  }
}

/* for the describe we must generate an SDP */
static gboolean
handle_describe_response (GstRTSPClient *client, const gchar *location, GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;
  GstSDPMessage *sdp;
  GstRTSPUrl *uri;
  guint n_streams, i;
  gchar *sdptext;
  GstRTSPMediaFactory *factory;
  GstRTSPMediaBin *mediabin;
  GstElement *pipeline;

  /* the uri contains the stream number we added in the SDP config, which is
   * always /stream=%d so we need to strip that off */
  if ((res = gst_rtsp_url_parse (location, &uri)) != GST_RTSP_OK)
    goto bad_url;

  /* find the factory for the uri first */
  if (!(factory = gst_rtsp_media_mapping_find_factory (client->mapping, uri)))
    goto no_factory;

  /* check what kind of format is accepted */

  /* create a pipeline to preroll the media */
  pipeline = gst_pipeline_new ("client-describe-pipeline");

  /* prepare the media and add it to the pipeline */
  if (!(mediabin = gst_rtsp_media_factory_construct (factory, uri->abspath)))
    goto no_media_bin;
  
  gst_bin_add (GST_BIN_CAST (pipeline), mediabin->element);

  /* link fakesink to all stream pads and set the pipeline to PLAYING */
  n_streams = gst_rtsp_media_bin_n_streams (mediabin);
  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;
    GstElement *sink;
    GstPad *sinkpad;
    GstPadLinkReturn lret;

    stream = gst_rtsp_media_bin_get_stream (mediabin, i);

    sink = gst_element_factory_make ("fakesink", NULL);
    gst_bin_add (GST_BIN (pipeline), sink);

    sinkpad = gst_element_get_static_pad (sink, "sink");
    lret = gst_pad_link (stream->srcpad, sinkpad);
    if (lret != GST_PAD_LINK_OK) {
      g_warning ("failed to link pad to sink: %d", lret);
    }
    gst_object_unref (sinkpad);
  }

  /* now play and wait till we get the pads blocked. At that time the pipeline
   * is prerolled and we have the caps on the streams too. */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* wait for state change to complete */
  gst_element_get_state (pipeline, NULL, NULL, -1);

  /* we should now be able to construct the SDP message */
  gst_sdp_message_new (&sdp);

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");
  gst_sdp_message_set_origin (sdp, "-", "1188340656180883", "1", "IN", "IP4", "127.0.0.1");
  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtsp-server");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");
  gst_sdp_message_add_attribute (sdp, "type", "broadcast");

  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;
    GstSDPMedia *smedia;
    GstStructure *s;
    const gchar *caps_str, *caps_enc, *caps_params;
    gchar *tmp;
    gint caps_pt, caps_rate;
    guint n_fields, j;
    gboolean first;
    GString *fmtp;

    stream = gst_rtsp_media_bin_get_stream (mediabin, i);
    gst_sdp_media_new (&smedia);

    s = gst_caps_get_structure (stream->caps, 0);

    /* get media type and payload for the m= line */
    caps_str = gst_structure_get_string (s, "media");
    gst_sdp_media_set_media (smedia, caps_str);

    gst_structure_get_int (s, "payload", &caps_pt);
    tmp = g_strdup_printf ("%d", caps_pt);
    gst_sdp_media_add_format (smedia, tmp);
    g_free (tmp);

    gst_sdp_media_set_port_info (smedia, 0, 1);
    gst_sdp_media_set_proto (smedia, "RTP/AVP");

    /* for the c= line */
    gst_sdp_media_add_connection (smedia, "IN", "IP4", "127.0.0.1", 0, 0);

    /* get clock-rate, media type and params for the rtpmap attribute */
    gst_structure_get_int (s, "clock-rate", &caps_rate);
    caps_enc = gst_structure_get_string (s, "encoding-name");
    caps_params = gst_structure_get_string (s, "encoding-params");

    if (caps_params)
      tmp = g_strdup_printf ("%d %s/%d/%s", caps_pt, caps_enc, caps_rate,
		      caps_params);
    else
      tmp = g_strdup_printf ("%d %s/%d", caps_pt, caps_enc, caps_rate);

    gst_sdp_media_add_attribute (smedia, "rtpmap", tmp);
    g_free (tmp);

    /* the config uri */
    tmp = g_strdup_printf ("stream=%d", i);
    gst_sdp_media_add_attribute (smedia, "control", tmp);
    g_free (tmp);

    /* collect all other properties and add them to fmtp */
    fmtp = g_string_new ("");
    g_string_append_printf (fmtp, "%d ", caps_pt);
    first = TRUE;
    n_fields = gst_structure_n_fields (s);
    for (j = 0; j < n_fields; j++) {
      const gchar *fname, *fval;

      fname = gst_structure_nth_field_name (s, j);

      /* filter out standard properties */
      if (!strcmp (fname, "media")) 
	continue;
      if (!strcmp (fname, "payload")) 
	continue;
      if (!strcmp (fname, "clock-rate")) 
	continue;
      if (!strcmp (fname, "encoding-name")) 
	continue;
      if (!strcmp (fname, "encoding-params")) 
	continue;
      if (!strcmp (fname, "ssrc")) 
	continue;
      if (!strcmp (fname, "clock-base")) 
	continue;
      if (!strcmp (fname, "seqnum-base")) 
	continue;

      if ((fval = gst_structure_get_string (s, fname))) {
        g_string_append_printf (fmtp, "%s%s=%s", first ? "":";", fname, fval);
	first = FALSE;
      }
    }
    if (!first) {
      tmp = g_string_free (fmtp, FALSE);
      gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
      g_free (tmp);
    }
    else {
      g_string_free (fmtp, TRUE);
    }
    gst_sdp_message_add_media (sdp, smedia);
  }
  /* go back to NULL */
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (factory);

  gst_object_unref (pipeline);
  pipeline = NULL;

  gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, 
	gst_rtsp_status_as_text (GST_RTSP_STS_OK), request);

  /* add SDP to the response body */
  sdptext = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (&response, (guint8 *)sdptext, strlen (sdptext));
  gst_sdp_message_free (sdp);

  gst_rtsp_connection_send (client->connection, &response, NULL);

  return TRUE;

  /* ERRORS */
bad_url:
  {
    handle_generic_response (client, GST_RTSP_STS_BAD_REQUEST, request);
    return FALSE;
  }
no_factory:
  {
    handle_generic_response (client, GST_RTSP_STS_NOT_FOUND, request);
    return FALSE;
  }
no_media_bin:
  {
    handle_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, request);
    g_object_unref (factory);
    return FALSE;
  }
}

static void
handle_options_response (GstRTSPClient *client, const gchar *uri, GstRTSPMessage *request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPMethod options;
  GString *str;

  gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, 
	gst_rtsp_status_as_text (GST_RTSP_STS_OK), request);

  options = GST_RTSP_DESCRIBE |
	    GST_RTSP_OPTIONS |
    //        GST_RTSP_PAUSE |
            GST_RTSP_PLAY |
            GST_RTSP_SETUP |
            GST_RTSP_TEARDOWN;

  /* always return options.. */
  str = g_string_new ("OPTIONS");

  if (options & GST_RTSP_DESCRIBE)
    g_string_append (str, ", DESCRIBE");
  if (options & GST_RTSP_ANNOUNCE)
    g_string_append (str, ", ANNOUNCE");
  if (options & GST_RTSP_GET_PARAMETER)
    g_string_append (str, ", GET_PARAMETER");
  if (options & GST_RTSP_PAUSE)
    g_string_append (str, ", PAUSE");
  if (options & GST_RTSP_PLAY)
    g_string_append (str, ", PLAY");
  if (options & GST_RTSP_RECORD)
    g_string_append (str, ", RECORD");
  if (options & GST_RTSP_REDIRECT)
    g_string_append (str, ", REDIRECT");
  if (options & GST_RTSP_SETUP)
    g_string_append (str, ", SETUP");
  if (options & GST_RTSP_SET_PARAMETER)
    g_string_append (str, ", SET_PARAMETER");
  if (options & GST_RTSP_TEARDOWN)
    g_string_append (str, ", TEARDOWN");

  gst_rtsp_message_add_header (&response, GST_RTSP_HDR_PUBLIC, str->str);

  g_string_free (str, TRUE);

  gst_rtsp_connection_send (client->connection, &response, NULL);
}

/* this function runs in a client specific thread and handles all rtsp messages
 * with the client */
static gpointer
handle_client (GstRTSPClient *client)
{
  GstRTSPMessage request = { 0 };
  GstRTSPResult res;
  GstRTSPMethod method;
  const gchar *uri;
  GstRTSPVersion version;

  while (TRUE) {
    /* start by waiting for a message from the client */
    res = gst_rtsp_connection_receive (client->connection, &request, NULL);
    if (res < 0)
      goto receive_failed;

#ifdef DEBUG
    gst_rtsp_message_dump (&request);
#endif

    gst_rtsp_message_parse_request (&request, &method, &uri, &version);

    if (version != GST_RTSP_VERSION_1_0) {
      /* we can only handle 1.0 requests */
      handle_generic_response (client, GST_RTSP_STS_RTSP_VERSION_NOT_SUPPORTED, &request);
      continue;
    }

    /* now see what is asked and dispatch to a dedicated handler */
    switch (method) {
      case GST_RTSP_OPTIONS:
        handle_options_response (client, uri, &request);
        break;
      case GST_RTSP_DESCRIBE:
        handle_describe_response (client, uri, &request);
        break;
      case GST_RTSP_SETUP:
        handle_setup_response (client, uri, &request);
        break;
      case GST_RTSP_PLAY:
        handle_play_response (client, uri, &request);
        break;
      case GST_RTSP_PAUSE:
        handle_pause_response (client, uri, &request);
        break;
      case GST_RTSP_TEARDOWN:
        handle_teardown_response (client, uri, &request);
        break;
      case GST_RTSP_ANNOUNCE:
      case GST_RTSP_GET_PARAMETER:
      case GST_RTSP_RECORD:
      case GST_RTSP_REDIRECT:
      case GST_RTSP_SET_PARAMETER:
        handle_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, &request);
        break;
      case GST_RTSP_INVALID:
      default:
        handle_generic_response (client, GST_RTSP_STS_BAD_REQUEST, &request);
        break;
    }
  }
  g_object_unref (client);
  return NULL;

  /* ERRORS */
receive_failed:
  {
    g_message ("receive failed %d (%s), disconnect client %p", res, 
	    gst_rtsp_strresult (res), client);
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

  old = client->pool;
  if (old != pool) {
    if (pool)
      g_object_ref (pool);
    client->pool = pool;
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

  if ((result = client->pool))
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

  old = client->mapping;

  if (old != mapping) {
    if (mapping)
      g_object_ref (mapping);
    client->mapping = mapping;
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

  if ((result = client->mapping))
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
  if (!client_accept (client, channel))
    goto accept_failed;

  /* client accepted, spawn a thread for the client */
  g_object_ref (client);
  client->thread = g_thread_create ((GThreadFunc)handle_client, client, TRUE, NULL);

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    return FALSE;
  }
}
