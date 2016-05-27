# Basic tutorial 13: Playback speed

This page last changed on Jul 06, 2012 by xartigas.

# Goal

Fast-forward, reverse-playback and slow-motion are all techniques
collectively known as *trick modes* and they all have in common that
modify the normal playback rate. This tutorial shows how to achieve
these effects and adds frame-stepping into the deal. In particular, it
shows:

  - How to change the playback rate, faster and slower than normal,
    forward and backwards.
  - How to advance a video frame-by-frame

# Introduction

Fast-forward is the technique that plays a media at a speed higher than
its normal (intended) speed; whereas slow-motion uses a speed lower than
the intended one. Reverse playback does the same thing but backwards,
from the end of the stream to the beginning.

All these techniques do is change the playback rate, which is a variable
equal to 1.0 for normal playback, greater than 1.0 (in absolute value)
for fast modes, lower than 1.0 (in absolute value) for slow modes,
positive for forward playback and negative for reverse playback.

GStreamer provides two mechanisms to change the playback rate: Step
Events and Seek Events. Step Events allow skipping a given amount of
media besides changing the subsequent playback rate (only to positive
values). Seek Events, additionally, allow jumping to any position in the
stream and set positive and negative playback rates.

In [Basic tutorial 4: Time
management](Basic%2Btutorial%2B4%253A%2BTime%2Bmanagement.html) seek
events have already been shown, using a helper function to hide their
complexity. This tutorial explains a bit more how to use these events.

Step Events are a more convenient way of changing the playback rate, due
to the reduced number of parameters needed to create them; however,
their implementation in GStreamer still needs a bit more polishing
so Seek Events are used in this tutorial instead.

To use these events, they are created and then passed onto the pipeline,
where they propagate upstream until they reach an element that can
handle them. If an event is passed onto a bin element like `playbin2`,
it will simply feed the event to all its sinks, which will result in
multiple seeks being performed. The common approach is to retrieve one
of `playbin2`’s sinks through the `video-sink` or
`audio-sink` properties and feed the event directly into the sink.

Frame stepping is a technique that allows playing a video frame by
frame. It is implemented by pausing the pipeline, and then sending Step
Events to skip one frame each time.

# A trick mode player

Copy this code into a text file named `basic-tutorial-13.c`.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>This tutorial is included in the SDK since release 2012.7. If you cannot find it in the downloaded code, please install the latest release of the GStreamer SDK.</p></td>
</tr>
</tbody>
</table>

**basic-tutorial-13.c**

``` lang=c
#include <string.h>
#include <gst/gst.h>

typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *video_sink;
  GMainLoop *loop;

  gboolean playing;  /* Playing or Paused */
  gdouble rate;      /* Current playback rate (can be negative) */
} CustomData;

/* Send seek event to change rate */
static void send_seek_event (CustomData *data) {
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;
  GstEvent *seek_event;

  /* Obtain the current position, needed for the seek event */
  if (!gst_element_query_position (data->pipeline, &format, &position)) {
    g_printerr ("Unable to retrieve current position.\n");
    return;
  }

  /* Create the seek event */
  if (data->rate > 0) {
    seek_event = gst_event_new_seek (data->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
        GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, 0);
  } else {
    seek_event = gst_event_new_seek (data->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, position);
  }

  if (data->video_sink == NULL) {
    /* If we have not done so, obtain the sink through which we will send the seek events */
    g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
  }

  /* Send the event */
  gst_element_send_event (data->video_sink, seek_event);

  g_print ("Current rate: %g\n", data->rate);
}

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
    send_seek_event (data);
    break;
  case 'd':
    data->rate *= -1.0;
    send_seek_event (data);
    break;
  case 'n':
    if (data->video_sink == NULL) {
      /* If we have not done so, obtain the sink through which we will send the step events */
      g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
    }

    gst_element_send_event (data->video_sink,
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
  if (data.video_sink != NULL)
    gst_object_unref (data.video_sink);
  gst_object_unref (data.pipeline);
  return 0;
}
```

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><div id="expander-1662010270" class="expand-container">
<div id="expander-control-1662010270" class="expand-control">
<span class="expand-control-icon"><img src="images/icons/grey_arrow_down.gif" class="expand-control-image" /></span><span class="expand-control-text">Need help? (Click to expand)</span>
</div>
<div id="expander-content-1662010270" class="expand-content">
<p>If you need help to compile this code, refer to the <strong>Building the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Build">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Build">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Build">Windows</a>, or use this specific command on Linux:</p>
<div class="panel" style="border-width: 1px;">
<div class="panelContent">
<p><code>gcc basic-tutorial-13.c -o basic-tutorial-13 `pkg-config --cflags --libs gstreamer-0.10`</code></p>
</div>
</div>
<p>If you need help to run this code, refer to the <strong>Running the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Run">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Run">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Run">Windows</a></p>
<p><span>This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. The console shows the available commands, composed of a single upper-case or lower-case letter, which you should input followed by the Enter key.</span></p>
<p>Required libraries: <code>gstreamer-0.10</code></p>
</div>
</div></td>
</tr>
</tbody>
</table>

# Walkthrough

There is nothing new in the initialization code in the main function:  a
`playbin2` pipeline is instantiated, an I/O watch is installed to track
keystrokes and a GLib main loop is executed.

Then, in the keyboard handler function:

``` lang=c
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
```

Pause / Playing toggle is handled with `gst_element_set_state()` as in
previous tutorials.

``` lang=c
case 's':
  if (g_ascii_isupper (str[0])) {
    data->rate *= 2.0;
  } else {
    data->rate /= 2.0;
  }
  send_seek_event (data);
  break;
case 'd':
  data->rate *= -1.0;
  send_seek_event (data);
  break;
```

Use ‘S’ and ‘s’ to double or halve the current playback rate, and ‘d’ to
reverse the current playback direction. In both cases, the
`rate` variable is updated and `send_seek_event` is called. Let’s
review this function.

``` lang=c
/* Send seek event to change rate */
static void send_seek_event (CustomData *data) {
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;
  GstEvent *seek_event;

  /* Obtain the current position, needed for the seek event */
  if (!gst_element_query_position (data->pipeline, &format, &position)) {
    g_printerr ("Unable to retrieve current position.\n");
    return;
  }
```

This function creates a new Seek Event and sends it to the pipeline to
update the rate. First, the current position is recovered with
`gst_element_query_position()`. This is needed because the Seek Event
jumps to another position in the stream, and, since we do not actually
want to move, we jump to the current position. Using a Step Event would
be simpler, but this event is not currently fully functional, as
explained in the Introduction.

``` lang=c
/* Create the seek event */
if (data->rate > 0) {
  seek_event = gst_event_new_seek (data->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, 0);
} else {
  seek_event = gst_event_new_seek (data->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, position);
}
```

The Seek Event is created with `gst_event_new_seek()`. Its parameters
are, basically, the new rate, the new start position and the new stop
position. Regardless of the playback direction, the start position must
be smaller than the stop position, so the two playback directions are
treated differently.

``` lang=c
if (data->video_sink == NULL) {
  /* If we have not done so, obtain the sink through which we will send the seek events */
  g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
}
```

As explained in the Introduction, to avoid performing multiple Seeks,
the Event is sent to only one sink, in this case, the video sink. It is
obtained from `playbin2` through the `video-sink` property. It is read
at this time instead at initialization time because the actual sink may
change depending on the media contents, and this won’t be known until
the pipeline is PLAYING and some media has been read.

``` lang=c
/* Send the event */
gst_element_send_event (data->video_sink, seek_event);
```

The new Event is finally sent to the selected sink with
`gst_element_send_event()`.

Back to the keyboard handler, we still miss the frame stepping code,
which is really simple:

``` lang=c
case 'n':
  if (data->video_sink == NULL) {
    /* If we have not done so, obtain the sink through which we will send the step events */
    g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
  }

  gst_element_send_event (data->video_sink,
      gst_event_new_step (GST_FORMAT_BUFFERS, 1, data->rate, TRUE, FALSE));
  g_print ("Stepping one frame\n");
  break;
```

A new Step Event is created with `gst_event_new_step()`, whose
parameters basically specify the amount to skip (1 frame in the example)
and the new rate (which we do not change).

The video sink is grabbed from `playbin2` in case we didn’t have it yet,
just like before.

And with this we are done. When testing this tutorial, keep in mind that
backward playback is not optimal in many elements.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>Changing the playback rate might only work with local files. If you cannot modify it, try changing the URI passed to <code>playbin2</code> in line 114 to a local URI, starting with <code>file:///</code></p></td>
</tr>
</tbody>
</table>

# Conclusion

This tutorial has shown:

  - How to change the playback rate using a Seek Event, created with
    `gst_event_new_seek()` and fed to the pipeline
    with `gst_element_send_event()`.
  - How to advance a video frame-by-frame by using Step Events, created
    with `gst_event_new_step()`.

It has been a pleasure having you here, and see you soon\!

## Attachments:

![](images/icons/bullet_blue.gif)
[basic-tutorial-13.c](attachments/327800/2424883.c) (text/plain)
![](images/icons/bullet_blue.gif)
[vs2010.zip](attachments/327800/2424884.zip) (application/zip)

Document generated by Confluence on Oct 08, 2015 10:27
