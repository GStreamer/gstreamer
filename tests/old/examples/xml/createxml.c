#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

static void
object_saved (GstObject *object, xmlNodePtr parent, gpointer data)
{
  xmlNodePtr child;
  xmlNsPtr ns;
  
  /* i'm not sure why both of these Ns things are necessary, but they are */
  ns = xmlNewNs (NULL, "http://gstreamer.net/gst-test/1.0/", "test");
  child = xmlNewChild(parent, ns, "comment", NULL);
  xmlNewNs (child, "http://gstreamer.net/gst-test/1.0/", "test");
  
  xmlNewChild(child, NULL, "text", (gchar *)data);
}

int main(int argc,char *argv[])
{
  GstElement *filesrc, *osssink, *queue, *queue2, *decode;
  GstElement *pipeline;
  GstElement *thread, *thread2;

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create new threads to hold the elements */
  thread = gst_elementfactory_make ("thread", "thread");
  g_assert (thread != NULL);
  thread2 = gst_elementfactory_make ("thread", "thread2");
  g_assert (thread2 != NULL);

  /* these signals will allow us to save custom tags with the gst xml output */
  g_signal_connect (G_OBJECT (thread), "object_saved",
		    G_CALLBACK (object_saved),
		    g_strdup ("decoder thread"));
  g_signal_connect (G_OBJECT (thread2), "object_saved",
		    G_CALLBACK (object_saved),
		    g_strdup ("render thread"));
  
  /* create a new bin to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a disk reader */
  filesrc = gst_elementfactory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  queue = gst_elementfactory_make ("queue", "queue");
  queue2 = gst_elementfactory_make ("queue", "queue2");

  /* and an audio sink */
  osssink = gst_elementfactory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  decode = gst_elementfactory_make ("mad", "decode");
  g_assert (decode != NULL);

  /* add objects to the main pipeline */
  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), queue);

  gst_bin_add (GST_BIN (thread), decode);
  gst_bin_add (GST_BIN (thread), queue2);

  gst_bin_add (GST_BIN (thread2), osssink);
  
  gst_pad_connect (gst_element_get_pad (filesrc,"src"),
                   gst_element_get_pad (queue,"sink"));

  gst_pad_connect (gst_element_get_pad (queue,"src"),
                   gst_element_get_pad (decode,"sink"));
  gst_pad_connect (gst_element_get_pad (decode,"src"),
                   gst_element_get_pad (queue2,"sink"));

  gst_pad_connect (gst_element_get_pad (queue2,"src"),
                   gst_element_get_pad (osssink,"sink"));

  gst_bin_add (GST_BIN (pipeline), thread);
  gst_bin_add (GST_BIN (pipeline), thread2);

  /* write the bin to stdout */
  gst_xml_write_file (GST_ELEMENT (pipeline), stdout);

  /* write the bin to a file */
  gst_xml_write_file (GST_ELEMENT (pipeline), fopen ("xmlTest.gst", "w"));

  exit (0);
}

