#include <stdio.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *pipeline, *thread;
  GstElement *src, *queue1, *sink;

  gst_init(&argc,&argv);
  gst_info_set_categories(-1);
  gst_debug_set_categories(-1);

  pipeline = gst_pipeline_new("pipeline");
  thread = gst_thread_new("thread");
  src = gst_elementfactory_make("fakesrc","src");
  queue1 = gst_elementfactory_make("queue","queue");
  sink = gst_elementfactory_make("fakesink","sink");

  gst_bin_add(pipeline,src);
  gst_bin_add(pipeline,queue1);
  gst_bin_add(pipeline,GST_ELEMENT(thread));
  gst_bin_add(thread,sink);

  gst_element_add_ghost_pad(GST_ELEMENT(thread),gst_element_get_pad(sink,"sink"),"sink");

  gst_element_connect (src,"src",queue1,"sink");
  gst_element_connect (queue1, "src", thread, "sink");


  fprintf(stderr,"\n\n\n");
  gst_element_set_state (pipeline, GST_STATE_READY);


  fprintf(stderr,"\n\n\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
}
