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
#include <time.h>

#define DEBUG

gboolean playing = TRUE;
gboolean cut_start_signalled = FALSE;
gboolean cut_stop_signalled = FALSE;

int id = 0;                     /* increment this for each new cut */
GstElement *main_bin;
GstElement *audiosrc;
GstElement *queue;
GstElement *thread;
GstElement *cutter;
GstElement *disksink;
GstElement *encoder;
char buffer[255];

/* signal callbacks */

void
cut_start (GstElement * element)
{
  g_print ("\nDEBUG: main: cut start\n");
  /* we should pause the pipeline, unlink cutter and disksink
   * create a new disksink to a real file, relink, and set to play
   */
  g_print ("DEBUG: cut_start: main_bin pausing\n");
  gst_element_set_state (main_bin, GST_STATE_PAUSED);
  g_print ("DEBUG: cut_start: main_bin paused\n");

  {
    time_t seconds;
    struct tm *ct;

    time (&seconds);
    ct = localtime (&seconds);
/*    sprintf (buffer, "/news/incoming/audio/cutter.%06d.wav", id); */
    sprintf (buffer,
        "/news/incoming/audio/cutter.%04d%02d%02d.%02d%02d%02d.wav",
        ct->tm_year + 1900, ct->tm_mon, ct->tm_mday, ct->tm_hour, ct->tm_min,
        ct->tm_sec);
  }
  g_print ("DEBUG: cut_start: setting new location to %s\n", buffer);
  g_object_set (G_OBJECT (disksink), "location", buffer, NULL);
  g_object_set (G_OBJECT (disksink), "type", 4, NULL);

  gst_element_set_state (main_bin, GST_STATE_PLAYING);
  ++id;
  g_print ("start_cut_signal done\n");
  return;
}

void
cut_start_signal (GstElement * element)
{
  g_print ("\nDEBUG: main: cut start signal\n");
  cut_start_signalled = TRUE;
}

void
cut_stop (GstElement * element)
{
  g_print ("\nDEBUG: main: cut stop\n");
  /* we should pause the pipeline, unlink disksink, create a fake disksink,
   * link to pipeline, and set to play
   */
  g_print ("DEBUG: cut_stop: main_bin paused\n");
  gst_element_set_state (main_bin, GST_STATE_PAUSED);

  g_print ("DEBUG: cut_stop: setting new location\n");
  g_object_set (G_OBJECT (disksink), "location", "/dev/null", NULL);

  gst_element_set_state (main_bin, GST_STATE_PLAYING);
  g_print ("stop_cut_signal done\n");
  return;
}

void
cut_stop_signal (GstElement * element)
{
  g_print ("\nDEBUG: main: cut stop signal\n");
  cut_stop_signalled = TRUE;
}

int
main (int argc, char *argv[])
{
  /*int i, j; */
  /*gboolean done; */

  /*char buffer[20]; */

  /*output_channel_t *channel_out; */

  GstElement *audiosrc;

  gst_init (&argc, &argv);
/*
  if (argc == 1) 
  {
    g_print("usage: %s <filename1> <filename2> <...>\n", argv[0]);
    exit(-1);
  }*/

  /* set up input channel and main bin */

  g_print ("creating main bin\n");
  /* create cutter */
  cutter = gst_element_factory_make ("cutter", "cutter");

  g_object_set (G_OBJECT (cutter),
      "threshold_dB", -40.0, "runlength", 0.5, "prelength", 1.0, NULL);

  /* create an audio src */
  if (!(audiosrc = gst_element_factory_make ("osssrc", "audio_src")))
    g_error ("Could not create 'osssrc' element !\n");

  /* set params */

  g_object_set (G_OBJECT (audiosrc), "frequency", 44100,
      "channels", 1, "format", 16, NULL);

  if (!(encoder = gst_element_factory_make ("passthrough", "encoder")))
    g_error ("Could not create 'passthrough' element !\n");

  if (!(disksink = gst_element_factory_make ("afsink", "disk_sink")))
    g_error ("Could not create 'afsink' element !\n");

  g_object_set (G_OBJECT (disksink), "location", "/dev/null", NULL);

  thread = gst_thread_new ("thread");
  g_assert (thread != NULL);

  /* create main bin */
  main_bin = gst_pipeline_new ("bin");
  g_assert (main_bin != NULL);

  queue = gst_element_factory_make ("queue", "queue");
  g_assert (queue);

  /* add elements to bin */
  gst_bin_add (GST_BIN (main_bin), audiosrc);
  gst_bin_add (GST_BIN (thread), queue);

  gst_bin_add_many (GST_BIN (thread), cutter, encoder, disksink, NULL);

  gst_element_link_many (audiosrc, queue, cutter, encoder, disksink, NULL);
  gst_bin_add (GST_BIN (main_bin), thread);

  /* set signal handlers */
  g_print ("setting signal handlers\n");
  g_signal_connect (G_OBJECT (cutter), "cut_start",
      (GCallback) cut_start_signal, NULL);
  g_signal_connect (G_OBJECT (cutter), "cut_stop",
      (GCallback) cut_stop_signal, NULL);

  /* start playing */
  g_print ("setting to play\n");
  gst_element_set_state (main_bin, GST_STATE_PLAYING);
/*
  g_print ("setting thread to play\n");
  gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PLAYING);
*/
  while (playing) {
/*      g_print ("> "); */
    gst_bin_iterate (GST_BIN (main_bin));
/*      g_print (" <"); */
    if (cut_start_signalled) {
      g_print ("\nDEBUG: main: cut_start_signalled true !\n");
      cut_start (cutter);
      cut_start_signalled = FALSE;
    }
    if (cut_stop_signalled) {
      g_print ("\nDEBUG: main: cut_stop_signalled true !\n");
      cut_stop (cutter);
      cut_stop_signalled = FALSE;
    }
  }
  g_print ("we're done iterating.\n");
  /* stop the bin */

  gst_element_set_state (main_bin, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (disksink));
  gst_object_unref (GST_OBJECT (main_bin));

  exit (0);
}
