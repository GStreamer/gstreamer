#include <gst/gst.h>

gboolean playing;

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink, *queue, *queue2, *parse, *decode;
  GstElement *bin;
  GstElement *thread, *thread2;

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  gst_init(&argc,&argv);

  /* create a new thread to hold the elements */
  //thread = gst_thread_new("thread");
  thread = gst_elementfactory_make("thread", "thread");
  g_assert(thread != NULL);
  thread2 = gst_elementfactory_make("thread", "thread2");
  //thread2 = gst_thread_new("thread2");
  g_assert(thread2 != NULL);

  /* create a new bin to hold the elements */
  bin = gst_bin_new("bin");
  g_assert(bin != NULL);

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  g_assert(disksrc != NULL);
  gtk_object_set(GTK_OBJECT(disksrc),"location", argv[1],NULL);

  queue = gst_elementfactory_make("queue", "queue");
  queue2 = gst_elementfactory_make("queue", "queue2");

  /* and an audio sink */
  audiosink = gst_elementfactory_make("audiosink", "play_audio");
  g_assert(audiosink != NULL);

  parse = gst_elementfactory_make("mp3parse", "parse");
  decode = gst_elementfactory_make("mpg123", "decode");

  /* add objects to the main bin */
  gst_bin_add(GST_BIN(bin), disksrc);
  gst_bin_add(GST_BIN(bin), queue);

  gst_bin_add(GST_BIN(thread), parse);
  gst_bin_add(GST_BIN(thread), decode);
  gst_bin_add(GST_BIN(thread), queue2);

  gst_bin_add(GST_BIN(thread2), audiosink);
  
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(queue,"sink"));

  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_connect(gst_element_get_pad(decode,"src"),
                  gst_element_get_pad(queue2,"sink"));

  gst_pad_connect(gst_element_get_pad(queue2,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  gst_bin_add(GST_BIN(bin), thread);
  gst_bin_add(GST_BIN(bin), thread2);

  xmlSaveFile("xmlTest.gst", gst_xml_write(GST_ELEMENT(bin)));

  exit(0);
}

