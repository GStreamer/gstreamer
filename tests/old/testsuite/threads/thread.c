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
  GstElement *src, *sink, *queue, *identity, *thread;

  src      = gst_element_factory_make ("fakesrc",  NULL);
  sink     = gst_element_factory_make ("fakesink", NULL);
  identity = gst_element_factory_make ("identity", NULL);
  queue    = gst_element_factory_make ("queue",    NULL);
  thread   = gst_element_factory_make ("thread",   NULL);

  gst_element_connect_many (src, queue, identity, sink, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, queue, thread, NULL);
  gst_bin_add_many (GST_BIN (thread), identity, sink, NULL);

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

    g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (change_state), pipeline);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("running3 ...\n");
    while (gst_bin_iterate (GST_BIN (pipeline)));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }
  
  return 0;
}
