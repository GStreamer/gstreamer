/*
 * mixer.c - stereo audio mixer - thomas@apestaart.org
 * example based on helloworld by
 * demonstrates the adder plugin and the volume envelope plugin 
 * work in progress but do try it out 
 * 
 * Latest change : 	16/04/2001
 * 					mixer & adder plugin now work with variable-size input buffers 
 * Version :		0.2
 */

#include <stdlib.h>
#include <gst/gst.h>
#include "mixer.h"
#include <unistd.h>

//#define DEBUG

/* function prototypes */

input_channel_t*	create_input_channel (int id, char* location);
void				destroy_input_channel (input_channel_t *pipe);


gboolean playing;


/* eos will be called when the src element has an end of stream */
void eos(GstElement *element) 
{
  g_print("have eos, quitting ?\n");

//  playing = FALSE;
}

static void
gst_play_have_type (GstElement *sink, GstElement *sink2, gpointer data)
{
  GST_DEBUG (0,"GstPipeline: play have type %p\n", (gboolean *)data);
 
  *(gboolean *)data = TRUE;
}

static GstCaps*
gst_play_typefind (GstBin *bin, GstElement *element)
{
  gboolean found = FALSE;
  GstElement *typefind;
  GstCaps *caps = NULL;

  GST_DEBUG (0,"GstPipeline: typefind for element \"%s\" %p\n",
             GST_ELEMENT_NAME(element), &found);
 
  typefind = gst_elementfactory_make ("typefind", "typefind");
  g_return_val_if_fail (typefind != NULL, FALSE);

  gtk_signal_connect (GTK_OBJECT (typefind), "have_type",  
                      GTK_SIGNAL_FUNC (gst_play_have_type), &found);
 
  gst_pad_connect (gst_element_get_pad (element, "src"),
                   gst_element_get_pad (typefind, "sink"));
  gst_bin_add (bin, typefind);
  
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  
  // push a buffer... the have_type signal handler will set the found flag
  gst_bin_iterate (bin);
  
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);

  caps = gst_pad_get_caps (gst_element_get_pad (element, "src"));

  gst_pad_disconnect (gst_element_get_pad (element, "src"),
                      gst_element_get_pad (typefind, "sink"));
  gst_bin_remove (bin, typefind);
  gst_object_unref (GST_OBJECT (typefind));
                   
  return caps;
}

int main(int argc,char *argv[]) 
{
  input_channel_t *channel_in1;
  input_channel_t *channel_in2;
  
  GstElement *main_bin;
  GstElement *adder;
  GstElement *audiosink;

  GstPad *pad; /* to request pads for the adder */

  gst_init(&argc,&argv);

  if (argc != 3) {
    g_print("usage: %s <filename1> <filename2>\n", argv[0]);
    exit(-1);
  }

  /* create input channels */

  channel_in1 = create_input_channel (1, argv[1]);
  channel_in2 = create_input_channel (2, argv[2]);


  /* create adder */
  adder = gst_elementfactory_make("adder", "adderel");

  /* create an audio sink */
  audiosink = gst_elementfactory_make("esdsink", "play_audio");

  /* now create main bin */
  main_bin = gst_bin_new("bin");

  gst_bin_add(GST_BIN(main_bin), channel_in1->pipe);
  gst_bin_add(GST_BIN(main_bin), channel_in2->pipe);
  gst_bin_add(GST_BIN(main_bin), adder);
  gst_bin_add(GST_BIN(main_bin), audiosink);

  /* request pads and connect to adder */

  pad = gst_element_request_pad_by_name (adder, "sink%d");
  g_print ("new pad %s\n", gst_pad_get_name (pad));
  gst_pad_connect (gst_element_get_pad (channel_in1->pipe, "channel1"), pad);
  pad = gst_element_request_pad_by_name (adder, "sink%d");
  g_print ("new pad %s\n", gst_pad_get_name (pad));
  gst_pad_connect (gst_element_get_pad (channel_in2->pipe, "channel2"), pad);

  /* connect adder and audiosink */

  gst_pad_connect(gst_element_get_pad(adder,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  /* register the volume envelope */

  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "0:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "5:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "10:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "15:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "20:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "40:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in1->volenv), "controlpoint", "45:0.5", NULL);

  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "0:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "5:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "10:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "15:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "20:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "40:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "45:0.5", NULL);

  /* sleep a few seconds */

  printf ("Sleeping a few seconds ...\n");
  sleep (2);
  printf ("Waking up ...\n");
  
  /* start playing */
  gst_element_set_state(main_bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(main_bin));
  }

  /* stop the bin */
  gst_element_set_state(main_bin, GST_STATE_NULL);

  destroy_input_channel (channel_in1);
  destroy_input_channel (channel_in2);
  
  gst_object_destroy(GST_OBJECT(audiosink));

  gst_object_destroy(GST_OBJECT(main_bin));

  exit(0);
}

input_channel_t*
create_input_channel (int id, char* location)
{
  /* create an input channel, reading from location
   * return a pointer to the channel
   * return NULL if failed
   */

  input_channel_t *channel;
  
  char buffer[20]; 		/* hold the names */

  GstAutoplug *autoplug;
  GstCaps *srccaps;
  GstElement *new_element;  

#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating channel with id %d for file %s\n",
  		  id, location);
#endif
  
  /* allocate channel */

  channel = (input_channel_t *) malloc (sizeof (input_channel_t));
  if (channel == NULL)
  {
    printf ("create_input_channel : could not allocate memory for channel !\n");
    return NULL;
  }

  /* create channel */

#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating pipeline\n");
#endif

  channel->pipe = gst_bin_new ("pipeline");
  g_assert(channel->pipe != NULL);    
    
  /* create elements */

#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating disksrc\n");
#endif

  sprintf (buffer, "disksrc%d", id);
  channel->disksrc = gst_elementfactory_make ("disksrc", buffer);
  g_assert(channel->disksrc != NULL);    
  
  gtk_object_set(GTK_OBJECT(channel->disksrc),"location", location, NULL);

  /* add disksrc to the bin before autoplug */
  gst_bin_add(GST_BIN(channel->pipe), channel->disksrc);

  /* connect signal to eos of disksrc */
  gtk_signal_connect(GTK_OBJECT(channel->disksrc),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);


#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating volume envelope\n");
#endif

  sprintf (buffer, "volenv%d", id);
  channel->volenv = gst_elementfactory_make ("volenv", buffer);
  g_assert(channel->volenv != NULL);    

  /* autoplug the pipe */

#ifdef DEBUG
  printf ("DEBUG : c_i_p : getting srccaps\n");
#endif

  srccaps = gst_play_typefind (GST_BIN (channel->pipe), channel->disksrc);

  if (!srccaps) {
    g_print ("could not autoplug, unknown media type...\n");
    exit (-1);
  }

#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating autoplug\n");
#endif

  autoplug = gst_autoplugfactory_make ("static");
  g_assert (autoplug != NULL);

#ifdef DEBUG
  printf ("DEBUG : c_i_p : autoplugging\n");
#endif
 
  new_element = gst_autoplug_to_caps (autoplug, srccaps, 
  					gst_caps_new ("audio", "audio/raw", NULL), NULL);
 
  if (!new_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }
  
  gst_bin_add (GST_BIN(channel->pipe), channel->volenv);
  gst_bin_add (GST_BIN (channel->pipe), new_element);
  
  gst_element_connect (channel->disksrc, "src", new_element, "sink");
  gst_element_connect (new_element, "src_00", channel->volenv, "sink");
  
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
destroy_input_channel (input_channel_t *channel)
{
  /* 
   * destroy an input channel
   */
   
#ifdef DEBUG
  printf ("DEBUG : d_i_p : start\n");
#endif

  /* destroy elements */

  gst_object_destroy (GST_OBJECT (channel->pipe));

  free (channel);
}




