#include <gst/gst.h>

int 
main (int argc, gchar *argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc, *fakesink, *identity;
  GstElement *queue, *thread;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main");
  g_assert (pipeline != NULL);

  queue = gst_elementfactory_make ("queue", "queue");
  g_assert (queue);
  thread = gst_elementfactory_make ("thread", "thread");
  g_assert (thread);
  fakesrc = gst_elementfactory_make ("fakesrc", "fakesrc");
  g_assert (fakesrc);
  fakesink = gst_elementfactory_make ("fakesink", "fakesink");
  g_assert (fakesink);
  identity = gst_elementfactory_make ("identity", "identity");
  g_assert (identity);

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), identity);
  gst_bin_add (GST_BIN (pipeline), queue);

  gst_bin_add (GST_BIN (thread), fakesink);
  gst_bin_add (GST_BIN (pipeline), thread);

  gst_element_connect (fakesrc, "src", identity, "sink");
  gst_element_connect (identity, "src", queue, "sink");
  gst_element_connect (queue, "src", fakesink, "sink");

  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_bin_iterate (GST_BIN (pipeline));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
