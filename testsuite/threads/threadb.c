#include <gst/gst.h>

/* threadb.c
 * this tests if we can make a GstThread, put some stuff in it,
 * dispatch it, and let it run from a main gst loop
 * we repeat the main loop a hundred times to test thread reuse
 * underneath GstThread
 */

gboolean running = FALSE;

static void
construct_pipeline (GstElement * pipeline)
{
  GstElement *src, *sink, *identity;

  src = gst_element_factory_make ("fakesrc", NULL);
  identity = gst_element_factory_make ("identity", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (src);
  g_assert (identity);
  g_assert (sink);

  gst_element_link_many (src, identity, sink, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, identity, sink, NULL);

  g_object_set (G_OBJECT (src), "num_buffers", 5, NULL);
}

void
state_changed (GstElement * el, gint arg1, gint arg2, gpointer user_data)
{
  GstElementState state = gst_element_get_state (el);

  g_print ("element %s has changed state to %s\n",
      GST_ELEMENT_NAME (el), gst_element_state_get_name (state));
  if (state == GST_STATE_PLAYING)
    running = TRUE;
  /* if we move from PLAYING to PAUSED, we're done */
  if (state == GST_STATE_PAUSED && running) {
    running = FALSE;
    gst_main_quit ();
  }
}

int
main (gint argc, gchar * argv[])
{
  int runs = 100;
  int i;
  gulong id;
  GstElement *thread;

  gst_init (&argc, &argv);

  for (i = 0; i < runs; ++i) {
    thread = gst_thread_new ("main_thread");
    g_assert (thread);

    /* connect state change signal */
    id = g_signal_connect (G_OBJECT (thread), "state_change",
        G_CALLBACK (state_changed), NULL);
    construct_pipeline (thread);

    g_print ("Setting thread to play\n");
    gst_element_set_state (thread, GST_STATE_PLAYING);

    g_print ("Going into the main GStreamer loop\n");
    gst_main ();
    g_print ("Coming out of the main GStreamer loop\n");
    g_signal_handler_disconnect (G_OBJECT (thread), id);
    gst_element_set_state (thread, GST_STATE_NULL);
    g_print ("Unreffing thread\n");
    g_object_unref (G_OBJECT (thread));
    running = FALSE;
  }

  return 0;
}
