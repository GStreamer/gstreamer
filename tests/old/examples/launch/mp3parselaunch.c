#include <gst/gst.h>

int
main (int argc, char *argv[]) 
{
  GstElement *pipeline;
  GstElement *filesrc;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    return -1;
  }
  pipeline = gst_pipeline_new ("my_pipeline");

  gst_parse_launch ("filesrc[my_filesrc] ! mp3parse ! mpg123 ! osssink", GST_BIN (pipeline));

  filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "my_filesrc");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
