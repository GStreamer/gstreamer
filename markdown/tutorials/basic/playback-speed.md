# Basic tutorial 13: Playback speed


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}


## Goal

Fast-forward, reverse-playback and slow-motion are all techniques
collectively known as *trick modes* and they all have in common that
modify the normal playback rate. This tutorial shows how to achieve
these effects and adds frame-stepping into the deal. In particular, it
shows:

  - How to change the playback rate, faster and slower than normal,
    forward and backwards.
  - How to advance a video frame-by-frame

## Introduction

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

In [](tutorials/basic/time-management.md) seek
events have already been shown, using a helper function to hide their
complexity. This tutorial explains a bit more how to use these events.

Step Events are a more convenient way of changing the playback rate,
due to the reduced number of parameters needed to create them;
however, they have some downsides, so Seek Events are used in this
tutorial instead. Step events only affect the sink (at the end of the
pipeline), so they will only work if the rest of the pipeline can
support going at a different speed, Seek events go all the way through
the pipeline so every element can react to them. The upside of Step
events is that they are much faster to act. Step events are also
unable to change the playback direction.

To use these events, they are created and then passed onto the pipeline,
where they propagate upstream until they reach an element that can
handle them. If an event is passed onto a bin element like `playbin`,
it will simply feed the event to all its sinks, which will result in
multiple seeks being performed. The common approach is to retrieve one
of `playbin`’s sinks through the `video-sink` or
`audio-sink` properties and feed the event directly into the sink.

Frame stepping is a technique that allows playing a video frame by
frame. It is implemented by pausing the pipeline, and then sending Step
Events to skip one frame each time.

## A trick mode player

Copy this code into a text file named `basic-tutorial-13.c`.

**basic-tutorial-13.c**

{{ tutorials/basic-tutorial-13.c }}

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> `` gcc basic-tutorial-13.c -o basic-tutorial-13 `pkg-config --cflags --libs gstreamer-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. The console shows the available commands, composed of a single upper-case or lower-case letter, which you should input followed by the Enter key.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

There is nothing new in the initialization code in the main function:  a
`playbin` pipeline is instantiated, an I/O watch is installed to track
keystrokes and a GLib main loop is executed.

Then, in the keyboard handler function:

``` c
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

Pause / Playing toggle is handled with `gst_element_set_state()` as in
previous tutorials.

``` c
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
`rate` variable is updated and `send_seek_event` is called. Let’s
review this function.

``` c
/* Send seek event to change rate */
static void send_seek_event (CustomData *data) {
  gint64 position;
  GstEvent *seek_event;

  /* Obtain the current position, needed for the seek event */
  if (!gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position)) {
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

``` c
/* Create the seek event */
if (data->rate > 0) {
  seek_event = gst_event_new_seek (data->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_END, 0);
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

``` c
if (data->video_sink == NULL) {
  /* If we have not done so, obtain the sink through which we will send the seek events */
  g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
}
```

As explained in the Introduction, to avoid performing multiple Seeks,
the Event is sent to only one sink, in this case, the video sink. It is
obtained from `playbin` through the `video-sink` property. It is read
at this time instead at initialization time because the actual sink may
change depending on the media contents, and this won’t be known until
the pipeline is `PLAYING` and some media has been read.

``` c
/* Send the event */
gst_element_send_event (data->video_sink, seek_event);
```

The new Event is finally sent to the selected sink with
`gst_element_send_event()`.

Back to the keyboard handler, we still miss the frame stepping code,
which is really simple:

``` c
case 'n':
  if (data->video_sink == NULL) {
    /* If we have not done so, obtain the sink through which we will send the step events */
    g_object_get (data->pipeline, "video-sink", &data->video_sink, NULL);
  }

  gst_element_send_event (data->video_sink,
      gst_event_new_step (GST_FORMAT_BUFFERS, 1, ABS (data->rate), TRUE, FALSE));
  g_print ("Stepping one frame\n");
  break;
```

A new Step Event is created with `gst_event_new_step()`, whose
parameters basically specify the amount to skip (1 frame in the example)
and the new rate (which we do not change).

The video sink is grabbed from `playbin` in case we didn’t have it yet,
just like before.

And with this we are done. When testing this tutorial, keep in mind that
backward playback is not optimal in many elements.

> ![Warning](images/icons/emoticons/warning.svg)
>
>Changing the playback rate might only work with local files. If you cannot modify it, try changing the URI passed to `playbin` in line 114 to a local URI, starting with `file:///`
</table>

## Conclusion

This tutorial has shown:

  - How to change the playback rate using a Seek Event, created with
    `gst_event_new_seek()` and fed to the pipeline
    with `gst_element_send_event()`.
  - How to advance a video frame-by-frame by using Step Events, created
    with `gst_event_new_step()`.

It has been a pleasure having you here, and see you soon!
