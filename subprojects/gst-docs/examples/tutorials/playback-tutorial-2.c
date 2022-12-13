#include <stdio.h>
#include <gst/gst.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData
{
  GstElement *playbin;          /* Our one and only element */

  gint n_video;                 /* Number of embedded video streams */
  gint n_audio;                 /* Number of embedded audio streams */
  gint n_text;                  /* Number of embedded subtitle streams */

  gint current_video;           /* Currently playing video stream */
  gint current_audio;           /* Currently playing audio stream */
  gint current_text;            /* Currently playing subtitle stream */

  GMainLoop *main_loop;         /* GLib's Main Loop */
} CustomData;

/* playbin flags */
typedef enum
{
  GST_PLAY_FLAG_VIDEO = (1 << 0),       /* We want video output */
  GST_PLAY_FLAG_AUDIO = (1 << 1),       /* We want audio output */
  GST_PLAY_FLAG_TEXT = (1 << 2) /* We want subtitle output */
} GstPlayFlags;

/* Forward definition for the message and keyboard processing functions */
static gboolean handle_message (GstBus * bus, GstMessage * msg,
    CustomData * data);
static gboolean handle_keyboard (GIOChannel * source, GIOCondition cond,
    CustomData * data);

int
tutorial_main (int argc, char *argv[])
{
  CustomData data;
  GstBus *bus;
  GstStateChangeReturn ret;
  gint flags;
  GIOChannel *io_stdin;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.playbin = gst_element_factory_make ("playbin", "playbin");

  if (!data.playbin) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.playbin, "uri",
      "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.ogv",
      NULL);

  /* Set the subtitle URI to play and some font description */
  g_object_set (data.playbin, "suburi",
      "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer_gr.srt",
      NULL);
  g_object_set (data.playbin, "subtitle-font-desc", "Sans, 18", NULL);

  /* Set flags to show Audio, Video and Subtitles */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
  g_object_set (data.playbin, "flags", flags, NULL);

  /* Add a bus watch, so we get notified when a message arrives */
  bus = gst_element_get_bus (data.playbin);
  gst_bus_add_watch (bus, (GstBusFunc) handle_message, &data);

  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef G_OS_WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin);
    return -1;
  }

  /* Create a GLib Main Loop and set it to run */
  data.main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.main_loop);

  /* Free resources */
  g_main_loop_unref (data.main_loop);
  g_io_channel_unref (io_stdin);
  gst_object_unref (bus);
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  return 0;
}

/* Extract some metadata from the streams and print it on the screen */
static void
analyze_streams (CustomData * data)
{
  gint i;
  GstTagList *tags;
  gchar *str;
  guint rate;

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &data->n_video, NULL);
  g_object_get (data->playbin, "n-audio", &data->n_audio, NULL);
  g_object_get (data->playbin, "n-text", &data->n_text, NULL);

  g_print ("%d video stream(s), %d audio stream(s), %d text stream(s)\n",
      data->n_video, data->n_audio, data->n_text);

  g_print ("\n");
  for (i = 0; i < data->n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
    if (tags) {
      g_print ("video stream %d:\n", i);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
      g_print ("  codec: %s\n", str ? str : "unknown");
      g_free (str);
      gst_tag_list_free (tags);
    }
  }

  g_print ("\n");
  for (i = 0; i < data->n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->playbin, "get-audio-tags", i, &tags);
    if (tags) {
      g_print ("audio stream %d:\n", i);
      if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
        g_print ("  codec: %s\n", str);
        g_free (str);
      }
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
        g_print ("  bitrate: %d\n", rate);
      }
      gst_tag_list_free (tags);
    }
  }

  g_print ("\n");
  for (i = 0; i < data->n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_print ("subtitle stream %d:\n", i);
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    } else {
      g_print ("  no tags found\n");
    }
  }

  g_object_get (data->playbin, "current-video", &data->current_video, NULL);
  g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
  g_object_get (data->playbin, "current-text", &data->current_text, NULL);

  g_print ("\n");
  g_print
      ("Currently playing video stream %d, audio stream %d and subtitle stream %d\n",
      data->current_video, data->current_audio, data->current_text);
  g_print
      ("Type any number and hit ENTER to select a different subtitle stream\n");
}

/* Process messages from GStreamer */
static gboolean
handle_message (GstBus * bus, GstMessage * msg, CustomData * data)
{
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n",
          debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("End-Of-Stream reached.\n");
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_STATE_CHANGED:{
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state,
          &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        if (new_state == GST_STATE_PLAYING) {
          /* Once we are in the playing state, analyze the streams */
          analyze_streams (data);
        }
      }
    }
      break;
    default:
      break;
  }

  /* We want to keep receiving messages */
  return TRUE;
}

/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, CustomData * data)
{
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) == G_IO_STATUS_NORMAL) {
    int index = g_ascii_strtoull (str, NULL, 0);
    if (index < 0 || index >= data->n_text) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid subtitle stream index, set the current subtitle stream */
      g_print ("Setting current subtitle stream to %d\n", index);
      g_object_set (data->playbin, "current-text", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}

int
main (int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main (tutorial_main, argc, argv, NULL);
#else
  return tutorial_main (argc, argv);
#endif
}
