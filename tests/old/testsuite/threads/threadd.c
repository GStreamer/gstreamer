#include <gst/gst.h>
#include <unistd.h>

/* threadc.c
 * this tests if we can make a GstThread, with enough cothreads to stress it
 */

#define MAX_IDENTITIES 29
#define RUNS_PER_IDENTITY 5

gboolean running = FALSE;
gboolean done = FALSE;

static void
construct_pipeline (GstElement * pipeline, gint identities)
{
  GstElement *src, *sink, *identity = NULL;
  GstElement *from;
  int i;

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

  g_object_set (G_OBJECT (src), "num_buffers", 10, "sizetype", 3, NULL);
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
  if (state == GST_STATE_PAUSED && running)
    done = TRUE;
}

int
main (gint argc, gchar * argv[])
{
  int runs = MAX_IDENTITIES * RUNS_PER_IDENTITY;
  int i;
  gulong id;
  GstElement *thread;

  alarm (10);

  gst_init (&argc, &argv);

  for (i = 0; i < runs; ++i) {
    thread = gst_thread_new ("main_thread");
    g_assert (thread);

    /* connect state change signal */
    id = g_signal_connect (G_OBJECT (thread), "state_change",
	G_CALLBACK (state_changed), NULL);
    construct_pipeline (thread, i / RUNS_PER_IDENTITY + 1);

    g_print ("Setting thread to play with %d identities\n",
	i / RUNS_PER_IDENTITY + 1);
    done = FALSE;
    if (gst_element_set_state (thread, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
      g_warning ("failed to go to PLAYING");
    } else {
      g_print ("Waiting for thread PLAYING->PAUSED\n");
      while (!done)		/* do nothing */
	;
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
