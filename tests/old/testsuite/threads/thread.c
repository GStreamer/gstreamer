#include <gst/gst.h>

void
usage (void)
{
  g_print ("usage: thread <testnum>  \n"
           "   available testnums:   \n"
           "          1: stress test state change      \n"
           "          2: iterate once                  \n"
           "          3: iterate twice                 \n"
           "          4: state change while running    \n"
           "          5: state change in thread context\n");
}

static void
construct_pipeline (GstElement *pipeline) 
{
  GstElement *src, *sink, *queue, *thread;

  src    = gst_elementfactory_make ("fakesrc",  "src");
  sink   = gst_elementfactory_make ("fakesink", "sink");
  queue  = gst_elementfactory_make ("queue",    "queue");
  thread = gst_elementfactory_make ("thread",   "thread");

  gst_element_connect (src, "src", queue, "sink");
  gst_element_connect (queue, "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), thread);
  
  gst_bin_add (GST_BIN (thread), sink);

  g_object_set (G_OBJECT (src), "num_buffers", 5, NULL);
}

void
change_state (GstElement *element, GstBuffer *buf, GstElement *pipeline) 
{
  gst_element_set_state (pipeline, GST_STATE_NULL);
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  gint testnum;
  
  gst_init (&argc, &argv);

  if (argc < 2) {
    usage();
    return 0;
  }

  testnum = atoi (argv[1]);
  
  pipeline = gst_pipeline_new ("main_pipeline");
  construct_pipeline (pipeline);

  if (testnum == 1) {
    g_print ("stress test state changes...\n");

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  if (testnum == 2 || testnum == 3) {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (testnum == 3) {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running2 ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (testnum == 4) {
    gint run;

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running3 ...\n");
    for (run = 0; run < 3; run++) {
      gst_bin_iterate (GST_BIN (pipeline));
    }
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  if (testnum == 5) {
    GstElement *sink;

    sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

    g_signal_connect (G_OBJECT (sink), "handoff", change_state, pipeline);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running3 ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  
  return 0;
}
