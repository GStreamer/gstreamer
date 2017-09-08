---
title: Interfaces
...

# Interfaces

In [Using an element as a GObject][element-object], you have
learned how to use `GObject` properties as a simple way to do
interaction between applications and elements. This method suffices for
the simple'n'straight settings, but fails for anything more complicated
than a getter and setter. For the more complicated use cases, GStreamer
uses interfaces based on the GObject
[`GTypeInterface`](http://library.gnome.org/devel/gobject/stable/gtype-non-instantiable-classed.html)
type.

Most of the interfaces handled here will not contain any example code.
See the API references for details. Here, we will just describe the
scope and purpose of each interface.

[element-object]: application-development/basics/elements.md#using-an-element-as-a-gobject

## The URI Handler interface

In the examples so far, we have only showed support for local files
using the “filesrc” element, but GStreamer supports many more location
sources.

GStreamer doesn't require applications to know any `URI` specifics, like
what element to use for a particular network source types. These details
are abstracted by the `GstURIHandler` interface.

There is no strict rule for `URI` naming, but in general, we follow
common-usage naming conventions. For example, assuming you have the
correct plugins installed, GStreamer supports:

```
file:///<path>/<file>
http://<host>/<path>/<file>
mms://<host>/<path>/<file>
dvb://<CHANNEL>
...
```

In order to get the source or sink element supporting a particular URI,
use `gst_element_make_from_uri ()` with `GST_URI_SRC` or `GST_URI_SINK`
as `GstURIType` depending in the direction you need.

You can convert filenames to and from URIs using GLib's
`g_filename_to_uri ()` and `g_uri_to_filename ()`.

## The Color Balance interface

The `GstColorBalance` interface is a way to control video-related properties
on an element, such as brightness, contrast and so on. It's sole reason
for existence is that, as far as its authors know, there's no way to
dynamically register properties using `GObject`.

The colorbalance interface is implemented by several plugins, including
`xvimagesink`, `glimagesink` and the `Video4linux2` elements.

## The Video Overlay interface

The `GstVideoOverlay` interface was created to solve the problem of
embedding video streams in an application window. The application
provides a window handle to an element implementing this interface,
and the element will then use this window handle to draw on
rather than creating a new toplevel window. This is useful to embed
video in video players.

This interface is implemented by, amongst others, the `Video4linux2`
elements and by `glimagesink`, `ximagesink`, `xvimagesink` and `sdlvideosink`.
