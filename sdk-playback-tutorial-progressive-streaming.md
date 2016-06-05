# Playback tutorial 4: Progressive streaming

# Goal

[Basic tutorial 12:
Streaming](Basic%2Btutorial%2B12%253A%2BStreaming.html) showed how to
enhance the user experience in poor network conditions, by taking
buffering into account. This tutorial further expands [Basic tutorial
12: Streaming](Basic%2Btutorial%2B12%253A%2BStreaming.html) by enabling
the local storage of the streamed media, and describes the advantages of
this technique. In particular, it shows:

  - How to enable progressive downloading
  - How to know what has been downloaded
  - How to know where it has been downloaded
  - How to limit the amount of downloaded data that is kept

# Introduction

When streaming, data is fetched from the network and a small buffer of
future-data is kept to ensure smooth playback (see [Basic tutorial 12:
Streaming](Basic%2Btutorial%2B12%253A%2BStreaming.html)). However, data
is discarded as soon as it is displayed or rendered (there is no
past-data buffer). This means, that if a user wants to jump back and
continue playback from a point in the past, data needs to be
re-downloaded.

Media players tailored for streaming, like YouTube, usually keep all
downloaded data stored locally for this contingency. A graphical widget
is also normally used to show how much of the file has already been
downloaded.

`playbin` offers similar functionalities through the `DOWNLOAD` flag
which stores the media in a local temporary file for faster playback of
already-downloaded chunks.

This code also shows how to use the Buffering Query, which allows
knowing what parts of the file are available.

# A network-resilient example with local storage

Copy this code into a text file named `playback-tutorial-4.c`.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>This tutorial is included in the SDK since release 2012.7. If you cannot find it in the downloaded code, please install the latest release of the GStreamer SDK.</p></td>
</tr>
</tbody>
</table>

**playback-tutorial-4.c**

``` lang=c
#include <gst/gst.h>
#include <string.h>

#define GRAPH_LENGTH 80

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
    GstFormat format = GST_FORMAT_TIME;
    gint64 position = 0, duration = 0;

    memset (graph, ' ', GRAPH_LENGTH);
    graph[GRAPH_LENGTH] = '\0';

    n_ranges = gst_query_get_n_buffering_ranges (query);
    for (range = 0; range < n_ranges; range++) {
      gint64 start, stop;
      gst_query_parse_nth_buffering_range (query, range, &start, &stop);
      start = start * GRAPH_LENGTH / 100;
      stop = stop * GRAPH_LENGTH / 100;
      for (i = (gint)start; i < stop; i++)
        graph [i] = '-';
    }
    if (gst_element_query_position (data->pipeline, &format, &position) &&
        GST_CLOCK_TIME_IS_VALID (position) &&
        gst_element_query_duration (data->pipeline, &format, &duration) &&
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
  pipeline = gst_parse_launch ("playbin uri=http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);
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

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><div id="expander-1295673640" class="expand-container">
<div id="expander-control-1295673640" class="expand-control">
<span class="expand-control-icon"><img src="images/icons/grey_arrow_down.gif" class="expand-control-image" /></span><span class="expand-control-text">Need help? (Click to expand)</span>
</div>
<div id="expander-content-1295673640" class="expand-content">
<p>If you need help to compile this code, refer to the <strong>Building the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Build">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Build">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Build">Windows</a>, or use this specific command on Linux:</p>
<div class="panel" style="border-width: 1px;">
<div class="panelContent">
<p><code>gcc playback-tutorial-3.c -o playback-tutorial-3 `pkg-config --cflags --libs gstreamer-1.0`</code></p>
</div>
</div>
<p>If you need help to run this code, refer to the <strong>Running the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Run">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Run">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Run">Windows</a></p>
<p>This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. In the console window, you should see a message indicating where the media is being stored, and a text graph representing the downloaded portions and the current position. A buffering message appears whenever buffering is required, which might never happen is your network connection is fast enough</p>
<p>Required libraries: <code>gstreamer-1.0</code></p>
</div>
</div></td>
</tr>
</tbody>
</table>

# Walkthrough

This code is based on that of [Basic tutorial 12:
Streaming](Basic%2Btutorial%2B12%253A%2BStreaming.html). Let’s review
only the differences.

#### Setup

``` lang=c
/* Set the download flag */
g_object_get (pipeline, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_DOWNLOAD;
g_object_set (pipeline, "flags", flags, NULL);
```

By setting this flag, `playbin` instructs its internal queue (a
`queue2` element, actually) to store all downloaded
data.

``` lang=c
g_signal_connect (pipeline, "deep-notify::temp-location", G_CALLBACK (got_location), NULL);
```

`deep-notify` signals are emitted by `GstObject` elements (like
`playbin`) when the properties of any of their children elements
change. In this case we want to know when the `temp-location` property
changes, indicating that the `queue2` has decided where to store the
downloaded
data.

``` lang=c
static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}
```

The `temp-location` property is read from the element that triggered the
signal (the `queue2`) and printed on screen.

When the pipeline state changes from `PAUSED` to `READY`, this file is
removed. As the comment reads, you can keep it by setting the
`temp-remove` property of the `queue2` to `FALSE`.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>On Windows this file is usually created inside the <code>Temporary Internet Files</code> folder, which might hide it from Windows Explorer. If you cannot find the downloaded files, try to use the console.</p></td>
</tr>
</tbody>
</table>

#### User Interface

In `main` we also install a timer which we use to refresh the UI every
second.

``` lang=c
/* Register a function that GLib will call every second */
g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);
```

The `refresh_ui` method queries the pipeline to find out which parts of
the file have been downloaded and what the currently playing position
is. It builds a graph to display this information (sort of a text-mode
user interface) and prints it on screen, overwriting the previous one so
it looks like it is animated:

    [---->-------                ]

The dashes ‘`-`’ indicate the downloaded parts, and the greater-than
sign ‘`>`’ shows the current position (turning into an ‘`X`’ when the
pipeline is paused). Keep in mind that if your network is fast enough,
you will not see the download bar (the dashes) advance at all; it will
be completely full from the beginning.

``` lang=c
static gboolean refresh_ui (CustomData *data) {
  GstQuery *query;
  gboolean result;
  query = gst_query_new_buffering (GST_FORMAT_PERCENT);
  result = gst_element_query (data->pipeline, query);
```

The first thing we do in `refresh_ui` is construct a new Buffering
`GstQuery` with `gst_query_new_buffering()` and pass it to the pipeline
(`playbin`) with `gst_element_query()`. In [Basic tutorial 4: Time
management](Basic%2Btutorial%2B4%253A%2BTime%2Bmanagement.html) we have
already seen how to perform simple queries like Position and Duration
using specific methods. More complex queries, like Buffering, need to
use the more general `gst_element_query()`.

The Buffering query can be made in different `GstFormat` (TIME, BYTES,
PERCENTAGE and a few more). Not all elements can answer the query in all
the formats, so you need to check which ones are supported in your
particular pipeline. If `gst_element_query()` returns `TRUE`, the query
succeeded. The answer to the query is contained in the same
`GstQuery` structure we created, and can be retrieved using multiple
parse methods:

``` lang=c
n_ranges = gst_query_get_n_buffering_ranges (query);
for (range = 0; range < n_ranges; range++) {
  gint64 start, stop;
  gst_query_parse_nth_buffering_range (query, range, &start, &stop);
  start = start * GRAPH_LENGTH / 100;
  stop = stop * GRAPH_LENGTH / 100;
  for (i = (gint)start; i < stop; i++)
    graph [i] = '-';
}
```

Data does not need to be downloaded in consecutive pieces from the
beginning of the file: Seeking, for example, might force to start
downloading from a new position and leave a downloaded chunk behind.
Therefore, `gst_query_get_n_buffering_ranges()` returns the number of
chunks, or *ranges* of downloaded data, and then, the position and size
of each range is retrieved with `gst_query_parse_nth_buffering_range()`.

The format of the returned values (start and stop position for each
range) depends on what we requested in the
`gst_query_new_buffering()` call. In this case, PERCENTAGE. These
values are used to generate the graph.

``` lang=c
if (gst_element_query_position (data->pipeline, &format, &position) &&
    GST_CLOCK_TIME_IS_VALID (position) &&
    gst_element_query_duration (data->pipeline, &format, &duration) &&
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
`cb_message` method will have set the pipeline to `PAUSED`, so we print
an ‘`X`’. If the buffering level is 100% the pipeline is in the
`PLAYING` state and we print a ‘`>`’.

``` lang=c
if (data->buffering_level < 100) {
  g_print (" Buffering: %3d%%", data->buffering_level);
} else {
  g_print ("                ");
}
```

Finally, if the buffering level is below 100%, we report this
information (and delete it otherwise).

#### Limiting the size of the downloaded file

``` lang=c
/* Uncomment this line to limit the amount of downloaded data */
/* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */
```

Uncomment line 139 to see how this can be achieved. This reduces the
size of the temporary file, by overwriting already played regions.
Observe the download bar to see which regions are kept available in the
file.

# Conclusion

This tutorial has shown:

  - How to enable progressive downloading with the
    `GST_PLAY_FLAG_DOWNLOAD` `playbin` flag
  - How to know what has been downloaded using a Buffering `GstQuery`
  - How to know where it has been downloaded with the
    `deep-notify::temp-location` signal
  - How to limit the size of the temporary file with
    the `ring-buffer-max-size` property of `playbin`.

It has been a pleasure having you here, and see you soon\!

## Attachments:

![](images/icons/bullet_blue.gif)
[playback-tutorial-4.c](attachments/327808/2424846.c) (text/plain)
![](images/icons/bullet_blue.gif)
[vs2010.zip](attachments/327808/2424847.zip) (application/zip)
