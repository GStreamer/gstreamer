/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * gstnettimeprovider.c: Unit test for the network time provider
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
#include <gst/net/gstnet.h>

#include <unistd.h>

GST_START_TEST (test_refcounts)
{
  GstNetTimeProvider *ntp;
  GstClock *clock;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "failed to get system clock");

  /* one for gstreamer, one for us */
  ASSERT_OBJECT_REFCOUNT (clock, "system clock", 2);

  ntp = gst_net_time_provider_new (clock, NULL, 0);
  fail_unless (ntp != NULL, "failed to create net time provider");

  /* one for ntp, one for gstreamer, one for us */
  ASSERT_OBJECT_REFCOUNT (clock, "system clock", 3);
  /* one for us */
  ASSERT_OBJECT_REFCOUNT (ntp, "net time provider", 1);

  gst_object_unref (ntp);
  ASSERT_OBJECT_REFCOUNT (clock, "net time provider", 2);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_functioning)
{
  GstNetTimeProvider *ntp;
  GstNetTimePacket *packet;
  GstClock *clock;
  GstClockTime local;
  GSocketAddress *server_addr;
  GInetAddress *addr;
  GSocket *socket;
  gint port = -1;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "failed to get system clock");
  ntp = gst_net_time_provider_new (clock, "127.0.0.1", 0);
  fail_unless (ntp != NULL, "failed to create net time provider");

  g_object_get (ntp, "port", &port, NULL);
  fail_unless (port > 0);

  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, NULL);
  fail_unless (socket != NULL, "could not create socket");

  addr = g_inet_address_new_from_string ("127.0.0.1");
  server_addr = g_inet_socket_address_new (addr, port);
  g_object_unref (addr);

  packet = gst_net_time_packet_new (NULL);
  fail_unless (packet != NULL, "failed to create packet");

  packet->local_time = local = gst_clock_get_time (clock);

  fail_unless (gst_net_time_packet_send (packet, socket, server_addr, NULL));

  g_free (packet);

  packet = gst_net_time_packet_receive (socket, NULL, NULL);

  fail_unless (packet != NULL, "failed to receive packet");
  fail_unless (packet->local_time == local, "local time is not the same");
  fail_unless (packet->remote_time > local, "remote time not after local time");
  fail_unless (packet->remote_time < gst_clock_get_time (clock),
      "remote time in the future");

  g_free (packet);

  g_object_unref (socket);
  g_object_unref (server_addr);

  gst_object_unref (ntp);
  gst_object_unref (clock);
}

GST_END_TEST;

static Suite *
gst_net_time_provider_suite (void)
{
  Suite *s = suite_create ("GstNetTimeProvider");
  TCase *tc_chain = tcase_create ("generic tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_refcounts);
  tcase_add_test (tc_chain, test_functioning);

  return s;
}

GST_CHECK_MAIN (gst_net_time_provider);
