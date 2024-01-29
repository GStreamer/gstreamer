---
title: Constructing the Boilerplate
...

# Constructing the Boilerplate

In this chapter you will learn how to construct the bare minimum code
for a new plugin. Starting from ground zero, you will see how to get the
GStreamer template source. Then you will learn how to use a few basic
tools to copy and modify a template plugin to create a new plugin. If
you follow the examples here, then by the end of this chapter you will
have a functional audio filter plugin that you can compile and use in
GStreamer applications.

## Getting the GStreamer Plugin Templates

There are currently two ways to develop a new plugin for GStreamer: You
can write the entire plugin by hand, or you can copy an existing plugin
template and write the plugin code you need. The second method is by far
the simpler of the two, so the first method will not even be described
here. (Errm, that is, “it is left as an exercise to the reader.”)

The first step is to check out a copy of the `gst-template` git module
to get an important tool and the source code template for a basic
GStreamer plugin. To check out the `gst-template` module, make sure you
are connected to the internet, and type the following commands at a
command
console:

```
shell $ git clone https://gitlab.freedesktop.org/gstreamer/gst-template.git
Initialized empty Git repository in /some/path/gst-template/.git/
remote: Counting objects: 373, done.
remote: Compressing objects: 100% (114/114), done.
remote: Total 373 (delta 240), reused 373 (delta 240)
Receiving objects: 100% (373/373), 75.16 KiB | 78 KiB/s, done.
Resolving deltas: 100% (240/240), done.

```

This command will check out a series of files and directories into
`gst-template`. The template you will be using is in the
`gst-template/gst-plugin/` directory. You should look over the files in
that directory to get a general idea of the structure of a source tree
for a plugin.

If for some reason you can't access the git repository, you can also
[download a snapshot of the latest
revision](https://gitlab.freedesktop.org/gstreamer/gst-template)
via the gitlab web interface.

## Using the Project Stamp

The first thing to do when making a new element is to specify some basic
details about it: what its name is, who wrote it, what version number it
is, etc. We also need to define an object to represent the element and
to store the data the element needs. These details are collectively
known as the *boilerplate*.

The standard way of defining the boilerplate is simply to write some
code, and fill in some structures. As mentioned in the previous section,
the easiest way to do this is to copy a template and add functionality
according to your needs. To help you do so, there is a tool in the
`./gst-plugin/tools/` directory. This tool, `make_element`, is a command
line utility that creates the boilerplate code for you.

To use `make_element`, first open up a terminal window. Change to the
`gst-template/gst-plugin/src` directory, and then run the `make_element`
command. The arguments to the `make_element` are:

1.  the name of the plugin, and

2.  the source file that the tool will use. By default, `gstplugin` is
    used.

For example, the following commands create the MyFilter plugin based on
the plugin template and put the output files in the
`gst-template/gst-plugin/src` directory:

```
shell $ cd gst-template/gst-plugin/src
shell $ ../tools/make_element MyFilter

```

> **Note**
>
> Capitalization is important for the name of the plugin. Keep in mind
> that under some operating systems, capitalization is also important
> when specifying directory and file names in general.

The last command creates two files: `gstmyfilter.c` and `gstmyfilter.h`.

> **Note**
>
> It is recommended that you create a copy of the `gst-plugin` directory
> before continuing.

Now one needs to run `meson setup build` from the parent directory to bootstrap the
build environment. After that, the project can be built and installed using the
well known `ninja -C build` commands.

> **Note**
>
> Be aware that by default `meson` will choose `/usr/local` as a default
> location. One would need to add `/usr/local/lib/gstreamer-1.0` to
> `GST_PLUGIN_PATH` in order to make the new plugin show up in a gstreamer
> that's been installed from packages.

> **Note**
>
> FIXME: this section is slightly outdated. gst-template is still useful
> as an example for a minimal plugin build system skeleton. However, for
> creating elements the tool gst-element-maker from gst-plugins-bad is
> recommended these days.

## Examining the Basic Code

First we will examine the code you would be likely to place in a header
file (although since the interface to the code is entirely defined by
the plugin system, and doesn't depend on reading a header file, this is
not crucial.)

``` c
#include <gst/gst.h>

/* Definition of structure storing data for this element. */
typedef struct _GstMyFilter {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;



} GstMyFilter;

/* Standard definition defining a class for this element. */
typedef struct _GstMyFilterClass {
  GstElementClass parent_class;
} GstMyFilterClass;

/* Standard macros for defining types for this element.  */
#define GST_TYPE_MY_FILTER (gst_my_filter_get_type())
#define GST_MY_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MY_FILTER,GstMyFilter))
#define GST_MY_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MY_FILTER,GstMyFilterClass))
#define GST_IS_MY_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MY_FILTER))
#define GST_IS_MY_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MY_FILTER))

/* Standard function returning type information. */
GType gst_my_filter_get_type (void);

GST_ELEMENT_REGISTER_DECLARE(my_filter)

```

Using this header file, you can use the following macros to setup the
`Element` basics in your source file so that all functions will be
called appropriately:

``` c
#include "filter.h"

G_DEFINE_TYPE (GstMyFilter, gst_my_filter, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE(my_filter, "my-filter", GST_RANK_NONE, GST_TYPE_MY_FILTER);

```

The macro `GST_ELEMENT_REGISTER_DEFINE` in combination with `GST_ELEMENT_REGISTER_DECLARE`
allows to register the element from within the plugin or from any other plugin/application by calling
`GST_ELEMENT_REGISTER (my_filter)`.

## Element metadata

The Element metadata provides extra element information. It is
configured with `gst_element_class_set_metadata` or
`gst_element_class_set_static_metadata` which takes the following
parameters:

  - A long, English, name for the element.

  - The type of the element, see the docs/additional/design/draft-klass.txt
    document in the GStreamer core source tree for details and examples.

  - A brief description of the purpose of the element.

  - The name of the author of the element, optionally followed by a
    contact email address in angle brackets.

For example:

``` c
gst_element_class_set_static_metadata (klass,
  "An example plugin",
  "Example/FirstExample",
  "Shows the basic structure of a plugin",
  "your name <your.name@your.isp>");

```

The element details are registered with the plugin during the
`_class_init ()` function, which is part of the GObject system. The
`_class_init ()` function should be set for this GObject in the function
where you register the type with GLib.

``` c
static void
gst_my_filter_class_init (GstMyFilterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

[..]
  gst_element_class_set_static_metadata (element_class,
    "An example plugin",
    "Example/FirstExample",
    "Shows the basic structure of a plugin",
    "your name <your.name@your.isp>");

}

```

## GstStaticPadTemplate

A GstStaticPadTemplate is a description of a pad that the element will
(or might) create and use. It contains:

  - A short name for the pad.

  - Pad direction.

  - Existence property. This indicates whether the pad exists always (an
    “always” pad), only in some cases (a “sometimes” pad) or only if the
    application requested such a pad (a “request” pad).

  - Supported types by this element (capabilities).

For example:

``` c
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("ANY")
);



```

Those pad templates are registered during the `_class_init ()` function
with the `gst_element_class_add_pad_template ()`. For this function you
need a handle to the `GstPadTemplate` which you can create from the static
pad template with `gst_static_pad_template_get ()`. See below for more
details on this.

Pads are created from these static templates in the element's `_init ()`
function using `gst_pad_new_from_static_template ()`. In order to create
a new pad from this template using `gst_pad_new_from_static_template
()`, you will need to declare the pad template as a global variable. More on
this subject in [Specifying the pads][pads].

```c
static GstStaticPadTemplate sink_factory = [..],
    src_factory = [..];

static void
gst_my_filter_class_init (GstMyFilterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
[..]

  gst_element_class_add_pad_template (element_class,
    gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
    gst_static_pad_template_get (&sink_factory));
}
```

The last argument in a template is its type or list of supported types.
In this example, we use 'ANY', which means that this element will accept
all input. In real-life situations, you would set a media type and
optionally a set of properties to make sure that only supported input
will come in. This representation should be a string that starts with a
media type, then a set of comma-separates properties with their
supported values. In case of an audio filter that supports raw integer
16-bit audio, mono or stereo at any samplerate, the correct template
would look like this:

``` c

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw, "
      "format = (string) " GST_AUDIO_NE (S16) ", "
      "channels = (int) { 1, 2 }, "
      "rate = (int) [ 8000, 96000 ]"
  )
);


```

Values surrounded by curly brackets (“{” and “}”) are lists, values
surrounded by square brackets (“\[” and “\]”) are ranges. Multiple sets
of types are supported too, and should be separated by a semicolon
(“;”). Later, in the chapter on pads, we will see how to use types
to know the exact format of a stream: [Specifying the pads][pads].

[pads]: plugin-development/basics/pads.md

## Constructor Functions

Each element has two functions which are used for construction of an
element. The `_class_init()` function, which is used to initialise the
class only once (specifying what signals, arguments and virtual
functions the class has and setting up global state); and the `_init()`
function, which is used to initialise a specific instance of this type.

## The plugin\_init function

Once we have written code defining all the parts of the plugin, we need
to write the plugin\_init() function. This is a special function, which
is called as soon as the plugin is loaded, and should return TRUE or
FALSE depending on whether it loaded initialized any dependencies
correctly. Also, in this function, any supported element type in the
plugin should be registered.

``` c


static gboolean
plugin_init (GstPlugin *plugin)
{
  return GST_ELEMENT_REGISTER (my_filter, plugin);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  my_filter,
  "My filter plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "GStreamer",
  "http://gstreamer.net/"
)



```

Note that the information returned by the plugin\_init() function will
be cached in a central registry. For this reason, it is important that
the same information is always returned by the function: for example, it
must not make element factories available based on runtime conditions.
If an element can only work in certain conditions (for example, if the
soundcard is not being used by some other process) this must be
reflected by the element being unable to enter the READY state if
unavailable, rather than the plugin attempting to deny existence of the
plugin.
