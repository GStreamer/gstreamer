/*
 * testsuite program to test clock behaviour
 *
 * creates a fakesrc ! identity ! fakesink pipeline
 * registers a callback on fakesrc and one on fakesink
 * also register a normal GLib timeout which should not be reached
 */

#include <gst/gst.h>

void
gst_clock_debug (GstClock * clock, GstElement * fakesink)
{
  GstClockTime time;

  time = gst_clock_get_time (clock);

  g_print ("Clock info: time %" G_GUINT64_FORMAT " Element %" GST_TIME_FORMAT
      "\n", time, GST_TIME_ARGS (time - fakesink->base_time));
}

static void
element_wait (GstElement * element, GstClockTime time)
{
  GstClockID id;

  id = gst_clock_new_single_shot_id (clock, time + element->base_time);
  gst_clock_id_wait (id, NULL);
  gst_clock_id_unref (id);
}

int
main (int argc, char *argv[])
{
  GstClock *clock = NULL;
  GstElement *pipeline, *fakesrc, *fakesink;

  gst_init (&argc, &argv);

  clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);

  /* we check the time on an element */
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  g_assert (fakesrc);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink);
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  gst_bin_add_many (GST_BIN (pipeline), fakesink, fakesrc, NULL);
  gst_element_link (fakesrc, fakesink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_clock_debug (clock, fakesink);
  g_usleep (G_USEC_PER_SEC);
  gst_clock_debug (clock, fakesink);

  element_wait (fakesink, 2 * GST_SECOND);
  gst_clock_debug (clock, fakesink);

  element_wait (fakesink, 5 * GST_SECOND);
  gst_clock_debug (clock, fakesink);

  g_usleep (G_USEC_PER_SEC);
  gst_clock_debug (clock, fakesink);

  /* success */
  return 0;
}
