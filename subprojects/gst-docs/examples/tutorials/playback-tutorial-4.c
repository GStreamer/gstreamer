#include <gst/gst.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define GRAPH_LENGTH 78

/* playbin flags */
typedef enum
{
  GST_PLAY_FLAG_DOWNLOAD = (1 << 7)     /* Enable progressive download (on selected formats) */
} GstPlayFlags;

typedef struct _CustomData
{
  gboolean is_live;
  GstElement *pipeline;
  GMainLoop *loop;
  gint buffering_level;
} CustomData;

static void
got_location (GstObject * gstobject, GstObject * prop_object, GParamSpec * prop,
    gpointer data)
{
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  g_free (location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}

static void
cb_message (GstBus * bus, GstMessage * msg, CustomData * data)
{

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    case GST_MESSAGE_BUFFERING:
      /* If the stream is live, we do not care about buffering. */
      if (data->is_live)
        break;

      gst_message_parse_buffering (msg, &data->buffering_level);

      /* Wait until buffering is complete before start/resume playing */
      if (data->buffering_level < 100)
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    case GST_MESSAGE_CLOCK_LOST:
      /* Get a new clock */
      gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    default:
      /* Unhandled message */
      break;
  }
}

static gboolean
refresh_ui (CustomData * data)
{
  GstQuery *query;
  gboolean result;

  query = gst_query_new_buffering (GST_FORMAT_PERCENT);
  result = gst_element_query (data->pipeline, query);
  if (result) {
    gint n_ranges, range, i;
    gchar graph[GRAPH_LENGTH + 1];
    gint64 position = 0, duration = 0;

    memset (graph, ' ', GRAPH_LENGTH);
    graph[GRAPH_LENGTH] = '\0';

    n_ranges = gst_query_get_n_buffering_ranges (query);
    for (range = 0; range < n_ranges; range++) {
      gint64 start, stop;
      gst_query_parse_nth_buffering_range (query, range, &start, &stop);
      start = start * GRAPH_LENGTH / (stop - start);
      stop = stop * GRAPH_LENGTH / (stop - start);
      for (i = (gint) start; i < stop; i++)
        graph[i] = '-';
    }
    if (gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)
        && GST_CLOCK_TIME_IS_VALID (position)
        && gst_element_query_duration (data->pipeline, GST_FORMAT_TIME,
            &duration) && GST_CLOCK_TIME_IS_VALID (duration)) {
      i = (gint) (GRAPH_LENGTH * (double) position / (double) (duration + 1));
      graph[i] = data->buffering_level < 100 ? 'X' : '>';
    }
    g_print ("[%s]", graph);
    if (data->buffering_level < 100) {
      g_print (" Buffering: %3d%%", data->buffering_level);
    } else {
      g_print ("                ");
    }
    g_print ("\r");
  }

  return TRUE;

}

int
tutorial_main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;
  GMainLoop *main_loop;
  CustomData data;
  guint flags;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  data.buffering_level = 100;

  /* Build the pipeline */
  pipeline =
      gst_parse_launch
      ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm",
      NULL);
  bus = gst_element_get_bus (pipeline);

  /* Set the download flag */
  g_object_get (pipeline, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_DOWNLOAD;
  g_object_set (pipeline, "flags", flags, NULL);

  /* Uncomment this line to limit the amount of downloaded data */
  /* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    data.is_live = TRUE;
  }

  main_loop = g_main_loop_new (NULL, FALSE);
  data.loop = main_loop;
  data.pipeline = pipeline;

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (cb_message), &data);
  g_signal_connect (pipeline, "deep-notify::temp-location",
      G_CALLBACK (got_location), NULL);

  /* Register a function that GLib will call every second */
  g_timeout_add_seconds (1, (GSourceFunc) refresh_ui, &data);

  g_main_loop_run (main_loop);

  /* Free resources */
  g_main_loop_unref (main_loop);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_print ("\n");
  return 0;
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
