/*
 * reconnect.c - test disconnect and reconnect
 * thomas@apestaart.org
 * 
 * Latest change : 	31/05/2001
 *
 * Version :		0.1
 */

#include <stdlib.h>
#include <gst/gst.h>
#include <unistd.h>

#define DEBUG

/* function prototypes */


GstElement      *main_bin;
GstElement      *fakesrc;
GstElement      *fakesink;

int main (int argc, char *argv[]) 
{
  int i;

  gst_init (&argc, &argv);
  
  /* set up input channel and main bin */

  g_print ("creating main bin\n");  

  /* create an audio src */
  fakesrc = gst_elementfactory_make ("fakesrc", "fakesrc");
  fakesink = gst_elementfactory_make ("fakesink", "fakesink");
/*
  gtk_object_set (GTK_OBJECT (fakesink), "silent", TRUE, NULL);
*/
  /* create main bin */
  main_bin = gst_pipeline_new ("bin");

  gst_bin_add (GST_BIN (main_bin), fakesrc);
  gst_bin_add (GST_BIN (main_bin), fakesink);


  gst_pad_connect (gst_element_get_pad (fakesrc, "src"),
                   gst_element_get_pad (fakesink, "sink"));

  /* start playing */

  g_print ("setting to play\n");

  gst_element_set_state (main_bin, GST_STATE_PLAYING);

  for (i = 0; i < 5; ++i) 
  {
    g_print ("going to iterate\n");
    gst_bin_iterate (GST_BIN (main_bin));
    g_print ("back from iterate\n");
  }

  /* disconnect and reconnect fakesink */

  gst_element_set_state (main_bin, GST_STATE_PAUSED);
  g_print ("disconnecting...\n");
  gst_pad_disconnect (gst_element_get_pad (fakesrc, "src"),
                      gst_element_get_pad (fakesink, "sink"));
  g_print ("reconnecting...\n");
  gst_pad_connect (gst_element_get_pad (fakesrc, "src"),
                   gst_element_get_pad (fakesink, "sink"));
  gst_element_set_state (main_bin, GST_STATE_PLAYING);

  for (i = 0; i < 5; ++i) 
  {
    g_print ("going to iterate\n");
    gst_bin_iterate (GST_BIN (main_bin));
    g_print ("back from iterate\n");
  }

  g_print ("we're done iterating.\n");

  /* stop the bin */
  gst_element_set_state(main_bin, GST_STATE_NULL);
/*
  gst_object_destroy (GST_OBJECT (fakesink));
  gst_object_destroy (GST_OBJECT (main_bin));
*/

  exit(0);
}
