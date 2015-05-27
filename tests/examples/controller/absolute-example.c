/*
 * text-color-example.c
 *
 * Builds a pipeline with [videotestsrc ! textoverlay ! ximagesink] and
 * moves text
 *
 * Needs gst-plugins-base installed.
 */

#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstlfocontrolsource.h>
#include <gst/controller/gstargbcontrolbinding.h>
#include <gst/controller/gstdirectcontrolbinding.h>

gint
main (gint argc, gchar ** argv)
{
  gint res = 1;
  GstElement *src, *text, *sink;
  GstElement *bin;
  GstControlSource *cs;
  GstClock *clock;
  GstClockID clock_id;
  GstClockReturn wait_ret;

  gst_init (&argc, &argv);

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  clock = gst_pipeline_get_clock (GST_PIPELINE (bin));
  src = gst_element_factory_make ("videotestsrc", NULL);
  if (!src) {
    GST_WARNING ("need videotestsrc from gst-plugins-base");
    goto Error;
  }
  g_object_set (src, "pattern", /* red */ 4,
      NULL);
  text = gst_element_factory_make ("textoverlay", NULL);
  if (!text) {
    GST_WARNING ("need textoverlay from gst-plugins-base");
    goto Error;
  }
  g_object_set (text,
      "text", "GStreamer rocks!",
      "font-desc", "Sans, 30",
      "xpos", 0.0, "wrap-mode", -1, "halignment", /* position */ 4,
      "valignment", /* position */ 3,
      NULL);
  sink = gst_element_factory_make ("ximagesink", NULL);
  if (!sink) {
    GST_WARNING ("need ximagesink from gst-plugins-base");
    goto Error;
  }

  gst_bin_add_many (GST_BIN (bin), src, text, sink, NULL);
  if (!gst_element_link_many (src, text, sink, NULL)) {
    GST_WARNING ("can't link elements");
    goto Error;
  }

  /* setup control sources */
  cs = gst_interpolation_control_source_new ();
  gst_object_add_control_binding (GST_OBJECT_CAST (text),
      gst_direct_control_binding_new_absolute (GST_OBJECT_CAST (text), "deltax",
          cs));

  g_object_set (cs, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  // At second 0 the text will be at 0px on the x-axis
  gst_timed_value_control_source_set ((GstTimedValueControlSource *) cs, 0, 0);
  // At second 5 the text will be at 1000px on the x-axis
  gst_timed_value_control_source_set ((GstTimedValueControlSource *) cs,
      GST_SECOND * 5, 1000);

  gst_object_unref (cs);

  /* run for 10 seconds */
  clock_id =
      gst_clock_new_single_shot_id (clock,
      gst_clock_get_time (clock) + (10 * GST_SECOND));

  if (gst_element_set_state (bin, GST_STATE_PLAYING)) {
    if ((wait_ret = gst_clock_id_wait (clock_id, NULL)) != GST_CLOCK_OK) {
      GST_WARNING ("clock_id_wait returned: %d", wait_ret);
    }
    gst_element_set_state (bin, GST_STATE_NULL);
  }

  /* cleanup */
  gst_clock_id_unref (clock_id);
  gst_object_unref (G_OBJECT (clock));
  gst_object_unref (G_OBJECT (bin));
  res = 0;
Error:
  return (res);
}
