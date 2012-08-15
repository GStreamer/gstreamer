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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>
#include <gst/sdp/gstsdpmessage.h>

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

/* returns an unused port that can be used by the test */
static int
get_unused_port (gint type)
{
  int sock;
  struct sockaddr_in addr;
  socklen_t addr_len;
  gint port;

  /* create socket */
  fail_unless ((sock = socket (AF_INET, type, 0)) > 0);

  /* pass port 0 to bind, which will bind to any free port */
  memset (&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons (0);
  fail_unless (bind (sock, (struct sockaddr *) &addr, sizeof addr) == 0);

  /* ask what port was bound using getsockname */
  addr_len = sizeof addr;
  memset (&addr, 0, addr_len);
  fail_unless (getsockname (sock, (struct sockaddr *) &addr, &addr_len) == 0);
  port = ntohs (addr.sin_port);

  /* close the socket so the port gets unbound again (and can be used by the
   * test) */
  close (sock);

  return port;
}

/* returns TRUE if the given port is not currently bound */
static gboolean
port_is_unused (gint port, gint type)
{
  int sock;
  struct sockaddr_in addr;
  gboolean is_bound;

  /* create socket */
  fail_unless ((sock = socket (AF_INET, type, 0)) > 0);

  /* check if the port is already bound by trying to bind to it (again) */
  memset (&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons (port);
  is_bound = (bind (sock, (struct sockaddr *) &addr, sizeof addr) != 0);

  /* close the socket, which will unbind if bound by our call to bind */
  close (sock);

  return !is_bound;
}

/* get a free rtp/rtcp client port pair */
static void
get_client_ports (GstRTSPRange * range)
{
  gint rtp_port;
  gint rtcp_port;

  /* get a pair of unused ports, where the rtp port is even */
  do {
    rtp_port = get_unused_port (SOCK_DGRAM);
    rtcp_port = rtp_port + 1;
  } while (rtp_port % 2 != 0 || !port_is_unused (rtcp_port, SOCK_DGRAM));
  range->min = rtp_port;
  range->max = rtcp_port;
  GST_DEBUG ("client_port=%d-%d", range->min, range->max);
}

/* start the tested rtsp server */
static void
start_server ()
{
  GstRTSPMediaMapping *mapping;
  gchar *service;
  GstRTSPMediaFactory *factory;

  mapping = gst_rtsp_server_get_media_mapping (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_launch (factory,
      "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");

  gst_rtsp_media_mapping_add_factory (mapping, TEST_MOUNT_POINT, factory);
  g_object_unref (mapping);

  /* set port */
  test_port = get_unused_port (SOCK_STREAM);
  service = g_strdup_printf ("%d", test_port);
  gst_rtsp_server_set_service (server, service);
  g_free (service);

  /* attach to default main context */
  source_id = gst_rtsp_server_attach (server, NULL);
  fail_if (source_id == 0);

  GST_DEBUG ("rtsp server listening on port %d", test_port);
}

/* stop the tested rtsp server */
static void
stop_server ()
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
  gst_rtsp_url_parse (uri_string, &url);
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
do_request (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * control, const gchar * session_in, const gchar * transport_in,
    gchar ** content_type, gchar ** content_base, gchar ** body,
    gchar ** session_out, gchar ** transport_out)
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

  /* send request */
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);

  iterate ();

  /* read response */
  response = read_response (conn);

  /* check status line */
  gst_rtsp_message_parse_response (response, &code, NULL, NULL);
  if (code != GST_RTSP_STS_OK) {
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
    if (session_in) {
      /* check that we got the same session back */
      fail_unless (!g_strcmp0 (value, session_in));
    }
    *session_out = g_strdup (value);
  }
  if (transport_out) {
    gst_rtsp_message_get_header (response, GST_RTSP_HDR_TRANSPORT, &value, 0);
    *transport_out = g_strdup (value);
  }

  gst_rtsp_message_free (response);
  return code;
}

/* send an rtsp request with a method and a session, and receive response */
static GstRTSPStatusCode
do_simple_request (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * session)
{
  return do_request (conn, method, NULL, session, NULL, NULL, NULL,
      NULL, NULL, NULL);
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
  fail_unless (do_request (conn, GST_RTSP_DESCRIBE, NULL, NULL, NULL,
          &content_type, &content_base, &body, NULL, NULL) == GST_RTSP_STS_OK);

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
do_setup (GstRTSPConnection * conn, const gchar * control,
    const GstRTSPRange * client_ports, gchar ** session,
    GstRTSPTransport ** transport)
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
      do_request (conn, GST_RTSP_SETUP, control, session_in,
      transport_string_in, NULL, NULL, NULL, session_out,
      &transport_string_out);
  g_free (transport_string_in);

  if (transport_string_out) {
    /* create transport */
    fail_unless (gst_rtsp_transport_new (transport) == GST_RTSP_OK);
    fail_unless (gst_rtsp_transport_parse (transport_string_out,
            *transport) == GST_RTSP_OK);
    g_free (transport_string_out);
  }

  return code;
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

  /* need to unref the server here, otherwise threads will remain
   * and teardown won't be run */
  g_object_unref (server);
  server = NULL;
}

GST_END_TEST;

GST_START_TEST (test_play)
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

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
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
  port = g_socket_listener_add_any_inet_port (G_SOCKET_LISTENER (service), NULL, &error);
  g_assert_no_error (error);

  port_str = g_strdup_printf ("%d\n", port);

  /* try to bind server to the same port */
  g_object_set (serv, "service", port_str, NULL);
  g_free (port_str);

  /* attach to default main context */
  fail_unless (gst_rtsp_server_attach (serv, NULL) == 0);

  /* cleanup */
  g_object_unref (serv);
  g_socket_listener_close (G_SOCKET_LISTENER (service));
  g_object_unref (service);
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
  tcase_add_test (tc, test_setup_non_existing_stream);
  tcase_add_test (tc, test_play);
  tcase_add_test (tc, test_play_without_session);
  tcase_add_test (tc, test_bind_already_in_use);

  return s;
}

GST_CHECK_MAIN (rtspserver);
