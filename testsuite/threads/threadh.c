#include <unistd.h>
#include <gst/gst.h>

static GstElement *thread, *pipeline;
static GstElement *src, *sink;

static void
handoff_src (GstElement * element)
{
  g_print ("identity handoff\n");

  if (gst_element_set_state (thread,
          GST_STATE_PAUSED) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();

  if (gst_element_set_state (sink, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_remove (GST_BIN (thread), src);
}

int
main (int argc, char **argv)
{
  gst_init (&argc, &argv);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");

  thread = gst_element_factory_make ("thread", "thread");
  g_assert (thread);

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);
  g_signal_connect (G_OBJECT (src), "handoff", (GCallback) handoff_src, NULL);
  g_object_set (G_OBJECT (src), "signal-handoffs", TRUE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);

  gst_bin_add (GST_BIN (pipeline), thread);

  gst_bin_add_many (GST_BIN (thread), src, sink, NULL);
  if (!gst_element_link_many (src, sink, NULL))
    g_assert_not_reached ();

  /* run a bit */
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();

  sleep (2);

  return 0;
}
