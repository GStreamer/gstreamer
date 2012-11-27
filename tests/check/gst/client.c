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
test_option_response (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  GstRTSPMethod methods;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  gst_rtsp_message_dump (response);
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

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_option_response, NULL, NULL);
  gst_rtsp_message_dump (&request);
  fail_unless (gst_rtsp_client_handle_message (client,
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
  tcase_add_test (tc, test_options);

  return s;
}

GST_CHECK_MAIN (rtspclient);
