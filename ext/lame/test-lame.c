#include <gst/gst.h>


/* 
 * this example uses tee, fakesrc, fakesink and lame
 *
 * it requests a new pad from tee and attaches lame and fakesink
 * after iterating
 * it requests another one
 * this is to test if the encoder is initialized ok when adding to a
 * pipe that has played
 */

static void
error_callback (GObject * object, GstObject * orig, gchar * error)
{
  g_print ("ERROR: %s: %s\n", GST_OBJECT_NAME (orig), error);
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstElement *src, *tee, *encoder1, *encoder2, *sink1, *sink2;
  GstPad *teepad1, *teepad2;
  GstCaps *caps;


  gst_init (&argc, &argv);

  /* create elements */
  if (!(pipeline = gst_element_factory_make ("pipeline", "pipeline")))
    return 1;
  if (!(src = gst_element_factory_make ("fakesrc", "source")))
    return 1;
  if (!(tee = gst_element_factory_make ("tee", "tee")))
    return 1;
  if (!(encoder1 = gst_element_factory_make ("lame", "lame1")))
    return 1;
  if (!(encoder2 = gst_element_factory_make ("lame", "lame2")))
    return 1;
  if (!(sink1 = gst_element_factory_make ("fakesink", "sink1")))
    return 1;
  if (!(sink2 = gst_element_factory_make ("fakesink", "sink2")))
    return 1;

  pipeline = gst_pipeline_new ("pipeline");
  g_signal_connect (pipeline, "error", G_CALLBACK (error_callback), NULL);

  /* set up input */
  g_print ("setting up input\n");
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), tee);
  gst_pad_link (gst_element_get_pad (src, "src"),
      gst_element_get_pad (tee, "sink"));

  /* set up fakesrc */
  g_object_set (G_OBJECT (src), "filltype", 3, NULL);
  g_object_set (G_OBJECT (src), "sizetype", 3, NULL);

  /* set caps on fakesrc */
  caps = GST_CAPS_NEW ("input audio",
      "audio/raw",
      "format", GST_PROPS_STRING ("int"),
      "rate", GST_PROPS_INT (44100),
      "width", GST_PROPS_INT (16),
      "depth", GST_PROPS_INT (16),
      "law", GST_PROPS_INT (0),
      "signed", GST_PROPS_BOOLEAN (TRUE), "channels", GST_PROPS_INT (1)
      );

  g_object_set (G_OBJECT (src), "sizetype", 3, "filltype", 3, NULL);


  gst_element_set_state (pipeline, GST_STATE_READY);
  g_print ("Setting caps on fakesrc's src pad\n");
  if (gst_pad_try_set_caps (gst_element_get_pad (src, "src"), caps) <= 0)
    g_print ("Could not set caps !\n");

  /* request first pad from tee and connect */
  g_print ("attaching first output pipe to tee\n");
  teepad1 = gst_element_request_pad_by_name (tee, "src%d");

  gst_bin_add (GST_BIN (pipeline), encoder1);
  gst_bin_add (GST_BIN (pipeline), sink1);
  gst_pad_link (teepad1, gst_element_get_pad (encoder1, "sink"));
  gst_pad_link (gst_element_get_pad (encoder1, "src"),
      gst_element_get_pad (sink1, "sink"));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* iterate */
  g_print ("iterate\n");
  gst_bin_iterate (GST_BIN (pipeline));
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  /* request second pad and connect */
  g_print ("attaching second output pipe to tee\n");
  teepad2 = gst_element_request_pad_by_name (tee, "src%d");

  gst_bin_add (GST_BIN (pipeline), encoder2);
  gst_bin_add (GST_BIN (pipeline), sink2);
  gst_pad_link (teepad2, gst_element_get_pad (encoder2, "sink"));
  gst_pad_link (gst_element_get_pad (encoder2, "src"),
      gst_element_get_pad (sink2, "sink"));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("iterate\n");
  gst_bin_iterate (GST_BIN (pipeline));
  g_print ("done\n");
  return 0;
}
