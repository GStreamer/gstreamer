#include <stdlib.h>
#include <gst/gst.h>

gboolean playing = TRUE;

static void
handoff_signal (GstElement * element, GstBuffer * buf)
{
  g_print ("handoff \"%s\" %" G_GINT64_FORMAT "\n",
      gst_element_get_name (element), GST_BUFFER_TIMESTAMP (buf));
}

static void
eos_signal (GstElement * element)
{
  g_print ("eos received from \"%s\"\n", gst_element_get_name (element));

  playing = FALSE;
}

int
main (int argc, char *argv[])
{
  GstBin *pipeline;
  GstElement *src, *tee, *identity1, *identity2, *aggregator, *sink;

  gst_init (&argc, &argv);

  pipeline = GST_BIN (gst_pipeline_new ("pipeline"));
  g_return_val_if_fail (pipeline != NULL, 1);

  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "num_buffers", 40, NULL);
  g_return_val_if_fail (src != NULL, 2);
  tee = gst_element_factory_make ("tee", "tee");
  g_return_val_if_fail (tee != NULL, 3);
  identity1 = gst_element_factory_make ("identity", "identity0");
  g_return_val_if_fail (identity1 != NULL, 3);
  identity2 = gst_element_factory_make ("identity", "identity1");
  g_object_set (G_OBJECT (identity2), "duplicate", 2, NULL);
  g_object_set (G_OBJECT (identity2), "loop_based", TRUE, NULL);
  g_return_val_if_fail (identity2 != NULL, 3);
  aggregator = gst_element_factory_make ("aggregator", "aggregator");
  g_object_set (G_OBJECT (aggregator), "sched", 4, NULL);
  g_return_val_if_fail (aggregator != NULL, 3);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_return_val_if_fail (sink != NULL, 4);

  gst_bin_add_many (pipeline, src, tee, identity1, identity2, aggregator, sink,
      NULL);

  gst_element_link_pads (src, "src", tee, "sink");
  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (identity1, "sink"));
  gst_pad_link (gst_element_get_request_pad (tee, "src%d"),
      gst_element_get_pad (identity2, "sink"));
  gst_pad_link (gst_element_get_pad (identity1, "src"),
      gst_element_get_request_pad (aggregator, "sink%d"));
  gst_pad_link (gst_element_get_pad (identity2, "src"),
      gst_element_get_request_pad (aggregator, "sink%d"));
  gst_element_link_pads (aggregator, "src", sink, "sink");

  g_signal_connect (G_OBJECT (src), "eos", G_CALLBACK (eos_signal), NULL);
  g_signal_connect (G_OBJECT (sink), "handoff",
      G_CALLBACK (handoff_signal), NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (pipeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  exit (0);
}
