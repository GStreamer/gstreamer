---
title: Different scheduling modes
...

# Different scheduling modes

The scheduling mode of a pad defines how data is retrieved from (source)
or given to (sink) pads. GStreamer can operate in two scheduling mode,
called push- and pull-mode. GStreamer supports elements with pads in any
of the scheduling modes where not all pads need to be operating in the
same mode.

So far, we have only discussed `_chain ()`-operating elements, i.e.
elements that have a chain-function set on their sink pad and push
buffers on their source pad(s). We call this the push-mode because a
peer element will use `gst_pad_push ()` on a srcpad, which will cause
our `_chain ()`-function to be called, which in turn causes our element
to push out a buffer on the source pad. The initiative to start the
dataflow happens somewhere upstream when it pushes out a buffer and all
downstream elements get scheduled when their `_chain ()`-functions are
called in turn.

Before we explain pull-mode scheduling, let's first understand how the
different scheduling modes are selected and activated on a pad.

## The pad activation stage

During the element state change of READY-\>PAUSED, the pads of an
element will be activated. This happens first on the source pads and
then on the sink pads of the element. GStreamer calls the `_activate ()`
of a pad. By default this function will activate the pad in push-mode by
calling `gst_pad_activate_mode ()` with the GST\_PAD\_MODE\_PUSH
scheduling mode. It is possible to override the `_activate ()` of a pad
and decide on a different scheduling mode. You can know in what
scheduling mode a pad is activated by overriding the `_activate_mode
()`-function.

GStreamer allows the different pads of an element to operate in
different scheduling modes. This allows for many different possible
use-cases. What follows is an overview of some typical use-cases.

  - If all pads of an element are activated in push-mode scheduling, the
    element as a whole is operating in push-mode. For source elements
    this means that they will have to start a task that pushes out
    buffers on the source pad to the downstream elements. Downstream
    elements will have data pushed to them by upstream elements using
    the sinkpads `_chain ()`-function which will push out buffers on the
    source pads. Prerequisites for this scheduling mode are that a
    chain-function was set for each sinkpad using
    `gst_pad_set_chain_function ()` and that all downstream elements
    operate in the same mode.

  - Alternatively, sinkpads can be the driving force behind a pipeline
    by operating in pull-mode, while the sourcepads of the element still
    operate in push-mode. In order to be the driving force, those pads
    start a `GstTask` when they are activated. This task is a thread,
    which will call a function specified by the element. When called,
    this function will have random data access (through
    `gst_pad_pull_range ()`) over all sinkpads, and can push data over
    the sourcepads, which effectively means that this element controls
    data flow in the pipeline. Prerequisites for this mode are that all
    downstream elements can act in push mode, and that all upstream
    elements operate in pull-mode (see below).

    Source pads can be activated in PULL mode by a downstream element
    when they return GST\_PAD\_MODE\_PULL from the
    GST\_QUERY\_SCHEDULING query. Prerequisites for this scheduling mode
    are that a getrange-function was set for the source pad using
    `gst_pad_set_getrange_function ()`.

  - Lastly, all pads in an element can be activated in PULL-mode.
    However, contrary to the above, this does not mean that they start a
    task on their own. Rather, it means that they are pull slave for the
    downstream element, and have to provide random data access to it
    from their `_get_range ()`-function. Requirements are that the a
    `_get_range
                                            ()`-function was set on this pad using the function
    `gst_pad_set_getrange_function ()`. Also, if the element has any
    sinkpads, all those pads (and thereby their peers) need to operate
    in PULL access mode, too.

    When a sink element is activated in PULL mode, it should start a
    task that calls `gst_pad_pull_range ()` on its sinkpad. It can only
    do this when the upstream SCHEDULING query returns support for the
    GST\_PAD\_MODE\_PULL scheduling mode.

In the next two sections, we will go closer into pull-mode scheduling
(elements/pads driving the pipeline, and elements/pads providing random
access), and some specific use cases will be given.

## Pads driving the pipeline

Sinkpads operating in pull-mode, with the sourcepads operating in
push-mode (or it has no sourcepads when it is a sink), can start a task
that will drive the pipeline data flow. Within this task function, you
have random access over all of the sinkpads, and push data over the
sourcepads. This can come in useful for several different kinds of
elements:

  - Demuxers, parsers and certain kinds of decoders where data comes in
    unparsed (such as MPEG-audio or video streams), since those will
    prefer byte-exact (random) access from their input. If possible,
    however, such elements should be prepared to operate in push-mode
    mode, too.

  - Certain kind of audio outputs, which require control over their
    input data flow, such as the Jack sound server.

First you need to perform a SCHEDULING query to check if the upstream
element(s) support pull-mode scheduling. If that is possible, you can
activate the sinkpad in pull-mode. Inside the activate\_mode function
you can then start the task.

``` c
#include "filter.h"
#include <string.h>

static gboolean gst_my_filter_activate      (GstPad      * pad,
                                             GstObject   * parent);
static gboolean gst_my_filter_activate_mode (GstPad      * pad,
                                             GstObject   * parent,
                                             GstPadMode    mode,
                         gboolean      active);
static void gst_my_filter_loop      (GstMyFilter * filter);

G_DEFINE_TYPE (GstMyFilter, gst_my_filter, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE(my_filter, "my-filter", GST_RANK_NONE, GST_TYPE_MY_FILTER);

static void
gst_my_filter_init (GstMyFilter * filter)
{

[..]

  gst_pad_set_activate_function (filter->sinkpad, gst_my_filter_activate);
  gst_pad_set_activatemode_function (filter->sinkpad,
      gst_my_filter_activate_mode);


[..]
}

[..]

static gboolean
gst_my_filter_activate (GstPad * pad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  /* first check what upstream scheduling is supported */
  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (pad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  /* see if pull-mode is supported */
  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  /* now we can activate in pull-mode. GStreamer will also
   * activate the upstream peer in pull-mode */
  return gst_pad_activate_mode (pad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    /* something not right, we fallback to push-mode */
    return gst_pad_activate_mode (pad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
gst_my_filter_activate_pull (GstPad    * pad,
                 GstObject * parent,
                 GstPadMode  mode,
                 gboolean    active)
{
  gboolean res;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        filter->offset = 0;
        res = gst_pad_start_task (pad,
            (GstTaskFunction) gst_my_filter_loop, filter, NULL);
      } else {
        res = gst_pad_stop_task (pad);
      }
      break;
    default:
      /* unknown scheduling mode */
      res = FALSE;
      break;
  }
  return res;
}

```

Once started, your task has full control over input and output. The most
simple case of a task function is one that reads input and pushes that
over its source pad. It's not all that useful, but provides some more
flexibility than the old push-mode case that we've been looking at so
far.

``` c
    #define BLOCKSIZE 2048

    static void
    gst_my_filter_loop (GstMyFilter * filter)
    {
      GstFlowReturn ret;
      guint64 len;
      GstBuffer *buf = NULL;

      if (!gst_pad_query_duration (filter->sinkpad, GST_FORMAT_BYTES, &len)) {
        GST_DEBUG_OBJECT (filter, "failed to query duration, pausing");
        goto stop;
      }

       if (filter->offset >= len) {
        GST_DEBUG_OBJECT (filter, "at end of input, sending EOS, pausing");
        gst_pad_push_event (filter->srcpad, gst_event_new_eos ());
        goto stop;
      }

      /* now, read BLOCKSIZE bytes from byte offset filter->offset */
      ret = gst_pad_pull_range (filter->sinkpad, filter->offset,
          BLOCKSIZE, &buf);

      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (filter, "pull_range failed: %s", gst_flow_get_name (ret));
        goto stop;
      }

      /* now push buffer downstream */
      ret = gst_pad_push (filter->srcpad, buf);

      buf = NULL; /* gst_pad_push() took ownership of buffer */

      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (filter, "pad_push failed: %s", gst_flow_get_name (ret));
        goto stop;
      }

      /* everything is fine, increase offset and wait for us to be called again */
      filter->offset += BLOCKSIZE;
      return;

    stop:
      GST_DEBUG_OBJECT (filter, "pausing task");
      gst_pad_pause_task (filter->sinkpad);
    }
```

## Providing random access

In the previous section, we have talked about how elements (or pads)
that are activated to drive the pipeline using their own task, must use
pull-mode scheduling on their sinkpads. This means that all pads linked
to those pads need to be activated in pull-mode. Source pads activated
in pull-mode must implement a `_get_range ()`-function set using
`gst_pad_set_getrange_function ()`, and that function will be called
when the peer pad requests some data with `gst_pad_pull_range ()`. The
element is then responsible for seeking to the right offset and
providing the requested data. Several elements can implement random
access:

  - Data sources, such as a file source, that can provide data from any
    offset with reasonable low latency.

  - Filters that would like to provide a pull-mode scheduling over the
    whole pipeline.

  - Parsers who can easily provide this by skipping a small part of
    their input and are thus essentially "forwarding" getrange requests
    literally without any own processing involved. Examples include tag
    readers (e.g. ID3) or single output parsers, such as a WAVE parser.

The following example will show how a `_get_range
()`-function can be implemented in a source element:

```c
#include "filter.h"
static GstFlowReturn
        gst_my_filter_get_range (GstPad     * pad,
                     GstObject  * parent,
                     guint64      offset,
                     guint        length,
                     GstBuffer ** buf);

G_DEFINE_TYPE (GstMyFilter, gst_my_filter, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE(my_filter, "my-filter", GST_RANK_NONE, GST_TYPE_MY_FILTER);


static void
gst_my_filter_init (GstMyFilter * filter)
{

[..]

  gst_pad_set_getrange_function (filter->srcpad,
      gst_my_filter_get_range);

[..]
}

static GstFlowReturn
gst_my_filter_get_range (GstPad     * pad,
             GstObject  * parent,
             guint64      offset,
             guint        length,
             GstBuffer ** buf)
{

  GstMyFilter *filter = GST_MY_FILTER (parent);

  [.. here, you would fill *buf ..]

  return GST_FLOW_OK;
}
```

In practice, many elements that could theoretically do random access,
may in practice often be activated in push-mode scheduling anyway, since
there is no downstream element able to start its own task. Therefore, in
practice, those elements should implement both a `_get_range
()`-function and a `_chain
()`-function (for filters and parsers) or a `_get_range
()`-function and be prepared to start their own task by providing
`_activate_* ()`-functions (for source elements).
