/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
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

#include <rtsp-client.h>

static gchar * session_id;
static gint cseq;
static guint expected_session_timeout = 60;

static gboolean
test_response_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static gboolean
test_response_400 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_BAD_REQUEST);
  fail_unless (g_str_equal (reason, "Bad Request"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static gboolean
test_response_404 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_NOT_FOUND);
  fail_unless (g_str_equal (reason, "Not Found"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static gboolean
test_response_454 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_SESSION_NOT_FOUND);
  fail_unless (g_str_equal (reason, "Session Not Found"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static GstRTSPClient *
setup_client (const gchar * launch_line)
{
  GstRTSPClient *client;
  GstRTSPSessionPool *session_pool;
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPThreadPool *thread_pool;

  client = gst_rtsp_client_new ();

  session_pool = gst_rtsp_session_pool_new ();
  gst_rtsp_client_set_session_pool (client, session_pool);

  mount_points = gst_rtsp_mount_points_new ();
  factory = gst_rtsp_media_factory_new ();
  if (launch_line == NULL)
    gst_rtsp_media_factory_set_launch (factory,
        "videotestsrc ! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96");
  else
    gst_rtsp_media_factory_set_launch (factory, launch_line);

  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  gst_rtsp_client_set_mount_points (client, mount_points);

  thread_pool = gst_rtsp_thread_pool_new ();
  gst_rtsp_client_set_thread_pool (client, thread_pool);

  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (thread_pool);

  return client;
}

static void
teardown_client (GstRTSPClient * client)
{
  gst_rtsp_client_set_thread_pool (client, NULL);
  g_object_unref (client);
}

GST_START_TEST (test_request)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPConnection *conn;
  GSocket *sock;
  GError *error = NULL;

  client = gst_rtsp_client_new ();

  /* OPTIONS with invalid url */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "foopy://padoop/") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_400, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);

  gst_rtsp_message_unset (&request);

  /* OPTIONS with unknown session id */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, "foobar");

  gst_rtsp_client_set_send_func (client, test_response_454, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);

  gst_rtsp_message_unset (&request);

  /* OPTIONS with an absolute path instead of an absolute url */
  /* set host information */
  sock = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_TCP, &error);
  g_assert_no_error (error);
  gst_rtsp_connection_create_from_socket (sock, "localhost", 444, NULL, &conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));
  g_object_unref (sock);

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* OPTIONS with an absolute path instead of an absolute url with invalid
   * host information */
  g_object_unref (client);
  client = gst_rtsp_client_new ();
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_400, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_object_unref (client);
}

GST_END_TEST;

static gboolean
test_option_response_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  GstRTSPMethod methods;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_PUBLIC, &str,
          0) == GST_RTSP_OK);

  methods = gst_rtsp_options_from_text (str);
  fail_if (methods == 0);
  fail_unless (methods == (GST_RTSP_DESCRIBE |
          GST_RTSP_OPTIONS |
          GST_RTSP_PAUSE |
          GST_RTSP_PLAY |
          GST_RTSP_SETUP |
          GST_RTSP_GET_PARAMETER | GST_RTSP_SET_PARAMETER | GST_RTSP_TEARDOWN));

  return TRUE;
}

GST_START_TEST (test_options)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = gst_rtsp_client_new ();

  /* simple OPTIONS */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_option_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_object_unref (client);
}

GST_END_TEST;

GST_START_TEST (test_describe)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = gst_rtsp_client_new ();

  /* simple DESCRIBE for non-existing url */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_404, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_object_unref (client);

  /* simple DESCRIBE for an existing url */
  client = setup_client (NULL);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  teardown_client (client);
}

GST_END_TEST;

static const gchar *expected_transport = NULL;;

static gboolean
test_setup_response_200_multicast (GstRTSPClient * client,
    GstRTSPMessage * response, gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPSession *session;
  gchar **session_hdr_params;

  fail_unless (expected_transport != NULL);

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_TRANSPORT,
          &str, 0) == GST_RTSP_OK);

  fail_unless (!strcmp (str, expected_transport));

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION,
          &str, 0) == GST_RTSP_OK);
  session_hdr_params = g_strsplit (str, ";", -1);

  /* session-id value */
  fail_unless (session_hdr_params[0] != NULL);

  if (expected_session_timeout != 60) {
    /* session timeout param */
    gchar *timeout_str = g_strdup_printf ("timeout=%u",
        expected_session_timeout);

    fail_unless (session_hdr_params[1] != NULL);
    g_strstrip (session_hdr_params[1]);
    fail_unless (g_strcmp0 (session_hdr_params[1], timeout_str) == 0);
    g_free (timeout_str);
  }

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 1);
  session = gst_rtsp_session_pool_find (session_pool, session_hdr_params[0]);
  g_strfreev (session_hdr_params);

  /* remember session id to be able to send teardown */
  session_id = g_strdup (gst_rtsp_session_get_sessionid (session));
  fail_unless (session_id != NULL);

  fail_unless (session != NULL);
  g_object_unref (session);

  g_object_unref (session_pool);


  return TRUE;
}

static gboolean
test_teardown_response_200 (GstRTSPClient * client,
    GstRTSPMessage * response, gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static void
send_teardown (GstRTSPClient * client)
{
  GstRTSPMessage request = { 0, };
  gchar *str;

  fail_unless (session_id != NULL);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_TEARDOWN,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION,
      session_id);
  gst_rtsp_client_set_send_func (client, test_teardown_response_200,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  g_free (session_id);
  session_id = NULL;
}

static GstRTSPClient *
setup_multicast_client (void)
{
  GstRTSPClient *client;
  GstRTSPSessionPool *session_pool;
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *address_pool;
  GstRTSPThreadPool *thread_pool;

  client = gst_rtsp_client_new ();

  session_pool = gst_rtsp_session_pool_new ();
  gst_rtsp_client_set_session_pool (client, session_pool);

  mount_points = gst_rtsp_mount_points_new ();
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory,
      "audiotestsrc ! audio/x-raw,rate=44100 ! audioconvert ! rtpL16pay name=pay0");
  address_pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (address_pool,
          "233.252.0.1", "233.252.0.1", 5000, 5010, 1));
  gst_rtsp_media_factory_set_address_pool (factory, address_pool);
  gst_rtsp_media_factory_add_role (factory, "user",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  gst_rtsp_client_set_mount_points (client, mount_points);

  thread_pool = gst_rtsp_thread_pool_new ();
  gst_rtsp_client_set_thread_pool (client, thread_pool);

  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (address_pool);
  g_object_unref (thread_pool);

  return client;
}

GST_START_TEST (test_client_multicast_transport_404)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_multicast_client ();

  /* simple SETUP for non-existing url */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test2/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast");

  gst_rtsp_client_set_send_func (client, test_response_404, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  teardown_client (client);
}

GST_END_TEST;

static void
new_session_cb (GObject * client, GstRTSPSession * session, gpointer user_data)
{
  GST_DEBUG ("%p: new session %p", client, session);
  gst_rtsp_session_set_timeout (session, expected_session_timeout);
}

GST_START_TEST (test_client_multicast_transport)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_multicast_client ();

  expected_session_timeout = 20;
  g_signal_connect (G_OBJECT (client), "new-session",
      G_CALLBACK (new_session_cb), NULL);

  /* simple SETUP with a valid URI and multicast */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast");

  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  gst_rtsp_client_set_send_func (client, test_setup_response_200_multicast,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;
  expected_session_timeout = 60;

  send_teardown (client);

  teardown_client (client);
}

GST_END_TEST;

GST_START_TEST (test_client_multicast_ignore_transport_specific)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_multicast_client ();

  /* simple SETUP with a valid URI and multicast and a specific dest,
   * but ignore it  */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast;destination=233.252.0.2;ttl=2;port=5001-5006;");

  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  gst_rtsp_client_set_send_func (client, test_setup_response_200_multicast,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  send_teardown (client);

  teardown_client (client);
}

GST_END_TEST;

static gboolean
test_setup_response_461 (GstRTSPClient * client,
    GstRTSPMessage * response, gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;

  fail_unless (expected_transport == NULL);

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_UNSUPPORTED_TRANSPORT);
  fail_unless (g_str_equal (reason, "Unsupported transport"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);


  return TRUE;
}

GST_START_TEST (test_client_multicast_invalid_transport_specific)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client ();

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  /* simple SETUP with a valid URI and multicast, but an invalid ip */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast;destination=233.252.0.2;ttl=1;port=5000-5001;");

  gst_rtsp_client_set_send_func (client, test_setup_response_461, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  /* FIXME: There seems to be a leak of a session here ! */
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0);
  g_object_unref (session_pool);



  /* simple SETUP with a valid URI and multicast, but an invalid prt */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast;destination=233.252.0.1;ttl=1;port=6000-6001;");

  gst_rtsp_client_set_send_func (client, test_setup_response_461, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  /* FIXME: There seems to be a leak of a session here ! */
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0);
  g_object_unref (session_pool);



  /* simple SETUP with a valid URI and multicast, but an invalid ttl */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast;destination=233.252.0.1;ttl=2;port=5000-5001;");

  gst_rtsp_client_set_send_func (client, test_setup_response_461, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  /* FIXME: There seems to be a leak of a session here ! */
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0);
  g_object_unref (session_pool);


  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);
}

GST_END_TEST;

GST_START_TEST (test_client_multicast_transport_specific)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client ();

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";

  /* simple SETUP with a valid URI and multicast, but an invalid ip */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      expected_transport);

  gst_rtsp_client_set_send_func (client, test_setup_response_200_multicast,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  gst_rtsp_client_set_send_func (client, test_setup_response_200_multicast,
      NULL, NULL);
  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 1);
  g_object_unref (session_pool);

  send_teardown (client);

  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);
}

GST_END_TEST;

static gboolean
test_response_sdp (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  guint8 *data;
  guint size;
  GstSDPMessage *sdp_msg;
  const GstSDPMedia *sdp_media;
  const GstSDPBandwidth *bw;
  gint bandwidth_val = GPOINTER_TO_INT (user_data);

  fail_unless (gst_rtsp_message_get_body (response, &data, &size)
      == GST_RTSP_OK);
  gst_sdp_message_new (&sdp_msg);
  fail_unless (gst_sdp_message_parse_buffer (data, size, sdp_msg)
      == GST_SDP_OK);

  /* session description */
  /* v= */
  fail_unless (gst_sdp_message_get_version (sdp_msg) != NULL);
  /* o= */
  fail_unless (gst_sdp_message_get_origin (sdp_msg) != NULL);
  /* s= */
  fail_unless (gst_sdp_message_get_session_name (sdp_msg) != NULL);
  /* t=0 0 */
  fail_unless (gst_sdp_message_times_len (sdp_msg) == 0);

  /* verify number of medias */
  fail_unless (gst_sdp_message_medias_len (sdp_msg) == 1);

  /* media description */
  sdp_media = gst_sdp_message_get_media (sdp_msg, 0);
  fail_unless (sdp_media != NULL);

  /* m= */
  fail_unless (gst_sdp_media_get_media (sdp_media) != NULL);

  /* media bandwidth */
  if (bandwidth_val) {
    fail_unless (gst_sdp_media_bandwidths_len (sdp_media) == 1);
    bw = gst_sdp_media_get_bandwidth (sdp_media, 0);
    fail_unless (bw != NULL);
    fail_unless (g_strcmp0 (bw->bwtype, "AS") == 0);
    fail_unless (bw->bandwidth == bandwidth_val);
  } else {
    fail_unless (gst_sdp_media_bandwidths_len (sdp_media) == 0);
  }

  gst_sdp_message_free (sdp_msg);

  return TRUE;
}

static void
test_client_sdp (const gchar * launch_line, guint * bandwidth_val)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  /* simple DESCRIBE for an existing url */
  client = setup_client (launch_line);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_sdp,
      (gpointer) bandwidth_val, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  teardown_client (client);
}

GST_START_TEST (test_client_sdp_with_max_bitrate_tag)
{
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"maximum-bitrate=(uint)50000000\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (50000));


  /* max-bitrate=0: no bandwidth line */
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"maximum-bitrate=(uint)0\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (0));
}

GST_END_TEST;

GST_START_TEST (test_client_sdp_with_bitrate_tag)
{
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"bitrate=(uint)7000000\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (7000));

  /* bitrate=0: no bandwdith line */
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"bitrate=(uint)0\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (0));
}

GST_END_TEST;

GST_START_TEST (test_client_sdp_with_max_bitrate_and_bitrate_tags)
{
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"bitrate=(uint)7000000,maximum-bitrate=(uint)50000000\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (50000));

  /* max-bitrate is zero: fallback to bitrate */
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"bitrate=(uint)7000000,maximum-bitrate=(uint)0\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (7000));

  /* max-bitrate=bitrate=0o: no bandwidth line */
  test_client_sdp ("videotestsrc "
      "! taginject tags=\"bitrate=(uint)0,maximum-bitrate=(uint)0\" "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96",
      GUINT_TO_POINTER (0));
}

GST_END_TEST;

GST_START_TEST (test_client_sdp_with_no_bitrate_tags)
{
  test_client_sdp ("videotestsrc "
      "! video/x-raw,width=352,height=288 ! rtpgstpay name=pay0 pt=96", NULL);
}

GST_END_TEST;

static Suite *
rtspclient_suite (void)
{
  Suite *s = suite_create ("rtspclient");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_request);
  tcase_add_test (tc, test_options);
  tcase_add_test (tc, test_describe);
  tcase_add_test (tc, test_client_multicast_transport_404);
  tcase_add_test (tc, test_client_multicast_transport);
  tcase_add_test (tc, test_client_multicast_ignore_transport_specific);
  tcase_add_test (tc, test_client_multicast_invalid_transport_specific);
  tcase_add_test (tc, test_client_multicast_transport_specific);
  tcase_add_test (tc, test_client_sdp_with_max_bitrate_tag);
  tcase_add_test (tc, test_client_sdp_with_bitrate_tag);
  tcase_add_test (tc, test_client_sdp_with_max_bitrate_and_bitrate_tags);
  tcase_add_test (tc, test_client_sdp_with_no_bitrate_tags);

  return s;
}

GST_CHECK_MAIN (rtspclient);
