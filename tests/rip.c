#include <gst/gst.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  GstElement *paranoia,*lame,*sink;
  int i;
  int outfile;

  GST_DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  if (argc != 2) argv[1] = "output.mp3";
  unlink(argv[1]);
  outfile = open(argv[1],O_CREAT | O_RDWR | O_TRUNC);
  if (!outfile) {
    fprintf(stderr,"couldn't open file\n");
    exit(1);
  }
  fprintf(stderr,"outfile is fd %d\n",outfile);

  pipeline = gst_pipeline_new("ripper");
  g_return_val_if_fail(pipeline != NULL,1);

  paranoia = gst_elementfactory_make("cdparanoia","paranoia");
  g_return_val_if_fail(paranoia != NULL,2);
  g_object_set(G_OBJECT(paranoia),"paranoia_mode",0,NULL);
//  g_object_set(G_OBJECT(paranoia),"start_sector",0,"end_sector",75,NULL);

  lame = gst_elementfactory_make("lame","lame");
  g_return_val_if_fail(lame != NULL,3);
  g_object_set(G_OBJECT(lame),"bitrate",128,NULL);
  sink = gst_elementfactory_make("fdsink","fdsink");
  g_return_val_if_fail(sink != NULL,4);
  g_object_set(G_OBJECT(sink),"fd",outfile,NULL);

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

  return 0;
}
