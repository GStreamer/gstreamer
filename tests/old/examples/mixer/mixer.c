#include <stdlib.h>
#include <gst/gst.h>
#include "mixer.h"

#define DEBUG

/* function prototypes */

input_pipe_t*	create_input_pipe (int id, char* location);
void			destroy_input_pipe (input_pipe_t *pipe);


gboolean playing;

/* example based on helloworld by thomas@apestaart.org
   demonstrates the adder plugin and the volume envelope plugin 
   work in progress but do try it out */

/* eos will be called when the src element has an end of stream */
void eos(GstElement *element) 
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

int main(int argc,char *argv[]) 
{
  input_pipe_t *channel_in1;
  input_pipe_t *channel_in2;
  
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

  channel_in1 = create_input_pipe (1, argv[1]);
  channel_in2 = create_input_pipe (2, argv[2]);


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

  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "0:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "5:1", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "10:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "15:0.000001", NULL);
  gtk_object_set(GTK_OBJECT(channel_in2->volenv), "controlpoint", "20:1", NULL);

  /* start playing */
  gst_element_set_state(main_bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(main_bin));
  }

  /* stop the bin */
  gst_element_set_state(main_bin, GST_STATE_NULL);

  destroy_input_pipe (channel_in1);
  destroy_input_pipe (channel_in2);
  
  gst_object_destroy(GST_OBJECT(audiosink));

  gst_object_destroy(GST_OBJECT(main_bin));

  exit(0);
}

input_pipe_t*
create_input_pipe (int id, char* location)
{
  /* create an input pipeline, reading from location
   * return a pointer to the pipe
   * return NULL if failed
   */

  input_pipe_t *pipe;
  char buffer[20]; 		/* hold the names */


#ifdef DEBUG
  printf ("DEBUG : c_i_p : creating pipe with id %d for file %s\n",
  		  id, location);
#endif
  
  /* allocate pipe */

  pipe = (input_pipe_t *) malloc (sizeof (input_pipe_t));
  if (pipe == NULL)
  {
    printf ("create_input_pipe : could not allocate memory for pipe !\n");
    return NULL;
  }

  /* create pipe */

  pipe->pipe = gst_bin_new ("pipeline");
     
  /* create elements */

  sprintf (buffer, "disksrc%d", id);
  pipe->disksrc = gst_elementfactory_make ("disksrc", buffer);
  gtk_object_set(GTK_OBJECT(pipe->disksrc),"location", location, NULL);
/*  gtk_signal_connect(GTK_OBJECT(disksrc1),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);
*/
  sprintf (buffer, "decoder%d", id);
  pipe->decoder = gst_elementfactory_make("mad", buffer);
  sprintf (buffer, "volume%d", id);
  pipe->volenv = gst_elementfactory_make("volenv", buffer);

  gst_bin_add(GST_BIN(pipe->pipe), pipe->disksrc);
  gst_bin_add(GST_BIN(pipe->pipe), pipe->decoder);
  gst_bin_add(GST_BIN(pipe->pipe), pipe->volenv);

  /* connect elements */

  gst_pad_connect(gst_element_get_pad(pipe->disksrc,"src"),
                  gst_element_get_pad(pipe->decoder,"sink"));
  gst_pad_connect(gst_element_get_pad(pipe->decoder,"src"),
                  gst_element_get_pad(pipe->volenv,"sink"));

  /* add a ghost pad */
  sprintf (buffer, "channel%d", id);
  gst_element_add_ghost_pad (pipe->pipe, 
                             gst_element_get_pad (pipe->volenv, "src"), buffer);

#ifdef DEBUG
  printf ("DEBUG : c_i_p : end function\n");
#endif

  return pipe;
}

void
destroy_input_pipe (input_pipe_t *pipe)
{
  /* 
   * destroy an input pipeline
   */
   
#ifdef DEBUG
  printf ("DEBUG : d_i_p : start\n");
#endif
  
  /* destroy elements */

  gst_object_destroy (GST_OBJECT (pipe->disksrc));
  gst_object_destroy (GST_OBJECT (pipe->decoder));
  gst_object_destroy (GST_OBJECT (pipe->volenv));

  gst_object_destroy (GST_OBJECT (pipe->pipe));

  free (pipe);
}




