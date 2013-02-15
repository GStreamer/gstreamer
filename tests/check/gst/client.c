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

static gint cseq;

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

GST_START_TEST (test_request)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

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
}

GST_END_TEST;

gchar *expected_transport = NULL;;

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

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);

  fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 1);
  session = gst_rtsp_session_pool_find (session_pool, str);
  fail_unless (session != NULL);
  g_object_unref (session);

  g_object_unref (session_pool);


  return TRUE;
}

static GstRTSPClient *
setup_multicast_client (void)
{
  GstRTSPClient *client;
  GstRTSPSessionPool *session_pool;
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPAddressPool *address_pool;

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
  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  gst_rtsp_client_set_mount_points (client, mount_points);

  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (address_pool);

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

  g_object_unref (client);
}

GST_END_TEST;

GST_START_TEST (test_client_multicast_transport)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_multicast_client ();

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

  g_object_unref (client);
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

  g_object_unref (client);
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

  client = setup_multicast_client ();

  gst_rtsp_client_set_use_client_settings (client, TRUE);
  fail_unless (gst_rtsp_client_get_use_client_settings (client));


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
  /* fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0); */
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
  /* fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0); */
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
  /* fail_unless (gst_rtsp_session_pool_get_n_sessions (session_pool) == 0); */
  g_object_unref (session_pool);


  g_object_unref (client);
}

GST_END_TEST;

GST_START_TEST (test_client_multicast_transport_specific)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;
  GstRTSPSessionPool *session_pool;

  client = setup_multicast_client ();

  gst_rtsp_client_set_use_client_settings (client, TRUE);
  fail_unless (gst_rtsp_client_get_use_client_settings (client));

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
  tcase_add_test (tc, test_request);
  tcase_add_test (tc, test_options);
  tcase_add_test (tc, test_describe);
  tcase_add_test (tc, test_client_multicast_transport_404);
  tcase_add_test (tc, test_client_multicast_transport);
  tcase_add_test (tc, test_client_multicast_ignore_transport_specific);
  tcase_add_test (tc, test_client_multicast_invalid_transport_specific);
  tcase_add_test (tc, test_client_multicast_transport_specific);

  return s;
}

GST_CHECK_MAIN (rtspclient);
