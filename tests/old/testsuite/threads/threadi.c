/*
 * Test two ways of going non-lineairly to PLAYING. Both tests have a thread
 * containing a fakesrc/sink and a containing thread.
 *
 * Test 1 tests by adding fakesrc, putting thread to PLAYING, adding
 * fakesink, syncing state and see if it iterates.
 *
 * Test2 tests by adding fakesrc/fakesink, setting fakesrc to PLAYING
 * (which should increment the container state) and then synchronizing
 * state. This reflects bug #123775.
 */

#include <gst/gst.h>

static GstElement *pipeline, *fakesrc, *fakesink;
gboolean bug = FALSE;

static gboolean
cb_timeout (gpointer data)
{
  g_assert_not_reached ();

  return FALSE;
}

static gboolean
cb_quit (gpointer data)
{
  gst_main_quit ();

  g_print ("Quit mainloop\n");

  /* once */
  return FALSE;
}

static void
cb_eos (gpointer data)
{
  g_print ("Received EOS\n");

  g_idle_add ((GSourceFunc) cb_quit, NULL);
}

static void
cb_state (GstElement * element, GstElementState old_state,
    GstElementState new_state, gpointer data)
{
  g_print ("Changed state from %d to %d\n", old_state, new_state);
}

static gboolean
cb_play (gpointer data)
{
  GstElementStateReturn res;

  if (bug) {
    g_print ("Setting state\n");
    gst_element_set_state (fakesrc, GST_STATE_PLAYING);
    g_print ("Done\n");
  } else {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_bin_add (GST_BIN (pipeline), fakesink);
  }
  g_print ("Syncing state\n");
  res = gst_bin_sync_children_state (GST_BIN (data));
  g_assert (res == GST_STATE_SUCCESS);

  g_print ("Set to playing correctly: %d\n", GST_STATE (pipeline));

  /* once */
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  gint n, id;

  gst_init (&argc, &argv);

  for (n = 0; n < 2; n++) {
    pipeline = gst_thread_new ("p");
    g_signal_connect (pipeline, "state-change", G_CALLBACK (cb_state), NULL);
    fakesrc = gst_element_factory_make ("fakesrc", "src");
    g_object_set (G_OBJECT (fakesrc), "num-buffers", 1, NULL);
    fakesink = gst_element_factory_make ("fakesink", "sink");
    if (bug) {
      gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
    } else {
      gst_bin_add (GST_BIN (pipeline), fakesrc);
    }
    gst_element_link (fakesrc, fakesink);
    g_signal_connect (pipeline, "eos", G_CALLBACK (cb_eos), NULL);
    g_idle_add ((GSourceFunc) cb_play, pipeline);

    /* give 5 seconds */
    id = g_timeout_add (5000, (GSourceFunc) cb_timeout, NULL);
    g_print ("Enter mainloop\n");
    gst_main ();
    g_source_remove (id);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (pipeline));

    g_print ("Done with reproduce-bug-123775=%s\n", bug ? "true" : "false");
    bug = !bug;
  }

  return 0;
}
