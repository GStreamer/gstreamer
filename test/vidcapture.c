#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  int fd;
  GstPipeline *pipeline;
  GstElement *audiosrc, *videosrc, *fdsink, *encoder, *compress;
  GstElementFactory *audiosrcfactory, *fdsinkfactory, *encoderfactory, *compressfactory;
  GstElementFactory *videosrcfactory;
  GList *padlist;

  gst_init(&argc,&argv);

  gst_plugin_load("v4lsrc");
  gst_plugin_load("aviencoder");
  gst_plugin_load("jpeg");

  pipeline = gst_pipeline_new("pipeline");

  audiosrcfactory = gst_elementfactory_find("audiosrc");
  audiosrc = gst_elementfactory_create(audiosrcfactory,"audiosrc");
  videosrcfactory = gst_elementfactory_find("v4lsrc");
  videosrc = gst_elementfactory_create(videosrcfactory,"videosrc");
  compressfactory = gst_elementfactory_find("jpegenc");
  compress = gst_elementfactory_create(compressfactory,"jpegenc");
  encoderfactory = gst_elementfactory_find("aviencoder");
  encoder = gst_elementfactory_create(encoderfactory,"aviencoder");
  gtk_object_set(GTK_OBJECT(videosrc),"width",256,"height",192,NULL);

  gtk_object_set(GTK_OBJECT(encoder),"video","00:MJPG",NULL);

  fd = open(argv[1],O_CREAT|O_RDWR|O_TRUNC);

  fdsinkfactory = gst_elementfactory_find("fdsink");
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(videosrc));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(encoder));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(videosrc,"src"),
                  gst_element_get_pad(compress,"sink"));
  gst_pad_connect(gst_element_get_pad(compress,"src"),
                  gst_element_get_pad(encoder,"video_00"));
  gst_pad_connect(gst_element_get_pad(encoder,"src"),
                  gst_element_get_pad(fdsink,"sink"));

  g_print("\neverything's built, setting it up to be runnable\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  g_print("\nok, runnable, hitting 'play'...\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while(1)
    gst_src_push(GST_SRC(videosrc));
}

