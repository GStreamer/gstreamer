#include <gst/gst.h>

void eos_handler(GstElement *element) {
  printf("got EOS signal\n");
}

int main (int argc,char *argv[]) {
  GstElement *pipeline, *disksrc, *identity, *fakesink;

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  disksrc = gst_elementfactory_make("disksrc","disksrc");
  identity = gst_elementfactory_make("identity","identity");
  fakesink = gst_elementfactory_make("fakesink","fakesink");

  g_object_set(G_OBJECT(disksrc),"location","events.c",NULL);
  g_signal_connectc(G_OBJECT(fakesink),"eos",eos_handler,NULL,FALSE);

  gst_bin_add(GST_BIN(pipeline),disksrc);
  gst_bin_add(GST_BIN(pipeline),fakesink);

  gst_element_connect(disksrc,"src",fakesink,"sink");

  gst_element_set_state(pipeline,GST_STATE_PLAYING);

  gst_bin_iterate(GST_BIN(pipeline));
  gst_bin_iterate(GST_BIN(pipeline));
}
