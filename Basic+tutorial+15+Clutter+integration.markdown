# Basic tutorial 15: Clutter integration

This page last changed on Jul 11, 2012 by xartigas.

# Goal

“[Clutter](https://clutter-project.org/) is an open source software
library for creating fast, compelling, portable, and dynamic graphical
user interfaces”. GStreamer can be integrated into Clutter through the
`cluttersink` element, allowing video to be used as a texture. This
tutorial shows:

  - How to use the video output of a GStreamer pipeline as a texture in
    Clutter.

# Introduction

The process to link GStreamer with Clutter is actually very simple. A `
cluttersink `element must be instantiated (or, better,
`autocluttersink`, if available) and used as the video sink. Through the
`texture` property this element accepts a Clutter texture, which is
automatically updated by GStreamer.

# A 3D media player

Copy this code into a text file named `basic-tutorial-15.c`..

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>This tutorial is included in the SDK since release 2012.9. If you cannot find it in the downloaded code, please install the latest release of the GStreamer SDK.</p></td>
</tr>
</tbody>
</table>

**basic-tutorial-15.c**

``` theme: Default; brush: cpp; gutter: true
#include <clutter-gst/clutter-gst.h>

/* Setup the video texture once its size is known */
void size_change (ClutterActor *texture, gint width, gint height, gpointer user_data) {
  ClutterActor *stage;
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;
  ClutterAnimation *animation = NULL;

  stage = clutter_actor_get_stage (texture);
  if (stage == NULL)
    return;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  /* Center video on window and calculate new size preserving aspect ratio */
  new_height = (height * stage_width) / width;
  if (new_height <= stage_height) {
    new_width = stage_width;

    new_x = 0;
    new_y = (stage_height - new_height) / 2;
  } else {
    new_width  = (width * stage_height) / height;
    new_height = stage_height;

    new_x = (stage_width - new_width) / 2;
    new_y = 0;
  }
  clutter_actor_set_position (texture, new_x, new_y);
  clutter_actor_set_size (texture, new_width, new_height);
  clutter_actor_set_rotation (texture, CLUTTER_Y_AXIS, 0.0, stage_width / 2, 0, 0);
  /* Animate it */
  animation = clutter_actor_animate (texture, CLUTTER_LINEAR, 10000, "rotation-angle-y", 360.0, NULL);
  clutter_animation_set_loop (animation, TRUE);
}

int main(int argc, char *argv[]) {
  GstElement *pipeline, *sink;
  ClutterTimeline *timeline;
  ClutterActor *stage, *texture;

  /* clutter-gst takes care of initializing Clutter and GStreamer */
  if (clutter_gst_init (&argc, &argv) != CLUTTER_INIT_SUCCESS) {
    g_error ("Failed to initialize clutter\n");
    return -1;
  }

  stage = clutter_stage_get_default ();

  /* Make a timeline */
  timeline = clutter_timeline_new (1000);
  g_object_set(timeline, "loop", TRUE, NULL);

  /* Create new texture and disable slicing so the video is properly mapped onto it */
  texture = CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_TEXTURE, "disable-slicing", TRUE, NULL));
  g_signal_connect (texture, "size-change", G_CALLBACK (size_change), NULL);

  /* Build the GStreamer pipeline */
  pipeline = gst_parse_launch ("playbin2 uri=http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);

  /* Instantiate the Clutter sink */
  sink = gst_element_factory_make ("autocluttersink", NULL);
  if (sink == NULL) {
    /* Revert to the older cluttersink, in case autocluttersink was not found */
    sink = gst_element_factory_make ("cluttersink", NULL);
  }
  if (sink == NULL) {
    g_printerr ("Unable to find a Clutter sink.\n");
    return -1;
  }

  /* Link GStreamer with Clutter by passing the Clutter texture to the Clutter sink*/
  g_object_set (sink, "texture", texture, NULL);

  /* Add the Clutter sink to the pipeline */
  g_object_set (pipeline, "video-sink", sink, NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* start the timeline */
  clutter_timeline_start (timeline);

  /* Add texture to the stage, and show it */
  clutter_group_add (CLUTTER_GROUP (stage), texture);
  clutter_actor_show_all (stage);

  clutter_main();

  /* Free resources */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><div id="expander-1303246949" class="expand-container">
<div id="expander-control-1303246949" class="expand-control">
<span class="expand-control-icon"><img src="images/icons/grey_arrow_down.gif" class="expand-control-image" /></span><span class="expand-control-text">Need help? (Click to expand)</span>
</div>
<div id="expander-content-1303246949" class="expand-content">
<p>If you need help to compile this code, refer to the <strong>Building the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Build">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Build">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Build">Windows</a>, or use this specific command on Linux:</p>
<div class="panel" style="border-width: 1px;">
<div class="panelContent">
<p><code>gcc basic-tutorial-15.c -o basic-tutorial-15 `pkg-config --cflags --libs clutter-gst-1.0 gstreamer-0.10`</code></p>
</div>
</div>
<p>If you need help to run this code, refer to the <strong>Running the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Run">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Run">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Run">Windows</a></p>
<p><span>This tutorial opens a window and displays a movie <span>on a revolving plane</span>, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed.</span></p>
<p>Required libraries: <code>clutter-gst-1.0 gstreamer-0.10</code></p>
</div>
</div></td>
</tr>
</tbody>
</table>

# Walkthrough

It is not the purpose of this tutorial to teach how to use Clutter, but
how to integrate GStreamer with it. This is accomplished through the
clutter-gst library, so its header must be included (and the program
must link against it):

``` first-line: 1; theme: Default; brush: cpp; gutter: true
#include <clutter-gst/clutter-gst.h>
```

The first thing this library does is initialize both GStreamer and
Clutter, so you must call ` clutter-gst-init()` instead of initializing
these libraries yourself.

``` first-line: 43; theme: Default; brush: cpp; gutter: true
/* clutter-gst takes care of initializing Clutter and GStreamer */
if (clutter_gst_init (&argc, &argv) != CLUTTER_INIT_SUCCESS) {
  g_error ("Failed to initialize clutter\n");
  return -1;
}
```

The GStreamer video is to be played on a Clutter texture, so, we need to
create a texture. Just remember to disable texture slicing to allow for
proper
integration:

``` first-line: 55; theme: Default; brush: cpp; gutter: true
/* Create new texture and disable slicing so the video is properly mapped onto it */
texture = CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_TEXTURE, "disable-slicing", TRUE, NULL));
g_signal_connect (texture, "size-change", G_CALLBACK (size_change), NULL);
```

We connect to the size-change signal so we can perform final setup once
the video size is known.

``` theme: Default; brush: cpp; gutter: true
/* Instantiate the Clutter sink */
sink = gst_element_factory_make ("autocluttersink", NULL);
if (sink == NULL) {
  /* Revert to the older cluttersink, in case autocluttersink was not found */
  sink = gst_element_factory_make ("cluttersink", NULL);
}
if (sink == NULL) {
  g_printerr ("Unable to find a Clutter sink.\n");
  return -1;
}
```

The proper Clutter sink element to instantiate for GStreamer is
`autocluttersink`, which works more or less like `autovideosink`, trying
to find the best Clutter sink available. However, `autocluttersink` (for
which there is no documentation yet) is only available since the 2012.7
release of the SDK, so, if it cannot be found, the
simpler `cluttersink` element is created
instead.

``` first-line: 73; theme: Default; brush: cpp; gutter: true
/* Link GStreamer with Clutter by passing the Clutter texture to the Clutter sink*/
g_object_set (sink, "texture", texture, NULL);
```

This texture is everything GStreamer needs to know about Clutter.

``` first-line: 76; theme: Default; brush: cpp; gutter: true
/* Add the Clutter sink to the pipeline */
g_object_set (pipeline, "video-sink", sink, NULL);
```

Finally, tell `playbin2` to use the sink we created instead of the
default one.

Then the GStreamer pipeline and the Clutter timeline are started and the
ball starts rolling. Once the pipeline has received enough information
to know the video size (width and height), the Clutter texture gets
updated and we receive a notification, handled in the
`size_change` callback. This method sets the texture to the proper
size, centers it on the output window and starts a revolving animation,
just for demonstration purposes. But this has nothing to do with
GStreamer.

# Conclusion

This tutorial has shown:

  - How to use the video output of a GStreamer pipeline as a Clutter
    texture using the ` cluttersink` or `autocluttersink` elements.
  - How to link GStreamer and Clutter through the `texture` property of
    ` cluttersink` or `autocluttersink`.

It has been a pleasure having you here, and see you soon\!

Document generated by Confluence on Oct 08, 2015 10:27
