---
title: What are states?
...

# What are states?

A state describes whether the element instance is initialized, whether
it is ready to transfer data and whether it is currently handling data.
There are four states defined in GStreamer:

  - `GST_STATE_NULL`

  - `GST_STATE_READY`

  - `GST_STATE_PAUSED`

  - `GST_STATE_PLAYING`

which will from now on be referred to simply as “NULL”, “READY”,
“PAUSED” and “PLAYING”.

`GST_STATE_NULL` is the default state of an element. In this state, it
has not allocated any runtime resources, it has not loaded any runtime
libraries and it can obviously not handle data.

`GST_STATE_READY` is the next state that an element can be in. In the
READY state, an element has all default resources (runtime-libraries,
runtime-memory) allocated. However, it has not yet allocated or defined
anything that is stream-specific. When going from NULL to READY state
(`GST_STATE_CHANGE_NULL_TO_READY`), an element should allocate any
non-stream-specific resources and should load runtime-loadable libraries
(if any). When going the other way around (from READY to NULL,
`GST_STATE_CHANGE_READY_TO_NULL`), an element should unload these
libraries and free all allocated resources. Examples of such resources
are hardware devices. Note that files are generally streams, and these
should thus be considered as stream-specific resources; therefore, they
should *not* be allocated in this state.

`GST_STATE_PAUSED` is the state in which an element is ready to accept
and handle data. For most elements this state is the same as PLAYING.
The only exception to this rule are sink elements. Sink elements only
accept one single buffer of data and then block. At this point the
pipeline is 'prerolled' and ready to render data immediately.

`GST_STATE_PLAYING` is the highest state that an element can be in. For
most elements this state is exactly the same as PAUSED, they accept and
process events and buffers with data. Only sink elements need to
differentiate between PAUSED and PLAYING state. In PLAYING state, sink
elements actually render incoming data, e.g. output audio to a sound
card or render video pictures to an image sink.

## Managing filter state

If at all possible, your element should derive from one of the new base
classes ([Pre-made base classes](plugin-development/element-types/base-classes.md)). There are
ready-made general purpose base classes for different types of sources,
sinks and filter/transformation elements. In addition to those,
specialised base classes exist for audio and video elements and others.

If you use a base class, you will rarely have to handle state changes
yourself. All you have to do is override the base class's start() and
stop() virtual functions (might be called differently depending on the
base class) and the base class will take care of everything for you.

If, however, you do not derive from a ready-made base class, but from
GstElement or some other class not built on top of a base class, you
will most likely have to implement your own state change function to be
notified of state changes. This is definitively necessary if your plugin
is a demuxer or a muxer, as there are no base classes for muxers or
demuxers yet.

An element can be notified of state changes through a virtual function
pointer. Inside this function, the element can initialize any sort of
specific data needed by the element, and it can optionally fail to go
from one state to another.

Do not g\_assert for unhandled state changes; this is taken care of by
the GstElement base class.

```c
static GstStateChangeReturn
gst_my_filter_change_state (GstElement *element, GstStateChange transition);

static void
gst_my_filter_class_init (GstMyFilterClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = gst_my_filter_change_state;
}



static GstStateChangeReturn
gst_my_filter_change_state (GstElement *element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstMyFilter *filter = GST_MY_FILTER (element);

  switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
	  if (!gst_my_filter_allocate_memory (filter))
		return GST_STATE_CHANGE_FAILURE;
	  break;
	default:
	  break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
	return ret;

  switch (transition) {
	case GST_STATE_CHANGE_READY_TO_NULL:
	  gst_my_filter_free_memory (filter);
	  break;
	default:
	  break;
  }

  return ret;
}
```
Note that upwards (NULL=\>READY, READY=\>PAUSED, PAUSED=\>PLAYING) and
downwards (PLAYING=\>PAUSED, PAUSED=\>READY, READY=\>NULL) state changes
are handled in two separate blocks with the downwards state change
handled only after we have chained up to the parent class's state change
function. This is necessary in order to safely handle concurrent access
by multiple threads.

The reason for this is that in the case of downwards state changes you
don't want to destroy allocated resources while your plugin's chain
function (for example) is still accessing those resources in another
thread. Whether your chain function might be running or not depends on
the state of your plugin's pads, and the state of those pads is closely
linked to the state of the element. Pad states are handled in the
GstElement class's state change function, including proper locking,
that's why it is essential to chain up before destroying allocated
resources.
