---
title: Pads and capabilities
...

# Pads and capabilities

As we have seen in [Elements][elements], the pads are the
element's interface to the outside world. Data streams from one
element's source pad to another element's sink pad. The specific type of
media that the element can handle will be exposed by the pad's
capabilities. We will talk more on capabilities later in this chapter
(see [Capabilities of a pad](#capabilities-of-a-pad)).

[elements]: application-development/basics/elements.md

## Pads

A pad type is defined by two properties: its direction and its
availability. As we've mentioned before, GStreamer defines two pad
directions: source pads and sink pads. This terminology is defined from
the view of within the element: elements receive data on their sink pads
and generate data on their source pads. Schematically, sink pads are
drawn on the left side of an element, whereas source pads are drawn on
the right side of an element. In such graphs, data flows from left to
right. \[1\]

Pad directions are very simple compared to pad availability. A pad can
have any of three availabilities: always, sometimes and on request. The
meaning of those three types is exactly as it says: always pads always
exist, sometimes pads exist only in certain cases (and can disappear
randomly), and on-request pads appear only if explicitly requested by
applications.

### Dynamic (or sometimes) pads

Some elements might not have all of their pads when the element is
created. This can happen, for example, with an Ogg demuxer element. The
element will read the Ogg stream and create dynamic pads for each
contained elementary stream (vorbis, theora) when it detects such a
stream in the Ogg stream. Likewise, it will delete the pad when the
stream ends. This principle is very useful for demuxer elements, for
example.

Running `gst-inspect-1.0 oggdemux` will show that the element has only one
pad: a sink pad called 'sink'. The other pads are “dormant”. You can see
this in the pad template because there is an “Availability: Sometimes”
property. Depending on the type of Ogg file you play, the pads will be
created. We will see that this is very important when you are going to
create dynamic pipelines. You can attach a signal handler to an element
to inform you when the element has created a new pad from one of its
“sometimes” pad templates. The following piece of code is an example
of how to do this:

``` c
#include <gst/gst.h>

static void
cb_new_pad (GstElement *element,
        GstPad     *pad,
        gpointer    data)
{
  gchar *name;

  name = gst_pad_get_name (pad);
  g_print ("A new pad %s was created\n", name);
  g_free (name);

  /* here, you would setup a new pad link for the newly created pad */
[..]

}

int
main (int   argc,
      char *argv[])
{
  GstElement *pipeline, *source, *demux;
  GMainLoop *loop;

  /* init */
  gst_init (&argc, &argv);

  /* create elements */
  pipeline = gst_pipeline_new ("my_pipeline");
  source = gst_element_factory_make ("filesrc", "source");
  g_object_set (source, "location", argv[1], NULL);
  demux = gst_element_factory_make ("oggdemux", "demuxer");

  /* you would normally check that the elements were created properly */

  /* put together a pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, demux, NULL);
  gst_element_link_pads (source, "src", demux, "sink");

  /* listen for newly created pads */
  g_signal_connect (demux, "pad-added", G_CALLBACK (cb_new_pad), NULL);

  /* start the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

[..]

}

```

It is not uncommon to add elements to the pipeline only from within the
"pad-added" callback. If you do this, don't forget to set the state of
the newly-added elements to the target state of the pipeline using
`gst_element_set_state ()` or `gst_element_sync_state_with_parent ()`.

### Request pads

An element can also have request pads. These pads are not created
automatically but are only created on demand. This is very useful for
multiplexers, aggregators and tee elements. Aggregators are elements
that merge the content of several input streams together into one output
stream. Tee elements are the reverse: they are elements that have one
input stream and copy this stream to each of their output pads, which
are created on request. Whenever an application needs another copy of
the stream, it can simply request a new output pad from the tee element.

The following piece of code shows how you can request a new output pad
from a “tee” element:

{{ snippets.c#some_function }}

The `gst_element_request_pad_simple ()` method can be used to get a pad
from the element based on the name of the pad template. It is also
possible to request a pad that is compatible with another pad template.
This is very useful if you want to link an element to a multiplexer
element and you need to request a pad that is compatible. The method
`gst_element_get_compatible_pad ()` can be used to request a compatible
pad, as shown in the next example. It will request a compatible pad from
an Ogg multiplexer from any input.

{{ snippets.c#link_to_multiplexer }}

## Capabilities of a pad

Since the pads play a very important role in how the element is viewed
by the outside world, a mechanism is implemented to describe the data
that can flow or currently flows through the pad by using capabilities.
Here, we will briefly describe what capabilities are and how to use
them, enough to get an understanding of the concept. For an in-depth
look into capabilities and a list of all capabilities defined in
GStreamer, see the [Plugin Writers Guide](plugin-development/index.md)

Capabilities are attached to pad templates and to pads. For pad
templates, it will describe the types of media that may stream over a
pad created from this template. For pads, it can either be a list of
possible caps (usually a copy of the pad template's capabilities), in
which case the pad is not yet negotiated, or it is the type of media
that currently streams over this pad, in which case the pad has been
negotiated already.

### Dissecting capabilities

A pad's capabilities are described in a `GstCaps` object. Internally, a
[`GstCaps`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstCaps.html)
will contain one or more
[`GstStructure`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstStructure.html)
that will describe one media type. A negotiated pad will have
capabilities set that contain exactly *one* structure. Also, this
structure will contain only *fixed* values. These constraints are not
true for unnegotiated pads or pad templates.

As an example, below is a dump of the capabilities of the “vorbisdec”
element, which you will get by running `gst-inspect vorbisdec`. You will
see two pads: a source and a sink pad. Both of these pads are always
available, and both have capabilities attached to them. The sink pad
will accept vorbis-encoded audio data, with the media type
“audio/x-vorbis”. The source pad will be used to send raw (decoded)
audio samples to the next element, with a raw audio media type (in this
case, “audio/x-raw”). The source pad will also contain properties for
the audio samplerate and the amount of channels, plus some more that you
don't need to worry about for now.

```
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      audio/x-raw
                 format: F32LE
                   rate: [ 1, 2147483647 ]
               channels: [ 1, 256 ]

  SINK template: 'sink'
    Availability: Always
    Capabilities:
      audio/x-vorbis
```

### Properties and values

Properties are used to describe extra information for capabilities. A
property consists of a key (a string) and a value. There are different
possible value types that can be used:

  - Basic types, this can be pretty much any `GType` registered with
    Glib. Those properties indicate a specific, non-dynamic value for
    this property. Examples include:

      - An integer value (`G_TYPE_INT`): the property has this exact
        value.

      - A boolean value (`G_TYPE_BOOLEAN`): the property is either `TRUE`
        or `FALSE`.

      - A float value (`G_TYPE_FLOAT`): the property has this exact
        floating point value.

      - A string value (`G_TYPE_STRING`): the property contains a UTF-8
        string.

      - A fraction value (`GST_TYPE_FRACTION`): contains a fraction
        expressed by an integer numerator and denominator.

  - Range types are `GType`s registered by GStreamer to indicate a range
    of possible values. They are used for indicating allowed audio
    samplerate values or supported video sizes. The two types defined in
    GStreamer are:

      - An integer range value (`GST_TYPE_INT_RANGE`): the property
        denotes a range of possible integers, with a lower and an upper
        boundary. The “vorbisdec” element, for example, has a rate
        property that can be between 8000 and 50000.

      - A float range value (`GST_TYPE_FLOAT_RANGE`): the property
        denotes a range of possible floating point values, with a lower
        and an upper boundary.

      - A fraction range value (`GST_TYPE_FRACTION_RANGE`): the property
        denotes a range of possible fraction values, with a lower and an
        upper boundary.

  - A list value (`GST_TYPE_LIST`): the property can take any value from
    a list of basic values given in this list.

    Example: caps that express that either a sample rate of 44100 Hz and
    a sample rate of 48000 Hz is supported would use a list of integer
    values, with one value being 44100 and one value being 48000.

  - An array value (`GST_TYPE_ARRAY`): the property is an array of
    values. Each value in the array is a full value on its own, too. All
    values in the array should be of the same elementary type. This
    means that an array can contain any combination of integers, lists
    of integers, integer ranges together, and the same for floats or
    strings, but it can not contain both floats and ints at the same
    time.

    Example: for audio where there are more than two channels involved
    the channel layout needs to be specified (for one and two channel
    audio the channel layout is implicit unless stated otherwise in the
    caps). So the channel layout would be an array of integer enum
    values where each enum value represents a loudspeaker position.
    Unlike a `GST_TYPE_LIST`, the values in an array will be interpreted
    as a whole.

## What capabilities are used for

Capabilities (short: caps) describe the type of data that is streamed
between two pads, or that one pad (template) supports. This makes them
very useful for various purposes:

  - Autoplugging: automatically finding elements to link to a pad based
    on its capabilities. All autopluggers use this method.

  - Compatibility detection: when two pads are linked, GStreamer can
    verify if the two pads are talking about the same media type. The
    process of linking two pads and checking if they are compatible is
    called “caps negotiation”.

  - Metadata: by reading the capabilities from a pad, applications can
    provide information about the type of media that is being streamed
    over the pad, which is information about the stream that is
    currently being played back.

  - Filtering: an application can use capabilities to limit the possible
    media types that can stream between two pads to a specific subset of
    their supported stream types. An application can, for example, use
    “filtered caps” to set a specific (fixed or non-fixed) video size
    that should stream between two pads. You will see an example of
    filtered caps later in this manual, in [Manually adding or removing
    data from/to a pipeline][inserting-or-extracting-data].
    You can do caps filtering by inserting a capsfilter element into
    your pipeline and setting its “caps” property. Caps filters are
    often placed after converter elements like audioconvert,
    audioresample, videoconvert or videoscale to force those converters
    to convert data to a specific output format at a certain point in a
    stream.

[inserting-or-extracting-data]: application-development/advanced/pipeline-manipulation.md#manually-adding-or-removing-data-fromto-a-pipeline

### Using capabilities for metadata

A pad can have a set (i.e. one or more) of capabilities attached to it.
Capabilities (`GstCaps`) are represented as an array of one or more
`GstStructure`s, and each `GstStructure` is an array of fields where
each field consists of a field name string (e.g. "width") and a typed
value (e.g. `G_TYPE_INT` or `GST_TYPE_INT_RANGE`).

Note that there is a distinct difference between the *possible*
capabilities of a pad (ie. usually what you find as caps of pad
templates as they are shown in gst-inspect), the *allowed* caps of a pad
(can be the same as the pad's template caps or a subset of them,
depending on the possible caps of the peer pad) and lastly *negotiated*
caps (these describe the exact format of a stream or buffer and contain
exactly one structure and have no variable bits like ranges or lists,
ie. they are fixed caps).

You can get values of properties in a set of capabilities by querying
individual properties of one structure. You can get a structure from a
caps using `gst_caps_get_structure ()` and the number of structures in a
`GstCaps` using `gst_caps_get_size ()`.

Caps are called *simple caps* when they contain only one structure, and
*fixed caps* when they contain only one structure and have no variable
field types (like ranges or lists of possible values). Two other special
types of caps are *ANY caps* and *empty caps*.

Here is an example of how to extract the width and height from a set of
fixed video caps:

``` c
static void
read_video_props (GstCaps *caps)
{
  gint width, height;
  const GstStructure *str;

  g_return_if_fail (gst_caps_is_fixed (caps));

  str = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (str, "width", &width) ||
      !gst_structure_get_int (str, "height", &height)) {
    g_print ("No width/height available\n");
    return;
  }

  g_print ("The video size of this set of capabilities is %dx%d\n",
       width, height);
}

```

### Creating capabilities for filtering

While capabilities are mainly used inside a plugin to describe the media
type of the pads, the application programmer often also has to have
basic understanding of capabilities in order to interface with the
plugins, especially when using filtered caps. When you're using filtered
caps or fixation, you're limiting the allowed types of media that can
stream between two pads to a subset of their supported media types. You
do this using a `capsfilter` element in your pipeline. In order to do
this, you also need to create your own `GstCaps`. The easiest way to do
this is by using the convenience function `gst_caps_new_simple ()`:

``` c
static gboolean
link_elements_with_filter (GstElement *element1, GstElement *element2)
{
  gboolean link_ok;
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "I420",
          "width", G_TYPE_INT, 384,
          "height", G_TYPE_INT, 288,
          "framerate", GST_TYPE_FRACTION, 25, 1,
          NULL);

  link_ok = gst_element_link_filtered (element1, element2, caps);
  gst_caps_unref (caps);

  if (!link_ok) {
    g_warning ("Failed to link element1 and element2!");
  }

  return link_ok;
}

```

This will force the data flow between those two elements to a certain
video format, width, height and framerate (or the linking will fail if
that cannot be achieved in the context of the elements involved). Keep
in mind that when you use `
gst_element_link_filtered ()` it will automatically create a
`capsfilter` element for you and insert it into your bin or pipeline
between the two elements you want to connect (this is important if you
ever want to disconnect those elements because then you will have to
disconnect both elements from the capsfilter instead).

In some cases, you will want to create a more elaborate set of
capabilities to filter a link between two pads. Then, this function is
too simplistic and you'll want to use the method `gst_caps_new_full ()`:

``` c
static gboolean
link_elements_with_filter (GstElement *element1, GstElement *element2)
{
  gboolean link_ok;
  GstCaps *caps;

  caps = gst_caps_new_full (
      gst_structure_new ("video/x-raw",
             "width", G_TYPE_INT, 384,
             "height", G_TYPE_INT, 288,
             "framerate", GST_TYPE_FRACTION, 25, 1,
             NULL),
      gst_structure_new ("video/x-bayer",
             "width", G_TYPE_INT, 384,
             "height", G_TYPE_INT, 288,
             "framerate", GST_TYPE_FRACTION, 25, 1,
             NULL),
      NULL);

  link_ok = gst_element_link_filtered (element1, element2, caps);
  gst_caps_unref (caps);

  if (!link_ok) {
    g_warning ("Failed to link element1 and element2!");
  }

  return link_ok;
}

```

See the API references for the full API of
[`GstStructure`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstStructure.html)
and
[`GstCaps`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstCaps.html).

## Ghost pads

You can see from [Visualisation of a GstBin element without ghost
pads](#visualisation-of-a-gstbin-------element-without-ghost-pads) how a
bin has no pads of its own. This is where "ghost pads" come into play.

![Visualisation of a
[`GstBin`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstBin.html)
element without ghost pads](images/bin-element-noghost.png "fig:")

A ghost pad is a pad from some element in the bin that can be accessed
directly from the bin as well. Compare it to a symbolic link in UNIX
filesystems. Using ghost pads on bins, the bin also has a pad and can
transparently be used as an element in other parts of your code.

![Visualisation of a
[`GstBin`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstBin.html)
element with a ghost pad](images/bin-element-ghost.png "fig:")

[Visualisation of a GstBin element with a ghost
pad](#visualisation-of-a-gstbin-------element-with-a-ghost-pad) is a
representation of a ghost pad. The sink pad of element one is now also a
pad of the bin. Because ghost pads look and work like any other pads,
they can be added to any type of elements, not just to a `GstBin`, just
like ordinary pads.

A ghostpad is created using the function `gst_ghost_pad_new ()`:

``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  GstElement *bin, *sink;
  GstPad *pad;

  /* init */
  gst_init (&argc, &argv);

  /* create element, add to bin */
  sink = gst_element_factory_make ("fakesink", "sink");
  bin = gst_bin_new ("mybin");
  gst_bin_add (GST_BIN (bin), sink);

  /* add ghostpad */
  pad = gst_element_get_static_pad (sink, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

[..]

}

```

In the above example, the bin now also has a pad: the pad called “sink”
of the given element. The bin can, from here on, be used as a substitute
for the sink element. You could, for example, link another element to
the bin.

1.  In reality, there is no objection to data flowing from a source pad
    to the sink pad of an element upstream (to the left of this element
    in drawings). Data will, however, always flow from a source pad of
    one element to the sink pad of another.
