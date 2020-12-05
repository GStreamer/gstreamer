# Playback tutorial 6: Audio visualization


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

GStreamer comes with a set of elements that turn audio into video. They
can be used for scientific visualization or to spice up your music
player, for example. This tutorial shows:

  - How to enable audio visualization
  - How to select the visualization element

## Introduction

Enabling audio visualization in `playbin` is actually very easy. Just
set the appropriate `playbin` flag and, when an audio-only stream is
found, it will instantiate the necessary elements to create and display
the visualization.

If you want to specify the actual element that you want to use to
generate the visualization, you instantiate it yourself and then tell
`playbin` about it through the `vis-plugin` property.

This tutorial searches the GStreamer registry for all the elements of
the Visualization class, tries to select `goom` (or another one if it is
not available) and passes it to `playbin`.

## A fancy music player

Copy this code into a text file named `playback-tutorial-6.c`.

**playback-tutorial-6.c**

``` c
#include <gst/gst.h>

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIS           = (1 << 3) /* Enable rendering of visualizations when there is no video stream. */
} GstPlayFlags;

/* Return TRUE if this is a Visualization element */
static gboolean filter_vis_features (GstPluginFeature *feature, gpointer data) {
  GstElementFactory *factory;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;
  factory = GST_ELEMENT_FACTORY (feature);
  if (!g_strrstr (gst_element_factory_get_klass (factory), "Visualization"))
    return FALSE;

  return TRUE;
}

int main(int argc, char *argv[]) {
  GstElement *pipeline, *vis_plugin;
  GstBus *bus;
  GstMessage *msg;
  GList *list, *walk;
  GstElementFactory *selected_factory = NULL;
  guint flags;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Get a list of all visualization plugins */
  list = gst_registry_feature_filter (gst_registry_get (), filter_vis_features, FALSE, NULL);

  /* Print their names */
  g_print("Available visualization plugins:\n");
  for (walk = list; walk != NULL; walk = g_list_next (walk)) {
    const gchar *name;
    GstElementFactory *factory;

    factory = GST_ELEMENT_FACTORY (walk->data);
    name = gst_element_factory_get_longname (factory);
    g_print("  %s\n", name);

    if (selected_factory == NULL || g_str_has_prefix (name, "GOOM")) {
      selected_factory = factory;
    }
  }

  /* Don't use the factory if it's still empty */
  /* e.g. no visualization plugins found */
  if (!selected_factory) {
    g_print ("No visualization plugins found!\n");
    return -1;
  }

  /* We have now selected a factory for the visualization element */
  g_print ("Selected '%s'\n", gst_element_factory_get_longname (selected_factory));
  vis_plugin = gst_element_factory_create (selected_factory, NULL);
  if (!vis_plugin)
    return -1;

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin uri=http://radio.hbr1.com:19800/ambient.ogg", NULL);

  /* Set the visualization flag */
  g_object_get (pipeline, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIS;
  g_object_set (pipeline, "flags", flags, NULL);

  /* set vis plugin for playbin */
  g_object_set (pipeline, "vis-plugin", vis_plugin, NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_plugin_feature_list_free (list);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

> ![information] If you need help to compile this code, refer to the
> **Building the tutorials** section for your platform: [Mac] or
> [Windows] or use this specific command on Linux:
>
> `` gcc playback-tutorial-6.c -o playback-tutorial-6 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Mac OS X], [Windows][1], for
> [iOS] or for [android].
>
> This tutorial plays music streamed from the [HBR1](http://www.hbr1.com/) Internet radio station. A window should open displaying somewhat psychedelic color patterns moving with the music. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

First off, we indicate `playbin` that we want an audio visualization by
setting the `GST_PLAY_FLAG_VIS` flag. If the media already contains
video, this flag has no effect.

``` c
/* Set the visualization flag */
g_object_get (pipeline, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIS;
g_object_set (pipeline, "flags", flags, NULL);
```

If no visualization plugin is enforced by the user, `playbin` will use
`goom` (audio visualization will be disabled if `goom` is not
available). The rest of the tutorial shows how to find out the available
visualization elements and enforce one to `playbin`.

``` c
/* Get a list of all visualization plugins */
list = gst_registry_feature_filter (gst_registry_get (), filter_vis_features, FALSE, NULL);
```

`gst_registry_feature_filter()` examines all elements currently in the
GStreamer registry and selects those for which
the `filter_vis_features` function returns TRUE. This function selects
only the Visualization plugins:

``` c
/* Return TRUE if this is a Visualization element */
static gboolean filter_vis_features (GstPluginFeature *feature, gpointer data) {
  GstElementFactory *factory;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;
  factory = GST_ELEMENT_FACTORY (feature);
  if (!g_strrstr (gst_element_factory_get_klass (factory), "Visualization"))
    return FALSE;

  return TRUE;
}
```

A bit of theory regarding the organization of GStreamer elements is in
place: Each of the files that GStreamer loads at runtime is known as a
Plugin (`GstPlugin`). A Plugin can contain many Features
(`GstPluginFeature`). There are different kinds of Features, among them,
the Element Factories (`GstElementFactory`) that we have been using to
build Elements (`GstElement`).

This function simply disregards all Features which are not Factories,
and then all Factories whose class (obtained with
`gst_element_factory_get_klass()`) does not include “Visualization”.  As
stated in the documentation for `GstElementFactory`, a Factory’s class
is a “string describing the type of element, as an unordered list
separated with slashes (/)”. Examples of classes are “Source/Network”,
“Codec/Decoder/Video”, “Codec/Encoder/Audio” or “Visualization”.

``` c
/* Print their names */
g_print("Available visualization plugins:\n");
for (walk = list; walk != NULL; walk = g_list_next (walk)) {
  const gchar *name;
  GstElementFactory *factory;

  factory = GST_ELEMENT_FACTORY (walk->data);
  name = gst_element_factory_get_longname (factory);
  g_print("  %s\n", name);

  if (selected_factory == NULL || g_str_has_prefix (name, "GOOM")) {
    selected_factory = factory;
  }
}
```

Once we have the list of Visualization plugins, we print their names
(`gst_element_factory_get_longname()`) and choose one (in this case,
GOOM).

``` c
/* We have now selected a factory for the visualization element */
g_print ("Selected '%s'\n", gst_element_factory_get_longname (selected_factory));
vis_plugin = gst_element_factory_create (selected_factory, NULL);
if (!vis_plugin)
  return -1;
```

The selected factory is used to instantiate an actual `GstElement` which
is then passed to `playbin` through the `vis-plugin` property:

``` c
/* set vis plugin for playbin */
g_object_set (pipeline, "vis-plugin", vis_plugin, NULL);
```

And we are done.

## Conclusion

This tutorial has shown:

  - How to enable Audio Visualization in `playbin` with the
    `GST_PLAY_FLAG_VIS` flag
  - How to enforce one particular visualization element with the
    `vis-plugin` `playbin` property

It has been a pleasure having you here, and see you soon\!

  [information]: images/icons/emoticons/information.svg
  [Mac]: installing/on-mac-osx.md
  [Windows]: installing/on-windows.md
  [Mac OS X]: installing/on-mac-osx.md#building-the-tutorials
  [1]: installing/on-windows.md#running-the-tutorials
  [iOS]: installing/for-ios-development.md#building-the-tutorials
  [android]: installing/for-android-development.md#building-the-tutorials
  [warning]: images/icons/emoticons/warning.svg
