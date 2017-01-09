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
#include <rtsp-address-pool.h>

GST_START_TEST (test_get_sockets)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPAddressPool *pool;
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

  /* configure address pool for IPv4 and IPv6 unicast addresses */
  pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          GST_RTSP_ADDRESS_POOL_ANY_IPV4, GST_RTSP_ADDRESS_POOL_ANY_IPV4, 50000,
          60000, 0));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          GST_RTSP_ADDRESS_POOL_ANY_IPV6, GST_RTSP_ADDRESS_POOL_ANY_IPV6, 50000,
          60000, 0));
  gst_rtsp_stream_set_address_pool (stream, pool);

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

  g_object_unref (pool);

  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));

  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

GST_START_TEST (test_allocate_udp_ports_fail)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPAddressPool *pool;

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

  pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (pool, "192.168.1.1",
          "192.168.1.1", 6000, 6001, 0));
  gst_rtsp_stream_set_address_pool (stream, pool);

  fail_if (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  g_object_unref (pool);
  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));
  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

GST_START_TEST (test_get_multicast_address)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstRTSPAddressPool *pool;
  GstRTSPAddress *addr1;
  GstRTSPAddress *addr2;

  srcpad = gst_pad_new ("testsrcpad", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  gst_pad_set_active (srcpad, TRUE);
  pay = gst_element_factory_make ("rtpgstpay", "testpayloader");
  fail_unless (pay != NULL);
  stream = gst_rtsp_stream_new (0, pay, srcpad);
  fail_unless (stream != NULL);
  gst_object_unref (pay);
  gst_object_unref (srcpad);

  pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.0", "233.252.0.0", 5100, 5101, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "FF11:DB8::1", "FF11:DB8::1", 5102, 5103, 1));
  gst_rtsp_stream_set_address_pool (stream, pool);

  addr1 = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr1 != NULL);
  fail_unless_equals_string (addr1->address, "233.252.0.0");
  fail_unless_equals_int (addr1->port, 5100);
  fail_unless_equals_int (addr1->n_ports, 2);

  addr2 = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr2 != NULL);
  fail_unless_equals_string (addr2->address, "233.252.0.0");
  fail_unless_equals_int (addr2->port, 5100);
  fail_unless_equals_int (addr2->n_ports, 2);

  gst_rtsp_address_free (addr1);
  gst_rtsp_address_free (addr2);

  addr1 = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV6);
  fail_unless (addr1 != NULL);
  fail_unless (!g_ascii_strcasecmp (addr1->address, "FF11:DB8::1"));
  fail_unless_equals_int (addr1->port, 5102);
  fail_unless_equals_int (addr1->n_ports, 2);

  addr2 = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV6);
  fail_unless (addr2 != NULL);
  fail_unless (!g_ascii_strcasecmp (addr2->address, "FF11:DB8::1"));
  fail_unless_equals_int (addr2->port, 5102);
  fail_unless_equals_int (addr2->n_ports, 2);

  gst_rtsp_address_free (addr1);
  gst_rtsp_address_free (addr2);

  g_object_unref (pool);

  gst_object_unref (stream);
}

GST_END_TEST;

/*  test case: address pool only contains multicast addresses,
 *  but the client is requesting unicast udp */
GST_START_TEST (test_multicast_address_and_unicast_udp)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPAddressPool *pool;

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

  pool = gst_rtsp_address_pool_new ();
  /* add a multicast addres to the address pool */
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.0", "233.252.0.0", 5200, 5201, 1));
  gst_rtsp_stream_set_address_pool (stream, pool);

  fail_unless (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  g_object_unref (pool);
  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));
  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

GST_START_TEST (test_allocate_udp_ports_multicast)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPAddressPool *pool;
  GstRTSPAddress *addr;

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

  pool = gst_rtsp_address_pool_new ();
  /* add multicast addresses to the address pool */
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.1", "233.252.0.1", 6000, 6001, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "FF11:DB8::1", "FF11:DB8::1", 6002, 6003, 1));
  gst_rtsp_stream_set_address_pool (stream, pool);

  fail_unless (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  /* check the multicast address and ports for IPv4 */
  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr != NULL);
  fail_unless_equals_string (addr->address, "233.252.0.1");
  fail_unless_equals_int (addr->port, 6000);
  fail_unless_equals_int (addr->n_ports, 2);
  gst_rtsp_address_free (addr);

  /* check the multicast address and ports for IPv6 */
  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV6);
  fail_unless (addr != NULL);
  fail_unless (!g_ascii_strcasecmp (addr->address, "FF11:DB8::1"));
  fail_unless_equals_int (addr->port, 6002);
  fail_unless_equals_int (addr->n_ports, 2);
  gst_rtsp_address_free (addr);

  g_object_unref (pool);
  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));
  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

GST_START_TEST (test_allocate_udp_ports_client_settings)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPAddressPool *pool;
  GstRTSPAddress *addr;

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

  pool = gst_rtsp_address_pool_new ();
  /* add multicast addresses to the address pool */
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.1", "233.252.0.1", 6000, 6001, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "FF11:DB7::1", "FF11:DB7::1", 6004, 6005, 1));
  /* multicast address specified by the client */
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.2", "233.252.0.2", 6002, 6003, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "FF11:DB8::1", "FF11:DB8::1", 6006, 6007, 1));
  gst_rtsp_stream_set_address_pool (stream, pool);

  fail_unless (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  /* Reserve IPV4 mcast address */
  addr = gst_rtsp_stream_reserve_address (stream, "233.252.0.2", 6002, 2, 1);
  fail_unless (addr != NULL);
  gst_rtsp_address_free (addr);

  /* verify that the multicast address and ports correspond to the requested client
   * transport information for IPv4 */
  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr != NULL);
  fail_unless_equals_string (addr->address, "233.252.0.2");
  fail_unless_equals_int (addr->port, 6002);
  fail_unless_equals_int (addr->n_ports, 2);
  gst_rtsp_address_free (addr);

  /* Reserve IPV6 mcast address */
  addr = gst_rtsp_stream_reserve_address (stream, "FF11:DB8::1", 6006, 2, 1);
  fail_unless (addr != NULL);
  gst_rtsp_address_free (addr);

  /* verify that the multicast address and ports correspond to the requested client
   * transport information for IPv6 */
  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV6);
  fail_unless (addr != NULL);
  fail_unless (!g_ascii_strcasecmp (addr->address, "FF11:DB8::1"));
  fail_unless_equals_int (addr->port, 6006);
  fail_unless_equals_int (addr->n_ports, 2);
  gst_rtsp_address_free (addr);

  g_object_unref (pool);
  fail_unless (gst_rtsp_stream_leave_bin (stream, bin, rtpbin));
  gst_object_unref (bin);
  gst_object_unref (stream);
}

GST_END_TEST;

GST_START_TEST (test_tcp_transport)
{
  GstPad *srcpad;
  GstElement *pay;
  GstRTSPStream *stream;
  GstBin *bin;
  GstElement *rtpbin;
  GstRTSPRange server_port;

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

  /* TCP transport */
  gst_rtsp_stream_set_protocols (stream, GST_RTSP_LOWER_TRANS_TCP);
  fail_unless (gst_rtsp_stream_join_bin (stream, bin, rtpbin, GST_STATE_NULL));

  /* port that the server will use to receive RTCP makes only sense in the UDP
   * case so verify that the received server port is 0 in the TCP case */
  gst_rtsp_stream_get_server_port (stream, &server_port, G_SOCKET_FAMILY_IPV4);
  fail_unless_equals_int (server_port.min, 0);
  fail_unless_equals_int (server_port.max, 0);

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
  tcase_add_test (tc, test_allocate_udp_ports_fail);
  tcase_add_test (tc, test_get_multicast_address);
  tcase_add_test (tc, test_multicast_address_and_unicast_udp);
  tcase_add_test (tc, test_allocate_udp_ports_multicast);
  tcase_add_test (tc, test_allocate_udp_ports_client_settings);
  tcase_add_test (tc, test_tcp_transport);

  return s;
}

GST_CHECK_MAIN (rtspstream);
