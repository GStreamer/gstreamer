#include <gst/gst.h>
#include <assert.h>

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  GstElement *src,*identity,*sink;
  int i;

  DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("fakepipeline");
  src = gst_elementfactory_make("fakesrc","src");
/*  g_return_val_if_fail(src != NULL,1);
  if (argc == 2)
    gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  else
    gtk_object_set(GTK_OBJECT(src),"location","simplefake.c",NULL);*/
  identity = gst_elementfactory_make("identity","identity");
  g_return_val_if_fail(identity != NULL,2);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(sink != NULL,3);

  fprintf(stderr,"src %p identity %p sink %p\n",src,identity,sink);
  gst_bin_add(GST_BIN(pipeline),src);
  gst_bin_add(GST_BIN(pipeline),identity);
  gst_bin_add(GST_BIN(pipeline),sink);

  gst_element_connect(src,"src",identity,"sink");
  gst_element_connect(identity,"src",sink,"sink");

//  gst_bin_use_cothreads(GST_BIN(pipeline),TRUE);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(src) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  for (i=0;i<2;i++) {
    fprintf(stderr,"\n");
    gst_bin_iterate(GST_BIN(pipeline));
  }

  exit (0);
}
