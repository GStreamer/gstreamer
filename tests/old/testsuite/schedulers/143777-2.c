
#include <gst/gst.h>

int
main (int argc, char **argv)
{
  GstElement *src, *sink, *enc, *tee;
  GstElement *pipeline;
  int i;


  gst_init (&argc, &argv);
  pipeline = gst_element_factory_make ("pipeline", "pipeline");

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);
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

  for (i = 0; i < 5; i++) {
    if (!gst_bin_iterate (GST_BIN (pipeline)))
      g_assert_not_reached ();
    g_print ("%d\n", i);
  }

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

  for (i = 5; i < 10; i++) {
    if (!gst_bin_iterate (GST_BIN (pipeline)))
      g_assert_not_reached ();
    g_print ("%d\n", i);
  }

  return 0;
}
