#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstAutoplug *autoplug;
  GstElement *element;
  GstElement *sink;
  GstElement *pipeline;
  GstElement *filesrc;

  gst_init(&argc,&argv);

  sink = gst_element_factory_make ("osssink", "osssink");
  g_assert (sink != NULL);

  autoplug = gst_autoplug_factory_make ("staticrender");
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

  filesrc = gst_element_factory_make ("filesrc", "disk_reader");
  g_assert (filesrc != NULL);

  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), element);

  gst_element_connect (filesrc, "src", element, "sink");

  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  exit (0);
}
