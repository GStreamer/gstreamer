#include <gst/gst.h>

static GstElement *
create_pipeline (void)
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline;
  GstElement *thread, *queue;

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  thread = gst_thread_new ("thread");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  queue = gst_element_factory_make ("queue", "queue");
  gst_bin_add (GST_BIN (thread), fakesink);
  gst_bin_add (GST_BIN (thread), queue);
  gst_element_link (queue, fakesink);
  gst_element_add_ghost_pad (thread, gst_element_get_pad (queue, "sink"),
      "sink");

  gst_element_link (fakesrc, thread);

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), thread);

  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);

  return pipeline;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  gint i = 10000;
  gint step = 100;

  free (malloc (8));		/* -lefence */

  gst_init (&argc, &argv);


  g_mem_chunk_info ();
  while (i--) {
    if (i % step == 0)
      fprintf (stderr, "%10d\r", i);
    pipeline = create_pipeline ();

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_object_unref (GST_OBJECT (pipeline));
  }
  fprintf (stderr, "\n");
  g_mem_chunk_info ();

  return 0;
}
