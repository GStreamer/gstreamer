#include <gst/gst.h>

/* threadc.c
 * this tests if we can make a GstThread, with enough cothreads to stress it
 */

gboolean running = FALSE;
gboolean can_quit = FALSE;

static void
construct_pipeline (GstElement * pipeline, gint identities)
{
  GstElement *src, *sink;
  GstElement *identity = NULL;
  GstElement *from;
  int i;

  identity = NULL;
  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (src);
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  from = src;

  for (i = 0; i < identities; ++i) {
    identity = gst_element_factory_make ("identity", NULL);
    g_assert (identity);
    gst_bin_add (GST_BIN (pipeline), identity);
    gst_element_link (from, identity);
    from = identity;
  }
  gst_element_link (identity, sink);

  g_object_set (G_OBJECT (src), "num_buffers", 1, "sizetype", 3, NULL);
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
    while (!can_quit);
    can_quit = FALSE;
    g_print ("quitting main loop\n");
    gst_main_quit ();
  }
}

int
main (gint argc, gchar * argv[])
{
  int runs = 290;
  int i;
  gulong id;
  GstElement *thread;

  gst_init (&argc, &argv);

  for (i = 90; i < runs; ++i) {
    thread = gst_thread_new ("main_thread");
    g_assert (thread);

    /* connect state change signal */
    id = g_signal_connect (G_OBJECT (thread), "state_change",
        G_CALLBACK (state_changed), NULL);
    construct_pipeline (thread, i / 10 + 1);

    g_print ("Setting thread to play with %d identities\n", i / 10 + 1);
    if (gst_element_set_state (thread, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
      g_error ("Failed setting thread to play\n");
    } else {
      g_print ("Going into the main GStreamer loop\n");
      can_quit = TRUE;          /* we don't want gst_main_quit called before gst_main */
      gst_main ();
    }
    running = FALSE;
    g_print ("Coming out of the main GStreamer loop\n");
    g_signal_handler_disconnect (G_OBJECT (thread), id);
    gst_element_set_state (thread, GST_STATE_NULL);
    g_print ("Unreffing thread\n");
    g_object_unref (G_OBJECT (thread));
  }

  return 0;
}
