#include <gst/gst.h>

/*
 * FIXME:
 * these tests should have a maximum run length, so that they get killed
 * if they lock up, which they're bound to do.
 */

void
usage (void)
{
  g_print ("compile this test with TESTNUM defined.\n"
      "   available TESTNUMs:   \n"
      "          1: stress test state change      \n"
      "          2: iterate once                  \n"
      "          3: iterate twice                 \n"
      "          4: state change while running    \n"
      "          5: state change in thread context\n");
}

static void
construct_pipeline (GstElement * pipeline)
{
  GstElement *src, *sink, *queue, *identity, *thread;

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  identity = gst_element_factory_make ("identity", NULL);
  queue = gst_element_factory_make ("queue", NULL);
  thread = gst_element_factory_make ("thread", NULL);

  gst_element_link_many (src, queue, identity, sink, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, queue, thread, NULL);
  gst_bin_add_many (GST_BIN (thread), identity, sink, NULL);

  g_object_set (G_OBJECT (src), "num_buffers", 5, NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
}

void
change_state (GstElement * element, GstBuffer * buf, GstElement * pipeline)
{
  gst_element_set_state (pipeline, GST_STATE_NULL);
}

int
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;

  gst_init (&argc, &argv);

#ifndef TESTNUM
  usage ();
  return -1;
#endif

  pipeline = gst_pipeline_new ("main_pipeline");
  construct_pipeline (pipeline);

  if (TESTNUM == 1) {
    g_print ("thread test 1: stress test state changes...\n");

    g_print ("NULL\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("READY\n");
    gst_element_set_state (pipeline, GST_STATE_READY);
    g_print ("NULL\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("PAUSED\n");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    g_print ("READY\n");
    gst_element_set_state (pipeline, GST_STATE_READY);
    g_print ("PAUSED\n");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    g_print ("PLAYING\n");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    /* element likely hits EOS and does a state transition to PAUSED */
    g_print ("READY\n");
    gst_element_set_state (pipeline, GST_STATE_READY);
    g_print ("NULL\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  if (TESTNUM == 2 || TESTNUM == 3) {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (TESTNUM == 3) {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (TESTNUM == 4) {
    gint run;

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running ...\n");
    for (run = 0; run < 3; run++) {
      gst_bin_iterate (GST_BIN (pipeline));
    }
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (TESTNUM == 5) {
    /* I don't think this test is supposed to work */
    GstElement *sink;

    sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
    g_assert (sink);

    g_signal_connect (G_OBJECT (sink), "handoff",
	G_CALLBACK (change_state), pipeline);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  return 0;
}
