---
title: Interfaces
...

# Interfaces

[Using an element as a GObject][element-object] presents the use of `GObject`
properties as a simple way for applications and elements to interact. This
method suffices for simple getters and setters, but fails for anything more
complicated. For more complex use cases, GStreamer uses interfaces based on the
`GObject`
[`GTypeInterface`](http://library.gnome.org/devel/gobject/stable/gtype-non-instantiatable-classed.html)
type.

This text is meant to be introductory and does not include source code examples.
Please take a look at the API reference for additional details.

[element-object]: application-development/basics/elements.md#using-an-element-as-a-gobject

## The URI Handler interface

In our examples so far, we have only showed support for local files
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
rtsp://<host>/<path>
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
on an element, such as brightness, contrast and so on. Its sole reason
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

## Other interfaces

There are quite a few other interfaces provided by GStreamer and implemented by
some of its elements. Among them:

* `GstChildProxy` For access to internal element's properties on multi-child elements
* `GstNavigation` For the sending and parsing of navigation events
* `GstPreset` For handling element presets
* `GstRTSPExtension` An RTSP Extension interface
* `GstStreamVolume` Interface to provide access and control stream volume levels
* `GstTagSetter` For handling media metadata
* `GstTagXmpWriter` For elements that provide XMP serialization
* `GstTocSetter` For setting and retrieval of TOC-like data
* `GstTuner` For elements providing RF tunning operations
* `GstVideoDirection` For video rotation and flipping controls
* `GstVideoOrientation` For video orientation controls
* `GstWaylandVideo` Wayland video interface

