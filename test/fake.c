#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GstBin *bin;
  GstElement *src, *sink;
  GstPad *srcpad, *sinkpad;

//  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);

  bin = gst_pipeline_new("pipeline");
  g_return_if_fail(bin != NULL);

  g_print("--- creating src and sink elements\n");
  src = gst_elementfactory_make("fakesrc","src");
  g_return_if_fail(src != NULL);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_if_fail(sink != NULL);

  g_print("--- about to add the elements to the bin\n");
  gst_bin_add(bin,GST_ELEMENT(src));
  gst_bin_add(bin,GST_ELEMENT(sink));

  g_print("--- getting pads\n");
  srcpad = gst_element_get_pad(src,"src");
  g_return_if_fail(srcpad != NULL);
  sinkpad = gst_element_get_pad(sink,"sink");
  g_return_if_fail(srcpad != NULL);

  g_print("--- connecting\n");
  gst_pad_connect(srcpad,sinkpad);

  g_print("--- setting up\n");
  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_READY);

  g_print("--- iterating\n");
  gst_bin_iterate(bin);
}
