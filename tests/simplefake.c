#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *src,*sink;

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("fakepipeline");
  src = gst_elementfactory_make("fakesrc","fakesrc");
  g_return_val_if_fail(1,src != NULL);
  sink = gst_elementfactory_make("fakesink","fakesink");
  g_return_val_if_fail(1,sink != NULL);

  gst_bin_add(GST_BIN(pipeline),src);
  gst_bin_add(GST_BIN(pipeline),sink);
}
