#include <gst/gst.h>

int
main (int argc, char *argv[]) 
{
  GstElement *pipeline;
  GstElement *disksrc;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("my_pipeline");

  gst_parse_launch ("disksrc[my_disksrc] ! mp3parse ! mpg123 ! osssink", GST_BIN (pipeline));

  disksrc = gst_bin_get_by_name (GST_BIN (pipeline), "my_disksrc");
  g_object_set (G_OBJECT (disksrc), "location", argv[1], NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
}
