#include <gst/gst.h>

static GstElement *
create_pipeline (void)
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline;
  GstElement *thread, *queue;

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc = gst_elementfactory_make ("fakesrc", "fakesrc");
  thread = gst_thread_new ("thread");
  fakesink = gst_elementfactory_make ("fakesink", "fakesink");
  queue = gst_elementfactory_make ("queue", "queue");
  gst_bin_add (GST_BIN (thread), fakesink);
  gst_bin_add (GST_BIN (thread), queue);
  gst_element_connect (queue, "src", fakesink, "sink");
  gst_element_add_ghost_pad (thread, gst_element_get_pad (queue, "sink"), "sink");

  gst_element_connect (fakesrc, "src", thread, "sink");

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), thread);

  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);

  return pipeline;
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  gint i;

  free (malloc(8)); /* -lefence */

  gst_init (&argc, &argv);

  i = 10000;

  g_mem_chunk_info ();
  while (i--) {
    fprintf (stderr, "+");
    pipeline = create_pipeline ();
	  
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    fprintf (stderr, "-");
    gst_object_unref (GST_OBJECT (pipeline));
  }
  fprintf (stderr, "\n");
  g_mem_chunk_info ();

  return 0;
}
