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

GST_DEBUG_CATEGORY_STATIC (gst_base_sink_debug);
#define GST_CAT_DEFAULT gst_base_sink_debug

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

static void gst_base_sink_base_init (gpointer g_class);
static void gst_base_sink_class_init (GstBaseSinkClass * klass);
static void gst_base_sink_init (GstBaseSink * trans, gpointer g_class);
static void gst_base_sink_finalize (GObject * object);

GType
gst_base_sink_get_type (void)
{
  static GType base_sink_type = 0;

  if (!base_sink_type) {
    static const GTypeInfo base_sink_info = {
      sizeof (GstBaseSinkClass),
      (GBaseInitFunc) gst_base_sink_base_init,
      NULL,
      (GClassInitFunc) gst_base_sink_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSink),
      0,
      (GInstanceInitFunc) gst_base_sink_init,
    };

    base_sink_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSink", &base_sink_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_sink_type;
}

static void gst_base_sink_set_clock (GstElement * element, GstClock * clock);

static void gst_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_base_sink_get_caps (GstBaseSink * sink);
static gboolean gst_base_sink_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstFlowReturn gst_base_sink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_base_sink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);

static GstElementStateReturn gst_base_sink_change_state (GstElement * element);

static GstFlowReturn gst_base_sink_chain (GstPad * pad, GstBuffer * buffer);
static void gst_base_sink_loop (GstPad * pad);
static GstFlowReturn gst_base_sink_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_base_sink_activate_push (GstPad * pad, gboolean active);
static gboolean gst_base_sink_activate_pull (GstPad * pad, gboolean active);
static gboolean gst_base_sink_event (GstPad * pad, GstEvent * event);
static inline GstFlowReturn gst_base_sink_handle_buffer (GstBaseSink * basesink,
    GstBuffer * buf);
static inline gboolean gst_base_sink_handle_event (GstBaseSink * basesink,
    GstEvent * event);

static void
gst_base_sink_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_sink_debug, "basesink", 0,
      "basesink element");
}

static void
gst_base_sink_class_init (GstBaseSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_sink_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_base_sink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_base_sink_get_property);

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

  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_base_sink_set_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_sink_change_state);

  klass->get_caps = GST_DEBUG_FUNCPTR (gst_base_sink_get_caps);
  klass->set_caps = GST_DEBUG_FUNCPTR (gst_base_sink_set_caps);
  klass->buffer_alloc = GST_DEBUG_FUNCPTR (gst_base_sink_buffer_alloc);
  klass->get_times = GST_DEBUG_FUNCPTR (gst_base_sink_get_times);
}

static GstCaps *
gst_base_sink_pad_getcaps (GstPad * pad)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  GstCaps *caps = NULL;

  bsink = GST_BASE_SINK (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);
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
gst_base_sink_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  gboolean res = FALSE;

  bsink = GST_BASE_SINK (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bclass->set_caps)
    res = bclass->set_caps (bsink, caps);

  return res;
}

static GstFlowReturn
gst_base_sink_pad_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  GstFlowReturn result = GST_FLOW_OK;

  bsink = GST_BASE_SINK (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bclass->buffer_alloc)
    result = bclass->buffer_alloc (bsink, offset, size, caps, buf);
  else
    *buf = NULL;

  return result;
}

static void
gst_base_sink_init (GstBaseSink * basesink, gpointer g_class)
{
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);

  basesink->sinkpad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_getcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_getcaps));
  gst_pad_set_setcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_setcaps));
  gst_pad_set_bufferalloc_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_buffer_alloc));
  gst_element_add_pad (GST_ELEMENT (basesink), basesink->sinkpad);

  basesink->pad_mode = GST_ACTIVATE_NONE;
  GST_PAD_TASK (basesink->sinkpad) = NULL;
  basesink->preroll_queue = g_queue_new ();

  GST_FLAG_SET (basesink, GST_ELEMENT_IS_SINK);
}

static void
gst_base_sink_finalize (GObject * object)
{
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (object);

  g_queue_free (basesink->preroll_queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_sink_set_pad_functions (GstBaseSink * this, GstPad * pad)
{
  gst_pad_set_activatepush_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_sink_activate_push));
  gst_pad_set_activatepull_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_sink_activate_pull));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_base_sink_event));

  if (this->has_chain)
    gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_base_sink_chain));
  else
    gst_pad_set_chain_function (pad, NULL);
}

static void
gst_base_sink_set_all_pad_functions (GstBaseSink * this)
{
  GList *l;

  for (l = GST_ELEMENT_PADS (this); l; l = l->next)
    gst_base_sink_set_pad_functions (this, (GstPad *) l->data);
}

static void
gst_base_sink_set_clock (GstElement * element, GstClock * clock)
{
  GstBaseSink *sink;

  sink = GST_BASE_SINK (element);

  sink->clock = clock;
}

static void
gst_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSink *sink;

  sink = GST_BASE_SINK (object);

  switch (prop_id) {
    case PROP_HAS_LOOP:
      GST_LOCK (sink);
      sink->has_loop = g_value_get_boolean (value);
      gst_base_sink_set_all_pad_functions (sink);
      GST_UNLOCK (sink);
      break;
    case PROP_HAS_CHAIN:
      GST_LOCK (sink);
      sink->has_chain = g_value_get_boolean (value);
      gst_base_sink_set_all_pad_functions (sink);
      GST_UNLOCK (sink);
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
}

static void
gst_base_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSink *sink;

  sink = GST_BASE_SINK (object);

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

static GstFlowReturn
gst_base_sink_buffer_alloc (GstBaseSink * sink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  *buf = NULL;
  return GST_FLOW_OK;
}

/* with PREROLL_LOCK */
static GstFlowReturn
gst_base_sink_preroll_queue_empty (GstBaseSink * basesink, GstPad * pad)
{
  GstMiniObject *obj;
  GQueue *q = basesink->preroll_queue;
  GstFlowReturn ret;

  ret = GST_FLOW_OK;

  if (q) {
    GST_DEBUG ("emptying queue");
    while ((obj = g_queue_pop_head (q))) {
      gboolean is_buffer;

      is_buffer = GST_IS_BUFFER (obj);
      if (is_buffer) {
        basesink->preroll_queued--;
        basesink->buffers_queued--;
      } else {
        switch (GST_EVENT_TYPE (obj)) {
          case GST_EVENT_EOS:
            basesink->preroll_queued--;
            break;
          default:
            break;
        }
        basesink->events_queued--;
      }
      /* we release the preroll lock while pushing so that we
       * can still flush it while blocking on the clock or
       * inside the element. */
      GST_PREROLL_UNLOCK (pad);

      if (is_buffer) {
        GST_DEBUG ("popped buffer %p", obj);
        ret = gst_base_sink_handle_buffer (basesink, GST_BUFFER (obj));
      } else {
        GST_DEBUG ("popped event %p", obj);
        gst_base_sink_handle_event (basesink, GST_EVENT (obj));
        ret = GST_FLOW_OK;
      }

      GST_PREROLL_LOCK (pad);
    }
    GST_DEBUG ("queue empty");
  }
  return ret;
}

/* with PREROLL_LOCK */
static void
gst_base_sink_preroll_queue_flush (GstBaseSink * basesink, GstPad * pad)
{
  GstMiniObject *obj;
  GQueue *q = basesink->preroll_queue;

  GST_DEBUG ("flushing queue %p", basesink);
  if (q) {
    while ((obj = g_queue_pop_head (q))) {
      GST_DEBUG ("popped %p", obj);
      gst_mini_object_unref (obj);
    }
  }
  /* we can't have EOS anymore now */
  basesink->eos = FALSE;
  basesink->preroll_queued = 0;
  basesink->buffers_queued = 0;
  basesink->events_queued = 0;
  /* and signal any waiters now */
  GST_PREROLL_SIGNAL (pad);
}

/* with STREAM_LOCK */
static GstFlowReturn
gst_base_sink_handle_object (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj)
{
  gint length;
  gboolean have_event;

  GST_PREROLL_LOCK (pad);
  /* push object on the queue */
  GST_DEBUG ("push on queue %p", basesink, obj);
  g_queue_push_tail (basesink->preroll_queue, obj);

  have_event = GST_IS_EVENT (obj);
  if (have_event) {
    GstEvent *event = GST_EVENT (obj);

    switch (GST_EVENT_TYPE (obj)) {
      case GST_EVENT_EOS:
        basesink->preroll_queued++;
        basesink->eos = TRUE;
        break;
      case GST_EVENT_DISCONTINUOUS:
        /* the discont event is needed to bring the buffer timestamps to the
         * stream time */
        if (!gst_event_discont_get_value (event, GST_FORMAT_TIME,
                (gint64 *) & basesink->discont_start,
                (gint64 *) & basesink->discont_stop)) {
          basesink->discont_start = 0;
          basesink->discont_stop = 0;
        }
        basesink->have_discont = TRUE;

        GST_DEBUG ("received DISCONT %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
            GST_TIME_ARGS (basesink->discont_start),
            GST_TIME_ARGS (basesink->discont_stop));
        break;
      default:
        break;
    }
    basesink->events_queued++;
  } else {
    if (!basesink->have_discont) {
      GST_ELEMENT_ERROR (basesink, STREAM, STOPPED,
          ("received buffer without a discont"),
          ("received buffer without a discont"));
    }
    basesink->preroll_queued++;
    basesink->buffers_queued++;
  }
  GST_DEBUG ("now %d preroll, %d buffers, %d events on queue",
      basesink->preroll_queued,
      basesink->buffers_queued, basesink->events_queued);

  /* check if we are prerolling */
  if (!basesink->need_preroll)
    goto no_preroll;

  /* there is a buffer queued */
  if (basesink->buffers_queued == 1) {
    GST_DEBUG ("do preroll %p", obj);

    /* if it's a buffer, we need to call the preroll method */
    if (GST_IS_BUFFER (obj)) {
      GstBaseSinkClass *bclass;
      GstFlowReturn pres;

      bclass = GST_BASE_SINK_GET_CLASS (basesink);
      if (bclass->preroll)
        if ((pres =
                bclass->preroll (basesink, GST_BUFFER (obj))) != GST_FLOW_OK)
          goto preroll_failed;
    }
  }
  length = basesink->preroll_queued;
  GST_DEBUG ("prerolled length %d", length);

  if (length == 1) {
    guint t;

    basesink->have_preroll = TRUE;
    /* we are prerolling */
    GST_PREROLL_UNLOCK (pad);

    /* have to release STREAM_LOCK as we cannot take the STATE_LOCK
     * inside the STREAM_LOCK */
    t = GST_STREAM_UNLOCK_FULL (pad);
    GST_DEBUG ("released stream lock %d times", t);
    if (t == 0) {
      GST_WARNING ("STREAM_LOCK should have been locked !!");
      g_warning ("STREAM_LOCK should have been locked !!");
    }

    /* now we commit our state */
    GST_STATE_LOCK (basesink);
    GST_DEBUG ("commit state %p >", basesink);
    gst_element_commit_state (GST_ELEMENT (basesink));
    GST_STATE_UNLOCK (basesink);

    /* reacquire stream lock, pad could be flushing now */
    /* FIXME in glib, if t==0, the lock is still taken... hmmm */
    if (t > 0)
      GST_STREAM_LOCK_FULL (pad, t);

    /* and wait if needed */
    GST_PREROLL_LOCK (pad);

    GST_LOCK (pad);
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
      goto flushing;
    GST_UNLOCK (pad);

    /* it is possible that the application set the state to PLAYING
     * now in which case we don't need to block anymore. */
    if (!basesink->need_preroll)
      goto no_preroll;

    length = basesink->preroll_queued;
  }

  /* see if we need to block now. We cannot block on events, only
   * on buffers, the reason is that events can be sent from the
   * application thread and we don't want to block there. */
  if (length > basesink->preroll_queue_max_len && !have_event) {
    /* block until the state changes, or we get a flush, or something */
    GST_DEBUG ("element %s waiting to finish preroll",
        GST_ELEMENT_NAME (basesink));
    GST_PREROLL_WAIT (pad);
    GST_DEBUG ("done preroll");
    basesink->have_preroll = FALSE;
  }
  GST_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;
  GST_UNLOCK (pad);

  GST_PREROLL_UNLOCK (pad);

  return GST_FLOW_OK;

no_preroll:
  {
    GstFlowReturn ret;

    GST_DEBUG ("no preroll needed");
    /* maybe it was another sink that blocked in preroll, need to check for
       buffers to drain */
    basesink->have_preroll = FALSE;
    ret = gst_base_sink_preroll_queue_empty (basesink, pad);
    GST_PREROLL_UNLOCK (pad);

    return ret;
  }
flushing:
  {
    GST_UNLOCK (pad);
    basesink->have_preroll = FALSE;
    GST_PREROLL_UNLOCK (pad);
    GST_DEBUG ("pad is flushing");
    return GST_FLOW_WRONG_STATE;
  }
preroll_failed:
  {
    guint t;

    GST_DEBUG ("preroll failed");
    basesink->have_preroll = FALSE;
    gst_base_sink_preroll_queue_flush (basesink, pad);
    GST_PREROLL_UNLOCK (pad);

    /* have to release STREAM_LOCK as we cannot take the STATE_LOCK
     * inside the STREAM_LOCK */
    t = GST_STREAM_UNLOCK_FULL (pad);
    GST_DEBUG ("released stream lock %d times", t);
    if (t == 0) {
      GST_WARNING ("STREAM_LOCK should have been locked !!");
      g_warning ("STREAM_LOCK should have been locked !!");
    }

    /* now we abort our state */
    GST_STATE_LOCK (basesink);
    GST_DEBUG ("abort state %p >", basesink);
    gst_element_abort_state (GST_ELEMENT (basesink));
    GST_STATE_UNLOCK (basesink);

    /* reacquire stream lock, pad could be flushing now */
    if (t > 0)
      GST_STREAM_LOCK_FULL (pad, t);

    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_base_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseSink *basesink;
  gboolean result = TRUE;
  GstBaseSinkClass *bclass;

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  GST_DEBUG ("event %p", event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret;

      GST_STREAM_LOCK (pad);
      /* EOS also finishes the preroll */
      ret =
          gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (event));
      GST_STREAM_UNLOCK (pad);
      break;
    }
    case GST_EVENT_DISCONTINUOUS:
    {
      GstFlowReturn ret;

      GST_STREAM_LOCK (pad);
      ret =
          gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (event));
      GST_STREAM_UNLOCK (pad);
      break;
    }
    case GST_EVENT_FLUSH:
      /* make sure we are not blocked on the clock also clear any pending
       * eos state. */
      if (bclass->event)
        bclass->event (basesink, event);

      if (!GST_EVENT_FLUSH_DONE (event)) {
        GST_PREROLL_LOCK (pad);
        /* we need preroll after the flush */
        basesink->need_preroll = TRUE;
        /* unlock from a possible state change/preroll */
        gst_base_sink_preroll_queue_flush (basesink, pad);

        GST_LOCK (basesink);
        if (basesink->clock_id) {
          gst_clock_id_unschedule (basesink->clock_id);
        }
        GST_UNLOCK (basesink);
        GST_PREROLL_UNLOCK (pad);

        /* and we need to commit our state again on the next
         * prerolled buffer */
        GST_STATE_LOCK (basesink);
        GST_STREAM_LOCK (pad);
        gst_element_lost_state (GST_ELEMENT (basesink));
        GST_STREAM_UNLOCK (pad);
        GST_STATE_UNLOCK (basesink);
      } else {
        /* now we are completely unblocked and the _chain method
         * will return */
        GST_STREAM_LOCK (pad);
        GST_STREAM_UNLOCK (pad);
      }

      break;
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }

  return result;
}

/* default implementation to calculate the start and end
 * timestamps on a buffer, subclasses can override
 */
static void
gst_base_sink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstClockTime timestamp, duration;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    /* bring timestamp to stream time using last
     * discont offset. */
    timestamp -= basesink->discont_start;
    /* get duration to calculate end time */
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
static gboolean
gst_base_sink_do_sync (GstBaseSink * basesink, GstBuffer * buffer)
{
  gboolean result = TRUE;

  if (basesink->clock) {
    GstClockTime start, end;
    GstBaseSinkClass *bclass;

    bclass = GST_BASE_SINK_GET_CLASS (basesink);
    start = end = -1;
    if (bclass->get_times)
      bclass->get_times (basesink, buffer, &start, &end);

    GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
        ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

    if (GST_CLOCK_TIME_IS_VALID (start)) {
      GstClockReturn ret;
      GstClockTime base_time;

      GST_LOCK (basesink);
      base_time = GST_ELEMENT (basesink)->base_time;

      GST_LOG_OBJECT (basesink,
          "waiting for clock, base time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (base_time));
      /* save clock id so that we can unlock it if needed */
      basesink->clock_id = gst_clock_new_single_shot_id (basesink->clock,
          start + base_time);
      basesink->end_time = end;
      GST_UNLOCK (basesink);

      ret = gst_clock_id_wait (basesink->clock_id, NULL);

      GST_LOCK (basesink);
      if (basesink->clock_id) {
        gst_clock_id_unref (basesink->clock_id);
        basesink->clock_id = NULL;
      }
      GST_UNLOCK (basesink);

      GST_LOG_OBJECT (basesink, "clock entry done: %d", ret);
      if (ret == GST_CLOCK_UNSCHEDULED)
        result = FALSE;
    }
  }
  return result;
}


/* handle an event
 *
 * 2) render the event
 * 3) unref the event
 */
static inline gboolean
gst_base_sink_handle_event (GstBaseSink * basesink, GstEvent * event)
{
  GstBaseSinkClass *bclass;
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_LOCK (basesink);
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
        }
      }
      GST_UNLOCK (basesink);
      break;
    default:
      break;
  }

  bclass = GST_BASE_SINK_GET_CLASS (basesink);
  if (bclass->event)
    ret = bclass->event (basesink, event);
  else
    ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_PREROLL_LOCK (basesink->sinkpad);
      /* if we are still EOS, we can post the EOS message */
      if (basesink->eos) {
        /* ok, now we can post the message */
        GST_DEBUG_OBJECT (basesink, "Now posting EOS");
        gst_element_post_message (GST_ELEMENT (basesink),
            gst_message_new_eos (GST_OBJECT (basesink)));
      }
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    default:
      break;
  }

  GST_DEBUG ("event unref %p %p", basesink, event);
  gst_event_unref (event);

  return ret;
}

/* handle a buffer
 *
 * 1) first sync on the buffer
 * 2) render the buffer
 * 3) unref the buffer
 */
static inline GstFlowReturn
gst_base_sink_handle_buffer (GstBaseSink * basesink, GstBuffer * buf)
{
  GstBaseSinkClass *bclass;
  GstFlowReturn ret;

  gst_base_sink_do_sync (basesink, buf);

  bclass = GST_BASE_SINK_GET_CLASS (basesink);
  if (bclass->render)
    ret = bclass->render (basesink, buf);
  else
    ret = GST_FLOW_OK;

  GST_DEBUG ("buffer unref after render %p", basesink, buf);
  gst_buffer_unref (buf);

  return ret;
}

static GstFlowReturn
gst_base_sink_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseSink *basesink;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  result = gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (buf));

  return result;
}

/* FIXME, not all sinks can operate in pull mode 
 */
static void
gst_base_sink_loop (GstPad * pad)
{
  GstBaseSink *basesink;
  GstBuffer *buf = NULL;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  g_assert (basesink->pad_mode == GST_ACTIVATE_PULL);

  result = gst_pad_pull_range (pad, basesink->offset, DEFAULT_SIZE, &buf);
  if (result != GST_FLOW_OK)
    goto paused;

  result = gst_base_sink_chain (pad, buf);
  if (result != GST_FLOW_OK)
    goto paused;

  /* default */
  return;

paused:
  {
    gst_pad_pause_task (pad);
    return;
  }
}

static gboolean
gst_base_sink_deactivate (GstBaseSink * basesink, GstPad * pad)
{
  gboolean result = FALSE;
  GstBaseSinkClass *bclass;

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  /* step 1, unblock clock sync (if any) or any other blocking thing */
  GST_PREROLL_LOCK (pad);
  GST_LOCK (basesink);
  if (basesink->clock_id) {
    gst_clock_id_unschedule (basesink->clock_id);
  }
  GST_UNLOCK (basesink);

  /* unlock any subclasses */
  if (bclass->unlock)
    bclass->unlock (basesink);

  /* flush out the data thread if it's locked in finish_preroll */
  basesink->need_preroll = FALSE;
  gst_base_sink_preroll_queue_flush (basesink, pad);
  GST_PREROLL_UNLOCK (pad);

  /* step 2, make sure streaming finishes */
  result = gst_pad_stop_task (pad);

  return result;
}

static gboolean
gst_base_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  if (active) {
    g_return_val_if_fail (basesink->has_chain, FALSE);
    result = TRUE;
  } else {
    result = gst_base_sink_deactivate (basesink, pad);
  }
  basesink->pad_mode = GST_ACTIVATE_PUSH;

  return result;
}

/* this won't get called until we implement an activate function */
static gboolean
gst_base_sink_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  if (active) {
    /* if we have a scheduler we can start the task */
    g_return_val_if_fail (basesink->has_loop, FALSE);
    result =
        gst_pad_start_task (pad, (GstTaskFunction) gst_base_sink_loop, pad);
  } else {
    result = gst_base_sink_deactivate (basesink, pad);
  }

  return result;
}

static GstElementStateReturn
gst_base_sink_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstBaseSink *basesink = GST_BASE_SINK (element);
  GstElementState transition = GST_STATE_TRANSITION (element);
  GstBaseSinkClass *bclass;

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if (bclass->start)
        if (!bclass->start (basesink))
          goto start_failed;
      break;
    case GST_STATE_READY_TO_PAUSED:
      /* need to complete preroll before this state change completes, there
       * is no data flow in READY so we can safely assume we need to preroll. */
      basesink->offset = 0;
      GST_PREROLL_LOCK (basesink->sinkpad);
      basesink->have_preroll = FALSE;
      basesink->need_preroll = TRUE;
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      basesink->have_discont = FALSE;
      basesink->discont_start = 0;
      basesink->discont_stop = 0;
      ret = GST_STATE_ASYNC;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    {
      GST_PREROLL_LOCK (basesink->sinkpad);
      /* if we have EOS, we should empty the queue now as there will
       * be no more data received in the chain function.
       * FIXME, this could block the state change function too long when
       * we are pushing and syncing the buffers, better start a new
       * thread to do this. */
      if (basesink->eos) {
        gst_base_sink_preroll_queue_empty (basesink, basesink->sinkpad);
      }
      /* don't need the preroll anymore */
      basesink->need_preroll = FALSE;
      if (basesink->have_preroll) {
        /* now let it play */
        GST_PREROLL_SIGNAL (basesink->sinkpad);
      }
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    }
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
    {
      GstBaseSinkClass *bclass;

      bclass = GST_BASE_SINK_GET_CLASS (basesink);

      GST_PREROLL_LOCK (basesink->sinkpad);
      GST_LOCK (basesink);
      /* unlock clock wait if any */
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }
      GST_UNLOCK (basesink);

      /* unlock any subclasses */
      if (bclass->unlock)
        bclass->unlock (basesink);

      /* if we don't have a preroll buffer and we have not received EOS,
       * we need to wait for a preroll */
      GST_DEBUG ("have_preroll: %d, EOS: %d", basesink->have_preroll,
          basesink->eos);
      if (!basesink->have_preroll && !basesink->eos) {
        basesink->need_preroll = TRUE;
        ret = GST_STATE_ASYNC;
      }
      GST_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    }
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      if (bclass->stop)
        if (!bclass->stop (basesink)) {
          GST_WARNING ("failed to stop");
        }
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
start_failed:
  {
    GST_DEBUG ("failed to start");
    return GST_STATE_FAILURE;
  }
}
