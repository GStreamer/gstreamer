#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>

static gboolean use_postproc;
static GOptionEntry g_options[] = {
  {"postproc", 'p', 0, G_OPTION_ARG_NONE, &use_postproc,
      "use vaapipostproc to rotate rather than vaapisink", NULL},
  {NULL,}
};

typedef struct _CustomData
{
  GstElement *pipeline;
  GstElement *rotator;
  GMainLoop *loop;
  gboolean orient_automatic;
} AppData;

static void
send_rotate_event (AppData * data)
{
  gboolean res = FALSE;
  GstEvent *event;
  static gint counter = 0;
  const static gchar *tags[] = { "rotate-90", "rotate-180", "rotate-270",
    "rotate-0", "flip-rotate-0", "flip-rotate-90", "flip-rotate-180",
    "flip-rotate-270",
  };

  event = gst_event_new_tag (gst_tag_list_new (GST_TAG_IMAGE_ORIENTATION,
          tags[counter++ % G_N_ELEMENTS (tags)], NULL));

  /* Send the event */
  g_print ("Sending event %" GST_PTR_FORMAT ": ", event);
  res = gst_element_send_event (data->pipeline, event);
  g_print ("%s\n", res ? "ok" : "failed");

}

static void
keyboard_cb (const gchar * key, AppData * data)
{
  switch (g_ascii_tolower (key[0])) {
    case 'r':
      send_rotate_event (data);
      break;
    case 's':{
      if (use_postproc) {
        g_object_set (G_OBJECT (data->rotator), "video-direction",
            GST_VIDEO_ORIENTATION_AUTO, NULL);
      } else {
        /* rotation=360 means auto for vaapisnk */
        g_object_set (G_OBJECT (data->rotator), "rotation", 360, NULL);
      }
      break;
    }
    case 'q':
      g_main_loop_quit (data->loop);
      break;
    default:
      break;
  }
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  AppData *data = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ELEMENT:
    {
      GstNavigationMessageType mtype = gst_navigation_message_get_type (msg);
      if (mtype == GST_NAVIGATION_MESSAGE_EVENT) {
        GstEvent *ev = NULL;

        if (gst_navigation_message_parse_event (msg, &ev)) {
          GstNavigationEventType type = gst_navigation_event_get_type (ev);
          if (type == GST_NAVIGATION_EVENT_KEY_PRESS) {
            const gchar *key;

            if (gst_navigation_event_parse_key_event (ev, &key))
              keyboard_cb (key, data);
          }
        }
        if (ev)
          gst_event_unref (ev);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
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

  keyboard_cb (str, data);
  g_free (str);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  AppData data;
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;
  GOptionContext *ctx;
  GError *err = NULL;
  guint srcid;

  /* Initialize GStreamer */
  ctx = g_option_context_new ("- test options");
  if (!ctx)
    return -1;
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_main_entries (ctx, g_options, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, NULL))
    return -1;
  g_option_context_free (ctx);

  /* Print usage map */
  g_print ("USAGE: Choose one of the following options, then press enter:\n"
      " 'r' to send image-orientation tag event\n"
      " 's' to set orient-automatic\n" " 'Q' to quit\n");

  if (use_postproc) {
    data.pipeline =
        gst_parse_launch ("videotestsrc ! vaapipostproc name=pp ! xvimagesink",
        &err);
  } else {
    data.pipeline =
        gst_parse_launch ("videotestsrc ! vaapisink name=sink", &err);
  }
  if (err) {
    g_printerr ("failed to create pipeline: %s\n", err->message);
    g_error_free (err);
    return -1;
  }

  if (use_postproc)
    data.rotator = gst_bin_get_by_name (GST_BIN (data.pipeline), "pp");
  else
    data.rotator = gst_bin_get_by_name (GST_BIN (data.pipeline), "sink");
  srcid = gst_bus_add_watch (GST_ELEMENT_BUS (data.pipeline), bus_msg, &data);

  /* Add a keyboard watch so we get notified of keystrokes */
  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    goto bail;
  }

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  gst_element_set_state (data.pipeline, GST_STATE_NULL);

bail:
  /* Free resources */
  g_source_remove (srcid);
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);

  gst_object_unref (data.rotator);
  gst_object_unref (data.pipeline);

  return 0;
}
