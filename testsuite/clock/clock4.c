/*
 * testsuite program to test clock behaviour
 *
 * creates a fakesrc ! identity ! fakesink pipeline
 * registers a callback on fakesrc and one on fakesink
 * also register a normal GLib timeout which should not be reached
 */

#include <gst/gst.h>

static GstClock *clock = NULL;

void
gst_clock_debug (GstClock * clock)
{
  GstClockTime time;

  time = gst_clock_get_time (clock);

  g_print ("Clock info: time %" G_GUINT64_FORMAT "\n", time);
}

static gboolean
ok_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  g_print ("unlocked async id %p\n", id);
  return FALSE;
}

static gboolean
error_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  g_print ("unlocked unscheduled async id %p, this is wrong\n", id);
  g_assert_not_reached ();

  return FALSE;
}

int
main (int argc, char *argv[])
{
  GstClockID id, id2;
  GstClockTime base;
  GstClockReturn result;

  gst_init (&argc, &argv);

  clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  /* signal every half a second */
  id = gst_clock_new_periodic_id (clock, base + GST_SECOND, GST_SECOND / 2);
  g_assert (id);

  g_print ("waiting one second\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  g_assert (result == GST_CLOCK_OK);

  g_print ("waiting for the next\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  g_assert (result == GST_CLOCK_OK);

  g_print ("waiting for the next async %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("waiting some more for the next async %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (2 * G_USEC_PER_SEC);

  id2 = gst_clock_new_periodic_id (clock, base + GST_SECOND, GST_SECOND / 2);
  g_assert (id2);

  g_print ("waiting some more for another async %p\n", id2);
  result = gst_clock_id_wait_async (id2, ok_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("unschedule %p\n", id);
  gst_clock_id_unschedule (id);

  /* entry cannot be used again */
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  g_assert (result == GST_CLOCK_UNSCHEDULED);
  result = gst_clock_id_wait (id, NULL);
  g_assert (result == GST_CLOCK_UNSCHEDULED);
  g_usleep (2 * G_USEC_PER_SEC);

  /* success */
  return 0;
}
