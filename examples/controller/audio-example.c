/*
 * audio-example.c
 * 
 * Build a pipeline with testaudiosource->alsasink
 * and sweep frequency and volume
 *
 */

#include <gst/gst.h>
#include <gst/controller/gst-controller.h>

gint
main (gint argc, gchar ** argv)
{
  gint res = 1;
  GstElement *src, *sink;
  GstBin *bin;
  GstController *ctrl;
  GValue vol = { 0, };

  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  // build pipeline
  bin = GST_BIN (gst_pipeline_new ("pipeline"));
  /* TODO make this "testaudiosrc", when its ready */
  src = gst_element_factory_make ("sinesrc", "gen_audio");
  sink = gst_element_factory_make ("alsasink", "play_audio");
  gst_bin_add_many (bin, src, sink, NULL);

  // add a controller to the source
  if (!(ctrl =
          gst_controller_new (G_OBJECT (src), "frequency", "volume", NULL))) {
    goto Error;
  }
  // set interpolation
  gst_controller_set_interpolation_mode (ctrl, "volume",
      GST_INTERPOLATE_LINEAR);

  // set control values
  g_value_init (&vol, G_TYPE_DOUBLE);
  g_value_set_double (&vol, 0.0);
  gst_controller_set (ctrl, "volume", 0 * GST_SECOND, &vol);
  g_value_set_double (&vol, 1.0);
  gst_controller_set (ctrl, "volume", 1 * GST_SECOND, &vol);

  // iterate two seconds
  /*
     if(gst_element_set_state (bin, GST_STATE_PLAYING))
     {
     while (gst_bin_iterate (bin))
     {
     }
     }
     gst_element_set_state (bin, GST_STATE_NULL);
   */

  // cleanup
  g_object_unref (G_OBJECT (ctrl));
  g_object_unref (G_OBJECT (bin));
  res = 0;
Error:
  return (res);
}
