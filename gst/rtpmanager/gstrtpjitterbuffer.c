/*
 * Farsight Voice+Video library
 *
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
 *  Copyright 2007 Wim Taymans <wim@fluendo.com>
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
 *
 */

/**
 * SECTION:element-gstrtpjitterbuffer
 * @short_description: buffer, reorder and remove duplicate RTP packets to
 * compensate for network oddities.
 *
 * <refsect2>
 * <para>
 * This element reorders and removes duplicate RTP packets as they are received
 * from a network source. It will also wait for missing packets up to a
 * configurable time limit using the ::latency property. Packets arriving too
 * late are considered to be lost packets.
 * </para>
 * <para>
 * This element acts as a live element and so adds ::latency to the pipeline.
 * </para>
 * <para>
 * The element needs the clock-rate of the RTP payload in order to estimate the
 * delay. This information is obtained either from the caps on the sink pad or,
 * when no caps are present, from the ::request-pt-map signal. To clear the
 * previous pt-map use the ::clear-pt-map signal.
 * </para>
 * <para>
 * This element will automatically be used inside gstrtpbin.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch rtspsrc location=rtsp://192.168.1.133:8554/mpeg1or2AudioVideoTest ! gstrtpjitterbuffer ! rtpmpvdepay ! mpeg2dec ! xvimagesink
 * </programlisting>
 * Connect to a streaming server and decode the MPEG video. The jitterbuffer is
 * inserted into the pipeline to smooth out network jitter and to reorder the
 * out-of-order RTP packets.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"

#include "gstrtpjitterbuffer.h"
#include "async_jitter_queue.h"

GST_DEBUG_CATEGORY (rtpjitterbuffer_debug);
#define GST_CAT_DEFAULT (rtpjitterbuffer_debug)

/* low and high threshold tell the queue when to start and stop buffering */
#define LOW_THRESHOLD 0.2
#define HIGH_THRESHOLD 0.8

/* elementfactory information */
static const GstElementDetails gst_rtp_jitter_buffer_details =
GST_ELEMENT_DETAILS ("RTP packet jitter-buffer",
    "Filter/Network/RTP",
    "A buffer that deals with network jitter and other transmission faults",
    "Philippe Kalaf <philippe.kalaf@collabora.co.uk>, "
    "Wim Taymans <wim@fluendo.com>");

/* RTPJitterBuffer signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS      200
#define DEFAULT_DROP_ON_LATENCY FALSE

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_DROP_ON_LATENCY
};

struct _GstRTPJitterBufferPrivate
{
  GstPad *sinkpad, *srcpad;

  AsyncJitterQueue *queue;

  /* properties */
  guint latency_ms;
  gboolean drop_on_latency;

  /* the last seqnum we pushed out */
  guint32 last_popped_seqnum;
  /* the next expected seqnum */
  guint32 next_seqnum;

  /* clock rate and rtp timestamp offset */
  gint32 clock_rate;
  gint64 clock_base;

  /* when we are shutting down */
  GstFlowReturn srcresult;

  /* for sync */
  GstSegment segment;
  GstClockID clock_id;
  guint32 waiting_seqnum;

  /* some accounting */
  guint64 num_late;
  guint64 num_duplicates;
};

#define GST_RTP_JITTER_BUFFER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_RTP_JITTER_BUFFER, \
                                GstRTPJitterBufferPrivate))

static GstStaticPadTemplate gst_rtp_jitter_buffer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "clock-rate = (int) [ 1, 2147483647 ]"
        /* "payload = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static GstStaticPadTemplate gst_rtp_jitter_buffer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"
        /* "payload = (int) , "
         * "clock-rate = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static guint gst_rtp_jitter_buffer_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstRTPJitterBuffer, gst_rtp_jitter_buffer, GstElement,
    GST_TYPE_ELEMENT);

/* object overrides */
static void gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_dispose (GObject * object);

/* element overrides */
static GstStateChangeReturn gst_rtp_jitter_buffer_change_state (GstElement
    * element, GstStateChange transition);

/* pad overrides */
static GstCaps *gst_rtp_jitter_buffer_getcaps (GstPad * pad);

/* sinkpad overrides */
static gboolean gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_rtp_jitter_buffer_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain (GstPad * pad,
    GstBuffer * buffer);

/* srcpad overrides */
static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active);
static void gst_rtp_jitter_buffer_loop (GstRTPJitterBuffer * jitterbuffer);
static gboolean gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query);

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRTPJitterBuffer * jitterbuffer);

static void
gst_rtp_jitter_buffer_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_jitter_buffer_details);
}

static void
gst_rtp_jitter_buffer_class_init (GstRTPJitterBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRTPJitterBufferPrivate));

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_dispose);

  gobject_class->set_property = gst_rtp_jitter_buffer_set_property;
  gobject_class->get_property = gst_rtp_jitter_buffer_get_property;

  /**
   * GstRTPJitterBuffer::latency:
   * 
   * The maximum latency of the jitterbuffer. Packets will be kept in the buffer
   * for at most this time.
   */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE));
  /**
   * GstRTPJitterBuffer::drop-on-latency:
   * 
   * Drop oldest buffers when the queue is completely filled. 
   */
  g_object_class_install_property (gobject_class, PROP_DROP_ON_LATENCY,
      g_param_spec_boolean ("drop-on-latency",
          "Drop buffers when maximum latency is reached",
          "Tells the jitterbuffer to never exceed the given latency in size",
          DEFAULT_DROP_ON_LATENCY, G_PARAM_READWRITE));
  /**
   * GstRTPJitterBuffer::request-pt-map:
   * @buffer: the object which received the signal
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPJitterBufferClass,
          request_pt_map), NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT,
      GST_TYPE_CAPS, 1, G_TYPE_UINT);
  /**
   * GstRTPJitterBuffer::clear-pt-map:
   * @buffer: the object which received the signal
   *
   * Invalidate the clock-rate as obtained with the ::request-pt-map signal.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPJitterBufferClass,
          clear_pt_map), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gstelement_class->change_state = gst_rtp_jitter_buffer_change_state;

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_clear_pt_map);

  GST_DEBUG_CATEGORY_INIT
      (rtpjitterbuffer_debug, "rtpjitterbuffer", 0, "RTP Jitter Buffer");
}

static void
gst_rtp_jitter_buffer_init (GstRTPJitterBuffer * jitterbuffer,
    GstRTPJitterBufferClass * klass)
{
  GstRTPJitterBufferPrivate *priv;

  priv = GST_RTP_JITTER_BUFFER_GET_PRIVATE (jitterbuffer);
  jitterbuffer->priv = priv;

  priv->latency_ms = DEFAULT_LATENCY_MS;
  priv->drop_on_latency = DEFAULT_DROP_ON_LATENCY;

  priv->queue = async_jitter_queue_new ();
  async_jitter_queue_set_low_threshold (priv->queue, LOW_THRESHOLD);
  async_jitter_queue_set_high_threshold (priv->queue, HIGH_THRESHOLD);

  priv->waiting_seqnum = -1;

  priv->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_src_template,
      "src");

  gst_pad_set_activatepush_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_activate_push));
  gst_pad_set_query_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_query));
  gst_pad_set_getcaps_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));

  priv->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_sink_template,
      "sink");

  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_chain));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_sink_event));
  gst_pad_set_setcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jitter_buffer_sink_setcaps));
  gst_pad_set_getcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));

  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->sinkpad);
}

static void
gst_rtp_jitter_buffer_dispose (GObject * object)
{
  GstRTPJitterBuffer *jitterbuffer;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  if (jitterbuffer->priv->queue) {
    async_jitter_queue_unref (jitterbuffer->priv->queue);
    jitterbuffer->priv->queue = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRTPJitterBuffer * jitterbuffer)
{
  GstRTPJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  /* this will trigger a new pt-map request signal, FIXME, do something better. */
  priv->clock_rate = -1;
}

static GstCaps *
gst_rtp_jitter_buffer_getcaps (GstPad * pad)
{
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;
  GstPad *other;
  GstCaps *caps;
  const GstCaps *templ;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  other = (pad == priv->srcpad ? priv->sinkpad : priv->srcpad);

  caps = gst_pad_peer_get_caps (other);

  templ = gst_pad_get_pad_template_caps (pad);
  if (caps == NULL) {
    GST_DEBUG_OBJECT (jitterbuffer, "copy template");
    caps = gst_caps_copy (templ);
  } else {
    GstCaps *intersect;

    GST_DEBUG_OBJECT (jitterbuffer, "intersect with template");

    intersect = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);

    caps = intersect;
  }
  gst_object_unref (jitterbuffer);

  return caps;
}

static gboolean
gst_jitter_buffer_sink_parse_caps (GstRTPJitterBuffer * jitterbuffer,
    GstCaps * caps)
{
  GstRTPJitterBufferPrivate *priv;
  GstStructure *caps_struct;
  const GValue *value;

  priv = jitterbuffer->priv;

  /* first parse the caps */
  caps_struct = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (jitterbuffer, "got caps");

  /* we need a clock-rate to convert the rtp timestamps to GStreamer time and to
   * measure the amount of data in the buffer */
  if (!gst_structure_get_int (caps_struct, "clock-rate", &priv->clock_rate))
    goto error;

  if (priv->clock_rate <= 0)
    goto wrong_rate;

  GST_DEBUG_OBJECT (jitterbuffer, "got clock-rate %d", priv->clock_rate);

  /* gah, clock-base is uint. If we don't have a base, we will use the first
   * buffer timestamp as the base time. This will screw up sync but it's better
   * than nothing. */
  value = gst_structure_get_value (caps_struct, "clock-base");
  if (value && G_VALUE_HOLDS_UINT (value)) {
    priv->clock_base = g_value_get_uint (value);
    GST_DEBUG_OBJECT (jitterbuffer, "got clock-base %" G_GINT64_FORMAT,
        priv->clock_base);
  } else
    priv->clock_base = -1;

  /* first expected seqnum */
  value = gst_structure_get_value (caps_struct, "seqnum-base");
  if (value && G_VALUE_HOLDS_UINT (value)) {
    priv->next_seqnum = g_value_get_uint (value);
    GST_DEBUG_OBJECT (jitterbuffer, "got seqnum-base %d", priv->next_seqnum);
  } else
    priv->next_seqnum = -1;

  async_jitter_queue_set_max_queue_length (priv->queue,
      priv->latency_ms * priv->clock_rate / 1000);

  return TRUE;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "No clock-rate in caps!");
    return FALSE;
  }
wrong_rate:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "Invalid clock-rate %d", priv->clock_rate);
    return FALSE;
  }
}

static gboolean
gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;
  gboolean res;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);

  /* set same caps on srcpad on success */
  if (res)
    gst_pad_set_caps (priv->srcpad, caps);

  gst_object_unref (jitterbuffer);

  return res;
}

static void
free_func (gpointer data, GstRTPJitterBuffer * user_data)
{
  if (GST_IS_BUFFER (data))
    gst_buffer_unref (GST_BUFFER_CAST (data));
  else
    gst_event_unref (GST_EVENT_CAST (data));
}

static void
gst_rtp_jitter_buffer_flush_start (GstRTPJitterBuffer * jitterbuffer)
{
  GstRTPJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  async_jitter_queue_lock (priv->queue);
  /* mark ourselves as flushing */
  priv->srcresult = GST_FLOW_WRONG_STATE;
  GST_DEBUG_OBJECT (jitterbuffer, "Disabling pop on queue");
  /* this unblocks any waiting pops on the src pad task */
  async_jitter_queue_set_flushing_unlocked (jitterbuffer->priv->queue,
      (GFunc) free_func, jitterbuffer);
  /* unlock clock, we just unschedule, the entry will be released by the 
   * locking streaming thread. */
  if (priv->clock_id)
    gst_clock_id_unschedule (priv->clock_id);

  async_jitter_queue_unlock (priv->queue);
}

static void
gst_rtp_jitter_buffer_flush_stop (GstRTPJitterBuffer * jitterbuffer)
{
  GstRTPJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  async_jitter_queue_lock (priv->queue);
  GST_DEBUG_OBJECT (jitterbuffer, "Enabling pop on queue");
  /* Mark as non flushing */
  priv->srcresult = GST_FLOW_OK;
  gst_segment_init (&priv->segment, GST_FORMAT_TIME);
  priv->last_popped_seqnum = -1;
  priv->next_seqnum = -1;
  priv->clock_rate = -1;
  /* allow pops from the src pad task */
  async_jitter_queue_unset_flushing_unlocked (jitterbuffer->priv->queue);
  async_jitter_queue_unlock (priv->queue);
}

static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstRTPJitterBuffer *jitterbuffer = NULL;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (active) {
    /* allow data processing */
    gst_rtp_jitter_buffer_flush_stop (jitterbuffer);

    /* start pushing out buffers */
    GST_DEBUG_OBJECT (jitterbuffer, "Starting task on srcpad");
    gst_pad_start_task (jitterbuffer->priv->srcpad,
        (GstTaskFunction) gst_rtp_jitter_buffer_loop, jitterbuffer);
  } else {
    /* make sure all data processing stops ASAP */
    gst_rtp_jitter_buffer_flush_start (jitterbuffer);

    /* NOTE this will hardlock if the state change is called from the src pad
     * task thread because we will _join() the thread. */
    GST_DEBUG_OBJECT (jitterbuffer, "Stopping task on srcpad");
    result = gst_pad_stop_task (pad);
  }

  gst_object_unref (jitterbuffer);

  return result;
}

static GstStateChangeReturn
gst_rtp_jitter_buffer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  jitterbuffer = GST_RTP_JITTER_BUFFER (element);
  priv = jitterbuffer->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      async_jitter_queue_lock (priv->queue);
      /* reset negotiated values */
      priv->clock_rate = -1;
      priv->clock_base = -1;
      /* block until we go to PLAYING */
      async_jitter_queue_set_blocking_unlocked (jitterbuffer->priv->queue,
          TRUE);
      async_jitter_queue_unlock (priv->queue);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      async_jitter_queue_lock (priv->queue);
      /* unblock to allow streaming in PLAYING */
      async_jitter_queue_set_blocking_unlocked (jitterbuffer->priv->queue,
          FALSE);
      async_jitter_queue_unlock (priv->queue);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* we are a live element because we sync to the clock, which we can only
       * do in the PLAYING state */
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      async_jitter_queue_lock (priv->queue);
      /* block to stop streaming when PAUSED */
      async_jitter_queue_set_blocking_unlocked (jitterbuffer->priv->queue,
          TRUE);
      async_jitter_queue_unlock (priv->queue);
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

/**
 * Performs comparison 'b - a' with check for overflows.
 */
static inline gint
priv_compare_rtp_seq_lt (guint16 a, guint16 b)
{
  /* check if diff more than half of the 16bit range */
  if (abs (b - a) > (1 << 15)) {
    /* one of a/b has wrapped */
    return a - b;
  } else {
    return b - a;
  }
}

/**
 * gets the seqnum from the buffers and compare them 
 */
static gint
compare_rtp_buffers_seq_num (GstBuffer * a, GstBuffer * b)
{
  gint ret;

  if (GST_IS_BUFFER (a) && GST_IS_BUFFER (b)) {
    /* two buffers */
    ret = priv_compare_rtp_seq_lt
        (gst_rtp_buffer_get_seq (GST_BUFFER_CAST (a)),
        gst_rtp_buffer_get_seq (GST_BUFFER_CAST (b)));
  } else {
    /* one of them is an event, the event always goes before the other element
     * so we return -1. */
    if (GST_IS_EVENT (a))
      ret = -1;
    else
      ret = 1;
  }
  return ret;
}

static gboolean
gst_rtp_jitter_buffer_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (jitterbuffer,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      gst_segment_set_newsegment_full (&priv->segment, update,
          rate, arate, format, start, stop, time);

      /* FIXME, push SEGMENT in the queue. Sorting order might be difficult. */
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_rtp_jitter_buffer_flush_start (jitterbuffer);
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (priv->srcpad, event);
      ret = gst_rtp_jitter_buffer_src_activate_push (priv->srcpad, TRUE);
      break;
    case GST_EVENT_EOS:
    {
      /* push EOS in queue. We always push it at the head */
      async_jitter_queue_lock (priv->queue);
      GST_DEBUG_OBJECT (jitterbuffer, "queuing EOS");
      /* check for flushing, we need to discard the event and return FALSE when
       * we are flushing */
      ret = priv->srcresult == GST_FLOW_OK;
      if (ret)
        async_jitter_queue_push_unlocked (priv->queue, event);
      else
        gst_event_unref (event);
      async_jitter_queue_unlock (priv->queue);
      break;
    }
    default:
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
  }

done:
  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "received non TIME newsegment");
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_rtp_jitter_buffer_get_clock_rate (GstRTPJitterBuffer * jitterbuffer,
    guint8 pt)
{
  GValue ret = { 0 };
  GValue args[2] = { {0}, {0} };
  GstCaps *caps;
  gboolean res;

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], jitterbuffer);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP], 0,
      &ret);

  caps = (GstCaps *) g_value_get_boxed (&ret);
  if (!caps)
    goto no_caps;

  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);

  return res;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "could not get caps");
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;
  guint16 seqnum;
  GstFlowReturn ret;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (!gst_rtp_buffer_validate (buffer))
    goto invalid_buffer;

  priv = jitterbuffer->priv;

  if (priv->clock_rate == -1) {
    guint8 pt;

    /* no clock rate given on the caps, try to get one with the signal */
    pt = gst_rtp_buffer_get_payload_type (buffer);

    gst_rtp_jitter_buffer_get_clock_rate (jitterbuffer, pt);
    if (priv->clock_rate == -1)
      goto not_negotiated;
  }

  seqnum = gst_rtp_buffer_get_seq (buffer);
  GST_DEBUG_OBJECT (jitterbuffer, "Received packet #%d", seqnum);

  async_jitter_queue_lock (priv->queue);
  ret = priv->srcresult;
  if (ret != GST_FLOW_OK)
    goto out_flushing;

  /* let's check if this buffer is too late, we cannot accept packets with
   * bigger seqnum than the one we already pushed. */
  if (priv->last_popped_seqnum != -1) {
    if (priv_compare_rtp_seq_lt (priv->last_popped_seqnum, seqnum) < 0)
      goto too_late;
  }

  /* let's drop oldest packet if the queue is already full and drop-on-latency
   * is set. */
  if (priv->drop_on_latency) {
    if (async_jitter_queue_length_ts_units_unlocked (priv->queue) >=
        priv->latency_ms * priv->clock_rate / 1000) {
      GstBuffer *old_buf;

      GST_DEBUG_OBJECT (jitterbuffer, "Queue full, dropping old packet #%d",
          seqnum);

      old_buf = async_jitter_queue_pop_unlocked (priv->queue);
      gst_buffer_unref (old_buf);
    }
  }

  /* now insert the packet into the queue in sorted order. This function returns
   * FALSE if a packet with the same seqnum was already in the queue, meaning we
   * have a duplicate. */
  if (!async_jitter_queue_push_sorted_unlocked (priv->queue, buffer,
          (GCompareDataFunc) compare_rtp_buffers_seq_num, NULL))
    goto duplicate;

  /* let's unschedule and unblock any waiting buffers. We only want to do this
   * if there is a currently waiting newer (> seqnum) buffer  */
  if (priv->clock_id) {
    if (priv->waiting_seqnum > seqnum) {
      gst_clock_id_unschedule (priv->clock_id);
      GST_DEBUG_OBJECT (jitterbuffer, "Unscheduling waiting buffer");
    }
  }

  GST_DEBUG_OBJECT (jitterbuffer, "Pushed packet #%d on queue %d",
      seqnum, async_jitter_queue_length_unlocked (priv->queue));

finished:
  async_jitter_queue_unlock (priv->queue);

  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTP payload"));
    gst_buffer_unref (buffer);
    gst_object_unref (jitterbuffer);
    return GST_FLOW_ERROR;
  }
not_negotiated:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "No clock-rate in caps!");
    gst_buffer_unref (buffer);
    gst_object_unref (jitterbuffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
out_flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "flushing %s", gst_flow_get_name (ret));
    gst_buffer_unref (buffer);
    goto finished;
  }
too_late:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "Packet #%d too late as #%d was already"
        " popped, dropping", seqnum, priv->last_popped_seqnum);
    priv->num_late++;
    gst_buffer_unref (buffer);
    goto finished;
  }
duplicate:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "Duplicate packet #%d detected, dropping",
        seqnum);
    priv->num_duplicates++;
    gst_buffer_unref (buffer);
    goto finished;
  }
}

/**
 * This funcion will push out buffers on the source pad.
 *
 * For each pushed buffer, the seqnum is recorded, if the next buffer B has a
 * different seqnum (missing packets before B), this function will wait for the
 * missing packet to arrive up to the rtp timestamp of buffer B.
 */
static void
gst_rtp_jitter_buffer_loop (GstRTPJitterBuffer * jitterbuffer)
{
  GstRTPJitterBufferPrivate *priv;
  gpointer elem;
  GstBuffer *outbuf;
  GstFlowReturn result;
  guint16 seqnum;
  guint32 rtp_time;
  GstClockTime timestamp;
  gint64 running_time;

  priv = jitterbuffer->priv;

  async_jitter_queue_lock (priv->queue);
again:
  GST_DEBUG_OBJECT (jitterbuffer, "Popping item");
  /* pop a buffer, we will get NULL if the queue was shut down */
  elem = async_jitter_queue_pop_unlocked (priv->queue);
  if (!elem)
    goto no_elem;

  /* special code for events */
  if (G_UNLIKELY (GST_IS_EVENT (elem))) {
    GstEvent *event = GST_EVENT_CAST (elem);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        GST_DEBUG_OBJECT (jitterbuffer, "Popped EOS from queue");
        /* we don't expect more data now, makes upstream perform EOS actions */
        priv->srcresult = GST_FLOW_UNEXPECTED;
        break;
      default:
        GST_DEBUG_OBJECT (jitterbuffer, "Popped event %s from queue",
            GST_EVENT_TYPE_NAME (event));
        break;
    }
    async_jitter_queue_unlock (priv->queue);

    /* push event */
    gst_pad_push_event (priv->srcpad, event);
    return;
  }

  /* we know it's a buffer now */
  outbuf = GST_BUFFER_CAST (elem);

  seqnum = gst_rtp_buffer_get_seq (outbuf);

  GST_DEBUG_OBJECT (jitterbuffer, "Popped buffer #%d from queue %d",
      gst_rtp_buffer_get_seq (outbuf),
      async_jitter_queue_length_unlocked (priv->queue));

  /* If we don't know what the next seqnum should be (== -1) we have to wait
   * because it might be possible that we are not receiving this buffer in-order,
   * a buffer with a lower seqnum could arrive later and we want to push that
   * earlier buffer before this buffer then.
   * If we know the expected seqnum, we can compare it to the current seqnum to
   * determine if we have missing a packet. If we have a missing packet (which
   * must be before this packet) we can wait for it until the deadline for this
   * packet expires. */
  if (priv->next_seqnum == -1 || priv->next_seqnum != seqnum) {
    GstClockID id;
    GstClockTimeDiff jitter;
    GstClockReturn ret;
    GstClock *clock;

    if (priv->next_seqnum != -1) {
      /* we expected next_seqnum but received something else, that's a gap */
      GST_DEBUG_OBJECT (jitterbuffer,
          "Sequence number GAP detected -> %d instead of %d", priv->next_seqnum,
          seqnum);
    } else {
      /* we don't know what the next_seqnum should be, wait for the last
       * possible moment to push this buffer, maybe we get an earlier seqnum
       * while we wait */
      GST_DEBUG_OBJECT (jitterbuffer, "First buffer %d, do sync", seqnum);
    }

    /* get the max deadline to wait for the missing packets, this is the time
     * of the currently popped packet */
    rtp_time = gst_rtp_buffer_get_timestamp (outbuf);

    GST_DEBUG_OBJECT (jitterbuffer, "rtp_time %u, base %" G_GINT64_FORMAT,
        rtp_time, priv->clock_base);

    /* if no clock_base was given, take first ts as base */
    if (priv->clock_base == -1)
      priv->clock_base = rtp_time;

    /* take rtp timestamp offset into account, this can wrap around */
    rtp_time -= priv->clock_base;

    /* bring timestamp to gst time */
    timestamp = gst_util_uint64_scale (GST_SECOND, rtp_time, priv->clock_rate);

    GST_DEBUG_OBJECT (jitterbuffer,
        "rtptime %u, clock-rate %u, timestamp %" GST_TIME_FORMAT, rtp_time,
        priv->clock_rate, GST_TIME_ARGS (timestamp));

    /* bring to running time */
    running_time = gst_segment_to_running_time (&priv->segment, GST_FORMAT_TIME,
        timestamp);

    /* correct for sync against the gstreamer clock, add latency */
    GST_OBJECT_LOCK (jitterbuffer);
    clock = GST_ELEMENT_CLOCK (jitterbuffer);
    if (!clock) {
      GST_OBJECT_UNLOCK (jitterbuffer);
      /* let's just push if there is no clock */
      goto push_buffer;
    }

    /* add latency */
    running_time += (priv->latency_ms * GST_MSECOND);

    GST_DEBUG_OBJECT (jitterbuffer, "sync to running_time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (running_time));

    /* prepare for sync against clock */
    running_time += GST_ELEMENT_CAST (jitterbuffer)->base_time;

    /* create an entry for the clock */
    id = priv->clock_id = gst_clock_new_single_shot_id (clock, running_time);
    priv->waiting_seqnum = seqnum;
    GST_OBJECT_UNLOCK (jitterbuffer);

    /* release the lock so that the other end can push stuff or unlock */
    async_jitter_queue_unlock (priv->queue);

    ret = gst_clock_id_wait (id, &jitter);

    async_jitter_queue_lock (priv->queue);
    /* and free the entry */
    gst_clock_id_unref (id);
    priv->clock_id = NULL;
    priv->waiting_seqnum = -1;

    /* at this point, the clock could have been unlocked by a timeout, a new
     * tail element was added to the queue or because we are shutting down. Check
     * for shutdown first. */
    if (priv->srcresult != GST_FLOW_OK)
      goto flushing;

    /* if we got unscheduled and we are not flushing, it's because a new tail
     * element became available in the queue. Grab it and try to push or sync. */
    if (ret == GST_CLOCK_UNSCHEDULED) {
      GST_DEBUG_OBJECT (jitterbuffer,
          "Wait got unscheduled, will retry to push with new buffer");
      /* reinserting popped buffer into queue */
      if (!async_jitter_queue_push_sorted_unlocked (priv->queue, outbuf,
              (GCompareDataFunc) compare_rtp_buffers_seq_num, NULL)) {
        GST_DEBUG_OBJECT (jitterbuffer,
            "Duplicate packet #%d detected, dropping", seqnum);
        priv->num_duplicates++;
        gst_buffer_unref (outbuf);
      }
      goto again;
    }
  }
push_buffer:
  /* check if we are pushing something unexpected */
  if (priv->next_seqnum != -1 && priv->next_seqnum != seqnum) {
    gint dropped;

    /* calc number of missing packets, careful for wraparounds */
    dropped = priv_compare_rtp_seq_lt (priv->next_seqnum, seqnum);

    GST_DEBUG_OBJECT (jitterbuffer,
        "Pushing DISCONT after dropping %d (%d to %d)", dropped,
        priv->next_seqnum, seqnum);

    /* update stats */
    priv->num_late += dropped;

    /* set DISCONT flag */
    outbuf = gst_buffer_make_metadata_writable (outbuf);
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }
  /* now we are ready to push the buffer. Save the seqnum and release the lock
   * so the other end can push stuff in the queue again. */
  priv->last_popped_seqnum = seqnum;
  priv->next_seqnum = (seqnum + 1) & 0xffff;
  async_jitter_queue_unlock (priv->queue);

  /* push buffer */
  GST_DEBUG_OBJECT (jitterbuffer, "Pushing buffer %d", seqnum);
  result = gst_pad_push (priv->srcpad, outbuf);
  if (result != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
no_elem:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (jitterbuffer, "Pop returned NULL, we're flushing");
    priv->srcresult = GST_FLOW_WRONG_STATE;
    gst_pad_pause_task (priv->srcpad);
    async_jitter_queue_unlock (priv->queue);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "we are flushing");
    gst_buffer_unref (outbuf);
    async_jitter_queue_unlock (priv->queue);
    return;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (result);

    GST_DEBUG_OBJECT (jitterbuffer, "pausing task, reason %s", reason);

    async_jitter_queue_lock (priv->queue);
    /* store result */
    priv->srcresult = result;
    /* we don't post errors or anything because upstream will do that for us
     * when we pass the return value upstream. */
    gst_pad_pause_task (priv->srcpad);
    async_jitter_queue_unlock (priv->queue);
    return;
  }
}

static gboolean
gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query)
{
  GstRTPJitterBuffer *jitterbuffer;
  GstRTPJitterBufferPrivate *priv;
  gboolean res = FALSE;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstPad *peer;

      if ((peer = gst_pad_get_peer (priv->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

          GST_DEBUG_OBJECT (jitterbuffer, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

          min_latency += priv->latency_ms * GST_MSECOND;
          max_latency += priv->latency_ms * GST_MSECOND;

          GST_DEBUG_OBJECT (jitterbuffer, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

          gst_query_set_latency (query, TRUE, min_latency, max_latency);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;
}

static void
gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPJitterBuffer *jitterbuffer = GST_RTP_JITTER_BUFFER (object);

  switch (prop_id) {
    case PROP_LATENCY:
    {
      guint new_latency, old_latency;

      /* FIXME, not threadsafe */
      new_latency = g_value_get_uint (value);
      old_latency = jitterbuffer->priv->latency_ms;

      jitterbuffer->priv->latency_ms = new_latency;
      if (jitterbuffer->priv->clock_rate != -1) {
        async_jitter_queue_set_max_queue_length (jitterbuffer->priv->queue,
            gst_util_uint64_scale_int (new_latency,
                jitterbuffer->priv->clock_rate, 1000));
      }
      /* post message if latency changed, this will infor the parent pipeline
       * that a latency reconfiguration is possible. */
      if (new_latency != old_latency) {
        gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer),
            gst_message_new_latency (GST_OBJECT_CAST (jitterbuffer)));
      }
      break;
    }
    case PROP_DROP_ON_LATENCY:
    {
      jitterbuffer->priv->drop_on_latency = g_value_get_boolean (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPJitterBuffer *jitterbuffer = GST_RTP_JITTER_BUFFER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_uint (value, jitterbuffer->priv->latency_ms);
      break;
    case PROP_DROP_ON_LATENCY:
      g_value_set_boolean (value, jitterbuffer->priv->drop_on_latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
