#include <stdlib.h>
#include <gst/gst.h>

int main (int argc, char *argv[]) 
{
  GstElement *bin, *filesrc, *decoder, *osssink;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <mp3 file>\n", argv[0]);
    exit (-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");

  /* create a disk reader */
  filesrc = gst_elementfactory_make ("filesrc", "disk_source");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* now it's time to get the decoder */
  decoder = gst_elementfactory_make ("mad", "parse");
  if (!decoder) {
    g_print ("could not find plugin \"mad\"");
    return -1;
  }
  /* and an audio sink */
  osssink = gst_elementfactory_make ("osssink", "play_audio");

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), filesrc, decoder, osssink, NULL);

  /* connect the elements */
  gst_element_connect_elements_many (filesrc, decoder, osssink, NULL);
  
  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (bin)));

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  exit (0);
}

