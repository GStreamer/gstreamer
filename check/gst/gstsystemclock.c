/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsystemclock.c: Unit test for GstSystemClock
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

GST_START_TEST (test_signedness)
{
  GstClockTime time[] = { 0, 1, G_MAXUINT64 / GST_SECOND };
  GstClockTimeDiff diff[] =
      { 0, 1, -1, G_MAXINT64 / GST_SECOND, G_MININT64 / GST_SECOND };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (time); i++) {
    fail_if (time[i] != (time[i] * GST_SECOND / GST_SECOND));
  }
  for (i = 0; i < G_N_ELEMENTS (diff); i++) {
    fail_if (diff[i] != (diff[i] * GST_SECOND / GST_SECOND));
  }
}

GST_END_TEST
#define TIME_UNIT (GST_SECOND / 5)
    static void
gst_clock_debug (GstClock * clock)
{
  GstClockTime time;

  time = gst_clock_get_time (clock);
  g_message ("Clock info: time %" G_GUINT64_FORMAT "\n", time);
}

static gboolean
ok_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  g_message ("unlocked async id %p\n", id);
  return FALSE;
}

static gboolean
error_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  g_message ("unlocked unscheduled async id %p, this is wrong\n", id);
  fail_if (TRUE);

  return FALSE;
}

GST_START_TEST (test_single_shot)
{
  GstClock *clock;
  GstClockID id, id2;
  GstClockTime base;
  GstClockReturn result;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  id = gst_clock_new_single_shot_id (clock, base + TIME_UNIT);
  fail_unless (id != NULL, "Could not create single shot id");

  g_message ("waiting one second\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  g_message ("waiting in the past\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_EARLY, "Waiting did not return EARLY");
  gst_clock_id_unref (id);

  id = gst_clock_new_single_shot_id (clock, base + 2 * TIME_UNIT);
  g_message ("waiting one second async id %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  gst_clock_id_unref (id);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  id = gst_clock_new_single_shot_id (clock, base + 5 * TIME_UNIT);
  g_message ("waiting one second async, with cancel on id %p\n", id);
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  g_message ("cancel id %p after half a time unit\n", id);
  gst_clock_id_unschedule (id);
  gst_clock_id_unref (id);
  g_message ("canceled id %p\n", id);

  g_message ("waiting multiple one second async, with cancel\n");
  id = gst_clock_new_single_shot_id (clock, base + 5 * TIME_UNIT);
  id2 = gst_clock_new_single_shot_id (clock, base + 6 * TIME_UNIT);
  g_message ("waiting id %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  gst_clock_id_unref (id);
  g_message ("waiting id %p\n", id2);
  result = gst_clock_id_wait_async (id2, error_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  g_message ("cancel id %p after half a time unit\n", id2);
  gst_clock_id_unschedule (id2);
  g_message ("canceled id %p\n", id2);
  gst_clock_id_unref (id2);
  g_usleep (TIME_UNIT / (2 * 1000));
}

GST_END_TEST
GST_START_TEST (test_periodic_shot)
{
  GstClock *clock;
  GstClockID id, id2;
  GstClockTime base;
  GstClockReturn result;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  /* signal every half a time unit */
  id = gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT / 2);
  fail_unless (id != NULL, "Could not create periodic id");

  g_message ("waiting one time unit\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  g_message ("waiting for the next\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  g_message ("waiting for the next async %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  g_message ("waiting some more for the next async %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  id2 = gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT / 2);
  fail_unless (id2 != NULL, "Could not create second periodic id");

  g_message ("waiting some more for another async %p\n", id2);
  result = gst_clock_id_wait_async (id2, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  g_message ("unschedule %p\n", id);
  gst_clock_id_unschedule (id);

  /* entry cannot be used again */
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  fail_unless (result == GST_CLOCK_UNSCHEDULED,
      "Waiting did not return UNSCHEDULED");
  result = gst_clock_id_wait (id, NULL);
  fail_unless (result == GST_CLOCK_UNSCHEDULED,
      "Waiting did not return UNSCHEDULED");
  g_usleep (TIME_UNIT / (2 * 1000));

  /* clean up */
  gst_clock_id_unref (id);
}

GST_END_TEST Suite * gst_systemclock_suite (void)
{
  Suite *s = suite_create ("GstSystemClock");
  TCase *tc_chain = tcase_create ("waiting");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_signedness);
  tcase_add_test (tc_chain, test_single_shot);
  tcase_add_test (tc_chain, test_periodic_shot);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_systemclock_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
