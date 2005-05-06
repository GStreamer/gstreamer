/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasesink.c: 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesink.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (gst_basesink_debug);
#define GST_CAT_DEFAULT gst_basesink_debug

/* #define DEBUGGING */
#ifdef DEBUGGING
#define DEBUG(str,args...) g_print (str,##args)
#else
#define DEBUG(str,args...)
#endif

/* BaseSink signals and properties */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

/* FIXME, need to figure out a better way to handle the pull mode */
#define DEFAULT_SIZE 1024
#define DEFAULT_HAS_LOOP FALSE
#define DEFAULT_HAS_CHAIN TRUE

enum
{
  PROP_0,
  PROP_HAS_LOOP,
  PROP_HAS_CHAIN,
  PROP_PREROLL_QUEUE_LEN
};

static GstElementClass *parent_class = NULL;

static void gst_basesink_base_init (gpointer g_class);
static void gst_basesink_class_init (GstBaseSinkClass * klass);
static void gst_basesink_init (GstBaseSink * trans, gpointer g_class);

GType
gst_basesink_get_type (void)
{
  static GType basesink_type = 0;

  if (!basesink_type) {
    static const GTypeInfo basesink_info = {
      sizeof (GstBaseSinkClass),
      (GBaseInitFunc) gst_basesink_base_init,
      NULL,
      (GClassInitFunc) gst_basesink_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSink),
      0,
      (GInstanceInitFunc) gst_basesink_init,
    };

    basesink_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSink", &basesink_info, G_TYPE_FLAG_ABSTRACT);
  }
  return basesink_type;
}

static void gst_basesink_set_clock (GstElement * element, GstClock * clock);

static void gst_basesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_base_sink_get_caps (GstBaseSink * sink);
static gboolean gst_base_sink_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstBuffer *gst_base_sink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps);
static void gst_basesink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);

static GstElementStateReturn gst_basesink_change_state (GstElement * element);

static GstFlowReturn gst_basesink_chain_unlocked (GstPad * pad,
    GstBuffer * buffer);
static void gst_basesink_loop (GstPad * pad);
static GstFlowReturn gst_basesink_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_basesink_activate (GstPad * pad, GstActivateMode mode);
static gboolean gst_basesink_event (GstPad * pad, GstEvent * event);
static inline GstFlowReturn gst_basesink_handle_buffer (GstBaseSink * basesink,
    GstBuffer * buf);

static void
gst_basesink_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_basesink_debug, "basesink", 0,
      "basesink element");
}

static void
gst_basesink_class_init (GstBaseSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_basesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_basesink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_LOOP,
      g_param_spec_boolean ("has-loop", "has-loop",
          "Enable loop-based operation", DEFAULT_HAS_LOOP,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "has-chain",
          "Enable chain-based operation", DEFAULT_HAS_CHAIN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  /* FIXME, this next value should be configured using an event from the
   * upstream element */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PREROLL_QUEUE_LEN,
      g_param_spec_uint ("preroll-queue-len", "preroll-queue-len",
          "Number of buffers to queue during preroll", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_basesink_set_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_basesink_change_state);

  klass->get_caps = GST_DEBUG_FUNCPTR (gst_base_sink_get_caps);
  klass->set_caps = GST_DEBUG_FUNCPTR (gst_base_sink_set_caps);
  klass->buffer_alloc = GST_DEBUG_FUNCPTR (gst_base_sink_buffer_alloc);
  klass->get_times = GST_DEBUG_FUNCPTR (gst_basesink_get_times);
}

static GstCaps *
gst_basesink_pad_getcaps (GstPad * pad)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  GstCaps *caps = NULL;

  bsink = GST_BASESINK (GST_PAD_PARENT (pad));
  bclass = GST_BASESINK_GET_CLASS (bsink);
  if (bclass->get_caps)
    caps = bclass->get_caps (bsink);

  if (caps == NULL) {
    GstPadTemplate *pad_template;

    pad_template =
        gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
    if (pad_template != NULL) {
      caps = gst_caps_ref (gst_pad_template_get_caps (pad_template));
    }
  }

  return caps;
}

static gboolean
gst_basesink_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  gboolean res = FALSE;

  bsink = GST_BASESINK (GST_PAD_PARENT (pad));
  bclass = GST_BASESINK_GET_CLASS (bsink);

  if (bclass->set_caps)
    res = bclass->set_caps (bsink, caps);

  return res;
}

static GstBuffer *
gst_basesink_pad_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  GstBuffer *buffer = NULL;

  bsink = GST_BASESINK (GST_PAD_PARENT (pad));
  bclass = GST_BASESINK_GET_CLASS (bsink);

  if (bclass->buffer_alloc)
    buffer = bclass->buffer_alloc (bsink, offset, size, caps);

  return buffer;
}

static void
gst_basesink_init (GstBaseSink * basesink, gpointer g_class)
{
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);

  basesink->sinkpad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_getcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_basesink_pad_getcaps));
  gst_pad_set_setcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_basesink_pad_setcaps));
  gst_pad_set_bufferalloc_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_basesink_pad_buffer_alloc));
  gst_element_add_pad (GST_ELEMENT (basesink), basesink->sinkpad);

  basesink->pad_mode = GST_ACTIVATE_NONE;
  GST_RPAD_TASK (basesink->sinkpad) = NULL;
}

static void
gst_basesink_set_pad_functions (GstBaseSink * this, GstPad * pad)
{
  gst_pad_set_activate_function (pad,
      GST_DEBUG_FUNCPTR (gst_basesink_activate));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_basesink_event));

  if (this->has_chain)
    gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_basesink_chain));
  else
    gst_pad_set_chain_function (pad, NULL);

  if (this->has_loop)
    gst_pad_set_loop_function (pad, GST_DEBUG_FUNCPTR (gst_basesink_loop));
  else
    gst_pad_set_loop_function (pad, NULL);
}

static void
gst_basesink_set_all_pad_functions (GstBaseSink * this)
{
  GList *l;

  for (l = GST_ELEMENT_PADS (this); l; l = l->next)
    gst_basesink_set_pad_functions (this, (GstPad *) l->data);
}

static void
gst_basesink_set_clock (GstElement * element, GstClock * clock)
{
  GstBaseSink *sink;

  sink = GST_BASESINK (element);

  sink->clock = clock;
}

static void
gst_basesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSink *sink;

  sink = GST_BASESINK (object);

  GST_LOCK (sink);
  switch (prop_id) {
    case PROP_HAS_LOOP:
      sink->has_loop = g_value_get_boolean (value);
      gst_basesink_set_all_pad_functions (sink);
      break;
    case PROP_HAS_CHAIN:
      sink->has_chain = g_value_get_boolean (value);
      gst_basesink_set_all_pad_functions (sink);
      break;
    case PROP_PREROLL_QUEUE_LEN:
      /* preroll lock necessary to serialize with finish_preroll */
      GST_PREROLL_LOCK (sink->sinkpad);
      sink->preroll_queue_max_len = g_value_get_uint (value);
      GST_PREROLL_UNLOCK (sink->sinkpad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_UNLOCK (sink);
}

static void
gst_basesink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSink *sink;

  sink = GST_BASESINK (object);

  GST_LOCK (sink);
  switch (prop_id) {
    case PROP_HAS_LOOP:
      g_value_set_boolean (value, sink->has_loop);
      break;
    case PROP_HAS_CHAIN:
      g_value_set_boolean (value, sink->has_chain);
      break;
    case PROP_PREROLL_QUEUE_LEN:
      g_value_set_uint (value, sink->preroll_queue_max_len);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_UNLOCK (sink);
}

static GstCaps *
gst_base_sink_get_caps (GstBaseSink * sink)
{
  return NULL;
}

static gboolean
gst_base_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  return TRUE;
}

static GstBuffer *
gst_base_sink_buffer_alloc (GstBaseSink * sink, guint64 offset, guint size,
    GstCaps * caps)
{
  return NULL;
}

/* with PREROLL_LOCK */
static void
gst_basesink_preroll_queue_push (GstBaseSink * basesink, GstPad * pad,
    GstBuffer * buffer)
{
  if (basesink->preroll_queue->length == 0) {
    GstBaseSinkClass *bclass = GST_BASESINK_GET_CLASS (basesink);

    if (bclass->preroll)
      bclass->preroll (basesink, buffer);
  }

  if (basesink->preroll_queue->length < basesink->preroll_queue_max_len) {
    DEBUG ("push %p %p\n", basesink, buffer);
    g_queue_push_tail (basesink->preroll_queue, buffer);
  } else {
    /* block until the state changes, or we get a flush, or something */
    DEBUG ("block %p %p\n", basesink, buffer);
    GST_DEBUG ("element %s waiting to finish preroll",
        GST_ELEMENT_NAME (basesink));
    basesink->need_preroll = FALSE;
    basesink->have_preroll = TRUE;
    GST_PREROLL_WAIT (pad);
    GST_DEBUG ("done preroll");
    basesink->have_preroll = FALSE;
  }
}

/* with PREROLL_LOCK */
static GstFlowReturn
gst_basesink_preroll_queue_empty (GstBaseSink * basesink, GstPad * pad)
{
  GstBuffer *buf;
  GQueue *q = basesink->preroll_queue;
  GstFlowReturn ret;

  ret = GST_FLOW_OK;

  if (q) {
    DEBUG ("empty queue\n");
    while ((buf = g_queue_pop_head (q))) {
      DEBUG ("pop %p\n", buf);
      ret = gst_basesink_handle_buffer (basesink, buf);
    }
    DEBUG ("queue len %p %d\n", basesink, q->length);
  }
  return ret;
}

/* with PREROLL_LOCK */
static void
gst_basesink_preroll_queue_flush (GstBaseSink * basesink)
{
  GstBuffer *buf;
  GQueue *q = basesink->preroll_queue;

  DEBUG ("flush %p\n", basesink);
  if (q) {
    while ((buf = g_queue_pop_head (q))) {
      DEBUG ("pop %p\n", buf);
      gst_buffer_unref (buf);
    }
  }
}

typedef enum
{
  PREROLL_QUEUEING,
  PREROLL_PLAYING,
  PREROLL_FLUSHING,
  PREROLL_ERROR
} PrerollReturn;

/* with STREAM_LOCK */
PrerollReturn
gst_basesink_finish_preroll (GstBaseSink * basesink, GstPad * pad,
    GstBuffer * buffer)
{
  gboolean usable;

  DEBUG ("finish preroll %p <\n", basesink);
  /* lock order is important */
  GST_STATE_LOCK (basesink);
  GST_PREROLL_LOCK (pad);
  DEBUG ("finish preroll %p >\n", basesink);
  if (!basesink->need_preroll)
    goto no_preroll;

  gst_element_commit_state (GST_ELEMENT (basesink));
  GST_STATE_UNLOCK (basesink);

  gst_basesink_preroll_queue_push (basesink, pad, buffer);

  GST_LOCK (pad);
  usable = !GST_RPAD_IS_FLUSHING (pad) && GST_RPAD_IS_ACTIVE (pad);
  GST_UNLOCK (pad);
  if (!usable)
    goto unusable;

  if (basesink->need_preroll)
    goto still_queueing;

  GST_DEBUG ("done preroll");

  gst_basesink_preroll_queue_empty (basesink, pad);

  GST_PREROLL_UNLOCK (pad);

  return PREROLL_PLAYING;

no_preroll:
  {
    /* maybe it was another sink that blocked in preroll, need to check for
       buffers to drain */
    if (basesink->preroll_queue->length)
      gst_basesink_preroll_queue_empty (basesink, pad);
    GST_PREROLL_UNLOCK (pad);
    GST_STATE_UNLOCK (basesink);
    return PREROLL_PLAYING;
  }
unusable:
  {
    GST_DEBUG ("pad is flushing");
    GST_PREROLL_UNLOCK (pad);
    return PREROLL_FLUSHING;
  }
still_queueing:
  {
    GST_PREROLL_UNLOCK (pad);
    return PREROLL_QUEUEING;
  }
}

static gboolean
gst_basesink_event (GstPad * pad, GstEvent * event)
{
  GstBaseSink *basesink;
  gboolean result = TRUE;
  GstBaseSinkClass *bclass;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));

  bclass = GST_BASESINK_GET_CLASS (basesink);

  DEBUG ("event %p\n", basesink);

  if (bclass->event)
    bclass->event (basesink, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      gboolean need_eos;

      GST_STREAM_LOCK (pad);

      /* EOS also finishes the preroll */
      gst_basesink_finish_preroll (basesink, pad, NULL);

      GST_LOCK (basesink);
      need_eos = basesink->eos = TRUE;
      if (basesink->clock) {
        /* wait for last buffer to finish if we have a valid end time */
        if (GST_CLOCK_TIME_IS_VALID (basesink->end_time)) {
          basesink->clock_id = gst_clock_new_single_shot_id (basesink->clock,
              basesink->end_time + GST_ELEMENT (basesink)->base_time);
          GST_UNLOCK (basesink);

          gst_clock_id_wait (basesink->clock_id, NULL);

          GST_LOCK (basesink);
          if (basesink->clock_id) {
            gst_clock_id_unref (basesink->clock_id);
            basesink->clock_id = NULL;
          }
          basesink->end_time = GST_CLOCK_TIME_NONE;
          need_eos = basesink->eos;
        }
        GST_UNLOCK (basesink);

        /* if we are still EOS, we can post the EOS message */
        if (need_eos) {
          /* ok, now we can post the message */
          gst_element_post_message (GST_ELEMENT (basesink),
              gst_message_new_eos (GST_OBJECT (basesink)));
        }
      }
      GST_STREAM_UNLOCK (pad);
      break;
    }
    case GST_EVENT_DISCONTINUOUS:
      GST_STREAM_LOCK (pad);
      if (basesink->clock) {
        //gint64 value = GST_EVENT_DISCONT_OFFSET (event, 0).value;
      }
      GST_STREAM_UNLOCK (pad);
      break;
    case GST_EVENT_FLUSH:
      /* make sure we are not blocked on the clock also clear any pending
       * eos state. */
      if (!GST_EVENT_FLUSH_DONE (event)) {
        GST_LOCK (basesink);
        basesink->eos = FALSE;
        if (basesink->clock_id) {
          gst_clock_id_unschedule (basesink->clock_id);
        }
        GST_UNLOCK (basesink);

        /* unlock from a possible state change/preroll */
        GST_PREROLL_LOCK (pad);
        basesink->need_preroll = TRUE;
        gst_basesink_preroll_queue_flush (basesink);
        GST_PREROLL_SIGNAL (pad);
        GST_PREROLL_UNLOCK (pad);
      }
      /* now we are completely unblocked and the _chain method
       * will return */
      break;
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }

  return result;
}

/* default implementation to calculate the start and end
 * timestamps on a buffer, subclasses cna override
 */
static void
gst_basesink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstClockTime timestamp, duration;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    duration = GST_BUFFER_DURATION (buffer);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      *end = timestamp + duration;
    }
    *start = timestamp;
  }
}

/* perform synchronisation on a buffer
 * 
 * 1) check if we have a clock, if not, do nothing
 * 2) calculate the start and end time of the buffer
 * 3) create a single shot notification to wait on
 *    the clock, save the entry so we can unlock it
 * 4) wait on the clock, this blocks
 * 5) unref the clockid again
 */
static void
gst_basesink_do_sync (GstBaseSink * basesink, GstBuffer * buffer)
{
  if (basesink->clock) {
    GstClockReturn ret;
    GstClockTime start, end;
    GstBaseSinkClass *bclass;

    bclass = GST_BASESINK_GET_CLASS (basesink);
    start = end = -1;
    if (bclass->get_times)
      bclass->get_times (basesink, buffer, &start, &end);

    GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
        ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

    if (GST_CLOCK_TIME_IS_VALID (start)) {
      /* save clock id so that we can unlock it if needed */
      GST_LOCK (basesink);
      basesink->clock_id = gst_clock_new_single_shot_id (basesink->clock,
          start + GST_ELEMENT (basesink)->base_time);
      basesink->end_time = end;
      GST_UNLOCK (basesink);

      ret = gst_clock_id_wait (basesink->clock_id, NULL);

      GST_LOCK (basesink);
      if (basesink->clock_id) {
        gst_clock_id_unref (basesink->clock_id);
        basesink->clock_id = NULL;
      }
      /* FIXME, don't mess with end_time here */
      basesink->end_time = GST_CLOCK_TIME_NONE;
      GST_UNLOCK (basesink);

      GST_LOG_OBJECT (basesink, "clock entry done: %d", ret);
    }
  }
}

/* handle a buffer
 *
 * 1) first sync on the buffer
 * 2) render the buffer
 * 3) unref the buffer
 */
static inline GstFlowReturn
gst_basesink_handle_buffer (GstBaseSink * basesink, GstBuffer * buf)
{
  GstBaseSinkClass *bclass;
  GstFlowReturn ret;

  gst_basesink_do_sync (basesink, buf);

  bclass = GST_BASESINK_GET_CLASS (basesink);
  if (bclass->render)
    ret = bclass->render (basesink, buf);
  else
    ret = GST_FLOW_OK;

  DEBUG ("unref %p %p\n", basesink, buf);
  gst_buffer_unref (buf);

  return ret;
}

static GstFlowReturn
gst_basesink_chain_unlocked (GstPad * pad, GstBuffer * buf)
{
  GstBaseSink *basesink;
  PrerollReturn result;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));

  DEBUG ("chain_unlocked %p\n", basesink);

  result = gst_basesink_finish_preroll (basesink, pad, buf);

  DEBUG ("chain_unlocked %p after, result %d\n", basesink, result);

  switch (result) {
    case PREROLL_QUEUEING:
      return GST_FLOW_OK;
    case PREROLL_PLAYING:
      return gst_basesink_handle_buffer (basesink, buf);
    case PREROLL_FLUSHING:
      return GST_FLOW_UNEXPECTED;
    default:
      g_assert_not_reached ();
      return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_basesink_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn result;

  g_assert (GST_BASESINK (GST_OBJECT_PARENT (pad))->pad_mode ==
      GST_ACTIVATE_PUSH);

  GST_STREAM_LOCK (pad);

  result = gst_basesink_chain_unlocked (pad, buf);

  GST_STREAM_UNLOCK (pad);

  return result;
}

/* FIXME, not all sinks can operate in pull mode 
 */
static void
gst_basesink_loop (GstPad * pad)
{
  GstBaseSink *basesink;
  GstBuffer *buf = NULL;
  GstFlowReturn result;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));

  g_assert (basesink->pad_mode == GST_ACTIVATE_PULL);

  GST_STREAM_LOCK (pad);

  result = gst_pad_pull_range (pad, basesink->offset, DEFAULT_SIZE, &buf);
  if (result != GST_FLOW_OK)
    goto paused;

  result = gst_basesink_chain_unlocked (pad, buf);
  if (result != GST_FLOW_OK)
    goto paused;

  /* default */
  GST_STREAM_UNLOCK (pad);
  return;

paused:
  gst_task_pause (GST_RPAD_TASK (pad));
  GST_STREAM_UNLOCK (pad);
  return;
}

static gboolean
gst_basesink_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;
  GstBaseSinkClass *bclass;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));
  bclass = GST_BASESINK_GET_CLASS (basesink);

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      g_return_val_if_fail (basesink->has_chain, FALSE);
      result = TRUE;
      break;
    case GST_ACTIVATE_PULL:
      /* if we have a scheduler we can start the task */
      g_return_val_if_fail (basesink->has_loop, FALSE);
      gst_pad_peer_set_active (pad, mode);
      if (GST_ELEMENT_SCHEDULER (basesink)) {
        GST_STREAM_LOCK (pad);
        GST_RPAD_TASK (pad) =
            gst_scheduler_create_task (GST_ELEMENT_SCHEDULER (basesink),
            (GstTaskFunction) gst_basesink_loop, pad);

        gst_task_start (GST_RPAD_TASK (pad));
        GST_STREAM_UNLOCK (pad);
        result = TRUE;
      }
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) or any other blocking thing */
      GST_LOCK (basesink);
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }
      GST_UNLOCK (basesink);

      /* unlock any subclasses */
      if (bclass->unlock)
        bclass->unlock (basesink);

      /* unlock preroll */
      GST_PREROLL_LOCK (pad);
      GST_PREROLL_SIGNAL (pad);
      GST_PREROLL_UNLOCK (pad);

      /* step 2, make sure streaming finishes */
      GST_STREAM_LOCK (pad);
      /* step 3, stop the task */
      if (GST_RPAD_TASK (pad)) {
        gst_task_stop (GST_RPAD_TASK (pad));
        gst_object_unref (GST_OBJECT (GST_RPAD_TASK (pad)));
        GST_RPAD_TASK (pad) = NULL;
      }
      GST_STREAM_UNLOCK (pad);

      result = TRUE;
      break;
  }
  basesink->pad_mode = mode;

  return result;
}

static GstElementStateReturn
gst_basesink_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstBaseSink *basesink = GST_BASESINK (element);
  GstElementState transition = GST_STATE_TRANSITION (element);

  DEBUG ("state change > %p %x\n", basesink, transition);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      /* need to complete preroll before this state change completes, there
       * is no data flow in READY so we can safely assume we need to preroll. */
      basesink->offset = 0;
      GST_PREROLL_LOCK (basesink->sinkpad);
      basesink->preroll_queue = g_queue_new ();
      basesink->need_preroll = TRUE;
      basesink->have_preroll = FALSE;
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      ret = GST_STATE_ASYNC;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      GST_PREROLL_LOCK (basesink->sinkpad);
      if (basesink->have_preroll) {
        /* now let it play */
        GST_PREROLL_SIGNAL (basesink->sinkpad);
      } else {
        /* FIXME. We do not have a preroll and we don't need it anymore 
         * now, this is a case we want to avoid. One way would be to make
         * a 'lost state' function that makes get_state return PAUSED with
         * ASYNC to indicate that we are prerolling again. */
        basesink->need_preroll = FALSE;
      }
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
    {
      gboolean eos;

      /* unlock clock wait if any */
      GST_LOCK (basesink);
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }
      eos = basesink->eos;
      GST_UNLOCK (basesink);

      GST_PREROLL_LOCK (basesink->sinkpad);
      /* if we don't have a preroll buffer and we have not received EOS,
       * we need to wait for a preroll */
      if (!basesink->have_preroll && !eos) {
        basesink->need_preroll = TRUE;
        ret = GST_STATE_ASYNC;
      }
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    }
    case GST_STATE_PAUSED_TO_READY:
      /* flush out the data thread if it's locked in finish_preroll */
      GST_PREROLL_LOCK (basesink->sinkpad);

      gst_basesink_preroll_queue_flush (basesink);
      g_queue_free (basesink->preroll_queue);
      basesink->preroll_queue = NULL;

      if (basesink->have_preroll)
        GST_PREROLL_SIGNAL (basesink->sinkpad);

      basesink->need_preroll = FALSE;
      basesink->have_preroll = FALSE;
      GST_PREROLL_UNLOCK (basesink->sinkpad);

      /* make sure the element is finished processing */
      GST_STREAM_LOCK (basesink->sinkpad);
      GST_STREAM_UNLOCK (basesink->sinkpad);
      /* clear EOS state */
      basesink->eos = FALSE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  DEBUG ("state change < %p %x\n", basesink, transition);
  return ret;
}
