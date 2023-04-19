---
title: Things to check when writing an application
...

# Things to check when writing an application

This chapter contains a fairly random selection of things that can be
useful to keep in mind when writing GStreamer-based applications. It's
up to you how much you're going to use the information provided here. We
will shortly discuss how to debug pipeline problems using GStreamer
applications. Also, we will touch upon how to acquire knowledge about
plugins and elements and how to test simple pipelines before building
applications around them.

## Good programming habits

  - Always add a `GstBus` handler to your pipeline. Always report errors
    in your application, and try to do something with warnings and
    information messages, too.

  - Always check return values of GStreamer functions. Especially, check
    return values of `gst_element_link ()` and `gst_element_set_state
    ()`.

  - Dereference return values of all functions returning a non-base
    type, such as `gst_element_get_pad ()`. Also, always free non-const
    string returns, such as `gst_object_get_name ()`.

  - Always use your pipeline object to keep track of the current state
    of your pipeline. Don't keep private variables in your application.
    Also, don't update your user interface if a user presses the “play”
    button. Instead, listen for the “state-changed” message on the
    `GstBus` and only update the user interface whenever this message is
    received.

  - Report all bugs that you find to Gitlab at
    [https://gitlab.freedesktop.org/gstreamer/](https://gitlab.freedesktop.org/gstreamer).

## Debugging

Applications can make use of the extensive GStreamer debugging system to
debug pipeline problems. Elements will write output to this system to
log what they're doing. It's not used for error reporting, but it is
very useful for tracking what an element is doing exactly, which can
come in handy when debugging application issues (such as failing seeks,
out-of-sync media, etc.).

Most GStreamer-based applications accept the commandline option
`--gst-debug=LIST` and related family members. The list consists of a
comma-separated list of category/level pairs, which can set the
debugging level for a specific debugging category. For example,
`--gst-debug=oggdemux:5` would turn on debugging for the Ogg demuxer
element. You can use wildcards as well. A debugging level of 0 will turn
off all debugging, and a level of 9 will turn on all debugging.
Intermediate values only turn on some debugging (based on message
severity; 2, for example, will only display errors and warnings). Here's
a list of all available options:

  - `--gst-debug-help` will print available debug categories and exit.

  - `--gst-debug-level=LEVEL` will set the default debug level (which
    can range from 0 (no output) to 9 (everything)).

  - `--gst-debug=LIST` takes a comma-separated list of
    category\_name:level pairs to set specific levels for the individual
    categories. Example: `GST_AUTOPLUG:5,avidemux:3`. Alternatively, you
    can also set the `GST_DEBUG` environment variable, which has the
    same effect.

  - `--gst-debug-no-color` will disable color debugging. You can also
    set the GST\_DEBUG\_NO\_COLOR environment variable to 1 if you want
    to disable colored debug output permanently. Note that if you are
    disabling color purely to avoid messing up your pager output, try
    using `less -R`.

  - `--gst-debug-color-mode=MODE` will change debug log coloring mode.
    MODE can be one of the following: `on`, `off`, `auto`, `disable`,
    `unix`. You can also set the GST\_DEBUG\_COLOR\_MODE environment
    variable if you want to change colored debug output permanently.
    Note that if you are disabling color purely to avoid messing up your
    pager output, try using `less -R`.

  - `--gst-debug-disable` disables debugging altogether.

## Conversion plugins

GStreamer contains a bunch of conversion plugins that most applications
will find useful. Specifically, those are videoscalers (videoscale),
colorspace convertors (videoconvert), audio format convertors and
channel resamplers (audioconvert) and audio samplerate convertors
(audioresample). Those convertors don't do anything when not required,
they will act in passthrough mode. They will activate when the hardware
doesn't support a specific request, though. All applications are
recommended to use those elements.

## Utility applications provided with GStreamer

GStreamer comes with a default set of command-line utilities that can
help in application development. We will discuss only `gst-launch` and
`gst-inspect` here.

### `gst-launch`

`gst-launch` is a simple script-like commandline application that can be
used to test pipelines. For example, the command `gst-launch
audiotestsrc ! audioconvert !
audio/x-raw,channels=2 ! alsasink` will run a pipeline which generates a
sine-wave audio stream and plays it to your ALSA audio card.
`gst-launch` also allows the use of threads (will be used automatically
as required or as queue elements are inserted in the pipeline) and bins
(using brackets, so “(” and “)”). You can use dots to imply padnames on
elements, or even omit the padname to automatically select a pad. Using
all this, the pipeline `gst-launch filesrc location=file.ogg ! oggdemux
name=d
d. ! queue ! theoradec ! videoconvert ! xvimagesink
d. ! queue ! vorbisdec ! audioconvert ! audioresample ! alsasink
` will play an Ogg file containing a Theora video-stream and a Vorbis
audio-stream. You can also use autopluggers such as decodebin on the
commandline. See the manual page of `gst-launch` for more information.

### `gst-inspect`

`gst-inspect` can be used to inspect all properties, signals, dynamic
parameters and the object hierarchy of an element. This can be very
useful to see which `GObject` properties or which signals (and using
what arguments) an element supports. Run `gst-inspect fakesrc` to get an
idea of what it does. See the manual page of `gst-inspect` for more
information.
