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

#define VIDEO_PIPELINE "videotestsrc ! " \
  "video/x-raw,width=352,height=288 ! " \
  "rtpgstpay name=pay0 pt=96"
#define AUDIO_PIPELINE "audiotestsrc ! " \
  "audio/x-raw,rate=8000 ! " \
  "rtpgstpay name=pay1 pt=97"

static gchar *session_id;
static gint cseq;
static guint expected_session_timeout = 60;
static const gchar *expected_unsupported_header;
static const gchar *expected_scale_header;
static const gchar *expected_speed_header;
static gdouble fake_rate_value = 0;
static gdouble fake_applied_rate_value = 0;

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
test_response_play_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  gchar **session_hdr_params;
  gchar *pattern;

  fail_unless_equals_int (gst_rtsp_message_get_type (response),
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless_equals_int (code, GST_RTSP_STS_OK);
  fail_unless_equals_string (reason, "OK");
  fail_unless_equals_int (version, GST_RTSP_VERSION_1_0);

  /* Verify mandatory headers according to RFC 2326 */
  /* verify mandatory CSeq header */
  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);

  /* verify mandatory Session header */
  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION,
          &str, 0) == GST_RTSP_OK);
  session_hdr_params = g_strsplit (str, ";", -1);
  fail_unless (session_hdr_params[0] != NULL);
  g_strfreev (session_hdr_params);

  /* verify mandatory RTP-Info header */
  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_RTP_INFO,
          &str, 0) == GST_RTSP_OK);
  pattern = g_strdup_printf ("^url=rtsp://.+;seq=[0-9]+;rtptime=[0-9]+");
  fail_unless (g_regex_match_simple (pattern, str, 0, 0),
      "GST_RTSP_HDR_RTP_INFO '%s' doesn't match pattern '%s'", str, pattern);
  g_free (pattern);

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

static gboolean
test_response_551 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *options;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OPTION_NOT_SUPPORTED);
  fail_unless (g_str_equal (reason, "Option not supported"));
  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_UNSUPPORTED,
          &options, 0) == GST_RTSP_OK);
  fail_unless (!g_strcmp0 (expected_unsupported_header, options));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static void
create_connection (GstRTSPConnection ** conn)
{
  GSocket *sock;
  GError *error = NULL;

  sock = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_TCP, &error);
  g_assert_no_error (error);
  fail_unless (gst_rtsp_connection_create_from_socket (sock, "127.0.0.1", 444,
          NULL, conn) == GST_RTSP_OK);
  g_object_unref (sock);
}

static GstRTSPClient *
setup_client (const gchar * launch_line, const gchar * mount_point,
    gboolean enable_rtcp)
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
        "( " VIDEO_PIPELINE "  " AUDIO_PIPELINE " )");
  else
    gst_rtsp_media_factory_set_launch (factory, launch_line);

  gst_rtsp_media_factory_set_enable_rtcp (factory, enable_rtcp);

  gst_rtsp_mount_points_add_factory (mount_points, mount_point, factory);
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

static gchar *
check_requirements_cb (GstRTSPClient * client, GstRTSPContext * ctx,
    gchar ** req, gpointer user_data)
{
  int index = 0;
  GString *result = g_string_new ("");

  while (req[index] != NULL) {
    if (g_strcmp0 (req[index], "test-requirements")) {
      if (result->len > 0)
        g_string_append (result, ", ");
      g_string_append (result, req[index]);
    }
    index++;
  }

  return g_string_free (result, FALSE);
}

GST_START_TEST (test_require)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = gst_rtsp_client_new ();

  /* require header without handler */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("test-not-supported1");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);

  expected_unsupported_header = "test-not-supported1";
  gst_rtsp_client_set_send_func (client, test_response_551, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_signal_connect (G_OBJECT (client), "check-requirements",
      G_CALLBACK (check_requirements_cb), NULL);

  /* one supported option */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("test-requirements");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* unsupported option */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("test-not-supported1");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);

  expected_unsupported_header = "test-not-supported1";
  gst_rtsp_client_set_send_func (client, test_response_551, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* more than one unsupported options */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("test-not-supported1");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);
  str = g_strdup_printf ("test-not-supported2");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);

  expected_unsupported_header = "test-not-supported1, test-not-supported2";
  gst_rtsp_client_set_send_func (client, test_response_551, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* supported and unsupported together */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("test-not-supported1");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);
  str = g_strdup_printf ("test-requirements");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);
  str = g_strdup_printf ("test-not-supported2");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, str);
  g_free (str);

  expected_unsupported_header = "test-not-supported1, test-not-supported2";
  gst_rtsp_client_set_send_func (client, test_response_551, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_object_unref (client);
}

GST_END_TEST;

GST_START_TEST (test_request)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPConnection *conn;

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
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));
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
          GST_RTSP_ANNOUNCE |
          GST_RTSP_OPTIONS |
          GST_RTSP_PAUSE |
          GST_RTSP_PLAY |
          GST_RTSP_RECORD |
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

static void
test_describe_sub (const gchar * mount_point, const gchar * url)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = gst_rtsp_client_new ();

  /* simple DESCRIBE for non-existing url */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          url) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_404, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_object_unref (client);

  /* simple DESCRIBE for an existing url */
  client = setup_client (NULL, mount_point, TRUE);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          url) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  teardown_client (client);
}

GST_START_TEST (test_describe)
{
  test_describe_sub ("/test", "rtsp://localhost/test");
}

GST_END_TEST;

GST_START_TEST (test_describe_root_mount_point)
{
  test_describe_sub ("/", "rtsp://localhost");
}

GST_END_TEST;

static const gchar *expected_transport = NULL;

static gboolean
test_setup_response_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  gchar *pattern;
  GstRTSPSessionPool *session_pool;
  GstRTSPSession *session;
  gchar **session_hdr_params;

  fail_unless (expected_transport != NULL);

  fail_unless_equals_int (gst_rtsp_message_get_type (response),
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless_equals_int (code, GST_RTSP_STS_OK);
  fail_unless_equals_string (reason, "OK");
  fail_unless_equals_int (version, GST_RTSP_VERSION_1_0);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_TRANSPORT,
          &str, 0) == GST_RTSP_OK);

  pattern = g_strdup_printf ("^%s$", expected_transport);
  fail_unless (g_regex_match_simple (pattern, str, 0, 0),
      "Transport '%s' doesn't match pattern '%s'", str, pattern);
  g_free (pattern);

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

  session = gst_rtsp_session_pool_find (session_pool, session_hdr_params[0]);
  g_strfreev (session_hdr_params);

  /* remember session id to be able to send teardown */
  if (session_id)
    g_free (session_id);
  session_id = g_strdup (gst_rtsp_session_get_sessionid (session));
  fail_unless (session_id != NULL);

  fail_unless (session != NULL);
  g_object_unref (session);

  g_object_unref (session_pool);


  return TRUE;
}

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
send_teardown (GstRTSPClient * client, const gchar * url)
{
  GstRTSPMessage request = { 0, };
  gchar *str;

  fail_unless (session_id != NULL);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_TEARDOWN,
          url) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client, test_teardown_response_200,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  g_free (session_id);
  session_id = NULL;
}

static void
test_setup_tcp_sub (const gchar * mount_point, const gchar * url1,
    const gchar * url2)
{
  GstRTSPClient *client;
  GstRTSPConnection *conn;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL, mount_point, TRUE);
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          url1) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;unicast");

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  expected_transport =
      "RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=.*;mode=\"PLAY\"";
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);

  gst_rtsp_message_unset (&request);

  send_teardown (client, url2);
  teardown_client (client);
}

GST_START_TEST (test_setup_tcp)
{
  test_setup_tcp_sub ("/test", "rtsp://localhost/test/stream=0",
      "rtsp://localhost/test");
}

GST_END_TEST;

GST_START_TEST (test_setup_tcp_root_mount_point)
{
  test_setup_tcp_sub ("/", "rtsp://localhost/stream=0", "rtsp://localhost");
}

GST_END_TEST;

GST_START_TEST (test_setup_no_rtcp)
{
  GstRTSPClient *client;
  GstRTSPConnection *conn;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL, "/test", FALSE);
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;unicast;client_port=5000-5001");

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  /* We want to verify that server_port holds a single number, not a range */
  expected_transport =
      "RTP/AVP;unicast;client_port=5000-5001;server_port=[0-9]+;ssrc=.*;mode=\"PLAY\"";
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);

  gst_rtsp_message_unset (&request);

  send_teardown (client, "rtsp://localhost/test");
  teardown_client (client);
}

GST_END_TEST;

static void
test_setup_tcp_two_streams_same_channels_sub (const gchar * mount_point,
    const gchar * url1, const gchar * url2, const gchar * url3)
{
  GstRTSPClient *client;
  GstRTSPConnection *conn;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL, mount_point, TRUE);
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));

  /* test SETUP of a video stream with 0-1 as interleaved channels */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          url1) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;unicast;interleaved=0-1");
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  expected_transport =
      "RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=.*;mode=\"PLAY\"";
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* test SETUP of an audio stream with *the same* interleaved channels.
   * we expect the server to allocate new channel numbers */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          url2) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;unicast;interleaved=0-1");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  expected_transport =
      "RTP/AVP/TCP;unicast;interleaved=2-3;ssrc=.*;mode=\"PLAY\"";
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  send_teardown (client, url3);
  teardown_client (client);
}

GST_START_TEST (test_setup_tcp_two_streams_same_channels)
{
  test_setup_tcp_two_streams_same_channels_sub ("/test",
      "rtsp://localhost/test/stream=0", "rtsp://localhost/test/stream=1",
      "rtsp://localhost/test");
}

GST_END_TEST;

GST_START_TEST (test_setup_tcp_two_streams_same_channels_root_mount_point)
{
  test_setup_tcp_two_streams_same_channels_sub ("/",
      "rtsp://localhost/stream=0", "rtsp://localhost/stream=1",
      "rtsp://localhost");
}

GST_END_TEST;

static GstRTSPClient *
setup_multicast_client (guint max_ttl, const gchar * mount_point)
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
  gst_rtsp_mount_points_add_factory (mount_points, mount_point, factory);
  gst_rtsp_client_set_mount_points (client, mount_points);
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, max_ttl);

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

  client = setup_multicast_client (1, "/test");

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

  client = setup_multicast_client (1, "/test");

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
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;
  expected_session_timeout = 60;

  send_teardown (client, "rtsp://localhost/test");

  teardown_client (client);
}

GST_END_TEST;

GST_START_TEST (test_client_multicast_ignore_transport_specific)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_multicast_client (1, "/test");

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
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  send_teardown (client, "rtsp://localhost/test");

  teardown_client (client);
}

GST_END_TEST;

static void
multicast_transport_specific (void)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client (1, "/test");

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  /* simple SETUP with a valid URI */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      expected_transport);

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 1);
  g_object_unref (session_pool);

  /* send PLAY request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  send_teardown (client, "rtsp://localhost/test");
  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);
}

/* CASE: multicast address requested by the client exists in the address pool */
GST_START_TEST (test_client_multicast_transport_specific)
{
  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  multicast_transport_specific ();
  expected_transport = NULL;
}

GST_END_TEST;

/* CASE: multicast address requested by the client does not exist in the address pool */
GST_START_TEST (test_client_multicast_transport_specific_no_address_in_pool)
{
  expected_transport = "RTP/AVP;multicast;destination=234.252.0.3;"
      "ttl=1;port=10002-10004;mode=\"PLAY\"";
  multicast_transport_specific ();
  expected_transport = NULL;
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
  client = setup_client (launch_line, "/test", TRUE);
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

static void
mcast_transport_two_clients (gboolean shared, const gchar * transport1,
    const gchar * expected_transport1, const gchar * addr1,
    const gchar * transport2, const gchar * expected_transport2,
    const gchar * addr2, gboolean bind_mcast_address)
{
  GstRTSPClient *client1, *client2;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };
  GstRTSPContext ctx2 = { NULL };
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *address_pool;
  GstRTSPThreadPool *thread_pool;
  gchar *session_id1;
  gchar *client_addr = NULL;

  mount_points = gst_rtsp_mount_points_new ();
  factory = gst_rtsp_media_factory_new ();
  if (shared)
    gst_rtsp_media_factory_set_shared (factory, TRUE);
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, 5);
  gst_rtsp_media_factory_set_bind_mcast_address (factory, bind_mcast_address);
  gst_rtsp_media_factory_set_launch (factory,
      "audiotestsrc ! audio/x-raw,rate=44100 ! audioconvert ! rtpL16pay name=pay0");
  address_pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (address_pool,
          "233.252.0.1", "233.252.0.1", 5000, 5001, 1));
  gst_rtsp_media_factory_set_address_pool (factory, address_pool);
  gst_rtsp_media_factory_add_role (factory, "user",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  session_pool = gst_rtsp_session_pool_new ();
  thread_pool = gst_rtsp_thread_pool_new ();

  /* first multicast client with transport specific request */
  client1 = gst_rtsp_client_new ();
  gst_rtsp_client_set_session_pool (client1, session_pool);
  gst_rtsp_client_set_mount_points (client1, mount_points);
  gst_rtsp_client_set_thread_pool (client1, thread_pool);

  ctx.client = client1;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  expected_transport = expected_transport1;

  /* send SETUP request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT, transport1);

  gst_rtsp_client_set_send_func (client1, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client1,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  /* send PLAY request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client1, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client1,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* check address */
  client_addr = gst_rtsp_stream_get_multicast_client_addresses (ctx.stream);
  fail_if (client_addr == NULL);
  fail_unless (g_str_equal (client_addr, addr1));
  g_free (client_addr);

  gst_rtsp_context_pop_current (&ctx);
  session_id1 = g_strdup (session_id);

  /* second multicast client with transport specific request */
  cseq = 0;
  client2 = gst_rtsp_client_new ();
  gst_rtsp_client_set_session_pool (client2, session_pool);
  gst_rtsp_client_set_mount_points (client2, mount_points);
  gst_rtsp_client_set_thread_pool (client2, thread_pool);

  ctx2.client = client2;
  ctx2.auth = gst_rtsp_auth_new ();
  ctx2.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx2);

  expected_transport = expected_transport2;

  /* send SETUP request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT, transport2);

  gst_rtsp_client_set_send_func (client2, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client2,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  /* send PLAY request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client2, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client2,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* check addresses */
  client_addr = gst_rtsp_stream_get_multicast_client_addresses (ctx2.stream);
  fail_if (client_addr == NULL);
  if (shared) {
    if (g_str_equal (addr1, addr2)) {
      fail_unless (g_str_equal (client_addr, addr1));
    } else {
      gchar *addr_str = g_strdup_printf ("%s,%s", addr2, addr1);
      fail_unless (g_str_equal (client_addr, addr_str));
      g_free (addr_str);
    }
  } else {
    fail_unless (g_str_equal (client_addr, addr2));
  }
  g_free (client_addr);

  send_teardown (client2, "rtsp://localhost/test");
  gst_rtsp_context_pop_current (&ctx2);

  gst_rtsp_context_push_current (&ctx);
  session_id = session_id1;
  send_teardown (client1, "rtsp://localhost/test");
  gst_rtsp_context_pop_current (&ctx);

  teardown_client (client1);
  teardown_client (client2);
  g_object_unref (ctx.auth);
  g_object_unref (ctx2.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_token_unref (ctx2.token);
  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (address_pool);
  g_object_unref (thread_pool);
}

/* CASE: media is shared.
 * client 1: SETUP    --->
 * client 1: PLAY     --->
 * client 2: SETUP    --->
 * client 1: TEARDOWN --->
 * client 2: PLAY     --->
 * client 2: TEARDOWN --->
 */
static void
mcast_transport_two_clients_teardown_play (const gchar * transport1,
    const gchar * expected_transport1, const gchar * transport2,
    const gchar * expected_transport2, gboolean bind_mcast_address,
    gboolean is_shared)
{
  GstRTSPClient *client1, *client2;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };
  GstRTSPContext ctx2 = { NULL };
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *address_pool;
  GstRTSPThreadPool *thread_pool;
  gchar *session_id1, *session_id2;

  mount_points = gst_rtsp_mount_points_new ();
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, is_shared);
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, 5);
  gst_rtsp_media_factory_set_bind_mcast_address (factory, bind_mcast_address);
  gst_rtsp_media_factory_set_launch (factory,
      "audiotestsrc ! audio/x-raw,rate=44100 ! audioconvert ! rtpL16pay name=pay0");
  address_pool = gst_rtsp_address_pool_new ();
  if (is_shared)
    fail_unless (gst_rtsp_address_pool_add_range (address_pool,
            "233.252.0.1", "233.252.0.1", 5000, 5001, 1));
  else
    fail_unless (gst_rtsp_address_pool_add_range (address_pool,
            "233.252.0.1", "233.252.0.1", 5000, 5003, 1));
  gst_rtsp_media_factory_set_address_pool (factory, address_pool);
  gst_rtsp_media_factory_add_role (factory, "user",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  session_pool = gst_rtsp_session_pool_new ();
  thread_pool = gst_rtsp_thread_pool_new ();

  /* client 1 configuration */
  client1 = gst_rtsp_client_new ();
  gst_rtsp_client_set_session_pool (client1, session_pool);
  gst_rtsp_client_set_mount_points (client1, mount_points);
  gst_rtsp_client_set_thread_pool (client1, thread_pool);

  ctx.client = client1;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  expected_transport = expected_transport1;

  /* client 1 sends SETUP request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT, transport1);

  gst_rtsp_client_set_send_func (client1, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client1,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;


  /* client 1 sends PLAY request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client1, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client1,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  gst_rtsp_context_pop_current (&ctx);
  session_id1 = g_strdup (session_id);

  /* client 2 configuration */
  cseq = 0;
  client2 = gst_rtsp_client_new ();
  gst_rtsp_client_set_session_pool (client2, session_pool);
  gst_rtsp_client_set_mount_points (client2, mount_points);
  gst_rtsp_client_set_thread_pool (client2, thread_pool);

  ctx2.client = client2;
  ctx2.auth = gst_rtsp_auth_new ();
  ctx2.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx2);

  expected_transport = expected_transport2;

  /* client 2 sends SETUP request */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT, transport2);

  gst_rtsp_client_set_send_func (client2, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client2,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  session_id2 = g_strdup (session_id);
  g_free (session_id);
  gst_rtsp_context_pop_current (&ctx2);

  /* the first client sends TEARDOWN request */
  gst_rtsp_context_push_current (&ctx);
  session_id = session_id1;
  send_teardown (client1, "rtsp://localhost/test");
  gst_rtsp_context_pop_current (&ctx);
  teardown_client (client1);

  /* the second client sends PLAY request */
  gst_rtsp_context_push_current (&ctx2);
  session_id = session_id2;
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client2, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client2,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  /* client 2 sends TEARDOWN request */
  send_teardown (client2, "rtsp://localhost/test");
  gst_rtsp_context_pop_current (&ctx2);

  teardown_client (client2);
  g_object_unref (ctx.auth);
  g_object_unref (ctx2.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_token_unref (ctx2.token);
  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (address_pool);
  g_object_unref (thread_pool);
}

/* test if two multicast clients can choose different transport settings
 * CASE: media is shared */
GST_START_TEST
    (test_client_multicast_transport_specific_two_clients_shared_media) {
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=5002-5003;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:5002";

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if two multicast clients can choose different transport settings
 * CASE: media is not shared */
GST_START_TEST (test_client_multicast_transport_specific_two_clients)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=5002-5003;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:5002";

  mcast_transport_two_clients (FALSE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if two multicast clients can choose the same ports but different
 * multicast destinations
 * CASE: media is not shared */
GST_START_TEST (test_client_multicast_transport_specific_two_clients_same_ports)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=9000-9001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:9000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=9000-9001;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:9000";

  /* configure the multicast socket to be bound to the requested multicast address instead of INADDR_ANY.
   * The clients request the same rtp/rtcp borts and having the socket that are bound to ANY would result
   * in bind() failure */
  gboolean allow_bind_mcast_address = TRUE;

  mcast_transport_two_clients (FALSE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, allow_bind_mcast_address);
}

GST_END_TEST;

/* test if two multicast clients can choose the same multicast destination but different
 * ports
 * CASE: media is not shared */
GST_START_TEST
    (test_client_multicast_transport_specific_two_clients_same_destination) {
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=9002-9003;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.2:9002";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=9004-9005;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:9004";

  mcast_transport_two_clients (FALSE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;
/* test if two multicast clients can choose the same transport settings.
 * CASE: media is shared */
GST_START_TEST
    (test_client_multicast_transport_specific_two_clients_shared_media_same_transport)
{

  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = transport_client_1;
  const gchar *expected_transport_2 = expected_transport_1;
  const gchar *addr_client_2 = addr_client_1;

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if two multicast clients get the same transport settings without
 * requesting specific transport.
 * CASE: media is shared */
GST_START_TEST (test_client_multicast_two_clients_shared_media)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;mode=\"PLAY\"";
  const gchar *expected_transport_1 =
      "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = transport_client_1;
  const gchar *expected_transport_2 = expected_transport_1;
  const gchar *addr_client_2 = addr_client_1;

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if it's possible to play the shared media, after one of the clients
 * has terminated its session.
 */
GST_START_TEST (test_client_multicast_two_clients_shared_media_teardown_play)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;mode=\"PLAY\"";
  const gchar *expected_transport_1 =
      "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";

  const gchar *transport_client_2 = transport_client_1;
  const gchar *expected_transport_2 = expected_transport_1;

  mcast_transport_two_clients_teardown_play (transport_client_1,
      expected_transport_1, transport_client_2, expected_transport_2, FALSE,
      TRUE);
}

GST_END_TEST;

/* test if it's possible to play the shared media, after one of the clients
 * has terminated its session.
 */
GST_START_TEST
    (test_client_multicast_two_clients_not_shared_media_teardown_play) {
  const gchar *transport_client_1 = "RTP/AVP;multicast;mode=\"PLAY\"";
  const gchar *expected_transport_1 =
      "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";

  const gchar *transport_client_2 = transport_client_1;
  const gchar *expected_transport_2 =
      "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5002-5003;mode=\"PLAY\"";

  mcast_transport_two_clients_teardown_play (transport_client_1,
      expected_transport_1, transport_client_2, expected_transport_2, FALSE,
      FALSE);
}

GST_END_TEST;

/* test if two multicast clients get the different transport settings: the first client 
 * requests the specific transport configuration while the second client lets
 * the server select the multicast address and the ports.
 * CASE: media is shared */
GST_START_TEST
    (test_client_multicast_two_clients_first_specific_transport_shared_media) {
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;mode=\"PLAY\"";
  const gchar *expected_transport_2 = expected_transport_1;
  const gchar *addr_client_2 = addr_client_1;

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;
/* test if two multicast clients get the different transport settings: the first client lets
 * the server select the multicast address and the ports while the second client requests 
 * the specific transport configuration.
 * CASE: media is shared */
GST_START_TEST
    (test_client_multicast_two_clients_second_specific_transport_shared_media) {
  const gchar *transport_client_1 = "RTP/AVP;multicast;mode=\"PLAY\"";
  const gchar *expected_transport_1 =
      "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=5000-5001;mode=\"PLAY\"";
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=2;port=5004-5005;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:5004";

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if the maximum ttl multicast value is chosen by the server
 * CASE: the first client provides the highest ttl value */
GST_START_TEST (test_client_multicast_max_ttl_first_client)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=3;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=1;port=5002-5003;mode=\"PLAY\"";
  const gchar *expected_transport_2 =
      "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=3;port=5002-5003;mode=\"PLAY\"";
  const gchar *addr_client_2 = "233.252.0.2:5002";

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;

/* test if the maximum ttl multicast value is chosen by the server
 * CASE: the second client provides the highest ttl value */
GST_START_TEST (test_client_multicast_max_ttl_second_client)
{
  const gchar *transport_client_1 = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=2;port=5000-5001;mode=\"PLAY\"";
  const gchar *expected_transport_1 = transport_client_1;
  const gchar *addr_client_1 = "233.252.0.1:5000";

  const gchar *transport_client_2 = "RTP/AVP;multicast;destination=233.252.0.2;"
      "ttl=4;port=5002-5003;mode=\"PLAY\"";
  const gchar *expected_transport_2 = transport_client_2;
  const gchar *addr_client_2 = "233.252.0.2:5002";

  mcast_transport_two_clients (TRUE, transport_client_1,
      expected_transport_1, addr_client_1, transport_client_2,
      expected_transport_2, addr_client_2, FALSE);
}

GST_END_TEST;
GST_START_TEST (test_client_multicast_invalid_ttl)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client (3, "/test");

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  /* simple SETUP with an invalid ttl=0 */
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast;destination=233.252.0.1;ttl=0;port=5000-5001;");

  gst_rtsp_client_set_send_func (client, test_setup_response_461, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);
  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0);
  g_object_unref (session_pool);

  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);
}

GST_END_TEST;

static gboolean
test_response_scale_speed (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *header_value;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_RANGE,
          &header_value, 0) == GST_RTSP_OK);

  if (expected_scale_header != NULL) {
    fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SCALE,
            &header_value, 0) == GST_RTSP_OK);
    ck_assert_str_eq (header_value, expected_scale_header);
  } else {
    fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SCALE,
            &header_value, 0) == GST_RTSP_ENOTIMPL);
  }

  if (expected_speed_header != NULL) {
    fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SPEED,
            &header_value, 0) == GST_RTSP_OK);
    ck_assert_str_eq (header_value, expected_speed_header);
  } else {
    fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SPEED,
            &header_value, 0) == GST_RTSP_ENOTIMPL);
  }

  return TRUE;
}

/* Probe that tweaks segment events according to the values of the
 * fake_rate_value and fake_applied_rate_value variables. Used to simulate
 * seek results with different combinations of rate and applied rate.
 */
static GstPadProbeReturn
rate_tweaking_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstSegment segment;

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    GST_DEBUG ("got segment event %" GST_PTR_FORMAT, event);
    gst_event_copy_segment (event, &segment);
    if (fake_applied_rate_value)
      segment.applied_rate = fake_applied_rate_value;
    if (fake_rate_value)
      segment.rate = fake_rate_value;
    gst_event_unref (event);
    info->data = gst_event_new_segment (&segment);
    GST_DEBUG ("forwarding segment event %" GST_PTR_FORMAT,
        GST_EVENT (info->data));
  }

  return GST_PAD_PROBE_OK;
}

static void
attach_rate_tweaking_probe (void)
{
  GstRTSPContext *ctx;
  GstRTSPMedia *media;
  GstRTSPStream *stream;
  GstPad *srcpad;

  fail_unless ((ctx = gst_rtsp_context_get_current ()) != NULL);

  media = ctx->media;
  fail_unless (media != NULL);
  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  srcpad = gst_rtsp_stream_get_srcpad (stream);
  fail_unless (srcpad != NULL);

  GST_DEBUG ("adding rate_tweaking_probe");

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      rate_tweaking_probe, NULL, NULL);
  gst_object_unref (srcpad);
}

static void
do_test_scale_and_speed (const gchar * scale, const gchar * speed,
    GstRTSPStatusCode expected_response_code)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client (1, "/test");

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_TRANSPORT_CLIENT_SETTINGS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  expected_session_timeout = 20;
  g_signal_connect (G_OBJECT (client), "new-session",
      G_CALLBACK (new_session_cb), NULL);

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast");
  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=.*;mode=\"PLAY\"";
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;
  expected_session_timeout = 60;

  if (fake_applied_rate_value || fake_rate_value)
    attach_rate_tweaking_probe ();

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);

  if (scale != NULL)
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SCALE, scale);
  if (speed != NULL)
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SPEED, speed);

  if (expected_response_code == GST_RTSP_STS_BAD_REQUEST)
    gst_rtsp_client_set_send_func (client, test_response_400, NULL, NULL);
  else
    gst_rtsp_client_set_send_func (client, test_response_scale_speed, NULL,
        NULL);

  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  send_teardown (client, "rtsp://localhost/test");
  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);

}

GST_START_TEST (test_scale_and_speed)
{
  /* no scale/speed requested, no scale/speed should be received */
  expected_scale_header = NULL;
  expected_speed_header = NULL;
  do_test_scale_and_speed (NULL, NULL, GST_RTSP_STS_OK);

  /* scale requested, scale should be received */
  fake_applied_rate_value = 2;
  fake_rate_value = 1;
  expected_scale_header = "2.000";
  expected_speed_header = NULL;
  do_test_scale_and_speed ("2.000", NULL, GST_RTSP_STS_OK);

  /* speed requested, speed should be received */
  fake_applied_rate_value = 0;
  fake_rate_value = 0;
  expected_scale_header = NULL;
  expected_speed_header = "2.000";
  do_test_scale_and_speed (NULL, "2.000", GST_RTSP_STS_OK);

  /* both requested, both should be received */
  fake_applied_rate_value = 2;
  fake_rate_value = 2;
  expected_scale_header = "2.000";
  expected_speed_header = "2.000";
  do_test_scale_and_speed ("2", "2", GST_RTSP_STS_OK);

  /* scale requested but media doesn't handle scaling so both should be
   * received, with scale set to 1.000 and speed set to (requested scale
   * requested speed) */
  fake_applied_rate_value = 0;
  fake_rate_value = 5;
  expected_scale_header = "1.000";
  expected_speed_header = "5.000";
  do_test_scale_and_speed ("5", NULL, GST_RTSP_STS_OK);

  /* both requested but media only handles scaling so both should be received,
   * with scale set to (requested scale * requested speed) and speed set to 1.00
   */
  fake_rate_value = 1.000;
  fake_applied_rate_value = 4.000;
  expected_scale_header = "4.000";
  expected_speed_header = "1.000";
  do_test_scale_and_speed ("2", "2", GST_RTSP_STS_OK);

  /* test invalid values */
  fake_applied_rate_value = 0;
  fake_rate_value = 0;
  expected_scale_header = NULL;
  expected_speed_header = NULL;

  /* scale or speed not decimal values */
  do_test_scale_and_speed ("x", NULL, GST_RTSP_STS_BAD_REQUEST);
  do_test_scale_and_speed (NULL, "y", GST_RTSP_STS_BAD_REQUEST);

  /* scale or speed illegal decimal values */
  do_test_scale_and_speed ("0", NULL, GST_RTSP_STS_BAD_REQUEST);
  do_test_scale_and_speed (NULL, "0", GST_RTSP_STS_BAD_REQUEST);
  do_test_scale_and_speed (NULL, "-2", GST_RTSP_STS_BAD_REQUEST);
}

GST_END_TEST static void
test_client_play_sub (const gchar * mount_point, const gchar * url1,
    const gchar * url2)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPContext ctx = { NULL };

  client = setup_multicast_client (1, mount_point);

  ctx.client = client;
  ctx.auth = gst_rtsp_auth_new ();
  ctx.token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_context_push_current (&ctx);

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          url1) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP;multicast");
  /* destination is from adress pool */
  expected_transport = "RTP/AVP;multicast;destination=233.252.0.1;"
      "ttl=1;port=.*;mode=\"PLAY\"";
  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  expected_transport = NULL;

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          url2) == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client, test_response_play_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  send_teardown (client, url2);
  teardown_client (client);
  g_object_unref (ctx.auth);
  gst_rtsp_token_unref (ctx.token);
  gst_rtsp_context_pop_current (&ctx);
}

GST_START_TEST (test_client_play)
{
  test_client_play_sub ("/test", "rtsp://localhost/test/stream=0",
      "rtsp://localhost/test");
}

GST_END_TEST;

GST_START_TEST (test_client_play_root_mount_point)
{
  test_client_play_sub ("/", "rtsp://localhost/stream=0", "rtsp://localhost");
}

GST_END_TEST;

#define RTSP_CLIENT_TEST_TYPE (rtsp_client_test_get_type ())
#define RTSP_CLIENT_TEST_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RTSP_CLIENT_TEST_TYPE, RtspClientTestClass))

typedef struct RtspClientTest
{
  GstRTSPClient parent;
} RtspClientTest;

typedef struct RtspClientTestClass
{
  GstRTSPClientClass parent_class;
} RtspClientTestClass;

GType rtsp_client_test_get_type (void);

G_DEFINE_TYPE (RtspClientTest, rtsp_client_test, GST_TYPE_RTSP_CLIENT);

static void
rtsp_client_test_init (RtspClientTest * client)
{
}

static void
rtsp_client_test_class_init (RtspClientTestClass * klass)
{
}

static GstRTSPStatusCode
adjust_error_code_cb (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPStatusCode code)
{
  return GST_RTSP_STS_NOT_FOUND;
}

GST_START_TEST (test_adjust_error_code)
{
  RtspClientTest *client;
  RtspClientTestClass *klass;
  GstRTSPClientClass *base_klass;
  GstRTSPMessage request = { 0, };

  client = g_object_new (RTSP_CLIENT_TEST_TYPE, NULL);

  /* invalid request to trigger error response */
  ck_assert (gst_rtsp_message_init_request (&request, GST_RTSP_INVALID,
          "foopy://padoop/") == GST_RTSP_OK);

  /* expect non-adjusted error response 400 */
  gst_rtsp_client_set_send_func (GST_RTSP_CLIENT (client), test_response_400,
      NULL, NULL);
  ck_assert (gst_rtsp_client_handle_message (GST_RTSP_CLIENT (client),
          &request) == GST_RTSP_OK);

  /* override virtual function for adjusting error code */
  klass = RTSP_CLIENT_TEST_GET_CLASS (client);
  base_klass = GST_RTSP_CLIENT_CLASS (klass);
  base_klass->adjust_error_code = adjust_error_code_cb;

  /* expect error adjusted to 404 */
  gst_rtsp_client_set_send_func (GST_RTSP_CLIENT (client), test_response_404,
      NULL, NULL);
  ck_assert (gst_rtsp_client_handle_message (GST_RTSP_CLIENT (client),
          &request) == GST_RTSP_OK);

  gst_rtsp_message_unset (&request);
  g_object_unref (client);
}

GST_END_TEST;

static Suite *
rtspclient_suite (void)
{
  Suite *s = suite_create ("rtspclient");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_require);
  tcase_add_test (tc, test_request);
  tcase_add_test (tc, test_options);
  tcase_add_test (tc, test_describe);
  tcase_add_test (tc, test_describe_root_mount_point);
  tcase_add_test (tc, test_setup_tcp);
  tcase_add_test (tc, test_setup_tcp_root_mount_point);
  tcase_add_test (tc, test_setup_no_rtcp);
  tcase_add_test (tc, test_setup_tcp_two_streams_same_channels);
  tcase_add_test (tc,
      test_setup_tcp_two_streams_same_channels_root_mount_point);
  tcase_add_test (tc, test_client_multicast_transport_404);
  tcase_add_test (tc, test_client_multicast_transport);
  tcase_add_test (tc, test_client_multicast_ignore_transport_specific);
  tcase_add_test (tc, test_client_multicast_transport_specific);
  tcase_add_test (tc, test_client_sdp_with_max_bitrate_tag);
  tcase_add_test (tc, test_client_sdp_with_bitrate_tag);
  tcase_add_test (tc, test_client_sdp_with_max_bitrate_and_bitrate_tags);
  tcase_add_test (tc, test_client_sdp_with_no_bitrate_tags);
  tcase_add_test (tc,
      test_client_multicast_transport_specific_two_clients_shared_media);
  tcase_add_test (tc, test_client_multicast_transport_specific_two_clients);
#ifndef G_OS_WIN32
  tcase_add_test (tc,
      test_client_multicast_transport_specific_two_clients_same_ports);
#else
  /* skip the test on windows as the test restricts the multicast sockets to multicast traffic only,
   * by specifying the multicast IP as the bind address and this currently doesn't work on Windows */
  tcase_skip_broken_test (tc,
      test_client_multicast_transport_specific_two_clients_same_ports);
#endif
  tcase_add_test (tc,
      test_client_multicast_transport_specific_two_clients_same_destination);
  tcase_add_test (tc,
      test_client_multicast_transport_specific_two_clients_shared_media_same_transport);
  tcase_add_test (tc, test_client_multicast_two_clients_shared_media);
  tcase_add_test (tc,
      test_client_multicast_two_clients_shared_media_teardown_play);
  tcase_add_test (tc,
      test_client_multicast_two_clients_not_shared_media_teardown_play);
  tcase_add_test (tc,
      test_client_multicast_two_clients_first_specific_transport_shared_media);
  tcase_add_test (tc,
      test_client_multicast_two_clients_second_specific_transport_shared_media);
  tcase_add_test (tc,
      test_client_multicast_transport_specific_no_address_in_pool);
  tcase_add_test (tc, test_client_multicast_max_ttl_first_client);
  tcase_add_test (tc, test_client_multicast_max_ttl_second_client);
  tcase_add_test (tc, test_client_multicast_invalid_ttl);
  tcase_add_test (tc, test_scale_and_speed);
  tcase_add_test (tc, test_client_play);
  tcase_add_test (tc, test_client_play_root_mount_point);
  tcase_add_test (tc, test_adjust_error_code);

  return s;
}

GST_CHECK_MAIN (rtspclient);
