#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gst/gst.h>


void eof(GstSrc *src) {
   g_print("have eof, quitting\n");
   exit(0);
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElementFactory *srcfactory, *pipefactory, *sinkfactory;
  GstElement *src, *pipe, *sink;
  int fd;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  srcfactory = gst_elementfactory_find("disksrc");
  g_return_val_if_fail(srcfactory != NULL, -1);
  pipefactory = gst_elementfactory_find("pipefilter");
  g_return_val_if_fail(pipefactory != NULL, -1);
  sinkfactory = gst_elementfactory_find("fdsink");
  g_return_val_if_fail(sinkfactory != NULL, -1);

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  pipe = gst_elementfactory_create(pipefactory,"pipe");
  g_return_val_if_fail(pipe != NULL, -1);
  sink = gst_elementfactory_create(sinkfactory,"fdsink");
  g_return_val_if_fail(sink != NULL, -1);

  fd = open(argv[2],O_CREAT|O_RDWR|O_TRUNC);
  gtk_object_set(GTK_OBJECT(sink),"fd",fd,NULL);

  gtk_signal_connect(GTK_OBJECT(src),"eos",
	                       GTK_SIGNAL_FUNC(eof),NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(pipe));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(sink));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(pipe,"sink"));
  gst_pad_connect(gst_element_get_pad(pipe,"src"),
                  gst_element_get_pad(sink,"sink"));

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);

  g_print("about to enter loop\n");
  while (1) {
    gst_src_push(GST_SRC(src));
  }
}
