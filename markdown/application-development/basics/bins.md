---
title: Bins
...

# Bins

A bin is a container element. You can add elements to a bin. Since a bin
is an element itself, a bin can be handled in the same way as any other
element. Therefore, the whole previous chapter ([Elements][elements]) applies
to bins as well.

[elements]: application-development/basics/elements.md

## What are bins

Bins allow you to combine a group of linked elements into one logical
element. You do not deal with the individual elements anymore but with
just one element, the bin. We will see that this is extremely powerful
when you are going to construct complex pipelines since it allows you to
break up the pipeline in smaller chunks.

The bin will also manage the elements contained in it. It will perform
state changes on the elements as well as collect and forward bus
messages.

![Visualisation of a bin with some elements in
it](images/bin-element.png "fig:")

There is one specialized type of bin available to the GStreamer
programmer:

  - A pipeline: a generic container that manages the synchronization and
    bus messages of the contained elements. The toplevel bin has to be a
    pipeline, every application thus needs at least one of these.

## Creating a bin

Bins are created in the same way that other elements are created, i.e.
using an element factory. There are also convenience functions available
(`gst_bin_new ()` and `gst_pipeline_new ()`). To add elements to a bin
or remove elements from a bin, you can use `gst_bin_add ()` and
`gst_bin_remove ()`. Note that the bin that you add an element to will
take ownership of that element. If you destroy the bin, the element will
be dereferenced with it. If you remove an element from a bin, it will be
dereferenced automatically.

```c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElement *bin, *pipeline, *source, *sink;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  pipeline = gst_pipeline_new ("my_pipeline");
  bin = gst_bin_new ("my_bin");
  source = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  /* First add the elements to the bin */
  gst_bin_add_many (GST_BIN (bin), source, sink, NULL);
  /* add the bin to the pipeline */
  gst_bin_add (GST_BIN (pipeline), bin);

  /* link the elements */
  gst_element_link (source, sink);

[..]

}

```

There are various functions to lookup elements in a bin. The most
commonly used are `gst_bin_get_by_name ()` and `gst_bin_get_by_interface
()`. You can also iterate over all elements that a bin contains using
the function `gst_bin_iterate_elements ()`. See the API references of
[`GstBin`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstBin.html)
for details.

## Custom bins

The application programmer can create custom bins packed with elements
to perform a specific task. This allows you, for example, to write an
Ogg/Vorbis decoder with just the following lines of code:

```c
int
main (int   argc,
      char *argv[])
{
  GstElement *player;

  /* init */
  gst_init (&argc, &argv);

  /* create player */
  player = gst_element_factory_make ("oggvorbisplayer", "player");

  /* set the source audio file */
  g_object_set (player, "location", "helloworld.ogg", NULL);

  /* start playback */
  gst_element_set_state (GST_ELEMENT (player), GST_STATE_PLAYING);
[..]
}

```

(This is a silly example of course, there already exists a much more
powerful and versatile custom bin like this: the playbin element.)

Custom bins can be created with a plugin or from the application. You
will find more information about creating custom bin in the [Plugin
Writer's Guide](plugin-development/index.md)

Examples of such custom bins are the playbin and uridecodebin elements
from [gst-plugins-base](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/index.html).

## Bins manage states of their children

Bins manage the state of all elements contained in them. If you set a
bin (or a pipeline, which is a special top-level type of bin) to a
certain target state using `gst_element_set_state ()`, it will make sure
all elements contained within it will also be set to this state. This
means it's usually only necessary to set the state of the top-level
pipeline to start up the pipeline or shut it down.

The bin will perform the state changes on all its children from the sink
element to the source element. This ensures that the downstream element
is ready to receive data when the upstream element is brought to `PAUSED`
or `PLAYING`. Similarly when shutting down, the sink elements will be set
to `READY` or `NULL` first, which will cause the upstream elements to
receive a `FLUSHING` error and stop the streaming threads before the
elements are set to the `READY` or `NULL` state.

Note, however, that if elements are added to a bin or pipeline that's
already running, e.g. from within a "pad-added" signal callback, its
state will not automatically be brought in line with the current state
or target state of the bin or pipeline it was added to. Instead, you
need to set it to the desired target state yourself using
`gst_element_set_state ()` or `gst_element_sync_state_with_parent ()`
when adding elements to an already-running pipeline.
