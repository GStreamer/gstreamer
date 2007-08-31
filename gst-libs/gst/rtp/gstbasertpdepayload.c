/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org> 
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
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
 * SECTION:gstbasertpdepayload
 * @short_description: Base class for RTP depayloader
 *
 * <refsect2>
 * <para>
 * Provides a base class for RTP depayloaders
 * </para>
 * </refsect2>
 */

#include "gstbasertpdepayload.h"

#ifdef GST_DISABLE_DEPRECATED
#define QUEUE_LOCK_INIT(base)   (g_static_rec_mutex_init(&base->queuelock))
#define QUEUE_LOCK_FREE(base)   (g_static_rec_mutex_free(&base->queuelock))
#define QUEUE_LOCK(base)        (g_static_rec_mutex_lock(&base->queuelock))
#define QUEUE_UNLOCK(base)      (g_static_rec_mutex_unlock(&base->queuelock))
#else
/* otherwise it's already been defined in the header (FIXME 0.11)*/
#endif

GST_DEBUG_CATEGORY_STATIC (basertpdepayload_debug);
#define GST_CAT_DEFAULT (basertpdepayload_debug)

#define GST_BASE_RTP_DEPAYLOAD_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_RTP_DEPAYLOAD, GstBaseRTPDepayloadPrivate))

struct _GstBaseRTPDepayloadPrivate
{
  guint64 clock_base;

  GstClockTime npt_start;
  GstClockTime npt_stop;
  gdouble play_speed;
  gdouble play_scale;

  GstClockTime exttimestamp;
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_QUEUE_DELAY	0

enum
{
  PROP_0,
  PROP_QUEUE_DELAY
};

static void gst_base_rtp_depayload_finalize (GObject * object);
static void gst_base_rtp_depayload_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_rtp_depayload_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_base_rtp_depayload_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_base_rtp_depayload_chain (GstPad * pad,
    GstBuffer * in);
static gboolean gst_base_rtp_depayload_handle_sink_event (GstPad * pad,
    GstEvent * event);

static GstStateChangeReturn gst_base_rtp_depayload_change_state (GstElement *
    element, GstStateChange transition);

static void gst_base_rtp_depayload_set_gst_timestamp
    (GstBaseRTPDepayload * filter, guint32 timestamp, GstBuffer * buf);

GST_BOILERPLATE (GstBaseRTPDepayload, gst_base_rtp_depayload, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_base_rtp_depayload_base_init (gpointer klass)
{
  /*GstElementClass *element_class = GST_ELEMENT_CLASS (klass); */
}

static void
gst_base_rtp_depayload_class_init (GstBaseRTPDepayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = (GstElementClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstBaseRTPDepayloadPrivate));

  gobject_class->finalize = gst_base_rtp_depayload_finalize;
  gobject_class->set_property = gst_base_rtp_depayload_set_property;
  gobject_class->get_property = gst_base_rtp_depayload_get_property;

  g_object_class_install_property (gobject_class, PROP_QUEUE_DELAY,
      g_param_spec_uint ("queue_delay", "Queue Delay",
          "Amount of ms to queue/buffer, deprecated", 0, G_MAXUINT,
          DEFAULT_QUEUE_DELAY, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_base_rtp_depayload_change_state;

  klass->set_gst_timestamp = gst_base_rtp_depayload_set_gst_timestamp;

  GST_DEBUG_CATEGORY_INIT (basertpdepayload_debug, "basertpdepayload", 0,
      "Base class for RTP Depayloaders");
}

static void
gst_base_rtp_depayload_init (GstBaseRTPDepayload * filter,
    GstBaseRTPDepayloadClass * klass)
{
  GstPadTemplate *pad_template;
  GstBaseRTPDepayloadPrivate *priv;

  priv = GST_BASE_RTP_DEPAYLOAD_GET_PRIVATE (filter);
  filter->priv = priv;

  GST_DEBUG_OBJECT (filter, "init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);
  filter->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      gst_base_rtp_depayload_setcaps);
  gst_pad_set_chain_function (filter->sinkpad, gst_base_rtp_depayload_chain);
  gst_pad_set_event_function (filter->sinkpad,
      gst_base_rtp_depayload_handle_sink_event);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);
  filter->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->queue = g_queue_new ();
  filter->queue_delay = DEFAULT_QUEUE_DELAY;
}

static void
gst_base_rtp_depayload_finalize (GObject * object)
{
  GstBaseRTPDepayload *filter = GST_BASE_RTP_DEPAYLOAD (object);

  g_queue_free (filter->queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_base_rtp_depayload_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseRTPDepayload *filter;
  GstBaseRTPDepayloadClass *bclass;
  GstBaseRTPDepayloadPrivate *priv;
  gboolean res;
  GstStructure *caps_struct;
  const GValue *value;

  filter = GST_BASE_RTP_DEPAYLOAD (gst_pad_get_parent (pad));
  priv = filter->priv;

  bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  GST_DEBUG_OBJECT (filter, "Set caps");

  caps_struct = gst_caps_get_structure (caps, 0);

  /* get clock base if any, we need this for the newsegment */
  value = gst_structure_get_value (caps_struct, "clock-base");
  if (value && G_VALUE_HOLDS_UINT (value))
    priv->clock_base = g_value_get_uint (value);
  else
    priv->clock_base = -1;

  /* get other values for newsegment */
  value = gst_structure_get_value (caps_struct, "npt-start");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    priv->npt_start = g_value_get_uint64 (value);
  else
    priv->npt_start = 0;

  value = gst_structure_get_value (caps_struct, "npt-stop");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    priv->npt_stop = g_value_get_uint64 (value);
  else
    priv->npt_stop = -1;

  value = gst_structure_get_value (caps_struct, "play-speed");
  if (value && G_VALUE_HOLDS_DOUBLE (value))
    priv->play_speed = g_value_get_double (value);
  else
    priv->play_speed = 1.0;

  value = gst_structure_get_value (caps_struct, "play-scale");
  if (value && G_VALUE_HOLDS_DOUBLE (value))
    priv->play_scale = g_value_get_double (value);
  else
    priv->play_scale = 1.0;

  priv->exttimestamp = -1;

  if (bclass->set_caps)
    res = bclass->set_caps (filter, caps);
  else
    res = TRUE;

  gst_object_unref (filter);

  return res;
}

static GstFlowReturn
gst_base_rtp_depayload_chain (GstPad * pad, GstBuffer * in)
{
  GstBaseRTPDepayload *filter;
  GstBaseRTPDepayloadClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;

  filter = GST_BASE_RTP_DEPAYLOAD (GST_OBJECT_PARENT (pad));

  if (filter->clock_rate == 0)
    goto not_configured;

  bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  /* let's send it out to processing */
  out_buf = bclass->process (filter, in);
  if (out_buf) {
    guint32 timestamp;

    timestamp = gst_rtp_buffer_get_timestamp (in);

    /* push buffer with timestamp 
     * We are assuming here that the timestamp of the last RTP buffer
     * is the same as the timestamp wanted on the collector. If this is not a
     * desired result, the process function should push itself with another
     * timestamp and return NULL.
     */
    ret = gst_base_rtp_depayload_push_ts (filter, timestamp, out_buf);
  }
  gst_buffer_unref (in);

  return ret;

  /* ERRORS */
not_configured:
  {
    GST_ELEMENT_ERROR (filter, STREAM, FORMAT,
        (NULL), ("no clock rate was specified, likely incomplete input caps"));
    gst_buffer_unref (in);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_base_rtp_depayload_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseRTPDepayload *filter =
      GST_BASE_RTP_DEPAYLOAD (GST_OBJECT_PARENT (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;

      gst_event_parse_new_segment (event, NULL, NULL, &format, NULL, NULL,
          NULL);
      if (format != GST_FORMAT_TIME)
        goto wrong_format;

      GST_DEBUG_OBJECT (filter, "Upstream sent a NEWSEGMENT, passing through.");
      /* fallthrough */
    }
    default:
      /* pass other events forward */
      res = gst_pad_push_event (filter->srcpad, event);
      break;
  }
  return res;

  /* ERRORS */
wrong_format:
  {
    GST_DEBUG_OBJECT (filter,
        "Upstream sent a NEWSEGMENT in wrong format, dropping.");
    gst_event_unref (event);
    return TRUE;
  }
}

static GstFlowReturn
gst_base_rtp_depayload_push_full (GstBaseRTPDepayload * filter,
    gboolean do_ts, guint32 timestamp, GstBuffer * out_buf)
{
  GstFlowReturn ret;
  GstCaps *srccaps;
  GstBaseRTPDepayloadClass *bclass;

  /* set the caps if any */
  srccaps = GST_PAD_CAPS (filter->srcpad);
  if (srccaps)
    gst_buffer_set_caps (out_buf, srccaps);

  bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  /* set the timestamp if we must and can */
  if (bclass->set_gst_timestamp && do_ts)
    bclass->set_gst_timestamp (filter, timestamp, out_buf);

  /* push it */
  GST_LOG_OBJECT (filter, "Pushing buffer size %d, timestamp %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (out_buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out_buf)));
  ret = gst_pad_push (filter->srcpad, out_buf);
  GST_LOG_OBJECT (filter, "Pushed buffer: %s", gst_flow_get_name (ret));

  return ret;
}

/**
 * gst_base_rtp_depayload_push_ts:
 * @filter: a #GstBaseRTPDepayload
 * @timestamp: an RTP timestamp to apply
 * @out_buf: a #GstBuffer
 *
 * Push @out_buf to the peer of @filter. This function takes ownership of
 * @out_buf.
 *
 * Unlike gst_base_rtp_depayload_push(), this function will apply @timestamp
 * on the outgoing buffer, using the configured clock_rate to convert the
 * timestamp to a valid GStreamer clock time.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_base_rtp_depayload_push_ts (GstBaseRTPDepayload * filter, guint32 timestamp,
    GstBuffer * out_buf)
{
  return gst_base_rtp_depayload_push_full (filter, TRUE, timestamp, out_buf);
}

/**
 * gst_base_rtp_depayload_push:
 * @filter: a #GstBaseRTPDepayload
 * @out_buf: a #GstBuffer
 *
 * Push @out_buf to the peer of @filter. This function takes ownership of
 * @out_buf.
 *
 * Unlike gst_base_rtp_depayload_push_ts(), this function will not apply
 * any timestamp on the outgoing buffer.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_base_rtp_depayload_push (GstBaseRTPDepayload * filter, GstBuffer * out_buf)
{
  return gst_base_rtp_depayload_push_full (filter, FALSE, 0, out_buf);
}

static void
gst_base_rtp_depayload_set_gst_timestamp (GstBaseRTPDepayload * filter,
    guint32 timestamp, GstBuffer * buf)
{
  GstClockTime ts, adjusted, exttimestamp;
  GstBaseRTPDepayloadPrivate *priv;

  priv = filter->priv;

  /* no clock-base set, take first timestamp as base */
  if (priv->clock_base == -1)
    priv->clock_base = timestamp;

  /* get extended timestamp */
  exttimestamp = gst_rtp_buffer_ext_timestamp (&priv->exttimestamp, timestamp);

  /* rtp timestamps are based on the clock_rate
   * gst timesamps are in nanoseconds */
  ts = gst_util_uint64_scale_int (exttimestamp, GST_SECOND, filter->clock_rate);

  GST_DEBUG_OBJECT (filter,
      "timestamp: %u, exttimestamp %" G_GUINT64_FORMAT ", clockrate : %u",
      timestamp, exttimestamp, filter->clock_rate);

  /* add delay to timestamp */
  adjusted = ts + (filter->queue_delay * GST_MSECOND);

  GST_DEBUG_OBJECT (filter, "RTP: %u, GST: %" GST_TIME_FORMAT ", adjusted %"
      GST_TIME_FORMAT, timestamp, GST_TIME_ARGS (ts), GST_TIME_ARGS (adjusted));

  GST_BUFFER_TIMESTAMP (buf) = adjusted;

  /* if this is the first buf send a NEWSEGMENT */
  if (filter->need_newsegment) {
    GstEvent *event;
    GstClockTime start, stop, position;

    start = gst_util_uint64_scale_int (priv->clock_base, GST_SECOND,
        filter->clock_rate);

    if (priv->npt_stop != -1)
      stop = priv->npt_stop - priv->npt_start + start;
    else
      stop = -1;

    position = priv->npt_start;

    event =
        gst_event_new_new_segment_full (FALSE, priv->play_speed,
        priv->play_scale, GST_FORMAT_TIME, start, stop, position);

    gst_pad_push_event (filter->srcpad, event);

    filter->need_newsegment = FALSE;
    GST_DEBUG_OBJECT (filter, "Pushed newsegment event on this first buffer");
  }
}

static GstStateChangeReturn
gst_base_rtp_depayload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseRTPDepayload *filter;
  GstStateChangeReturn ret;

  filter = GST_BASE_RTP_DEPAYLOAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* clock_rate needs to be overwritten by child */
      filter->clock_rate = 0;
      filter->priv->clock_base = -1;
      filter->priv->exttimestamp = -1;
      filter->need_newsegment = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
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

static void
gst_base_rtp_depayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseRTPDepayload *filter;

  filter = GST_BASE_RTP_DEPAYLOAD (object);

  switch (prop_id) {
    case PROP_QUEUE_DELAY:
      filter->queue_delay = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_rtp_depayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseRTPDepayload *filter;

  filter = GST_BASE_RTP_DEPAYLOAD (object);

  switch (prop_id) {
    case PROP_QUEUE_DELAY:
      g_value_set_uint (value, filter->queue_delay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
