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
  sink = gst_elementfactory_make("fakesink","sink");

  fprintf(stderr,"ADDING src\n");
  gst_bin_add(thread,src);
  fprintf(stderr,"ADDING sink\n");
  gst_bin_add(thread,sink);
  fprintf(stderr,"ADDING thread\n");
  gst_bin_add(pipeline,GST_ELEMENT(thread));

//  gst_element_add_ghost_pad(GST_ELEMENT(thread),gst_element_get_pad(sink,"sink"),"sink");

  fprintf(stderr,"CONNECTING src to sink\n");
  gst_element_connect (src, "src", sink, "sink");

  fprintf(stderr,"\nSWITCHING to READY:\n");
  gst_element_set_state (thread, GST_STATE_READY);
  fprintf(stderr,"\nPIPELINE sched:\n");
  gst_schedule_show(GST_ELEMENT_SCHED(pipeline));
  fprintf(stderr,"\nTHREAD sched:\n");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  fprintf(stderr,"\nSWITCHING to PLAYING:\n");
  gst_element_set_state (thread, GST_STATE_PLAYING);
  gst_schedule_show(GST_ELEMENT_SCHED(pipeline));
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

fprintf(stderr,"sleeping...\n");
sleep(1);
fprintf(stderr,"done sleeping...\n");

  fprintf(stderr,"\nSWITCHING to READY:\n");
  gst_element_set_state (thread, GST_STATE_READY);
  gst_schedule_show(GST_ELEMENT_SCHED(pipeline));
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

sleep(1);
}
