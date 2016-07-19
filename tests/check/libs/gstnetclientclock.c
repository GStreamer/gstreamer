/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * gstnetclientclock.c: Unit test for the network client clock
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

GST_START_TEST (test_instantiation)
{
  GstClock *client, *local;

  local = gst_system_clock_obtain ();
  client = gst_net_client_clock_new (NULL, "127.0.0.1", 1234, GST_SECOND);
  fail_unless (local != NULL, "failed to get system clock");
  fail_unless (client != NULL, "failed to get network client clock");

  /* one for gstreamer, one for us */
  ASSERT_OBJECT_REFCOUNT (local, "system clock", 2);
  ASSERT_OBJECT_REFCOUNT (client, "network client clock", 1);

  gst_object_unref (client);

  ASSERT_OBJECT_REFCOUNT (local, "system clock", 2);

  gst_object_unref (local);
}

GST_END_TEST;

GST_START_TEST (test_functioning)
{
  GstNetTimeProvider *ntp;
  GstClock *client, *server;
  GstClockTime basex, basey, rate_num, rate_denom;
  GstClockTime servtime, clienttime, diff;
  gint port;
  gchar sign;

  server = gst_system_clock_obtain ();
  fail_unless (server != NULL, "failed to get system clock");

  /* move the clock ahead 100 seconds */
  gst_clock_get_calibration (server, &basex, &basey, &rate_num, &rate_denom);
  basey += 100 * GST_SECOND;
  gst_clock_set_calibration (server, basex, basey, rate_num, rate_denom);

  ntp = gst_net_time_provider_new (server, "127.0.0.1", 0);
  fail_unless (ntp != NULL, "failed to create network time provider");

  g_object_get (ntp, "port", &port, NULL);

  client = gst_net_client_clock_new (NULL, "127.0.0.1", port, GST_SECOND);
  fail_unless (client != NULL, "failed to get network client clock");

  g_object_get (client, "port", &port, NULL);

  /* let the clocks synchronize */
  gst_clock_wait_for_sync (GST_CLOCK (client), GST_SECOND);

  servtime = gst_clock_get_time (server);
  clienttime = gst_clock_get_time (client);

  if (servtime > clienttime) {
    sign = '-';
    diff = servtime - clienttime;
  } else {
    sign = '+';
    diff = clienttime - servtime;
  }

  GST_LOG ("server time:  %" GST_TIME_FORMAT, GST_TIME_ARGS (servtime));
  GST_LOG ("client time:  %" GST_TIME_FORMAT, GST_TIME_ARGS (clienttime));
  GST_LOG ("diff       : %c%" GST_TIME_FORMAT, sign, GST_TIME_ARGS (diff));

  /* can't in general make a precise assertion here, because this depends on
   * system load and a lot of things. however within half a second they should
   * at least be within 1/10 of a second of each other... */
  if (diff > 100 * GST_MSECOND)
    fail ("clocks not in sync (%" GST_TIME_FORMAT ")", diff);

  /*
     g_print ("diff: %" GST_TIME_FORMAT,
     GST_TIME_ARGS (servtime > clienttime ? servtime - clienttime
     : clienttime - servtime));
   */

  /* one for gstreamer, one for ntp, one for us */
  ASSERT_OBJECT_REFCOUNT (server, "system clock", 3);
  ASSERT_OBJECT_REFCOUNT (client, "network client clock", 1);

  gst_object_unref (ntp);

  ASSERT_OBJECT_REFCOUNT (server, "system clock", 2);

  gst_object_unref (client);
  gst_object_unref (server);
}

GST_END_TEST;

static Suite *
gst_net_client_clock_suite (void)
{
  Suite *s = suite_create ("GstNetClientClock");
  TCase *tc_chain = tcase_create ("generic tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_instantiation);
  tcase_add_test (tc_chain, test_functioning);

  return s;
}

GST_CHECK_MAIN (gst_net_client_clock);
