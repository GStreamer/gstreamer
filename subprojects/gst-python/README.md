# gst-python

gst-python is an extension of the regular GStreamer Python bindings
based on gobject-introspection information and PyGObject.

It provides two things:

1. "syntactic sugar" in form of overrides for various GStreamer APIs
   that makes them easier to use in Python and more pythonic; and

2. support for APIs that aren't available through the regular
   gobject-introspection based bindings, such as e.g. GStreamer's
   fundamental GLib types such as `Gst.Fraction`, `Gst.IntRange` etc.

## Prerequisites

These libraries are needed to build gst-python:
 - gstreamer core
 - gst-plugins-base
 - pygobject

You will also need pygobject and glib installed. On debian-based distros
you can install these with:

    sudo apt build-dep python3-gst-1.0

Only Python 3 is supported.

## Building

    meson setup builddir && ninja -C builddir
    meson install -C builddir

## Using

Once installed in the right place, you don't need to do anything in order
to use the overrides. They will be loaded automatically on

```python
import gi
gi.require_version('Gst', '1.0')
gi.repository import GObject, Gst
```

Note that additional imports will be required for other GStreamer libraries to
make use of their respective APIs, e.g. `GstApp` or `GstVideo`.

## License

gst-python is licensed under the [LGPL 2.1](https://www.gnu.org/licenses/lgpl-2.1.html)
