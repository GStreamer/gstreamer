#include <gst/gst.h>

#include <stdlib.h>
#include "mem.h"

extern gboolean _gst_plugin_spew;

GstPipeline *teardown_create_pipeline() {
  GstPipeline *pipeline;
  GstElementFactory *srcfactory, *sinkfactory;
  GstElement *src, *sink;
  GstPad *srcpad, *sinkpad;

  pipeline = GST_PIPELINE (gst_pipeline_new("pipeline"));
  g_return_val_if_fail(pipeline != NULL, NULL);

  srcfactory = gst_elementfactory_find("fakesrc");
  g_return_val_if_fail(srcfactory != NULL, NULL);
  sinkfactory = gst_elementfactory_find("fakesink");
  g_return_val_if_fail(sinkfactory != NULL, NULL);
  src = gst_elementfactory_create(srcfactory,"src");
  g_return_val_if_fail(src != NULL, NULL);
  sink = gst_elementfactory_create(sinkfactory,"sink");
  g_return_val_if_fail(sink != NULL, NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(sink));

  srcpad = gst_element_get_pad(src,"src");
  g_return_val_if_fail(srcpad != NULL, NULL);
  sinkpad = gst_element_get_pad(sink,"sink");
  g_return_val_if_fail(srcpad != NULL, NULL);

  gst_pad_connect(srcpad,sinkpad);

  return GST_PIPELINE(pipeline);
}

void teardown_destroy_pipeline(GstPipeline *pipeline) {
  gst_element_destroy(pipeline);
}


int main(int argc,char *argv[]) {
  GstElement *pipeline, *src;
  int i,max = 1;
  long usage1,usage2;

//  _gst_plugin_spew = TRUE;

  gst_init(&argc,&argv);

  if (argc == 2)
    max = atoi(argv[1]);

  usage1 = vmsize();
  for (i=0;i<max;i++) {
    pipeline = GST_ELEMENT (teardown_create_pipeline());
    src = gst_bin_get_by_name(GST_BIN(pipeline),"src");
//    g_print("got source %p, pushing",src);
//    for (j=0;j<max;j++) {
//      gst_src_push(GST_SRC(src));
//      g_print(".");
//    }
//    g_print("\n");
    teardown_destroy_pipeline(GST_PIPELINE (pipeline));
  }
  usage2 = vmsize();
  g_print("uses %ld bytes\n",usage2-usage1);

  return 0;
}
