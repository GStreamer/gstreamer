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
  GstElement *src, *id, *sink, *pipeline;
  GstClock *clock = NULL;

  gst_init (&argc, &argv);

  if ((src = gst_element_factory_make ("fakesrc", "source")) == NULL)
  {
    g_print ("Could not create a fakesrc element !\n");
    return 1;
  }
  if ((id = gst_element_factory_make ("identity", "filter")) == NULL)
  {
    g_print ("Could not create a identity element !\n");
    return 1;
  }
  if ((sink = gst_element_factory_make ("fakesink", "sink")) == NULL)
  {
    g_print ("Could not create a fakesink element !\n");
    return 1;
  }

  if ((pipeline = gst_pipeline_new ("pipeline")) == NULL)
  {
    g_print ("Could not create a pipeline element !\n");
    return 1;
  }

  gst_element_connect_many (src, id, sink, NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, id, sink, NULL);

  clock = gst_bin_get_clock (GST_BIN (pipeline));
  g_assert (clock != NULL);
  gst_clock_debug (clock);
  gst_clock_set_active (clock, TRUE);
  gst_clock_debug (clock);
  //clock = gst_clock_new ("clock");
  //gst_element_set_clock (src, clock);
  //clock = gst_element_get_clock (src);
  //g_assert (clock != NULL);

  gst_bin_iterate (GST_BIN (pipeline));
  gst_clock_debug (clock);
  gst_clock_debug (clock);
  gst_clock_debug (clock);

  /* success */
  return 0;
}
