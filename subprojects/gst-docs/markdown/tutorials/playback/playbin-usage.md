# Playback tutorial 1: Playbin usage


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

We have already worked with the `playbin` element, which is capable of
building a complete playback pipeline without much work on our side.
This tutorial shows how to further customize `playbin` in case its
default values do not suit our particular needs.

We will learn:

-   How to find out how many streams a file contains, and how to switch
    among them.

-   How to gather information regarding each stream.

## Introduction

More often than not, multiple audio, video and subtitle streams can be
found embedded in a single file. The most common case are regular
movies, which contain one video and one audio stream (Stereo or 5.1
audio tracks are considered a single stream). It is also increasingly
common to find movies with one video and multiple audio streams, to
account for different languages. In this case, the user selects one
audio stream, and the application will only play that one, ignoring the
others.

To be able to select the appropriate stream, the user needs to know
certain information about them, for example, their language. This
information is embedded in the streams in the form of “metadata”
(annexed data), and this tutorial shows how to retrieve it.

Subtitles can also be embedded in a file, along with audio and video,
but they are dealt with in more detail in [Playback tutorial 2: Subtitle
management]. Finally, multiple video streams can also be found in a
single file, for example, in DVD with multiple angles of the same scene,
but they are somewhat rare.

> ![information] Embedding multiple streams inside a single file is
> called “multiplexing” or “muxing”, and such file is then known as a
> “container”. Common container formats are Matroska (.mkv), Quicktime
> (.qt, .mov, .mp4), Ogg (.ogg) or Webm (.webm).
>
> Retrieving the individual streams from within the container is called
> “demultiplexing” or “demuxing”.

The following code recovers the amount of streams in the file, their
associated metadata, and allows switching the audio stream while the
media is playing.

## The multilingual player

Copy this code into a text file named `playback-tutorial-1.c` (or find
it in the GStreamer installation).

**playback-tutorial-1.c**

``` c
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */

  gint n_video;          /* Number of embedded video streams */
  gint n_audio;          /* Number of embedded audio streams */
  gint n_text;           /* Number of embedded subtitle streams */

  gint current_video;    /* Currently playing video stream */
  gint current_audio;    /* Currently playing audio stream */
  gint current_text;     /* Currently playing subtitle stream */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT          = (1 << 2)  /* We want subtitle output */
} GstPlayFlags;

/* Forward definition for the message and keyboard processing functions */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data);

int main(int argc, char *argv[]) {
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
  g_object_set (data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_cropped_multilingual.webm", NULL);

  /* Set flags to show Audio and Video but ignore Subtitles */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set (data.playbin, "flags", flags, NULL);

  /* Set connection speed. This will affect some internal decisions of playbin */
  g_object_set (data.playbin, "connection-speed", 56, NULL);

  /* Add a bus watch, so we get notified when a message arrives */
  bus = gst_element_get_bus (data.playbin);
  gst_bus_add_watch (bus, (GstBusFunc)handle_message, &data);

  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef G_OS_WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);

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
static void analyze_streams (CustomData *data) {
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
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      g_print ("subtitle stream %d:\n", i);
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    }
  }

  g_object_get (data->playbin, "current-video", &data->current_video, NULL);
  g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
  g_object_get (data->playbin, "current-text", &data->current_text, NULL);

  g_print ("\n");
  g_print ("Currently playing video stream %d, audio stream %d and text stream %d\n",
    data->current_video, data->current_audio, data->current_text);
  g_print ("Type any number and hit ENTER to select a different audio stream\n");
}

/* Process messages from GStreamer */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("End-Of-Stream reached.\n");
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        if (new_state == GST_STATE_PLAYING) {
          /* Once we are in the playing state, analyze the streams */
          analyze_streams (data);
        }
      }
    } break;
  }

  /* We want to keep receiving messages */
  return TRUE;
}

/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    int index = g_ascii_strtoull (str, NULL, 0);
    if (index < 0 || index >= data->n_audio) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid audio stream index, set the current audio stream */
      g_print ("Setting current audio stream to %d\n", index);
      g_object_set (data->playbin, "current-audio", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}
```

> ![information] If you need help to compile this code, refer to the
> **Building the tutorials** section for your platform: [Mac] or
> [Windows] or use this specific command on Linux:
>
> `` gcc playback-tutorial-1.c -o playback-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Mac OS X], [Windows][1], for
> [iOS] or for [android].
>
> This tutorial opens a window and displays a movie, with accompanying
> audio. The media is fetched from the Internet, so the window might take
> a few seconds to appear, depending on your connection speed. The number
> of audio streams is shown in the terminal, and the user can switch from
> one to another by entering a number and pressing enter. A small delay is
> to be expected.
>
> Bear in mind that there is no latency management (buffering), so on slow
> connections, the movie might stop after a few seconds. See how [Tutorial
> 12: Live streaming] solves this issue.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

``` c
/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */

  gint n_video;          /* Number of embedded video streams */
  gint n_audio;          /* Number of embedded audio streams */
  gint n_text;           /* Number of embedded subtitle streams */

  gint current_video;    /* Currently playing video stream */
  gint current_audio;    /* Currently playing audio stream */
  gint current_text;     /* Currently playing subtitle stream */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;
```

We start, as usual, putting all our variables in a structure, so we can
pass it around to functions. For this tutorial, we need the amount of
streams of each type, and the currently playing one. Also, we are going
to use a different mechanism to wait for messages that allows
interactivity, so we need a GLib's main loop object.

``` c
/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT          = (1 << 2)  /* We want subtitle output */
} GstPlayFlags;
```

Later we are going to set some of `playbin`'s flags. We would like to
have a handy enum that allows manipulating these flags easily, but since
`playbin` is a plug-in and not a part of the GStreamer core, this enum
is not available to us. The “trick” is simply to declare this enum in
our code, as it appears in the `playbin` documentation: `GstPlayFlags`.
GObject allows introspection, so the possible values for these flags can
be retrieved at runtime without using this trick, but in a far more
cumbersome way.

``` c
/* Forward definition for the message and keyboard processing functions */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data);
```

Forward declarations for the two callbacks we will be using.
`handle_message` for the GStreamer messages, as we have already seen,
and `handle_keyboard` for key strokes, since this tutorial is
introducing a limited amount of interactivity.

We skip over the creation of the pipeline, the instantiation of
`playbin` and pointing it to our test media through the `uri`
property. `playbin` is in itself a pipeline, and in this case it is the
only element in the pipeline, so we skip completely the creation of the
pipeline, and use directly the  `playbin` element.

We focus on some of the other properties of `playbin`, though:

``` c
/* Set flags to show Audio and Video but ignore Subtitles */
g_object_get (data.playbin, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
flags &= ~GST_PLAY_FLAG_TEXT;
g_object_set (data.playbin, "flags", flags, NULL);
```

`playbin`'s behavior can be changed through its `flags` property, which
can have any combination of `GstPlayFlags`. The most interesting values
are:

| Flag                      | Description                                                                                                                        |
|---------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| GST_PLAY_FLAG_VIDEO       | Enable video rendering. If this flag is not set, there will be no video output.                                                    |
| GST_PLAY_FLAG_AUDIO       | Enable audio rendering. If this flag is not set, there will be no audio output.                                                    |
| GST_PLAY_FLAG_TEXT        | Enable subtitle rendering. If this flag is not set, subtitles will not be shown in the video output.                               |
| GST_PLAY_FLAG_VIS         | Enable rendering of visualisations when there is no video stream. Playback tutorial 6: Audio visualization goes into more details. |
| GST_PLAY_FLAG_DOWNLOAD    | See Basic tutorial 12: Streaming  and Playback tutorial 4: Progressive streaming.                                                  |
| GST_PLAY_FLAG_BUFFERING   | See Basic tutorial 12: Streaming  and Playback tutorial 4: Progressive streaming.                                                  |
| GST_PLAY_FLAG_DEINTERLACE | If the video content was interlaced, this flag instructs playbin to deinterlace it before displaying it.                           |

In our case, for demonstration purposes, we are enabling audio and video
and disabling subtitles, leaving the rest of flags to their default
values (this is why we read the current value of the flags with
`g_object_get()` before overwriting it with `g_object_set()`).

``` c
/* Set connection speed. This will affect some internal decisions of playbin */
g_object_set (data.playbin, "connection-speed", 56, NULL);
```

This property is not really useful in this example.
`connection-speed` informs `playbin` of the maximum speed of our network
connection, so, in case multiple versions of the requested media are
available in the server, `playbin` chooses the most appropriate. This is
mostly used in combination with streaming protocols like `hls` or
`rtsp`.

We have set all these properties one by one, but we could have all of
them with a single call to `g_object_set()`:

``` c
g_object_set (data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_cropped_multilingual.webm", "flags", flags, "connection-speed", 56, NULL);
```

This is why `g_object_set()` requires a NULL as the last parameter.

``` c
  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef _WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);
```

These lines connect a callback function to the standard input (the
keyboard). The mechanism shown here is specific to GLib, and not really
related to GStreamer, so there is no point in going into much depth.
Applications normally have their own way of handling user input, and
GStreamer has little to do with it besides the Navigation interface
discussed briefly in [Tutorial 17: DVD playback].

``` c
/* Create a GLib Main Loop and set it to run */
data.main_loop = g_main_loop_new (NULL, FALSE);
g_main_loop_run (data.main_loop);
```

To allow interactivity, we will no longer poll the GStreamer bus
manually. Instead, we create a `GMainLoop`(GLib main loop) and set it
running with `g_main_loop_run()`. This function blocks and will not
return until `g_main_loop_quit()` is issued. In the meantime, it will
call the callbacks we have registered at the appropriate
times: `handle_message` when a message appears on the bus, and
`handle_keyboard` when the user presses any key.

There is nothing new in handle\_message, except that when the pipeline
moves to the PLAYING state, it will call the `analyze_streams` function:

``` c
/* Extract some metadata from the streams and print it on the screen */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str;
  guint rate;

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &data->n_video, NULL);
  g_object_get (data->playbin, "n-audio", &data->n_audio, NULL);
  g_object_get (data->playbin, "n-text", &data->n_text, NULL);
```

As the comment says, this function just gathers information from the
media and prints it on the screen. The number of video, audio and
subtitle streams is directly available through the `n-video`,
`n-audio` and `n-text` properties.

``` c
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
```

Now, for each stream, we want to retrieve its metadata. Metadata is
stored as tags in a `GstTagList` structure, which is a list of data
pieces identified by a name. The `GstTagList` associated with a stream
can be recovered with `g_signal_emit_by_name()`, and then individual
tags are extracted with the `gst_tag_list_get_*` functions
like `gst_tag_list_get_string()` for example.

> ![information]
> This rather unintuitive way of retrieving the tag list
> is called an Action Signal. Action signals are emitted by the
> application to a specific element, which then performs an action and
> returns a result. They behave like a dynamic function call, in which
> methods of a class are identified by their name (the signal's name)
> instead of their memory address. These signals are listed In the
> documentation along with the regular signals, and are tagged “Action”.
> See `playbin`, for example.

`playbin` defines 3 action signals to retrieve metadata:
`get-video-tags`, `get-audio-tags` and `get-text-tags`. The name if the
tags is standardized, and the list can be found in the `GstTagList`
documentation. In this example we are interested in the
`GST_TAG_LANGUAGE_CODE` of the streams and their `GST_TAG_*_CODEC`
(audio, video or text).

``` c
g_object_get (data->playbin, "current-video", &data->current_video, NULL);
g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
g_object_get (data->playbin, "current-text", &data->current_text, NULL);
```

Once we have extracted all the metadata we want, we get the streams that
are currently selected through 3 more properties of `playbin`:
`current-video`, `current-audio` and `current-text`.

It is interesting to always check the currently selected streams and
never make any assumption. Multiple internal conditions can make
`playbin` behave differently in different executions. Also, the order in
which the streams are listed can change from one run to another, so
checking the metadata to identify one particular stream becomes crucial.

``` c
/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    int index = g_ascii_strtoull (str, NULL, 0);
    if (index < 0 || index >= data->n_audio) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid audio stream index, set the current audio stream */
      g_print ("Setting current audio stream to %d\n", index);
      g_object_set (data->playbin, "current-audio", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}
```

Finally, we allow the user to switch the running audio stream. This very
basic function just reads a string from the standard input (the
keyboard), interprets it as a number, and tries to set the
`current-audio` property of `playbin` (which previously we have only
read).

Bear in mind that the switch is not immediate. Some of the previously
decoded audio will still be flowing through the pipeline, while the new
stream becomes active and is decoded. The delay depends on the
particular multiplexing of the streams in the container, and the length
`playbin` has selected for its internal queues (which depends on the
network conditions).

If you execute the tutorial, you will be able to switch from one
language to another while the movie is running by pressing 0, 1 or 2
(and ENTER). This concludes this tutorial.

## Conclusion

This tutorial has shown:

-   A few more of `playbin`'s properties: `flags`, `connection-speed`,
    `n-video`, `n-audio`, `n-text`, `current-video`, `current-audio` and
    `current-text`.

-   How to retrieve the list of tags associated with a stream
    with `g_signal_emit_by_name()`.

-   How to retrieve a particular tag from the list with
    `gst_tag_list_get_string()`or `gst_tag_list_get_uint()`

-   How to switch the current audio simply by writing to the
    `current-audio` property.

The next playback tutorial shows how to handle subtitles, either
embedded in the container or in an external file.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.

It has been a pleasure having you here, and see you soon!

  [Playback tutorial 2: Subtitle management]: tutorials/playback/subtitle-management.md
  [information]: images/icons/emoticons/information.svg
  [Mac]: installing/on-mac-osx.md
  [Windows]: installing/on-windows.md
  [Mac OS X]: installing/on-mac-osx.md#building-the-tutorials
  [1]: installing/on-windows.md#running-the-tutorials
  [iOS]: installing/for-ios-development.md#building-the-tutorials
  [android]: installing/for-android-development.md#building-the-tutorials
