#include <stdlib.h>
#include <gst/gst.h>

/* this pipeline is:
 * { filesrc ! mad ! osssink }
 */

/* eos will be called when the src element has an end of stream */
void
eos (GstElement * element, gpointer data)
{
  GstThread *thread = GST_THREAD (data);

  g_print ("have eos, quitting\n");

  /* stop the bin */
  gst_element_set_state (GST_ELEMENT (thread), GST_STATE_NULL);

  gst_main_quit ();
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc, *osssink;
  GstElement *thread;
  GstElement *mad;
  gint x;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new thread to hold the elements */
  thread = gst_thread_new ("thread");
  g_assert (thread != NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  g_signal_connect (G_OBJECT (filesrc), "eos", G_CALLBACK (eos), thread);

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  /* did i mention that this is an mp3 player? */
  mad = gst_element_factory_make ("mad", "mp3_decoder");
  g_assert (mad != NULL);

  gst_bin_add_many (GST_BIN (thread), filesrc, mad, osssink, NULL);
  gst_element_link_many (filesrc, mad, osssink, NULL);

  for (x = 0; x < 10; x++) {
    g_print ("playing %d\n", x);
    gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PLAYING);
    g_usleep (G_USEC_PER_SEC * 2);

    g_print ("pausing %d\n", x);
    gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PAUSED);
    g_usleep (G_USEC_PER_SEC * 2);
  }

  exit (0);
}
