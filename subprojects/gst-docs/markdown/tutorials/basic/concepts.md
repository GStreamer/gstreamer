---
short-description: Gstreamer Elements, Pipeline and the Bus
...

{{ ALERT_JS.md }}

# Basic tutorial 2: GStreamer concepts



## Goal

The previous tutorial showed how to build a pipeline automatically. Now
we are going to build a pipeline manually by instantiating each element
and linking them all together. In the process, we will learn:

  - What is a GStreamer element and how to create one.

  - How to connect elements to each other.

  - How to customize an element's behavior.

  - How to watch the bus for error conditions and extract information
    from GStreamer messages.

## Manual Hello World

{{ C+JS_FALLBACK.md }}
  Copy this code into a text file named `basic-tutorial-2.c` (or find it
  in your GStreamer installation).

  **basic-tutorial-2.c**

  {{ tutorials/basic-tutorial-2.c }}

  > ![Information](images/icons/emoticons/information.svg)
  > Need help?
  >
  > If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux], [Mac OS X] or [Windows], or use this specific command on Linux:
  >
  > `` gcc basic-tutorial-2.c -o basic-tutorial-2 `pkg-config --cflags --libs gstreamer-1.0` ``
  >
  >If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux][1], [Mac OS X][[2]] or [Windows][3].
  >
  >This tutorial opens a window and displays a test pattern, without audio
  >
  >Required libraries: `gstreamer-1.0`
{{ END_LANG.md }}


{{ PY.md }}
  Copy this code into a text file named `basic-tutorial-2.py` (or find it
  in your GStreamer installation).
  **basic-tutorial-2.py**

  {{ tutorials/python/basic-tutorial-2.py }}

  Then, you can run the file with `python3 basic-tutorial-2.py`
{{ END_LANG.md }}



## Walkthrough

The *elements* are GStreamer's basic construction blocks. They process
the data as it flows *downstream* from the source elements (data producers)
to the sink elements (data consumers), passing through filter elements.

![](images/figure-1.png)

**Figure 1**. Example pipeline

### Element creation

We will skip GStreamer initialization, since it is the same as the
previous tutorial:

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[17:20] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[18:21] }}
{{ END_LANG.md }}


As seen in this code, new elements can be created
with [gst_element_factory_make]\(). The first parameter is the type of
element to create ([Basic tutorial 14: Handy
elements] shows a
few common types, and [Basic tutorial 10: GStreamer
tools] shows how to
obtain the list of all available types). The second parameter is the
name we want to give to this particular instance. Naming your elements
is useful to retrieve them later if you didn't keep a pointer (and for
more meaningful debug output). If you pass [NULL] for the name, however,
GStreamer will provide a unique name for you.

For this tutorial we create two elements: a [videotestsrc] and
an [autovideosink]. There are no filter elements. Hence, the pipeline would
look like the following:

![](images/basic-concepts-pipeline.png)

**Figure 2**. Pipeline built in this tutorial

[videotestsrc] is a source element (it produces data), which creates a
test video pattern. This element is useful for debugging purposes (and
tutorials) and is not usually found in real applications.

[autovideosink] is a sink element (it consumes data), which displays on
a window the images it receives. There exist several video sinks,
depending on the operating system, with a varying range of capabilities.
[autovideosink] automatically selects and instantiates the best one, so
you do not have to worry with the details, and your code is more
platform-independent.

### Pipeline creation

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[21:23] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[22:24] }}
{{ END_LANG.md }}

All elements in GStreamer must typically be contained inside a pipeline
before they can be used, because it takes care of some clocking and
messaging functions. We create the pipeline with [gst_pipeline_new]\().

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[29:36] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[30:35] }}
{{ END_LANG.md }}


A pipeline is a particular type of [bin], which is the element used to
contain other elements. Therefore all methods which apply to bins also
apply to pipelines.
{{ C+JS_FALLBACK.md }}
  In our case, we call [gst_bin_add_many]\() to add the
  elements to the pipeline (mind the cast). This function accepts a list
  of elements to be added, ending with [NULL]. Individual elements can be
  added with [gst_bin_add]\().
{{ END_LANG.md }}
{{ PY.md }}
  In our case, we call [gst_bin_add]\() to add elements to the pipeline.
  The function accepts any number of Gst Elements as its arguments
{{ END_LANG.md }}
These elements, however, are not linked with each other yet. For this,
we need to use [gst_element_link]\(). Its first parameter is the source,
and the second one the destination. The order counts, because links must
be established following the data flow (this is, from source elements to
sink elements). Keep in mind that only elements residing in the same bin
can be linked together, so remember to add them to the pipeline before
trying to link them!

### Properties

GStreamer elements are all a particular kind of [GObject], which is the
entity offering **property** facilities.

Most GStreamer elements have customizable properties: named attributes
that can be modified to change the element's behavior (writable
properties) or inquired to find out about the element's internal state
(readable properties).

{{ C+JS_FALLBACK.md }}
  Properties are read from with [g_object_get]\() and written to
  with [g_object_set]\().

  [g_object_set]\() accepts a [NULL]-terminated list of property-name,
  property-value pairs, so multiple properties can be changed in one go.

  This is why the property handling methods have the `g_` prefix.
{{ END_LANG.md }}

{{ PY.md }}
  For understanding how to get and set [properties](https://pygobject.readthedocs.io/en/latest/guide/api/properties.html),
  let us assume we have a Gst Element `source` with a property `pattern`

  The current state of a property can be fetched by either:
  1. Accessing the property as an attribute of the `props` attribute of an
  element. Ex: `_ = source.props.pattern` to print it on the screen
  2. Using the `get_property` method of the element.
  Ex: `_ = source.get_property("pattern")`

  And properties can be set by one of three methods:
  1. Setting the property as an attribute of the `props` attribute.
  Ex: `source.props.pattern = 1` or equivalently `source.props.pattern="snow"`
  2. Using the `set_property` method of the element.
  Ex: `source.set_property("pattern", 1)` or equivalently `source.set_property("pattern", "snow")`
  3. Using the `Gst.util_set_object_arg()` method. This mode also allows you to
  pass Gst Caps and other structures. Ex: `Gst.util_set_object_arg(source, "pattern", "snow")`,
  or equivalently, `Gst.util_set_object_arg(source, "pattern", 1)`

  Note: In all three methods of setting a property, if a string is passed as
  the value to set, it has to be the serialized version of a flag or value
  (using [gst_value_serialize]\())
{{ END_LANG.md }}

Coming back to what's in the example above,
{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[37:39] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[36:40] }}
{{ END_LANG.md }}


The line of code above changes the “pattern” property of [videotestsrc],
which controls the type of test video the element outputs. Try different
values!

The names and possible values of all the properties an element exposes
can be found using the gst-inspect-1.0 tool described in [Basic tutorial 10:
GStreamer tools] or alternatively in the docs for that element
([here](GstVideoTestSrcPattern) in the case of videotestsrc).

### Error checking

At this point, we have the whole pipeline built and setup, and the rest
of the tutorial is very similar to the previous one, but we are going to
add more error checking:

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[40:47] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[41:46] }}
{{ END_LANG.md }}

We call [gst_element_set_state]\(), but this time we check its return
value for errors. Changing states is a delicate process and a few more
details are given in [Basic tutorial 3: Dynamic
pipelines].

{{ C+JS_FALLBACK.md }}
  {{ tutorials/basic-tutorial-2.c[48:79] }}
{{ END_LANG.md }}

{{ PY.md }}
  {{ tutorials/python/basic-tutorial-2.py[47:62] }}
{{ END_LANG.md }}

[gst_bus_timed_pop_filtered]\() waits for execution to end and returns
with a [GstMessage] which we previously ignored. We
asked [gst_bus_timed_pop_filtered]\() to return when GStreamer
encountered either an error condition or an [EOS], so we need to check
which one happened, and print a message on screen (Your application will
probably want to undertake more complex actions).

[GstMessage] is a very versatile structure which can deliver virtually
any kind of information. Fortunately, GStreamer provides a series of
parsing functions for each kind of message.

In this case, once we know the message contains an error (by using the
[GST_MESSAGE_TYPE]\() macro), we can use
[gst_message_parse_error]\() which returns a GLib [GError] error
structure and a string useful for debugging. Examine the code to see how
these are used and freed afterward.

### The GStreamer bus

At this point it is worth introducing the GStreamer bus a bit more
formally. It is the object responsible for delivering to the application
the [GstMessage]s generated by the elements, in order and to the
application thread. This last point is important, because the actual
streaming of media is done in another thread than the application.

Messages can be extracted from the bus synchronously with
[gst_bus_timed_pop_filtered]\() and its siblings, or asynchronously,
using signals (shown in the next tutorial). Your application should
always keep an eye on the bus to be notified of errors and other
playback-related issues.

The rest of the code is the cleanup sequence, which is the same as
in [Basic tutorial 1: Hello
world!].

## Exercise

If you feel like practicing, try this exercise: Add a video filter
element in between the source and the sink of this pipeline. Use
[vertigotv] for a nice effect. You will need to create it, add it to the
pipeline, and link it with the other elements.

Depending on your platform and available plugins, you might get a
“negotiation” error, because the sink does not understand what the
filter is producing (more about negotiation in [Basic tutorial 6: Media
formats and Pad
Capabilities]).
In this case, try to add an element called [videoconvert] after the
filter (this is, build a pipeline of 4 elements. More on
[videoconvert] in [Basic tutorial 14: Handy
elements]).

## Conclusion

This tutorial showed:

  - How to create elements with [gst_element_factory_make]\()

  - How to create an empty pipeline with [gst_pipeline_new]\()

  - How to add elements to the pipeline with [gst_bin_add_many]\()

  - How to link the elements with each other with [gst_element_link]\()

This concludes the first of the two tutorials devoted to basic GStreamer
concepts. The second one comes next.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.

It has been a pleasure having you here, and see you soon!

  [Linux]: installing/on-linux.md#InstallingonLinux-Build
  [Mac OS X]: installing/on-mac-osx.md#InstallingonMacOSX-Build
  [Windows]: installing/on-windows.md#InstallingonWindows-Build
  [1]: installing/on-linux.md#InstallingonLinux-Run
  [2]: installing/on-mac-osx.md#InstallingonMacOSX-Run
  [3]: installing/on-windows.md#InstallingonWindows-Run
  [Basic tutorial 14: Handy elements]: tutorials/basic/handy-elements.md
  [Basic tutorial 10: GStreamer tools]: tutorials/basic/gstreamer-tools.md
  [Basic tutorial 10: GStreamer tools]: tutorials/basic/gstreamer-tools.md
  [Basic tutorial 3: Dynamic pipelines]: tutorials/basic/dynamic-pipelines.md
  [Basic tutorial 1: Hello world!]: tutorials/basic/hello-world.md
  [Basic tutorial 6: Media formats and Pad Capabilities]: tutorials/basic/media-formats-and-pad-capabilities.md
  [gst_element_factory_make]: gst_element_factory_make
  [videotestsrc]: videotestsrc
  [autovideosink]: autovideosink
  [bin]: GstBin
  [NULL]: NULL
  [gst_bin_add_many]: gst_bin_add_many
  [gst_bin_add]: gst_bin_add
  [gst_element_link]: gst_element_link
  [GObject]: GObject
  [gst_value_serialize]: gst_value_serialize
  [g_object_get]: g_object_get
  [g_object_set]: g_object_set
  [gst_element_set_state]: gst_element_set_state
  [gst_bus_timed_pop_filtered]: gst_bus_timed_pop_filtered
  [GstMessage]: GstMessage
  [EOS]: GST_MESSAGE_EOS
  [GST_MESSAGE_TYPE]: GST_MESSAGE_TYPE
  [gst_message_parse_error]: gst_message_parse_error
  [GError]: GError
  [vertigotv]: vertigotv
  [videoconvert]: videoconvert
  [gst_pipeline_new]: gst_pipeline_new
