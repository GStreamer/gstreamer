#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElement *src, *identity, *sink;

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_elementfactory_make("bin","bin");
  g_return_val_if_fail(bin != NULL, -1);

  g_print("--- creating src and sink elements\n");
  src = gst_elementfactory_make("fakesrc","src");
  g_return_val_if_fail(src != NULL, -1);
  identity = gst_elementfactory_make(argv[1],"identity");
  g_return_val_if_fail(identity != NULL, -1);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(sink != NULL, -1);

  g_print("--- about to add the elements to the pipeline\n");
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(identity));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(sink));

  g_print("--- connecting\n");
  gst_pad_connect(gst_element_get_pad(src,"src"),
		  gst_element_get_pad(identity,"sink"));
  gst_pad_connect(gst_element_get_pad(identity,"src"),
		  gst_element_get_pad(sink,"sink"));


  g_print("--- starting up\n");
  gst_bin_iterate(GST_BIN(bin));

  g_print("\n");

  return 0;
}
