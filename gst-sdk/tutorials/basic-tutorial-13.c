#include <string.h>
#include <gst/gst.h>
  
typedef struct _CustomData {
  GstElement *pipeline;
  GMainLoop *loop;
  
  gboolean playing;  /* Playing or Paused */
  gdouble rate;      /* Current playback rate */
  gboolean backward; /* Forward or backwards */
} CustomData;
  
/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;
  
  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;
  }
  
  switch (g_ascii_tolower (str[0])) {
  case 'p':
    data->playing = !data->playing;
    gst_element_set_state (data->pipeline, data->playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    g_print ("Setting state to %s\n", data->playing ? "PLAYING" : "PAUSE");
    break;
  case 's':
    if (g_ascii_isupper (str[0])) {
      data->rate *= 2.0;
    } else {
      data->rate /= 2.0;
    }
    gst_element_send_event (data->pipeline,
        gst_event_new_step (GST_FORMAT_TIME, -1, data->rate, TRUE, FALSE));
    g_print ("Current rate: %g\n", data->rate);
    break;
  case 'd':
    data->backward = !data->backward;
    gst_element_send_event (data->pipeline,
        gst_event_new_seek (data->backward ? -data->rate : data->rate,
            GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0));
    g_print ("Going %s\n", data->backward ? "backwards" : "forward");
    break;
  case 'n':
    gst_element_send_event (data->pipeline,
        gst_event_new_step (GST_FORMAT_BUFFERS, 1, data->rate, TRUE, FALSE));
    g_print ("Stepping one frame\n");
    break;
  case 'q':
    g_main_loop_quit (data->loop);
    break;
  default:
    break;
  }
  
  g_free (str);
  
  return TRUE;
}
  
int main(int argc, char *argv[]) {
  CustomData data;
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;
  
  /* Initialize GStreamer */
  gst_init (&argc, &argv);
  
  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  
  /* Print usage map */
  g_print (
    "USAGE: Choose one of the following options, then press enter:\n"
    " 'P' to toggle between PAUSE and PLAY\n"
    " 'S' to increase playback speed, 's' to decrease playback speed\n"
    " 'D' to toggle playback direction\n"
    " 'N' to move to next frame (in the current direction, better in PAUSE)\n"
    " 'Q' to quit\n");
  
  /* Build the pipeline */
  data.pipeline = gst_parse_launch ("playbin2 uri=http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);
  
  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef _WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);
  
  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  data.playing = TRUE;
  data.rate = 1.0;
  
  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);
  
  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}
