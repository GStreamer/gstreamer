#include <stdlib.h>
#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *filesrc, *osssink, *parse, *decode, *queue;
  GstElement *pipeline;
  GstElement *thread;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new thread to hold the elements */
  thread = gst_thread_new ("thread");
  g_assert (thread != NULL);

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  parse = gst_element_factory_make ("mp3parse", "parse");
  decode = gst_element_factory_make ("mad", "decode");

  queue = gst_element_factory_make ("queue", "queue");

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (pipeline), filesrc, parse, decode, queue, NULL);

  gst_bin_add (GST_BIN (thread), osssink);
  gst_bin_add (GST_BIN (pipeline), thread);

  gst_element_link_many (filesrc, parse, decode, queue, osssink, NULL);

  /* start playing */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  exit (0);
}
