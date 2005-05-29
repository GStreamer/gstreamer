
#include <gst/gst.h>

int
main (int argc, char **argv)
{
  GstElement *src, *sink, *enc, *tee;
  GstElement *pipeline;

  gst_init (&argc, &argv);
  pipeline = gst_element_factory_make ("pipeline", "pipeline");

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);
  g_object_set (src, "num-buffers", 10, NULL);
  tee = gst_element_factory_make ("tee", "tee1");
  g_assert (tee);
  enc = gst_element_factory_make ("identity", "enc");
  g_assert (enc);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);

  gst_bin_add_many (GST_BIN (pipeline), src, tee, enc, sink, NULL);
  if (!gst_element_link_many (src, tee, enc, sink, NULL))
    g_assert_not_reached ();
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_iterate (GST_BIN (pipeline));

  if (gst_element_set_state (pipeline, GST_STATE_PAUSED) != GST_STATE_SUCCESS)
    g_assert_not_reached ();
  gst_element_unlink_many (tee, enc, sink, NULL);
  gst_bin_remove_many (GST_BIN (pipeline), enc, sink, NULL);

  enc = gst_element_factory_make ("identity", "enc");
  g_assert (enc);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline), enc, sink, NULL);
  if (!gst_element_link_many (tee, enc, sink, NULL))
    g_assert_not_reached ();
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_iterate (GST_BIN (pipeline));
  g_print ("cleaning up...\n");
  gst_object_unref (GST_OBJECT (pipeline));

  g_print ("done.\n");
  return 0;
}
