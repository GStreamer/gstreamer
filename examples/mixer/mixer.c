/*
 * mixer.c - stereo audio mixer - thomas@apestaart.org
 * example based on helloworld
 * demonstrates the adder plugin and the volume envelope plugin 
 * work in progress but do try it out 
 * 
 * Latest change : 	28/08/2001
 * 					trying to adapt to incsched
 * 					delayed start for channels > 1
 *					now works by quickhacking the
 *					adder plugin to set
 * 					GST_ELEMENT_COTHREAD_STOPPING		
 * Version :		0.5.1
 */

#include <stdlib.h>
#include <gst/gst.h>
#include "mixer.h"
#include <unistd.h>

/*#define WITH_BUG */
/*#define WITH_BUG2 */
/*#define DEBUG */
/*#define AUTOPLUG	* define if you want autoplugging of input channels * */
/* function prototypes */

input_channel_t *create_input_channel (int id, char *location);
void destroy_input_channel (input_channel_t * pipe);
void env_register_cp (GstElement * volenv, double cp_time, double cp_level);


gboolean playing;


/* eos will be called when the src element has an end of stream */
void
eos (GstElement * element)
{
  g_print ("have eos, quitting ?\n");

/*  playing = FALSE; */
}

G_GNUC_UNUSED static GstCaps *
gst_play_type_find (GstBin * bin, GstElement * element)
{
  GstElement *typefind;
  GstElement *pipeline;
  GstCaps *caps = NULL;

  GST_DEBUG ("GstPipeline: typefind for element \"%s\"",
      GST_ELEMENT_NAME (element));

  pipeline = gst_pipeline_new ("autoplug_pipeline");

  typefind = gst_element_factory_make ("typefind", "typefind");
  g_return_val_if_fail (typefind != NULL, FALSE);

  gst_pad_link (gst_element_get_pad (element, "src"),
      gst_element_get_pad (typefind, "sink"));
  gst_bin_add (bin, typefind);
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (bin));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* push a buffer... the have_type signal handler will set the found flag */
  gst_bin_iterate (GST_BIN (pipeline));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  caps = gst_pad_get_caps (gst_element_get_pad (element, "src"));

  gst_pad_unlink (gst_element_get_pad (element, "src"),
      gst_element_get_pad (typefind, "sink"));
  gst_bin_remove (bin, typefind);
  gst_bin_remove (GST_BIN (pipeline), GST_ELEMENT (bin));
  gst_object_unref (GST_OBJECT (typefind));
  gst_object_unref (GST_OBJECT (pipeline));

  return caps;
}

int
main (int argc, char *argv[])
{
  int i, j;
  int num_channels;

  char buffer[20];

  GList *input_channels;	/* structure holding all the input channels */

  input_channel_t *channel_in;

  GstElement *main_bin;
  GstElement *adder;
  GstElement *audiosink;

  GstPad *pad;			/* to request pads for the adder */

  gst_init (&argc, &argv);

  if (argc == 1) {
    g_print ("usage: %s <filename1> <filename2> <...>\n", argv[0]);
    exit (-1);
  }
  num_channels = argc - 1;

  /* set up output channel and main bin */

  /* create adder */
  adder = gst_element_factory_make ("adder", "adderel");

  /* create an audio sink */
  audiosink = gst_element_factory_make ("esdsink", "play_audio");

  /* create main bin */
  main_bin = gst_pipeline_new ("bin");

  /* link adder and output to bin */
  GST_INFO ("main: adding adder to bin");
  gst_bin_add (GST_BIN (main_bin), adder);
  GST_INFO ("main: adding audiosink to bin");
  gst_bin_add (GST_BIN (main_bin), audiosink);

  /* link adder and audiosink */

  gst_pad_link (gst_element_get_pad (adder, "src"),
      gst_element_get_pad (audiosink, "sink"));

  /* start looping */
  input_channels = NULL;

  for (i = 1; i < argc; ++i) {
    printf ("Opening channel %d from file %s...\n", i, argv[i]);
    channel_in = create_input_channel (i, argv[i]);
    input_channels = g_list_append (input_channels, channel_in);

    if (i > 1)
      gst_element_set_state (main_bin, GST_STATE_PAUSED);
    gst_bin_add (GST_BIN (main_bin), channel_in->pipe);

    /* request pads and link to adder */
    GST_INFO ("requesting pad\n");
    pad = gst_element_get_request_pad (adder, "sink%d");
    printf ("\tGot new adder sink pad %s\n", gst_pad_get_name (pad));
    sprintf (buffer, "channel%d", i);
    gst_pad_link (gst_element_get_pad (channel_in->pipe, buffer), pad);

    /* register a volume envelope */
    printf ("\tregistering volume envelope...\n");

    /* 
     * this is the volenv :
     * each song gets a slot of 5 seconds, with a 5 second fadeout
     * at the end of that, all audio streams play simultaneously
     * at a level ensuring no distortion
     * example for three songs :
     * song1 : starts at full level, plays 5 seconds, faded out at 10 seconds,
     *             sleep until 25, fade to end level at 30
     * song2 : starts silent, fades in at 5 seconds, full blast at 10 seconds,
     *             full level until 15, faded out at 20, sleep until 25, fade to end at 30
     * song3 : starts muted, fades in from 15, full at 20, until 25, fade to end level
     */

    if (i == 1) {
      /* first song gets special treatment for end style */
      env_register_cp (channel_in->volenv, 0.0, 1.0);
    } else {
      env_register_cp (channel_in->volenv, 0.0, 0.0000001);	/* start muted */
      env_register_cp (channel_in->volenv, i * 10.0 - 15.0, 0.0000001);	/* start fade in */
      env_register_cp (channel_in->volenv, i * 10.0 - 10.0, 1.0);
    }
    env_register_cp (channel_in->volenv, i * 10.0 - 5.0, 1.0);	/* end of full level */

    if (i != num_channels) {
      env_register_cp (channel_in->volenv, i * 10.0, 0.0000001);	/* fade to black */
      env_register_cp (channel_in->volenv, num_channels * 10.0 - 5.0, 0.0000001);	/* start fade in */
    }
    env_register_cp (channel_in->volenv, num_channels * 10.0, 1.0 / num_channels);	/* to end level */

#ifndef GST_DISABLE_LOADSAVE
    gst_xml_write_file (GST_ELEMENT (main_bin), fopen ("mixer.xml", "w"));
#endif

    /* start playing */
    gst_element_set_state (main_bin, GST_STATE_PLAYING);

    /* write out the schedule */
    gst_scheduler_show (GST_ELEMENT_SCHED (main_bin));
    playing = TRUE;

    j = 0;
    /*printf ("main: start iterating from 0"); */
    while (playing && j < 100) {
/*      printf ("main: iterating %d\n", j); */
      gst_bin_iterate (GST_BIN (main_bin));
      /*fprintf(stderr,"after iterate()\n"); */
      ++j;
    }
  }
  printf ("main: all the channels are open\n");
  while (playing) {
    gst_bin_iterate (GST_BIN (main_bin));
    /*fprintf(stderr,"after iterate()\n"); */
  }
  /* stop the bin */
  gst_element_set_state (main_bin, GST_STATE_NULL);

  while (input_channels) {
    destroy_input_channel (input_channels->data);
    input_channels = g_list_next (input_channels);
  }
  g_list_free (input_channels);

  gst_object_unref (GST_OBJECT (audiosink));

  gst_object_unref (GST_OBJECT (main_bin));

  exit (0);
}

input_channel_t *
create_input_channel (int id, char *location)
{
  /* create an input channel, reading from location
   * return a pointer to the channel
   * return NULL if failed
   */

  input_channel_t *channel;

  char buffer[20];		/* hold the names */

/*  GstAutoplug *autoplug;
  GstCaps *srccaps; */
  GstElement *new_element;
  GstElement *decoder;

  GST_DEBUG ("c_i_p : creating channel with id %d for file %s", id, location);

  /* allocate channel */

  channel = (input_channel_t *) malloc (sizeof (input_channel_t));
  if (channel == NULL) {
    printf ("create_input_channel : could not allocate memory for channel !\n");
    return NULL;
  }

  /* create channel */

  GST_DEBUG ("c_i_p : creating pipeline");

  sprintf (buffer, "pipeline%d", id);
  channel->pipe = gst_bin_new (buffer);
  g_assert (channel->pipe != NULL);

  /* create elements */

  GST_DEBUG ("c_i_p : creating filesrc");

  sprintf (buffer, "filesrc%d", id);
  channel->filesrc = gst_element_factory_make ("filesrc", buffer);
  g_assert (channel->filesrc != NULL);

  GST_DEBUG ("c_i_p : setting location");
  g_object_set (G_OBJECT (channel->filesrc), "location", location, NULL);

  /* add filesrc to the bin before autoplug */
  gst_bin_add (GST_BIN (channel->pipe), channel->filesrc);

  /* link signal to eos of filesrc */
  g_signal_connect (G_OBJECT (channel->filesrc), "eos", G_CALLBACK (eos), NULL);


#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating volume envelope\n");
#endif

  sprintf (buffer, "volenv%d", id);
  channel->volenv = gst_element_factory_make ("volenv", buffer);
  g_assert (channel->volenv != NULL);

  /* autoplug the pipe */

#ifdef DEBUG
  printf ("DEBUG : c_i_p : getting srccaps\n");
#endif

#ifdef WITH_BUG
  srccaps = gst_play_type_find (GST_BIN (channel->pipe), channel->filesrc);
#endif
#ifdef WITH_BUG2
  {
    GstElement *pipeline;

    pipeline = gst_pipeline_new ("autoplug_pipeline");

    gst_bin_add (GST_BIN (pipeline), channel->pipe);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (pipeline), channel->pipe);

  }
#endif

#ifdef AUTOPLUG
  if (!srccaps) {
    g_print ("could not autoplug, unknown media type...\n");
    exit (-1);
  }
#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating autoplug\n");
#endif

  autoplug = gst_autoplug_factory_make ("static");
  g_assert (autoplug != NULL);

#ifdef DEBUG
  printf ("DEBUG : c_i_p : autoplugging\n");
#endif

  new_element = gst_autoplug_to_caps (autoplug, srccaps,
      gst_caps_new ("audio/raw", NULL), NULL);

  if (!new_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }
#else

  new_element = gst_bin_new ("autoplug_bin");

  /* static plug, use mad plugin and assume mp3 input */
  printf ("using static plugging for input channel\n");
  decoder = gst_element_factory_make ("mad", "mpg123");
  if (!decoder) {
    fprintf (stderr, "Could not get a decoder element !\n");
    exit (1);
  }
  gst_bin_add (GST_BIN (new_element), decoder);

  gst_element_add_ghost_pad (new_element,
      gst_element_get_pad (decoder, "sink"), "sink");
  gst_element_add_ghost_pad (new_element,
      gst_element_get_pad (decoder, "src"), "src_00");

#endif
#ifndef GST_DISABLE_LOADSAVE
  gst_xml_write_file (GST_ELEMENT (new_element), fopen ("mixer.gst", "w"));
#endif

  gst_bin_add (GST_BIN (channel->pipe), channel->volenv);
  gst_bin_add (GST_BIN (channel->pipe), new_element);

  gst_element_link_pads (channel->filesrc, "src", new_element, "sink");
  gst_element_link_pads (new_element, "src_00", channel->volenv, "sink");

  /* add a ghost pad */
  sprintf (buffer, "channel%d", id);
  gst_element_add_ghost_pad (channel->pipe,
      gst_element_get_pad (channel->volenv, "src"), buffer);


#ifdef DEBUG
  printf ("DEBUG : c_i_p : end function\n");
#endif

  return channel;
}

void
destroy_input_channel (input_channel_t * channel)
{
  /* 
   * destroy an input channel
   */

#ifdef DEBUG
  printf ("DEBUG : d_i_p : start\n");
#endif

  /* destroy elements */

  gst_object_unref (GST_OBJECT (channel->pipe));

  free (channel);
}

void
env_register_cp (GstElement * volenv, double cp_time, double cp_level)
{
  char buffer[30];

  sprintf (buffer, "%f:%f", cp_time, cp_level);
  g_object_set (G_OBJECT (volenv), "controlpoint", buffer, NULL);

}
