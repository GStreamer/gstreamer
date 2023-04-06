# Playback tutorial 4: Progressive streaming


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

[](tutorials/basic/streaming.md) showed how to
enhance the user experience in poor network conditions, by taking
buffering into account. This tutorial further expands
[](tutorials/basic/streaming.md) by enabling
the local storage of the streamed media, and describes the advantages of
this technique. In particular, it shows:

  - How to enable progressive downloading
  - How to know what has been downloaded
  - How to know where it has been downloaded
  - How to limit the amount of downloaded data that is kept

## Introduction

When streaming, data is fetched from the network and a small buffer of
future-data is kept to ensure smooth playback (see
[](tutorials/basic/streaming.md)). However, data
is discarded as soon as it is displayed or rendered (there is no
past-data buffer). This means, that if a user wants to jump back and
continue playback from a point in the past, data needs to be
re-downloaded.

Media players tailored for streaming, like YouTube, usually keep all
downloaded data stored locally for this contingency. A graphical widget
is also normally used to show how much of the file has already been
downloaded.

`playbin` offers similar functionalities through the `DOWNLOAD` flag
which stores the media in a local temporary file for faster playback of
already-downloaded chunks.

This code also shows how to use the Buffering Query, which allows
knowing what parts of the file are available.

## A network-resilient example with local storage

Copy this code into a text file named `playback-tutorial-4.c`.

**playback-tutorial-4.c**

``` c
#include <gst/gst.h>
#include <string.h>

#define GRAPH_LENGTH 78

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7) /* Enable progressive download (on selected formats) */
} GstPlayFlags;

typedef struct _CustomData {
  gboolean is_live;
  GstElement *pipeline;
  GMainLoop *loop;
  gint buffering_level;
} CustomData;

static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  g_free (location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}

static void cb_message (GstBus *bus, GstMessage *msg, CustomData *data) {

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
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
      if (data->is_live) break;

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

static gboolean refresh_ui (CustomData *data) {
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
      for (i = (gint)start; i < stop; i++)
        graph [i] = '-';
    }
    if (gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position) &&
        GST_CLOCK_TIME_IS_VALID (position) &&
        gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &duration) &&
        GST_CLOCK_TIME_IS_VALID (duration)) {
      i = (gint)(GRAPH_LENGTH * (double)position / (double)(duration + 1));
      graph [i] = data->buffering_level < 100 ? 'X' : '>';
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

int main(int argc, char *argv[]) {
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
  pipeline = gst_parse_launch ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
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
  g_signal_connect (pipeline, "deep-notify::temp-location", G_CALLBACK (got_location), NULL);

  /* Register a function that GLib will call every second */
  g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);

  g_main_loop_run (main_loop);

  /* Free resources */
  g_main_loop_unref (main_loop);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_print ("\n");
  return 0;
}
```

> ![information] If you need help to compile this code, refer to the
> **Building the tutorials** section for your platform: [Mac] or
> [Windows] or use this specific command on Linux:
>
> `` gcc playback-tutorial-4.c -o playback-tutorial-4 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Mac OS X], [Windows][1], for
> [iOS] or for [android].
>
> This tutorial opens a window and displays a movie, with accompanying
> audio. The media is fetched from the Internet, so the window might
> take a few seconds to appear, depending on your connection
> speed. In the console window, you should see a message indicating
> where the media is being stored, and a text graph representing the
> downloaded portions and the current position. A buffering message
> appears whenever buffering is required, which might never happen is
> your network connection is fast enough
>
> Required libraries: `gstreamer-1.0`


## Walkthrough

This code is based on that of [](tutorials/basic/streaming.md). Let’s review
only the differences.

### Setup

``` c
/* Set the download flag */
g_object_get (pipeline, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_DOWNLOAD;
g_object_set (pipeline, "flags", flags, NULL);
```

By setting this flag, `playbin` instructs its internal queue (a
`queue2` element, actually) to store all downloaded
data.

``` c
g_signal_connect (pipeline, "deep-notify::temp-location", G_CALLBACK (got_location), NULL);
```

`deep-notify` signals are emitted by `GstObject` elements (like
`playbin`) when the properties of any of their children elements
change. In this case we want to know when the `temp-location` property
changes, indicating that the `queue2` has decided where to store the
downloaded
data.

``` c
static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  g_free (location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}
```

The `temp-location` property is read from the element that triggered the
signal (the `queue2`) and printed on screen.

When the pipeline state changes from `PAUSED` to `READY`, this file is
removed. As the comment reads, you can keep it by setting the
`temp-remove` property of the `queue2` to `FALSE`.

> ![warning]
> On Windows this file is usually created inside the `Temporary Internet Files` folder, which might hide it from Windows Explorer. If you cannot find the downloaded files, try to use the console.

### User Interface

In `main` we also install a timer which we use to refresh the UI every
second.

``` c
/* Register a function that GLib will call every second */
g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);
```

The `refresh_ui` method queries the pipeline to find out which parts of
the file have been downloaded and what the currently playing position
is. It builds a graph to display this information (sort of a text-mode
user interface) and prints it on screen, overwriting the previous one so
it looks like it is animated:

    [---->-------                ]

The dashes ‘`-`’ indicate the downloaded parts, and the greater-than
sign ‘`>`’ shows the current position (turning into an ‘`X`’ when the
pipeline is paused). Keep in mind that if your network is fast enough,
you will not see the download bar (the dashes) advance at all; it will
be completely full from the beginning.

``` c
static gboolean refresh_ui (CustomData *data) {
  GstQuery *query;
  gboolean result;
  query = gst_query_new_buffering (GST_FORMAT_PERCENT);
  result = gst_element_query (data->pipeline, query);
```

The first thing we do in `refresh_ui` is construct a new Buffering
`GstQuery` with `gst_query_new_buffering()` and pass it to the pipeline
(`playbin`) with `gst_element_query()`. In [](tutorials/basic/time-management.md) we have
already seen how to perform simple queries like Position and Duration
using specific methods. More complex queries, like Buffering, need to
use the more general `gst_element_query()`.

The Buffering query can be made in different `GstFormat` (TIME, BYTES,
PERCENTAGE and a few more). Not all elements can answer the query in all
the formats, so you need to check which ones are supported in your
particular pipeline. If `gst_element_query()` returns `TRUE`, the query
succeeded. The answer to the query is contained in the same
`GstQuery` structure we created, and can be retrieved using multiple
parse methods:

``` c
n_ranges = gst_query_get_n_buffering_ranges (query);
for (range = 0; range < n_ranges; range++) {
  gint64 start, stop;
  gst_query_parse_nth_buffering_range (query, range, &start, &stop);
  start = start * GRAPH_LENGTH / (stop - start);
  stop = stop * GRAPH_LENGTH / (stop - start);
  for (i = (gint)start; i < stop; i++)
    graph [i] = '-';
}
```

Data does not need to be downloaded in consecutive pieces from the
beginning of the file: Seeking, for example, might force to start
downloading from a new position and leave a downloaded chunk behind.
Therefore, `gst_query_get_n_buffering_ranges()` returns the number of
chunks, or *ranges* of downloaded data, and then, the position and size
of each range is retrieved with `gst_query_parse_nth_buffering_range()`.

The format of the returned values (start and stop position for each
range) depends on what we requested in the
`gst_query_new_buffering()` call. In this case, PERCENTAGE. These
values are used to generate the graph.

``` c
if (gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position) &&
    GST_CLOCK_TIME_IS_VALID (position) &&
    gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &duration) &&
    GST_CLOCK_TIME_IS_VALID (duration)) {
  i = (gint)(GRAPH_LENGTH * (double)position / (double)(duration + 1));
  graph [i] = data->buffering_level < 100 ? 'X' : '>';
}
```

Next, the current position is queried. It could be queried in the
PERCENT format, so code similar to the one used for the ranges is used,
but currently this format is not well supported for position queries.
Instead, we use the TIME format and also query the duration to obtain a
percentage.

The current position is indicated with either a ‘`>`’ or an ‘`X`’
depending on the buffering level. If it is below 100%, the code in the
`cb_message` method will have set the pipeline to `PAUSED`, so we print
an ‘`X`’. If the buffering level is 100% the pipeline is in the
`PLAYING` state and we print a ‘`>`’.

``` c
if (data->buffering_level < 100) {
  g_print (" Buffering: %3d%%", data->buffering_level);
} else {
  g_print ("                ");
}
```

Finally, if the buffering level is below 100%, we report this
information (and delete it otherwise).

### Limiting the size of the downloaded file

``` c
/* Uncomment this line to limit the amount of downloaded data */
/* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */
```

Uncomment line 139 to see how this can be achieved. This reduces the
size of the temporary file, by overwriting already played regions.
Observe the download bar to see which regions are kept available in the
file.

## Conclusion

This tutorial has shown:

  - How to enable progressive downloading with the
    `GST_PLAY_FLAG_DOWNLOAD` `playbin` flag
  - How to know what has been downloaded using a Buffering `GstQuery`
  - How to know where it has been downloaded with the
    `deep-notify::temp-location` signal
  - How to limit the size of the temporary file with
    the `ring-buffer-max-size` property of `playbin`.

It has been a pleasure having you here, and see you soon!

  [information]: images/icons/emoticons/information.svg
  [Mac]: installing/on-mac-osx.md
  [Windows]: installing/on-windows.md
  [Mac OS X]: installing/on-mac-osx.md#building-the-tutorials
  [1]: installing/on-windows.md#running-the-tutorials
  [iOS]: installing/for-ios-development.md#building-the-tutorials
  [android]: installing/for-android-development.md#building-the-tutorials
  [warning]: images/icons/emoticons/warning.svg
