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

  id = gst_clock_new_single_shot_id (clock, base + GST_SECOND);
  g_assert (id);

  g_print ("waiting one second\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  g_assert (result == GST_CLOCK_OK);

  g_print ("waiting in the past\n");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  g_assert (result == GST_CLOCK_EARLY);
  gst_clock_id_unref (id);

  id = gst_clock_new_single_shot_id (clock, base + 2 * GST_SECOND);
  g_print ("waiting one second async id %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  gst_clock_id_unref (id);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (2 * G_USEC_PER_SEC);

  id = gst_clock_new_single_shot_id (clock, base + 5 * GST_SECOND);
  g_print ("waiting one second async, with cancel on id %p\n", id);
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (G_USEC_PER_SEC / 2);
  g_print ("cancel id %p after 0.5 seconds\n", id);
  gst_clock_id_unschedule (id);
  gst_clock_id_unref (id);
  g_print ("canceled id %p\n", id);

  g_print ("waiting multiple one second async, with cancel\n");
  id = gst_clock_new_single_shot_id (clock, base + 5 * GST_SECOND);
  id2 = gst_clock_new_single_shot_id (clock, base + 6 * GST_SECOND);
  g_print ("waiting id %p\n", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  gst_clock_id_unref (id);
  g_print ("waiting id %p\n", id2);
  result = gst_clock_id_wait_async (id2, error_callback, NULL);
  g_assert (result == GST_CLOCK_OK);
  g_usleep (G_USEC_PER_SEC / 2);
  g_print ("cancel id %p after 0.5 seconds\n", id2);
  gst_clock_id_unschedule (id2);
  gst_clock_id_unref (id2);
  g_print ("canceled id %p\n", id2);

  g_usleep (2 * G_USEC_PER_SEC);

  /* success */
  return 0;
}
