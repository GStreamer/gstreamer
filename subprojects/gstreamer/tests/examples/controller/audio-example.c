/*
 * audio-example.c
 *
 * Builds a pipeline with [ audiotestsource ! autoaudiosink ] and sweeps
 * frequency and volume.
 *
 * Needs gst-plugins-base + gst-plugins-good installed.
 */

#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>

gint
main (gint argc, gchar ** argv)
{
  gint res = 1;
  GstElement *src, *sink;
  GstElement *bin;
  GstControlSource *cs1, *cs2;
  GstTimedValueControlSource *tvcs;
  GstClock *clock;
  GstClockID clock_id;
  GstClockReturn wait_ret;

  gst_init (&argc, &argv);

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  clock = gst_pipeline_get_clock (GST_PIPELINE (bin));
  src = gst_element_factory_make ("audiotestsrc", NULL);
  if (!src) {
    GST_WARNING ("need audiotestsrc from gst-plugins-base");
    goto Error;
  }
  sink = gst_element_factory_make ("autoaudiosink", NULL);
  if (!sink) {
    GST_WARNING ("need autoaudiosink from gst-plugins-good");
    goto Error;
  }

  gst_bin_add_many (GST_BIN (bin), src, sink, NULL);
  if (!gst_element_link (src, sink)) {
    GST_WARNING ("can't link elements");
    goto Error;
  }

  /* setup control sources */
  cs1 = gst_interpolation_control_source_new ();
  cs2 = gst_interpolation_control_source_new ();

  gst_object_add_control_binding (GST_OBJECT_CAST (src),
      gst_direct_control_binding_new (GST_OBJECT_CAST (src), "volume", cs1));
  gst_object_add_control_binding (GST_OBJECT_CAST (src),
      gst_direct_control_binding_new (GST_OBJECT_CAST (src), "freq", cs2));

  /* set interpolation mode */

  g_object_set (cs1, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
  g_object_set (cs2, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  /* set control values */
  tvcs = (GstTimedValueControlSource *) cs1;
  gst_timed_value_control_source_set (tvcs, 0 * GST_SECOND, 0.0);
  gst_timed_value_control_source_set (tvcs, 5 * GST_SECOND, 1.0);

  gst_object_unref (cs1);

  tvcs = (GstTimedValueControlSource *) cs2;
  gst_timed_value_control_source_set (tvcs, 0 * GST_SECOND, 220.0 / 20000.0);
  gst_timed_value_control_source_set (tvcs, 3 * GST_SECOND, 3520.0 / 20000.0);
  gst_timed_value_control_source_set (tvcs, 6 * GST_SECOND, 440.0 / 20000.0);

  gst_object_unref (cs2);

  /* run for 7 seconds */
  clock_id =
      gst_clock_new_single_shot_id (clock,
      gst_clock_get_time (clock) + (7 * GST_SECOND));

  if (gst_element_set_state (bin, GST_STATE_PLAYING)) {
    if ((wait_ret = gst_clock_id_wait (clock_id, NULL)) != GST_CLOCK_OK) {
      GST_WARNING ("clock_id_wait returned: %d", wait_ret);
    }
    gst_element_set_state (bin, GST_STATE_NULL);
  }

  /* cleanup */
  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
  gst_object_unref (bin);
  res = 0;
Error:
  return (res);
}
