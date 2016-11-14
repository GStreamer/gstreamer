---
title: Buffers and Events
...

# Buffers and Events

The data flowing through a pipeline consists of a combination of buffers
and events. Buffers contain the actual media data. Events contain
control information, such as seeking information and end-of-stream
notifiers. All this will flow through the pipeline automatically when
it's running. This chapter is mostly meant to explain the concept to
you; you don't need to do anything for this.

## Buffers

Buffers contain the data that will flow through the pipeline you have
created. A source element will typically create a new buffer and pass it
through a pad to the next element in the chain. When using the GStreamer
infrastructure to create a media pipeline you will not have to deal with
buffers yourself; the elements will do that for you.

A buffer consists, amongst others, of:

  - Pointers to memory objects. Memory objects encapsulate a region in
    the memory.

  - A timestamp for the buffer.

  - A refcount that indicates how many elements are using this buffer.
    This refcount will be used to destroy the buffer when no element has
    a reference to it.

  - Buffer flags.

The simple case is that a buffer is created, memory allocated, data put
in it, and passed to the next element. That element reads the data, does
something (like creating a new buffer and decoding into it), and
unreferences the buffer. This causes the data to be free'ed and the
buffer to be destroyed. A typical video or audio decoder works like
this.

There are more complex scenarios, though. Elements can modify buffers
in-place, i.e. without allocating a new one. Elements can also write to
hardware memory (such as from video-capture sources) or memory allocated
from the X-server (using XShm). Buffers can be read-only, and so on.

## Events

Events are control particles that are sent both up- and downstream in a
pipeline along with buffers. Downstream events notify fellow elements of
stream states. Possible events include seeking, flushes, end-of-stream
notifications and so on. Upstream events are used both in
application-element interaction as well as element-element interaction
to request changes in stream state, such as seeks. For applications,
only upstream events are important. Downstream events are just explained
to get a more complete picture of the data concept.

Since most applications seek in time units, our example below does so
too:

``` c
static void
seek_to_time (GstElement *element,
          guint64     time_ns)
{
  GstEvent *event;

  event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
                  GST_SEEK_FLAG_NONE,
                  GST_SEEK_METHOD_SET, time_ns,
                  GST_SEEK_TYPE_NONE, G_GUINT64_CONSTANT (0));
  gst_element_send_event (element, event);
}

```

The function `gst_element_seek ()` is a shortcut for this. This is
mostly just to show how it all works.
