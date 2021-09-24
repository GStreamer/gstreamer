---
title: The event function
...

# The event function

The event function notifies you of special events that happen in the
datastream (such as caps, end-of-stream, newsegment, tags, etc.). Events
can travel both upstream and downstream, so you can receive them on sink
pads as well as source pads.

Below follows a very simple event function that we install on the sink
pad of our element.

``` c

static gboolean gst_my_filter_sink_event (GstPad    *pad,
                                          GstObject *parent,
                                          GstEvent  *event);

[..]

static void
gst_my_filter_init (GstMyFilter * filter)
{
[..]
  /* configure event function on the pad before adding
   * the pad to the element */
  gst_pad_set_event_function (filter->sinkpad,
      gst_my_filter_sink_event);
[..]
}

static gboolean
gst_my_filter_sink_event (GstPad    *pad,
                  GstObject *parent,
                  GstEvent  *event)
{
  gboolean ret;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      /* we should handle the format here */

      /* push the event downstream */
      ret = gst_pad_push_event (filter->srcpad, event);
      break;
    case GST_EVENT_EOS:
      /* end-of-stream, we should close down all stream leftovers here */
      gst_my_filter_stop_processing (filter);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      /* just call the default handler */
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}


```

It is a good idea to call the default event handler
`gst_pad_event_default ()` for unknown events. Depending on the event
type, the default handler will forward the event or simply unref it. The
CAPS event is by default not forwarded so we need to do this in the
event handler ourselves.
