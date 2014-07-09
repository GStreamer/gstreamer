/* GStreamer
 *
 * unit test for GstRTSPServer
 *
 * Copyright (C) 2012 Axis Communications <dev-gstreamer at axis dot com>
 * @author David Svensson Fors <davidsf at axis dot com>
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

#include <gst/check/gstcheck.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include <stdio.h>
#include <netinet/in.h>

#include "rtsp-server.h"

#define VIDEO_PIPELINE "videotestsrc ! " \
  "video/x-raw,width=352,height=288 ! " \
  "rtpgstpay name=pay0 pt=96"
#define AUDIO_PIPELINE "audiotestsrc ! " \
  "audio/x-raw,rate=8000 ! " \
  "rtpgstpay name=pay1 pt=97"

#define TEST_MOUNT_POINT  "/test"
#define TEST_PROTO        "RTP/AVP"
#define TEST_ENCODING     "X-GST"
#define TEST_CLOCK_RATE   "90000"

/* tested rtsp server */
static GstRTSPServer *server = NULL;

/* tcp port that the test server listens for rtsp requests on */
static gint test_port = 0;

/* id of the server's source within the GMainContext */
static guint source_id;

/* iterate the default main loop until there are no events to dispatch */
static void
iterate (void)
{
  while (g_main_context_iteration (NULL, FALSE)) {
    GST_DEBUG ("iteration");
  }
}

static void
get_client_ports_full (GstRTSPRange * range, GSocket ** rtp_socket,
    GSocket ** rtcp_socket)
{
  GSocket *rtp = NULL;
  GSocket *rtcp = NULL;
  gint rtp_port = 0;
  gint rtcp_port;
  GInetAddress *anyaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  GSocketAddress *sockaddr;
  gboolean bound;

  for (;;) {
    if (rtp_port != 0)
      rtp_port += 2;

    rtp = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP, NULL);
    fail_unless (rtp != NULL);

    sockaddr = g_inet_socket_address_new (anyaddr, rtp_port);
    fail_unless (sockaddr != NULL);
    bound = g_socket_bind (rtp, sockaddr, FALSE, NULL);
    g_object_unref (sockaddr);
    if (!bound) {
      g_object_unref (rtp);
      continue;
    }

    sockaddr = g_socket_get_local_address (rtp, NULL);
    fail_unless (sockaddr != NULL && G_IS_INET_SOCKET_ADDRESS (sockaddr));
    rtp_port =
        g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (sockaddr));
    g_object_unref (sockaddr);

    if (rtp_port % 2 != 0) {
      rtp_port += 1;
      g_object_unref (rtp);
      continue;
    }

    rtcp_port = rtp_port + 1;

    rtcp = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP, NULL);
    fail_unless (rtcp != NULL);

    sockaddr = g_inet_socket_address_new (anyaddr, rtcp_port);
    fail_unless (sockaddr != NULL);
    bound = g_socket_bind (rtcp, sockaddr, FALSE, NULL);
    g_object_unref (sockaddr);
    if (!bound) {
      g_object_unref (rtp);
      g_object_unref (rtcp);
      continue;
    }

    sockaddr = g_socket_get_local_address (rtcp, NULL);
    fail_unless (sockaddr != NULL && G_IS_INET_SOCKET_ADDRESS (sockaddr));
    fail_unless (rtcp_port ==
        g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (sockaddr)));
    g_object_unref (sockaddr);

    break;
  }

  range->min = rtp_port;
  range->max = rtcp_port;
  if (rtp_socket)
    *rtp_socket = rtp;
  else
    g_object_unref (rtp);
  if (rtcp_socket)
    *rtcp_socket = rtcp;
  else
    g_object_unref (rtcp);
  GST_DEBUG ("client_port=%d-%d", range->min, range->max);
  g_object_unref (anyaddr);
}

/* get a free rtp/rtcp client port pair */
static void
get_client_ports (GstRTSPRange * range)
{
  get_client_ports_full (range, NULL, NULL);
}

/* start the tested rtsp server */
static void
start_server (void)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_launch (factory,
      "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT, factory);
  g_object_unref (mounts);

  /* set port to any */
  gst_rtsp_server_set_service (server, "0");

  /* attach to default main context */
  source_id = gst_rtsp_server_attach (server, NULL);
  fail_if (source_id == 0);

  /* get port */
  service = gst_rtsp_server_get_service (server);
  test_port = atoi (service);
  fail_unless (test_port != 0);
  g_free (service);

  GST_DEBUG ("rtsp server listening on port %d", test_port);
}

/* stop the tested rtsp server */
static void
stop_server (void)
{
  g_source_remove (source_id);
  source_id = 0;

  GST_DEBUG ("rtsp server stopped");
}

/* create an rtsp connection to the server on test_port */
static GstRTSPConnection *
connect_to_server (gint port, const gchar * mount_point)
{
  GstRTSPConnection *conn = NULL;
  gchar *address;
  gchar *uri_string;
  GstRTSPUrl *url = NULL;

  address = gst_rtsp_server_get_address (server);
  uri_string = g_strdup_printf ("rtsp://%s:%d%s", address, port, mount_point);
  g_free (address);
  fail_unless (gst_rtsp_url_parse (uri_string, &url) == GST_RTSP_OK);
  g_free (uri_string);

  fail_unless (gst_rtsp_connection_create (url, &conn) == GST_RTSP_OK);
  gst_rtsp_url_free (url);

  fail_unless (gst_rtsp_connection_connect (conn, NULL) == GST_RTSP_OK);

  return conn;
}

/* create an rtsp request */
static GstRTSPMessage *
create_request (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * control)
{
  GstRTSPMessage *request = NULL;
  gchar *base_uri;
  gchar *full_uri;

  base_uri = gst_rtsp_url_get_request_uri (gst_rtsp_connection_get_url (conn));
  full_uri = g_strdup_printf ("%s/%s", base_uri, control ? control : "");
  g_free (base_uri);
  if (gst_rtsp_message_new_request (&request, method, full_uri) != GST_RTSP_OK) {
    GST_DEBUG ("failed to create request object");
    g_free (full_uri);
    return NULL;
  }
  g_free (full_uri);
  return request;
}

/* send an rtsp request */
static gboolean
send_request (GstRTSPConnection * conn, GstRTSPMessage * request)
{
  if (gst_rtsp_connection_send (conn, request, NULL) != GST_RTSP_OK) {
    GST_DEBUG ("failed to send request");
    return FALSE;
  }
  return TRUE;
}

/* read rtsp response. response must be freed by the caller */
static GstRTSPMessage *
read_response (GstRTSPConnection * conn)
{
  GstRTSPMessage *response = NULL;

  if (gst_rtsp_message_new (&response) != GST_RTSP_OK) {
    GST_DEBUG ("failed to create response object");
    return NULL;
  }
  if (gst_rtsp_connection_receive (conn, response, NULL) != GST_RTSP_OK) {
    GST_DEBUG ("failed to read response");
    gst_rtsp_message_free (response);
    return NULL;
  }
  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);
  return response;
}

/* send an rtsp request and receive response. gchar** parameters are out
 * parameters that have to be freed by the caller */
static GstRTSPStatusCode
do_request_full (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * control, const gchar * session_in, const gchar * transport_in,
    const gchar * range_in, const gchar * require_in,
    gchar ** content_type, gchar ** content_base, gchar ** body,
    gchar ** session_out, gchar ** transport_out, gchar ** range_out,
    gchar ** unsupported_out)
{
  GstRTSPMessage *request;
  GstRTSPMessage *response;
  GstRTSPStatusCode code;
  gchar *value;

  /* create request */
  request = create_request (conn, method, control);

  /* add headers */
  if (session_in) {
    gst_rtsp_message_add_header (request, GST_RTSP_HDR_SESSION, session_in);
  }
  if (transport_in) {
    gst_rtsp_message_add_header (request, GST_RTSP_HDR_TRANSPORT, transport_in);
  }
  if (range_in) {
    gst_rtsp_message_add_header (request, GST_RTSP_HDR_RANGE, range_in);
  }
  if (require_in) {
    gst_rtsp_message_add_header (request, GST_RTSP_HDR_REQUIRE, require_in);
  }

  /* send request */
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);

  iterate ();

  /* read response */
  response = read_response (conn);

  /* check status line */
  gst_rtsp_message_parse_response (response, &code, NULL, NULL);
  if (code != GST_RTSP_STS_OK) {
    if (unsupported_out != NULL && code == GST_RTSP_STS_OPTION_NOT_SUPPORTED) {
      gst_rtsp_message_get_header (response, GST_RTSP_HDR_UNSUPPORTED,
          &value, 0);
      *unsupported_out = g_strdup (value);
    }
    gst_rtsp_message_free (response);
    return code;
  }

  /* get information from response */
  if (content_type) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_CONTENT_TYPE,
        &value, 0);
    *content_type = g_strdup (value);
  }
  if (content_base) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_CONTENT_BASE,
        &value, 0);
    *content_base = g_strdup (value);
  }
  if (body) {
    *body = g_malloc (response->body_size + 1);
    strncpy (*body, (gchar *) response->body, response->body_size);
  }
  if (session_out) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION, &value, 0);

    value = g_strdup (value);

    /* Remove the timeout */
    if (value) {
      char *pos = strchr (value, ';');
      if (pos)
        *pos = 0;
    }
    if (session_in) {
      /* check that we got the same session back */
      fail_unless (!g_strcmp0 (value, session_in));
    }
    *session_out = value;
  }
  if (transport_out) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_TRANSPORT, &value, 0);
    *transport_out = g_strdup (value);
  }
  if (range_out) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_RANGE, &value, 0);
    *range_out = g_strdup (value);
  }

  gst_rtsp_message_free (response);
  return code;
}

/* send an rtsp request and receive response. gchar** parameters are out
 * parameters that have to be freed by the caller */
static GstRTSPStatusCode
do_request (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * control, const gchar * session_in,
    const gchar * transport_in, const gchar * range_in,
    gchar ** content_type, gchar ** content_base, gchar ** body,
    gchar ** session_out, gchar ** transport_out, gchar ** range_out)
{
  return do_request_full (conn, method, control, session_in, transport_in,
      range_in, NULL, content_type, content_base, body, session_out,
      transport_out, range_out, NULL);
}

/* send an rtsp request with a method and a session, and receive response */
static GstRTSPStatusCode
do_simple_request (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * session)
{
  return do_request (conn, method, NULL, session, NULL, NULL, NULL,
      NULL, NULL, NULL, NULL, NULL);
}

/* send a DESCRIBE request and receive response. returns a received
 * GstSDPMessage that must be freed by the caller */
static GstSDPMessage *
do_describe (GstRTSPConnection * conn, const gchar * mount_point)
{
  GstSDPMessage *sdp_message;
  gchar *content_type;
  gchar *content_base;
  gchar *body;
  gchar *address;
  gchar *expected_content_base;

  /* send DESCRIBE request */
  fail_unless (do_request (conn, GST_RTSP_DESCRIBE, NULL, NULL, NULL, NULL,
          &content_type, &content_base, &body, NULL, NULL, NULL) ==
      GST_RTSP_STS_OK);

  /* check response values */
  fail_unless (!g_strcmp0 (content_type, "application/sdp"));
  address = gst_rtsp_server_get_address (server);
  expected_content_base =
      g_strdup_printf ("rtsp://%s:%d%s/", address, test_port, mount_point);
  fail_unless (!g_strcmp0 (content_base, expected_content_base));

  /* create sdp message */
  fail_unless (gst_sdp_message_new (&sdp_message) == GST_SDP_OK);
  fail_unless (gst_sdp_message_parse_buffer ((guint8 *) body,
          strlen (body), sdp_message) == GST_SDP_OK);

  /* clean up */
  g_free (content_type);
  g_free (content_base);
  g_free (body);
  g_free (address);
  g_free (expected_content_base);

  return sdp_message;
}

/* send a SETUP request and receive response. if *session is not NULL,
 * it is used in the request. otherwise, *session is set to a returned
 * session string that must be freed by the caller. the returned
 * transport must be freed by the caller. */
static GstRTSPStatusCode
do_setup_full (GstRTSPConnection * conn, const gchar * control,
    const GstRTSPRange * client_ports, const gchar * require, gchar ** session,
    GstRTSPTransport ** transport, gchar ** unsupported)
{
  GstRTSPStatusCode code;
  gchar *session_in = NULL;
  gchar *transport_string_in = NULL;
  gchar **session_out = NULL;
  gchar *transport_string_out = NULL;

  /* prepare and send SETUP request */
  if (session) {
    if (*session) {
      session_in = *session;
    } else {
      session_out = session;
    }
  }
  transport_string_in =
      g_strdup_printf (TEST_PROTO ";unicast;client_port=%d-%d",
      client_ports->min, client_ports->max);
  code =
      do_request_full (conn, GST_RTSP_SETUP, control, session_in,
      transport_string_in, NULL, require, NULL, NULL, NULL, session_out,
      &transport_string_out, NULL, unsupported);
  g_free (transport_string_in);

  if (transport_string_out) {
    /* create transport */
    fail_unless (gst_rtsp_transport_new (transport) == GST_RTSP_OK);
    fail_unless (gst_rtsp_transport_parse (transport_string_out,
            *transport) == GST_RTSP_OK);
    g_free (transport_string_out);
  }
  GST_INFO ("code=%d", code);
  return code;
}

/* send a SETUP request and receive response. if *session is not NULL,
 * it is used in the request. otherwise, *session is set to a returned
 * session string that must be freed by the caller. the returned
 * transport must be freed by the caller. */
static GstRTSPStatusCode
do_setup (GstRTSPConnection * conn, const gchar * control,
    const GstRTSPRange * client_ports, gchar ** session,
    GstRTSPTransport ** transport)
{
  return do_setup_full (conn, control, client_ports, NULL, session, transport,
      NULL);
}

/* fixture setup function */
static void
setup (void)
{
  server = gst_rtsp_server_new ();
}

/* fixture clean-up function */
static void
teardown (void)
{
  if (server) {
    g_object_unref (server);
    server = NULL;
  }
  test_port = 0;
}

GST_START_TEST (test_connect)
{
  GstRTSPConnection *conn;

  start_server ();

  /* connect to server */
  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* clean up */
  gst_rtsp_connection_free (conn);
  stop_server ();

  /* iterate so the clean-up can finish */
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_describe)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  gint32 format;
  gchar *expected_rtpmap;
  const gchar *rtpmap;
  const gchar *control_video;
  const gchar *control_audio;

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* send DESCRIBE request */
  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);

  /* check video sdp */
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  fail_unless (!g_strcmp0 (gst_sdp_media_get_proto (sdp_media), TEST_PROTO));
  fail_unless (gst_sdp_media_formats_len (sdp_media) == 1);
  sscanf (gst_sdp_media_get_format (sdp_media, 0), "%" G_GINT32_FORMAT,
      &format);
  expected_rtpmap =
      g_strdup_printf ("%d " TEST_ENCODING "/" TEST_CLOCK_RATE, format);
  rtpmap = gst_sdp_media_get_attribute_val (sdp_media, "rtpmap");
  fail_unless (!g_strcmp0 (rtpmap, expected_rtpmap));
  g_free (expected_rtpmap);
  control_video = gst_sdp_media_get_attribute_val (sdp_media, "control");
  fail_unless (!g_strcmp0 (control_video, "stream=0"));

  /* check audio sdp */
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  fail_unless (!g_strcmp0 (gst_sdp_media_get_proto (sdp_media), TEST_PROTO));
  fail_unless (gst_sdp_media_formats_len (sdp_media) == 1);
  sscanf (gst_sdp_media_get_format (sdp_media, 0), "%" G_GINT32_FORMAT,
      &format);
  expected_rtpmap =
      g_strdup_printf ("%d " TEST_ENCODING "/" TEST_CLOCK_RATE, format);
  rtpmap = gst_sdp_media_get_attribute_val (sdp_media, "rtpmap");
  fail_unless (!g_strcmp0 (rtpmap, expected_rtpmap));
  g_free (expected_rtpmap);
  control_audio = gst_sdp_media_get_attribute_val (sdp_media, "control");
  fail_unless (!g_strcmp0 (control_audio, "stream=1"));

  /* clean up and iterate so the clean-up can finish */
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_describe_non_existing_mount_point)
{
  GstRTSPConnection *conn;

  start_server ();

  /* send DESCRIBE request for a non-existing mount point
   * and check that we get a 404 Not Found */
  conn = connect_to_server (test_port, "/non-existing");
  fail_unless (do_simple_request (conn, GST_RTSP_DESCRIBE, NULL)
      == GST_RTSP_STS_NOT_FOUND);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_setup)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_ports;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_ports);

  /* send SETUP request for video */
  fail_unless (do_setup (conn, video_control, &client_ports, &session,
          &video_transport) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up video %s, got session '%s'", video_control, session);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* send SETUP request for audio */
  fail_unless (do_setup (conn, audio_control, &client_ports, &session,
          &audio_transport) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up audio %s with session '%s'", audio_control, session);

  /* check response from SETUP */
  fail_unless (audio_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (audio_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (audio_transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (audio_transport->mode_play);
  gst_rtsp_transport_free (audio_transport);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_free (session);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_setup_with_require_header)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  GstRTSPRange client_ports;
  gchar *session = NULL;
  gchar *unsupported = NULL;
  GstRTSPTransport *video_transport = NULL;

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_ports);

  /* send SETUP request for video, with single Require header */
  fail_unless_equals_int (do_setup_full (conn, video_control, &client_ports,
          "funky-feature", &session, &video_transport, &unsupported),
      GST_RTSP_STS_OPTION_NOT_SUPPORTED);
  fail_unless_equals_string (unsupported, "funky-feature");
  g_free (unsupported);
  unsupported = NULL;

  /* send SETUP request for video, with multiple Require headers */
  fail_unless_equals_int (do_setup_full (conn, video_control, &client_ports,
          "funky-feature, foo-bar, superburst", &session, &video_transport,
          &unsupported), GST_RTSP_STS_OPTION_NOT_SUPPORTED);
  fail_unless_equals_string (unsupported, "funky-feature, foo-bar, superburst");
  g_free (unsupported);
  unsupported = NULL;

  /* ok, just do a normal setup then (make sure that still works) */
  fail_unless_equals_int (do_setup (conn, video_control, &client_ports,
          &session, &video_transport), GST_RTSP_STS_OK);

  GST_DEBUG ("set up video %s, got session '%s'", video_control, session);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_free (session);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_setup_non_existing_stream)
{
  GstRTSPConnection *conn;
  GstRTSPRange client_ports;

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  get_client_ports (&client_ports);

  /* send SETUP request with a non-existing stream and check that we get a
   * 404 Not Found */
  fail_unless (do_setup (conn, "stream=7", &client_ports, NULL,
          NULL) == GST_RTSP_STS_NOT_FOUND);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

static void
receive_rtp (GSocket * socket, GSocketAddress ** addr)
{
  GstBuffer *buffer = gst_buffer_new_allocate (NULL, 65536, NULL);

  for (;;) {
    gssize bytes;
    GstMapInfo map = GST_MAP_INFO_INIT;
    GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

    gst_buffer_map (buffer, &map, GST_MAP_WRITE);
    bytes = g_socket_receive_from (socket, addr, (gchar *) map.data,
        map.maxsize, NULL, NULL);
    fail_unless (bytes > 0);
    gst_buffer_unmap (buffer, &map);
    gst_buffer_set_size (buffer, bytes);

    if (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtpbuffer)) {
      gst_rtp_buffer_unmap (&rtpbuffer);
      break;
    }

    if (addr)
      g_clear_object (addr);
  }

  gst_buffer_unref (buffer);
}

static void
receive_rtcp (GSocket * socket, GSocketAddress ** addr, GstRTCPType type)
{
  GstBuffer *buffer = gst_buffer_new_allocate (NULL, 65536, NULL);

  for (;;) {
    gssize bytes;
    GstMapInfo map = GST_MAP_INFO_INIT;

    gst_buffer_map (buffer, &map, GST_MAP_WRITE);
    bytes = g_socket_receive_from (socket, addr, (gchar *) map.data,
        map.maxsize, NULL, NULL);
    fail_unless (bytes > 0);
    gst_buffer_unmap (buffer, &map);
    gst_buffer_set_size (buffer, bytes);

    if (gst_rtcp_buffer_validate (buffer)) {
      GstRTCPBuffer rtcpbuffer = GST_RTCP_BUFFER_INIT;
      GstRTCPPacket packet;

      if (type) {
        fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READ, &rtcpbuffer));
        fail_unless (gst_rtcp_buffer_get_first_packet (&rtcpbuffer, &packet));
        do {
          if (gst_rtcp_packet_get_type (&packet) == type) {
            gst_rtcp_buffer_unmap (&rtcpbuffer);
            goto done;
          }
        } while (gst_rtcp_packet_move_to_next (&packet));
        gst_rtcp_buffer_unmap (&rtcpbuffer);
      } else {
        break;
      }
    }

    if (addr)
      g_clear_object (addr);
  }

done:

  gst_buffer_unref (buffer);
}

static void
do_test_play (const gchar * range)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GSocket *rtp_socket, *rtcp_socket;
  gchar *range_out = NULL;

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports_full (&client_port, &rtp_socket, &rtcp_socket);

  /* do SETUP for video and audio */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);
  fail_unless (do_setup (conn, audio_control, &client_port, &session,
          &audio_transport) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_request (conn, GST_RTSP_PLAY, NULL, session, NULL, range,
          NULL, NULL, NULL, NULL, NULL, &range_out) == GST_RTSP_STS_OK);
  if (range)
    fail_unless_equals_string (range, range_out);
  g_free (range_out);

  receive_rtp (rtp_socket, NULL);
  receive_rtcp (rtcp_socket, NULL, 0);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* FIXME: The rtsp-server always disconnects the transport before
   * sending the RTCP BYE
   * receive_rtcp (rtcp_socket, NULL, GST_RTCP_TYPE_BYE);
   */

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (rtp_socket);
  g_object_unref (rtcp_socket);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
}


GST_START_TEST (test_play)
{
  start_server ();

  do_test_play (NULL);

  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_play_without_session)
{
  GstRTSPConnection *conn;

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* send PLAY request without a session and check that we get a
   * 454 Session Not Found */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          NULL) == GST_RTSP_STS_SESSION_NOT_FOUND);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_bind_already_in_use)
{
  GstRTSPServer *serv;
  GSocketService *service;
  GError *error = NULL;
  guint16 port;
  gchar *port_str;

  serv = gst_rtsp_server_new ();
  service = g_socket_service_new ();

  /* bind service to port */
  port =
      g_socket_listener_add_any_inet_port (G_SOCKET_LISTENER (service), NULL,
      &error);
  g_assert_no_error (error);

  port_str = g_strdup_printf ("%d\n", port);

  /* try to bind server to the same port */
  g_object_set (serv, "service", port_str, NULL);
  g_free (port_str);

  /* attach to default main context */
  fail_unless (gst_rtsp_server_attach (serv, NULL) == 0);

  /* cleanup */
  g_object_unref (serv);
  g_socket_service_stop (service);
  g_object_unref (service);
}

GST_END_TEST;


GST_START_TEST (test_play_multithreaded)
{
  GstRTSPThreadPool *pool;

  pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_thread_pool_set_max_threads (pool, 2);
  g_object_unref (pool);

  start_server ();

  do_test_play (NULL);

  stop_server ();
  iterate ();
}

GST_END_TEST;

enum
{
  BLOCK_ME,
  BLOCKED,
  UNBLOCK
};


static void
media_constructed_block (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media, gpointer user_data)
{
  gint *block_state = user_data;

  g_mutex_lock (&check_mutex);

  *block_state = BLOCKED;
  g_cond_broadcast (&check_cond);

  while (*block_state != UNBLOCK)
    g_cond_wait (&check_cond, &check_mutex);
  g_mutex_unlock (&check_mutex);
}


GST_START_TEST (test_play_multithreaded_block_in_describe)
{
  GstRTSPConnection *conn;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  gint block_state = BLOCK_ME;
  GstRTSPMessage *request;
  GstRTSPMessage *response;
  GstRTSPStatusCode code;
  GstRTSPThreadPool *pool;

  pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_thread_pool_set_max_threads (pool, 2);
  g_object_unref (pool);

  mounts = gst_rtsp_server_get_mount_points (server);
  fail_unless (mounts != NULL);
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory,
      "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");
  g_signal_connect (factory, "media-constructed",
      G_CALLBACK (media_constructed_block), &block_state);
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT "2", factory);
  g_object_unref (mounts);

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT "2");
  iterate ();

  /* do describe, it will not return now as we've blocked it */
  request = create_request (conn, GST_RTSP_DESCRIBE, NULL);
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);

  g_mutex_lock (&check_mutex);
  while (block_state != BLOCKED)
    g_cond_wait (&check_cond, &check_mutex);
  g_mutex_unlock (&check_mutex);

  /* Do a second connection while the first one is blocked */
  do_test_play (NULL);

  /* Now unblock the describe */
  g_mutex_lock (&check_mutex);
  block_state = UNBLOCK;
  g_cond_broadcast (&check_cond);
  g_mutex_unlock (&check_mutex);

  response = read_response (conn);
  gst_rtsp_message_parse_response (response, &code, NULL, NULL);
  fail_unless (code == GST_RTSP_STS_OK);
  gst_rtsp_message_free (response);


  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();

}

GST_END_TEST;


static void
new_session_timeout_one (GstRTSPClient * client,
    GstRTSPSession * session, gpointer user_data)
{
  gst_rtsp_session_set_timeout (session, 1);

  g_signal_handlers_disconnect_by_func (client, new_session_timeout_one,
      user_data);
}

static void
session_connected_new_session_cb (GstRTSPServer * server,
    GstRTSPClient * client, gpointer user_data)
{

  g_signal_connect (client, "new-session", user_data, NULL);
}

GST_START_TEST (test_play_multithreaded_timeout_client)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GstRTSPSessionPool *pool;
  GstRTSPThreadPool *thread_pool;

  thread_pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_thread_pool_set_max_threads (thread_pool, 2);
  g_object_unref (thread_pool);

  pool = gst_rtsp_server_get_session_pool (server);
  g_signal_connect (server, "client-connected",
      G_CALLBACK (session_connected_new_session_cb), new_session_timeout_one);

  start_server ();


  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_port);

  /* do SETUP for video and audio */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);
  fail_unless (do_setup (conn, audio_control, &client_port, &session,
          &audio_transport) == GST_RTSP_STS_OK);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 1);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);

  sleep (7);

  fail_unless (gst_rtsp_session_pool_cleanup (pool) == 1);
  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 0);

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (pool);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);

  stop_server ();
  iterate ();
}

GST_END_TEST;


GST_START_TEST (test_play_multithreaded_timeout_session)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session1 = NULL;
  gchar *session2 = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GstRTSPSessionPool *pool;
  GstRTSPThreadPool *thread_pool;

  thread_pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_thread_pool_set_max_threads (thread_pool, 2);
  g_object_unref (thread_pool);

  pool = gst_rtsp_server_get_session_pool (server);
  g_signal_connect (server, "client-connected",
      G_CALLBACK (session_connected_new_session_cb), new_session_timeout_one);

  start_server ();


  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  gst_rtsp_connection_set_remember_session_id (conn, FALSE);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_port);

  /* do SETUP for video and audio */
  fail_unless (do_setup (conn, video_control, &client_port, &session1,
          &video_transport) == GST_RTSP_STS_OK);
  fail_unless (do_setup (conn, audio_control, &client_port, &session2,
          &audio_transport) == GST_RTSP_STS_OK);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 2);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session1) == GST_RTSP_STS_OK);
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session2) == GST_RTSP_STS_OK);

  sleep (7);

  fail_unless (gst_rtsp_session_pool_cleanup (pool) == 1);

  /* send TEARDOWN request and check that we get 454 Session Not found */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session1) == GST_RTSP_STS_SESSION_NOT_FOUND);

  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session2) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (pool);
  g_free (session1);
  g_free (session2);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);

  stop_server ();
  iterate ();
}

GST_END_TEST;


GST_START_TEST (test_play_disconnect)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  g_signal_connect (server, "client-connected",
      G_CALLBACK (session_connected_new_session_cb), new_session_timeout_one);

  start_server ();

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_port);

  /* do SETUP for video and audio */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);
  fail_unless (do_setup (conn, audio_control, &client_port, &session,
          &audio_transport) == GST_RTSP_STS_OK);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 1);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);

  gst_rtsp_connection_free (conn);

  sleep (7);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 1);
  fail_unless (gst_rtsp_session_pool_cleanup (pool) == 1);


  /* clean up and iterate so the clean-up can finish */
  g_object_unref (pool);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);

  stop_server ();
  iterate ();
}

GST_END_TEST;

/* Only different with test_play is the specific ports selected */

GST_START_TEST (test_play_specific_server_port)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *pool;
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GSocket *rtp_socket, *rtcp_socket;
  GSocketAddress *rtp_address, *rtcp_address;
  guint16 rtp_port, rtcp_port;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();
  pool = gst_rtsp_address_pool_new ();
  gst_rtsp_address_pool_add_range (pool, GST_RTSP_ADDRESS_POOL_ANY_IPV4,
      GST_RTSP_ADDRESS_POOL_ANY_IPV4, 7770, 7780, 0);
  gst_rtsp_media_factory_set_address_pool (factory, pool);
  g_object_unref (pool);
  gst_rtsp_media_factory_set_launch (factory, "( " VIDEO_PIPELINE " )");
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT, factory);
  g_object_unref (mounts);

  /* set port to any */
  gst_rtsp_server_set_service (server, "0");

  /* attach to default main context */
  source_id = gst_rtsp_server_attach (server, NULL);
  fail_if (source_id == 0);

  /* get port */
  service = gst_rtsp_server_get_service (server);
  test_port = atoi (service);
  fail_unless (test_port != 0);
  g_free (service);

  GST_DEBUG ("rtsp server listening on port %d", test_port);


  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 1);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports_full (&client_port, &rtp_socket, &rtcp_socket);

  /* do SETUP for video */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);

  receive_rtp (rtp_socket, &rtp_address);
  receive_rtcp (rtcp_socket, &rtcp_address, 0);

  fail_unless (G_IS_INET_SOCKET_ADDRESS (rtp_address));
  fail_unless (G_IS_INET_SOCKET_ADDRESS (rtcp_address));
  rtp_port =
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (rtp_address));
  rtcp_port =
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (rtcp_address));
  fail_unless (rtp_port >= 7770 && rtp_port <= 7780 && rtp_port % 2 == 0);
  fail_unless (rtcp_port >= 7770 && rtcp_port <= 7780 && rtcp_port % 2 == 1);
  fail_unless (rtp_port + 1 == rtcp_port);

  g_object_unref (rtp_address);
  g_object_unref (rtcp_address);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* FIXME: The rtsp-server always disconnects the transport before
   * sending the RTCP BYE
   * receive_rtcp (rtcp_socket, NULL, GST_RTCP_TYPE_BYE);
   */

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (rtp_socket);
  g_object_unref (rtcp_socket);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);


  stop_server ();
  iterate ();
}

GST_END_TEST;


GST_START_TEST (test_play_smpte_range)
{
  start_server ();

  do_test_play ("npt=5-");
  do_test_play ("smpte=0:00:00-");
  do_test_play ("smpte=1:00:00-");
  do_test_play ("smpte=1:00:03-");
  do_test_play ("clock=20120321T152256Z-");

  stop_server ();
  iterate ();
}

GST_END_TEST;


static Suite *
rtspserver_suite (void)
{
  Suite *s = suite_create ("rtspserver");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_connect);
  tcase_add_test (tc, test_describe);
  tcase_add_test (tc, test_describe_non_existing_mount_point);
  tcase_add_test (tc, test_setup);
  tcase_add_test (tc, test_setup_with_require_header);
  tcase_add_test (tc, test_setup_non_existing_stream);
  tcase_add_test (tc, test_play);
  tcase_add_test (tc, test_play_without_session);
  tcase_add_test (tc, test_bind_already_in_use);
  tcase_add_test (tc, test_play_multithreaded);
  tcase_add_test (tc, test_play_multithreaded_block_in_describe);
  tcase_add_test (tc, test_play_multithreaded_timeout_client);
  tcase_add_test (tc, test_play_multithreaded_timeout_session);
  tcase_add_test (tc, test_play_disconnect);
  tcase_add_test (tc, test_play_specific_server_port);
  tcase_add_test (tc, test_play_smpte_range);
  return s;
}

GST_CHECK_MAIN (rtspserver);
