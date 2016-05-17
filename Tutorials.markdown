# Tutorials 

## Welcome to the GStreamer SDK Tutorials!

The following sections introduce a series of tutorials designed to help
you learn how to use GStreamer, the multi-platform, modular,
open-source, media streaming framework.

### Prerequisites

Before following these tutorials, you need to set up your development
environment according to your platform. If you have not done so yet,
follow the appropriate link for [Linux](Installing+on+Linux.markdown),
[Mac OS X](Installing+on+Mac+OS+X.markdown) or
[Windows](Installing+on+Windows.markdown) and come back here
afterwards.

The tutorials are currently written only in the C programming language,
so you need to be comfortable with it. Even though C is not an
Object-Oriented (OO) language per se, the GStreamer framework uses
`GObject`s, so some knowledge of OO concepts will come in handy.
Knowledge of the `GObject` and `GLib` libraries is not mandatory, but
will make the trip easier.

### Source code

Every tutorial represents a self-contained project, with full source
code in C (and eventually in other languages too). Source code snippets
are introduced alongside the text, and the full code (with any other
required files like makefiles or project files) is distributed with the
SDK, as explained in the installation instructions.

### A short note on GObject and GLib

GStreamer is built on top of the `GObject` (for object orientation) and
`GLib` (for common algorithms) libraries, which means that every now and
then you will have to call functions of these libraries. Even though the
tutorials will make sure that deep knowledge of these libraries is not
required, familiarity with them will certainly ease the process of
learning GStreamer.

You can always tell which library you are calling because all GStreamer
functions, structures and types have the `gst_` prefix, whereas GLib and
GObject use `g_`.

### Sources of documentation

You have the `GObject` and `GLib` reference guides, and, of course the
upstream [GStreamer
documentation](http://gstreamer.freedesktop.org/documentation/).

### Structure

The tutorials are organized in sections, revolving about a common theme:

  - [Basic tutorials](Basic+tutorials.markdown): Describe general topics
    required to understand the rest of tutorials in the GStreamer SDK.
  - [Playback tutorials](Playback+tutorials.markdown): Explain everything
    you need to know to produce a media playback application using
    GStreamer.
  - [Android tutorials](Android+tutorials.markdown): Tutorials dealing
    with the few Android-specific topics you need to know.
  - [iOS tutorials](iOS+tutorials.markdown): Tutorials dealing with the
    few iOS-specific topics you need to know.

If you cannot remember in which tutorial a certain GStreamer concept is
explained, use the following:

  - [Table of Concepts](Table+of+Concepts.markdown)

### Sample media

The audio and video clips used throughout these tutorials are all
publicly available and the copyright remains with their respective
authors. In some cases they have been re-encoded for demonstration
purposes.

  - [Sintel, the Durian Open Movie Project](http://www.sintel.org/)