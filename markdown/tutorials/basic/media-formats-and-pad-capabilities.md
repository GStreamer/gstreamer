#  Basic tutorial 6: Media formats and Pad Capabilities


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

Pad Capabilities are a fundamental element of GStreamer, although most
of the time they are invisible because the framework handles them
automatically. This somewhat theoretical tutorial shows:

  - What are Pad Capabilities.

  - How to retrieve them.

  - When to retrieve them.

  - Why you need to know about them.

## Introduction

### Pads

As it has already been shown, Pads allow information to enter and leave
an element. The *Capabilities* (or *Caps*, for short) of a Pad, then,
specify what kind of information can travel through the Pad. For
example, “RGB video with a resolution of 320x200 pixels and 30 frames
per second”, or “16-bits per sample audio, 5.1 channels at 44100 samples
per second”, or even compressed formats like mp3 or h264.

Pads can support multiple Capabilities (for example, a video sink can
support video in different types of RGB or YUV formats) and Capabilities can be
specified as *ranges* (for example, an audio sink can support samples
rates from 1 to 48000 samples per second). However, the actual
information traveling from Pad to Pad must have only one well-specified
type. Through a process known as *negotiation*, two linked Pads agree on
a common type, and thus the Capabilities of the Pads become *fixed*
(they only have one type and do not contain ranges). The walkthrough of
the sample code below should make all this clear.

**In order for two elements to be linked together, they must share a
common subset of Capabilities** (Otherwise they could not possibly
understand each other). This is the main goal of Capabilities.

As an application developer, you will usually build pipelines by linking
elements together (to a lesser extent if you use all-in-all elements
like `playbin`). In this case, you need to know the *Pad Caps* (as they
are familiarly referred to) of your elements, or, at least, know what
they are when GStreamer refuses to link two elements with a negotiation
error.

### Pad templates

Pads are created from *Pad Templates*, which indicate all possible
Capabilities a Pad could ever have. Templates are useful to create several
similar Pads, and also allow early refusal of connections between
elements: If the Capabilities of their Pad Templates do not have a
common subset (their *intersection* is empty), there is no need to
negotiate further.

Pad Templates can be viewed as the first step in the negotiation
process. As the process evolves, actual Pads are instantiated and their
Capabilities refined until they are fixed (or negotiation fails).

### Capabilities examples

```
SINK template: 'sink'
  Availability: Always
  Capabilities:
    audio/x-raw
               format: S16LE
                 rate: [ 1, 2147483647 ]
             channels: [ 1, 2 ]
    audio/x-raw
               format: U8
                 rate: [ 1, 2147483647 ]
             channels: [ 1, 2 ]
```

This pad is a sink which is always available on the element (we will not
talk about availability for now). It supports two kinds of media, both
raw audio in integer format (`audio/x-raw`): signed, 16-bit little endian and
unsigned 8-bit. The square brackets indicate a range: for instance, the
number of channels varies from 1 to 2.

```
SRC template: 'src'
  Availability: Always
  Capabilities:
    video/x-raw
                width: [ 1, 2147483647 ]
               height: [ 1, 2147483647 ]
            framerate: [ 0/1, 2147483647/1 ]
               format: { I420, NV12, NV21, YV12, YUY2, Y42B, Y444, YUV9, YVU9, Y41B, Y800, Y8, GREY, Y16 , UYVY, YVYU, IYU1, v308, AYUV, A420 }
```

`video/x-raw` indicates that this source pad outputs raw video. It
supports a wide range of dimensions and framerates, and a set of YUV
formats (The curly braces indicate a *list*). All these formats
indicate different packing and subsampling of the image planes.

### Last remarks

You can use the `gst-inspect-1.0` tool described in [Basic tutorial 10:
GStreamer tools](tutorials/basic/gstreamer-tools.md) to
learn about the Caps of any GStreamer element.

Bear in mind that some elements query the underlying hardware for
supported formats and offer their Pad Caps accordingly (They usually do
this when entering the READY state or higher). Therefore, the shown caps
can vary from platform to platform, or even from one execution to the
next (even though this case is rare).

This tutorial instantiates two elements (this time, through their
factories), shows their Pad Templates, links them and sets the pipeline
to play. On each state change, the Capabilities of the sink element's
Pad are shown, so you can observe how the negotiation proceeds until the
Pad Caps are fixed.

## A trivial Pad Capabilities Example

Copy this code into a text file named `basic-tutorial-6.c` (or find it
in your GStreamer installation).

**basic-tutorial-6.c**

``` c
#include <gst/gst.h>

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information (GstElementFactory * factory) {
  const GList *pads;
  GstStaticPadTemplate *padtemplate;

  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));
  if (!gst_element_factory_get_num_pad_templates (factory)) {
    g_print ("  none\n");
    return;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = pads->data;
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST)
      g_print ("    Availability: On request\n");
    else
      g_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->static_caps.string) {
      GstCaps *caps;
      g_print ("    Capabilities:\n");
      caps = gst_static_caps_get (&padtemplate->static_caps);
      print_caps (caps, "      ");
      gst_caps_unref (caps);

    }

    g_print ("\n");
  }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

int main(int argc, char *argv[]) {
  GstElement *pipeline, *source, *sink;
  GstElementFactory *source_factory, *sink_factory;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the element factories */
  source_factory = gst_element_factory_find ("audiotestsrc");
  sink_factory = gst_element_factory_find ("autoaudiosink");
  if (!source_factory || !sink_factory) {
    g_printerr ("Not all element factories could be created.\n");
    return -1;
  }

  /* Print information about the pad templates of these factories */
  print_pad_templates_information (source_factory);
  print_pad_templates_information (sink_factory);

  /* Ask the factories to instantiate actual elements */
  source = gst_element_factory_create (source_factory, "source");
  sink = gst_element_factory_create (sink_factory, "sink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Print initial negotiated caps (in NULL state) */
  g_print ("In NULL state:\n");
  print_pad_capabilities (sink, "sink");

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state (check the bus for error messages).\n");
  }

  /* Wait until error, EOS or State Change */
  bus = gst_element_get_bus (pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
        GST_MESSAGE_STATE_CHANGED);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("\nPipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
            /* Print the current capabilities of the sink element */
            print_pad_capabilities (sink, "sink");
          }
          break;
        default:
          /* We should not reach here because we only asked for ERRORs, EOS and STATE_CHANGED */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (source_factory);
  gst_object_unref (sink_factory);
  return 0;
}
```

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> `` gcc basic-tutorial-6.c -o basic-tutorial-6 `pkg-config --cflags --libs gstreamer-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial simply displays information regarding the Pad Capabilities in different time instants.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

The `print_field`, `print_caps` and `print_pad_templates` simply
display, in a human-friendly format, the capabilities structures. If you
want to learn about the internal organization of the
`GstCaps` structure, read  the `GStreamer Documentation` regarding Pad
Caps.

``` c
/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}
```

`gst_element_get_static_pad()` retrieves the named Pad from the given
element. This Pad is *static* because it is always present in the
element. To know more about Pad availability read the `GStreamer
documentation` about Pads.

Then we call `gst_pad_get_current_caps()` to retrieve the Pad's
current Capabilities, which can be fixed or not, depending on the state
of the negotiation process. They could even be non-existent, in which
case, we call `gst_pad_query_caps()` to retrieve the currently
acceptable Pad Capabilities. The currently acceptable Caps will be the
Pad Template's Caps in the NULL state, but might change in later states,
as the actual hardware Capabilities might be queried.

We then print these Capabilities.

``` c
/* Create the element factories */
source_factory = gst_element_factory_find ("audiotestsrc");
sink_factory = gst_element_factory_find ("autoaudiosink");
if (!source_factory || !sink_factory) {
  g_printerr ("Not all element factories could be created.\n");
  return -1;
}

/* Print information about the pad templates of these factories */
print_pad_templates_information (source_factory);
print_pad_templates_information (sink_factory);

/* Ask the factories to instantiate actual elements */
source = gst_element_factory_create (source_factory, "source");
sink = gst_element_factory_create (sink_factory, "sink");
```

In the previous tutorials we created the elements directly using
`gst_element_factory_make()` and skipped talking about factories, but we
will do now. A `GstElementFactory` is in charge of instantiating a
particular type of element, identified by its factory name.

You can use `gst_element_factory_find()` to create a factory of type
“videotestsrc”, and then use it to instantiate multiple “videotestsrc”
elements using `gst_element_factory_create()`.
`gst_element_factory_make()` is really a shortcut for
`gst_element_factory_find()`+ `gst_element_factory_create()`.

The Pad Templates can already be accessed through the factories, so they
are printed as soon as the factories are created.

We skip the pipeline creation and start, and go to the State-Changed
message handling:

``` c
case GST_MESSAGE_STATE_CHANGED:
  /* We are only interested in state-changed messages from the pipeline */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    g_print ("\nPipeline state changed from %s to %s:\n",
        gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
    /* Print the current capabilities of the sink element */
    print_pad_capabilities (sink, "sink");
  }
  break;
```

This simply prints the current Pad Caps every time the state of the
pipeline changes. You should see, in the output, how the initial caps
(the Pad Template's Caps) are progressively refined until they are
completely fixed (they contain a single type with no ranges).

## Conclusion

This tutorial has shown:

  - What are Pad Capabilities and Pad Template Capabilities.

  - How to retrieve them
    with `gst_pad_get_current_caps()` or `gst_pad_query_caps()`.

  - That they have different meaning depending on the state of the
    pipeline (initially they indicate all the possible Capabilities,
    later they indicate the currently negotiated Caps for the Pad).

  - That Pad Caps are important to know beforehand if two elements can
    be linked together.

  - That Pad Caps can be found using the `gst-inspect-1.0` tool described
    in [Basic tutorial 10: GStreamer
    tools](tutorials/basic/gstreamer-tools.md).

Next tutorial shows how data can be manually injected into and extracted
from the GStreamer pipeline.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.
It has been a pleasure having you here, and see you soon!
