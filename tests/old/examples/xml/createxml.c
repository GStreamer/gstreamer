#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;
xmlNsPtr ns;

static void
object_saved (GstObject *object, xmlNodePtr parent, gpointer data)
{
  xmlNodePtr child;

  child = xmlNewChild(parent, ns, "comment", NULL);
  xmlNewChild(child, ns, "text", (gchar *)data);
}

int main(int argc,char *argv[])
{
  GstElement *filesrc, *osssink, *queue, *queue2, *parse, *decode;
  GstElement *bin;
  GstElement *thread, *thread2;

  ns = xmlNewNs (NULL, "http://gstreamer.net/gst-test/1.0/", "test");

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new thread to hold the elements */
  //thread = gst_thread_new("thread");
  thread = gst_elementfactory_make("thread", "thread");
  g_assert(thread != NULL);
  g_signal_connect (G_OBJECT (thread), "object_saved",
		    G_CALLBACK (object_saved),
		    g_strdup ("decoder thread"));

  thread2 = gst_elementfactory_make("thread", "thread2");
  //thread2 = gst_thread_new("thread2");
  g_assert(thread2 != NULL);
  g_signal_connect (G_OBJECT (thread2), "object_saved",
		    G_CALLBACK (object_saved),
		    g_strdup ("render thread"));

  /* create a new bin to hold the elements */
  bin = gst_bin_new("bin");
  g_assert(bin != NULL);

  /* create a disk reader */
  filesrc = gst_elementfactory_make("filesrc", "disk_source");
  g_assert(filesrc != NULL);
  g_object_set(G_OBJECT(filesrc),"location", argv[1],NULL);

  queue = gst_elementfactory_make("queue", "queue");
  queue2 = gst_elementfactory_make("queue", "queue2");

  /* and an audio sink */
  osssink = gst_elementfactory_make("osssink", "play_audio");
  g_assert(osssink != NULL);

  parse = gst_elementfactory_make("mp3parse", "parse");
  decode = gst_elementfactory_make("mpg123", "decode");

  /* add objects to the main bin */
  gst_bin_add(GST_BIN(bin), filesrc);
  gst_bin_add(GST_BIN(bin), queue);

  gst_bin_add(GST_BIN(thread), parse);
  gst_bin_add(GST_BIN(thread), decode);
  gst_bin_add(GST_BIN(thread), queue2);

  gst_bin_add(GST_BIN(thread2), osssink);

  gst_pad_connect(gst_element_get_pad(filesrc,"src"),
                  gst_element_get_pad(queue,"sink"));

  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_connect(gst_element_get_pad(decode,"src"),
                  gst_element_get_pad(queue2,"sink"));

  gst_pad_connect(gst_element_get_pad(queue2,"src"),
                  gst_element_get_pad(osssink,"sink"));

  gst_bin_add(GST_BIN(bin), thread);
  gst_bin_add(GST_BIN(bin), thread2);

  xmlSaveFile("xmlTest.gst", gst_xml_write(GST_ELEMENT(bin)));

  exit(0);
}

