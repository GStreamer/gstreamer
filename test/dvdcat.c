#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  GstElement *src, *sink;
  int fd;

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
//  gst_plugin_load("dvdsrc");

  fd = creat("output.vob",0644);

  pipeline = gst_elementfactory_make("pipeline","dvdcat");
  g_return_if_fail(pipeline != NULL);

  src = gst_elementfactory_make("dvdsrc","src");
  g_return_if_fail(src != NULL);

  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  if (argc >= 3)
    gtk_object_set(GTK_OBJECT(src),"offset",atoi(argv[2]),NULL);

  sink = gst_elementfactory_make("fdsink","sink");
  g_return_if_fail(sink != NULL);
  gtk_object_set(GTK_OBJECT(sink),"fd",fd,NULL);

  // construct the outer pipeline
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(sink));
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(sink,"sink"));

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while (1)
    gst_bin_iterate(GST_BIN(pipeline));
}
