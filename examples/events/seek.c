#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstBin *bin;
  GstElement *src, *sink;
  GstPad *srcpad, *sinkpad;

/*  _gst_plugin_spew = TRUE; */
  gst_init (&argc, &argv);

  bin = GST_BIN (gst_pipeline_new ("pipeline"));
  g_return_val_if_fail (bin != NULL, -1);

  g_print ("--- creating src and sink elements\n");
  src = gst_element_factory_make ("fakesrc", "src");
  g_return_val_if_fail (src != NULL, -1);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_return_val_if_fail (sink != NULL, -1);

  g_print ("--- about to add the elements to the bin\n");
  gst_bin_add (bin, GST_ELEMENT (src));
  gst_bin_add (bin, GST_ELEMENT (sink));

  g_print ("--- getting pads\n");
  srcpad = gst_element_get_pad (src, "src");
  g_return_val_if_fail (srcpad != NULL, -1);
  sinkpad = gst_element_get_pad (sink, "sink");
  g_return_val_if_fail (srcpad != NULL, -1);

  g_print ("--- linking\n");
  gst_pad_link (srcpad, sinkpad);

  g_print ("--- setting up\n");
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  g_print ("--- iterating\n");
  gst_bin_iterate (bin);
  gst_bin_iterate (bin);

  g_print ("--- seek to 100\n");
  gst_pad_send_event (srcpad, gst_event_new_seek (GST_SEEK_ANY, 100, FALSE));

  g_print ("--- seek done, iterating\n");
  gst_bin_iterate (bin);
  gst_bin_iterate (bin);

  g_print ("--- seek to 200 with flush\n");
  gst_pad_send_event (srcpad, gst_event_new_seek (GST_SEEK_ANY, 200, TRUE));

  g_print ("--- seek done, iterating\n");
  gst_bin_iterate (bin);
  gst_bin_iterate (bin);
  gst_bin_iterate (bin);

  g_print ("--- flush\n");
  gst_pad_send_event (srcpad, gst_event_new_flush ());

  g_print ("--- flush done, iterating\n");
  gst_bin_iterate (bin);
  gst_bin_iterate (bin);

  g_print ("--- cleaning up\n");
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);

  return 0;
}
