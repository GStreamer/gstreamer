/* GStreamer
 * Copyright (C) 2005,2006 Wim Taymans <wim@fluendo.com>
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
 * #GstBaseSink is the base class for sink elements in GStreamer, such as
 * xvimagesink or filesink. It is a layer on top of #GstElement that provides a
 * simplified interface to plugin writers. #GstBaseSink handles many details for
 * you, for example preroll, clock synchronization, state changes, activation in
 * push or pull mode, and queries. In most cases, when writing sink elements,
 * there is no need to implement class methods from #GstElement or to set
 * functions on pads, because the #GstBaseSink infrastructure is sufficient.
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
 * #GstBaseSink will handle the prerolling correctly. This means that it will
 * return GST_STATE_CHANGE_ASYNC from a state change to PAUSED until the first buffer
 * arrives in this element. The base class will call the GstBaseSink::preroll
 * vmethod with this preroll buffer and will then commit the state change to
 * PAUSED. 
 *
 * When the element is set to PLAYING, #GstBaseSink will synchronize on the clock
 * using the times returned from ::get_times. If this function returns
 * #GST_CLOCK_TIME_NONE for the start time, no synchronisation will be done.
 * Synchronisation can be disabled entirely by setting the object "sync" property 
 * to FALSE.
 *
 * After synchronisation the virtual method #GstBaseSink::render will be called.
 * Subclasses should minimally implement this method.
 *
 * Since 0.10.3 subclasses that synchronize on the clock in the ::render method
 * are supported as well. These classes typically receive a buffer in the render
 * method and can then potentially block on the clock while rendering. A typical
 * example would be an audiosink. 
 *
 * Upon receiving the EOS event in the PLAYING state, #GstBaseSink will wait for 
 * the clock to reach the time indicated by the stop time of the last ::get_times 
 * call before posting an EOS message. When the element receives EOS in PAUSED,
 * preroll completes, the event is queued and an EOS message is posted when going 
 * to PLAYING.
 * 
 * #GstBaseSink will internally use the GST_EVENT_NEW_SEGMENT events to schedule
 * synchronisation and clipping of buffers. Buffers that fall completely outside
 * of the segment are dropped. Buffers that fall partially in the segment are 
 * rendered (and prerolled), subclasses should do any subbuffer clipping themselves
 * when needed.
 * 
 * #GstBaseSink will by default report the current playback position in 
 * GST_FORMAT_TIME based on the current clock time and segment information. 
 * If the element is EOS, PAUSED or no clock has been set on the element, the 
 * query will be forwarded upstream.
 *
 * The ::set_caps function will be called when the subclass should configure itself
 * to precess a specific media type.
 * 
 * The ::start and ::stop virtual methods will be called when resources should be
 * allocated. Any ::preroll, ::render  and ::set_caps function will be called
 * between the ::start and ::stop calls. 
 *
 * The ::event virtual method will be called when an event is received by 
 * #GstBaseSink. Normally this method should only be overriden by very specific
 * elements such as file sinks that need to handle the newsegment event specially.
 * 
 * #GstBaseSink provides an overridable ::buffer_alloc function that can be used
 * by specific sinks that want to do reverse negotiation or want to provided 
 * hardware accelerated buffers for downstream elements.
 *
 * The ::unlock method is called when the elements should unblock any blocking
 * operations they perform in the ::render method. This is mostly usefull when
 * the ::render method performs a blocking write on a file descripter.
 *
 * Last reviewed on 2006-01-30 (0.10.3)
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
static gboolean gst_base_sink_set_flushing (GstBaseSink * basesink,
    GstPad * pad, gboolean flushing);

static GstStateChangeReturn gst_base_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_base_sink_chain (GstPad * pad, GstBuffer * buffer);
static void gst_base_sink_loop (GstPad * pad);
static gboolean gst_base_sink_activate (GstPad * pad);
static gboolean gst_base_sink_activate_push (GstPad * pad, gboolean active);
static gboolean gst_base_sink_activate_pull (GstPad * pad, gboolean active);
static gboolean gst_base_sink_event (GstPad * pad, GstEvent * event);

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
   * upstream element, ie, the BUFFER_SIZE event. */
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
  gst_element_add_pad (GST_ELEMENT_CAST (basesink), basesink->sinkpad);

  basesink->pad_mode = GST_ACTIVATE_NONE;
  basesink->preroll_queue = g_queue_new ();
  basesink->abidata.ABI.clip_segment = gst_segment_new ();

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
  gst_segment_free (basesink->abidata.ABI.clip_segment);

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
      GST_OBJECT_LOCK (sink);
      sink->sync = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (sink);
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

  switch (prop_id) {
    case PROP_PREROLL_QUEUE_LEN:
      GST_PAD_PREROLL_LOCK (sink->sinkpad);
      g_value_set_uint (value, sink->preroll_queue_max_len);
      GST_PAD_PREROLL_UNLOCK (sink->sinkpad);
      break;
    case PROP_SYNC:
      GST_OBJECT_LOCK (sink);
      g_value_set_boolean (value, sink->sync);
      GST_OBJECT_UNLOCK (sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

/* with PREROLL_LOCK, STREAM_LOCK */
static void
gst_base_sink_preroll_queue_flush (GstBaseSink * basesink, GstPad * pad)
{
  GstMiniObject *obj;

  GST_DEBUG_OBJECT (basesink, "flushing queue %p", basesink);
  while ((obj = g_queue_pop_head (basesink->preroll_queue))) {
    GST_DEBUG_OBJECT (basesink, "popped %p", obj);
    gst_mini_object_unref (obj);
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

/* with STREAM_LOCK, configures given segment with the event information. */
static void
gst_base_sink_configure_segment (GstBaseSink * basesink, GstPad * pad,
    GstEvent * event, GstSegment * segment)
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

  GST_OBJECT_LOCK (basesink);

  if (segment->format != format)
    gst_segment_init (segment, format);

  gst_segment_set_newsegment (segment, update, rate, format, start, stop, time);

  GST_DEBUG_OBJECT (basesink,
      "configured NEWSEGMENT %" GST_TIME_FORMAT " -- %"
      GST_TIME_FORMAT ", time %" GST_TIME_FORMAT ", accum %"
      GST_TIME_FORMAT,
      GST_TIME_ARGS (segment->start),
      GST_TIME_ARGS (segment->stop),
      GST_TIME_ARGS (segment->time), GST_TIME_ARGS (segment->accum));
  GST_OBJECT_UNLOCK (basesink);
}

/* with PREROLL_LOCK, STREAM_LOCK */
static gboolean
gst_base_sink_commit_state (GstBaseSink * basesink)
{
  /* commit state and proceed to next pending state */
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
      GST_DEBUG_OBJECT (basesink, "commiting state to PLAYING");
      basesink->need_preroll = FALSE;
      post_playing = TRUE;
      /* post PAUSED too when we were READY */
      if (current == GST_STATE_READY) {
        post_paused = TRUE;
      }
      break;
    case GST_STATE_PAUSED:
      GST_DEBUG_OBJECT (basesink, "commiting state to PAUSED");
      post_paused = TRUE;
      post_pending = GST_STATE_VOID_PENDING;
      break;
    case GST_STATE_READY:
    case GST_STATE_NULL:
      goto stopping;
    case GST_STATE_VOID_PENDING:
      goto nothing_pending;
    default:
      break;
  }

  GST_STATE (basesink) = pending;
  GST_STATE_NEXT (basesink) = GST_STATE_VOID_PENDING;
  GST_STATE_PENDING (basesink) = GST_STATE_VOID_PENDING;
  GST_STATE_RETURN (basesink) = GST_STATE_CHANGE_SUCCESS;
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

  return TRUE;

nothing_pending:
  {
    GST_DEBUG_OBJECT (basesink, "nothing to commit");
    GST_OBJECT_UNLOCK (basesink);
    return TRUE;
  }
stopping:
  {
    /* app is going to READY */
    GST_DEBUG_OBJECT (basesink, "stopping");
    basesink->need_preroll = FALSE;
    basesink->flushing = TRUE;
    GST_OBJECT_UNLOCK (basesink);
    return FALSE;
  }
}


/* with STREAM_LOCK, PREROLL_LOCK
 *
 * Returns TRUE if the object needs synchronisation and takes therefore
 * part in prerolling.
 *
 * start and stop cannot be NULL.
 */
static gboolean
gst_base_sink_get_sync_times (GstBaseSink * basesink, GstMiniObject * obj,
    GstClockTime * start, GstClockTime * stop)
{
  GstClockTime sstart, sstop;
  gint64 cstart, cstop;
  GstBaseSinkClass *bclass;
  GstBuffer *buffer;

  if (G_UNLIKELY (GST_IS_EVENT (obj))) {
    GstEvent *event = GST_EVENT_CAST (obj);

    switch (GST_EVENT_TYPE (event)) {
        /* EOS event needs syncing */
      case GST_EVENT_EOS:
        *start = basesink->end_time;
        *stop = -1;
        return TRUE;
        /* other events do not need syncing */
        /* FIXME, maybe NEWSEGMENT might need synchronisation
         * since the POSITION query depends on accumulated times and
         * we cannot accumulate the current segment before the previous
         * one completed.
         */
      default:
        return FALSE;
    }
  }

  /* else do buffer sync code */
  buffer = GST_BUFFER_CAST (obj);

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  sstart = sstop = -1;
  if (bclass->get_times)
    bclass->get_times (basesink, buffer, &sstart, &sstop);

  GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
      ", stop: %" GST_TIME_FORMAT, GST_TIME_ARGS (sstart),
      GST_TIME_ARGS (sstop));

  if (G_LIKELY (basesink->segment.format == GST_FORMAT_TIME)) {
    /* clip */
    if (G_UNLIKELY (!gst_segment_clip (&basesink->segment, GST_FORMAT_TIME,
                (gint64) sstart, (gint64) sstop, &cstart, &cstop)))
      goto out_of_segment;

    if (G_UNLIKELY (sstart != cstart || sstop != cstop)) {
      GST_DEBUG_OBJECT (basesink, "clipped to: start %" GST_TIME_FORMAT
          ", stop: %" GST_TIME_FORMAT, GST_TIME_ARGS (cstart),
          GST_TIME_ARGS (cstop));
    }

    /* save last valid times seen. */
    if (GST_CLOCK_TIME_IS_VALID (cstop))
      gst_segment_set_last_stop (&basesink->segment, GST_FORMAT_TIME,
          (gint64) cstop);
    else
      gst_segment_set_last_stop (&basesink->segment, GST_FORMAT_TIME,
          (gint64) cstart);
  } else {
    if (basesink->segment.accum == 0) {
      /* no clipping for formats different from GST_FORMAT_TIME */
      cstart = sstart;
      cstop = sstop;
    } else {
      cstart = -1;
      cstop = -1;
    }
  }

  *start =
      gst_segment_to_running_time (&basesink->segment, GST_FORMAT_TIME, cstart);
  *stop =
      gst_segment_to_running_time (&basesink->segment, GST_FORMAT_TIME, cstop);

  /* buffers always need syncing and preroll */
  return TRUE;

  /* special cases */
out_of_segment:
  {
    /* should not happen, we return FALSE so that we don't try to
     * sync on it. */
    GST_ELEMENT_WARNING (basesink, STREAM, FAILED,
        (NULL), ("unexpected buffer out of segment found."));
    GST_LOG_OBJECT (basesink, "buffer skipped, not in segment");
    return FALSE;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK
 *
 * Waits for the clock to reach @time. If @time is not valid, no
 * synchronisation is done. 
 * If synchronisation is disabled in the element or there is no
 * clock, no synchronisation is done.
 * Else a blocking wait is performed on the clock. We save the ClockID
 * so we can unlock the entry at any time. While we are blocking, we 
 * release the PREROLL_LOCK so that other threads can interrupt the entry.
 */
static GstClockReturn
gst_base_sink_wait_clock (GstBaseSink * basesink, GstClockTime time,
    GstClockTimeDiff * jitter)
{
  GstClockID id;
  GstClockReturn ret;
  GstClock *clock;
  GstClockTime base_time;

  *jitter = 0;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time)))
    goto invalid_time;

  GST_OBJECT_LOCK (basesink);
  if (G_UNLIKELY (!basesink->sync))
    goto no_sync;

  if (G_UNLIKELY ((clock = GST_ELEMENT_CLOCK (basesink)) == NULL))
    goto no_clock;

  base_time = GST_ELEMENT_CAST (basesink)->base_time;
  id = gst_clock_new_single_shot_id (clock, base_time + time);
  GST_OBJECT_UNLOCK (basesink);

  basesink->clock_id = id;
  /* release the preroll lock while waiting */
  GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);

  ret = gst_clock_id_wait (id, jitter);

  GST_PAD_PREROLL_LOCK (basesink->sinkpad);
  gst_clock_id_unref (id);
  basesink->clock_id = NULL;

  return ret;

invalid_time:
  {
    GST_DEBUG_OBJECT (basesink, "time not valid, no sync needed");
    return GST_CLOCK_OK;
  }
no_sync:
  {
    GST_DEBUG_OBJECT (basesink, "sync disabled");
    GST_OBJECT_UNLOCK (basesink);
    return GST_CLOCK_OK;
  }
no_clock:
  {
    GST_DEBUG_OBJECT (basesink, "no clock, can't sync");
    GST_OBJECT_UNLOCK (basesink);
    return GST_CLOCK_OK;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK
 *
 * Make sure we are in PLAYING and synchronize an object to the clock.
 *
 * If we need preroll, we are not in PLAYING. We try to commit the state
 * if needed and then block if we still are not PLAYING.
 *
 * We start waiting on the clock in PLAYING. If we got interrupted, we
 * immediatly try to repreroll.
 *
 * Some objects do not need synchronisation (most events) and so this function
 * immediatly returns GST_FLOW_OK.
 *
 * does not take ownership of obj.
 */
static GstFlowReturn
gst_base_sink_do_sync (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj, gboolean * late)
{
  GstClockTime start, stop;
  GstClockTimeDiff jitter;
  gboolean syncable;
  GstClockReturn status = GST_CLOCK_OK;

  /* get timing information for this object */
  start = stop = -1;
  syncable = gst_base_sink_get_sync_times (basesink, obj, &start, &stop);

  /* a syncable object needs to participate in preroll and
   * clocking. All buffers and EOS are syncable. */
  if (G_UNLIKELY (!syncable))
    goto not_syncable;

again:
  /* first do preroll, this makes sure we commit our state
   * to PAUSED and can continue to PLAYING. We cannot perform
   * any clock sync in PAUSED because there is no clock. 
   */
  while (G_UNLIKELY (basesink->need_preroll)) {
    GST_DEBUG_OBJECT (basesink, "prerolling object %p", obj);

    if (G_LIKELY (basesink->playing_async)) {
      basesink->playing_async = FALSE;
      /* commit state */
      if (G_UNLIKELY (!gst_base_sink_commit_state (basesink)))
        goto stopping;
    }

    /* need to recheck here because the commit state could have
     * made us not need the preroll anymore */
    if (G_LIKELY (basesink->need_preroll)) {
      /* block until the state changes, or we get a flush, or something */
      GST_DEBUG_OBJECT (basesink, "waiting to finish preroll");
      basesink->have_preroll = TRUE;
      GST_PAD_PREROLL_WAIT (pad);
      basesink->have_preroll = FALSE;
      GST_DEBUG_OBJECT (basesink, "done preroll");
      if (G_UNLIKELY (basesink->flushing))
        goto flushing;
    }
  }

  /* preroll done, we can sync since we ar in PLAYING now. */
  GST_DEBUG_OBJECT (basesink, "waiting for clock");
  basesink->end_time = stop;
  status = gst_base_sink_wait_clock (basesink, start, &jitter);
  GST_DEBUG_OBJECT (basesink, "clock returned %d", status);

  /* waiting could be interrupted and we can be flushing now */
  if (G_UNLIKELY (basesink->flushing))
    goto flushing;

  /* check for unlocked by a state change, we are not flushing so
   * we can try to preroll on the current buffer. */
  if (G_UNLIKELY (status == GST_CLOCK_UNSCHEDULED)) {
    GST_DEBUG_OBJECT (basesink, "unscheduled, waiting some more");
    goto again;
  }

  if (status == GST_CLOCK_EARLY && jitter > (10 * GST_MSECOND)) {
    /* FIXME, update clock stats here and do some QoS */
    GST_DEBUG_OBJECT (basesink, "late: jitter!! %" G_GINT64_FORMAT "\n",
        jitter);
    *late = TRUE;
  } else {
    *late = FALSE;
  }

  return GST_FLOW_OK;

  /* ERRORS */
not_syncable:
  {
    GST_DEBUG_OBJECT (basesink, "non syncable object %p", obj);
    return GST_FLOW_OK;
  }
flushing:
  {
    GST_DEBUG_OBJECT (basesink, "we are flushing");
    return GST_FLOW_WRONG_STATE;
  }
stopping:
  {
    GST_DEBUG_OBJECT (basesink, "stopping while commiting state");
    return GST_FLOW_WRONG_STATE;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK,
 *
 * Synchronize the object on the clock and then render it.
 *
 * takes ownership of obj.
 */
static GstFlowReturn
gst_base_sink_render_object (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseSinkClass *bclass;
  gboolean late = FALSE;

  /* synchronize this object */
  ret = gst_base_sink_do_sync (basesink, pad, obj, &late);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto sync_failed;

  /* and now render */
  if (G_LIKELY (GST_IS_BUFFER (obj))) {
    /* drop late messages unconditionally */
    if (late)
      goto dropped;

    bclass = GST_BASE_SINK_GET_CLASS (basesink);

    GST_DEBUG_OBJECT (basesink, "rendering buffer %p", obj);
    if (G_LIKELY (bclass->render))
      ret = bclass->render (basesink, GST_BUFFER_CAST (obj));
  } else {
    GstEvent *event = GST_EVENT_CAST (obj);
    gboolean ok = TRUE;
    GstEventType type;

    bclass = GST_BASE_SINK_GET_CLASS (basesink);

    type = GST_EVENT_TYPE (event);

    GST_DEBUG_OBJECT (basesink, "rendering event %p, type %s", obj,
        gst_event_type_get_name (type));

    if (bclass->event)
      ok = bclass->event (basesink, event);

    if (G_LIKELY (ok)) {
      switch (type) {
        case GST_EVENT_EOS:
          /* the EOS event is completely handled so we mark
           * ourselves as being in the EOS state. eos is also 
           * protected by the object lock so we can read it when 
           * answering the POSITION query. */
          GST_OBJECT_LOCK (basesink);
          basesink->eos = TRUE;
          GST_OBJECT_UNLOCK (basesink);
          /* ok, now we can post the message */
          GST_DEBUG_OBJECT (basesink, "Now posting EOS");
          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_eos (GST_OBJECT_CAST (basesink)));
          break;
        case GST_EVENT_NEWSEGMENT:
          /* configure the segment */
          gst_base_sink_configure_segment (basesink, pad, event,
              &basesink->segment);
        default:
          break;
      }
    }
  }

  GST_DEBUG_OBJECT (basesink, "object unref after render %p", obj);
  gst_mini_object_unref (obj);

  return ret;

  /* ERRORS */
sync_failed:
  {
    GST_DEBUG_OBJECT (basesink, "do_sync returned %s, unref object %p",
        gst_flow_get_name (ret), obj);
    gst_mini_object_unref (obj);

    return ret;
  }
dropped:
  {
    GST_DEBUG_OBJECT (basesink, "buffer late, dropping, unref object %p", obj);
    gst_mini_object_unref (obj);
    return GST_FLOW_OK;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK
 *
 * Perform preroll on the given object. For buffers this means 
 * calling the preroll subclass method. 
 * If that succeeds, the state will be commited.
 *
 * function does not take ownership of obj.
 */
static GstFlowReturn
gst_base_sink_preroll_object (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj)
{
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (basesink, "do preroll %p", obj);

  /* if it's a buffer, we need to call the preroll method */
  if (G_LIKELY (GST_IS_BUFFER (obj))) {
    GstBaseSinkClass *bclass;
    GstBuffer *buf = GST_BUFFER_CAST (obj);

    GST_DEBUG_OBJECT (basesink, "preroll buffer %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    bclass = GST_BASE_SINK_GET_CLASS (basesink);
    if (bclass->preroll)
      if ((ret = bclass->preroll (basesink, buf)) != GST_FLOW_OK)
        goto preroll_failed;
  }

  /* commit state */
  if (G_LIKELY (basesink->playing_async)) {
    basesink->playing_async = FALSE;
    if (G_UNLIKELY (!gst_base_sink_commit_state (basesink)))
      goto stopping;
  }

  return GST_FLOW_OK;

  /* ERRORS */
preroll_failed:
  {
    GST_DEBUG_OBJECT (basesink, "preroll failed, abort state");
    gst_element_abort_state (GST_ELEMENT_CAST (basesink));
    return ret;
  }
stopping:
  {
    GST_DEBUG_OBJECT (basesink, "stopping while commiting state");
    return GST_FLOW_WRONG_STATE;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK 
 *
 * Queue an object for rendering.
 * The first prerollable object queued will complete the preroll. If the
 * preroll queue if filled, we render all the objects in the queue.
 *
 * This function takes ownership of the object.
 */
static GstFlowReturn
gst_base_sink_queue_object_unlocked (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj, gboolean prerollable)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint length;
  GQueue *q;

  if (G_UNLIKELY (basesink->need_preroll)) {
    if (G_LIKELY (prerollable))
      basesink->preroll_queued++;

    length = basesink->preroll_queued;

    GST_DEBUG_OBJECT (basesink, "now %d prerolled items", length);

    /* first prerollable item needs to finish the preroll */
    if (length == 1) {
      ret = gst_base_sink_preroll_object (basesink, pad, obj);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto preroll_failed;
    }
    /* need to recheck if we need preroll, commmit state during preroll 
     * could have made us not need more preroll. */
    if (G_UNLIKELY (basesink->need_preroll)) {
      /* see if we can render now. */
      if (G_UNLIKELY (length <= basesink->preroll_queue_max_len))
        goto more_preroll;
    }
  }

  /* we can start rendering (or blocking) the queued object
   * if any. */
  q = basesink->preroll_queue;
  while (G_UNLIKELY (!g_queue_is_empty (q))) {
    GstMiniObject *o;

    o = g_queue_pop_head (q);
    GST_DEBUG_OBJECT (basesink, "rendering queued object %p", o);

    /* FIXME, do something with the return value? */
    ret = gst_base_sink_render_object (basesink, pad, o);
  }

  /* now render the object */
  ret = gst_base_sink_render_object (basesink, pad, obj);
  basesink->preroll_queued = 0;

  return ret;

  /* special cases */
preroll_failed:
  {
    GST_DEBUG_OBJECT (basesink, "preroll failed, reason %s",
        gst_flow_get_name (ret));
    gst_mini_object_unref (obj);
    return ret;
  }
more_preroll:
  {
    /* add object to the queue and return */
    GST_DEBUG_OBJECT (basesink, "need more preroll data %d <= %d",
        length, basesink->preroll_queue_max_len);
    g_queue_push_tail (basesink->preroll_queue, obj);
    return GST_FLOW_OK;
  }
}

/* with STREAM_LOCK
 *
 * This function grabs the PREROLL_LOCK and adds the object to
 * the queue.
 *
 * This function takes ownership of obj.
 */
static GstFlowReturn
gst_base_sink_queue_object (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj, gboolean prerollable)
{
  GstFlowReturn ret;

  GST_PAD_PREROLL_LOCK (pad);
  if (G_UNLIKELY (basesink->flushing))
    goto flushing;

  ret = gst_base_sink_queue_object_unlocked (basesink, pad, obj, prerollable);
  GST_PAD_PREROLL_UNLOCK (pad);

  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (basesink, "sink is flushing");
    GST_PAD_PREROLL_UNLOCK (pad);
    gst_mini_object_unref (obj);
    return GST_FLOW_WRONG_STATE;
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

      /* EOS is a prerollable object */
      ret =
          gst_base_sink_queue_object (basesink, pad,
          GST_MINI_OBJECT_CAST (event), TRUE);

      if (G_UNLIKELY (ret != GST_FLOW_OK))
        result = FALSE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      GstFlowReturn ret;

      basesink->have_newsegment = TRUE;

      /* the new segment is a non prerollable item and does not block anything,
       * we need to configure the current clipping segment and insert the event 
       * in the queue to serialize it with the buffers for rendering. */
      gst_base_sink_configure_segment (basesink, pad, event,
          basesink->abidata.ABI.clip_segment);

      ret =
          gst_base_sink_queue_object (basesink, pad,
          GST_MINI_OBJECT_CAST (event), FALSE);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        result = FALSE;
      break;
    }
    case GST_EVENT_FLUSH_START:
      if (bclass->event)
        bclass->event (basesink, event);

      /* make sure we are not blocked on the clock also clear any pending
       * eos state. */
      gst_base_sink_set_flushing (basesink, pad, TRUE);

      /* we grab the stream lock but that is not needed since setting the
       * sink to flushing would make sure no state commit is being done
       * anymore */
      GST_PAD_STREAM_LOCK (pad);
      /* and we need to commit our state again on the next
       * prerolled buffer */
      basesink->playing_async = TRUE;
      gst_element_lost_state (GST_ELEMENT_CAST (basesink));
      GST_DEBUG_OBJECT (basesink, "event unref %p %p", basesink, event);
      GST_PAD_STREAM_UNLOCK (pad);

      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (bclass->event)
        bclass->event (basesink, event);

      /* unset flushing so we can accept new data */
      gst_base_sink_set_flushing (basesink, pad, FALSE);

      /* we need new segment info after the flush. */
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (basesink->abidata.ABI.clip_segment,
          GST_FORMAT_UNDEFINED);
      basesink->have_newsegment = FALSE;

      GST_DEBUG_OBJECT (basesink, "event unref %p %p", basesink, event);
      gst_event_unref (event);
      break;
    default:
      /* other events are sent to queue or subclass depending on if they
       * are serialized. */
      if (GST_EVENT_IS_SERIALIZED (event)) {
        gst_base_sink_queue_object (basesink, pad,
            GST_MINI_OBJECT_CAST (event), FALSE);
      } else {
        if (bclass->event)
          bclass->event (basesink, event);
        gst_event_unref (event);
      }
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

/* must be called with PREROLL_LOCK */
static gboolean
gst_base_sink_is_prerolled (GstBaseSink * basesink)
{
  gboolean res;

  res = basesink->have_preroll || basesink->eos;
  GST_DEBUG_OBJECT (basesink, "have_preroll: %d, EOS: %d => prerolled: %d",
      basesink->have_preroll, basesink->eos, res);
  return res;
}

/* with STREAM_LOCK, PREROLL_LOCK 
 *
 * Takes a buffer and compare the timestamps with the last segment.
 * If the buffer falls outside of the segment boundaries, drop it.
 * Else queue the buffer for preroll and rendering.
 *
 * This function takes ownership of the buffer.
 */
static GstFlowReturn
gst_base_sink_chain_unlocked (GstBaseSink * basesink, GstPad * pad,
    GstBuffer * buf)
{
  GstFlowReturn result;
  GstClockTime start = -1, end = -1;

  if (G_UNLIKELY (basesink->flushing))
    goto flushing;

  if (G_UNLIKELY (!basesink->have_newsegment)) {
    gboolean sync;

    GST_OBJECT_LOCK (basesink);
    sync = basesink->sync;
    GST_OBJECT_UNLOCK (basesink);

    if (sync) {
      GST_ELEMENT_WARNING (basesink, STREAM, FAILED,
          (_("Internal data flow problem.")),
          ("Received buffer without a new-segment. Cannot sync to clock."));
    }

    basesink->have_newsegment = TRUE;
    /* this means this sink will not be able to sync to the clock */
    basesink->abidata.ABI.clip_segment->start = -1;
    basesink->abidata.ABI.clip_segment->stop = -1;
    basesink->segment.start = -1;
    basesink->segment.stop = -1;
  }

  /* check if the buffer needs to be dropped */
  /* we don't use the subclassed method as it may not return
   * valid values for our purpose here */
  gst_base_sink_get_times (basesink, buf, &start, &end);

  GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
      ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  if (GST_CLOCK_TIME_IS_VALID (start) &&
      (basesink->abidata.ABI.clip_segment->format == GST_FORMAT_TIME)) {
    if (G_UNLIKELY (!gst_segment_clip (basesink->abidata.ABI.clip_segment,
                GST_FORMAT_TIME, (gint64) start, (gint64) end, NULL, NULL)))
      goto out_of_segment;
  }

  /* now we can process the buffer in the queue, this function takes ownership
   * of the buffer */
  result =
      gst_base_sink_queue_object_unlocked (basesink, pad,
      GST_MINI_OBJECT_CAST (buf), TRUE);

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (basesink, "sink is flushing");
    gst_buffer_unref (buf);
    return GST_FLOW_WRONG_STATE;
  }
out_of_segment:
  {
    GST_DEBUG_OBJECT (basesink, "dropping buffer, out of clipping segment");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
}

/* with STREAM_LOCK
 */
static GstFlowReturn
gst_base_sink_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseSink *basesink;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  if (G_UNLIKELY (basesink->pad_mode != GST_ACTIVATE_PUSH))
    goto wrong_mode;

  GST_PAD_PREROLL_LOCK (pad);
  result = gst_base_sink_chain_unlocked (basesink, pad, buf);
  GST_PAD_PREROLL_UNLOCK (pad);

done:
  gst_object_unref (basesink);

  return result;

  /* ERRORS */
wrong_mode:
  {
    GST_OBJECT_LOCK (pad);
    GST_WARNING_OBJECT (basesink,
        "Push on pad %s:%s, but it was not activated in push mode",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    gst_buffer_unref (buf);
    /* we don't post an error message this will signal to the peer
     * pushing that EOS is reached. */
    result = GST_FLOW_UNEXPECTED;
    goto done;
  }
}

/* with STREAM_LOCK
 */
static void
gst_base_sink_loop (GstPad * pad)
{
  GstBaseSink *basesink;
  GstBuffer *buf = NULL;
  GstFlowReturn result;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));

  g_assert (basesink->pad_mode == GST_ACTIVATE_PULL);

  result = gst_pad_pull_range (pad, basesink->offset, DEFAULT_SIZE, &buf);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto paused;

  if (G_UNLIKELY (buf == NULL))
    goto no_buffer;

  GST_PAD_PREROLL_LOCK (pad);
  result = gst_base_sink_chain_unlocked (basesink, pad, buf);
  GST_PAD_PREROLL_UNLOCK (pad);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto paused;

  gst_object_unref (basesink);

  return;

  /* ERRORS */
paused:
  {
    GST_LOG_OBJECT (basesink, "pausing task, reason %s",
        gst_flow_get_name (result));
    gst_pad_pause_task (pad);
    /* fatal errors and NOT_LINKED cause EOS */
    if (GST_FLOW_IS_FATAL (result) || result == GST_FLOW_NOT_LINKED) {
      gst_base_sink_event (pad, gst_event_new_eos ());
      /* EOS does not cause an ERROR message */
      if (result != GST_FLOW_UNEXPECTED) {
        GST_ELEMENT_ERROR (basesink, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("stream stopped, reason %s", gst_flow_get_name (result)));
      }
    }
    gst_object_unref (basesink);
    return;
  }
no_buffer:
  {
    GST_LOG_OBJECT (basesink, "no buffer, pausing");
    result = GST_FLOW_ERROR;
    goto paused;
  }
}

static gboolean
gst_base_sink_set_flushing (GstBaseSink * basesink, GstPad * pad,
    gboolean flushing)
{
  GST_PAD_PREROLL_LOCK (pad);
  basesink->flushing = flushing;
  if (flushing) {
    GstBaseSinkClass *bclass;

    bclass = GST_BASE_SINK_GET_CLASS (basesink);

    /* step 1, unblock clock sync (if any) or any other blocking thing */
    basesink->need_preroll = TRUE;
    if (basesink->clock_id) {
      gst_clock_id_unschedule (basesink->clock_id);
    }

    /* unlock any subclasses */
    if (bclass->unlock)
      bclass->unlock (basesink);

    /* flush out the data thread if it's locked in finish_preroll */
    GST_DEBUG_OBJECT (basesink,
        "flushing out data thread, need preroll to TRUE");
    gst_base_sink_preroll_queue_flush (basesink, pad);
  }
  GST_PAD_PREROLL_UNLOCK (pad);

  return TRUE;
}

static gboolean
gst_base_sink_deactivate (GstBaseSink * basesink, GstPad * pad)
{
  gboolean result;

  gst_base_sink_set_flushing (basesink, pad, TRUE);

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

  gst_base_sink_set_flushing (basesink, pad, FALSE);

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
    gst_base_sink_set_flushing (basesink, pad, TRUE);
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
          /* we mark we have a newsegment here because pull based
           * mode works just fine without having a newsegment before the
           * first buffer */
          gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
          gst_segment_init (basesink->abidata.ABI.clip_segment,
              GST_FORMAT_UNDEFINED);
          basesink->have_newsegment = TRUE;

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
      result = gst_base_sink_deactivate (basesink, pad);
      basesink->pad_mode = GST_ACTIVATE_NONE;
    }
  }

  gst_object_unref (basesink);

  return result;
}

/* send an event to our sinkpad peer. */
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
      GstClockTime now, base;
      gint64 time, accum;
      gdouble abs_rate;

      /* we can answer time format */
      GST_OBJECT_LOCK (basesink);

      /* can only give answer if not EOS */
      if (G_UNLIKELY (basesink->eos))
        goto wrong_state;

      /* We get position from clock only in PLAYING */
      if (GST_STATE (basesink) != GST_STATE_PLAYING)
        goto wrong_state;

      /* and we need a clock */
      if (G_UNLIKELY ((clock = GST_ELEMENT_CLOCK (basesink)) == NULL))
        goto wrong_state;

      /* collect all data we need holding the lock */
      if (GST_CLOCK_TIME_IS_VALID (basesink->segment.time))
        time = basesink->segment.time;
      else
        time = 0;

      base = GST_ELEMENT_CAST (basesink)->base_time;
      accum = basesink->segment.accum;
      abs_rate = basesink->segment.abs_rate;

      gst_object_ref (clock);
      /* need to release the object lock before we can get the time */
      GST_OBJECT_UNLOCK (basesink);

      now = gst_clock_get_time (clock);
      base += accum;
      base = MIN (now, base);
      *cur = gst_guint64_to_gdouble (now - base) * abs_rate + time;

      GST_DEBUG_OBJECT (basesink,
          "now %" GST_TIME_FORMAT " - base %" GST_TIME_FORMAT " - accum %"
          GST_TIME_FORMAT " + time %" GST_TIME_FORMAT " = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (now), GST_TIME_ARGS (base),
          GST_TIME_ARGS (accum), GST_TIME_ARGS (time), GST_TIME_ARGS (*cur));

      gst_object_unref (clock);

      res = TRUE;
    }
    default:
      /* cannot answer other than TIME */
      break;
  }
  return res;

wrong_state:
  {
    GST_OBJECT_UNLOCK (basesink);
    return FALSE;
  }
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

      gst_query_parse_position (query, &format, NULL);

      GST_DEBUG_OBJECT (basesink, "current position format %d", format);

      /* first try to get the position based on the clock */
      if ((res = gst_base_sink_get_position (basesink, format, &cur))) {
        gst_query_set_position (query, format, cur);
      } else {
        /* fallback to peer query */
        res = gst_base_sink_peer_query (basesink, query);
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
      GST_DEBUG_OBJECT (basesink, "READY to PAUSED, need preroll");
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (basesink->abidata.ABI.clip_segment,
          GST_FORMAT_UNDEFINED);
      basesink->have_newsegment = FALSE;
      basesink->offset = 0;
      basesink->have_preroll = FALSE;
      basesink->need_preroll = TRUE;
      basesink->playing_async = TRUE;
      basesink->eos = FALSE;
      ret = GST_STATE_CHANGE_ASYNC;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      if (gst_base_sink_is_prerolled (basesink)) {
        /* no preroll needed anymore now. */
        GST_DEBUG_OBJECT (basesink, "PAUSED to PLAYING, don't need preroll");
        basesink->playing_async = FALSE;
        basesink->need_preroll = FALSE;
        if (basesink->eos) {
          /* need to post EOS message here */
          GST_DEBUG_OBJECT (basesink, "Now posting EOS");
          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_eos (GST_OBJECT_CAST (basesink)));
        } else {
          GST_DEBUG_OBJECT (basesink, "signal preroll");
          GST_PAD_PREROLL_SIGNAL (basesink->sinkpad);
        }
      } else {
        GST_DEBUG_OBJECT (basesink, "PAUSED to PLAYING, need preroll");
        basesink->need_preroll = TRUE;
        basesink->playing_async = TRUE;
        ret = GST_STATE_CHANGE_ASYNC;
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    default:
      break;
  }

  {
    GstStateChangeReturn bret;

    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (G_UNLIKELY (bret == GST_STATE_CHANGE_FAILURE))
      goto activate_failed;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT (basesink, "PLAYING to PAUSED");
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      basesink->need_preroll = TRUE;
      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }

      if (bclass->unlock)
        bclass->unlock (basesink);

      /* if we don't have a preroll buffer we need to wait for a preroll and
       * return ASYNC. */
      if (gst_base_sink_is_prerolled (basesink)) {
        basesink->playing_async = FALSE;
      } else {
        GST_DEBUG_OBJECT (basesink, "PLAYING to PAUSED, need preroll");
        basesink->playing_async = TRUE;
        ret = GST_STATE_CHANGE_ASYNC;
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (bclass->stop)
        if (!bclass->stop (basesink)) {
          GST_WARNING_OBJECT (basesink, "failed to stop");
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
