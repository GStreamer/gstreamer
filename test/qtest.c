#include <glib.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

/* we don't need a lock around the application's state yet, since it's 1
   bit.  as it gets more fleshed in, we'll need a lock so the callbacks
   don't screw around with state unexpectedly */
static gboolean playing = TRUE;

void eof(GstElement *src) {
  GST_DEBUG(0,"have EOF\n");
  playing = FALSE;
}

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  GstElement *decodethread;
  GstElementFactory *srcfactory;
  GstElement *src;
  GstElementFactory *decodefactory;
  GstElement *decode;
  GstElementFactory *queuefactory;
  GstElement *queue;
  GstElement *playthread;
  GstElementFactory *sinkfactory;
  GstElement *sink;

  gst_init(&argc,&argv);

  /* first create the main pipeline */
  pipeline = GST_ELEMENT(gst_pipeline_new("pipeline"));

  /* then the decode thread, source, and decoder */
  decodethread = gst_thread_new("decodethread");

  srcfactory = gst_elementfactory_find("disksrc");
  src = gst_elementfactory_create(srcfactory,"src");
  g_object_set(G_OBJECT(src),"location",argv[1],NULL);
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(src));

  _gst_plugin_spew = TRUE;

  if (argc > 2)
    gst_plugin_load(argv[2]);
  else
    gst_plugin_load_all();
  decodefactory = gst_elementfactory_find("mpg123");
  decode = gst_elementfactory_create(decodefactory,"decode");
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(decode));
  gst_element_add_ghost_pad(GST_ELEMENT(decodethread),
                            gst_element_get_pad(decode,"src"),"src");

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(decode,"sink"));

  /* then the play thread and sink */
  playthread = gst_thread_new("playthread");

  sinkfactory = gst_elementfactory_find("osssink");
  sink = gst_elementfactory_create(sinkfactory,"sink");
  gst_bin_add(GST_BIN(playthread),GST_ELEMENT(sink));
  gst_element_add_ghost_pad(GST_ELEMENT(playthread),
                            gst_element_get_pad(sink,"sink"),"sink");

  /* create the queue */
  queuefactory = gst_elementfactory_find("queue");
  queue = gst_elementfactory_create(queuefactory,"queue");

  /* add threads to the main pipeline */
  gst_bin_add(GST_BIN(pipeline),decodethread);
  gst_bin_add(GST_BIN(pipeline),queue);
  gst_bin_add(GST_BIN(pipeline),playthread);

  gst_pad_connect(gst_element_get_pad(decodethread,"src"),
//                  gst_element_get_pad(queue,"sink"));
//  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(playthread,"sink"));

  g_signal_connect(G_OBJECT(src),"eof",
                     G_CALLBACK(eof),NULL);

  g_print("\nsetting up the decode thread to *NOT* thread\n");
//  gtk_object_set(GTK_OBJECT(decodethread),"create_thread",TRUE,NULL);
  g_object_set(G_OBJECT(playthread),"create_thread",FALSE,NULL);

  g_print("\neverything's built, setting it up to be runnable\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);

  g_print("\nok, runnable, hitting 'play'...\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  g_print("\niterating on %p and %p\n",decodethread,playthread);
  while (playing) {
    gst_bin_iterate(GST_BIN(playthread));
    /* buffers got wedged in the queue, unstick them */
//    while (((GstQueue *)queue)->buffers_queued)
//      gst_connection_push(GST_CONNECTION(queue));
//      gst_thread_iterate(GST_THREAD(playthread));
//    g_print("stuffed and unstuck the queue\n");
//    sleep(1);
  }
  return 0;
}
