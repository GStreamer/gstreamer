
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <gst/gst.h>

extern GstElement *video_render_queue, *audio_render_queue;

void avi_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) 
{
  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {

    gst_pad_connect(pad,
                    gst_element_get_pad(audio_render_queue,"sink"));

  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
  //} else if (0) {

    gst_pad_connect(pad,
                    gst_element_get_pad(video_render_queue,"sink"));
  }
  g_print("\n");
}

