#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  int fd;
  GstPipeline *pipeline;
  GstElement *audiosrc, *videosrc, *fdsink, *encoder, *compress, *video_queue, *video_thread;
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
  gtk_object_set(GTK_OBJECT(videosrc),"width",320,"height",240,NULL);
  gtk_object_set(GTK_OBJECT(videosrc),"format",9,NULL);

  gtk_object_set(GTK_OBJECT(encoder),"video","00:I420",NULL);

  fd = open(argv[1],O_CREAT|O_RDWR|O_TRUNC);

  fdsinkfactory = gst_elementfactory_find("fdsink");
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(videosrc));

  /* add objects to the main pipeline */
  video_thread = gst_thread_new("video_thread");
  g_return_if_fail(video_thread != NULL);
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(compress));
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encoder));
  gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(fdsink));


  /* connect src to sink */
  gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
  //                gst_element_get_pad(compress,"sink"));
  //gst_pad_connect(gst_element_get_pad(compress,"src"),
                  gst_element_get_pad(encoder,"video_00"),"video_00");
  gst_pad_connect(gst_element_get_pad(encoder,"src"),
                  gst_element_get_pad(fdsink,"sink"));


  // construct queue and connect everything in the main pipeline
  video_queue = gst_elementfactory_make("queue","video_queue");
  gtk_object_set(GTK_OBJECT(video_queue),"max_level",30,NULL);
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
  gst_pad_connect(gst_element_get_pad(videosrc, "src"),
                  gst_element_get_pad(video_queue,"sink"));
  gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                  gst_element_get_pad(video_thread,"video_00"));

  gtk_object_set(GTK_OBJECT(video_thread),"create_thread",TRUE,NULL);
  g_print("\neverything's built, setting it up to be runnable\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);
  g_print("\nok, runnable, hitting 'play'...\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while(1)
    gst_bin_iterate(GST_BIN(pipeline));
}

