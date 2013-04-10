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
 * SECTION:gstrtpbasedepayload
 * @short_description: Base class for RTP depayloader
 *
 * Provides a base class for RTP depayloaders
 */

#include "gstrtpbasedepayload.h"

GST_DEBUG_CATEGORY_STATIC (rtpbasedepayload_debug);
#define GST_CAT_DEFAULT (rtpbasedepayload_debug)

#define GST_RTP_BASE_DEPAYLOAD_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_BASE_DEPAYLOAD, GstRTPBaseDepayloadPrivate))

struct _GstRTPBaseDepayloadPrivate
{
  GstClockTime npt_start;
  GstClockTime npt_stop;
  gdouble play_speed;
  gdouble play_scale;

  gboolean discont;
  GstClockTime pts;
  GstClockTime dts;
  GstClockTime duration;

  guint32 next_seqnum;

  gboolean negotiated;

  GstCaps *last_caps;
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LAST
};

static void gst_rtp_base_depayload_finalize (GObject * object);
static void gst_rtp_base_depayload_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_base_depayload_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_rtp_base_depayload_chain (GstPad * pad,
    GstObject * parent, GstBuffer * in);
static gboolean gst_rtp_base_depayload_handle_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

static GstStateChangeReturn gst_rtp_base_depayload_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_rtp_base_depayload_packet_lost (GstRTPBaseDepayload *
    filter, GstEvent * event);
static gboolean gst_rtp_base_depayload_handle_event (GstRTPBaseDepayload *
    filter, GstEvent * event);

static GstElementClass *parent_class = NULL;
static void gst_rtp_base_depayload_class_init (GstRTPBaseDepayloadClass *
    klass);
static void gst_rtp_base_depayload_init (GstRTPBaseDepayload * rtpbasepayload,
    GstRTPBaseDepayloadClass * klass);

GType
gst_rtp_base_depayload_get_type (void)
{
  static GType rtp_base_depayload_type = 0;

  if (g_once_init_enter ((gsize *) & rtp_base_depayload_type)) {
    static const GTypeInfo rtp_base_depayload_info = {
      sizeof (GstRTPBaseDepayloadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_rtp_base_depayload_class_init,
      NULL,
      NULL,
      sizeof (GstRTPBaseDepayload),
      0,
      (GInstanceInitFunc) gst_rtp_base_depayload_init,
    };

    g_once_init_leave ((gsize *) & rtp_base_depayload_type,
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPBaseDepayload",
            &rtp_base_depayload_info, G_TYPE_FLAG_ABSTRACT));
  }
  return rtp_base_depayload_type;
}

static void
gst_rtp_base_depayload_class_init (GstRTPBaseDepayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = (GstElementClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstRTPBaseDepayloadPrivate));

  gobject_class->finalize = gst_rtp_base_depayload_finalize;
  gobject_class->set_property = gst_rtp_base_depayload_set_property;
  gobject_class->get_property = gst_rtp_base_depayload_get_property;

  gstelement_class->change_state = gst_rtp_base_depayload_change_state;

  klass->packet_lost = gst_rtp_base_depayload_packet_lost;
  klass->handle_event = gst_rtp_base_depayload_handle_event;

  GST_DEBUG_CATEGORY_INIT (rtpbasedepayload_debug, "rtpbasedepayload", 0,
      "Base class for RTP Depayloaders");
}

static void
gst_rtp_base_depayload_init (GstRTPBaseDepayload * filter,
    GstRTPBaseDepayloadClass * klass)
{
  GstPadTemplate *pad_template;
  GstRTPBaseDepayloadPrivate *priv;

  priv = GST_RTP_BASE_DEPAYLOAD_GET_PRIVATE (filter);
  filter->priv = priv;

  GST_DEBUG_OBJECT (filter, "init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);
  filter->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_rtp_base_depayload_chain);
  gst_pad_set_event_function (filter->sinkpad,
      gst_rtp_base_depayload_handle_sink_event);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);
  filter->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_segment_init (&filter->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_rtp_base_depayload_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_base_depayload_setcaps (GstRTPBaseDepayload * filter, GstCaps * caps)
{
  GstRTPBaseDepayloadClass *bclass;
  GstRTPBaseDepayloadPrivate *priv;
  gboolean res;
  GstStructure *caps_struct;
  const GValue *value;

  priv = filter->priv;

  bclass = GST_RTP_BASE_DEPAYLOAD_GET_CLASS (filter);

  GST_DEBUG_OBJECT (filter, "Set caps %" GST_PTR_FORMAT, caps);

  if (priv->last_caps) {
    if (gst_caps_is_equal (priv->last_caps, caps)) {
      res = TRUE;
      goto caps_not_changed;
    } else {
      gst_caps_unref (priv->last_caps);
      priv->last_caps = NULL;
    }
  }

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

  if (priv->negotiated)
    priv->last_caps = gst_caps_ref (caps);

  return res;

caps_not_changed:
  {
    GST_DEBUG_OBJECT (filter, "Caps did not change");
    return res;
  }
}

static GstFlowReturn
gst_rtp_base_depayload_chain (GstPad * pad, GstObject * parent, GstBuffer * in)
{
  GstRTPBaseDepayload *filter;
  GstRTPBaseDepayloadPrivate *priv;
  GstRTPBaseDepayloadClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  GstClockTime pts, dts;
  guint16 seqnum;
  guint32 rtptime;
  gboolean discont;
  gint gap;
  GstRTPBuffer rtp = { NULL };

  filter = GST_RTP_BASE_DEPAYLOAD (parent);
  priv = filter->priv;

  /* we must have a setcaps first */
  if (G_UNLIKELY (!priv->negotiated))
    goto not_negotiated;

  if (G_UNLIKELY (!gst_rtp_buffer_map (in, GST_MAP_READ, &rtp)))
    goto invalid_buffer;

  if (!priv->discont)
    priv->discont = GST_BUFFER_IS_DISCONT (in);

  pts = GST_BUFFER_PTS (in);
  dts = GST_BUFFER_DTS (in);
  /* convert to running_time and save the timestamp, this is the timestamp
   * we put on outgoing buffers. */
  pts = gst_segment_to_running_time (&filter->segment, GST_FORMAT_TIME, pts);
  dts = gst_segment_to_running_time (&filter->segment, GST_FORMAT_TIME, dts);
  priv->pts = pts;
  priv->dts = dts;
  priv->duration = GST_BUFFER_DURATION (in);

  seqnum = gst_rtp_buffer_get_seq (&rtp);
  rtptime = gst_rtp_buffer_get_timestamp (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  discont = FALSE;

  GST_LOG_OBJECT (filter, "discont %d, seqnum %u, rtptime %u, pts %"
      GST_TIME_FORMAT ", dts %" GST_TIME_FORMAT, priv->discont, seqnum, rtptime,
      GST_TIME_ARGS (pts), GST_TIME_ARGS (dts));

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
    in = gst_buffer_make_writable (in);
    GST_BUFFER_FLAG_SET (in, GST_BUFFER_FLAG_DISCONT);
  }

  bclass = GST_RTP_BASE_DEPAYLOAD_GET_CLASS (filter);

  if (G_UNLIKELY (bclass->process == NULL))
    goto no_process;

  /* let's send it out to processing */
  out_buf = bclass->process (filter, in);
  if (out_buf) {
    ret = gst_rtp_base_depayload_push (filter, out_buf);
  }
  gst_buffer_unref (in);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION,
        ("No RTP format was negotiated."),
        ("Input buffers need to have RTP caps set on them. This is usually "
            "achieved by setting the 'caps' property of the upstream source "
            "element (often udpsrc or appsrc), or by putting a capsfilter "
            "element before the depayloader and setting the 'caps' property "
            "on that. Also see http://cgit.freedesktop.org/gstreamer/"
            "gst-plugins-good/tree/gst/rtp/README"));
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
gst_rtp_base_depayload_handle_event (GstRTPBaseDepayload * filter,
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
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      res = gst_rtp_base_depayload_setcaps (filter, caps);
      forward = FALSE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &filter->segment);
      /* don't pass the event downstream, we generate our own segment including
       * the NTP time and other things we receive in caps */
      forward = FALSE;
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstRTPBaseDepayloadClass *bclass;

      bclass = GST_RTP_BASE_DEPAYLOAD_GET_CLASS (filter);

      if (gst_event_has_name (event, "GstRTPPacketLost")) {
        /* we get this event from the jitterbuffer when it considers a packet as
         * being lost. We send it to our packet_lost vmethod. The default
         * implementation will make time progress by pushing out a GAP event.
         * Subclasses can override and to one of the following:
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
gst_rtp_base_depayload_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res = FALSE;
  GstRTPBaseDepayload *filter;
  GstRTPBaseDepayloadClass *bclass;

  filter = GST_RTP_BASE_DEPAYLOAD (parent);
  bclass = GST_RTP_BASE_DEPAYLOAD_GET_CLASS (filter);
  if (bclass->handle_event)
    res = bclass->handle_event (filter, event);
  else
    gst_event_unref (event);

  return res;
}

static GstEvent *
create_segment_event (GstRTPBaseDepayload * filter, GstClockTime position)
{
  GstEvent *event;
  GstClockTime stop;
  GstRTPBaseDepayloadPrivate *priv;
  GstSegment segment;

  priv = filter->priv;

  if (priv->npt_stop != -1)
    stop = priv->npt_stop - priv->npt_start;
  else
    stop = -1;

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = priv->play_speed;
  segment.applied_rate = priv->play_scale;
  segment.start = 0;
  segment.stop = stop;
  segment.time = priv->npt_start;
  segment.position = position;

  event = gst_event_new_segment (&segment);

  return event;
}

typedef struct
{
  GstRTPBaseDepayload *depayload;
  GstRTPBaseDepayloadClass *bclass;
} HeaderData;

static gboolean
set_headers (GstBuffer ** buffer, guint idx, HeaderData * data)
{
  GstRTPBaseDepayload *depayload = data->depayload;
  GstRTPBaseDepayloadPrivate *priv = depayload->priv;
  GstClockTime pts, dts, duration;

  *buffer = gst_buffer_make_writable (*buffer);

  pts = GST_BUFFER_PTS (*buffer);
  dts = GST_BUFFER_DTS (*buffer);
  duration = GST_BUFFER_DURATION (*buffer);

  /* apply last incomming timestamp and duration to outgoing buffer if
   * not otherwise set. */
  if (!GST_CLOCK_TIME_IS_VALID (pts))
    GST_BUFFER_PTS (*buffer) = priv->pts;
  if (!GST_CLOCK_TIME_IS_VALID (dts))
    GST_BUFFER_DTS (*buffer) = priv->dts;
  if (!GST_CLOCK_TIME_IS_VALID (duration))
    GST_BUFFER_DURATION (*buffer) = priv->duration;

  if (G_UNLIKELY (depayload->priv->discont)) {
    GST_LOG_OBJECT (depayload, "Marking DISCONT on output buffer");
    GST_BUFFER_FLAG_SET (*buffer, GST_BUFFER_FLAG_DISCONT);
    depayload->priv->discont = FALSE;
  }

  /* make sure we only set the timestamp on the first packet */
  priv->pts = GST_CLOCK_TIME_NONE;
  priv->dts = GST_CLOCK_TIME_NONE;
  priv->duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static GstFlowReturn
gst_rtp_base_depayload_prepare_push (GstRTPBaseDepayload * filter,
    gboolean is_list, gpointer obj)
{
  HeaderData data;

  data.depayload = filter;
  data.bclass = GST_RTP_BASE_DEPAYLOAD_GET_CLASS (filter);

  if (is_list) {
    GstBufferList **blist = obj;
    gst_buffer_list_foreach (*blist, (GstBufferListFunc) set_headers, &data);
  } else {
    GstBuffer **buf = obj;
    set_headers (buf, 0, &data);
  }

  /* if this is the first buffer send a NEWSEGMENT */
  if (G_UNLIKELY (filter->need_newsegment)) {
    GstEvent *event;

    event = create_segment_event (filter, 0);

    gst_pad_push_event (filter->srcpad, event);

    filter->need_newsegment = FALSE;
    GST_DEBUG_OBJECT (filter, "Pushed newsegment event on this first buffer");
  }

  return GST_FLOW_OK;
}

/**
 * gst_rtp_base_depayload_push:
 * @filter: a #GstRTPBaseDepayload
 * @out_buf: a #GstBuffer
 *
 * Push @out_buf to the peer of @filter. This function takes ownership of
 * @out_buf.
 *
 * This function will by default apply the last incomming timestamp on
 * the outgoing buffer when it didn't have a timestamp already.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_rtp_base_depayload_push (GstRTPBaseDepayload * filter, GstBuffer * out_buf)
{
  GstFlowReturn res;

  res = gst_rtp_base_depayload_prepare_push (filter, FALSE, &out_buf);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push (filter->srcpad, out_buf);
  else
    gst_buffer_unref (out_buf);

  return res;
}

/**
 * gst_rtp_base_depayload_push_list:
 * @filter: a #GstRTPBaseDepayload
 * @out_list: a #GstBufferList
 *
 * Push @out_list to the peer of @filter. This function takes ownership of
 * @out_list.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
gst_rtp_base_depayload_push_list (GstRTPBaseDepayload * filter,
    GstBufferList * out_list)
{
  GstFlowReturn res;

  res = gst_rtp_base_depayload_prepare_push (filter, TRUE, &out_list);

  if (G_LIKELY (res == GST_FLOW_OK))
    res = gst_pad_push_list (filter->srcpad, out_list);
  else
    gst_buffer_list_unref (out_list);

  return res;
}

/* convert the PacketLost event form a jitterbuffer to a GAP event.
 * subclasses can override this.  */
static gboolean
gst_rtp_base_depayload_packet_lost (GstRTPBaseDepayload * filter,
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

  /* send GAP event */
  sevent = gst_event_new_gap (timestamp, duration);

  return gst_pad_push_event (filter->srcpad, sevent);
}

static GstStateChangeReturn
gst_rtp_base_depayload_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRTPBaseDepayload *filter;
  GstRTPBaseDepayloadPrivate *priv;
  GstStateChangeReturn ret;

  filter = GST_RTP_BASE_DEPAYLOAD (element);
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
      gst_caps_replace (&priv->last_caps, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_rtp_base_depayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_base_depayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
