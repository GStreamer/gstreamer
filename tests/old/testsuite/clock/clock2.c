/*
 * testsuite program to test clock behaviour
 *
 * creates a fakesrc ! identity ! fakesink pipeline
 * registers a callback on fakesrc and one on fakesink
 * also register a normal GLib timeout which should not be reached
 */

#include <gst/gst.h>
void
gst_clock_debug (GstClock *clock)
{
  g_print ("Clock info: speed %f, active %s, time %d\n",
           gst_clock_get_speed (clock),
	   gst_clock_is_active (clock) ? "yes" : "no",
	   (gint) gst_clock_get_time (clock)
	   );
}
  
int
main (int argc, char *argv[])
{
  GstClock *clock = NULL;
  GstClockID id;

  gst_init (&argc, &argv);

  clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);

  gst_clock_debug (clock);
  sleep (1);
  gst_clock_debug (clock);
  gst_clock_set_active (clock, TRUE);
  gst_clock_debug (clock);
  sleep (1);
  gst_clock_debug (clock);

  id = gst_clock_new_single_shot_id (clock, GST_SECOND * 2);
  gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  
  id = gst_clock_new_single_shot_id (clock, GST_SECOND * 2);
  gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);

  gst_clock_set_active (clock, FALSE);
  gst_clock_debug (clock);
  sleep (1);
  gst_clock_debug (clock);

  /* success */
  return 0;
}
