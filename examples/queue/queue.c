#include <stdlib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink, *parse, *decode, *queue;
  GstElement *bin;
  GstElement *thread;

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new thread to hold the elements */
  thread = gst_thread_new("thread");
  g_assert(thread != NULL);

  /* create a new bin to hold the elements */
  bin = gst_bin_new("bin");
  g_assert(bin != NULL);

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  g_assert(disksrc != NULL);
  gtk_object_set(GTK_OBJECT(disksrc),"location", argv[1],NULL);

  parse = gst_elementfactory_make("mp3parse", "parse");
  decode = gst_elementfactory_make("mpg123", "decode");

  queue = gst_elementfactory_make("queue", "queue");

  /* and an audio sink */
  audiosink = gst_elementfactory_make("audiosink", "play_audio");
  g_assert(audiosink != NULL);

  gst_bin_use_cothreads (GST_BIN (bin), TRUE);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin), disksrc);
  gst_bin_add(GST_BIN(bin), parse);
  gst_bin_add(GST_BIN(bin), decode);
  gst_bin_add(GST_BIN(bin), queue);

  gst_bin_add(GST_BIN(thread), audiosink);

  gst_bin_add(GST_BIN(bin), thread);
  
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_connect(gst_element_get_pad(decode,"src"),
                  gst_element_get_pad(queue,"sink"));
  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  /* start playing */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

  while (gst_bin_iterate(GST_BIN(bin)));

  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);

  exit(0);
}

