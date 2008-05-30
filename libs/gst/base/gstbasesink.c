/* GStreamer
 * Copyright (C) 2005-2007 Wim Taymans <wim.taymans@gmail.com>
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
 * simplified interface to plugin writers. #GstBaseSink handles many details
 * for you, for example: preroll, clock synchronization, state changes,
 * activation in push or pull mode, and queries.
 *
 * In most cases, when writing sink elements, there is no need to implement
 * class methods from #GstElement or to set functions on pads, because the
 * #GstBaseSink infrastructure should be sufficient.
 *
 * #GstBaseSink provides support for exactly one sink pad, which should be
 * named "sink". A sink implementation (subclass of #GstBaseSink) should
 * install a pad template in its base_init function, like so:
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
 * return #GST_STATE_CHANGE_ASYNC from a state change to PAUSED until the first
 * buffer arrives in this element. The base class will call the
 * #GstBaseSink::preroll vmethod with this preroll buffer and will then commit
 * the state change to the next asynchronously pending state.
 *
 * When the element is set to PLAYING, #GstBaseSink will synchronise on the
 * clock using the times returned from ::get_times. If this function returns
 * #GST_CLOCK_TIME_NONE for the start time, no synchronisation will be done.
 * Synchronisation can be disabled entirely by setting the object "sync"
 * property to %FALSE.
 *
 * After synchronisation the virtual method #GstBaseSink::render will be called.
 * Subclasses should minimally implement this method.
 *
 * Since 0.10.3 subclasses that synchronise on the clock in the ::render method
 * are supported as well. These classes typically receive a buffer in the render
 * method and can then potentially block on the clock while rendering. A typical
 * example is an audiosink. Since 0.10.11 these subclasses can use
 * gst_base_sink_wait_preroll() to perform the blocking wait.
 *
 * Upon receiving the EOS event in the PLAYING state, #GstBaseSink will wait
 * for the clock to reach the time indicated by the stop time of the last
 * ::get_times call before posting an EOS message. When the element receives
 * EOS in PAUSED, preroll completes, the event is queued and an EOS message is
 * posted when going to PLAYING.
 *
 * #GstBaseSink will internally use the #GST_EVENT_NEWSEGMENT events to schedule
 * synchronisation and clipping of buffers. Buffers that fall completely outside
 * of the current segment are dropped. Buffers that fall partially in the
 * segment are rendered (and prerolled). Subclasses should do any subbuffer
 * clipping themselves when needed.
 *
 * #GstBaseSink will by default report the current playback position in
 * #GST_FORMAT_TIME based on the current clock time and segment information.
 * If no clock has been set on the element, the query will be forwarded
 * upstream.
 *
 * The ::set_caps function will be called when the subclass should configure
 * itself to process a specific media type.
 *
 * The ::start and ::stop virtual methods will be called when resources should
 * be allocated. Any ::preroll, ::render  and ::set_caps function will be
 * called between the ::start and ::stop calls.
 *
 * The ::event virtual method will be called when an event is received by
 * #GstBaseSink. Normally this method should only be overriden by very specific
 * elements (such as file sinks) which need to handle the newsegment event
 * specially.
 *
 * #GstBaseSink provides an overridable ::buffer_alloc function that can be
 * used by sinks that want to do reverse negotiation or to provide
 * custom buffers (hardware buffers for example) to upstream elements.
 *
 * The ::unlock method is called when the elements should unblock any blocking
 * operations they perform in the ::render method. This is mostly useful when
 * the ::render method performs a blocking write on a file descriptor, for
 * example.
 *
 * The max-lateness property affects how the sink deals with buffers that
 * arrive too late in the sink. A buffer arrives too late in the sink when
 * the presentation time (as a combination of the last segment, buffer
 * timestamp and element base_time) plus the duration is before the current
 * time of the clock.
 * If the frame is later than max-lateness, the sink will drop the buffer
 * without calling the render method.
 * This feature is disabled if sync is disabled, the ::get-times method does
 * not return a valid start time or max-lateness is set to -1 (the default).
 * Subclasses can use gst_base_sink_set_max_lateness() to configure the
 * max-lateness value.
 *
 * The qos property will enable the quality-of-service features of the basesink
 * which gather statistics about the real-time performance of the clock
 * synchronisation. For each buffer received in the sink, statistics are
 * gathered and a QOS event is sent upstream with these numbers. This
 * information can then be used by upstream elements to reduce their processing
 * rate, for example.
 *
 * Since 0.10.15 the async property can be used to instruct the sink to never
 * perform an ASYNC state change. This feature is mostly usable when dealing
 * with non-synchronized streams or sparse streams.
 *
 * Last reviewed on 2007-08-29 (0.10.15)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesink.h"
#include <gst/gstmarshal.h>
#include <gst/gst_private.h>
#include <gst/gst-i18n-lib.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_sink_debug);
#define GST_CAT_DEFAULT gst_base_sink_debug

#define GST_BASE_SINK_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_SINK, GstBaseSinkPrivate))

/* FIXME, some stuff in ABI.data and other in Private...
 * Make up your mind please.
 */
struct _GstBaseSinkPrivate
{
  gint qos_enabled;             /* ATOMIC */
  gboolean async_enabled;
  GstClockTimeDiff ts_offset;

  /* start, stop of current buffer, stream time, used to report position */
  GstClockTime current_sstart;
  GstClockTime current_sstop;

  /* start, stop and jitter of current buffer, running time */
  GstClockTime current_rstart;
  GstClockTime current_rstop;
  GstClockTimeDiff current_jitter;

  /* EOS sync time in running time */
  GstClockTime eos_rtime;

  /* last buffer that arrived in time, running time */
  GstClockTime last_in_time;
  /* when the last buffer left the sink, running time */
  GstClockTime last_left;

  /* running averages go here these are done on running time */
  GstClockTime avg_pt;
  GstClockTime avg_duration;
  gdouble avg_rate;

  /* these are done on system time. avg_jitter and avg_render are
   * compared to eachother to see if the rendering time takes a
   * huge amount of the processing, If so we are flooded with
   * buffers. */
  GstClockTime last_left_systime;
  GstClockTime avg_jitter;
  GstClockTime start, stop;
  GstClockTime avg_render;

  /* number of rendered and dropped frames */
  guint64 rendered;
  guint64 dropped;

  /* latency stuff */
  GstClockTime latency;

  /* if we already commited the state */
  gboolean commited;

  /* when we received EOS */
  gboolean received_eos;

  /* when we are prerolled and able to report latency */
  gboolean have_latency;

  /* the last buffer we prerolled or rendered. Useful for making snapshots */
  GstBuffer *last_buffer;
};

#define DO_RUNNING_AVG(avg,val,size) (((val) + ((size)-1) * (avg)) / (size))

/* generic running average, this has a neutral window size */
#define UPDATE_RUNNING_AVG(avg,val)   DO_RUNNING_AVG(avg,val,8)

/* the windows for these running averages are experimentally obtained.
 * possitive values get averaged more while negative values use a small
 * window so we can react faster to badness. */
#define UPDATE_RUNNING_AVG_P(avg,val) DO_RUNNING_AVG(avg,val,16)
#define UPDATE_RUNNING_AVG_N(avg,val) DO_RUNNING_AVG(avg,val,4)

/* BaseSink properties */

#define DEFAULT_SIZE 1024
#define DEFAULT_CAN_ACTIVATE_PULL FALSE /* fixme: enable me */
#define DEFAULT_CAN_ACTIVATE_PUSH TRUE

#define DEFAULT_PREROLL_QUEUE_LEN	0
#define DEFAULT_SYNC			TRUE
#define DEFAULT_MAX_LATENESS		-1
#define DEFAULT_QOS			FALSE
#define DEFAULT_ASYNC			TRUE
#define DEFAULT_TS_OFFSET		0

enum
{
  PROP_0,
  PROP_PREROLL_QUEUE_LEN,
  PROP_SYNC,
  PROP_MAX_LATENESS,
  PROP_QOS,
  PROP_ASYNC,
  PROP_TS_OFFSET,
  PROP_LAST_BUFFER,
  PROP_LAST
};

static GstElementClass *parent_class = NULL;

static void gst_base_sink_class_init (GstBaseSinkClass * klass);
static void gst_base_sink_init (GstBaseSink * trans, gpointer g_class);
static void gst_base_sink_finalize (GObject * object);

GType
gst_base_sink_get_type (void)
{
  static GType base_sink_type = 0;

  if (G_UNLIKELY (base_sink_type == 0)) {
    static const GTypeInfo base_sink_info = {
      sizeof (GstBaseSinkClass),
      NULL,
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
static gboolean gst_base_sink_default_activate_pull (GstBaseSink * basesink,
    gboolean active);

static GstStateChangeReturn gst_base_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_base_sink_chain (GstPad * pad, GstBuffer * buffer);
static void gst_base_sink_loop (GstPad * pad);
static gboolean gst_base_sink_pad_activate (GstPad * pad);
static gboolean gst_base_sink_pad_activate_push (GstPad * pad, gboolean active);
static gboolean gst_base_sink_pad_activate_pull (GstPad * pad, gboolean active);
static gboolean gst_base_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_sink_peer_query (GstBaseSink * sink, GstQuery * query);

/* check if an object was too late */
static gboolean gst_base_sink_is_too_late (GstBaseSink * basesink,
    GstMiniObject * obj, GstClockTime start, GstClockTime stop,
    GstClockReturn status, GstClockTimeDiff jitter);

static void
gst_base_sink_class_init (GstBaseSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_base_sink_debug, "basesink", 0,
      "basesink element");

  g_type_class_add_private (klass, sizeof (GstBaseSinkPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_sink_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_base_sink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_base_sink_get_property);

  /* FIXME, this next value should be configured using an event from the
   * upstream element, ie, the BUFFER_SIZE event. */
  g_object_class_install_property (gobject_class, PROP_PREROLL_QUEUE_LEN,
      g_param_spec_uint ("preroll-queue-len", "Preroll queue length",
          "Number of buffers to queue during preroll", 0, G_MAXUINT,
          DEFAULT_PREROLL_QUEUE_LEN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync on the clock", DEFAULT_SYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_LATENESS,
      g_param_spec_int64 ("max-lateness", "Max Lateness",
          "Maximum number of nanoseconds that a buffer can be late before it "
          "is dropped (-1 unlimited)", -1, G_MAXINT64, DEFAULT_MAX_LATENESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QOS,
      g_param_spec_boolean ("qos", "Qos",
          "Generate Quality-of-Service events upstream", DEFAULT_QOS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseSink:async
   *
   * If set to #TRUE, the basesink will perform asynchronous state changes.
   * When set to #FALSE, the sink will not signal the parent when it prerolls.
   * Use this option when dealing with sparse streams or when synchronisation is
   * not required.
   *
   * Since: 0.10.15
   */
  g_object_class_install_property (gobject_class, PROP_ASYNC,
      g_param_spec_boolean ("async", "Async",
          "Go asynchronously to PAUSED", DEFAULT_ASYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseSink:ts-offset
   *
   * Controls the final synchronisation, a negative value will render the buffer
   * earlier while a positive value delays playback. This property can be 
   * used to fix synchronisation in bad files.
   *
   * Since: 0.10.15
   */
  g_object_class_install_property (gobject_class, PROP_TS_OFFSET,
      g_param_spec_int64 ("ts-offset", "TS Offset",
          "Timestamp offset in nanoseconds", G_MININT64, G_MAXINT64,
          DEFAULT_TS_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstBaseSink:last-buffer
   *
   * The last buffer that arrived in the sink and was used for preroll or for
   * rendering. This property can be used to generate thumbnails. This property
   * can be NULL when the sink has not yet received a bufer.
   *
   * Since: 0.10.15
   */
  g_object_class_install_property (gobject_class, PROP_LAST_BUFFER,
      gst_param_spec_mini_object ("last-buffer", "Last Buffer",
          "The last buffer received in the sink", GST_TYPE_BUFFER,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_sink_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_base_sink_send_event);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_base_sink_query);

  klass->get_caps = GST_DEBUG_FUNCPTR (gst_base_sink_get_caps);
  klass->set_caps = GST_DEBUG_FUNCPTR (gst_base_sink_set_caps);
  klass->buffer_alloc = GST_DEBUG_FUNCPTR (gst_base_sink_buffer_alloc);
  klass->get_times = GST_DEBUG_FUNCPTR (gst_base_sink_get_times);
  klass->activate_pull =
      GST_DEBUG_FUNCPTR (gst_base_sink_default_activate_pull);
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
  gboolean res = TRUE;

  bsink = GST_BASE_SINK (gst_pad_get_parent (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bsink->pad_mode == GST_ACTIVATE_PULL) {
    GstPad *peer = gst_pad_get_peer (pad);

    if (peer)
      res = gst_pad_set_caps (peer, caps);
    else
      res = FALSE;

    if (!res)
      GST_DEBUG_OBJECT (bsink, "peer setcaps() failed");
  }

  if (res && bclass->set_caps)
    res = bclass->set_caps (bsink, caps);

  gst_object_unref (bsink);

  return res;
}

static void
gst_base_sink_pad_fixate (GstPad * pad, GstCaps * caps)
{
  GstBaseSinkClass *bclass;
  GstBaseSink *bsink;

  bsink = GST_BASE_SINK (gst_pad_get_parent (pad));
  bclass = GST_BASE_SINK_GET_CLASS (bsink);

  if (bclass->fixate)
    bclass->fixate (bsink, caps);

  gst_object_unref (bsink);
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
  GstBaseSinkPrivate *priv;

  basesink->priv = priv = GST_BASE_SINK_GET_PRIVATE (basesink);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);

  basesink->sinkpad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_getcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_getcaps));
  gst_pad_set_setcaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_setcaps));
  gst_pad_set_fixatecaps_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_fixate));
  gst_pad_set_bufferalloc_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_buffer_alloc));
  gst_pad_set_activate_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_activate));
  gst_pad_set_activatepush_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_activate_push));
  gst_pad_set_activatepull_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_pad_activate_pull));
  gst_pad_set_event_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_event));
  gst_pad_set_chain_function (basesink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_sink_chain));
  gst_element_add_pad (GST_ELEMENT_CAST (basesink), basesink->sinkpad);

  basesink->pad_mode = GST_ACTIVATE_NONE;
  basesink->preroll_queue = g_queue_new ();
  basesink->abidata.ABI.clip_segment = gst_segment_new ();
  priv->have_latency = FALSE;

  basesink->can_activate_push = DEFAULT_CAN_ACTIVATE_PUSH;
  basesink->can_activate_pull = DEFAULT_CAN_ACTIVATE_PULL;

  basesink->sync = DEFAULT_SYNC;
  basesink->abidata.ABI.max_lateness = DEFAULT_MAX_LATENESS;
  g_atomic_int_set (&priv->qos_enabled, DEFAULT_QOS);
  priv->async_enabled = DEFAULT_ASYNC;
  priv->ts_offset = DEFAULT_TS_OFFSET;

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

/**
 * gst_base_sink_set_sync:
 * @sink: the sink
 * @sync: the new sync value.
 *
 * Configures @sink to synchronize on the clock or not. When
 * @sync is FALSE, incomming samples will be played as fast as
 * possible. If @sync is TRUE, the timestamps of the incomming
 * buffers will be used to schedule the exact render time of its
 * contents.
 *
 * Since: 0.10.4
 */
void
gst_base_sink_set_sync (GstBaseSink * sink, gboolean sync)
{
  g_return_if_fail (GST_IS_BASE_SINK (sink));

  GST_OBJECT_LOCK (sink);
  sink->sync = sync;
  GST_OBJECT_UNLOCK (sink);
}

/**
 * gst_base_sink_get_sync:
 * @sink: the sink
 *
 * Checks if @sink is currently configured to synchronize against the
 * clock.
 *
 * Returns: TRUE if the sink is configured to synchronize against the clock.
 *
 * Since: 0.10.4
 */
gboolean
gst_base_sink_get_sync (GstBaseSink * sink)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), FALSE);

  GST_OBJECT_LOCK (sink);
  res = sink->sync;
  GST_OBJECT_UNLOCK (sink);

  return res;
}

/**
 * gst_base_sink_set_max_lateness:
 * @sink: the sink
 * @max_lateness: the new max lateness value.
 *
 * Sets the new max lateness value to @max_lateness. This value is
 * used to decide if a buffer should be dropped or not based on the
 * buffer timestamp and the current clock time. A value of -1 means
 * an unlimited time.
 *
 * Since: 0.10.4
 */
void
gst_base_sink_set_max_lateness (GstBaseSink * sink, gint64 max_lateness)
{
  g_return_if_fail (GST_IS_BASE_SINK (sink));

  GST_OBJECT_LOCK (sink);
  sink->abidata.ABI.max_lateness = max_lateness;
  GST_OBJECT_UNLOCK (sink);
}

/**
 * gst_base_sink_get_max_lateness:
 * @sink: the sink
 *
 * Gets the max lateness value. See gst_base_sink_set_max_lateness for
 * more details.
 *
 * Returns: The maximum time in nanoseconds that a buffer can be late
 * before it is dropped and not rendered. A value of -1 means an
 * unlimited time.
 *
 * Since: 0.10.4
 */
gint64
gst_base_sink_get_max_lateness (GstBaseSink * sink)
{
  gint64 res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), -1);

  GST_OBJECT_LOCK (sink);
  res = sink->abidata.ABI.max_lateness;
  GST_OBJECT_UNLOCK (sink);

  return res;
}

/**
 * gst_base_sink_set_qos_enabled:
 * @sink: the sink
 * @enabled: the new qos value.
 *
 * Configures @sink to send Quality-of-Service events upstream.
 *
 * Since: 0.10.5
 */
void
gst_base_sink_set_qos_enabled (GstBaseSink * sink, gboolean enabled)
{
  g_return_if_fail (GST_IS_BASE_SINK (sink));

  g_atomic_int_set (&sink->priv->qos_enabled, enabled);
}

/**
 * gst_base_sink_is_qos_enabled:
 * @sink: the sink
 *
 * Checks if @sink is currently configured to send Quality-of-Service events
 * upstream.
 *
 * Returns: TRUE if the sink is configured to perform Quality-of-Service.
 *
 * Since: 0.10.5
 */
gboolean
gst_base_sink_is_qos_enabled (GstBaseSink * sink)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), FALSE);

  res = g_atomic_int_get (&sink->priv->qos_enabled);

  return res;
}

/**
 * gst_base_sink_set_async_enabled:
 * @sink: the sink
 * @enabled: the new async value.
 *
 * Configures @sink to perform all state changes asynchronusly. When async is
 * disabled, the sink will immediatly go to PAUSED instead of waiting for a
 * preroll buffer. This feature is usefull if the sink does not synchronize
 * against the clock or when it is dealing with sparse streams.
 *
 * Since: 0.10.15
 */
void
gst_base_sink_set_async_enabled (GstBaseSink * sink, gboolean enabled)
{
  g_return_if_fail (GST_IS_BASE_SINK (sink));

  GST_PAD_PREROLL_LOCK (sink->sinkpad);
  sink->priv->async_enabled = enabled;
  GST_PAD_PREROLL_UNLOCK (sink->sinkpad);
}

/**
 * gst_base_sink_is_async_enabled:
 * @sink: the sink
 *
 * Checks if @sink is currently configured to perform asynchronous state
 * changes to PAUSED.
 *
 * Returns: TRUE if the sink is configured to perform asynchronous state
 * changes.
 *
 * Since: 0.10.15
 */
gboolean
gst_base_sink_is_async_enabled (GstBaseSink * sink)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), FALSE);

  GST_PAD_PREROLL_LOCK (sink->sinkpad);
  res = sink->priv->async_enabled;
  GST_PAD_PREROLL_UNLOCK (sink->sinkpad);

  return res;
}

/**
 * gst_base_sink_set_ts_offset:
 * @sink: the sink
 * @offset: the new offset
 *
 * Adjust the synchronisation of @sink with @offset. A negative value will
 * render buffers earlier than their timestamp. A positive value will delay
 * rendering. This function can be used to fix playback of badly timestamped
 * buffers.
 *
 * Since: 0.10.15
 */
void
gst_base_sink_set_ts_offset (GstBaseSink * sink, GstClockTimeDiff offset)
{
  g_return_if_fail (GST_IS_BASE_SINK (sink));

  GST_OBJECT_LOCK (sink);
  sink->priv->ts_offset = offset;
  GST_OBJECT_UNLOCK (sink);
}

/**
 * gst_base_sink_get_ts_offset:
 * @sink: the sink
 *
 * Get the synchronisation offset of @sink.
 *
 * Returns: The synchronisation offset.
 *
 * Since: 0.10.15
 */
GstClockTimeDiff
gst_base_sink_get_ts_offset (GstBaseSink * sink)
{
  GstClockTimeDiff res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), 0);

  GST_OBJECT_LOCK (sink);
  res = sink->priv->ts_offset;
  GST_OBJECT_UNLOCK (sink);

  return res;
}

/**
 * gst_base_sink_get_last_buffer:
 * @sink: the sink
 *
 * Get the last buffer that arrived in the sink and was used for preroll or for
 * rendering. This property can be used to generate thumbnails.
 *
 * The #GstCaps on the buffer can be used to determine the type of the buffer.
 * 
 * Returns: a #GstBuffer. gst_buffer_unref() after usage. This function returns
 * NULL when no buffer has arrived in the sink yet or when the sink is not in
 * PAUSED or PLAYING.
 *
 * Since: 0.10.15
 */
GstBuffer *
gst_base_sink_get_last_buffer (GstBaseSink * sink)
{
  GstBuffer *res;

  g_return_val_if_fail (GST_IS_BASE_SINK (sink), NULL);

  GST_OBJECT_LOCK (sink);
  if ((res = sink->priv->last_buffer))
    gst_buffer_ref (res);
  GST_OBJECT_UNLOCK (sink);

  return res;
}

static void
gst_base_sink_set_last_buffer (GstBaseSink * sink, GstBuffer * buffer)
{
  GstBuffer *old;

  if (buffer)
    gst_buffer_ref (buffer);

  GST_OBJECT_LOCK (sink);
  old = sink->priv->last_buffer;
  sink->priv->last_buffer = buffer;
  GST_OBJECT_UNLOCK (sink);

  if (old)
    gst_buffer_unref (old);
}

/**
 * gst_base_sink_get_latency:
 * @sink: the sink
 *
 * Get the currently configured latency.
 *
 * Returns: The configured latency.
 *
 * Since: 0.10.12
 */
GstClockTime
gst_base_sink_get_latency (GstBaseSink * sink)
{
  GstClockTime res;

  GST_OBJECT_LOCK (sink);
  res = sink->priv->latency;
  GST_OBJECT_UNLOCK (sink);

  return res;
}

/**
 * gst_base_sink_query_latency:
 * @sink: the sink
 * @live: if the sink is live
 * @upstream_live: if an upstream element is live
 * @min_latency: the min latency of the upstream elements
 * @max_latency: the max latency of the upstream elements
 *
 * Query the sink for the latency parameters. The latency will be queried from
 * the upstream elements. @live will be TRUE if @sink is configured to
 * synchronize against the clock. @upstream_live will be TRUE if an upstream
 * element is live. 
 *
 * If both @live and @upstream_live are TRUE, the sink will want to compensate
 * for the latency introduced by the upstream elements by setting the
 * @min_latency to a strictly possitive value.
 *
 * This function is mostly used by subclasses. 
 *
 * Returns: TRUE if the query succeeded.
 *
 * Since: 0.10.12
 */
gboolean
gst_base_sink_query_latency (GstBaseSink * sink, gboolean * live,
    gboolean * upstream_live, GstClockTime * min_latency,
    GstClockTime * max_latency)
{
  gboolean l, us_live, res, have_latency;
  GstClockTime min, max;
  GstQuery *query;
  GstClockTime us_min, us_max;

  /* we are live when we sync to the clock */
  GST_OBJECT_LOCK (sink);
  l = sink->sync;
  have_latency = sink->priv->have_latency;
  GST_OBJECT_UNLOCK (sink);

  /* assume no latency */
  min = 0;
  max = -1;
  us_live = FALSE;

  if (have_latency) {
    GST_DEBUG_OBJECT (sink, "we are ready for LATENCY query");
    /* we are ready for a latency query this is when we preroll or when we are
     * not async. */
    query = gst_query_new_latency ();

    /* ask the peer for the latency */
    if ((res = gst_base_sink_peer_query (sink, query))) {
      /* get upstream min and max latency */
      gst_query_parse_latency (query, &us_live, &us_min, &us_max);

      if (us_live) {
        /* upstream live, use its latency, subclasses should use these
         * values to create the complete latency. */
        min = us_min;
        max = us_max;
      }
    }
    gst_query_unref (query);
  } else {
    GST_DEBUG_OBJECT (sink, "we are not yet ready for LATENCY query");
    res = FALSE;
  }

  /* not live, we tried to do the query, if it failed we return TRUE anyway */
  if (!res) {
    if (!l) {
      res = TRUE;
      GST_DEBUG_OBJECT (sink, "latency query failed but we are not live");
    } else {
      GST_DEBUG_OBJECT (sink, "latency query failed and we are live");
    }
  }

  if (res) {
    GST_DEBUG_OBJECT (sink, "latency query: live: %d, have_latency %d,"
        " upstream: %d, min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT, l,
        have_latency, us_live, GST_TIME_ARGS (min), GST_TIME_ARGS (max));

    if (live)
      *live = l;
    if (upstream_live)
      *upstream_live = us_live;
    if (min_latency)
      *min_latency = min;
    if (max_latency)
      *max_latency = max;
  }
  return res;
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
      gst_base_sink_set_sync (sink, g_value_get_boolean (value));
      break;
    case PROP_MAX_LATENESS:
      gst_base_sink_set_max_lateness (sink, g_value_get_int64 (value));
      break;
    case PROP_QOS:
      gst_base_sink_set_qos_enabled (sink, g_value_get_boolean (value));
      break;
    case PROP_ASYNC:
      gst_base_sink_set_async_enabled (sink, g_value_get_boolean (value));
      break;
    case PROP_TS_OFFSET:
      gst_base_sink_set_ts_offset (sink, g_value_get_int64 (value));
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
      g_value_set_boolean (value, gst_base_sink_get_sync (sink));
      break;
    case PROP_MAX_LATENESS:
      g_value_set_int64 (value, gst_base_sink_get_max_lateness (sink));
      break;
    case PROP_QOS:
      g_value_set_boolean (value, gst_base_sink_is_qos_enabled (sink));
      break;
    case PROP_ASYNC:
      g_value_set_boolean (value, gst_base_sink_is_async_enabled (sink));
      break;
    case PROP_TS_OFFSET:
      g_value_set_int64 (value, gst_base_sink_get_ts_offset (sink));
      break;
    case PROP_LAST_BUFFER:
      gst_value_take_buffer (value, gst_base_sink_get_last_buffer (sink));
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
  basesink->priv->received_eos = FALSE;
  basesink->have_preroll = FALSE;
  basesink->eos_queued = FALSE;
  basesink->preroll_queued = 0;
  basesink->buffers_queued = 0;
  basesink->events_queued = 0;
  /* can't report latency anymore until we preroll again */
  if (basesink->priv->async_enabled) {
    GST_OBJECT_LOCK (basesink);
    basesink->priv->have_latency = FALSE;
    GST_OBJECT_UNLOCK (basesink);
  }
  /* and signal any waiters now */
  GST_PAD_PREROLL_SIGNAL (pad);
}

/* with STREAM_LOCK, configures given segment with the event information. */
static void
gst_base_sink_configure_segment (GstBaseSink * basesink, GstPad * pad,
    GstEvent * event, GstSegment * segment)
{
  gboolean update;
  gdouble rate, arate;
  GstFormat format;
  gint64 start;
  gint64 stop;
  gint64 time;

  /* the newsegment event is needed to bring the buffer timestamps to the
   * stream time and to drop samples outside of the playback segment. */
  gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
      &start, &stop, &time);

  /* The segment is protected with both the STREAM_LOCK and the OBJECT_LOCK.
   * We protect with the OBJECT_LOCK so that we can use the values to
   * safely answer a POSITION query. */
  GST_OBJECT_LOCK (basesink);
  gst_segment_set_newsegment_full (segment, update, rate, arate, format, start,
      stop, time);

  if (format == GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (basesink,
        "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format GST_FORMAT_TIME, "
        "%" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT
        ", time %" GST_TIME_FORMAT ", accum %" GST_TIME_FORMAT,
        update, rate, arate, GST_TIME_ARGS (segment->start),
        GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time),
        GST_TIME_ARGS (segment->accum));
  } else {
    GST_DEBUG_OBJECT (basesink,
        "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format %d, "
        "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
        G_GINT64_FORMAT ", accum %" G_GINT64_FORMAT, update, rate, arate,
        segment->format, segment->start, segment->stop, segment->time,
        segment->accum);
  }
  GST_OBJECT_UNLOCK (basesink);
}

/* with PREROLL_LOCK, STREAM_LOCK */
static gboolean
gst_base_sink_commit_state (GstBaseSink * basesink)
{
  /* commit state and proceed to next pending state */
  GstState current, next, pending, post_pending;
  gboolean post_paused = FALSE;
  gboolean post_async_done = FALSE;
  gboolean post_playing = FALSE;
  gboolean sync;

  /* we are certainly not playing async anymore now */
  basesink->playing_async = FALSE;

  GST_OBJECT_LOCK (basesink);
  current = GST_STATE (basesink);
  next = GST_STATE_NEXT (basesink);
  pending = GST_STATE_PENDING (basesink);
  post_pending = pending;
  sync = basesink->sync;

  switch (pending) {
    case GST_STATE_PLAYING:
    {
      GstBaseSinkClass *bclass;
      GstStateChangeReturn ret;

      bclass = GST_BASE_SINK_GET_CLASS (basesink);

      GST_DEBUG_OBJECT (basesink, "commiting state to PLAYING");

      basesink->need_preroll = FALSE;
      post_async_done = TRUE;
      basesink->priv->commited = TRUE;
      post_playing = TRUE;
      /* post PAUSED too when we were READY */
      if (current == GST_STATE_READY) {
        post_paused = TRUE;
      }

      /* make sure we notify the subclass of async playing */
      if (bclass->async_play) {
        ret = bclass->async_play (basesink);
        if (ret == GST_STATE_CHANGE_FAILURE)
          goto async_failed;
      }
      break;
    }
    case GST_STATE_PAUSED:
      GST_DEBUG_OBJECT (basesink, "commiting state to PAUSED");
      post_paused = TRUE;
      post_async_done = TRUE;
      basesink->priv->commited = TRUE;
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

  /* we can report latency queries now */
  basesink->priv->have_latency = TRUE;

  GST_STATE (basesink) = pending;
  GST_STATE_NEXT (basesink) = GST_STATE_VOID_PENDING;
  GST_STATE_PENDING (basesink) = GST_STATE_VOID_PENDING;
  GST_STATE_RETURN (basesink) = GST_STATE_CHANGE_SUCCESS;
  GST_OBJECT_UNLOCK (basesink);

  if (post_paused) {
    GST_DEBUG_OBJECT (basesink, "posting PAUSED state change message");
    gst_element_post_message (GST_ELEMENT_CAST (basesink),
        gst_message_new_state_changed (GST_OBJECT_CAST (basesink),
            current, next, post_pending));
  }
  if (post_async_done) {
    GST_DEBUG_OBJECT (basesink, "posting async-done message");
    gst_element_post_message (GST_ELEMENT_CAST (basesink),
        gst_message_new_async_done (GST_OBJECT_CAST (basesink)));
  }
  if (post_playing) {
    GST_DEBUG_OBJECT (basesink, "posting PLAYING state change message");
    gst_element_post_message (GST_ELEMENT_CAST (basesink),
        gst_message_new_state_changed (GST_OBJECT_CAST (basesink),
            next, pending, GST_STATE_VOID_PENDING));
  }

  GST_STATE_BROADCAST (basesink);

  return TRUE;

nothing_pending:
  {
    /* Depending on the state, set our vars. We get in this situation when the
     * state change function got a change to update the state vars before the
     * streaming thread did. This is fine but we need to make sure that we
     * update the need_preroll var since it was TRUE when we got here and might
     * become FALSE if we got to PLAYING. */
    GST_DEBUG_OBJECT (basesink, "nothing to commit, now in %s",
        gst_element_state_get_name (current));
    switch (current) {
      case GST_STATE_PLAYING:
        basesink->need_preroll = FALSE;
        break;
      case GST_STATE_PAUSED:
        basesink->need_preroll = TRUE;
        break;
      default:
        basesink->need_preroll = FALSE;
        basesink->flushing = TRUE;
        break;
    }
    /* we can report latency queries now */
    basesink->priv->have_latency = TRUE;
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
async_failed:
  {
    GST_DEBUG_OBJECT (basesink, "async commit failed");
    GST_STATE_RETURN (basesink) = GST_STATE_CHANGE_FAILURE;
    GST_OBJECT_UNLOCK (basesink);
    return FALSE;
  }
}


/* with STREAM_LOCK, PREROLL_LOCK
 *
 * Returns TRUE if the object needs synchronisation and takes therefore
 * part in prerolling.
 *
 * rsstart/rsstop contain the start/stop in stream time.
 * rrstart/rrstop contain the start/stop in running time.
 */
static gboolean
gst_base_sink_get_sync_times (GstBaseSink * basesink, GstMiniObject * obj,
    GstClockTime * rsstart, GstClockTime * rsstop,
    GstClockTime * rrstart, GstClockTime * rrstop, gboolean * do_sync,
    GstSegment * segment)
{
  GstBaseSinkClass *bclass;
  GstBuffer *buffer;
  GstClockTime start, stop;     /* raw start/stop timestamps */
  gint64 cstart, cstop;         /* clipped raw timestamps */
  gint64 rstart, rstop;         /* clipped timestamps converted to running time */
  GstClockTime sstart, sstop;   /* clipped timestamps converted to stream time */
  GstFormat format;
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

  /* start with nothing */
  start = stop = sstart = sstop = rstart = rstop = -1;

  if (G_UNLIKELY (GST_IS_EVENT (obj))) {
    GstEvent *event = GST_EVENT_CAST (obj);

    switch (GST_EVENT_TYPE (event)) {
        /* EOS event needs syncing */
      case GST_EVENT_EOS:
        if (basesink->segment.rate >= 0.0)
          sstart = sstop = priv->current_sstop;
        else
          sstart = sstop = priv->current_sstart;

        rstart = rstop = priv->eos_rtime;
        *do_sync = rstart != -1;
        GST_DEBUG_OBJECT (basesink, "sync times for EOS %" GST_TIME_FORMAT,
            GST_TIME_ARGS (rstart));
        goto done;
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

  /* just get the times to see if we need syncing */
  if (bclass->get_times)
    bclass->get_times (basesink, buffer, &start, &stop);

  if (start == -1) {
    gst_base_sink_get_times (basesink, buffer, &start, &stop);
    *do_sync = FALSE;
  } else {
    *do_sync = TRUE;
  }

  GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
      ", stop: %" GST_TIME_FORMAT ", do_sync %d", GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop), *do_sync);

  /* collect segment and format for code clarity */
  format = segment->format;

  /* no timestamp clipping if we did not * get a TIME segment format */
  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    cstart = start;
    cstop = stop;
    /* do running and stream time in TIME format */
    format = GST_FORMAT_TIME;
    goto do_times;
  }

  /* clip */
  if (G_UNLIKELY (!gst_segment_clip (segment, GST_FORMAT_TIME,
              (gint64) start, (gint64) stop, &cstart, &cstop)))
    goto out_of_segment;

  if (G_UNLIKELY (start != cstart || stop != cstop)) {
    GST_DEBUG_OBJECT (basesink, "clipped to: start %" GST_TIME_FORMAT
        ", stop: %" GST_TIME_FORMAT, GST_TIME_ARGS (cstart),
        GST_TIME_ARGS (cstop));
  }

  /* set last stop position */
  if (G_LIKELY (cstop != GST_CLOCK_TIME_NONE))
    gst_segment_set_last_stop (segment, GST_FORMAT_TIME, cstop);
  else
    gst_segment_set_last_stop (segment, GST_FORMAT_TIME, cstart);

do_times:
  /* this can produce wrong values if we accumulated non-TIME segments. If this happens,
   * upstream is behaving very badly */
  sstart = gst_segment_to_stream_time (segment, format, cstart);
  sstop = gst_segment_to_stream_time (segment, format, cstop);
  rstart = gst_segment_to_running_time (segment, format, cstart);
  rstop = gst_segment_to_running_time (segment, format, cstop);

done:
  /* save times */
  *rsstart = sstart;
  *rsstop = sstop;
  *rrstart = rstart;
  *rrstop = rstop;

  /* buffers and EOS always need syncing and preroll */
  return TRUE;

  /* special cases */
out_of_segment:
  {
    /* should not happen since we clip them in the chain function already, 
     * we return FALSE so that we don't try to sync on it. */
    GST_ELEMENT_WARNING (basesink, STREAM, FAILED,
        (NULL), ("unexpected buffer out of segment found."));
    GST_LOG_OBJECT (basesink, "buffer skipped, not in segment");
    return FALSE;
  }
}

/* with STREAM_LOCK, PREROLL_LOCK
 * adjust a timestamp with the latency and timestamp offset */
static GstClockTime
gst_base_sink_adjust_time (GstBaseSink * basesink, GstClockTime time)
{
  GstClockTimeDiff ts_offset;

  /* don't do anything funny with invalid timestamps */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time)))
    return time;

  time += basesink->priv->latency;

  /* apply offset, be carefull for underflows */
  ts_offset = basesink->priv->ts_offset;
  if (ts_offset < 0) {
    ts_offset = -ts_offset;
    if (ts_offset < time)
      time -= ts_offset;
    else
      time = 0;
  } else
    time += ts_offset;

  return time;
}

/* gst_base_sink_wait_clock:
 * @sink: the sink
 * @time: the running_time to be reached
 * @jitter: the jitter to be filled with time diff (can be NULL)
 *
 * This function will block until @time is reached. It is usually called by
 * subclasses that use their own internal synchronisation.
 *
 * If @time is not valid, no sycnhronisation is done and #GST_CLOCK_BADTIME is
 * returned. Likewise, if synchronisation is disabled in the element or there
 * is no clock, no synchronisation is done and #GST_CLOCK_BADTIME is returned.
 *
 * This function should only be called with the PREROLL_LOCK held, like when
 * receiving an EOS event in the ::event vmethod or when receiving a buffer in
 * the ::render vmethod.
 *
 * The @time argument should be the running_time of when this method should
 * return and is not adjusted with any latency or offset configured in the
 * sink.
 *
 * Since 0.10.20
 *
 * Returns: #GstClockReturn
 */
GstClockReturn
gst_base_sink_wait_clock (GstBaseSink * basesink, GstClockTime time,
    GstClockTimeDiff * jitter)
{
  GstClockID id;
  GstClockReturn ret;
  GstClock *clock;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time)))
    goto invalid_time;

  GST_OBJECT_LOCK (basesink);
  if (G_UNLIKELY (!basesink->sync))
    goto no_sync;

  if (G_UNLIKELY ((clock = GST_ELEMENT_CLOCK (basesink)) == NULL))
    goto no_clock;

  /* add base_time to running_time to get the time against the clock */
  time += GST_ELEMENT_CAST (basesink)->base_time;

  id = gst_clock_new_single_shot_id (clock, time);
  GST_OBJECT_UNLOCK (basesink);

  /* A blocking wait is performed on the clock. We save the ClockID
   * so we can unlock the entry at any time. While we are blocking, we 
   * release the PREROLL_LOCK so that other threads can interrupt the
   * entry. */
  basesink->clock_id = id;
  /* release the preroll lock while waiting */
  GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);

  ret = gst_clock_id_wait (id, jitter);

  GST_PAD_PREROLL_LOCK (basesink->sinkpad);
  gst_clock_id_unref (id);
  basesink->clock_id = NULL;

  return ret;

  /* no syncing needed */
invalid_time:
  {
    GST_DEBUG_OBJECT (basesink, "time not valid, no sync needed");
    return GST_CLOCK_BADTIME;
  }
no_sync:
  {
    GST_DEBUG_OBJECT (basesink, "sync disabled");
    GST_OBJECT_UNLOCK (basesink);
    return GST_CLOCK_BADTIME;
  }
no_clock:
  {
    GST_DEBUG_OBJECT (basesink, "no clock, can't sync");
    GST_OBJECT_UNLOCK (basesink);
    return GST_CLOCK_BADTIME;
  }
}

/**
 * gst_base_sink_wait_preroll:
 * @sink: the sink
 *
 * If the #GstBaseSinkClass::render method performs its own synchronisation against
 * the clock it must unblock when going from PLAYING to the PAUSED state and call
 * this method before continuing to render the remaining data.
 *
 * This function will block until a state change to PLAYING happens (in which
 * case this function returns #GST_FLOW_OK) or the processing must be stopped due
 * to a state change to READY or a FLUSH event (in which case this function
 * returns #GST_FLOW_WRONG_STATE).
 *
 * Since: 0.10.11
 *
 * Returns: #GST_FLOW_OK if the preroll completed and processing can
 * continue. Any other return value should be returned from the render vmethod.
 */
GstFlowReturn
gst_base_sink_wait_preroll (GstBaseSink * sink)
{
  sink->have_preroll = TRUE;
  GST_DEBUG_OBJECT (sink, "waiting in preroll for flush or PLAYING");
  /* block until the state changes, or we get a flush, or something */
  GST_PAD_PREROLL_WAIT (sink->sinkpad);
  sink->have_preroll = FALSE;
  if (G_UNLIKELY (sink->flushing))
    goto stopping;
  GST_DEBUG_OBJECT (sink, "continue after preroll");

  return GST_FLOW_OK;

  /* ERRORS */
stopping:
  {
    GST_DEBUG_OBJECT (sink, "preroll interrupted");
    return GST_FLOW_WRONG_STATE;
  }
}

/**
 * gst_base_sink_wait_eos:
 * @sink: the sink
 * @time: the running_time to be reached
 * @jitter: the jitter to be filled with time diff (can be NULL)
 *
 * This function will block until @time is reached. It is usually called by
 * subclasses that use their own internal synchronisation but want to let the
 * EOS be handled by the base class.
 *
 * This function should only be called with the PREROLL_LOCK held, like when
 * receiving an EOS event in the ::event vmethod.
 *
 * The @time argument should be the running_time of when the EOS should happen
 * and will be adjusted with any latency and offset configured in the sink.
 *
 * Since 0.10.15
 *
 * Returns: #GstFlowReturn
 */
GstFlowReturn
gst_base_sink_wait_eos (GstBaseSink * sink, GstClockTime time,
    GstClockTimeDiff * jitter)
{
  GstClockReturn status;
  GstFlowReturn ret;

  do {
    GstClockTime stime;

    GST_DEBUG_OBJECT (sink, "checking preroll");

    /* first wait for the playing state before we can continue */
    if (G_UNLIKELY (sink->need_preroll)) {
      ret = gst_base_sink_wait_preroll (sink);
      if (ret != GST_FLOW_OK)
        goto flushing;
    }

    /* preroll done, we can sync since we are in PLAYING now. */
    GST_DEBUG_OBJECT (sink, "possibly waiting for clock to reach %"
        GST_TIME_FORMAT, GST_TIME_ARGS (time));

    /* wait for the clock, this can be interrupted because we got shut down or 
     * we PAUSED. */
    stime = gst_base_sink_adjust_time (sink, time);
    status = gst_base_sink_wait_clock (sink, stime, jitter);

    GST_DEBUG_OBJECT (sink, "clock returned %d", status);

    /* invalid time, no clock or sync disabled, just continue then */
    if (status == GST_CLOCK_BADTIME)
      break;

    /* waiting could have been interrupted and we can be flushing now */
    if (G_UNLIKELY (sink->flushing))
      goto flushing;

    /* retry if we got unscheduled, which means we did not reach the timeout
     * yet. if some other error occures, we continue. */
  } while (status == GST_CLOCK_UNSCHEDULED);

  GST_DEBUG_OBJECT (sink, "end of stream");

  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (sink, "we are flushing");
    return GST_FLOW_WRONG_STATE;
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
 * immediatly try to re-preroll.
 *
 * Some objects do not need synchronisation (most events) and so this function
 * immediatly returns GST_FLOW_OK.
 *
 * for objects that arrive later than max-lateness to be synchronized to the 
 * clock have the @late boolean set to TRUE.
 *
 * This function keeps a running average of the jitter (the diff between the
 * clock time and the requested sync time). The jitter is negative for
 * objects that arrive in time and positive for late buffers.
 *
 * does not take ownership of obj.
 */
static GstFlowReturn
gst_base_sink_do_sync (GstBaseSink * basesink, GstPad * pad,
    GstMiniObject * obj, gboolean * late)
{
  GstClockTimeDiff jitter;
  gboolean syncable;
  GstClockReturn status = GST_CLOCK_OK;
  GstClockTime rstart, rstop, sstart, sstop, stime;
  gboolean do_sync;
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

  sstart = sstop = rstart = rstop = -1;
  do_sync = TRUE;

  priv->current_rstart = -1;

  /* get timing information for this object against the render segment */
  syncable = gst_base_sink_get_sync_times (basesink, obj,
      &sstart, &sstop, &rstart, &rstop, &do_sync, &basesink->segment);

  /* a syncable object needs to participate in preroll and
   * clocking. All buffers and EOS are syncable. */
  if (G_UNLIKELY (!syncable))
    goto not_syncable;

  /* store timing info for current object */
  priv->current_rstart = rstart;
  priv->current_rstop = (rstop != -1 ? rstop : rstart);
  /* save sync time for eos when the previous object needed sync */
  priv->eos_rtime = (do_sync ? priv->current_rstop : -1);

again:
  /* first do preroll, this makes sure we commit our state
   * to PAUSED and can continue to PLAYING. We cannot perform
   * any clock sync in PAUSED because there is no clock. 
   */
  while (G_UNLIKELY (basesink->need_preroll)) {
    GST_DEBUG_OBJECT (basesink, "prerolling object %p", obj);

    if (G_LIKELY (basesink->playing_async)) {
      /* commit state */
      if (G_UNLIKELY (!gst_base_sink_commit_state (basesink)))
        goto stopping;
    }

    /* need to recheck here because the commit state could have
     * made us not need the preroll anymore */
    if (G_LIKELY (basesink->need_preroll)) {
      /* block until the state changes, or we get a flush, or something */
      if (gst_base_sink_wait_preroll (basesink) != GST_FLOW_OK)
        goto flushing;
    }
  }

  /* After rendering we store the position of the last buffer so that we can use
   * it to report the position. We need to take the lock here. */
  GST_OBJECT_LOCK (basesink);
  priv->current_sstart = sstart;
  priv->current_sstop = (sstop != -1 ? sstop : sstart);
  GST_OBJECT_UNLOCK (basesink);

  if (!do_sync)
    goto done;

  /* preroll done, we can sync since we are in PLAYING now. */
  GST_DEBUG_OBJECT (basesink, "possibly waiting for clock to reach %"
      GST_TIME_FORMAT, GST_TIME_ARGS (rstart));

  /* this function will return immediatly if start == -1, no clock
   * or sync is disabled with GST_CLOCK_BADTIME. */
  stime = gst_base_sink_adjust_time (basesink, rstart);
  status = gst_base_sink_wait_clock (basesink, stime, &jitter);

  GST_DEBUG_OBJECT (basesink, "clock returned %d", status);

  /* invalid time, no clock or sync disabled, just render */
  if (status == GST_CLOCK_BADTIME)
    goto done;

  /* waiting could have been interrupted and we can be flushing now */
  if (G_UNLIKELY (basesink->flushing))
    goto flushing;

  /* check for unlocked by a state change, we are not flushing so
   * we can try to preroll on the current buffer. */
  if (G_UNLIKELY (status == GST_CLOCK_UNSCHEDULED)) {
    GST_DEBUG_OBJECT (basesink, "unscheduled, waiting some more");
    goto again;
  }

  /* successful syncing done, record observation */
  priv->current_jitter = jitter;

  /* check if the object should be dropped */
  *late = gst_base_sink_is_too_late (basesink, obj, rstart, rstop,
      status, jitter);

done:
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

static gboolean
gst_base_sink_send_qos (GstBaseSink * basesink,
    gdouble proportion, GstClockTime time, GstClockTimeDiff diff)
{
  GstEvent *event;
  gboolean res;

  /* generate Quality-of-Service event */
  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, basesink,
      "qos: proportion: %lf, diff %" G_GINT64_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, diff, GST_TIME_ARGS (time));

  event = gst_event_new_qos (proportion, diff, time);

  /* send upstream */
  res = gst_pad_push_event (basesink->sinkpad, event);

  return res;
}

static void
gst_base_sink_perform_qos (GstBaseSink * sink, gboolean dropped)
{
  GstBaseSinkPrivate *priv;
  GstClockTime start, stop;
  GstClockTimeDiff jitter;
  GstClockTime pt, entered, left;
  GstClockTime duration;
  gdouble rate;

  priv = sink->priv;

  start = priv->current_rstart;

  /* if Quality-of-Service disabled, do nothing */
  if (!g_atomic_int_get (&priv->qos_enabled) || start == -1)
    return;

  stop = priv->current_rstop;
  jitter = priv->current_jitter;

  /* this is the time the buffer entered the sink */
  entered = start + jitter;
  /* this is the time the buffer left the sink */
  left = start + (jitter < 0 ? 0 : jitter);

  /* calculate duration of the buffer */
  if (stop != -1)
    duration = stop - start;
  else
    duration = -1;

  /* if we have the time when the last buffer left us, calculate
   * processing time */
  if (priv->last_left != -1) {
    if (entered > priv->last_left) {
      pt = entered - priv->last_left;
    } else {
      pt = 0;
    }
  } else {
    pt = priv->avg_pt;
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, sink, "start: %" GST_TIME_FORMAT
      ", entered %" GST_TIME_FORMAT ", left %" GST_TIME_FORMAT ", pt: %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT ",jitter %"
      G_GINT64_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (entered),
      GST_TIME_ARGS (left), GST_TIME_ARGS (pt), GST_TIME_ARGS (duration),
      jitter);

  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, sink, "avg_duration: %" GST_TIME_FORMAT
      ", avg_pt: %" GST_TIME_FORMAT ", avg_rate: %g",
      GST_TIME_ARGS (priv->avg_duration), GST_TIME_ARGS (priv->avg_pt),
      priv->avg_rate);

  /* collect running averages. for first observations, we copy the
   * values */
  if (priv->avg_duration == -1)
    priv->avg_duration = duration;
  else
    priv->avg_duration = UPDATE_RUNNING_AVG (priv->avg_duration, duration);

  if (priv->avg_pt == -1)
    priv->avg_pt = pt;
  else
    priv->avg_pt = UPDATE_RUNNING_AVG (priv->avg_pt, pt);

  if (priv->avg_duration != 0)
    rate =
        gst_guint64_to_gdouble (priv->avg_pt) /
        gst_guint64_to_gdouble (priv->avg_duration);
  else
    rate = 0.0;

  if (priv->last_left != -1) {
    if (dropped || priv->avg_rate < 0.0) {
      priv->avg_rate = rate;
    } else {
      if (rate > 1.0)
        priv->avg_rate = UPDATE_RUNNING_AVG_N (priv->avg_rate, rate);
      else
        priv->avg_rate = UPDATE_RUNNING_AVG_P (priv->avg_rate, rate);
    }
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, sink,
      "updated: avg_duration: %" GST_TIME_FORMAT ", avg_pt: %" GST_TIME_FORMAT
      ", avg_rate: %g", GST_TIME_ARGS (priv->avg_duration),
      GST_TIME_ARGS (priv->avg_pt), priv->avg_rate);


  /* if we have a valid rate, start sending QoS messages */
  if (priv->avg_rate >= 0.0) {
    gst_base_sink_send_qos (sink, priv->avg_rate, priv->current_rstart,
        priv->current_jitter);
  }

  /* record when this buffer will leave us */
  priv->last_left = left;
}

/* reset all qos measuring */
static void
gst_base_sink_reset_qos (GstBaseSink * sink)
{
  GstBaseSinkPrivate *priv;

  priv = sink->priv;

  priv->last_in_time = -1;
  priv->last_left = -1;
  priv->avg_duration = -1;
  priv->avg_pt = -1;
  priv->avg_rate = -1.0;
  priv->avg_render = -1;
  priv->rendered = 0;
  priv->dropped = 0;

}

/* Checks if the object was scheduled too late.
 *
 * start/stop contain the raw timestamp start and stop values
 * of the object.
 *
 * status and jitter contain the return values from the clock wait.
 *
 * returns TRUE if the buffer was too late.
 */
static gboolean
gst_base_sink_is_too_late (GstBaseSink * basesink, GstMiniObject * obj,
    GstClockTime start, GstClockTime stop,
    GstClockReturn status, GstClockTimeDiff jitter)
{
  gboolean late;
  gint64 max_lateness;
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

  late = FALSE;

  /* only for objects that were too late */
  if (G_LIKELY (status != GST_CLOCK_EARLY))
    goto in_time;

  max_lateness = basesink->abidata.ABI.max_lateness;

  /* check if frame dropping is enabled */
  if (max_lateness == -1)
    goto no_drop;

  /* only check for buffers */
  if (G_UNLIKELY (!GST_IS_BUFFER (obj)))
    goto not_buffer;

  /* can't do check if we don't have a timestamp */
  if (G_UNLIKELY (start == -1))
    goto no_timestamp;

  /* we can add a valid stop time */
  if (stop != -1)
    max_lateness += stop;
  else
    max_lateness += start;

  /* if the jitter bigger than duration and lateness we are too late */
  if ((late = start + jitter > max_lateness)) {
    GST_DEBUG_OBJECT (basesink, "buffer is too late %" GST_TIME_FORMAT
        " > %" GST_TIME_FORMAT, GST_TIME_ARGS (start + jitter),
        GST_TIME_ARGS (max_lateness));
    /* !!emergency!!, if we did not receive anything valid for more than a 
     * second, render it anyway so the user sees something */
    if (priv->last_in_time && start - priv->last_in_time > GST_SECOND) {
      late = FALSE;
      GST_DEBUG_OBJECT (basesink,
          "**emergency** last buffer at %" GST_TIME_FORMAT " > GST_SECOND",
          GST_TIME_ARGS (priv->last_in_time));
    }
  }

done:
  if (!late) {
    priv->last_in_time = start;
  }
  return late;

  /* all is fine */
in_time:
  {
    GST_DEBUG_OBJECT (basesink, "object was scheduled in time");
    goto done;
  }
no_drop:
  {
    GST_DEBUG_OBJECT (basesink, "frame dropping disabled");
    goto done;
  }
not_buffer:
  {
    GST_DEBUG_OBJECT (basesink, "object is not a buffer");
    return FALSE;
  }
no_timestamp:
  {
    GST_DEBUG_OBJECT (basesink, "buffer has no timestamp");
    return FALSE;
  }
}

static void
gst_base_sink_do_render_stats (GstBaseSink * basesink, gboolean start)
{
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

  if (start) {
    priv->start = gst_util_get_timestamp ();
  } else {
    GstClockTime elapsed;

    priv->stop = gst_util_get_timestamp ();

    elapsed = GST_CLOCK_DIFF (priv->start, priv->stop);

    if (priv->avg_render == -1)
      priv->avg_render = elapsed;
    else
      priv->avg_render = UPDATE_RUNNING_AVG (priv->avg_render, elapsed);

    GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, basesink,
        "avg_render: %" GST_TIME_FORMAT, GST_TIME_ARGS (priv->avg_render));
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
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

  /* synchronize this object, non syncable objects return OK
   * immediatly. */
  ret = gst_base_sink_do_sync (basesink, pad, obj, &late);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto sync_failed;

  /* and now render, event or buffer. */
  if (G_LIKELY (GST_IS_BUFFER (obj))) {
    GstBuffer *buf;

    /* drop late buffers unconditionally, let's hope it's unlikely */
    if (G_UNLIKELY (late))
      goto dropped;

    buf = GST_BUFFER_CAST (obj);

    gst_base_sink_set_last_buffer (basesink, buf);

    bclass = GST_BASE_SINK_GET_CLASS (basesink);

    if (G_LIKELY (bclass->render)) {
      gint do_qos;

      /* read once, to get same value before and after */
      do_qos = g_atomic_int_get (&priv->qos_enabled);

      GST_DEBUG_OBJECT (basesink, "rendering buffer %p", obj);

      /* record rendering time for QoS and stats */
      if (do_qos)
        gst_base_sink_do_render_stats (basesink, TRUE);

      ret = bclass->render (basesink, buf);

      priv->rendered++;

      if (do_qos)
        gst_base_sink_do_render_stats (basesink, FALSE);
    }
  } else {
    GstEvent *event = GST_EVENT_CAST (obj);
    gboolean event_res = TRUE;
    GstEventType type;

    bclass = GST_BASE_SINK_GET_CLASS (basesink);

    type = GST_EVENT_TYPE (event);

    GST_DEBUG_OBJECT (basesink, "rendering event %p, type %s", obj,
        gst_event_type_get_name (type));

    if (bclass->event)
      event_res = bclass->event (basesink, event);

    if (G_LIKELY (event_res)) {
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
          break;
        default:
          break;
      }
    }
  }

done:
  gst_base_sink_perform_qos (basesink, late);

  GST_DEBUG_OBJECT (basesink, "object unref after render %p", obj);
  gst_mini_object_unref (obj);

  return ret;

  /* ERRORS */
sync_failed:
  {
    GST_DEBUG_OBJECT (basesink, "do_sync returned %s", gst_flow_get_name (ret));
    goto done;
  }
dropped:
  {
    priv->dropped++;
    GST_DEBUG_OBJECT (basesink, "buffer late, dropping");
    goto done;
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
    GstBuffer *buf;
    GstClockTime timestamp;

    buf = GST_BUFFER_CAST (obj);
    timestamp = GST_BUFFER_TIMESTAMP (buf);

    GST_DEBUG_OBJECT (basesink, "preroll buffer %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));

    gst_base_sink_set_last_buffer (basesink, buf);

    bclass = GST_BASE_SINK_GET_CLASS (basesink);
    if (bclass->preroll)
      if ((ret = bclass->preroll (basesink, buf)) != GST_FLOW_OK)
        goto preroll_failed;
  }

  /* commit state */
  if (G_LIKELY (basesink->playing_async)) {
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
      /* see if we can render now, if we can't add the object to the preroll
       * queue. */
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

    /* do something with the return value */
    ret = gst_base_sink_render_object (basesink, pad, o);
    if (ret != GST_FLOW_OK)
      goto dequeue_failed;
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
dequeue_failed:
  {
    GST_DEBUG_OBJECT (basesink, "rendering queued objects failed, reason %s",
        gst_flow_get_name (ret));
    gst_mini_object_unref (obj);
    return ret;
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

  if (G_UNLIKELY (basesink->priv->received_eos))
    goto was_eos;

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
was_eos:
  {
    GST_DEBUG_OBJECT (basesink,
        "we are EOS, dropping object, return UNEXPECTED");
    GST_PAD_PREROLL_UNLOCK (pad);
    gst_mini_object_unref (obj);
    return GST_FLOW_UNEXPECTED;
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

  GST_DEBUG_OBJECT (basesink, "event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret;

      GST_PAD_PREROLL_LOCK (pad);
      if (G_UNLIKELY (basesink->flushing))
        goto flushing;

      if (G_UNLIKELY (basesink->priv->received_eos)) {
        /* we can't accept anything when we are EOS */
        result = FALSE;
        gst_event_unref (event);
      } else {
        /* we set the received EOS flag here so that we can use it when testing if
         * we are prerolled and to refure more buffers. */
        basesink->priv->received_eos = TRUE;

        /* EOS is a prerollable object, we call the unlocked version because it
         * does not check the received_eos flag. */
        ret = gst_base_sink_queue_object_unlocked (basesink, pad,
            GST_MINI_OBJECT_CAST (event), TRUE);
        if (G_UNLIKELY (ret != GST_FLOW_OK))
          result = FALSE;
      }
      GST_PAD_PREROLL_UNLOCK (pad);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      GstFlowReturn ret;

      GST_DEBUG_OBJECT (basesink, "newsegment %p", event);

      GST_PAD_PREROLL_LOCK (pad);
      if (G_UNLIKELY (basesink->flushing))
        goto flushing;

      if (G_UNLIKELY (basesink->priv->received_eos)) {
        /* we can't accept anything when we are EOS */
        result = FALSE;
        gst_event_unref (event);
      } else {
        /* the new segment is a non prerollable item and does not block anything,
         * we need to configure the current clipping segment and insert the event 
         * in the queue to serialize it with the buffers for rendering. */
        gst_base_sink_configure_segment (basesink, pad, event,
            basesink->abidata.ABI.clip_segment);

        ret =
            gst_base_sink_queue_object_unlocked (basesink, pad,
            GST_MINI_OBJECT_CAST (event), FALSE);
        if (G_UNLIKELY (ret != GST_FLOW_OK))
          result = FALSE;
        else
          basesink->have_newsegment = TRUE;
      }
      GST_PAD_PREROLL_UNLOCK (pad);
      break;
    }
    case GST_EVENT_FLUSH_START:
      if (bclass->event)
        bclass->event (basesink, event);

      GST_DEBUG_OBJECT (basesink, "flush-start %p", event);

      /* make sure we are not blocked on the clock also clear any pending
       * eos state. */
      gst_base_sink_set_flushing (basesink, pad, TRUE);

      /* we grab the stream lock but that is not needed since setting the
       * sink to flushing would make sure no state commit is being done
       * anymore */
      GST_PAD_STREAM_LOCK (pad);
      gst_base_sink_reset_qos (basesink);
      if (basesink->priv->async_enabled) {
        /* and we need to commit our state again on the next
         * prerolled buffer */
        basesink->playing_async = TRUE;
        gst_element_lost_state (GST_ELEMENT_CAST (basesink));
      } else {
        basesink->priv->have_latency = TRUE;
        basesink->need_preroll = FALSE;
      }
      gst_base_sink_set_last_buffer (basesink, NULL);
      GST_PAD_STREAM_UNLOCK (pad);

      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (bclass->event)
        bclass->event (basesink, event);

      GST_DEBUG_OBJECT (basesink, "flush-stop %p", event);

      /* unset flushing so we can accept new data, this also flushes out any EOS
       * event. */
      gst_base_sink_set_flushing (basesink, pad, FALSE);

      /* we need new segment info after the flush. */
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (basesink->abidata.ABI.clip_segment,
          GST_FORMAT_UNDEFINED);
      basesink->have_newsegment = FALSE;

      /* for position reporting */
      GST_OBJECT_LOCK (basesink);
      basesink->priv->current_sstart = -1;
      basesink->priv->current_sstop = -1;
      GST_OBJECT_UNLOCK (basesink);

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
done:
  gst_object_unref (basesink);

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (basesink, "we are flushing");
    GST_PAD_PREROLL_UNLOCK (pad);
    result = FALSE;
    gst_event_unref (event);
    goto done;
  }
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
gst_base_sink_needs_preroll (GstBaseSink * basesink)
{
  gboolean is_prerolled, res;

  /* we have 2 cases where the PREROLL_LOCK is released:
   *  1) we are blocking in the PREROLL_LOCK and thus are prerolled.
   *  2) we are syncing on the clock
   */
  is_prerolled = basesink->have_preroll || basesink->priv->received_eos;
  res = !is_prerolled && basesink->pad_mode != GST_ACTIVATE_PULL;
  GST_DEBUG_OBJECT (basesink, "have_preroll: %d, EOS: %d => needs preroll: %d",
      basesink->have_preroll, basesink->priv->received_eos, res);

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
  GstBaseSinkClass *bclass;
  GstFlowReturn result;
  GstClockTime start = GST_CLOCK_TIME_NONE, end = GST_CLOCK_TIME_NONE;
  GstSegment *clip_segment;

  if (G_UNLIKELY (basesink->flushing))
    goto flushing;

  if (G_UNLIKELY (basesink->priv->received_eos))
    goto was_eos;

  /* for code clarity */
  clip_segment = basesink->abidata.ABI.clip_segment;

  if (G_UNLIKELY (!basesink->have_newsegment)) {
    gboolean sync;

    sync = gst_base_sink_get_sync (basesink);
    if (sync) {
      GST_ELEMENT_WARNING (basesink, STREAM, FAILED,
          (_("Internal data flow problem.")),
          ("Received buffer without a new-segment. Assuming timestamps start from 0."));
    }

    basesink->have_newsegment = TRUE;
    /* this means this sink will assume timestamps start from 0 */
    clip_segment->start = 0;
    clip_segment->stop = -1;
    basesink->segment.start = 0;
    basesink->segment.stop = -1;
  }

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  /* check if the buffer needs to be dropped, we first ask the subclass for the
   * start and end */
  if (bclass->get_times)
    bclass->get_times (basesink, buf, &start, &end);

  if (start == -1) {
    /* if the subclass does not want sync, we use our own values so that we at
     * least clip the buffer to the segment */
    gst_base_sink_get_times (basesink, buf, &start, &end);
  }

  GST_DEBUG_OBJECT (basesink, "got times start: %" GST_TIME_FORMAT
      ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  /* a dropped buffer does not participate in anything */
  if (GST_CLOCK_TIME_IS_VALID (start) &&
      (clip_segment->format == GST_FORMAT_TIME)) {
    if (G_UNLIKELY (!gst_segment_clip (clip_segment,
                GST_FORMAT_TIME, (gint64) start, (gint64) end, NULL, NULL)))
      goto out_of_segment;
  }

  /* now we can process the buffer in the queue, this function takes ownership
   * of the buffer */
  result = gst_base_sink_queue_object_unlocked (basesink, pad,
      GST_MINI_OBJECT_CAST (buf), TRUE);

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (basesink, "sink is flushing");
    gst_buffer_unref (buf);
    return GST_FLOW_WRONG_STATE;
  }
was_eos:
  {
    GST_DEBUG_OBJECT (basesink,
        "we are EOS, dropping object, return UNEXPECTED");
    gst_buffer_unref (buf);
    return GST_FLOW_UNEXPECTED;
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

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  if (G_UNLIKELY (basesink->pad_mode != GST_ACTIVATE_PUSH))
    goto wrong_mode;

  GST_PAD_PREROLL_LOCK (pad);
  result = gst_base_sink_chain_unlocked (basesink, pad, buf);
  GST_PAD_PREROLL_UNLOCK (pad);

done:
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

  basesink = GST_BASE_SINK (GST_OBJECT_PARENT (pad));

  g_assert (basesink->pad_mode == GST_ACTIVATE_PULL);

  GST_DEBUG_OBJECT (basesink, "pulling %" G_GUINT64_FORMAT ", %u",
      basesink->offset, (guint) DEFAULT_SIZE);

  result = gst_pad_pull_range (pad, basesink->offset, DEFAULT_SIZE, &buf);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto paused;

  if (G_UNLIKELY (buf == NULL))
    goto no_buffer;

  basesink->offset += GST_BUFFER_SIZE (buf);

  GST_PAD_PREROLL_LOCK (pad);
  result = gst_base_sink_chain_unlocked (basesink, pad, buf);
  GST_PAD_PREROLL_UNLOCK (pad);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto paused;

  return;

  /* ERRORS */
paused:
  {
    GST_LOG_OBJECT (basesink, "pausing task, reason %s",
        gst_flow_get_name (result));
    gst_pad_pause_task (pad);
    /* fatal errors and NOT_LINKED cause EOS */
    if (GST_FLOW_IS_FATAL (result) || result == GST_FLOW_NOT_LINKED) {
      /* FIXME, we shouldn't post EOS when we are operating in segment mode */
      gst_base_sink_event (pad, gst_event_new_eos ());
      /* EOS does not cause an ERROR message */
      if (result != GST_FLOW_UNEXPECTED) {
        GST_ELEMENT_ERROR (basesink, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("stream stopped, reason %s", gst_flow_get_name (result)));
      }
    }
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
  GstBaseSinkClass *bclass;

  bclass = GST_BASE_SINK_GET_CLASS (basesink);

  if (flushing) {
    /* unlock any subclasses, we need to do this before grabbing the
     * PREROLL_LOCK since we hold this lock before going into ::render. */
    if (bclass->unlock)
      bclass->unlock (basesink);
  }

  GST_PAD_PREROLL_LOCK (pad);
  basesink->flushing = flushing;
  if (flushing) {
    /* step 1, now that we have the PREROLL lock, clear our unlock request */
    if (bclass->unlock_stop)
      bclass->unlock_stop (basesink);

    /* set need_preroll before we unblock the clock. If the clock is unblocked
     * before timing out, we can reuse the buffer for preroll. */
    basesink->need_preroll = TRUE;

    /* step 2, unblock clock sync (if any) or any other blocking thing */
    if (basesink->clock_id) {
      gst_clock_id_unschedule (basesink->clock_id);
    }

    /* flush out the data thread if it's locked in finish_preroll, this will
     * also flush out the EOS state */
    GST_DEBUG_OBJECT (basesink,
        "flushing out data thread, need preroll to TRUE");
    gst_base_sink_preroll_queue_flush (basesink, pad);
  }
  GST_PAD_PREROLL_UNLOCK (pad);

  return TRUE;
}

static gboolean
gst_base_sink_default_activate_pull (GstBaseSink * basesink, gboolean active)
{
  gboolean result;

  if (active) {
    /* start task */
    result = gst_pad_start_task (basesink->sinkpad,
        (GstTaskFunction) gst_base_sink_loop, basesink->sinkpad);
  } else {
    /* step 2, make sure streaming finishes */
    result = gst_pad_stop_task (basesink->sinkpad);
  }

  return result;
}

static gboolean
gst_base_sink_pad_activate (GstPad * pad)
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
gst_base_sink_pad_activate_push (GstPad * pad, gboolean active)
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
      gst_base_sink_set_flushing (basesink, pad, TRUE);
      result = TRUE;
      basesink->pad_mode = GST_ACTIVATE_NONE;
    }
  }

  gst_object_unref (basesink);

  return result;
}

static gboolean
gst_base_sink_negotiate_pull (GstBaseSink * basesink)
{
  GstCaps *caps;
  GstPad *pad;

  GST_OBJECT_LOCK (basesink);
  pad = basesink->sinkpad;
  gst_object_ref (pad);
  GST_OBJECT_UNLOCK (basesink);

  caps = gst_pad_get_allowed_caps (pad);
  if (gst_caps_is_empty (caps))
    goto no_caps_possible;

  caps = gst_caps_make_writable (caps);
  gst_caps_truncate (caps);
  gst_pad_fixate_caps (pad, caps);

  if (gst_caps_is_any (caps)) {
    GST_DEBUG_OBJECT (basesink, "caps were ANY after fixating, "
        "allowing pull()");
    /* neither side has template caps in this case, so they are prepared for
       pull() without setcaps() */
  } else {
    if (!gst_pad_set_caps (pad, caps))
      goto could_not_set_caps;
  }

  gst_caps_unref (caps);
  gst_object_unref (pad);

  return TRUE;

no_caps_possible:
  {
    GST_INFO_OBJECT (basesink, "Pipeline could not agree on caps");
    GST_DEBUG_OBJECT (basesink, "get_allowed_caps() returned EMPTY");
    gst_object_unref (pad);
    return FALSE;
  }
could_not_set_caps:
  {
    GST_INFO_OBJECT (basesink, "Could not set caps: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    gst_object_unref (pad);
    return FALSE;
  }
}

/* this won't get called until we implement an activate function */
static gboolean
gst_base_sink_pad_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseSink *basesink;
  GstBaseSinkClass *bclass;

  basesink = GST_BASE_SINK (gst_pad_get_parent (pad));
  bclass = GST_BASE_SINK_GET_CLASS (basesink);

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
             correct state for the new thread. also the sink set_caps function
             checks this */
          basesink->pad_mode = GST_ACTIVATE_PULL;
          if ((result = gst_base_sink_negotiate_pull (basesink))) {
            if (bclass->activate_pull)
              result = bclass->activate_pull (basesink, TRUE);
            else
              result = FALSE;
          }
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
      result = gst_base_sink_set_flushing (basesink, pad, TRUE);
      if (bclass->activate_pull)
        result &= bclass->activate_pull (basesink, FALSE);
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
  gboolean forward, result = TRUE;

  /* only push UPSTREAM events upstream */
  forward = GST_EVENT_IS_UPSTREAM (event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
    {
      GstClockTime latency;

      gst_event_parse_latency (event, &latency);

      /* store the latency. We use this to adjust the running_time before syncing
       * it to the clock. */
      GST_OBJECT_LOCK (element);
      basesink->priv->latency = latency;
      GST_OBJECT_UNLOCK (element);
      GST_DEBUG_OBJECT (basesink, "latency set to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (latency));

      /* don't forward, yet. FIXME. The latency event should likely be forwarded
       * to upstream element so that they can configure themselves. Each element
       * would subtract the amount of LATENCY it can maximally compensate for. 
       * It's currently not very useful; even if this sink cannot compensate for
       * all the latency, upstream will block while this sink waits which will
       * trigger implicit buffering and latency there. */
      forward = FALSE;
      break;
    }
    default:
      break;
  }

  if (forward) {
    GST_OBJECT_LOCK (element);
    pad = gst_object_ref (basesink->sinkpad);
    GST_OBJECT_UNLOCK (element);

    result = gst_pad_push_event (pad, event);

    gst_object_unref (pad);
  } else {
    /* not forwarded, unref the event */
    gst_event_unref (event);
  }
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

/* get the end position of the last seen object, this is used
 * for EOS and for making sure that we don't report a position we
 * have not reached yet. */
static gboolean
gst_base_sink_get_position_last (GstBaseSink * basesink, gint64 * cur)
{
  /* return last observed stream time */
  *cur = basesink->priv->current_sstop;

  GST_DEBUG_OBJECT (basesink, "POSITION: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*cur));
  return TRUE;
}

/* get the position when we are PAUSED, this is the stream time of the buffer
 * that prerolled. If no buffer is prerolled (we are still flushing), this
 * value will be -1. */
static gboolean
gst_base_sink_get_position_paused (GstBaseSink * basesink, gint64 * cur)
{
  gboolean res;
  gint64 time;
  GstSegment *segment;

  *cur = basesink->priv->current_sstart;
  segment = basesink->abidata.ABI.clip_segment;

  time = segment->time;

  if (*cur != -1) {
    *cur = MAX (*cur, time);
    GST_DEBUG_OBJECT (basesink, "POSITION as max: %" GST_TIME_FORMAT
        ", time %" GST_TIME_FORMAT, GST_TIME_ARGS (*cur), GST_TIME_ARGS (time));
  } else {
    /* we have no buffer, use the segment times. */
    if (segment->rate >= 0.0) {
      /* forward, next position is always the time of the segment */
      *cur = time;
      GST_DEBUG_OBJECT (basesink, "POSITION as time: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (*cur));
    } else {
      /* reverse, next expected timestamp is segment->stop. We use the function
       * to get things right for negative applied_rates. */
      *cur =
          gst_segment_to_stream_time (segment, GST_FORMAT_TIME, segment->stop);
      GST_DEBUG_OBJECT (basesink, "reverse POSITION: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (*cur));
    }
  }
  res = (*cur != -1);

  return res;
}

static gboolean
gst_base_sink_get_position (GstBaseSink * basesink, GstFormat format,
    gint64 * cur)
{
  GstClock *clock;
  gboolean res = FALSE;

  switch (format) {
      /* we can answer time format */
    case GST_FORMAT_TIME:
    {
      GstClockTime now, base, latency;
      gint64 time, accum, duration;
      gdouble rate;
      gint64 last;

      GST_OBJECT_LOCK (basesink);

      /* can only give answer based on the clock if not EOS */
      if (G_UNLIKELY (basesink->eos))
        goto in_eos;

      /* in PAUSE we cannot read from the clock so we
       * report time based on the last seen timestamp. */
      if (GST_STATE (basesink) == GST_STATE_PAUSED)
        goto in_pause;

      /* We get position from clock only in PLAYING, we checked
       * the PAUSED case above, so this is check is to test 
       * READY and NULL, where the position is always 0 */
      if (GST_STATE (basesink) != GST_STATE_PLAYING)
        goto wrong_state;

      /* we need to sync on the clock. */
      if (basesink->sync == FALSE)
        goto no_sync;

      /* and we need a clock */
      if (G_UNLIKELY ((clock = GST_ELEMENT_CLOCK (basesink)) == NULL))
        goto no_sync;

      /* collect all data we need holding the lock */
      if (GST_CLOCK_TIME_IS_VALID (basesink->segment.time))
        time = basesink->segment.time;
      else
        time = 0;

      if (GST_CLOCK_TIME_IS_VALID (basesink->segment.stop))
        duration = basesink->segment.stop - basesink->segment.start;
      else
        duration = 0;

      base = GST_ELEMENT_CAST (basesink)->base_time;
      accum = basesink->segment.accum;
      rate = basesink->segment.rate * basesink->segment.applied_rate;
      gst_base_sink_get_position_last (basesink, &last);
      latency = basesink->priv->latency;

      gst_object_ref (clock);
      /* need to release the object lock before we can get the time, 
       * a clock might take the LOCK of the provider, which could be
       * a basesink subclass. */
      GST_OBJECT_UNLOCK (basesink);

      now = gst_clock_get_time (clock);

      /* subtract base time and accumulated time from the clock time. 
       * Make sure we don't go negative. This is the current time in
       * the segment which we need to scale with the combined 
       * rate and applied rate. */
      base += accum;
      base += latency;
      base = MIN (now, base);

      /* for negative rates we need to count back from from the segment
       * duration. */
      if (rate < 0.0)
        time += duration;

      *cur = time + gst_guint64_to_gdouble (now - base) * rate;

      /* never report more than last seen position */
      if (last != -1)
        *cur = MIN (last, *cur);

      gst_object_unref (clock);

      res = TRUE;

      GST_DEBUG_OBJECT (basesink,
          "now %" GST_TIME_FORMAT " - base %" GST_TIME_FORMAT " - accum %"
          GST_TIME_FORMAT " + time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (now), GST_TIME_ARGS (base),
          GST_TIME_ARGS (accum), GST_TIME_ARGS (time));
      break;
    }
    default:
      /* cannot answer other than TIME, we return FALSE, which will
       * send the query upstream. */
      break;
  }

done:
  GST_DEBUG_OBJECT (basesink, "res: %d, POSITION: %" GST_TIME_FORMAT,
      res, GST_TIME_ARGS (*cur));
  return res;

  /* special cases */
in_eos:
  {
    GST_DEBUG_OBJECT (basesink, "position in EOS");
    res = gst_base_sink_get_position_last (basesink, cur);
    GST_OBJECT_UNLOCK (basesink);
    goto done;
  }
in_pause:
  {
    GST_DEBUG_OBJECT (basesink, "position in PAUSED");
    res = gst_base_sink_get_position_paused (basesink, cur);
    GST_OBJECT_UNLOCK (basesink);
    goto done;
  }
wrong_state:
  {
    /* in NULL or READY we always return 0 */
    GST_DEBUG_OBJECT (basesink, "position in wrong state, return -1");
    res = FALSE;
    *cur = -1;
    GST_OBJECT_UNLOCK (basesink);
    goto done;
  }
no_sync:
  {
    /* report last seen timestamp if any, else return FALSE so
     * that upstream can answer */
    if ((*cur = basesink->priv->current_sstart) != -1)
      res = TRUE;
    GST_DEBUG_OBJECT (basesink, "no sync, res %d, POSITION %" GST_TIME_FORMAT,
        res, GST_TIME_ARGS (*cur));
    GST_OBJECT_UNLOCK (basesink);
    return res;
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

      GST_DEBUG_OBJECT (basesink, "position format %d", format);

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
      GST_DEBUG_OBJECT (basesink, "duration query");
      res = gst_base_sink_peer_query (basesink, query);
      break;
    case GST_QUERY_LATENCY:
    {
      gboolean live, us_live;
      GstClockTime min, max;

      if ((res = gst_base_sink_query_latency (basesink, &live, &us_live, &min,
                  &max))) {
        gst_query_set_latency (query, live, min, max);
      }
      break;
    }
    case GST_QUERY_JITTER:
      break;
    case GST_QUERY_RATE:
      /* gst_query_set_rate (query, basesink->segment_rate); */
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
  GstBaseSinkPrivate *priv;

  priv = basesink->priv;

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
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      GST_DEBUG_OBJECT (basesink, "READY to PAUSED");
      gst_segment_init (&basesink->segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (basesink->abidata.ABI.clip_segment,
          GST_FORMAT_UNDEFINED);
      basesink->have_newsegment = FALSE;
      basesink->offset = 0;
      basesink->have_preroll = FALSE;
      basesink->need_preroll = TRUE;
      basesink->playing_async = TRUE;
      priv->current_sstart = -1;
      priv->current_sstop = -1;
      priv->eos_rtime = -1;
      priv->latency = 0;
      basesink->eos = FALSE;
      priv->received_eos = FALSE;
      gst_base_sink_reset_qos (basesink);
      priv->commited = FALSE;
      if (priv->async_enabled) {
        GST_DEBUG_OBJECT (basesink, "doing async state change");
        /* when async enabled, post async-start message and return ASYNC from
         * the state change function */
        ret = GST_STATE_CHANGE_ASYNC;
        gst_element_post_message (GST_ELEMENT_CAST (basesink),
            gst_message_new_async_start (GST_OBJECT_CAST (basesink), FALSE));
      } else {
        priv->have_latency = TRUE;
      }
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      if (!gst_base_sink_needs_preroll (basesink)) {
        GST_DEBUG_OBJECT (basesink, "PAUSED to PLAYING, don't need preroll");
        /* no preroll needed anymore now. */
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
        GST_DEBUG_OBJECT (basesink, "PAUSED to PLAYING, we are not prerolled");
        basesink->need_preroll = TRUE;
        basesink->playing_async = TRUE;
        priv->commited = FALSE;
        if (priv->async_enabled) {
          GST_DEBUG_OBJECT (basesink, "doing async state change");
          ret = GST_STATE_CHANGE_ASYNC;
          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_async_start (GST_OBJECT_CAST (basesink), FALSE));
        }
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
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* note that this is the upward case, which doesn't follow most
         patterns */
      if (basesink->pad_mode == GST_ACTIVATE_PULL) {
        GST_DEBUG_OBJECT (basesink, "basesink activated in pull mode, "
            "returning SUCCESS directly");
        GST_PAD_PREROLL_LOCK (basesink->sinkpad);
        gst_element_post_message (GST_ELEMENT_CAST (basesink),
            gst_message_new_async_done (GST_OBJECT_CAST (basesink)));
        GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
        ret = GST_STATE_CHANGE_SUCCESS;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT (basesink, "PLAYING to PAUSED");
      /* FIXME, make sure we cannot enter _render first */

      /* we need to call ::unlock before locking PREROLL_LOCK
       * since we lock it before going into ::render */
      if (bclass->unlock)
        bclass->unlock (basesink);

      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      /* now that we have the PREROLL lock, clear our unlock request */
      if (bclass->unlock_stop)
        bclass->unlock_stop (basesink);

      /* we need preroll again and we set the flag before unlocking the clockid
       * because if the clockid is unlocked before a current buffer expired, we
       * can use that buffer to preroll with */
      basesink->need_preroll = TRUE;

      if (basesink->clock_id) {
        gst_clock_id_unschedule (basesink->clock_id);
      }

      /* if we don't have a preroll buffer we need to wait for a preroll and
       * return ASYNC. */
      if (!gst_base_sink_needs_preroll (basesink)) {
        GST_DEBUG_OBJECT (basesink, "PLAYING to PAUSED, we are prerolled");
        basesink->playing_async = FALSE;
      } else {
        if (GST_STATE_TARGET (GST_ELEMENT (basesink)) <= GST_STATE_READY) {
          ret = GST_STATE_CHANGE_SUCCESS;
        } else {
          GST_DEBUG_OBJECT (basesink,
              "PLAYING to PAUSED, we are not prerolled");
          basesink->playing_async = TRUE;
          priv->commited = FALSE;
          if (priv->async_enabled) {
            GST_DEBUG_OBJECT (basesink, "doing async state change");
            ret = GST_STATE_CHANGE_ASYNC;
            gst_element_post_message (GST_ELEMENT_CAST (basesink),
                gst_message_new_async_start (GST_OBJECT_CAST (basesink),
                    FALSE));
          }
        }
      }
      GST_DEBUG_OBJECT (basesink, "rendered: %" G_GUINT64_FORMAT
          ", dropped: %" G_GUINT64_FORMAT, priv->rendered, priv->dropped);

      gst_base_sink_reset_qos (basesink);
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PAD_PREROLL_LOCK (basesink->sinkpad);
      if (!priv->commited) {
        if (priv->async_enabled) {
          GST_DEBUG_OBJECT (basesink, "PAUSED to READY, posting async-done");

          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_state_changed (GST_OBJECT_CAST (basesink),
                  GST_STATE_PLAYING, GST_STATE_PAUSED, GST_STATE_READY));

          gst_element_post_message (GST_ELEMENT_CAST (basesink),
              gst_message_new_async_done (GST_OBJECT_CAST (basesink)));
        }
        priv->commited = TRUE;
      } else {
        GST_DEBUG_OBJECT (basesink, "PAUSED to READY, don't need_preroll");
      }
      priv->current_sstart = -1;
      priv->current_sstop = -1;
      priv->have_latency = FALSE;
      gst_base_sink_set_last_buffer (basesink, NULL);
      GST_PAD_PREROLL_UNLOCK (basesink->sinkpad);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (bclass->stop) {
        if (!bclass->stop (basesink)) {
          GST_WARNING_OBJECT (basesink, "failed to stop");
        }
      }
      gst_base_sink_set_last_buffer (basesink, NULL);
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
