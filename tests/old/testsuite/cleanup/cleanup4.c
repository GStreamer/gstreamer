#include <gst/gst.h>

static GstElement *
create_pipeline (void)
{
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc;
  gint i;

  free (malloc(8)); /* -lefence */

  gst_init (&argc, &argv);

  i = 10000;

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);
  gst_bin_add (GST_BIN (pipeline), fakesrc);

  g_mem_chunk_info ();
  while (i--) {
    GstElement *bin;
    GstElement *fakesink;

    fprintf (stderr, "+");

    bin = gst_bin_new ("bin");

    fakesink = gst_element_factory_make ("fakesink", "fakesink");

    gst_element_connect (fakesrc, "src", fakesink, "sink");

    gst_bin_add (GST_BIN (bin), fakesink);
    gst_bin_add (GST_BIN (pipeline), bin);
	  
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    fprintf (stderr, "-");
    gst_bin_remove (GST_BIN (pipeline), GST_ELEMENT (bin));

  }
  fprintf (stderr, "\n");
  g_mem_chunk_info ();

  return 0;
}
