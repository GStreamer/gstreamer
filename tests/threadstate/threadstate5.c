#include <stdlib.h>
#include <gst/gst.h>

/* this pipeline is:
 * { fakesrc ! { queue ! fakesink } }
 */

int
main (int argc, char *argv[])
{
  GstElement *fakesrc, *fakesink;
  GstElement *thread, *thread2;
  GstElement *queue;
  gint x;

  gst_init (&argc, &argv);

  thread = gst_thread_new ("thread");
  g_assert (thread != NULL);

  thread2 = gst_thread_new ("thread");
  g_assert (thread2 != NULL);

  queue = gst_element_factory_make ("queue", "the_queue");
  g_assert (queue != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fake_sink");
  g_assert (fakesink != NULL);

  gst_bin_add_many (GST_BIN (thread), fakesrc, thread2, NULL);
  gst_bin_add_many (GST_BIN (thread2), queue, fakesink, NULL);

  gst_element_add_ghost_pad (thread2, gst_element_get_pad (queue, "sink"),
      "sink");
  gst_element_link_many (queue, fakesink, NULL);
  gst_element_link_many (fakesrc, thread2, NULL);

  for (x = 0; x < 10; x++) {
    g_print ("playing %d\n", x);
    gst_element_set_state (thread, GST_STATE_PLAYING);
    g_usleep (G_USEC_PER_SEC);

    g_print ("nulling %d\n", x);
    gst_element_set_state (thread, GST_STATE_NULL);
    g_usleep (G_USEC_PER_SEC);
  }

  exit (0);
}
