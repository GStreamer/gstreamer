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

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_basesink_debug);
#define GST_CAT_DEFAULT gst_basesink_debug

/* BaseSink signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

#define DEFAULT_SIZE 1024
#define DEFAULT_HAS_LOOP FALSE
#define DEFAULT_HAS_CHAIN TRUE

enum
{
  ARG_0,
  ARG_HAS_LOOP,
  ARG_HAS_CHAIN
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_basesink_debug, "basesink", 0, "basesink element");

GST_BOILERPLATE_FULL (GstBaseSink, gst_basesink, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_basesink_set_clock (GstElement * element, GstClock * clock);

static void gst_basesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate *gst_base_sink_get_template (GstBaseSink * sink);
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

static GstStaticPadTemplate *
gst_basesink_get_template (GstBaseSink * bsink)
{
  GstStaticPadTemplate *template = NULL;
  GstBaseSinkClass *bclass;

  bclass = GST_BASESINK_GET_CLASS (bsink);

  if (bclass->get_template)
    template = bclass->get_template (bsink);

  if (template == NULL) {
    template = &sinktemplate;
  }
  return template;
}

static void
gst_basesink_base_init (gpointer g_class)
{
  //GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  /*
     gst_element_class_add_pad_template (gstelement_class,
     gst_static_pad_template_get (&sinktemplate));
   */
}

static void
gst_basesink_class_init (GstBaseSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_basesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_basesink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HAS_LOOP,
      g_param_spec_boolean ("has-loop", "has-loop",
          "Enable loop-based operation", DEFAULT_HAS_LOOP,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "has-chain",
          "Enable chain-based operation", DEFAULT_HAS_CHAIN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_basesink_set_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_basesink_change_state);

  klass->get_caps = GST_DEBUG_FUNCPTR (gst_base_sink_get_caps);
  klass->set_caps = GST_DEBUG_FUNCPTR (gst_base_sink_set_caps);
  klass->get_template = GST_DEBUG_FUNCPTR (gst_base_sink_get_template);
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
    GstStaticPadTemplate *stemplate;
    GstPadTemplate *template;

    stemplate = gst_basesink_get_template (bsink);
    template = gst_static_pad_template_get (stemplate);
    caps = gst_caps_copy (gst_pad_template_get_caps (template));
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
gst_basesink_init (GstBaseSink * basesink)
{
  GstStaticPadTemplate *template;

  template = gst_basesink_get_template (basesink);

  basesink->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (template),
      "sink");
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

  /* it's not null if we got it, but it might not be ours */
  sink = GST_BASESINK (object);

  switch (prop_id) {
    case ARG_HAS_LOOP:
      sink->has_loop = g_value_get_boolean (value);
      gst_basesink_set_all_pad_functions (sink);
      break;
    case ARG_HAS_CHAIN:
      sink->has_chain = g_value_get_boolean (value);
      gst_basesink_set_all_pad_functions (sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_basesink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_BASESINK (object));

  sink = GST_BASESINK (object);

  switch (prop_id) {
    case ARG_HAS_LOOP:
      g_value_set_boolean (value, sink->has_loop);
      break;
    case ARG_HAS_CHAIN:
      g_value_set_boolean (value, sink->has_chain);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStaticPadTemplate *
gst_base_sink_get_template (GstBaseSink * sink)
{
  return &sinktemplate;
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

/* STREAM_LOCK should be held */
GstFlowReturn
gst_basesink_finish_preroll (GstBaseSink * basesink, GstPad * pad,
    GstBuffer * buffer)
{
  gboolean usable;
  GstBaseSinkClass *bclass;

  /* lock order is important */
  GST_STATE_LOCK (basesink);
  GST_PREROLL_LOCK (pad);
  if (!basesink->need_preroll)
    goto no_preroll;

  bclass = GST_BASESINK_GET_CLASS (basesink);

  if (bclass->preroll)
    bclass->preroll (basesink, buffer);

  gst_element_commit_state (GST_ELEMENT (basesink));
  GST_STATE_UNLOCK (basesink);

  GST_DEBUG ("element %s waiting to finish preroll",
      GST_ELEMENT_NAME (basesink));
  basesink->need_preroll = FALSE;
  basesink->have_preroll = TRUE;
  GST_PREROLL_WAIT (pad);
  GST_DEBUG ("done preroll");
  basesink->have_preroll = FALSE;

  GST_LOCK (pad);
  usable = !GST_RPAD_IS_FLUSHING (pad) && GST_RPAD_IS_ACTIVE (pad);
  GST_UNLOCK (pad);
  if (!usable)
    goto unusable;

  GST_DEBUG ("done preroll");

  GST_PREROLL_UNLOCK (pad);
  return GST_FLOW_OK;

no_preroll:
  {
    GST_PREROLL_UNLOCK (pad);
    GST_STATE_UNLOCK (basesink);
    return GST_FLOW_OK;
  }
unusable:
  {
    GST_DEBUG ("pad is flushing");
    GST_PREROLL_UNLOCK (pad);
    return GST_FLOW_UNEXPECTED;
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

  if (bclass->event)
    bclass->event (basesink, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret;

      GST_STREAM_LOCK (pad);
      ret = gst_basesink_finish_preroll (basesink, pad, NULL);
      if (ret == GST_FLOW_OK) {
        gboolean need_eos;

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
      basesink->end_time = GST_CLOCK_TIME_NONE;
      GST_UNLOCK (basesink);

      GST_LOG_OBJECT (basesink, "clock entry done: %d", ret);
    }
  }
}

static GstFlowReturn
gst_basesink_chain_unlocked (GstPad * pad, GstBuffer * buf)
{
  GstBaseSink *basesink;
  GstFlowReturn result = GST_FLOW_OK;
  GstBaseSinkClass *bclass;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));

  result = gst_basesink_finish_preroll (basesink, pad, buf);
  if (result != GST_FLOW_OK)
    goto exit;

  gst_basesink_do_sync (basesink, buf);

  bclass = GST_BASESINK_GET_CLASS (basesink);
  if (bclass->render)
    bclass->render (basesink, buf);

exit:
  gst_buffer_unref (buf);

  return result;
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

exit:
  GST_STREAM_UNLOCK (pad);
  return;

paused:
  gst_task_pause (GST_RPAD_TASK (pad));
  goto exit;
}

static gboolean
gst_basesink_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;

  basesink = GST_BASESINK (GST_OBJECT_PARENT (pad));

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      g_return_val_if_fail (basesink->has_chain, FALSE);
      result = TRUE;
      break;
    case GST_ACTIVATE_PULL:
      /* if we have a scheduler we can start the task */
      g_return_val_if_fail (basesink->has_loop, FALSE);
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

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      /* need to complete preroll before this state change completes, there
       * is no data flow in READY so we cqn safely assume we need to preroll. */
      basesink->offset = 0;
      GST_PREROLL_LOCK (basesink->sinkpad);
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
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return ret;
}
