---
title: Design principles
...

# Design principles

## Clean and powerful

GStreamer provides a clean interface to:

  - The application programmer who wants to build a media pipeline. The
    programmer can use an extensive set of powerful tools to create
    media pipelines without writing a single line of code. Performing
    complex media manipulations becomes very easy.

  - The plugin programmer. Plugin programmers are provided a clean and
    simple API to create self-contained plugins. An extensive debugging
    and tracing mechanism has been integrated. GStreamer also comes with
    an extensive set of real-life plugins that serve as examples too.

## Object oriented

GStreamer adheres to `GObject`, the `GLib 2.0` object model. A programmer
familiar with `GLib 2.0` or `GTK+` will be comfortable with GStreamer.

GStreamer uses the mechanism of signals and object properties.

All objects can be queried at runtime for their various properties and
capabilities.

GStreamer intends to be similar in programming methodology to `GTK+`. This
applies to the object model, ownership of objects, reference counting,
etc.

## Extensible

All GStreamer Objects can be extended using the `GObject` inheritance
methods.

All plugins are loaded dynamically and can be extended and upgraded
independently.

## Allow binary-only plugins

Plugins are shared libraries that are loaded at runtime. Since all the
properties of the plugin can be set using the `GObject` properties, there
is no need (and in fact no way) to have any header files installed for
the plugins.

Special care has been taken to make plugins completely self-contained.
All relevant aspects of plugins can be queried at run-time.

## High performance

High performance is obtained by:

  - using GLib's `GSlice` allocator

  - extremely light-weight links between plugins. Data can travel the
    pipeline with minimal overhead. Data passing between plugins only
    involves a pointer dereference in a typical pipeline.

  - providing a mechanism to directly work on the target memory. A
    plugin can for example directly write to the X server's shared
    memory space. Buffers can also point to arbitrary memory, such as a
    sound card's internal hardware buffer.

  - refcounting and copy on write minimize usage of memcpy. Sub-buffers
    efficiently split buffers into manageable pieces.

  - dedicated streaming threads, with scheduling handled by the kernel.

  - allowing hardware acceleration by using specialized plugins.

  - using a plugin registry with the specifications of the plugins so
    that the plugin loading can be delayed until the plugin is actually
    used.

## Clean core/plugins separation

The core of GStreamer is essentially media-agnostic. It only knows about
bytes and blocks, and only contains basic elements. The core of
GStreamer is even functional enough to implement low-level system tools,
like cp.

All of the media handling functionality is provided by plugins external
to the core. These tell the core how to handle specific types of media.

## Provide a framework for codec experimentation

GStreamer also wants to be an easy framework where codec developers can
experiment with different algorithms, speeding up the development of
open and free multimedia codecs like those developed by the [Xiph.Org
Foundation](http://www.xiph.org) (such as Theora and Vorbis).
