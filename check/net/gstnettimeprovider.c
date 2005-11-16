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

GST_START_TEST (test_refcounts)
{
  GstNetTimeProvider *ntp;
  GstClock *clock;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "failed to get system clock");

  /* one for gstreamer, one for us */
  ASSERT_OBJECT_REFCOUNT (clock, "system clock", 2);

  ntp = gst_net_time_provider_new (clock, NULL, -1);
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

Suite *
gst_net_time_provider_suite (void)
{
  Suite *s = suite_create ("GstNetTimeProvider");
  TCase *tc_chain = tcase_create ("generic tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_refcounts);

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
