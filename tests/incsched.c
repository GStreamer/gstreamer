#include <stdlib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *thread, *bin;
  GstElement *src, *identity, *sink;

  gst_init(&argc,&argv);
  gst_info_set_categories(-1);
  gst_debug_set_categories(-1);

  g_print("\n\nConstructing stuff:\n");
  thread = gst_pipeline_new("thread");
  bin = gst_bin_new("bin");
  src = gst_elementfactory_make("fakesrc","src");
  identity = gst_elementfactory_make("identity","identity");
  sink = gst_elementfactory_make("fakesink","sink");

  g_print("\n\nConnecting:\n");
  gst_element_connect(src,"src",identity,"sink");
  gst_element_connect(identity,"src",sink,"sink");

  g_print("\n\nAssembling things:\n");
  g_print("\nAdding src to bin:\n");
  gst_bin_add(bin,src);
  g_print("there are %d managed elements in bin\n",bin->num_managed_elements);

  g_print("\nAdding identity to bin:\n");
  gst_bin_add(bin,identity);
  g_print("there are %d managed elements in bin\n",bin->num_managed_elements);

  g_print("\nAdding sink to bin:\n");
  gst_bin_add(bin,sink);
  g_print("there are %d managed elements in bin\n",bin->num_managed_elements);

  g_print("\n\nDisconnecting sink:\n");
  gst_element_disconnect(identity,"src",sink,"sink");

  g_print("\nRemoving sink from bin:\n");
  gst_bin_remove(bin,sink);
  g_print("there are %d managed elements in bin\n",bin->num_managed_elements);

//  g_print("\nAdding bin to thread:\n");
//  gst_bin_add(thread,bin);
//  g_print("there are %d managed elements in thread now\n",thread->num_managed_elements);
//  g_print("there are %d managed elements in bin now\n",bin->num_managed_elements);

/*
  g_print("\n\nSaving xml:\n");
  xmlSaveFile("threadsync.gst", gst_xml_write(GST_ELEMENT(thread)));
*/

//  g_print("\n\nSetting state to READY:\n");
//  gst_element_set_state(thread,GST_STATE_READY);

  sleep(1);
}
