---
title: The chain function
...

# The chain function

The chain function is the function in which all data processing takes
place. In the case of a simple filter, `_chain ()` functions are mostly
linear functions - so for each incoming buffer, one buffer will go out,
too. Below is a very simple implementation of a chain function:

``` c

static GstFlowReturn gst_my_filter_chain (GstPad    *pad,
                                          GstObject *parent,
                                          GstBuffer *buf);

[..]

static void
gst_my_filter_init (GstMyFilter * filter)
{
[..]
  /* configure chain function on the pad before adding
   * the pad to the element */
  gst_pad_set_chain_function (filter->sinkpad,
      gst_my_filter_chain);
[..]
}

static GstFlowReturn
gst_my_filter_chain (GstPad    *pad,
                     GstObject *parent,
             GstBuffer *buf)
{
  GstMyFilter *filter = GST_MY_FILTER (parent);

  if (!filter->silent)
    g_print ("Have data of size %" G_GSIZE_FORMAT" bytes!\n",
        gst_buffer_get_size (buf));

  return gst_pad_push (filter->srcpad, buf);
}
```

Obviously, the above doesn't do much useful. Instead of printing that
the data is in, you would normally process the data there. Remember,
however, that buffers are not always writeable.

In more advanced elements (the ones that do event processing), you may
want to additionally specify an event handling function, which will be
called when stream-events are sent (such as caps, end-of-stream,
newsegment, tags, etc.).

```c
static void
gst_my_filter_init (GstMyFilter * filter)
{
[..]
  gst_pad_set_event_function (filter->sinkpad,
      gst_my_filter_sink_event);
[..]
}



static gboolean
gst_my_filter_sink_event (GstPad    *pad,
                  GstObject *parent,
                  GstEvent  *event)
{
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      /* we should handle the format here */
      break;
    case GST_EVENT_EOS:
      /* end-of-stream, we should close down all stream leftovers here */
      gst_my_filter_stop_processing (filter);
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_my_filter_chain (GstPad    *pad,
             GstObject *parent,
             GstBuffer *buf)
{
  GstMyFilter *filter = GST_MY_FILTER (parent);
  GstBuffer *outbuf;

  outbuf = gst_my_filter_process_data (filter, buf);
  gst_buffer_unref (buf);
  if (!outbuf) {
    /* something went wrong - signal an error */
    GST_ELEMENT_ERROR (GST_ELEMENT (filter), STREAM, FAILED, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }

  return gst_pad_push (filter->srcpad, outbuf);
}
```

In some cases, it might be useful for an element to have control over
the input data rate, too. In that case, you probably want to write a
so-called *loop-based* element. Source elements (with only source pads)
can also be *get-based* elements. These concepts will be explained in
the advanced section of this guide, and in the section that specifically
discusses source pads.
