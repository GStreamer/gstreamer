/* GStreamer unit test for GstRTSPServer
 * Copyright (C) 2012 Axis Communications <dev-gstreamer at axis dot com>
 *   @author David Svensson Fors <davidsf at axis dot com>
 * Copyright (C) 2015 Centricular Ltd
 *   @author Tim-Philipp Müller <tim@centricular.com>
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

#define ERRORIGNORE "errorignore ignore-error=false ignore-notlinked=true " \
  "ignore-notnegotiated=false convert-to=ok"
#define VIDEO_PIPELINE "videotestsrc ! " \
  ERRORIGNORE " ! " \
  "video/x-raw,format=I420,width=352,height=288 ! " \
  "rtpgstpay name=pay0 pt=96"
#define AUDIO_PIPELINE "audiotestsrc ! " \
  ERRORIGNORE " ! " \
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
start_server (gboolean set_shared_factory)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *pool;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_launch (factory,
      "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT, factory);
  g_object_unref (mounts);

  /* use an address pool for multicast */
  pool = gst_rtsp_address_pool_new ();
  gst_rtsp_address_pool_add_range (pool,
      "224.3.0.0", "224.3.0.10", 5500, 5510, 16);
  gst_rtsp_address_pool_add_range (pool, GST_RTSP_ADDRESS_POOL_ANY_IPV4,
      GST_RTSP_ADDRESS_POOL_ANY_IPV4, 6000, 6010, 0);
  gst_rtsp_media_factory_set_address_pool (factory, pool);
  gst_rtsp_media_factory_set_shared (factory, set_shared_factory);
  gst_object_unref (pool);

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

static void
start_tcp_server (gboolean set_shared_factory)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_protocols (factory, GST_RTSP_LOWER_TRANS_TCP);
  gst_rtsp_media_factory_set_launch (factory,
      "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT, factory);
  gst_rtsp_media_factory_set_shared (factory, set_shared_factory);
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

/* start the testing rtsp server for RECORD mode */
static GstRTSPMediaFactory *
start_record_server (const gchar * launch_line)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMountPoints *mounts;
  gchar *service;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_transport_mode (factory,
      GST_RTSP_TRANSPORT_MODE_RECORD);
  gst_rtsp_media_factory_set_launch (factory, launch_line);
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
  return factory;
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
  GstRTSPMsgType type;

  if (gst_rtsp_message_new (&response) != GST_RTSP_OK) {
    GST_DEBUG ("failed to create response object");
    return NULL;
  }
  if (gst_rtsp_connection_receive (conn, response, NULL) != GST_RTSP_OK) {
    GST_DEBUG ("failed to read response");
    gst_rtsp_message_free (response);
    return NULL;
  }
  type = gst_rtsp_message_get_type (response);
  fail_unless (type == GST_RTSP_MESSAGE_RESPONSE
      || type == GST_RTSP_MESSAGE_DATA);
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
  GstRTSPMsgType msg_type;

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
  fail_unless (response != NULL);

  msg_type = gst_rtsp_message_get_type (response);

  if (msg_type == GST_RTSP_MESSAGE_DATA) {
    do {
      gst_rtsp_message_free (response);
      response = read_response (conn);
      msg_type = gst_rtsp_message_get_type (response);
    } while (msg_type == GST_RTSP_MESSAGE_DATA);
  }

  fail_unless (msg_type == GST_RTSP_MESSAGE_RESPONSE);

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

/* send an rtsp request with a method,session and range in,
 * and receive response. range_in is the Range in req header */
static GstRTSPStatusCode
do_simple_request_rangein (GstRTSPConnection * conn, GstRTSPMethod method,
    const gchar * session, const gchar * rangein)
{
  return do_request (conn, method, NULL, session, NULL, rangein, NULL,
      NULL, NULL, NULL, NULL, NULL);
}

/* send a DESCRIBE request and receive response. returns a received
 * GstSDPMessage that must be freed by the caller */
static GstSDPMessage *
do_describe (GstRTSPConnection * conn, const gchar * mount_point)
{
  GstSDPMessage *sdp_message;
  gchar *content_type = NULL;
  gchar *content_base = NULL;
  gchar *body = NULL;
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
    GstRTSPLowerTrans lower_transport, const GstRTSPRange * client_ports,
    const gchar * require, gchar ** session, GstRTSPTransport ** transport,
    gchar ** unsupported)
{
  GstRTSPStatusCode code;
  gchar *session_in = NULL;
  GString *transport_string_in = NULL;
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

  transport_string_in = g_string_new (TEST_PROTO);
  switch (lower_transport) {
    case GST_RTSP_LOWER_TRANS_UDP:
      transport_string_in =
          g_string_append (transport_string_in, "/UDP;unicast");
      break;
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
      transport_string_in =
          g_string_append (transport_string_in, "/UDP;multicast");
      break;
    case GST_RTSP_LOWER_TRANS_TCP:
      transport_string_in =
          g_string_append (transport_string_in, "/TCP;unicast");
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (client_ports) {
    g_string_append_printf (transport_string_in, ";client_port=%d-%d",
        client_ports->min, client_ports->max);
  }

  code =
      do_request_full (conn, GST_RTSP_SETUP, control, session_in,
      transport_string_in->str, NULL, require, NULL, NULL, NULL, session_out,
      &transport_string_out, NULL, unsupported);
  g_string_free (transport_string_in, TRUE);

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
  return do_setup_full (conn, control, GST_RTSP_LOWER_TRANS_UDP, client_ports,
      NULL, session, transport, NULL);
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

  start_server (FALSE);

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

  start_server (FALSE);

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

GST_START_TEST (test_describe_record_media)
{
  GstRTSPConnection *conn;

  start_record_server ("( fakesink name=depay0 )");

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* send DESCRIBE request */
  fail_unless_equals_int (do_request (conn, GST_RTSP_DESCRIBE, NULL, NULL, NULL,
          NULL, NULL, NULL, NULL, NULL, NULL, NULL),
      GST_RTSP_STS_METHOD_NOT_ALLOWED);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_describe_non_existing_mount_point)
{
  GstRTSPConnection *conn;

  start_server (FALSE);

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

static void
do_test_setup (GstRTSPLowerTrans lower_transport)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_ports = { 0 };
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;

  start_server (FALSE);

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
  fail_unless (do_setup_full (conn, video_control, lower_transport,
          &client_ports, NULL, &session, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up video %s, got session '%s'", video_control, session);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == lower_transport);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* send SETUP request for audio */
  fail_unless (do_setup_full (conn, audio_control, lower_transport,
          &client_ports, NULL, &session, &audio_transport,
          NULL) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up audio %s with session '%s'", audio_control, session);

  /* check response from SETUP */
  fail_unless (audio_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (audio_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (audio_transport->lower_transport == lower_transport);
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

GST_START_TEST (test_setup_udp)
{
  do_test_setup (GST_RTSP_LOWER_TRANS_UDP);
}

GST_END_TEST;

GST_START_TEST (test_setup_tcp)
{
  do_test_setup (GST_RTSP_LOWER_TRANS_TCP);
}

GST_END_TEST;

GST_START_TEST (test_setup_udp_mcast)
{
  do_test_setup (GST_RTSP_LOWER_TRANS_UDP_MCAST);
}

GST_END_TEST;

GST_START_TEST (test_setup_twice)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  GstRTSPRange client_ports;
  GstRTSPTransport *video_transport = NULL;
  gchar *session1 = NULL;
  gchar *session2 = NULL;

  start_server (FALSE);

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* we wan't more than one session for this connection */
  gst_rtsp_connection_set_remember_session_id (conn, FALSE);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get the control url */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_ports);

  /* send SETUP request for one session */
  fail_unless (do_setup (conn, video_control, &client_ports, &session1,
          &video_transport) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up video %s, got session '%s'", video_control, session1);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* send SETUP request for another session */
  fail_unless (do_setup (conn, video_control, &client_ports, &session2,
          &video_transport) == GST_RTSP_STS_OK);
  GST_DEBUG ("set up video %s, got session '%s'", video_control, session2);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* session can not be the same */
  fail_unless (strcmp (session1, session2));

  /* send TEARDOWN request for the first session */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session1) == GST_RTSP_STS_OK);

  /* send TEARDOWN request for the second session */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session2) == GST_RTSP_STS_OK);

  g_free (session1);
  g_free (session2);
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

  start_server (FALSE);

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_ports);

  /* send SETUP request for video, with single Require header */
  fail_unless_equals_int (do_setup_full (conn, video_control,
          GST_RTSP_LOWER_TRANS_UDP, &client_ports, "funky-feature", &session,
          &video_transport, &unsupported), GST_RTSP_STS_OPTION_NOT_SUPPORTED);
  fail_unless_equals_string (unsupported, "funky-feature");
  g_free (unsupported);
  unsupported = NULL;

  /* send SETUP request for video, with multiple Require headers */
  fail_unless_equals_int (do_setup_full (conn, video_control,
          GST_RTSP_LOWER_TRANS_UDP, &client_ports,
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

  start_server (FALSE);

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
do_test_play_tcp_full (const gchar * range)
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
  gchar *range_out = NULL;
  GstRTSPLowerTrans lower_transport = GST_RTSP_LOWER_TRANS_TCP;

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);
  get_client_ports (&client_port);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  /* do SETUP for video and audio */
  fail_unless (do_setup_full (conn, video_control, lower_transport,
          &client_port, NULL, &session, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  fail_unless (do_setup_full (conn, audio_control, lower_transport,
          &client_port, NULL, &session, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_request (conn, GST_RTSP_PLAY, NULL, session, NULL, range,
          NULL, NULL, NULL, NULL, NULL, &range_out) == GST_RTSP_STS_OK);

  if (range)
    fail_unless_equals_string (range, range_out);
  g_free (range_out);

  {
    GstRTSPMessage *message;
    fail_unless (gst_rtsp_message_new (&message) == GST_RTSP_OK);
    fail_unless (gst_rtsp_connection_receive (conn, message,
            NULL) == GST_RTSP_OK);
    fail_unless (gst_rtsp_message_get_type (message) == GST_RTSP_MESSAGE_DATA);
    gst_rtsp_message_free (message);
  }

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* FIXME: The rtsp-server always disconnects the transport before
   * sending the RTCP BYE
   * receive_rtcp (rtcp_socket, NULL, GST_RTCP_TYPE_BYE);
   */

  /* clean up and iterate so the clean-up can finish */
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);
}

static void
do_test_play_full (const gchar * range, GstRTSPLowerTrans lower_transport,
    GMutex * lock)
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
  fail_unless (do_setup_full (conn, video_control, lower_transport,
          &client_port, NULL, &session, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  fail_unless (do_setup_full (conn, audio_control, lower_transport,
          &client_port, NULL, &session, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_request (conn, GST_RTSP_PLAY, NULL, session, NULL, range,
          NULL, NULL, NULL, NULL, NULL, &range_out) == GST_RTSP_STS_OK);
  if (range)
    fail_unless_equals_string (range, range_out);
  g_free (range_out);

  for (;;) {
    receive_rtp (rtp_socket, NULL);
    receive_rtcp (rtcp_socket, NULL, 0);

    if (lock != NULL) {
      if (g_mutex_trylock (lock) == TRUE) {
        g_mutex_unlock (lock);
        break;
      }
    } else {
      break;
    }

  }

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

static void
do_test_play (const gchar * range)
{
  do_test_play_full (range, GST_RTSP_LOWER_TRANS_UDP, NULL);
}

GST_START_TEST (test_play)
{
  start_server (FALSE);

  do_test_play (NULL);

  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_play_tcp)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_ports = { 0 };
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;

  start_tcp_server (FALSE);

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* send DESCRIBE request */
  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_ports);

  /* send SETUP request for the first media */
  fail_unless (do_setup_full (conn, video_control, GST_RTSP_LOWER_TRANS_TCP,
          &client_ports, NULL, &session, &video_transport,
          NULL) == GST_RTSP_STS_OK);

  /* check response from SETUP */
  fail_unless (video_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (video_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (video_transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP);
  fail_unless (video_transport->mode_play);
  gst_rtsp_transport_free (video_transport);

  /* send SETUP request for the second media */
  fail_unless (do_setup_full (conn, audio_control, GST_RTSP_LOWER_TRANS_TCP,
          &client_ports, NULL, &session, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

  /* check response from SETUP */
  fail_unless (audio_transport->trans == GST_RTSP_TRANS_RTP);
  fail_unless (audio_transport->profile == GST_RTSP_PROFILE_AVP);
  fail_unless (audio_transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP);
  fail_unless (audio_transport->mode_play);
  gst_rtsp_transport_free (audio_transport);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);

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

GST_START_TEST (test_play_without_session)
{
  GstRTSPConnection *conn;

  start_server (FALSE);

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

  start_server (FALSE);

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

  start_server (FALSE);

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

  start_server (FALSE);


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
  fail_unless (do_setup_full (conn, video_control, GST_RTSP_LOWER_TRANS_UDP,
          &client_port, NULL, &session, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  fail_unless (do_setup_full (conn, audio_control, GST_RTSP_LOWER_TRANS_UDP,
          &client_port, NULL, &session, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

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

  start_server (FALSE);


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

static void
new_connection_and_session_timeout_one (GstRTSPClient * client,
    GstRTSPSession * session, gpointer user_data)
{
  gint ps_timeout = 0;

  g_object_set (G_OBJECT (client), "post-session-timeout", 1, NULL);
  g_object_get (G_OBJECT (client), "post-session-timeout", &ps_timeout, NULL);
  fail_unless_equals_int (ps_timeout, 1);

  g_object_set (G_OBJECT (session), "extra-timeout", 0, NULL);
  gst_rtsp_session_set_timeout (session, 1);

  g_signal_handlers_disconnect_by_func (client,
      new_connection_and_session_timeout_one, user_data);
}

GST_START_TEST (test_play_timeout_connection)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPSessionPool *pool;
  GstRTSPThreadPool *thread_pool;
  GstRTSPMessage *request;
  GstRTSPMessage *response;

  thread_pool = gst_rtsp_server_get_thread_pool (server);
  g_object_unref (thread_pool);

  pool = gst_rtsp_server_get_session_pool (server);
  g_signal_connect (server, "client-connected",
      G_CALLBACK (session_connected_new_session_cb),
      new_connection_and_session_timeout_one);

  start_server (FALSE);


  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  gst_rtsp_connection_set_remember_session_id (conn, FALSE);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_port);

  /* do SETUP for video and audio */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);
  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 1);
  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);
  sleep (2);
  fail_unless (gst_rtsp_session_pool_cleanup (pool) == 1);
  sleep (3);

  request = create_request (conn, GST_RTSP_TEARDOWN, NULL);

  /* add headers */
  if (session) {
    gst_rtsp_message_add_header (request, GST_RTSP_HDR_SESSION, session);
  }

  /* send request */
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);

  iterate ();

  /* read response */
  response = read_response (conn);
  fail_unless (response == NULL);

  if (response) {
    gst_rtsp_message_free (response);
  }

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (pool);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);

  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_no_session_timeout)
{
  GstRTSPSession *session;
  gint64 now;
  gboolean is_expired;

  session = gst_rtsp_session_new ("test-session");
  gst_rtsp_session_set_timeout (session, 0);

  now = g_get_monotonic_time ();
  /* add more than the extra 5 seconds that are usually added in
   * gst_rtsp_session_next_timeout_usec */
  now += 7000000;

  is_expired = gst_rtsp_session_is_expired_usec (session, now);
  fail_unless (is_expired == FALSE);

  g_object_unref (session);
}

GST_END_TEST;

/* media contains two streams: video and audio but only one
 * stream is requested */
GST_START_TEST (test_play_one_active_stream)
{
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPSessionPool *pool;
  GstRTSPThreadPool *thread_pool;

  thread_pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_thread_pool_set_max_threads (thread_pool, 2);
  g_object_unref (thread_pool);

  pool = gst_rtsp_server_get_session_pool (server);
  g_signal_connect (server, "client-connected",
      G_CALLBACK (session_connected_new_session_cb), new_session_timeout_one);

  start_server (FALSE);

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  gst_rtsp_connection_set_remember_session_id (conn, FALSE);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports (&client_port);

  /* do SETUP for video only */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (pool) == 1);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_PLAY,
          session) == GST_RTSP_STS_OK);


  /* send TEARDOWN request */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (pool);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
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

  start_server (FALSE);

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
  /* we have to suspend media after SDP in order to make sure that
   * we can reconfigure UDP sink with new UDP ports */
  gst_rtsp_media_factory_set_suspend_mode (factory,
      GST_RTSP_SUSPEND_MODE_RESET);
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
  start_server (FALSE);

  do_test_play ("npt=5-");
  do_test_play ("smpte=0:00:00-");
  do_test_play ("smpte=1:00:00-");
  do_test_play ("smpte=1:00:03-");
  do_test_play ("clock=20120321T152256Z-");

  stop_server ();
  iterate ();
}

GST_END_TEST;

GST_START_TEST (test_play_smpte_range_tcp)
{
  start_tcp_server (FALSE);

  do_test_play_tcp_full ("npt=5-");
  do_test_play_tcp_full ("smpte=0:00:00-");
  do_test_play_tcp_full ("smpte=1:00:00-");
  do_test_play_tcp_full ("smpte=1:00:03-");
  do_test_play_tcp_full ("clock=20120321T152256Z-");

  stop_server ();
  iterate ();
}

GST_END_TEST;

static gpointer
thread_func_udp (gpointer data)
{
  do_test_play_full (NULL, GST_RTSP_LOWER_TRANS_UDP, (GMutex *) data);
  return NULL;
}

static gpointer
thread_func_tcp (gpointer data)
{
  do_test_play_tcp_full (NULL);
  return NULL;
}

static void
test_shared (gpointer (thread_func) (gpointer data))
{
  GMutex lock1, lock2, lock3, lock4;
  GThread *thread1, *thread2, *thread3, *thread4;

  /* Locks for each thread. Each thread will keep reading data as long as the
   * thread is locked. */
  g_mutex_init (&lock1);
  g_mutex_init (&lock2);
  g_mutex_init (&lock3);
  g_mutex_init (&lock4);

  if (thread_func == thread_func_tcp)
    start_tcp_server (TRUE);
  else
    start_server (TRUE);

  /* Start the first receiver thread. */
  g_mutex_lock (&lock1);
  thread1 = g_thread_new ("thread1", thread_func, &lock1);

  /* Connect and disconnect another client. */
  g_mutex_lock (&lock2);
  thread2 = g_thread_new ("thread2", thread_func, &lock2);
  g_mutex_unlock (&lock2);
  g_mutex_clear (&lock2);
  g_thread_join (thread2);

  /* Do it again. */
  g_mutex_lock (&lock3);
  thread3 = g_thread_new ("thread3", thread_func, &lock3);
  g_mutex_unlock (&lock3);
  g_mutex_clear (&lock3);
  g_thread_join (thread3);

  /* Disconnect the last client. This will clean up the media. */
  g_mutex_unlock (&lock1);
  g_mutex_clear (&lock1);
  g_thread_join (thread1);

  /* Connect and disconnect another client. This will create and clean up the 
   * media. */
  g_mutex_lock (&lock4);
  thread4 = g_thread_new ("thread4", thread_func, &lock4);
  g_mutex_unlock (&lock4);
  g_mutex_clear (&lock4);
  g_thread_join (thread4);

  stop_server ();
  iterate ();
}

/* Test adding and removing clients to a 'Shared' media.
 * CASE: unicast UDP */
GST_START_TEST (test_shared_udp)
{
  test_shared (thread_func_udp);
}

GST_END_TEST;

/* Test adding and removing clients to a 'Shared' media.
 * CASE: unicast TCP */
GST_START_TEST (test_shared_tcp)
{
  test_shared (thread_func_tcp);
}

GST_END_TEST;

GST_START_TEST (test_announce_without_sdp)
{
  GstRTSPConnection *conn;
  GstRTSPStatusCode status;
  GstRTSPMessage *request;
  GstRTSPMessage *response;

  start_record_server ("( fakesink name=depay0 )");

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  /* create and send ANNOUNCE request */
  request = create_request (conn, GST_RTSP_ANNOUNCE, NULL);

  fail_unless (send_request (conn, request));

  iterate ();

  response = read_response (conn);

  /* check response */
  gst_rtsp_message_parse_response (response, &status, NULL, NULL);
  fail_unless_equals_int (status, GST_RTSP_STS_BAD_REQUEST);
  gst_rtsp_message_free (response);

  /* try again, this type with content-type, but still no SDP */
  gst_rtsp_message_add_header (request, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  fail_unless (send_request (conn, request));

  iterate ();

  response = read_response (conn);

  /* check response */
  gst_rtsp_message_parse_response (response, &status, NULL, NULL);
  fail_unless_equals_int (status, GST_RTSP_STS_BAD_REQUEST);
  gst_rtsp_message_free (response);

  /* try again, this type with an unknown content-type */
  gst_rtsp_message_remove_header (request, GST_RTSP_HDR_CONTENT_TYPE, -1);
  gst_rtsp_message_add_header (request, GST_RTSP_HDR_CONTENT_TYPE,
      "application/x-something");

  fail_unless (send_request (conn, request));

  iterate ();

  response = read_response (conn);

  /* check response */
  gst_rtsp_message_parse_response (response, &status, NULL, NULL);
  fail_unless_equals_int (status, GST_RTSP_STS_BAD_REQUEST);
  gst_rtsp_message_free (response);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_message_free (request);
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
}

GST_END_TEST;

static GstRTSPStatusCode
do_announce (GstRTSPConnection * conn, GstSDPMessage * sdp)
{
  GstRTSPMessage *request;
  GstRTSPMessage *response;
  GstRTSPStatusCode code;
  gchar *str;

  /* create request */
  request = create_request (conn, GST_RTSP_ANNOUNCE, NULL);

  gst_rtsp_message_add_header (request, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  /* add SDP to the response body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (request, (guint8 *) str, strlen (str));
  gst_sdp_message_free (sdp);

  /* send request */
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);

  iterate ();

  /* read response */
  response = read_response (conn);

  /* check status line */
  gst_rtsp_message_parse_response (response, &code, NULL, NULL);

  gst_rtsp_message_free (response);
  return code;
}

static void
media_constructed_cb (GstRTSPMediaFactory * mfactory, GstRTSPMedia * media,
    gpointer user_data)
{
  GstElement **p_sink = user_data;
  GstElement *bin;

  bin = gst_rtsp_media_get_element (media);
  *p_sink = gst_bin_get_by_name (GST_BIN (bin), "sink");
  GST_INFO ("media constructed!: %" GST_PTR_FORMAT, *p_sink);
  gst_object_unref (bin);
}

#define RECORD_N_BUFS 10

GST_START_TEST (test_record_tcp)
{
  GstRTSPMediaFactory *mfactory;
  GstRTSPConnection *conn;
  GstRTSPStatusCode status;
  GstRTSPMessage *response;
  GstRTSPMessage *request;
  GstSDPMessage *sdp;
  GstRTSPResult rres;
  GSocketAddress *sa;
  GInetAddress *ia;
  GstElement *server_sink = NULL;
  GSocket *conn_socket;
  const gchar *proto;
  gchar *client_ip, *sess_id, *session = NULL;
  gint i;

  mfactory =
      start_record_server
      ("( rtppcmadepay name=depay0 ! appsink name=sink async=false )");

  g_signal_connect (mfactory, "media-constructed",
      G_CALLBACK (media_constructed_cb), &server_sink);

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  conn_socket = gst_rtsp_connection_get_read_socket (conn);

  sa = g_socket_get_local_address (conn_socket, NULL);
  ia = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (sa));
  client_ip = g_inet_address_to_string (ia);
  if (g_socket_address_get_family (sa) == G_SOCKET_FAMILY_IPV6)
    proto = "IP6";
  else if (g_socket_address_get_family (sa) == G_SOCKET_FAMILY_IPV4)
    proto = "IP4";
  else
    g_assert_not_reached ();
  g_object_unref (sa);

  gst_sdp_message_new (&sdp);

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");

  /* session ID doesn't have to be super-unique in this case */
  sess_id = g_strdup_printf ("%u", g_random_int ());
  gst_sdp_message_set_origin (sdp, "-", sess_id, "1", "IN", proto, client_ip);
  g_free (sess_id);
  g_free (client_ip);

  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtsp-server-test");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");

  /* add stream 0 */
  {
    GstSDPMedia *smedia;

    gst_sdp_media_new (&smedia);
    gst_sdp_media_set_media (smedia, "audio");
    gst_sdp_media_add_format (smedia, "8");     /* pcma/alaw */
    gst_sdp_media_set_port_info (smedia, 0, 1);
    gst_sdp_media_set_proto (smedia, "RTP/AVP");
    gst_sdp_media_add_attribute (smedia, "rtpmap", "8 PCMA/8000");
    gst_sdp_message_add_media (sdp, smedia);
    gst_sdp_media_free (smedia);
  }

  /* send ANNOUNCE request */
  status = do_announce (conn, sdp);
  fail_unless_equals_int (status, GST_RTSP_STS_OK);

  /* create and send SETUP request */
  request = create_request (conn, GST_RTSP_SETUP, NULL);
  gst_rtsp_message_add_header (request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;interleaved=0;mode=record");
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);
  iterate ();
  response = read_response (conn);
  gst_rtsp_message_parse_response (response, &status, NULL, NULL);
  fail_unless_equals_int (status, GST_RTSP_STS_OK);

  rres =
      gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION, &session, 0);
  session = g_strdup (session);
  fail_unless_equals_int (rres, GST_RTSP_OK);
  gst_rtsp_message_free (response);

  /* send RECORD */
  request = create_request (conn, GST_RTSP_RECORD, NULL);
  gst_rtsp_message_add_header (request, GST_RTSP_HDR_SESSION, session);
  fail_unless (send_request (conn, request));
  gst_rtsp_message_free (request);
  iterate ();
  response = read_response (conn);
  gst_rtsp_message_parse_response (response, &status, NULL, NULL);
  fail_unless_equals_int (status, GST_RTSP_STS_OK);
  gst_rtsp_message_free (response);

  /* send some data */
  {
    GstElement *pipeline, *src, *enc, *pay, *sink;

    pipeline = gst_pipeline_new ("send-pipeline");
    src = gst_element_factory_make ("audiotestsrc", NULL);
    g_object_set (src, "num-buffers", RECORD_N_BUFS,
        "samplesperbuffer", 1000, NULL);
    enc = gst_element_factory_make ("alawenc", NULL);
    pay = gst_element_factory_make ("rtppcmapay", NULL);
    sink = gst_element_factory_make ("appsink", NULL);
    fail_unless (pipeline && src && enc && pay && sink);
    gst_bin_add_many (GST_BIN (pipeline), src, enc, pay, sink, NULL);
    gst_element_link_many (src, enc, pay, sink, NULL);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    do {
      GstRTSPMessage *data_msg;
      GstMapInfo map = GST_MAP_INFO_INIT;
      GstRTSPResult rres;
      GstSample *sample = NULL;
      GstBuffer *buf;

      g_signal_emit_by_name (G_OBJECT (sink), "pull-sample", &sample);
      if (sample == NULL)
        break;
      buf = gst_sample_get_buffer (sample);
      rres = gst_rtsp_message_new_data (&data_msg, 0);
      fail_unless_equals_int (rres, GST_RTSP_OK);
      gst_buffer_map (buf, &map, GST_MAP_READ);
      GST_INFO ("sending %u bytes of data on channel 0", (guint) map.size);
      GST_MEMDUMP ("data on channel 0", map.data, map.size);
      rres = gst_rtsp_message_set_body (data_msg, map.data, map.size);
      fail_unless_equals_int (rres, GST_RTSP_OK);
      gst_buffer_unmap (buf, &map);
      rres = gst_rtsp_connection_send (conn, data_msg, NULL);
      fail_unless_equals_int (rres, GST_RTSP_OK);
      gst_rtsp_message_free (data_msg);
      gst_sample_unref (sample);
    } while (TRUE);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  /* check received data (we assume every buffer created by audiotestsrc and
   * subsequently encoded by mulawenc results in exactly one RTP packet) */
  for (i = 0; i < RECORD_N_BUFS; ++i) {
    GstSample *sample = NULL;

    g_signal_emit_by_name (G_OBJECT (server_sink), "pull-sample", &sample);
    GST_INFO ("%2d recv sample: %p", i, sample);
    gst_sample_unref (sample);
  }

  fail_unless_equals_int (GST_STATE (server_sink), GST_STATE_PLAYING);

  /* clean up and iterate so the clean-up can finish */
  gst_rtsp_connection_free (conn);
  stop_server ();
  iterate ();
  g_free (session);
  /* release the reference to server_sink, obtained in media_constructed_cb */
  gst_object_unref (server_sink);
}

GST_END_TEST;

static void
do_test_multiple_transports (GstRTSPLowerTrans trans1, GstRTSPLowerTrans trans2)
{
  GstRTSPConnection *conn1;
  GstRTSPConnection *conn2;
  GstSDPMessage *sdp_message1 = NULL;
  GstSDPMessage *sdp_message2 = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port1, client_port2;
  gchar *session1 = NULL;
  gchar *session2 = NULL;
  GstRTSPTransport *video_transport = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GSocket *rtp_socket, *rtcp_socket;

  conn1 = connect_to_server (test_port, TEST_MOUNT_POINT);
  conn2 = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message1 = do_describe (conn1, TEST_MOUNT_POINT);

  get_client_ports_full (&client_port1, &rtp_socket, &rtcp_socket);
  /* get control strings from DESCRIBE response */
  sdp_media = gst_sdp_message_get_media (sdp_message1, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message1, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  /* do SETUP for video and audio */
  fail_unless (do_setup_full (conn1, video_control, trans1,
          &client_port1, NULL, &session1, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  fail_unless (do_setup_full (conn1, audio_control, trans1,
          &client_port1, NULL, &session1, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);

  sdp_message2 = do_describe (conn2, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  sdp_media = gst_sdp_message_get_media (sdp_message2, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message2, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports_full (&client_port2, NULL, NULL);
  /* do SETUP for video and audio */
  fail_unless (do_setup_full (conn2, video_control, trans2,
          &client_port2, NULL, &session2, &video_transport,
          NULL) == GST_RTSP_STS_OK);
  fail_unless (do_setup_full (conn2, audio_control, trans2,
          &client_port2, NULL, &session2, &audio_transport,
          NULL) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_request (conn1, GST_RTSP_PLAY, NULL, session1, NULL, NULL,
          NULL, NULL, NULL, NULL, NULL, NULL) == GST_RTSP_STS_OK);
  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_request (conn2, GST_RTSP_PLAY, NULL, session2, NULL, NULL,
          NULL, NULL, NULL, NULL, NULL, NULL) == GST_RTSP_STS_OK);


  /* receive UDP data */
  receive_rtp (rtp_socket, NULL);
  receive_rtcp (rtcp_socket, NULL, 0);

  /* receive TCP data */
  {
    GstRTSPMessage *message;
    fail_unless (gst_rtsp_message_new (&message) == GST_RTSP_OK);
    fail_unless (gst_rtsp_connection_receive (conn2, message,
            NULL) == GST_RTSP_OK);
    fail_unless (gst_rtsp_message_get_type (message) == GST_RTSP_MESSAGE_DATA);
    gst_rtsp_message_free (message);
  }

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn1, GST_RTSP_TEARDOWN,
          session1) == GST_RTSP_STS_OK);
  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn2, GST_RTSP_TEARDOWN,
          session2) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (rtp_socket);
  g_object_unref (rtcp_socket);
  g_free (session1);
  g_free (session2);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message1);
  gst_sdp_message_free (sdp_message2);
  gst_rtsp_connection_free (conn1);
  gst_rtsp_connection_free (conn2);
}

GST_START_TEST (test_multiple_transports)
{
  start_server (TRUE);
  do_test_multiple_transports (GST_RTSP_LOWER_TRANS_UDP,
      GST_RTSP_LOWER_TRANS_TCP);
  stop_server ();
}

GST_END_TEST;

GST_START_TEST (test_suspend_mode_reset_only_audio)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GSocket *rtp_socket, *rtcp_socket;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_suspend_mode (factory,
      GST_RTSP_SUSPEND_MODE_RESET);
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

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports_full (&client_port, &rtp_socket, &rtcp_socket);

  /* do SETUP for audio */
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
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);

  stop_server ();
  iterate ();
}

GST_END_TEST;


static GstRTSPStatusCode
adjust_play_mode (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPTimeRange ** range, GstSeekFlags * flags, gdouble * rate,
    GstClockTime * trickmode_interval, gboolean * enable_rate_control)
{
  GstRTSPState rtspstate;

  rtspstate = gst_rtsp_session_media_get_rtsp_state (ctx->sessmedia);
  if (rtspstate == GST_RTSP_STATE_PLAYING) {
    if (!gst_rtsp_session_media_set_state (ctx->sessmedia, GST_STATE_PAUSED))
      return GST_RTSP_STS_INTERNAL_SERVER_ERROR;

    if (!gst_rtsp_media_unsuspend (ctx->media))
      return GST_RTSP_STS_INTERNAL_SERVER_ERROR;
  }

  return GST_RTSP_STS_OK;
}

GST_START_TEST (test_double_play)
{
  GstRTSPMountPoints *mounts;
  gchar *service;
  GstRTSPMediaFactory *factory;
  GstRTSPConnection *conn;
  GstSDPMessage *sdp_message = NULL;
  const GstSDPMedia *sdp_media;
  const gchar *video_control;
  const gchar *audio_control;
  GstRTSPRange client_port;
  gchar *session = NULL;
  GstRTSPTransport *audio_transport = NULL;
  GstRTSPTransport *video_transport = NULL;
  GSocket *rtp_socket, *rtcp_socket;
  GstRTSPClient *client;
  GstRTSPClientClass *klass;

  client = gst_rtsp_client_new ();
  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  klass->adjust_play_mode = adjust_play_mode;

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

  conn = connect_to_server (test_port, TEST_MOUNT_POINT);

  sdp_message = do_describe (conn, TEST_MOUNT_POINT);

  /* get control strings from DESCRIBE response */
  fail_unless (gst_sdp_message_medias_len (sdp_message) == 2);
  sdp_media = gst_sdp_message_get_media (sdp_message, 0);
  video_control = gst_sdp_media_get_attribute_val (sdp_media, "control");
  sdp_media = gst_sdp_message_get_media (sdp_message, 1);
  audio_control = gst_sdp_media_get_attribute_val (sdp_media, "control");

  get_client_ports_full (&client_port, &rtp_socket, &rtcp_socket);

  /* do SETUP for video */
  fail_unless (do_setup (conn, video_control, &client_port, &session,
          &video_transport) == GST_RTSP_STS_OK);

  /* do SETUP for audio */
  fail_unless (do_setup (conn, audio_control, &client_port, &session,
          &audio_transport) == GST_RTSP_STS_OK);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request_rangein (conn, GST_RTSP_PLAY,
          session, "npt=0-") == GST_RTSP_STS_OK);

  /* let it play for a while, so it needs to seek
   * for next play (npt=0-) */
  g_usleep (30000);

  /* send PLAY request and check that we get 200 OK */
  fail_unless (do_simple_request_rangein (conn, GST_RTSP_PLAY,
          session, "npt=0-") == GST_RTSP_STS_OK);

  /* send TEARDOWN request and check that we get 200 OK */
  fail_unless (do_simple_request (conn, GST_RTSP_TEARDOWN,
          session) == GST_RTSP_STS_OK);

  /* clean up and iterate so the clean-up can finish */
  g_object_unref (rtp_socket);
  g_object_unref (rtcp_socket);
  g_free (session);
  gst_rtsp_transport_free (video_transport);
  gst_rtsp_transport_free (audio_transport);
  gst_sdp_message_free (sdp_message);
  gst_rtsp_connection_free (conn);

  stop_server ();
  iterate ();
  g_object_unref (client);
}

GST_END_TEST;


static Suite *
rtspserver_suite (void)
{
  Suite *s = suite_create ("rtspserver");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_set_timeout (tc, 120);
  tcase_add_test (tc, test_connect);
  tcase_add_test (tc, test_describe);
  tcase_add_test (tc, test_describe_non_existing_mount_point);
  tcase_add_test (tc, test_describe_record_media);
  tcase_add_test (tc, test_setup_udp);
  tcase_add_test (tc, test_setup_tcp);
  tcase_add_test (tc, test_setup_udp_mcast);
  tcase_add_test (tc, test_setup_twice);
  tcase_add_test (tc, test_setup_with_require_header);
  tcase_add_test (tc, test_setup_non_existing_stream);
  tcase_add_test (tc, test_play);
  tcase_add_test (tc, test_play_tcp);
  tcase_add_test (tc, test_play_without_session);
  tcase_add_test (tc, test_bind_already_in_use);
  tcase_add_test (tc, test_play_multithreaded);
  tcase_add_test (tc, test_play_multithreaded_block_in_describe);
  tcase_add_test (tc, test_play_multithreaded_timeout_client);
  tcase_add_test (tc, test_play_multithreaded_timeout_session);
  tcase_add_test (tc, test_play_timeout_connection);
  tcase_add_test (tc, test_no_session_timeout);
  tcase_add_test (tc, test_play_one_active_stream);
  tcase_add_test (tc, test_play_disconnect);
  tcase_add_test (tc, test_play_specific_server_port);
  tcase_add_test (tc, test_play_smpte_range);
  tcase_add_test (tc, test_play_smpte_range_tcp);
  tcase_add_test (tc, test_shared_udp);
  tcase_add_test (tc, test_shared_tcp);
  tcase_add_test (tc, test_announce_without_sdp);
  tcase_add_test (tc, test_record_tcp);
  tcase_add_test (tc, test_multiple_transports);
  tcase_add_test (tc, test_suspend_mode_reset_only_audio);
  tcase_add_test (tc, test_double_play);

  return s;
}

GST_CHECK_MAIN (rtspserver);
