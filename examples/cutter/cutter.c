/*
 * cutter.c - cut audio into pieces based on silence  - thomas@apestaart.org
 * 
 * construct a simple pipeline osssrc ! cutter ! disksink
 * pause when necessary, change output
 *
 * Latest change : 	03/06/2001
 *
 * Version :		0.3
 */

#include <stdlib.h>
#include <gst/gst.h>
#include <unistd.h>

#define DEBUG

gboolean        playing = TRUE;
int             id = 0;                 /* increment this for each new cut */
GstElement      *main_bin;
GstElement	*audiosrc;
GstElement      *cutter;
GstElement      *disksink;
GstElement      *encoder;
char            buffer[255];

/* signal callbacks */

void cut_start (GstElement *element)
{
  g_print ("\nDEBUG: main: cut start\n");
  /* we should pause the pipeline, disconnect cutter and disksink
   * create a new disksink to a real file, reconnect, and set to play
   */
  g_print ("DEBUG: cut_start: main_bin paused\n");
  gst_element_set_state (main_bin, GST_STATE_PAUSED);

  sprintf (buffer, "/tmp/test%d.raw", id);
  g_print ("DEBUG: cut_start: setting new location to %s\n", buffer);
  gtk_object_set (GTK_OBJECT (disksink), "location", buffer, NULL);
/*
  gtk_object_set (GTK_OBJECT (disksink), "type", 4, NULL);
*/
  gst_element_set_state (main_bin, GST_STATE_PLAYING);
  ++id;
  g_print ("start_cut_signal done\n");
  return;
}

void cut_stop (GstElement *element)
{
  g_print ("\nDEBUG: main: cut stop\n");
  /* we should pause the pipeline, disconnect disksink, create a fake disksink,
   * connect to pipeline, and set to play
   */
  g_print ("DEBUG: cut_stop: main_bin paused\n");
  gst_element_set_state (main_bin, GST_STATE_PAUSED);

  g_print ("DEBUG: cut_stop: setting new location\n");
  gtk_object_set (GTK_OBJECT (disksink), "location", "/dev/null", NULL);

  gst_element_set_state (main_bin, GST_STATE_PLAYING);
  g_print ("stop_cut_signal done\n");
  return;
}

int main (int argc, char *argv[]) 
{
  //int i, j;
  //gboolean done;
  
  //char buffer[20];
  
  //output_channel_t *channel_out;
  
  GstElement *audiosrc;

  gst_init (&argc,&argv);
/*
  if (argc == 1) 
  {
    g_print("usage: %s <filename1> <filename2> <...>\n", argv[0]);
    exit(-1);
  }*/
  
  /* set up input channel and main bin */

  g_print ("creating main bin\n");  
  /* create cutter */
  cutter = gst_elementfactory_make ("cutter", "cutter");

  gtk_object_set (GTK_OBJECT (cutter), 
	"threshold_dB", -60.0, 
	"runlength", 2.0,
	NULL);

  /* create an audio src */
  audiosrc = gst_elementfactory_make ("osssrc", "audio_src");

  /* set params */

  gtk_object_set (GTK_OBJECT (audiosrc), "frequency", 44100, 
                                         "channels", 2,
  					 "format", 16, NULL);

  encoder = gst_elementfactory_make ("passthrough", "encoder");
  disksink = gst_elementfactory_make ("disksink", "disk_sink");

  gtk_object_set (GTK_OBJECT (disksink), "location", "/dev/null", NULL);

  /* create main bin */
  main_bin = gst_pipeline_new ("bin");

  /* add elements to bin */
  gst_bin_add (GST_BIN (main_bin), cutter);
  gst_bin_add (GST_BIN (main_bin), audiosrc);
  gst_bin_add (GST_BIN (main_bin), encoder);
  gst_bin_add (GST_BIN (main_bin), disksink);

  /* connect adder and disksink */

  gst_pad_connect (gst_element_get_pad (audiosrc, "src"),
                   gst_element_get_pad (cutter,"sink"));
  gst_pad_connect (gst_element_get_pad (cutter, "src"),
                   gst_element_get_pad (encoder, "sink"));
  gst_pad_connect (gst_element_get_pad (encoder, "src"),
                   gst_element_get_pad (disksink, "sink"));

  /* set signal handlers */
  g_print ("setting signal handlers\n");
  gtk_signal_connect (GTK_OBJECT(cutter), "cut_start",
                      GTK_SIGNAL_FUNC(cut_start), NULL);
  gtk_signal_connect (GTK_OBJECT(cutter), "cut_stop",
                      GTK_SIGNAL_FUNC(cut_stop), NULL);

  /* start playing */
  g_print ("setting to play\n");
  gst_element_set_state (main_bin, GST_STATE_PLAYING);

  while (playing) 
  {
//      g_print ("> ");
      gst_bin_iterate (GST_BIN (main_bin));
//      g_print (" <");
  }
  g_print ("we're done iterating.\n");
  /* stop the bin */

  gst_element_set_state (main_bin, GST_STATE_NULL);

  gst_object_destroy (GST_OBJECT (disksink));
  gst_object_destroy (GST_OBJECT (main_bin));

  exit(0);
}
