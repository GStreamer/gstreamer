#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *pipeline,*thr1,*thr2;
  GstElement *src,*queue,*sink;
  int i;

  gst_init(&argc,&argv);

  pipeline = GST_BIN(gst_pipeline_new("pipeline"));
  g_return_val_if_fail(1,pipeline != NULL);
  thr1 = GST_BIN(gst_thread_new("thr1"));
//  thr1 = gst_bin_new("thr1");
  g_return_val_if_fail(2,thr1 != NULL);
//  thr2 = GST_BIN(gst_thread_new("thr2"));
  thr2 = GST_BIN(gst_bin_new("thr2"));
  g_return_val_if_fail(3,thr2 != NULL);
fprintf(stderr,"QUEUE: fakesrc\n");
  src = gst_elementfactory_make("fakesrc","src");
  g_return_val_if_fail(4,src != NULL);
fprintf(stderr,"QUEUE: queue\n");
  queue = gst_elementfactory_make("queue","queue");
  g_return_val_if_fail(4,queue != NULL);
  gtk_object_set(GTK_OBJECT(queue),"max_level",1,NULL);
fprintf(stderr,"QUEUE: fakesink\n");
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(5,sink != NULL);
  fprintf(stderr,"QUEUE: have elements\n");

  gst_bin_add(thr1,src);
  fprintf(stderr,"QUEUE: added src to thr1\n");
  gst_element_add_ghost_pad(GST_ELEMENT(thr1),gst_element_get_pad(src,"src"));
//  gst_bin_use_cothreads(thr1,TRUE);
  gst_bin_add(thr2,sink);
  fprintf(stderr,"QUEUE: added sink to thr2\n");
  gst_element_add_ghost_pad(GST_ELEMENT(thr2),gst_element_get_pad(sink,"sink"));
//  gst_bin_use_cothreads(thr2,TRUE);
  fprintf(stderr,"QUEUE: filled in threads\n");

  gst_bin_add(pipeline,GST_ELEMENT(thr1));
  gst_bin_add(pipeline,GST_ELEMENT(queue));
  gst_bin_add(pipeline,GST_ELEMENT(thr2));
  fprintf(stderr,"QUEUE: connecting elements\n");
  gst_element_connect(GST_ELEMENT(thr1),"src",queue,"sink");
  gst_element_connect(queue,"src",GST_ELEMENT(thr2),"sink");
//  gst_pad_connect(gst_element_get_pad(src,"src"),gst_element_get_pad(queue,"sink"));
//  gst_pad_connect(gst_element_get_pad(queue,"src"),gst_element_get_pad(sink,"sink"));
  fprintf(stderr,"QUEUE: constructed outer pipeline\n");

  fprintf(stderr,"\n\nQUEUE: setting state:\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(src) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  sleep(1);
  fprintf(stderr,"\n\n");
  for (i=0;i<1000;i++)
    gst_bin_iterate(thr2);
//  fprintf(stderr,"\n\n");
//  fflush(stdout);
//  fflush(stderr);
//  gst_bin_iterate(thr2);
//
  exit (0);
}
