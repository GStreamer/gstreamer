#include <stdlib.h>
#include <gst/gst.h>

/* this pipeline is:
 * { fakesrc ! fakesink }
 */


int
main (int argc, char *argv[])
{
  GstElement *fakesrc, *fakesink;
  GstElement *thread;
  gint x;

  gst_init (&argc, &argv);

  thread = gst_thread_new ("thread");
  g_assert (thread != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fake_sink");
  g_assert (fakesink != NULL);

  gst_bin_add_many (GST_BIN (thread), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  for (x = 0; x < 10; x++) {
    g_print ("playing %d\n", x);
    gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PLAYING);
    g_usleep (G_USEC_PER_SEC);

    g_print ("pausing %d\n", x);
    gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PAUSED);
    g_usleep (G_USEC_PER_SEC);
  }

  exit (0);
}
