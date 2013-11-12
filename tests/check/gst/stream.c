/* GStreamer
 * Copyright (C) 2013 Axis Communications AB <dev-gstreamer at axis dot com>
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

#include <rtsp-stream.h>

GST_START_TEST (test_get_sockets)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GSocket *socket;
  gboolean have_ipv4;
  gboolean have_ipv6;

  srcpad = gst_pad_new ("testsrcpad", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  gst_pad_set_active (srcpad, TRUE);
  pay = gst_element_factory_make ("rtpgstpay", "testpayloader");
  fail_unless (pay != NULL);
  stream = gst_rtsp_stream_new (0, pay, srcpad);
  fail_unless (stream != NULL);
  gst_object_unref (pay);
  gst_object_unref (srcpad);
  rtpbin = gst_element_factory_make ("rtpbin", "testrtpbin");
  fail_unless (rtpbin != NULL);
  bin = GST_BIN (gst_bin_new ("testbin"));
  fail_unless (bin != NULL);
  fail_unless (gst_bin_add (bin, rtpbin));

  fail_unless (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  socket = gst_rtsp_stream_get_rtp_socket (stream, G_SOCKET_FAMILY_IPV4);
  have_ipv4 = (socket != NULL);
  if (have_ipv4) {
    fail_unless (g_socket_get_fd (socket) >= 0);
    g_object_unref (socket);
  }

  socket = gst_rtsp_stream_get_rtcp_socket (stream, G_SOCKET_FAMILY_IPV4);
  if (have_ipv4) {
    fail_unless (socket != NULL);
    fail_unless (g_socket_get_fd (socket) >= 0);
    g_object_unref (socket);
  } else {
    fail_unless (socket == NULL);
  }

  socket = gst_rtsp_stream_get_rtp_socket (stream, G_SOCKET_FAMILY_IPV6);
  have_ipv6 = (socket != NULL);
  if (have_ipv6) {
    fail_unless (g_socket_get_fd (socket) >= 0);
    g_object_unref (socket);
  }

  socket = gst_rtsp_stream_get_rtcp_socket (stream, G_SOCKET_FAMILY_IPV6);
  if (have_ipv6) {
    fail_unless (socket != NULL);
    fail_unless (g_socket_get_fd (socket) >= 0);
    g_object_unref (socket);
  } else {
    fail_unless (socket == NULL);
  }

  /* check that at least one family is available */
  fail_unless (have_ipv4 || have_ipv6);

  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));

  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

static Suite *
rtspstream_suite (void)
{
  Suite *s = suite_create ("rtspstream");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_get_sockets);

  return s;
}

GST_CHECK_MAIN (rtspstream);
