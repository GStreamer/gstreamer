/*
 * text-color-example.c
 *
 * Builds a pipeline with [videotestsrc ! textoverlay ! ximagesink] and
 * modulates color, text and text pos.
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
  GstControlSource *cs_r, *cs_g, *cs_b;
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
  g_object_set (src, "pattern", /* checkers-8 */ 10,
      NULL);
  text = gst_element_factory_make ("textoverlay", NULL);
  if (!text) {
    GST_WARNING ("need textoverlay from gst-plugins-base");
    goto Error;
  }
  g_object_set (text,
      "text", "GStreamer rocks!",
      "font-desc", "Sans, 30", "halignment", /* position */ 4,
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
  cs = gst_lfo_control_source_new ();
  g_object_set (cs,
      "frequency", (gdouble) 0.11,
      "amplitude", (gdouble) 0.2, "offset", (gdouble) 0.5, NULL);
  gst_object_add_control_binding (GST_OBJECT_CAST (text),
      gst_direct_control_binding_new (GST_OBJECT_CAST (text), "xpos", cs));
  gst_object_unref (cs);

  cs = gst_lfo_control_source_new ();
  g_object_set (cs,
      "frequency", (gdouble) 0.04,
      "amplitude", (gdouble) 0.4, "offset", (gdouble) 0.5, NULL);
  gst_object_add_control_binding (GST_OBJECT_CAST (text),
      gst_direct_control_binding_new (GST_OBJECT_CAST (text), "ypos", cs));
  gst_object_unref (cs);

  cs_r = gst_lfo_control_source_new ();
  g_object_set (cs_r,
      "frequency", (gdouble) 0.19,
      "amplitude", (gdouble) 0.5, "offset", (gdouble) 0.5, NULL);
  cs_g = gst_lfo_control_source_new ();
  g_object_set (cs_g,
      "frequency", (gdouble) 0.27,
      "amplitude", (gdouble) 0.5, "offset", (gdouble) 0.5, NULL);
  cs_b = gst_lfo_control_source_new ();
  g_object_set (cs_b,
      "frequency", (gdouble) 0.13,
      "amplitude", (gdouble) 0.5, "offset", (gdouble) 0.5, NULL);
  gst_object_add_control_binding (GST_OBJECT_CAST (text),
      gst_argb_control_binding_new (GST_OBJECT_CAST (text), "color", NULL,
          cs_r, cs_g, cs_b));
  gst_object_unref (cs_r);
  gst_object_unref (cs_g);
  gst_object_unref (cs_b);

  /* run for 10 seconds */
  clock_id =
      gst_clock_new_single_shot_id (clock,
      gst_clock_get_time (clock) + (30 * GST_SECOND));

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
