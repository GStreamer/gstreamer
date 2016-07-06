#include <stdio.h>
#include <string.h>
#include <gst/gst.h>

typedef struct _CustomData
{
  GstElement *pipeline;
  GstElement *video_sink;
  GstElement *src_sink;
  GMainLoop *loop;
  GstPad *src_pad;
  gboolean orient_automatic;
} AppData;

static void
send_rotate_event (AppData * data)
{
  gboolean res = FALSE;
  GstEvent *event;
  static gint counter = 0;
  const static gchar *tags[] = { "rotate-90", "rotate-180", "rotate-270",
    "rotate-0"
  };

  event = gst_event_new_tag (gst_tag_list_new (GST_TAG_IMAGE_ORIENTATION,
          tags[counter++ % G_N_ELEMENTS (tags)], NULL));

  /* Send the event */
  res = gst_pad_push_event (data->src_pad, event);
  g_print ("Sending event %p done: %d\n", event, res);
}

/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, AppData * data)
{
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;
  }

  switch (g_ascii_tolower (str[0])) {
    case 'r':
      send_rotate_event (data);
      break;
    case 's':{
      g_object_set (G_OBJECT (data->video_sink), "rotation", 360, NULL);
      break;
    }
    case 'q':
      g_main_loop_quit (data->loop);
      break;
    default:
      break;
  }

  g_free (str);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  AppData data;
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));

  /* Print usage map */
  g_print ("USAGE: Choose one of the following options, then press enter:\n"
      " 'r' to send image-orientation tag event\n \
      's' to set orient-automatic\n \
      'Q' to quit\n");

  data.pipeline =
      gst_parse_launch
      ("videotestsrc name=src ! vaapisink name=vaapisink", NULL);
  data.video_sink = gst_bin_get_by_name (GST_BIN (data.pipeline), "vaapisink");
  data.src_sink = gst_bin_get_by_name (GST_BIN (data.pipeline), "src");
  data.src_pad = gst_element_get_static_pad (data.src_sink, "src");

  /* Add a keyboard watch so we get notified of keystrokes */
  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);

  if (data.video_sink != NULL)
    gst_object_unref (data.video_sink);
  gst_object_unref (data.pipeline);

  return 0;
}
