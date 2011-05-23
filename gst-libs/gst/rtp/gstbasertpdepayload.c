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
 * Provides a base class for RTP depayloaders
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
  GstClockTime npt_start;
  GstClockTime npt_stop;
  gdouble play_speed;
  gdouble play_scale;

  gboolean discont;
  GstClockTime timestamp;
  GstClockTime duration;

  guint32 next_seqnum;

  gboolean negotiated;
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
  PROP_QUEUE_DELAY,
  PROP_LAST
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
    (GstBaseRTPDepayload * filter, guint32 rtptime, GstBuffer * buf);
static gboolean gst_base_rtp_depayload_packet_lost (GstBaseRTPDepayload *
    filter, GstEvent * event);
static gboolean gst_base_rtp_depayload_handle_event (GstBaseRTPDepayload *
    filter, GstEvent * event);

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

  /**
   * GstBaseRTPDepayload::queue-delay
   *
   * Control the amount of packets to buffer.
   *
   * Deprecated: Use a jitterbuffer or RTP session manager to delay packet
   * playback. This property has no effect anymore since 0.10.15.
   */
#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_QUEUE_DELAY,
      g_param_spec_uint ("queue-delay", "Queue Delay",
          "Amount of ms to queue/buffer, deprecated", 0, G_MAXUINT,
          DEFAULT_QUEUE_DELAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  gstelement_class->change_state = gst_base_rtp_depayload_change_state;

  klass->set_gst_timestamp = gst_base_rtp_depayload_set_gst_timestamp;
  klass->packet_lost = gst_base_rtp_depayload_packet_lost;
  klass->handle_event = gst_base_rtp_depayload_handle_event;

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

  gst_segment_init (&filter->segment, GST_FORMAT_UNDEFINED);
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

  /* get other values for newsegment */
  value = gst_structure_get_value (caps_struct, "npt-start");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    priv->npt_start = g_value_get_uint64 (value);
  else
    priv->npt_start = 0;
  GST_DEBUG_OBJECT (filter, "NPT start %" G_GUINT64_FORMAT, priv->npt_start);

  value = gst_structure_get_value (caps_struct, "npt-stop");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    priv->npt_stop = g_value_get_uint64 (value);
  else
    priv->npt_stop = -1;

  GST_DEBUG_OBJECT (filter, "NPT stop %" G_GUINT64_FORMAT, priv->npt_stop);

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

  if (bclass->set_caps) {
    res = bclass->set_caps (filter, caps);
    if (!res) {
      GST_WARNING_OBJECT (filter, "Subclass rejected caps %" GST_PTR_FORMAT,
          caps);
    }
  } else {
    res = TRUE;
  }

  priv->negotiated = res;

  gst_object_unref (filter);

  return res;
}

static GstFlowReturn
gst_base_rtp_depayload_chain (GstPad * pad, GstBuffer * in)
{
  GstBaseRTPDepayload *filter;
  GstBaseRTPDepayloadPrivate *priv;
  GstBaseRTPDepayloadClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  GstClockTime timestamp;
  guint16 seqnum;
  guint32 rtptime;
  gboolean discont;
  gint gap;

  filter = GST_BASE_RTP_DEPAYLOAD (GST_OBJECT_PARENT (pad));
  priv = filter->priv;

  /* we must have a setcaps first */
  if (G_UNLIKELY (!priv->negotiated))
    goto not_negotiated;

  /* we must validate, it's possible that this element is plugged right after a
   * network receiver and we don't want to operate on invalid data */
  if (G_UNLIKELY (!gst_rtp_buffer_validate (in)))
    goto invalid_buffer;

  if (!priv->discont)
    priv->discont = GST_BUFFER_IS_DISCONT (in);

  timestamp = GST_BUFFER_TIMESTAMP (in);
  /* convert to running_time and save the timestamp, this is the timestamp
   * we put on outgoing buffers. */
  timestamp = gst_segment_to_running_time (&filter->segment, GST_FORMAT_TIME,
      timestamp);
  priv->timestamp = timestamp;
  priv->duration = GST_BUFFER_DURATION (in);

  seqnum = gst_rtp_buffer_get_seq (in);
  rtptime = gst_rtp_buffer_get_timestamp (in);
  discont = FALSE;

  GST_LOG_OBJECT (filter, "discont %d, seqnum %u, rtptime %u, timestamp %"
      GST_TIME_FORMAT, priv->discont, seqnum, rtptime,
      GST_TIME_ARGS (timestamp));

  /* Check seqnum. This is a very simple check that makes sure that the seqnums
   * are striclty increasing, dropping anything that is out of the ordinary. We
   * can only do this when the next_seqnum is known. */
  if (G_LIKELY (priv->next_seqnum != -1)) {
    gap = gst_rtp_buffer_compare_seqnum (seqnum, priv->next_seqnum);

    /* if we have no gap, all is fine */
    if (G_UNLIKELY (gap != 0)) {
      GST_LOG_OBJECT (filter, "got packet %u, expected %u, gap %d", seqnum,
          priv->next_seqnum, gap);
      if (gap < 0) {
        /* seqnum > next_seqnum, we are missing some packets, this is always a
         * DISCONT. */
        GST_LOG_OBJECT (filter, "%d missing packets", gap);
        discont = TRUE;
      } else {
        /* seqnum < next_seqnum, we have seen this packet before or the sender
         * could be restarted. If the packet is not too old, we throw it away as
         * a duplicate, otherwise we mark discont and continue. 100 misordered
         * packets is a good threshold. See also RFC 4737. */
        if (gap < 100)
          goto dropping;

        GST_LOG_OBJECT (filter,
            "%d > 100, packet too old, sender likely restarted", gap);
        discont = TRUE;
      }
    }
  }
  priv->next_seqnum = (seqnum + 1) & 0xffff;

  if (G_UNLIKELY (discont && !priv->discont)) {
    GST_LOG_OBJECT (filter, "mark DISCONT on input buffer");
    /* we detected a seqnum discont but the buffer was not flagged with a discont,
     * set the discont flag so that the subclass can throw away old data. */
    priv->discont = TRUE;
    in = gst_buffer_make_metadata_writable (in);
    GST_BUFFER_FLAG_SET (in, GST_BUFFER_FLAG_DISCONT);
  }

  bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  if (G_UNLIKELY (bclass->process == NULL))
    goto no_process;

  /* let's send it out to processing */
  out_buf = bclass->process (filter, in);
  if (out_buf) {
    /* we pass rtptime as backward compatibility, in reality, the incomming
     * buffer timestamp is always applied to the outgoing packet. */
    ret = gst_base_rtp_depayload_push_ts (filter, rtptime, out_buf);
  }
  gst_buffer_unref (in);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    /* this is not fatal but should be filtered earlier */
    if (GST_BUFFER_CAPS (in) == NULL) {
      GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION,
          ("No RTP format was negotiated."),
          ("Input buffers need to have RTP caps set on them. This is usually "
              "achieved by setting the 'caps' property of the upstream source "
              "element (often udpsrc or appsrc), or by putting a capsfilter "
              "element before the depayloader and setting the 'caps' property "
              "on that. Also see http://cgit.freedesktop.org/gstreamer/"
              "gst-plugins-good/tree/gst/rtp/README"));
    } else {
      GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION,
          ("No RTP format was negotiated."),
          ("RTP caps on input buffer were rejected, most likely because they "
              "were incomplete or contained wrong values. Check the debug log "
              "for more information."));
    }
    gst_buffer_unref (in);
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (filter, STREAM, DECODE, (NULL),
        ("Received invalid RTP payload, dropping"));
    gst_buffer_unref (in);
    return GST_FLOW_OK;
  }
dropping:
  {
    GST_WARNING_OBJECT (filter, "%d <= 100, dropping old packet", gap);
    gst_buffer_unref (in);
    return GST_FLOW_OK;
  }
no_process:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_ERROR (filter, STREAM, NOT_IMPLEMENTED, (NULL),
        ("The subclass does not have a process method"));
    gst_buffer_unref (in);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_base_rtp_depayload_handle_event (GstBaseRTPDepayload * filter,
    GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&filter->segment, GST_FORMAT_UNDEFINED);
      filter->need_newsegment = TRUE;
      filter->priv->next_seqnum = -1;
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat fmt;
      gint64 start, stop, position;

      gst_event_parse_new_segment (event, &update, &rate, &fmt, &start, &stop,
          &position);

      gst_segment_set_newsegment (&filter->segment, update, rate, fmt,
          start, stop, position);

      /* don't pass the event downstream, we generate our own segment including
       * the NTP time and other things we receive in caps */
      forward = FALSE;
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstBaseRTPDepayloadClass *bclass;

      bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

      if (gst_event_has_name (event, "GstRTPPacketLost")) {
        /* we get this event from the jitterbuffer when it considers a packet as
         * being lost. We send it to our packet_lost vmethod. The default
         * implementation will make time progress by pushing out a NEWSEGMENT
         * update event. Subclasses can override and to one of the following:
         *  - Adjust timestamp/duration to something more accurate before
         *    calling the parent (default) packet_lost method.
         *  - do some more advanced error concealing on the already received
         *    (fragmented) packets.
         *  - ignore the packet lost.
         */
        if (bclass->packet_lost)
          res = bclass->packet_lost (filter, event);
        forward = FALSE;
      }
      break;
    }
    default:
      break;
  }

  if (forward)
    res = gst_pad_push_event (filter->srcpad, event);
  else
    gst_event_unref (event);

  return res;
}

static gboolean
gst_base_rtp_depayload_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstBaseRTPDepayload *filter;
  GstBaseRTPDepayloadClass *bclass;

  filter = GST_BASE_RTP_DEPAYLOAD (gst_pad_get_parent (pad));
  if (G_UNLIKELY (filter == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);
  if (bclass->handle_event)
    res = bclass->handle_event (filter, event);
  else
    gst_event_unref (event);

  gst_object_unref (filter);
  return res;
}

static GstEvent *
create_segment_event (GstBaseRTPDepayload * filter, gboolean update,
    GstClockTime position)
{
  GstEvent *event;
  GstClockTime stop;
  GstBaseRTPDepayloadPrivate *priv;

  priv = filter->priv;

  if (priv->npt_stop != -1)
    stop = priv->npt_stop - priv->npt_start;
  else
    stop = -1;

  event = gst_event_new_new_segment_full (update, priv->play_speed,
      priv->play_scale, GST_FORMAT_TIME, position, stop,
      position + priv->npt_start);

  return event;
}

typedef struct
{
  GstBaseRTPDepayload *depayload;
  GstBaseRTPDepayloadClass *bclass;
  GstCaps *caps;
  gboolean do_ts;
  gboolean rtptime;
} HeaderData;

static GstBufferListItem
set_headers (GstBuffer ** buffer, guint group, guint idx, HeaderData * data)
{
  GstBaseRTPDepayload *depayload = data->depayload;

  *buffer = gst_buffer_make_metadata_writable (*buffer);
  gst_buffer_set_caps (*buffer, data->caps);

  /* set the timestamp if we must and can */
  if (data->bclass->set_gst_timestamp && data->do_ts)
    data->bclass->set_gst_timestamp (depayload, data->rtptime, *buffer);

  if (G_UNLIKELY (depayload->priv->discont)) {
    GST_LOG_OBJECT (depayload, "Marking DISCONT on output buffer");
    GST_BUFFER_FLAG_SET (*buffer, GST_BUFFER_FLAG_DISCONT);
    depayload->priv->discont = FALSE;
  }

  return GST_BUFFER_LIST_SKIP_GROUP;
}

static GstFlowReturn
gst_base_rtp_depayload_prepare_push (GstBaseRTPDepayload * filter,
    gboolean do_ts, guint32 rtptime, gboolean is_list, gpointer obj)
{
  HeaderData data;

  data.depayload = filter;
  data.caps = GST_PAD_CAPS (filter->srcpad);
  data.rtptime = rtptime;
  data.do_ts = do_ts;
  data.bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  if (is_list) {
    GstBufferList **blist = obj;
    gst_buffer_list_foreach (*blist, (GstBufferListFunc) set_headers, &data);
  } else {
    GstBuffer **buf = obj;
    set_headers (buf, 0, 0, &data);
  }

  /* if this is the first buffer send a NEWSEGMENT */
  if (G_UNLIKELY (filter->need_newsegment)) {
    GstEvent *event;

    event = create_segment_event (filter, FALSE, 0);

    gst_pad_push_event (filter->srcpad, event);

    filter->need_newsegment = FALSE;
    GST_DEBUG_OBJECT (filter, "Pushed newsegment event on this first buffer");
  }

  return GST_FLOW_OK;
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
 * Unlike gst_base_rtp_depayload_push(), this function will by default apply
 * the last incomming timestamp on the outgoing buffer when it didn't have a
 * timestamp already. The set_get_timestamp vmethod can be overwritten to change
 * this behaviour (and take, for example, @timestamp into account).
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_base_rtp_depayload_push_ts (GstBaseRTPDepayload * filter, guint32 timestamp,
    GstBuffer * out_buf)
{
  GstFlowReturn res;

  res =
      gst_base_rtp_depayload_prepare_push (filter, TRUE, timestamp, FALSE,
      &out_buf);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push (filter->srcpad, out_buf);
  else
    gst_buffer_unref (out_buf);

  return res;
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
 * any timestamp on the outgoing buffer. Subclasses should therefore timestamp
 * outgoing buffers themselves.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_base_rtp_depayload_push (GstBaseRTPDepayload * filter, GstBuffer * out_buf)
{
  GstFlowReturn res;

  res = gst_base_rtp_depayload_prepare_push (filter, FALSE, 0, FALSE, &out_buf);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push (filter->srcpad, out_buf);
  else
    gst_buffer_unref (out_buf);

  return res;
}

/**
 * gst_base_rtp_depayload_push_list:
 * @filter: a #GstBaseRTPDepayload
 * @out_list: a #GstBufferList
 *
 * Push @out_list to the peer of @filter. This function takes ownership of
 * @out_list.
 *
 * Returns: a #GstFlowReturn.
 *
 * Since: 0.10.32
 */
GstFlowReturn
gst_base_rtp_depayload_push_list (GstBaseRTPDepayload * filter,
    GstBufferList * out_list)
{
  GstFlowReturn res;

  res = gst_base_rtp_depayload_prepare_push (filter, TRUE, 0, TRUE, &out_list);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push_list (filter->srcpad, out_list);
  else
    gst_buffer_list_unref (out_list);

  return res;
}

/* convert the PacketLost event form a jitterbuffer to a segment update.
 * subclasses can override this.  */
static gboolean
gst_base_rtp_depayload_packet_lost (GstBaseRTPDepayload * filter,
    GstEvent * event)
{
  GstClockTime timestamp, duration, position;
  GstEvent *sevent;
  const GstStructure *s;

  s = gst_event_get_structure (event);

  /* first start by parsing the timestamp and duration */
  timestamp = -1;
  duration = -1;

  gst_structure_get_clock_time (s, "timestamp", &timestamp);
  gst_structure_get_clock_time (s, "duration", &duration);

  position = timestamp;
  if (duration != -1)
    position += duration;

  /* update the current segment with the elapsed time */
  sevent = create_segment_event (filter, TRUE, position);

  return gst_pad_push_event (filter->srcpad, sevent);
}

static void
gst_base_rtp_depayload_set_gst_timestamp (GstBaseRTPDepayload * filter,
    guint32 rtptime, GstBuffer * buf)
{
  GstBaseRTPDepayloadPrivate *priv;
  GstClockTime timestamp, duration;

  priv = filter->priv;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);

  /* apply last incomming timestamp and duration to outgoing buffer if
   * not otherwise set. */
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    GST_BUFFER_TIMESTAMP (buf) = priv->timestamp;
  if (!GST_CLOCK_TIME_IS_VALID (duration))
    GST_BUFFER_DURATION (buf) = priv->duration;
}

static GstStateChangeReturn
gst_base_rtp_depayload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseRTPDepayload *filter;
  GstBaseRTPDepayloadPrivate *priv;
  GstStateChangeReturn ret;

  filter = GST_BASE_RTP_DEPAYLOAD (element);
  priv = filter->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      filter->need_newsegment = TRUE;
      priv->npt_start = 0;
      priv->npt_stop = -1;
      priv->play_speed = 1.0;
      priv->play_scale = 1.0;
      priv->next_seqnum = -1;
      priv->negotiated = FALSE;
      priv->discont = FALSE;
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
