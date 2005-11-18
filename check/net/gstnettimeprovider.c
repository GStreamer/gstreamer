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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
  struct sockaddr_in servaddr;
  gint port = -1, sockfd, ret;
  socklen_t len;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "failed to get system clock");
  ntp = gst_net_time_provider_new (clock, "127.0.0.1", 0);
  fail_unless (ntp != NULL, "failed to create net time provider");

  g_object_get (ntp, "port", &port, NULL);
  fail_unless (port > 0);

  sockfd = socket (AF_INET, SOCK_DGRAM, 0);
  fail_if (sockfd < 0, "socket failed");

  memset (&servaddr, 0, sizeof (servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons (port);
  inet_aton ("127.0.0.1", &servaddr.sin_addr);

  packet = gst_net_time_packet_new (NULL);
  fail_unless (packet != NULL, "failed to create packet");

  packet->local_time = local = gst_clock_get_time (clock);

  len = sizeof (servaddr);
  ret = gst_net_time_packet_send (packet, sockfd,
      (struct sockaddr *) &servaddr, len);

  fail_unless (ret == GST_NET_TIME_PACKET_SIZE, "failed to send packet");

  g_free (packet);

  packet = gst_net_time_packet_receive (sockfd, (struct sockaddr *) &servaddr,
      &len);

  fail_unless (packet != NULL, "failed to receive packet");
  fail_unless (packet->local_time == local, "local time is not the same");
  fail_unless (packet->remote_time > local, "remote time not after local time");
  fail_unless (packet->remote_time < gst_clock_get_time (clock),
      "remote time in the future");

  g_free (packet);

  close (sockfd);

  gst_object_unref (ntp);
  gst_object_unref (clock);
}

GST_END_TEST;

Suite *
gst_net_time_provider_suite (void)
{
  Suite *s = suite_create ("GstNetTimeProvider");
  TCase *tc_chain = tcase_create ("generic tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_refcounts);
  tcase_add_test (tc_chain, test_functioning);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_net_time_provider_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
