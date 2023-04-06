# Basic tutorial 12: Streaming


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

Playing media straight from the Internet without storing it locally is
known as Streaming. We have been doing it throughout the tutorials
whenever we used a URI starting with `http://`. This tutorial shows a
couple of additional points to keep in mind when streaming. In
particular:

  - How to enable buffering (to alleviate network problems)
  - How to recover from interruptions (lost clock)

## Introduction

When streaming, media chunks are decoded and queued for presentation as
soon as they arrive from the network. This means that if a chunk is
delayed (which is not an uncommon situation at all on the Internet) the
presentation queue might run dry and media playback could stall.

The universal solution is to build a “buffer”, this is, allow a certain
number of media chunks to be queued before starting playback. In this
way, playback start is delayed a bit, but, if some chunks are late,
reproduction is not impacted as there are more chunks in the queue,
waiting.

As it turns out, this solution is already implemented in GStreamer, but
the previous tutorials have not been benefiting from it. Some elements,
like the `queue2` and `multiqueue` found inside `playbin`, are capable
of building this buffer and post bus messages regarding the buffer level
(the state of the queue). An application wanting to have more network
resilience, then, should listen to these messages and pause playback if
the buffer level is not high enough (usually, whenever it is below
100%).

To achieve synchronization among multiple sinks (for example an audio
and a video sink) a global clock is used. This clock is selected by
GStreamer among all elements which can provide one. Under some
circumstances, for example, an RTP source switching streams or changing
the output device, this clock can be lost and a new one needs to be
selected. This happens mostly when dealing with streaming, so the
process is explained in this tutorial.

When the clock is lost, the application receives a message on the bus;
to select a new one, the application just needs to set the pipeline to
`PAUSED` and then to `PLAYING` again.

## A network-resilient example

Copy this code into a text file named `basic-tutorial-12.c`.

**basic-tutorial-12.c**

``` c
#include <gst/gst.h>
#include <string.h>

typedef struct _CustomData {
  gboolean is_live;
  GstElement *pipeline;
  GMainLoop *loop;
} CustomData;

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
    case GST_MESSAGE_BUFFERING: {
      gint percent = 0;

      /* If the stream is live, we do not care about buffering. */
      if (data->is_live) break;

      gst_message_parse_buffering (msg, &percent);
      g_print ("Buffering (%3d%%)\r", percent);
      /* Wait until buffering is complete before start/resume playing */
      if (percent < 100)
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    }
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

int main(int argc, char *argv[]) {
  GstElement *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;
  GMainLoop *main_loop;
  CustomData data;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
  bus = gst_element_get_bus (pipeline);

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

  g_main_loop_run (main_loop);

  /* Free resources */
  g_main_loop_unref (main_loop);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> `` gcc basic-tutorial-12.c -o basic-tutorial-12 `pkg-config --cflags --libs gstreamer-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. In the console window, you should see a buffering message, and playback should only start when the buffering reaches 100%. This percentage might not change at all if your connection is fast enough and buffering is not required.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

The only special thing this tutorial does is react to certain messages;
therefore, the initialization code is very simple and should be
self-explanatory by now. The only new bit is the detection of live
streams:

``` c
/* Start playing */
ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
if (ret == GST_STATE_CHANGE_FAILURE) {
  g_printerr ("Unable to set the pipeline to the playing state.\n");
  gst_object_unref (pipeline);
  return -1;
} else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
  data.is_live = TRUE;
}
```

Live streams cannot be paused, so they behave in `PAUSED` state as if they
were in the `PLAYING` state. Setting live streams to `PAUSED` succeeds, but
returns `GST_STATE_CHANGE_NO_PREROLL`, instead of
`GST_STATE_CHANGE_SUCCESS` to indicate that this is a live stream. We
are receiving the `NO_PREROLL` return code even though we are trying to
set the pipeline to `PLAYING`, because state changes happen progressively
(from NULL to READY, to `PAUSED` and then to `PLAYING`).

We care about live streams because we want to disable buffering for
them, so we take note of the result of `gst_element_set_state()` in the
`is_live` variable.

Let’s now review the interesting parts of the message parsing callback:

``` c
case GST_MESSAGE_BUFFERING: {
  gint percent = 0;

  /* If the stream is live, we do not care about buffering. */
  if (data->is_live) break;

  gst_message_parse_buffering (msg, &percent);
  g_print ("Buffering (%3d%%)\r", percent);
  /* Wait until buffering is complete before start/resume playing */
  if (percent < 100)
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
  else
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  break;
}
```

First, if this is a live source, ignore buffering messages.

We parse the buffering message with `gst_message_parse_buffering()` to
retrieve the buffering level.

Then, we print the buffering level on the console and set the pipeline
to `PAUSED` if it is below 100%. Otherwise, we set the pipeline to
`PLAYING`.

At startup, we will see the buffering level rise up to 100% before
playback starts, which is what we wanted to achieve. If, later on, the
network becomes slow or unresponsive and our buffer depletes, we will
receive new buffering messages with levels below 100% so we will pause
the pipeline again until enough buffer has been built up.

``` c
case GST_MESSAGE_CLOCK_LOST:
  /* Get a new clock */
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  break;
```

For the second network issue, the loss of clock, we simply set the
pipeline to `PAUSED` and back to `PLAYING`, so a new clock is selected,
waiting for new media chunks to be received if necessary.

## Conclusion

This tutorial has described how to add network resilience to your
application with two very simple precautions:

  - Taking care of buffering messages sent by the pipeline
  - Taking care of clock loss

Handling these messages improves the application’s response to network
problems, increasing the overall playback smoothness.

It has been a pleasure having you here, and see you soon!
