#include <gst/gst.h>

#include <unistd.h>

/*
 * queue test code
 * starts a fakesrc num_buffers=50 ! { queue ! fakesink } thread
 * by first setting the output thread to play, then the whole pipeline
 */

static gint handoff_count = 0;

/* handoff callback */
static void
handoff (GstElement * element, gpointer data)
{
  ++handoff_count;
  g_print ("handoff (%d) ", handoff_count);
}

static void
construct_pipeline (GstElement * pipeline, GstElement * thread)
{
  GstElement *src, *sink, *queue;

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  queue = gst_element_factory_make ("queue", NULL);

  gst_bin_add_many (GST_BIN (thread), queue, sink, NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, thread, NULL);

  gst_element_link_many (src, queue, sink, NULL);

  g_object_set (G_OBJECT (src), "num_buffers", 50, NULL);

  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (handoff), NULL);
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
  GstElement *thread = NULL;

  gst_init (&argc, &argv);

  pipeline = gst_thread_new ("main_pipeline");
  thread = gst_element_factory_make ("thread", NULL);
  construct_pipeline (pipeline, thread);

  g_print ("First run: to show the pipeline works\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("SLEEPING 1 sec\n");
  sleep (1);

  g_print ("Pipeline done. Resetting to NULL.\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  if (handoff_count == 0) {
    g_print ("ERROR: no buffers have passed\n");
    return -1;
  }

  handoff_count = 0;

  g_print
      ("Second run: setting consumer thread to playing, then complete pipeline\n");
  gst_element_set_state (thread, GST_STATE_PLAYING);
  g_print ("SLEEPING 1 sec\n");
  sleep (1);
  gst_element_set_state (pipeline, gst_element_get_state (pipeline));
  g_print ("SLEEPING 2 sec\n");
  sleep (2);

  if (handoff_count == 0) {
    g_print ("ERROR: no buffers have passed\n");
    return -1;
  }

  return 0;
}
