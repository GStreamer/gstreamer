# Playback tutorial 2: Subtitle management


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

This tutorial is very similar to the previous one, but instead of
switching among different audio streams, we will use subtitle streams.
This will allow us to learn:

  - How to choose the subtitle stream

  - How to add external subtitles

  - How to customize the font used for the subtitles

## Introduction

We already know (from the previous tutorial) that container files can
hold multiple audio and video streams, and that we can very easily
choose among them by changing the `current-audio` or
`current-video` `playbin` property. Switching subtitles is just as
easy.

It is worth noting that, just like it happens with audio and video,
`playbin` takes care of choosing the right decoder for the subtitles,
and that the plugin structure of GStreamer allows adding support for new
formats as easily as copying a file. Everything is invisible to the
application developer.

Besides subtitles embedded in the container, `playbin` offers the
possibility to add an extra subtitle stream from an external URI.

This tutorial opens a file which already contains 5 subtitle streams,
and adds another one from another file (for the Greek language).

## The multilingual player with subtitles

Copy this code into a text file named `playback-tutorial-2.c` (or find
it in the GStreamer installation).

**playback-tutorial-2.c**

``` c
#include <stdio.h>
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
  g_object_set (data.playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.ogv", NULL);

  /* Set the subtitle URI to play and some font description */
  g_object_set (data.playbin, "suburi", "https://gstreamer.freedesktop.org/data/media/sintel_trailer_gr.srt", NULL);
  g_object_set (data.playbin, "subtitle-font-desc", "Sans, 18", NULL);

  /* Set flags to show Audio, Video and Subtitles */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
  g_object_set (data.playbin, "flags", flags, NULL);

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
  g_print ("Currently playing video stream %d, audio stream %d and subtitle stream %d\n",
      data->current_video, data->current_audio, data->current_text);
  g_print ("Type any number and hit ENTER to select a different subtitle stream\n");
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
    int index = atoi (str);
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
```


> ![information] Need help?
>
> If you need help to compile this code, refer to the **Building the
> tutorials** section for your platform: [Linux], [Mac OS X] or
> [Windows], or use this specific command on Linux:
>
> `` gcc playback-tutorial-2.c -o playback-tutorial-2 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Linux][1], [Mac OS X][2] or
> [Windows][3].
>
> This tutorial opens a window and displays a movie, with accompanying
> audio. The media is fetched from the Internet, so the window might
> take a few seconds to appear, depending on your connection
> speed. The number of subtitle streams is shown in the terminal, and
> the user can switch from one to another by entering a number and
> pressing enter. A small delay is to be
> expected. _Please read the note at the bottom of this
> page._ Bear in mind that
> there is no latency management (buffering), so on slow connections,
> the movie might stop after a few seconds. See how
> [](tutorials/basic/streaming.md) solves this issue.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

This tutorial is copied from
[](tutorials/playback/playbin-usage.md) with some changes, so let's
review only the changes.

``` c
/* Set the subtitle URI to play and some font description */
g_object_set (data.playbin, "suburi", "https://gstreamer.freedesktop.org/data/media/sintel_trailer_gr.srt", NULL);
g_object_set (data.playbin, "subtitle-font-desc", "Sans, 18", NULL);
```

After setting the media URI, we set the `suburi` property, which points
`playbin` to a file containing a subtitle stream. In this case, the
media file already contains multiple subtitle streams, so the one
provided in the `suburi` is added to the list, and will be the currently
selected one.

Note that metadata concerning a subtitle stream (like its language)
resides in the container file, therefore, subtitles not embedded in a
container will not have metadata. When running this tutorial you will
find that the first subtitle stream does not have a language tag.

The `subtitle-font-desc` property allows specifying the font to render
the subtitles. Since [Pango](http://www.pango.org/) is the library used
to render fonts, you can check its documentation to see how this font
should be specified, in particular, the
[pango-font-description-from-string](https://docs.gtk.org/Pango/type_func.FontDescription.from_string.html) function.

In a nutshell, the format of the string representation is `[FAMILY-LIST]
[STYLE-OPTIONS] [SIZE]` where `FAMILY-LIST` is a comma separated list of
families optionally terminated by a comma, `STYLE_OPTIONS` is a
whitespace separated list of words where each word describes one of
style, variant, weight, or stretch, and `SIZE` is an decimal number
(size in points). For example the following are all valid string
representations:

  - sans bold 12
  - serif, monospace bold italic condensed 16
  - normal 10

The commonly available font families are: Normal, Sans, Serif and
Monospace.

The available styles are: Normal (the font is upright), Oblique (the
font is slanted, but in a roman style), Italic (the font is slanted in
an italic style).

The available weights are: Ultra-Light, Light, Normal, Bold, Ultra-Bold,
Heavy.

The available variants are: Normal, Small\_Caps (A font with the lower
case characters replaced by smaller variants of the capital characters)

The available stretch styles
are: Ultra-Condensed, Extra-Condensed, Condensed, Semi-Condensed, Normal, Semi-Expanded, Expanded,
Extra-Expanded, Ultra-Expanded



``` c
/* Set flags to show Audio, Video and Subtitles */
g_object_get (data.playbin, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
g_object_set (data.playbin, "flags", flags, NULL);
```

We set the `flags` property to allow Audio, Video and Text (Subtitles).

The rest of the tutorial is the same as [](tutorials/playback/playbin-usage.md), except
that the keyboard input changes the `current-text` property instead of
the `current-audio`. As before, keep in mind that stream changes are not
immediate, since there is a lot of information flowing through the
pipeline that needs to reach the end of it before the new stream shows
up.

## Conclusion

This tutorial showed how to handle subtitles from `playbin`, whether
they are embedded in the container or in a different file:

  - Subtitles are chosen using the `current-tex`t and `n-tex`t
    properties of `playbin`.

  - External subtitle files can be selected using the `suburi` property.

  - Subtitle appearance can be customized with the
    `subtitle-font-desc` property.

The next playback tutorial shows how to change the playback speed.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.
It has been a pleasure having you here, and see you soon\!

  [information]: images/icons/emoticons/information.svg
