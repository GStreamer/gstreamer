/*
 * Test three ways of going non-lineairly to PLAYING. Both tests have a
 * thread containing a fakesrc/sink.
 *
 * Test1 tests by adding fakesrc/fakesink, setting fakesrc to PLAYING
 * (which should increment the container state) and then synchronizing
 * state and see if the bin iterates. This reflects bug #123775.
 *
 * Test2 does the same, but emits EOS directly. This will (in case of
 * race conditions) sometimes lead to a state-change before the previous
 * one succeeded. This bug is not fixed yet (999998).
 *
 * Test3 tests by adding fakesrc, putting thread to PLAYING, adding
 * fakesink, syncing state and see if it iterates. The group is sometimes
 * activated before fakesink is added to the bin, which is a bug in opt
 * and a race in core that is not fixed yet (999999).
 */

#include <gst/gst.h>

static GstElement *pipeline, *fakesrc, *fakesink;

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

#if TESTNUM != 123775
static void
cb_eos (gpointer data)
{
  g_print ("Received EOS\n");

  g_idle_add ((GSourceFunc) cb_quit, NULL);
}
#else
static void
cb_data (gpointer data)
{
  static gboolean first = TRUE;

  g_print ("Received data\n");

  if (first) {
    first = FALSE;
    g_idle_add ((GSourceFunc) cb_quit, NULL);
  }
}
#endif

static void
cb_state (GstElement * element, GstState old_state,
    GstState new_state, gpointer data)
{
  g_print ("Changed state from %d to %d\n", old_state, new_state);
}

static gboolean
cb_play (gpointer data)
{
  GstStateChangeReturn res;

#if TESTNUM != 999999
  g_print ("Setting state on fakesrc\n");
  gst_element_set_state (fakesrc, GST_STATE_PLAYING);
  g_print ("Done\n");
#else
  g_print ("Setting state on pipeline w/o fakesink\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Adding fakesink\n");
  gst_bin_add (GST_BIN (pipeline), fakesink);
  g_print ("Done\n");
#endif
  g_print ("Syncing state in pipeline\n");
  res = gst_bin_sync_children_state (GST_BIN (data));
  g_assert (res == GST_STATE_CHANGE_SUCCESS);
  g_print ("Set to playing correctly: %d\n", GST_STATE (pipeline));

  /* once */
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  gint id;

  gst_init (&argc, &argv);

  g_print ("Will do a test to see if bug %d is fixed\n", TESTNUM);

  pipeline = gst_thread_new ("p");
  g_signal_connect (pipeline, "state-change", G_CALLBACK (cb_state), NULL);
  fakesrc = gst_element_factory_make ("fakesrc", "src");
  fakesink = gst_element_factory_make ("fakesink", "sink");
#if TESTNUM != 123775
  g_object_set (G_OBJECT (fakesrc), "num-buffers", 0, NULL);
  g_signal_connect (pipeline, "eos", G_CALLBACK (cb_eos), NULL);
#else
  g_object_set (G_OBJECT (fakesink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (cb_data), NULL);
#endif

#if TESTNUM != 999999
  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
#else
  gst_bin_add (GST_BIN (pipeline), fakesrc);
#endif

  gst_element_link (fakesrc, fakesink);
  g_idle_add ((GSourceFunc) cb_play, pipeline);

  /* give 5 seconds */
  id = g_timeout_add (5000, (GSourceFunc) cb_timeout, NULL);
  g_print ("Enter mainloop\n");
  gst_main ();
  g_source_remove (id);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_print ("Done with test to show bug %d, fixed correctly\n", TESTNUM);

  return 0;
}
