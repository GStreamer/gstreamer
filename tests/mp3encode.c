#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *src,*lame,*sink;
  int bitrate = 128000;
  int fdin = -1;
  int fdout = -1;
  int i;

  gst_init(&argc,&argv);

  for (i=1;i<argc;i++) {
    fprintf(stderr,"arg is %s\n",argv[i]);
    if (argv[i][0] == '-') {
      fprintf(stderr,"  starts with -\n");
      switch (argv[i][1]) {
        case 'b': bitrate = atoi(argv[++i]);break;
        case 0: {
          if (fdin == -1) fdin = STDIN_FILENO;
          else if (fdout == -1) fdout = STDOUT_FILENO;
          else fprintf(stderr,"unknown argument\n");exit(1);
          break;
        }
        default: fprintf(stderr,"unknown argument\n");exit(1);
      }
    } else {
      fprintf(stderr,"  probably a filename\n");
      if (fdin == -1) {
        fdin = open(argv[i],O_RDONLY);
        if (fdin <= 0) {
          fprintf(stderr,"can't open file '%s' for read\n",argv[i]);
          exit(1);
        }
        fprintf(stderr,"  openned file %s for read, fd %d\n",argv[i],fdin);
      } else if (fdout == -1) {
        unlink(argv[i]);
        fdout = open(argv[i],O_CREAT|O_RDWR|O_TRUNC);
        if (fdout <= 0) {
          fprintf(stderr,"can't open file '%s' for write\n",argv[i]);
          exit(1);
        }
        fprintf(stderr,"  openned file %s for write, fd %d\n",argv[i],fdout);
      } else {
        fprintf(stderr,"unknown argument\n");exit(1);
      }
    }
  }

  pipeline = GST_PIPELINE (gst_pipeline_new("mp3encode"));

  src = gst_elementfactory_make("fdsrc","src");
  g_return_val_if_fail(src != NULL,1);
  g_object_set(G_OBJECT(src),"location",fdin,NULL);

  lame = gst_elementfactory_make("lame","encoder");
  g_return_val_if_fail(lame != NULL,2);
  g_object_set(G_OBJECT(lame),"bitrate",bitrate,NULL);

  sink = gst_elementfactory_make("fdsink","sink");
  g_return_val_if_fail(sink != NULL,3);
  g_object_set(G_OBJECT(src),"fd",fdout,NULL);

  gst_bin_add(GST_BIN(pipeline),src);
  gst_bin_add(GST_BIN(pipeline),lame);
  gst_bin_add(GST_BIN(pipeline),sink);

  gst_element_connect(src,"src",lame,"sink");
  gst_element_connect(lame,"src",sink,"sink");


  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(src) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  for (i=0;i<100;i++) {
    fprintf(stderr,"\n");
    gst_bin_iterate(GST_BIN(pipeline));
  }

  return 0;
}
