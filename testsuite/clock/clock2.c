/*
 * testsuite program to test clock behaviour
 *
 * creates a fakesrc ! identity ! fakesink pipeline
 * registers a callback on fakesrc and one on fakesink
 * also register a normal GLib timeout which should not be reached
 */

#include <gst/gst.h>
void
gst_clock_debug (GstClock *clock, GstElement *fakesink)
{
  g_print ("Clock info: time %"G_GUINT64_FORMAT" - Element info: time %"G_GUINT64_FORMAT"\n",
	   gst_clock_get_time (clock), gst_element_get_time (fakesink));
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

  gst_element_wait (fakesink, 2 * GST_SECOND);
  gst_clock_debug (clock, fakesink);
  
  gst_element_wait (fakesink, 5 * GST_SECOND);
  gst_clock_debug (clock, fakesink);

  g_usleep (G_USEC_PER_SEC);
  gst_clock_debug (clock, fakesink);

  /* success */
  return 0;
}
