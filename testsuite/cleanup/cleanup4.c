#include <gst/gst.h>

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc;
  gint i;

  free (malloc(8)); /* -lefence */

  gst_init (&argc, &argv);

  i = 1000;

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);
  gst_bin_add (GST_BIN (pipeline), fakesrc);

  g_mem_chunk_info ();
  while (i--) {
    GstElement *bin;
    GstElement *fakesink;

    g_print ("+");

    bin = gst_bin_new ("bin");

    fakesink = gst_element_factory_make ("fakesink", "fakesink");

    gst_element_link (fakesrc, fakesink);

    gst_bin_add (GST_BIN (bin), fakesink);
    gst_bin_add (GST_BIN (pipeline), bin);
	  
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print ("-");
    gst_bin_remove (GST_BIN (pipeline), GST_ELEMENT (bin));

  }
  g_print ("\n");
  g_mem_chunk_info ();

  return 0;
}
