#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

/* eos will be called when the src element has an end of stream */
void
eos (GstElement * element, gpointer data)
{
  g_print ("have eos, quitting\n");

  playing = FALSE;
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc, *osssink, *queue;
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

  /* create a new bin to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  g_signal_connect (G_OBJECT (filesrc), "eos", G_CALLBACK (eos), thread);

  queue = gst_element_factory_make ("queue", "queue");

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  /* add objects to the main pipeline */
  /*
     gst_pipeline_add_src(GST_PIPELINE(pipeline), filesrc);
     gst_pipeline_add_sink(GST_PIPELINE(pipeline), queue);

     gst_bin_add(GST_BIN(thread), osssink);

     gst_pad_link(gst_element_get_pad(queue,"src"),
     gst_element_get_pad(osssink,"sink"));

     if (!gst_pipeline_autoplug(GST_PIPELINE(pipeline))) {
     g_print("cannot autoplug pipeline\n");
     exit(-1);
     }
   */

  gst_bin_add (GST_BIN (pipeline), thread);

  /* make it ready */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  /* start playing */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate (GST_BIN (pipeline));
  }

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  exit (0);
}
