#include <gst/gst.h>

static GstElement *
create_pipeline (void)
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline;

  
  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_element_link (fakesrc, fakesink);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);

  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);

  return pipeline;
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  gint i = 1000;
  gint step = 100;

  free (malloc(8)); /* -lefence */

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
