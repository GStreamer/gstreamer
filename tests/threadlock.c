#include <stdio.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *pipeline, *thread;
  GstElement *src, *queue1, *sink;

  gst_info_set_categories(-1);
  gst_debug_set_categories(-1);
  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  thread = gst_thread_new("thread");
  src = gst_elementfactory_make("fakesrc","src");
  gtk_object_set(GTK_OBJECT(src),"silent",TRUE,NULL);
  sink = gst_elementfactory_make("fakesink","sink");
  gtk_object_set(GTK_OBJECT(sink),"silent",TRUE,NULL);

  fprintf(stderr,"ADDING src\n");
  gst_bin_add(thread,src);
  fprintf(stderr,"ADDING sink\n");
  gst_bin_add(thread,sink);
  fprintf(stderr,"CONNECTING src to sink\n");
  gst_element_connect (src, "src", sink, "sink");

  fprintf(stderr,"ADDING thread\n");
  gst_bin_add(pipeline,GST_ELEMENT(thread));

  while (1) {
    fprintf(stderr,"\nSWITCHING to PLAYING:\n");
    gst_element_set_state (thread, GST_STATE_PLAYING);
    sleep(1);
    fprintf(stderr,"\nSWITCHING to PAUSED:\n");
    gst_element_set_state (thread, GST_STATE_PAUSED);
//    sleep(1);
  }
}
