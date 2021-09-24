---
title: Elements
...

# Elements

The most important object in GStreamer for the application programmer is
the
[`GstElement`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElement.html)
object. An element is the basic building block for a media pipeline. All
the different high-level components you will use are derived from
`GstElement`. Every decoder, encoder, demuxer, video or audio output is
in fact a `GstElement`

## What are elements?

For the application programmer, elements are best visualized as black
boxes. On the one end, you might put something in, the element does
something with it and something else comes out at the other side. For a
decoder element, for example, you'd put in encoded data, and the element
would output decoded data. In the next chapter (see [Pads and
capabilities][pads]), you will learn more about data input
and output in elements, and how you can set that up in your application.

[pads]: application-development/basics/pads.md

### Source elements

Source elements generate data for use by a pipeline, for example reading
from disk or from a sound card. [Visualisation of a source
element](#visualisation-of-a-source-element) shows how we will visualise
a source element. We always draw a source pad to the right of the
element.

![Visualisation of a source element](images/src-element.png "fig:")

Source elements do not accept data, they only generate data. You can see
this in the figure because it only has a source pad (on the right). A
source pad can only generate data.

### Filters, convertors, demuxers, muxers and codecs

Filters and filter-like elements have both input and outputs pads. They
operate on data that they receive on their input (sink) pads, and will
provide data on their output (source) pads. Examples of such elements
are a volume element (filter), a video scaler (convertor), an Ogg
demuxer or a Vorbis decoder.

Filter-like elements can have any number of source or sink pads. A video
demuxer, for example, would have one sink pad and several (1-N) source
pads, one for each elementary stream contained in the container format.
Decoders, on the other hand, will only have one source and sink pads.

![Visualisation of a filter element](images/filter-element.png "fig:")

[Visualisation of a filter element](#visualisation-of-a-filter-element)
shows how we will visualise a filter-like element. This specific element
has one source pad and one sink pad. Sink pads, receiving input data,
are depicted at the left of the element; source pads are still on the
right.

![Visualisation of a filter element with more than one output
pad](images/filter-element-multi.png "fig:")

[Visualisation of a filter element with more than one output
pad](#visualisation-of-a-filter-element-with----more-than-one-output-pad)
shows another filter-like element, this one having more than one output
(source) pad. An example of one such element could, for example, be an
Ogg demuxer for an Ogg stream containing both audio and video. One
source pad will contain the elementary video stream, another will
contain the elementary audio stream. Demuxers will generally fire
signals when a new pad is created. The application programmer can then
handle the new elementary stream in the signal handler.

### Sink elements

Sink elements are end points in a media pipeline. They accept data but
do not produce anything. Disk writing, soundcard playback, and video
output would all be implemented by sink elements. [Visualisation of a
sink element](#visualisation-of-a-sink-element) shows a sink element.

![Visualisation of a sink element](images/sink-element.png "fig:")

## Creating a `GstElement`

The simplest way to create an element is to use
[`gst_element_factory_make
()`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElementFactory.html#gst-element-factory-make).
This function takes a factory name and an element name for the newly
created element. The name of the element is something you can use later
on to look up the element in a bin, for example. The name will also be
used in debug output. You can pass `NULL` as the name argument to get a
unique, default name.

When you don't need the element anymore, you need to unref it using
[`gst_object_unref
()`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstObject.html#gst-object-unref).
This decreases the reference count for the element by 1. An element has
a refcount of 1 when it gets created. An element gets destroyed
completely when the refcount is decreased to 0.

The following example \[1\] shows how to create an element named
*source* from the element factory named *fakesrc*. It checks if the
creation succeeded. After checking, it unrefs the element.

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElement *element;

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* create element */
  element = gst_element_factory_make ("fakesrc", "source");
  if (!element) {
    g_print ("Failed to create element of type 'fakesrc'\n");
    return -1;
  }

  gst_object_unref (GST_OBJECT (element));

  return 0;
}

```

`gst_element_factory_make` is actually a shorthand for a combination of
two functions. A
[`GstElement`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElement.html)
object is created from a factory. To create the element, you have to get
access to a
[`GstElementFactory`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElementFactory.html)
object using a unique factory name. This is done with
[`gst_element_factory_find
()`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElementFactory.html#gst-element-factory-find).

The following code fragment is used to get a factory that can be used to
create the *fakesrc* element, a fake data source. The function
[`gst_element_factory_create
()`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElementFactory.html#gst-element-factory-create)
will use the element factory to create an element with the given name.

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElementFactory *factory;
  GstElement * element;

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* create element, method #2 */
  factory = gst_element_factory_find ("fakesrc");
  if (!factory) {
    g_print ("Failed to find factory of type 'fakesrc'\n");
    return -1;
  }
  element = gst_element_factory_create (factory, "source");
  if (!element) {
    g_print ("Failed to create element, even though its factory exists!\n");
    return -1;
  }

  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (factory));

  return 0;
}

```

## Using an element as a `GObject`

A
[`GstElement`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElement.html)
can have several properties which are implemented using standard
`GObject` properties. The usual `GObject` methods to query, set and get
property values and `GParamSpecs` are therefore supported.

Every `GstElement` inherits at least one property from its parent
`GstObject`: the "name" property. This is the name you provide to the
functions `gst_element_factory_make ()` or `gst_element_factory_create
()`. You can get and set this property using the functions
`gst_object_set_name` and `gst_object_get_name` or use the `GObject`
property mechanism as shown below.

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElement *element;
  gchar *name;

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* create element */
  element = gst_element_factory_make ("fakesrc", "source");

  /* get name */
  g_object_get (G_OBJECT (element), "name", &name, NULL);
  g_print ("The name of the element is '%s'.\n", name);
  g_free (name);

  gst_object_unref (GST_OBJECT (element));

  return 0;
}

```

Most plugins provide additional properties to provide more information
about their configuration or to configure the element. `gst-inspect` is
a useful tool to query the properties of a particular element, it will
also use property introspection to give a short explanation about the
function of the property and about the parameter types and ranges it
supports. See [gst-inspect][checklist-gst-inspect] in
the appendix for details about `gst-inspect`.

For more information about `GObject` properties we recommend you read
the [GObject
manual](http://developer.gnome.org/gobject/stable/rn01.html) and an
introduction to [The Glib Object
system](http://developer.gnome.org/gobject/stable/pt01.html).

A
[`GstElement`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElement.html)
also provides various `GObject` signals that can be used as a flexible
callback mechanism. Here, too, you can use `gst-inspect` to see which
signals a specific element supports. Together, signals and properties
are the most basic way in which elements and applications interact.

[checklist-gst-inspect]: application-development/appendix/checklist-element.md#gst-inspect

## More about element factories

In the previous section, we briefly introduced the
[`GstElementFactory`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstElementFactory.html)
object already as a way to create instances of an element. Element
factories, however, are much more than just that. Element factories are
the basic types retrieved from the GStreamer registry, they describe all
plugins and elements that GStreamer can create. This means that element
factories are useful for automated element instancing, such as what
autopluggers do, and for creating lists of available elements.

### Getting information about an element using a factory

Tools like `gst-inspect` will provide some generic information about an
element, such as the person that wrote the plugin, a descriptive name
(and a shortname), a rank and a category. The category can be used to
get the type of the element that can be created using this element
factory. Examples of categories include `Codec/Decoder/Video` (video
decoder), `Codec/Encoder/Video` (video encoder), `Source/Video` (a video
generator), `Sink/Video` (a video output), and all these exist for audio
as well, of course. Then, there's also `Codec/Demuxer` and `Codec/Muxer`
and a whole lot more. `gst-inspect` will give a list of all factories,
and `gst-inspect <factory-name>` will list all of the above information,
and a lot more.

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElementFactory *factory;

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* get factory */
  factory = gst_element_factory_find ("fakesrc");
  if (!factory) {
    g_print ("You don't have the 'fakesrc' element installed!\n");
    return -1;
  }

  /* display information */
  g_print ("The '%s' element is a member of the category %s.\n"
           "Description: %s\n",
           gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
           gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS),
           gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_DESCRIPTION));

  gst_object_unref (GST_OBJECT (factory));

  return 0;
}

```

You can use `gst_registry_pool_feature_list (GST_TYPE_ELEMENT_FACTORY)`
to get a list of all the element factories that GStreamer knows about.

### Finding out what pads an element can contain

Perhaps the most powerful feature of element factories is that they
contain a full description of the pads that the element can generate,
and the capabilities of those pads (in layman words: what types of media
can stream over those pads), without actually having to load those
plugins into memory. This can be used to provide a codec selection list
for encoders, or it can be used for autoplugging purposes for media
players. All current GStreamer-based media players and autopluggers work
this way. We'll look closer at these features as we learn about `GstPad`
and `GstCaps` in the next chapter: [Pads and capabilities][pads]

## Linking elements

By linking a source element with zero or more filter-like elements and
finally a sink element, you set up a media pipeline. Data will flow
through the elements. This is the basic concept of media handling in
GStreamer.

![Visualisation of three linked elements](images/linked-elements.png
"fig:")

By linking these three elements, we have created a very simple chain of
elements. The effect of this will be that the output of the source
element will be used as input for the filter-like element. The filter-like
element will do something with the data and send the result to the final
sink element.

Imagine the above graph as a simple Ogg/Vorbis audio decoder. The source
is a disk source which reads the file from disc. The second element is a
Ogg/Vorbis audio decoder. The sink element is your soundcard, playing
back the decoded audio data. We will use this simple graph to construct
an Ogg/Vorbis player later in this manual.

In code, the above graph is written like this:

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElement *pipeline;
  GstElement *source, *filter, *sink;

  /* init */
  gst_init (&argc, &argv);

  /* create pipeline */
  pipeline = gst_pipeline_new ("my-pipeline");

  /* create elements */
  source = gst_element_factory_make ("fakesrc", "source");
  filter = gst_element_factory_make ("identity", "filter");
  sink = gst_element_factory_make ("fakesink", "sink");

  /* must add elements to pipeline before linking them */
  gst_bin_add_many (GST_BIN (pipeline), source, filter, sink, NULL);

  /* link */
  if (!gst_element_link_many (source, filter, sink, NULL)) {
    g_warning ("Failed to link elements!");
  }

[..]

}

```

For more specific behaviour, there are also the functions
`gst_element_link ()` and `gst_element_link_pads ()`. You can also
obtain references to individual pads and link those using various
`gst_pad_link_* ()` functions. See the API references for more details.

Important: you must add elements to a bin or pipeline *before* linking
them, since adding an element to a bin will disconnect any already
existing links. Also, you cannot directly link elements that are not in
the same bin or pipeline; if you want to link elements or pads at
different hierarchy levels, you will need to use ghost pads (more about
[ghost pads][ghostpads] later).

[ghostpads]: application-development/basics/pads.md#ghost-pads

## Element States

After being created, an element will not actually perform any actions
yet. You need to change elements state to make it do something.
GStreamer knows four element states, each with a very specific meaning.
Those four states are:

  - `GST_STATE_NULL`: this is the default state. No resources are
    allocated in this state, so, transitioning to it will free all
    resources. The element must be in this state when its refcount
    reaches 0 and it is freed.

  - `GST_STATE_READY`: in the ready state, an element has allocated all
    of its global resources, that is, resources that can be kept within
    streams. You can think about opening devices, allocating buffers and
    so on. However, the stream is not opened in this state, so the
    stream positions is automatically zero. If a stream was previously
    opened, it should be closed in this state, and position, properties
    and such should be reset.

  - `GST_STATE_PAUSED`: in this state, an element has opened the stream,
    but is not actively processing it. An element is allowed to modify a
    stream's position, read and process data and such to prepare for
    playback as soon as state is changed to PAUSED, but it is *not*
    allowed to play the data which would make the clock run. In summary,
    PAUSED is the same as PLAYING but without a running clock.

    Elements going into the `PAUSED` state should prepare themselves for
    moving over to the `PLAYING` state as soon as possible. Video or audio
    outputs would, for example, wait for data to arrive and queue it so
    they can play it right after the state change. Also, video sinks can
    already play the first frame (since this does not affect the clock
    yet). Autopluggers could use this same state transition to already
    plug together a pipeline. Most other elements, such as codecs or
    filters, do not need to explicitly do anything in this state,
    however.

  - `GST_STATE_PLAYING`: in the `PLAYING` state, an element does exactly
    the same as in the `PAUSED` state, except that the clock now runs.

You can change the state of an element using the function
`gst_element_set_state ()`. If you set an element to another state,
GStreamer will internally traverse all intermediate states. So if you
set an element from `NULL` to `PLAYING`, GStreamer will internally set the
element to `READY` and `PAUSED` in between.

When moved to `GST_STATE_PLAYING`, pipelines will process data
automatically. They do not need to be iterated in any form. Internally,
GStreamer will start threads that take on this task for them. GStreamer
will also take care of switching messages from the pipeline's thread
into the application's own thread, by using a
[`GstBus`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstBus.html).
See [Bus][bus] for details.

When you set a bin or pipeline to a certain target state, it will
usually propagate the state change to all elements within the bin or
pipeline automatically, so it's usually only necessary to set the state
of the top-level pipeline to start up the pipeline or shut it down.
However, when adding elements dynamically to an already-running
pipeline, e.g. from within a "pad-added" signal callback, you need to
set it to the desired target state yourself using `gst_element_set_state
()` or `gst_element_sync_state_with_parent ()`.

[bus]: application-development/basics/bus.md

1.  The code for this example is automatically extracted from the
    documentation and built under `tests/examples/manual` in the
    GStreamer tarball.
