#include <gst/gst.h>
#include <assert.h>

int main(int argc,char *argv[]) {
  GstBin *thread;
  GstElement *src,*identity,*sink;
  int i;

  DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  thread = GST_BIN(gst_bin_new("bin4"));
  src = gst_elementfactory_make("fakesrc","src");
  identity = gst_elementfactory_make("identity","identity");
  g_return_val_if_fail(identity != NULL,2);
  gtk_object_set(GTK_OBJECT(identity),"loop_based",TRUE,NULL);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(sink != NULL,3);

  fprintf(stderr,"src %p identity %p sink %p\n",src,identity,sink);
  gst_bin_add(thread,src);
  gst_bin_add(thread,identity);
  gst_bin_add(thread,sink);

  gst_element_connect(src,"src",identity,"sink");
  gst_element_connect(identity,"src",sink,"sink");
  fprintf(stderr,"done creating case4 pipeline\n\n\n");

  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_PLAYING);

  gst_bin_iterate(thread);
  gst_bin_iterate(thread);
}
