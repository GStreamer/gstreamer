#  Basic tutorial 4: Time management


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

This tutorial shows how to use GStreamer time-related facilities. In
particular:

  - How to query the pipeline for information like stream position or
    duration.
  - How to seek (jump) to a different position (time) inside the
    stream.

## Introduction

`GstQuery` is a mechanism that allows asking an element or pad for a
piece of information. In this example we ask the pipeline if seeking is
allowed (some sources, like live streams, do not allow seeking). If it
is allowed, then, once the movie has been running for ten seconds, we
skip to a different position using a seek.

In the previous tutorials, once we had the pipeline setup and running,
our main function just sat and waited to receive an `ERROR` or an `EOS`
through the bus. Here, we modify this function to periodically wake up
and query the pipeline for the stream position, so we can print it on
the screen. This is similar to what a media player would do, updating the
user Interface on a periodic basis.

Finally, the stream duration is queried and updated whenever it changes.

## Seeking example

Copy this code into a text file named `basic-tutorial-4.c` (or find it
in your GStreamer installation).

**basic-tutorial-4.c**

``` c
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */
  gboolean playing;      /* Are we in the PLAYING state? */
  gboolean terminate;    /* Should we terminate execution? */
  gboolean seek_enabled; /* Is seeking enabled for this media? */
  gboolean seek_done;    /* Have we performed the seek already? */
  gint64 duration;       /* How long does this media last, in nanoseconds */
} CustomData;

/* Forward definition of the message processing function */
static void handle_message (CustomData *data, GstMessage *msg);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  data.playing = FALSE;
  data.terminate = FALSE;
  data.seek_enabled = FALSE;
  data.seek_done = FALSE;
  data.duration = GST_CLOCK_TIME_NONE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.playbin = gst_element_factory_make ("playbin", "playbin");

  if (!data.playbin) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

  /* Start playing */
  ret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.playbin);
  do {
    msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);

    /* Parse message */
    if (msg != NULL) {
      handle_message (&data, msg);
    } else {
      /* We got no message, this means the timeout expired */
      if (data.playing) {
        gint64 current = -1;

        /* Query the current position of the stream */
        if (!gst_element_query_position (data.playbin, GST_FORMAT_TIME, &current)) {
          g_printerr ("Could not query current position.\n");
        }

        /* If we didn't know it yet, query the stream duration */
        if (!GST_CLOCK_TIME_IS_VALID (data.duration)) {
          if (!gst_element_query_duration (data.playbin, GST_FORMAT_TIME, &data.duration)) {
            g_printerr ("Could not query current duration.\n");
          }
        }

        /* Print current position and total duration */
        g_print ("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
            GST_TIME_ARGS (current), GST_TIME_ARGS (data.duration));

        /* If seeking is enabled, we have not done it yet, and the time is right, seek */
        if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND) {
          g_print ("\nReached 10s, performing seek...\n");
          gst_element_seek_simple (data.playbin, GST_FORMAT_TIME,
              GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
          data.seek_done = TRUE;
        }
      }
    }
  } while (!data.terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  return 0;
}

static void handle_message (CustomData *data, GstMessage *msg) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      data->terminate = TRUE;
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nEnd-Of-Stream reached.\n");
      data->terminate = TRUE;
      break;
    case GST_MESSAGE_DURATION:
      /* The duration has changed, mark the current one as invalid */
      data->duration = GST_CLOCK_TIME_NONE;
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        g_print ("Pipeline state changed from %s to %s:\n",
            gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

        /* Remember whether we are in the PLAYING state or not */
        data->playing = (new_state == GST_STATE_PLAYING);

        if (data->playing) {
          /* We just moved to PLAYING. Check if seeking is possible */
          GstQuery *query;
          gint64 start, end;
          query = gst_query_new_seeking (GST_FORMAT_TIME);
          if (gst_element_query (data->playbin, query)) {
            gst_query_parse_seeking (query, NULL, &data->seek_enabled, &start, &end);
            if (data->seek_enabled) {
              g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
                  GST_TIME_ARGS (start), GST_TIME_ARGS (end));
            } else {
              g_print ("Seeking is DISABLED for this stream.\n");
            }
          }
          else {
            g_printerr ("Seeking query failed.");
          }
          gst_query_unref (query);
        }
      }
    } break;
    default:
      /* We should not reach here */
      g_printerr ("Unexpected message received.\n");
      break;
  }
  gst_message_unref (msg);
}
```

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> ``gcc basic-tutorial-4.c -o basic-tutorial-4 `pkg-config --cflags --libs gstreamer-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. 10 seconds into the movie it skips to a new position
>
>Required libraries: `gstreamer-1.0`

## Walkthrough

``` c
/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */
  gboolean playing;      /* Are we in the PLAYING state? */
  gboolean terminate;    /* Should we terminate execution? */
  gboolean seek_enabled; /* Is seeking enabled for this media? */
  gboolean seek_done;    /* Have we performed the seek already? */
  gint64 duration;       /* How long does this media last, in nanoseconds */
} CustomData;

/* Forward definition of the message processing function */
static void handle_message (CustomData *data, GstMessage *msg);
```

We start by defining a structure to contain all our information, so we
can pass it around to other functions. In particular, in this example we
move the message handling code to its own function
`handle_message` because it is growing a bit too big.

We then build a pipeline composed of a single element, a
`playbin`, which we already saw in [Basic tutorial 1: Hello
world!](tutorials/basic/hello-world.md). However,
`playbin` is in itself a pipeline, and in this case it is the only
element in the pipeline, so we directly use the `playbin` element. We
will skip the details: the URI of the clip is given to `playbin` via
the URI property and the pipeline is set to the playing state.

``` c
msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
    GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);
```

Previously we did not provide a timeout to
`gst_bus_timed_pop_filtered()`, meaning that it didn't return until a
message was received. Now we use a timeout of 100 milliseconds, so, if
no message is received during one tenth of a second, the function will return
`NULL`. We are going to use this logic to update our “UI”.

Note that the desired timeout must be specified as a `GstClockTime`, hence,
in nanoseconds. Numbers expressing different time units then, should be
multiplied by macros like `GST_SECOND` or `GST_MSECOND`. This also makes
your code more readable.

If we got a message, we process it in the `handle_message` function
(next subsection), otherwise:

### User interface refreshing

``` c
/* We got no message, this means the timeout expired */
if (data.playing) {
```

If the pipeline is in `PLAYING` state, it is time to refresh the screen.
We don't want to do anything if we are not in `PLAYING` state, because
most queries would fail.

We get here approximately 10 times per second, a good enough refresh
rate for our UI. We are going to print on screen the current media
position, which we can learn by querying the pipeline. This involves a
few steps that will be shown in the next subsection, but, since position
and duration are common enough queries, `GstElement` offers easier,
ready-made alternatives:

``` c
/* Query the current position of the stream */
if (!gst_element_query_position (data.pipeline, GST_FORMAT_TIME, &current)) {
  g_printerr ("Could not query current position.\n");
}
```

`gst_element_query_position()` hides the management of the query object
and directly provides us with the result.

``` c
/* If we didn't know it yet, query the stream duration */
if (!GST_CLOCK_TIME_IS_VALID (data.duration)) {
  if (!gst_element_query_duration (data.pipeline, GST_FORMAT_TIME, &data.duration)) {
     g_printerr ("Could not query current duration.\n");
  }
}
```

Now is a good moment to know the length of the stream, with
another `GstElement` helper function: `gst_element_query_duration()`

``` c
/* Print current position and total duration */
g_print ("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
    GST_TIME_ARGS (current), GST_TIME_ARGS (data.duration));
```

Note the usage of the `GST_TIME_FORMAT` and `GST_TIME_ARGS` macros to
provide a user-friendly representation of GStreamer times.

``` c
/* If seeking is enabled, we have not done it yet, and the time is right, seek */
if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND) {
  g_print ("\nReached 10s, performing seek...\n");
  gst_element_seek_simple (data.pipeline, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
  data.seek_done = TRUE;
}
```

Now we perform the seek, “simply” by
calling `gst_element_seek_simple()` on the pipeline. A lot of the
intricacies of seeking are hidden in this method, which is a good
thing!

Let's review the parameters:

`GST_FORMAT_TIME` indicates that we are specifying the destination in
time units. Other seek-formats use different units.

Then come the `GstSeekFlags`, let's review the most common:

`GST_SEEK_FLAG_FLUSH`: This discards all data currently in the pipeline
before doing the seek. Might pause a bit while the pipeline is refilled
and the new data starts to show up, but greatly increases the
“responsiveness” of the application. If this flag is not provided,
“stale” data might be shown for a while until the new position appears
at the end of the pipeline.

`GST_SEEK_FLAG_KEY_UNIT`: With most encoded video streams, seeking to
arbitrary positions is not possible but only to certain frames called Key Frames. When this
flag is used, the seek will actually move to the closest key frame and
start producing data straight away. If this flag is not used, the
pipeline will move internally to the closest key frame (it has no other
alternative) but data will not be shown until it reaches the requested
position. This last alternative is more accurate, but might take longer.

`GST_SEEK_FLAG_ACCURATE`: Some media clips do not provide enough
indexing information, meaning that seeking to arbitrary positions is
time-consuming. In these cases, GStreamer usually estimates the position
to seek to, and usually works just fine. If this precision is not good
enough for your case (you see seeks not going to the exact time you
asked for), then provide this flag. Be warned that it might take longer
to calculate the seeking position (very long, on some files).

Finally, we provide the position to seek to. Since we asked
for `GST_FORMAT_TIME`, the value must be in nanoseconds so we express
the time in seconds, for simplicity, and then multiply by `GST_SECOND`.

### Message Pump

The `handle_message` function processes all messages received through
the pipeline's bus. `ERROR` and `EOS` handling is the same as in previous
tutorials, so we skip to the interesting part:

``` c
case GST_MESSAGE_DURATION:
  /* The duration has changed, mark the current one as invalid */
  data->duration = GST_CLOCK_TIME_NONE;
  break;
```

This message is posted on the bus whenever the duration of the stream
changes. Here we simply mark the current duration as invalid, so it gets
re-queried later.

``` c
case GST_MESSAGE_STATE_CHANGED: {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    g_print ("Pipeline state changed from %s to %s:\n",
        gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

    /* Remember whether we are in the PLAYING state or not */
    data->playing = (new_state == GST_STATE_PLAYING);
```

Seeks and time queries generally only get a valid reply when in the
`PAUSED` or `PLAYING` state, since all elements have had a chance to
receive information and configure themselves. Here, we use the `playing`
variable to keep track of whether the pipeline is in `PLAYING` state.
Also, if we have just entered the `PLAYING` state, we do our first query.
We ask the pipeline if seeking is allowed on this stream:

``` c
if (data->playing) {
  /* We just moved to PLAYING. Check if seeking is possible */
  GstQuery *query;
  gint64 start, end;
  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (gst_element_query (data->pipeline, query)) {
    gst_query_parse_seeking (query, NULL, &data->seek_enabled, &start, &end);
    if (data->seek_enabled) {
      g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (start), GST_TIME_ARGS (end));
    } else {
      g_print ("Seeking is DISABLED for this stream.\n");
    }
  }
  else {
    g_printerr ("Seeking query failed.");
  }
  gst_query_unref (query);
}
```

`gst_query_new_seeking()` creates a new query object of the "seeking"
type, with `GST_FORMAT_TIME` format. This indicates that we are
interested in seeking by specifying the new time to which we want to
move. We could also ask for `GST_FORMAT_BYTES`, and then seek to a
particular byte position inside the source file, but this is normally
less useful.

This query object is then passed to the pipeline with
`gst_element_query()`. The result is stored in the same query, and can
be easily retrieved with `gst_query_parse_seeking()`. It extracts a
boolean indicating if seeking is allowed, and the range in which seeking
is possible.

Don't forget to unref the query object when you are done with it.

And that's it! With this knowledge a media player can be built which
periodically updates a slider based on the current stream position and
allows seeking by moving the slider!

## Conclusion

This tutorial has shown:

  - How to query the pipeline for information using `GstQuery`

  - How to obtain common information like position and duration
    using `gst_element_query_position()` and `gst_element_query_duration()`

  - How to seek to an arbitrary position in the stream
    using `gst_element_seek_simple()`

  - In which states all these operations can be performed.

The next tutorial shows how to integrate GStreamer with a Graphical User
Interface toolkit.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.

It has been a pleasure having you here, and see you soon!
