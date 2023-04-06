# Playback tutorial 5: Color Balance


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

Brightness, Contrast, Hue and Saturation are common video adjustments,
which are collectively known as Color Balance settings in GStreamer.
This tutorial shows:

  - How to find out the available color balance channels
  - How to change them

## Introduction
[](tutorials/basic/toolkit-integration.md) has
already explained the concept of GObject interfaces: applications use
them to find out if certain functionality is available, regardless of
the actual element which implements it.

`playbin` implements the Color Balance interface (`GstColorBalance`),
which allows access to the color balance settings. If any of the
elements in the `playbin` pipeline support this interface,
`playbin` simply forwards it to the application, otherwise, a
colorbalance element is inserted in the pipeline.

This interface allows querying for the available color balance channels
(`GstColorBalanceChannel`), along with their name and valid range of
values, and then modify the current value of any of them.

## Color balance example

Copy this code into a text file named `playback-tutorial-5.c`.

**playback-tutorial-5.c**

``` c
#include <string.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/colorbalance.h>

typedef struct _CustomData {
  GstElement *pipeline;
  GMainLoop *loop;
} CustomData;

/* Process a color balance command */
static void update_color_channel (const gchar *channel_name, gboolean increase, GstColorBalance *cb) {
  gdouble step;
  gint value;
  GstColorBalanceChannel *channel = NULL;
  const GList *channels, *l;

  /* Retrieve the list of channels and locate the requested one */
  channels = gst_color_balance_list_channels (cb);
  for (l = channels; l != NULL; l = l->next) {
    GstColorBalanceChannel *tmp = (GstColorBalanceChannel *)l->data;

    if (g_strrstr (tmp->label, channel_name)) {
      channel = tmp;
      break;
    }
  }
  if (!channel)
    return;

  /* Change the channel's value */
  step = 0.1 * (channel->max_value - channel->min_value);
  value = gst_color_balance_get_value (cb, channel);
  if (increase) {
    value = (gint)(value + step);
    if (value > channel->max_value)
      value = channel->max_value;
  } else {
    value = (gint)(value - step);
    if (value < channel->min_value)
      value = channel->min_value;
  }
  gst_color_balance_set_value (cb, channel, value);
}

/* Output the current values of all Color Balance channels */
static void print_current_values (GstElement *pipeline) {
  const GList *channels, *l;

  /* Output Color Balance values */
  channels = gst_color_balance_list_channels (GST_COLOR_BALANCE (pipeline));
  for (l = channels; l != NULL; l = l->next) {
    GstColorBalanceChannel *channel = (GstColorBalanceChannel *)l->data;
    gint value = gst_color_balance_get_value (GST_COLOR_BALANCE (pipeline), channel);
    g_print ("%s: %3d%% ", channel->label,
        100 * (value - channel->min_value) / (channel->max_value - channel->min_value));
  }
  g_print ("\n");
}

/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;
  }

  switch (g_ascii_tolower (str[0])) {
  case 'c':
    update_color_channel ("CONTRAST", g_ascii_isupper (str[0]), GST_COLOR_BALANCE (data->pipeline));
    break;
  case 'b':
    update_color_channel ("BRIGHTNESS", g_ascii_isupper (str[0]), GST_COLOR_BALANCE (data->pipeline));
    break;
  case 'h':
    update_color_channel ("HUE", g_ascii_isupper (str[0]), GST_COLOR_BALANCE (data->pipeline));
    break;
  case 's':
    update_color_channel ("SATURATION", g_ascii_isupper (str[0]), GST_COLOR_BALANCE (data->pipeline));
    break;
  case 'q':
    g_main_loop_quit (data->loop);
    break;
  default:
    break;
  }

  g_free (str);

  print_current_values (data->pipeline);

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
    " 'C' to increase contrast, 'c' to decrease contrast\n"
    " 'B' to increase brightness, 'b' to decrease brightness\n"
    " 'H' to increase hue, 'h' to decrease hue\n"
    " 'S' to increase saturation, 's' to decrease saturation\n"
    " 'Q' to quit\n");

  /* Build the pipeline */
  data.pipeline = gst_parse_launch ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef G_OS_WIN32
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
  print_current_values (data.pipeline);

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}
```

> ![information] If you need help to compile this code, refer to the
> **Building the tutorials** section for your platform: [Mac] or
> [Windows] or use this specific command on Linux:
>
> `` gcc playback-tutorial-5.c -o playback-tutorial-5 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Mac OS X], [Windows][1], for
> [iOS] or for [android].
>
> This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed.
>
>The console should print all commands (Each command is a single upper-case or lower-case letter) and list all available Color Balance channels, typically, CONTRAST, BRIGHTNESS, HUE and SATURATION. Type each command (letter) followed by the Enter key.
>
> Required libraries: `gstreamer-1.0 gstreamer-video-1.0`

## Walkthrough

The `main()` function is fairly simple. A `playbin` pipeline is
instantiated and set to run, and a keyboard watch is installed so
keystrokes can be monitored.

``` c
/* Output the current values of all Color Balance channels */
static void print_current_values (GstElement *pipeline) {
  const GList *channels, *l;

  /* Output Color Balance values */
  channels = gst_color_balance_list_channels (GST_COLOR_BALANCE (pipeline));
  for (l = channels; l != NULL; l = l->next) {
    GstColorBalanceChannel *channel = (GstColorBalanceChannel *)l->data;
    gint value = gst_color_balance_get_value (GST_COLOR_BALANCE (pipeline), channel);
    g_print ("%s: %3d%% ", channel->label,
        100 * (value - channel->min_value) / (channel->max_value - channel->min_value));
  }
  g_print ("\n");
}
```

This method prints the current value for all channels, and exemplifies
how to retrieve the list of channels. This is accomplished through the
`gst_color_balance_list_channels()` method. It returns a `GList` which
needs to be traversed.

Each element in the list is a `GstColorBalanceChannel` structure,
informing of the channel’s name, minimum value and maximum value.
`gst_color_balance_get_value()` can then be called on each channel to
retrieve the current value.

In this example, the minimum and maximum values are used to output the
current value as a percentage.

``` c
/* Process a color balance command */
static void update_color_channel (const gchar *channel_name, gboolean increase, GstColorBalance *cb) {
  gdouble step;
  gint value;
  GstColorBalanceChannel *channel = NULL;
  const GList *channels, *l;

  /* Retrieve the list of channels and locate the requested one */
  channels = gst_color_balance_list_channels (cb);
  for (l = channels; l != NULL; l = l->next) {
    GstColorBalanceChannel *tmp = (GstColorBalanceChannel *)l->data;

    if (g_strrstr (tmp->label, channel_name)) {
      channel = tmp;
      break;
    }
  }
  if (!channel)
    return;
```

This method locates the specified channel by name and increases or
decreases it as requested. Again, the list of channels is retrieved and
parsed looking for the channel with the specified name. Obviously, this
list could be parsed only once and the pointers to the channels be
stored and indexed by something more efficient than a string.

``` c
  /* Change the channel's value */
  step = 0.1 * (channel->max_value - channel->min_value);
  value = gst_color_balance_get_value (cb, channel);
  if (increase) {
    value = (gint)(value + step);
    if (value > channel->max_value)
      value = channel->max_value;
  } else {
    value = (gint)(value - step);
    if (value < channel->min_value)
      value = channel->min_value;
  }
  gst_color_balance_set_value (cb, channel, value);
}
```

The current value for the channel is then retrieved, changed (the
increment is proportional to its dynamic range), clamped (to avoid
out-of-range values) and set using `gst_color_balance_set_value()`.

And there is not much more to it. Run the program and observe the effect
of changing each of the channels in real time.

## Conclusion

This tutorial has shown how to use the color balance interface.
Particularly, it has shown:

  - How to retrieve the list of color available balance channels
    with `gst_color_balance_list_channels()`
  - How to manipulate the current value of each channel using
    `gst_color_balance_get_value()` and `gst_color_balance_set_value()`

It has been a pleasure having you here, and see you soon\!


  [information]: images/icons/emoticons/information.svg
  [Mac]: installing/on-mac-osx.md
  [Windows]: installing/on-windows.md
  [Mac OS X]: installing/on-mac-osx.md#building-the-tutorials
  [1]: installing/on-windows.md#running-the-tutorials
  [iOS]: installing/for-ios-development.md#building-the-tutorials
  [android]: installing/for-android-development.md#building-the-tutorials
  [warning]: images/icons/emoticons/warning.svg
