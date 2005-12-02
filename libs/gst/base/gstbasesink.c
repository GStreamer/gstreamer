/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasesink.c: Base class for sink elements
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

/**
 * SECTION:gstbasesink
 * @short_description: Base class for sink elements
 * @see_also: #GstBaseTransform, #GstBaseSource
 *
 * GstBaseSink is the base class for sink elements in GStreamer, such as
 * xvimagesink or filesink. It is a layer on top of #GstElement that provides a
 * simplified interface to plugin writers. GstBaseSink handles many details for
 * you, for example preroll, clock synchronization, state changes, activation in
 * push or pull mode, and queries. In most cases, when writing sink elements,
 * there is no need to implement class methods from #GstElement or to set
 * functions on pads, because the GstBaseSink infrastructure is sufficient.
 *
 * There is only support in GstBaseSink for one sink pad, which should be named
 * "sink". A sink implementation (subclass of GstBaseSink) should install a pad
 * template in its base_init function, like so:
 * <programlisting>
 * static void
 * my_element_base_init (gpointer g_class)
 * {
 *   GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
 *   
 *   // sinktemplate should be a #GstStaticPadTemplate with direction
 *   // #GST_PAD_SINK and name "sink"
 *   gst_element_class_add_pad_template (gstelement_class,
 *       gst_static_pad_template_get (&amp;sinktemplate));
 *   // see #GstElementDetails
 *   gst_element_class_set_details (gstelement_class, &amp;details);
 * }
 * </programlisting>
 *
 * The one method which all subclasses of GstBaseSink must implement is
 * GstBaseSink::render. This method will be called...
 * 
 * preroll()
 *
 * event(): mostly useful for file-like sinks (seeking or flushing)
 *
 * get_caps/set_caps/buffer_alloc
 *
 * start/stop for resource allocation
 *
 * unlock if you block on an fd, for example
 *
 * get_times i'm sure is for something :P
 *
 * provide example of textsink
 *
 * admonishment not to try to implement your own sink with prerolling...
 *
 * extending via subclassing, setting pad functions, gstelement vmethods.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesink.h"
#include <gst/gstmarshal.h>
#include <gst/gst-i18n-lib.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_sink_debug);
#define GST_CAT_DEFAULT gst_base_sink_debug

/* BaseSink signals and properties */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

#define DEFAULT_SIZE 1024
#define DEFAULT_CAN_ACTIVATE_PULL FALSE /* fixme: enable me */
#define DEFAULT_CAN_ACTIVATE_PUSH TRUE

#define DEFAULT_SYNC TRUE

enum
{
  PROP_0,
  PROP_PREROLL_QUEUE_LEN,
  PROP_SYNC
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

static void gst_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_base_sink_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_base_sink_query (GstElement * element, GstQuery * query);

static GstCaps *gst_base_sink_get_caps (GstBaseSink * sink);
static gboolean gst_base_sink_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstFlowReturn gst_base_sink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_base_sink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);

static GstStateChangeReturn gst_base_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_base_sink_chain (GstPad * pad, GstBuffer * buffer);
static void gst_base_sink_loop (GstPad * pad);
static gboolean gst_base_sink_activate (GstPad * pad);
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

  /* FIXME, this next value should be configured using an event from the
   * upstream element */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PREROLL_QUEUE_LEN,
      g_param_spec_uint ("preroll-queue-len", "preroll-queue-len",
          "Number of buffers to queue during preroll", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync on the clock", DEFAULT_SYNC,
          G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_sink_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_base_sink_send_event);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_base_sink_query);

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

  bsink = GST_BASE_SINK (gst_pad_get_parent (pad));
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
  gst_object_unref (bsink);

  return caps;
}

static gboolean
gst_base_sink_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  gboolean res = FALSE;

  bsink = GST_BASE_SINK (gst_pad_get_parent (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bclass->set_caps)
    res = bclass->set_caps (bsink, caps);

  gst_object_unref (bsink);

  return res;
}

static GstFlowReturn
gst_base_sink_pad_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;
  GstFlowReturn result = GST_FLOW_OK;

  bsink = GST_BASE_SINK (gst_pad_get_parent (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bclass->buffer_alloc)
    result = bclass->buffer_alloc (bsink, offset, size, caps, buf);
  else
    *buf = NULL;                /* fallback in gstpad.c will allocate generic buffer */

  gst_object_unref (bsink);

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
  gst_pad_set_activate_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_activate));
  gst_pad_set_activatepush_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_activate_push));
  gst_pad_set_activatepull_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_activate_pull));
  gst_pad_set_event_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_event));
  gst_pad_set_chain_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_chain));
  gst_element_add_pad (GST_ELEMENT (basesink), basesink->sinkpad);

  basesink->pad_mode = GST_ACTIVATE_NONE;
  GST_PAD_TASK (basesink->sinkpad) = NULL;
  basesink->preroll_queue = g_queue_new ();

  basesink->can_activate_push = DEFAULT_CAN_ACTIVATE_PUSH;
  basesink->can_activate_pull = DEFAULT_CAN_ACTIVATE_PULL;

  basesink->sync = DEFAULT_SYNC;

  GST_OBJECT_FLAG_SET (basesink, GST_ELEMENT_IS_SINK);
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
gst_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSink *sink = GST_BASE_SINK (object);

  switch (prop_id) {
    case PROP_PREROLL_QUEUE_LEN:
      /* preroll lock necessary to serialize with finish_preroll */
      GST_PAD_PREROLL_LOCK (sink->sinkpad);
      sink->preroll_queue_max_len = g_value_get_uint (value);
      GST_PAD_PREROLL_UNLOCK (sink->sinkpad);
      break;
    case PROP_SYNC:
      sink->sync = g_value_get_boolean (value);
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
  GstBaseSink *sink = GST_BASE_SINK (object);

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_PREROLL_QUEUE_LEN:
      g_value_set_uint (value, sink->preroll_queue_max_len);
      break;
    case PROP_SYNC:
      g_value_set_boolean (value, sink->sync);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
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
    GST_DEBUG_OBJECT (basesink, "emptying queue");
    while ((obj = g_queue_pop_head (q))) {
      gboolean is_buffer;

      is_buffer = GST_IS_BUFFER (obj);
      if (G_LIKELY (is_buffer)) {
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
      GST_PAD_PREROLL_UNLOCK (pad);

      if (G_LIKELY (is_buffer)) {
        GST_DEBUG_OBJECT (basesink, "popped buffer %p", obj);
        ret = gst_base_sink_handle_buffer (basesink, GST_BUFFER_CAST (obj));
      } else {
        GST_DEBUG_OBJECT (basesink, "popped event %p", obj);
        gst_base_sink_handle_event (basesink, GST_EVENT_CAST (obj));
        ret = GST_FLOW_OK;
      }

      GST_PAD_PREROLL_LOCK (pad);
    }
    GST_DEBUG_OBJECT (basesink, "queue empty");
  }
  return ret;
}

/* with PREROLL_LOCK */
static void
gst_base_sink_preroll_queue_flush (GstBaseSink * basesink, GstPad * pad)
{
  GstMiniObject *obj;
  GQueue *q = basesink->preroll_queue;

  GST_DEBUG_OBJECT (basesink, "flushing queue %p", basesink);
  if (q) {
    while ((obj = g_queue_pop_head (q))) {
      GST_DEBUG_OBJECT (basesink, "popped %p", obj);
      gst_mini_object_unref (obj);
    }
  }
  /* we can't have EOS anymore now */
  basesink->eos = FALSE;
  basesink->eos_queued = FALSE;
  basesink->preroll_queued = 0;
  basesink->buffers_queued = 0;
  basesink->events_queued = 0;
  basesink->have_preroll = FALSE;
  /* and signal any waiters now */
  GST_PAD_PREROLL_SIGNAL (pad);
}

/* with PREROLL_LOCK */
static gboolean
gst_base_sink_commit_state (GstBaseSink * basesink)
{
  /* commit state and proceed to next pending state */
  {
    GstState current, next, pending, post_pending;
    GstMessage *message;
    gboolean post_paused = FALSE;
    gboolean post_playing = FALSE;

    GST_OBJECT_LOCK (basesink);
    current = GST_STATE (basesink);
    next = GST_STATE_NEXT (basesink);
    pending = GST_STATE_PENDING (basesink);
    post_pending = pending;

    switch (pending) {
      case GST_STATE_PLAYING:
        basesink->need_preroll = FALSE;
        post_playing = TRUE;
        /* post PAUSED too when we were READY */
        if (current == GST_STATE_READY) {
          post_paused = TRUE;
        }
        break;
      case GST_STATE_PAUSED:
        basesink->need_preroll = TRUE;
        post_paused = TRUE;
        post_pending = GST_STATE_VOID_PENDING;
        break;
      case GST_STATE_READY:
        goto stopping;
      default:
        break;
    }

    if (pending != GST_STATE_VOID_PENDING) {
      GST_STATE (basesink) = pending;
      GST_STATE_NEXT (basesink) = GST_STATE_VOID_PENDING;
      GST_STATE_PENDING (basesink) = GST_STATE_VOID_PENDING;
      GST_STATE_RETURN (basesink) = GST_STATE_CHANGE_SUCCESS;
    }
    GST_OBJECT_UNLOCK (basesink);

    if (post_paused) {
      message = gst_message_new_state_changed (GST_OBJECT_CAST (basesink),
          current, next, post_pending);
      gst_element_post_message (GST_ELEMENT_CAST (basesink), message);
    }
    if (post_playing) {
      message = gst_message_new_state_changed (GST_OBJECT_CAST (basesink),
          next, pending, GST_STATE_VOID_PENDING);
      gst_element_post_message (GST_ELEMENT_CAST (basesink), message);
    }
    /* and mark dirty */
    if (post_paused || post_playing) {
      gst_element_post_message (GST_ELEMENT_CAST (basesink),
          gst_message_new_state_dirty (GST_OBJECT_CAST (basesink)));
    }

    GST_STATE_BROADCAST (basesink);
  }
  return TRUE;

stopping:
  {
    /* app is going to READY */
    GST_OBJECT_UNLOCK (basesink);
    return FALSE;
  }
}

/* with STREAM_LOCK */
static GstFlowReturn
gst_base_sink_handle_object (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj)
{
  gint length;
  gboolean have_event;
  GstFlowReturn ret;

  GST_PAD_PREROLL_LOCK (pad);
  /* push object on the queue */
  GST_DEBUG_OBJECT (basesink, "push %p on preroll_queue", obj);
  g_queue_push_tail (basesink->preroll_queue, obj);

  have_event = GST_IS_EVENT (obj);
  if (have_event) {
    GstEvent *event = GST_EVENT (obj);

    switch (GST_EVENT_TYPE (obj)) {
      case GST_EVENT_EOS:
        basesink->preroll_queued++;
        basesink->eos = TRUE;
        basesink->eos_queued = TRUE;
        break;
      case GST_EVENT_NEWSEGMENT:
      {
        gboolean update;
        gdouble rate;
        GstFormat format;
        gint64 start;
        gint64 stop;
        gint64 time;

        /* the newsegment event is needed to bring the buffer timestamps to the
         * stream time and to drop samples outside of the playback segment. */
        gst_event_parse_new_segment (event, &update, &rate, &format,
            &start, &stop, &time);

        basesink->have_newsegment = TRUE;

        gst_segment_set_newsegment (&basesink->segment, update, rate, format,
            start, stop, time);

        GST_DEBUG_OBJECT (basesink,
            "received NEWSEGMENT %" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT ", time %" GST_TIME_FORMAT ", accum %"
            GST_TIME_FORMAT,
            GST_TIME_ARGS (basesink->segment.start),
            GST_TIME_ARGS (basesink->segment.stop),
            GST_TIME_ARGS (basesink->segment.time),
            GST_TIME_ARGS (basesink->segment.accum));
        break;
      }
      default:
        break;
    }
    basesink->events_queued++;
  } else {
    GstBuffer *buf = GST_BUFFER (obj);

    if (!basesink->have_newsegment) {
      GST_ELEMENT_WARNING (basesink, STREAM, FAILED,
          (_("Internal data flow problem.")),
          ("Received buffer without a new-segment. Cannot sync to clock."));
      basesink->have_newsegment = TRUE;
      /* this means this sink will not be able to sync to the clock */
      basesink->segment.start = -1;
      basesink->segment.stop = -1;
    }

    /* check if the buffer needs to be dropped */
    if (TRUE) {
      GstClockTime start = -1, end = -1;

      /* we don't use the subclassed method as it may not return
       * valid values for our purpose here */
      gst_base_sink_get_times (basesink, buf, &start, &end);

      GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
          ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
          GST_TIME_ARGS (end));

      if (GST_CLOCK_TIME_IS_VALID (start) &&
          (basesink->segment.format == GST_FORMAT_TIME)) {
        if (!gst_segment_clip (&basesink->segment, GST_FORMAT_TIME,
                (gint64) start, (gint64) end, NULL, NULL))
          goto dropping;
      }
    }
    basesink->preroll_queued++;
    basesink->buffers_queued++;
  }

  GST_DEBUG_OBJECT (basesink,
      "now %d preroll, %d buffers, %d events on queue",
      basesink->preroll_queued,
      basesink->buffers_queued, basesink->events_queued);

  /* check if we are prerolling */
  if (!basesink->need_preroll)
    goto no_preroll;

  /* there is a buffer queued */
  if (basesink->buffers_queued == 1) {
    GST_DEBUG_OBJECT (basesink, "do preroll %p", obj);

    /* if it's a buffer, we need to call the preroll method */
    if (GST_IS_BUFFER (obj)) {
      GstBaseSinkClass *bclass;
      GstBuffer *buf = GST_BUFFER (obj);

      GST_DEBUG_OBJECT (basesink, "preroll buffer %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

      bclass = GST_BASE_SINK_GET_CLASS (basesink);
      if (bclass->preroll)
        if ((ret = bclass->preroll (basesink, buf)) != GST_FLOW_OK)
          goto preroll_failed;
    }
  }
  length = basesink->preroll_queued;
  GST_DEBUG_OBJECT (basesink, "prerolled length %d", length);

  if (length == 1) {

    basesink->have_preroll = TRUE;

    /* commit state */
    if (!gst_base_sink_commit_state (basesink))
      goto stopping;

    GST_OBJECT_LOCK (pad);
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
      goto flushing;
    GST_OBJECT_UNLOCK (pad);

    /* it is possible that commiting the state made us go to PLAYING
     * now in which case we don't need to block anymore. */
    if (!basesink->need_preroll)
      goto no_preroll;

    length = basesink->preroll_queued;

    /* FIXME: a pad probe could have made us lose the buffer, according
     * to one of the python tests */
    if (length == 0) {
      GST_ERROR_OBJECT (basesink,
          "preroll_queued dropped from 1 to 0 while committing state change");
    }
    g_assert (length <= 1);
  }

  /* see if we need to block now. We cannot block on events, only
   * on buffers, the reason is that events can be sent from the
   * application thread and we don't want to block there. */
  if (length > basesink->preroll_queue_max_len && !have_event) {
    /* block until the state changes, or we get a flush, or something */
    GST_DEBUG_OBJECT (basesink, "waiting to finish preroll");
    GST_PAD_PREROLL_WAIT (pad);
    GST_DEBUG_OBJECT (basesink, "done preroll");
    GST_OBJECT_LOCK (pad);
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
      goto flushing;
    GST_OBJECT_UNLOCK (pad);
  }
  GST_PAD_PREROLL_UNLOCK (pad);

  return GST_FLOW_OK;

no_preroll:
  {
    GST_DEBUG_OBJECT (basesink, "no preroll needed");
    /* maybe it was another sink that blocked in preroll, need to check for
       buffers to drain */
    basesink->have_preroll = FALSE;
    ret = gst_base_sink_preroll_queue_empty (basesink, pad);
    GST_PAD_PREROLL_UNLOCK (pad);

    return ret;
  }
dropping:
  {
    GstBuffer *buf;

    GST_DEBUG_OBJECT (basesink, "dropping buffer");
    buf = GST_BUFFER (g_queue_pop_tail (basesink->preroll_queue));

    gst_buffer_unref (buf);
    GST_PAD_PREROLL_UNLOCK (pad);

    return GST_FLOW_OK;
  }
flushing:
  {
    GST_OBJECT_UNLOCK (pad);
    gst_base_sink_preroll_queue_flush (basesink, pad);
    GST_PAD_PREROLL_UNLOCK (pad);
    GST_DEBUG_OBJECT (basesink, "pad is flushing");

    return GST_FLOW_WRONG_STATE;
  }
stopping:
  {
    GST_PAD_PREROLL_UNLOCK (pad);
    GST_DEBUG_OBJECT (basesink, "stopping");

    return GST_FLOW_WRONG_STATE;
  }
preroll_failed:
  {
    GST_DEBUG_OBJECT (basesink, "preroll failed");
    gst_base_sink_preroll_queue_flush (basesink, pad);
    GST_PAD_PREROLL_UNLOCK (pad);

    GST_DEBUG_OBJECT (basesink, "abort state");
    gst_element_abort_state (GST_ELEMENT (basesink));

    return ret;
  }
}

static gboolean
gst_base_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseSink *basesink;
  gboolean result = TRUE;
  GstBaseSinkClass *bclass;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  GST_DEBUG_OBJECT (basesink, "event %p", event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret;

      /* EOS also finishes the preroll */
      ret =
          gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (event));
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      GstFlowReturn ret;

      ret =
          gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (event));
      break;
    }
    case GST_EVENT_FLUSH_START:
      /* make sure we are not blocked on the clock also clear any pending
       * eos state. */
      if (bclass->event)
        bclass->event (basesink, event);

      GST_OBJECT_LOCK (basesink);
      basesink->flushing = TRUE;
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }
      GST_OBJECT_UNLOCK (basesink);

      GST_PAD_PREROLL_LOCK (pad);
      /* we need preroll after the flush */
      GST_DEBUG_OBJECT (basesink, "flushing, need preroll after flush");
      basesink->need_preroll = TRUE;
      /* unlock from a possible state change/preroll */
      gst_base_sink_preroll_queue_flush (basesink, pad);
      GST_PAD_PREROLL_UNLOCK (pad);

      /* and we need to commit our state again on the next
       * prerolled buffer */
      GST_PAD_STREAM_LOCK (pad);
      gst_element_lost_state (GST_ELEMENT (basesink));
      GST_PAD_STREAM_UNLOCK (pad);
      GST_DEBUG_OBJECT (basesink, "event unref %p %p", basesink, event);
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (bclass->event)
        bclass->event (basesink, event);

      /* now we are completely unblocked and the _chain method
       * will return */
      GST_OBJECT_LOCK (basesink);
      basesink->flushing = FALSE;
      GST_OBJECT_UNLOCK (basesink);
      /* we need new segment info after the flush. */
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);

      GST_DEBUG_OBJECT (basesink, "event unref %p %p", basesink, event);
      gst_event_unref (event);
      break;
    default:
      gst_event_unref (event);
      break;
  }
  gst_object_unref (basesink);

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

    /* get duration to calculate end time */
    duration = GST_BUFFER_DURATION (buffer);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      *end = timestamp + duration;
    }
    *start = timestamp;
  }
}

/* with STREAM_LOCK and LOCK*/
static GstClockReturn
gst_base_sink_wait (GstBaseSink * basesink, GstClockTime time)
{
  GstClockReturn ret;
  GstClockID id;

  /* no need to attempt a clock wait if we are flushing */
  if (basesink->flushing) {
    return GST_CLOCK_UNSCHEDULED;
  }

  /* clock_id should be NULL outside of this function */
  g_assert (basesink->clock_id == NULL);
  g_assert (GST_CLOCK_TIME_IS_VALID (time));

  id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (basesink), time);

  basesink->clock_id = id;
  /* release the object lock while waiting */
  GST_OBJECT_UNLOCK (basesink);

  ret = gst_clock_id_wait (id, NULL);

  GST_OBJECT_LOCK (basesink);
  gst_clock_id_unref (id);
  basesink->clock_id = NULL;

  return ret;
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
static GstClockReturn
gst_base_sink_do_sync (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstClockReturn result = GST_CLOCK_OK;
  GstClockTime start, end;
  gint64 cstart, cend;
  GstBaseSinkClass *bclass;

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  start = end = -1;
  if (bclass->get_times)
    bclass->get_times (basesink, buffer, &start, &end);

  GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
      ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  /* if we don't have a timestamp, we don't sync */
  if (!GST_CLOCK_TIME_IS_VALID (start)) {
    GST_DEBUG_OBJECT (basesink, "start not valid");
    goto done;
  }

  if (basesink->segment.format == GST_FORMAT_TIME) {
    /* save last times seen. */
    if (GST_CLOCK_TIME_IS_VALID (end))
      gst_segment_set_last_stop (&basesink->segment, GST_FORMAT_TIME,
          (gint64) end);
    else
      gst_segment_set_last_stop (&basesink->segment, GST_FORMAT_TIME,
          (gint64) start);

    /* clip */
    if (!gst_segment_clip (&basesink->segment, GST_FORMAT_TIME,
            (gint64) start, (gint64) end, &cstart, &cend))
      goto out_of_segment;
  } else {
    /* no clipping for formats different from GST_FORMAT_TIME */
    cstart = start;
    cend = end;
  }

  if (!basesink->sync) {
    GST_DEBUG_OBJECT (basesink, "no need to sync");
    goto done;
  }

  /* now do clocking */
  if (GST_ELEMENT_CLOCK (basesink)
      && ((basesink->segment.format == GST_FORMAT_TIME)
          || (basesink->segment.accum == 0))) {
    GstClockTime base_time;
    GstClockTimeDiff stream_start, stream_end;

    stream_start =
        gst_segment_to_running_time (&basesink->segment, GST_FORMAT_TIME,
        cstart);
    stream_end =
        gst_segment_to_running_time (&basesink->segment, GST_FORMAT_TIME, cend);

    GST_OBJECT_LOCK (basesink);

    base_time = GST_ELEMENT_CAST (basesink)->base_time;

    GST_LOG_OBJECT (basesink,
        "waiting for clock, base time %" GST_TIME_FORMAT
        " stream_start %" GST_TIME_FORMAT,
        GST_TIME_ARGS (base_time), GST_TIME_ARGS (stream_start));

    /* also save end_time of this buffer so that we can wait
     * to signal EOS */
    if (GST_CLOCK_TIME_IS_VALID (stream_end))
      basesink->end_time = stream_end + base_time;
    else
      basesink->end_time = GST_CLOCK_TIME_NONE;

    result = gst_base_sink_wait (basesink, stream_start + base_time);

    GST_OBJECT_UNLOCK (basesink);

    GST_LOG_OBJECT (basesink, "clock entry done: %d", result);
  } else {
    GST_DEBUG_OBJECT (basesink, "no clock, not syncing");
  }

done:
  return result;

out_of_segment:
  {
    GST_LOG_OBJECT (basesink, "buffer skipped, not in segment");
    return GST_CLOCK_UNSCHEDULED;
  }
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
      GST_OBJECT_LOCK (basesink);
      if (GST_ELEMENT_CLOCK (basesink)) {
        /* wait for last buffer to finish if we have a valid end time */
        if (GST_CLOCK_TIME_IS_VALID (basesink->end_time)) {
          gst_base_sink_wait (basesink, basesink->end_time);
          basesink->end_time = GST_CLOCK_TIME_NONE;
        }
      }
      GST_OBJECT_UNLOCK (basesink);
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
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      /* if we are still EOS, we can post the EOS message */
      if (basesink->eos) {
        /* ok, now we can post the message */
        GST_DEBUG_OBJECT (basesink, "Now posting EOS");
        gst_element_post_message (GST_ELEMENT_CAST (basesink),
            gst_message_new_eos (GST_OBJECT_CAST (basesink)));
        basesink->eos_queued = FALSE;
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (basesink, "event unref %p %p", basesink, event);
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
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockReturn status;

  status = gst_base_sink_do_sync (basesink, buf);
  switch (status) {
    case GST_CLOCK_EARLY:
      GST_DEBUG_OBJECT (basesink, "buffer too late!, rendering anyway");
      /* fallthrough for now */
    case GST_CLOCK_OK:
    {
      GstBaseSinkClass *bclass;

      bclass = GST_BASE_SINK_GET_CLASS (basesink);
      if (bclass->render)
        ret = bclass->render (basesink, buf);
      break;
    }
    default:
      GST_DEBUG_OBJECT (basesink, "clock returned %d, not rendering", status);
      break;
  }

  GST_DEBUG_OBJECT (basesink, "buffer unref after render %p", basesink, buf);
  gst_buffer_unref (buf);

  return ret;
}

static GstFlowReturn
gst_base_sink_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseSink *basesink;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  if (!(basesink->pad_mode == GST_ACTIVATE_PUSH)) {
    GST_OBJECT_LOCK (pad);
    g_warning ("Push on pad %s:%s, but it was not activated in push mode",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    result = GST_FLOW_UNEXPECTED;
    goto done;
  }

  result =
      gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT_CAST (buf));

done:
  gst_object_unref (basesink);

  return result;
}

static void
gst_base_sink_loop (GstPad * pad)
{
  GstBaseSink *basesink;
  GstBuffer *buf = NULL;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  g_assert (basesink->pad_mode == GST_ACTIVATE_PULL);

  result = gst_pad_pull_range (pad, basesink->offset, DEFAULT_SIZE, &buf);
  if (result != GST_FLOW_OK)
    goto paused;

  result = gst_base_sink_handle_object (basesink, pad, GST_MINI_OBJECT (buf));
  if (result != GST_FLOW_OK)
    goto paused;

  gst_object_unref (basesink);

  /* default */
  return;

paused:
  {
    gst_base_sink_event (pad, gst_event_new_eos ());
    gst_object_unref (basesink);
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
  GST_PAD_PREROLL_LOCK (pad);
  GST_OBJECT_LOCK (basesink);
  if (basesink->clock_id) {
    gst_clock_id_unschedule (basesink->clock_id);
  }
  GST_OBJECT_UNLOCK (basesink);

  /* unlock any subclasses */
  if (bclass->unlock)
    bclass->unlock (basesink);

  /* flush out the data thread if it's locked in finish_preroll */
  GST_DEBUG_OBJECT (basesink,
      "flushing out data thread, need preroll to FALSE");
  basesink->need_preroll = FALSE;
  gst_base_sink_preroll_queue_flush (basesink, pad);
  GST_PAD_PREROLL_SIGNAL (pad);
  GST_PAD_PREROLL_UNLOCK (pad);

  /* step 2, make sure streaming finishes */
  result = gst_pad_stop_task (pad);

  return result;
}

static gboolean
gst_base_sink_activate (GstPad * pad)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (basesink, "Trying pull mode first");

  if (basesink->can_activate_pull && gst_pad_check_pull_range (pad)
      && gst_pad_activate_pull (pad, TRUE)) {
    GST_DEBUG_OBJECT (basesink, "Success activating pull mode");
    result = TRUE;
  } else {
    GST_DEBUG_OBJECT (basesink, "Falling back to push mode");
    if (gst_pad_activate_push (pad, TRUE)) {
      GST_DEBUG_OBJECT (basesink, "Success activating push mode");
      result = TRUE;
    }
  }

  if (!result) {
    GST_WARNING_OBJECT (basesink, "Could not activate pad in either mode");
  }

  gst_object_unref (basesink);

  return result;
}

static gboolean
gst_base_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result;
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  if (active) {
    if (!basesink->can_activate_push) {
      result = FALSE;
      basesink->pad_mode = GST_ACTIVATE_NONE;
    } else {
      result = TRUE;
      basesink->pad_mode = GST_ACTIVATE_PUSH;
    }
  } else {
    if (G_UNLIKELY (basesink->pad_mode != GST_ACTIVATE_PUSH)) {
      g_warning ("Internal GStreamer activation error!!!");
      result = FALSE;
    } else {
      result = gst_base_sink_deactivate (basesink, pad);
      basesink->pad_mode = GST_ACTIVATE_NONE;
    }
  }

  gst_object_unref (basesink);

  return result;
}

/* this won't get called until we implement an activate function */
static gboolean
gst_base_sink_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  if (active) {
    if (!basesink->can_activate_pull) {
      result = FALSE;
      basesink->pad_mode = GST_ACTIVATE_NONE;
    } else {
      GstPad *peer = gst_pad_get_peer (pad);

      if (G_UNLIKELY (peer == NULL)) {
        g_warning ("Trying to activate pad in pull mode, but no peer");
        result = FALSE;
        basesink->pad_mode = GST_ACTIVATE_NONE;
      } else {
        if (gst_pad_activate_pull (peer, TRUE)) {
          basesink->have_newsegment = TRUE;
          gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);

          /* set the pad mode before starting the task so that it's in the
             correct state for the new thread... */
          basesink->pad_mode = GST_ACTIVATE_PULL;
          result =
              gst_pad_start_task (pad, (GstTaskFunction) gst_base_sink_loop,
              pad);
          /* but if starting the thread fails, set it back */
          if (!result)
            basesink->pad_mode = GST_ACTIVATE_NONE;
        } else {
          GST_DEBUG_OBJECT (pad, "Failed to activate peer in pull mode");
          result = FALSE;
          basesink->pad_mode = GST_ACTIVATE_NONE;
        }
        gst_object_unref (peer);
      }
    }
  } else {
    if (G_UNLIKELY (basesink->pad_mode != GST_ACTIVATE_PULL)) {
      g_warning ("Internal GStreamer activation error!!!");
      result = FALSE;
    } else {
      basesink->have_newsegment = FALSE;
      result = gst_base_sink_deactivate (basesink, pad);
      basesink->pad_mode = GST_ACTIVATE_NONE;
    }
  }

  gst_object_unref (basesink);

  return result;
}

static gboolean
gst_base_sink_send_event (GstElement * element, GstEvent * event)
{
  GstPad *pad;
  GstBaseSink *basesink = GST_BASE_SINK (element);
  gboolean result;

  GST_OBJECT_LOCK (element);
  pad = basesink->sinkpad;
  gst_object_ref (pad);
  GST_OBJECT_UNLOCK (element);

  result = gst_pad_push_event (pad, event);

  gst_object_unref (pad);

  return result;
}

static gboolean
gst_base_sink_peer_query (GstBaseSink * sink, GstQuery * query)
{
  GstPad *peer;
  gboolean res = FALSE;

  if ((peer = gst_pad_get_peer (sink->sinkpad))) {
    res = gst_pad_query (peer, query);
    gst_object_unref (peer);
  }
  return res;
}

static gboolean
gst_base_sink_get_position (GstBaseSink * basesink, GstFormat format,
    gint64 * cur)
{
  GstClock *clock;
  gboolean res = FALSE;

  switch (format) {
    case GST_FORMAT_TIME:
    {
      /* we can answer time format */
      GST_OBJECT_LOCK (basesink);
      if ((clock = GST_ELEMENT_CLOCK (basesink))) {
        GstClockTime now;
        gint64 time;

        gst_object_ref (clock);
        GST_OBJECT_UNLOCK (basesink);

        now = gst_clock_get_time (clock);

        GST_OBJECT_LOCK (basesink);
        if (GST_CLOCK_TIME_IS_VALID (basesink->segment.time))
          time = basesink->segment.time;
        else
          time = 0;

        *cur = now - GST_ELEMENT_CAST (basesink)->base_time -
            basesink->segment.accum + time;

        GST_DEBUG_OBJECT (basesink,
            "now %" GST_TIME_FORMAT " + segment_time %" GST_TIME_FORMAT " = %"
            GST_TIME_FORMAT, GST_TIME_ARGS (now),
            GST_TIME_ARGS (time), GST_TIME_ARGS (*cur));

        gst_object_unref (clock);

        res = TRUE;
      }
      GST_OBJECT_UNLOCK (basesink);
    }
    default:
      break;
  }
  return res;
}

static gboolean
gst_base_sink_query (GstElement * element, GstQuery * query)
{
  gboolean res = FALSE;

  GstBaseSink *basesink = GST_BASE_SINK (element);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 cur = 0;
      GstFormat format;
      gboolean eos;

      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      eos = basesink->eos;
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);

      if (eos) {
        res = gst_base_sink_peer_query (basesink, query);
      } else {
        gst_query_parse_position (query, &format, NULL);

        GST_DEBUG_OBJECT (basesink, "current position format %d", format);

        if ((res = gst_base_sink_get_position (basesink, format, &cur))) {
          gst_query_set_position (query, format, cur);
        } else {
          res = gst_base_sink_peer_query (basesink, query);
        }
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_base_sink_peer_query (basesink, query);
      break;
    case GST_QUERY_LATENCY:
      break;
    case GST_QUERY_JITTER:
      break;
    case GST_QUERY_RATE:
      //gst_query_set_rate (query, basesink->segment_rate);
      res = TRUE;
      break;
    case GST_QUERY_SEGMENT:
    {
      /* FIXME, bring start/stop to stream time */
      gst_query_set_segment (query, basesink->segment.rate,
          GST_FORMAT_TIME, basesink->segment.start, basesink->segment.stop);
      break;
    }
    case GST_QUERY_SEEKING:
    case GST_QUERY_CONVERT:
    case GST_QUERY_FORMATS:
    default:
      res = gst_base_sink_peer_query (basesink, query);
      break;
  }
  return res;
}

static GstStateChangeReturn
gst_base_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseSink *basesink = GST_BASE_SINK (element);
  GstBaseSinkClass *bclass;

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (bclass->start)
        if (!bclass->start (basesink))
          goto start_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* need to complete preroll before this state change completes, there
       * is no data flow in READY so we can safely assume we need to preroll. */
      basesink->offset = 0;
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      basesink->have_preroll = FALSE;
      GST_DEBUG_OBJECT (basesink, "READY to PAUSED, need preroll to FALSE");
      basesink->need_preroll = TRUE;
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
      basesink->have_newsegment = FALSE;
      ret = GST_STATE_CHANGE_ASYNC;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      /* no preroll needed */
      basesink->need_preroll = FALSE;

      /* if we have EOS, we should empty the queue now as there will
       * be no more data received in the chain function.
       * FIXME, this could block the state change function too long when
       * we are pushing and syncing the buffers, better start a new
       * thread to do this. */
      if (basesink->eos) {
        gboolean do_eos = !basesink->eos_queued;

        gst_base_sink_preroll_queue_empty (basesink, basesink->sinkpad);

        /* need to post EOS message here if it was not in the preroll queue we
         * just emptied. */
        if (do_eos) {
          GST_DEBUG_OBJECT (basesink, "Now posting EOS");
          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_eos (GST_OBJECT_CAST (basesink)));
        }
      } else if (!basesink->have_preroll) {
        /* queue a commit_state */
        basesink->need_preroll = TRUE;
        GST_DEBUG_OBJECT (basesink,
            "PAUSED to PLAYING, !eos, !have_preroll, need preroll to TRUE");
        ret = GST_STATE_CHANGE_ASYNC;
        /* we know it's not waiting, no need to signal */
      } else {
        GST_DEBUG_OBJECT (basesink,
            "PAUSED to PLAYING, !eos, have_preroll, need preroll to FALSE");
        /* now let it play */
        GST_PAD_PREROLL_SIGNAL (basesink->sinkpad);
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    default:
      break;
  }

  {
    GstStateChangeReturn bret;

    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (bret == GST_STATE_CHANGE_FAILURE)
      goto activate_failed;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
      GstBaseSinkClass *bclass;

      bclass = GST_BASE_SINK_GET_CLASS (basesink);

      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      GST_OBJECT_LOCK (basesink);
      /* unlock clock wait if any */
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }
      GST_OBJECT_UNLOCK (basesink);

      /* unlock any subclasses */
      if (bclass->unlock)
        bclass->unlock (basesink);

      /* if we don't have a preroll buffer and we have not received EOS,
       * we need to wait for a preroll */
      GST_DEBUG_OBJECT (basesink, "have_preroll: %d, EOS: %d",
          basesink->have_preroll, basesink->eos);
      if (!basesink->have_preroll && !basesink->eos
          && GST_STATE_PENDING (basesink) == GST_STATE_PAUSED) {
        GST_DEBUG_OBJECT (basesink, "PLAYING to PAUSED, need preroll to TRUE");
        basesink->need_preroll = TRUE;
        ret = GST_STATE_CHANGE_ASYNC;
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
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
    GST_DEBUG_OBJECT (basesink, "failed to start");
    return GST_STATE_CHANGE_FAILURE;
  }
activate_failed:
  {
    GST_DEBUG_OBJECT (basesink,
        "element failed to change states -- activation problem?");
    return GST_STATE_CHANGE_FAILURE;
  }
}
