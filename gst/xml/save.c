#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

GstPipeline *create_pipeline() {
  GstPipeline *pipeline;
  GstElement *src, *sink;
  GstPad *srcpad, *sinkpad;

  pipeline = gst_pipeline_new("fake_pipeline");
  g_return_if_fail(pipeline != NULL);

  src = gst_elementfactory_make("fakesrc","fakesrc");
  g_return_if_fail(src != NULL);
  sink = gst_elementfactory_make("fakesink","fakesink");
  g_return_if_fail(sink != NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(sink));

  srcpad = gst_element_get_pad(src,"src");
  g_return_if_fail(srcpad != NULL);
  sinkpad = gst_element_get_pad(sink,"sink");
  g_return_if_fail(srcpad != NULL);

  gst_pad_connect(srcpad,sinkpad);

  return GST_PIPELINE(pipeline);
}

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  xmlDocPtr doc;

//  _gst_plugin_spew = TRUE;

  gst_init(&argc,&argv);

  pipeline = GST_ELEMENT(create_pipeline());

  doc = gst_xml_write(pipeline);
  xmlSaveFile("save.xml",doc);
}
