#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstAutoplug *autoplug;
  GstElement *element;
  GstElement *sink;
  GstElement *pipeline;
  GstElement *disksrc;

  gst_init(&argc,&argv);

  sink = gst_elementfactory_make ("osssink", "osssink");
  g_assert (sink != NULL);

  autoplug = gst_autoplugfactory_make ("staticrender");
  g_assert (autoplug != NULL);
  
  element = gst_autoplug_to_renderers (autoplug, 
		                       gst_caps_new (
					 "mp3caps", 
					 "audio/mp3",
					 NULL
				       ), 
				       sink,
				       NULL);
  g_assert (element != NULL);

  pipeline = gst_pipeline_new ("main_pipeline");
  g_assert (pipeline != NULL);

  disksrc = gst_elementfactory_make ("disksrc", "disk_reader");
  g_assert (disksrc != NULL);

  gst_bin_add (GST_BIN (pipeline), disksrc);
  gst_bin_add (GST_BIN (pipeline), element);

  gst_element_connect (disksrc, "src", element, "sink");

  gtk_object_set (GTK_OBJECT (disksrc), "location", argv[1], NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  exit (0);
}
