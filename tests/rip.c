#include <gst/gst.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *paranoia,*lame,*sink;
  int i;
  int outfile;

  DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  unlink(argv[1]);
  outfile = open(argv[1],O_CREAT | O_RDWR | O_TRUNC);
  if (!outfile) {
    fprintf(stderr,"couldn't open file\n");
    exit(1);
  }
  fprintf(stderr,"outfile is fd %d\n",outfile);

  pipeline = gst_pipeline_new("ripper");
  paranoia = gst_elementfactory_make("cdparanoia","paranoia");
  g_return_val_if_fail(1,paranoia != NULL);
  lame = gst_elementfactory_make("lame","lame");
  g_return_val_if_fail(2,lame != NULL);
  gtk_object_set(GTK_OBJECT(lame),"bitrate",320,NULL);
  sink = gst_elementfactory_make("fdsink","fdsink");
  g_return_val_if_fail(3,sink != NULL);
  gtk_object_set(GTK_OBJECT(sink),"fd",outfile,NULL);

  fprintf(stderr,"paranoia is %p, lame is %p, sink is %p\n",paranoia,lame,sink);
  gst_bin_add(GST_BIN(pipeline),paranoia);
  gst_bin_add(GST_BIN(pipeline),lame);
  gst_bin_add(GST_BIN(pipeline),sink);

  gst_element_connect(paranoia,"src",lame,"sink");
  gst_element_connect(lame,"src",sink,"sink");

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(paranoia) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  for (i=0;i<((argc >= 3)?atoi(argv[2]):4500);i++) {
    fprintf(stderr,"\n");
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
