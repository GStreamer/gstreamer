
#define BUFFER 1
#define VIDEO_DECODER "mpeg_play"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <gst/gst.h>

extern GstElement *show;

void avi_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) 
{
  GstElement *play;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    // construct internal pipeline elements
    play = gst_elementfactory_make("audiosink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to RUNNING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_RUNNING);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_PLAYING);
  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
	//} else if (0) {

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(show));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(show,"sink"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(video_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                    gst_element_get_pad(video_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(video_thread),"create_thread",TRUE,NULL);
    g_print("setting to RUNNING state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_RUNNING);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_PLAYING);
  }
  g_print("\n");
}

