/* RTP Retransmission sender element for GStreamer
 *
 * gstrtprtxsend.c:
 *
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Julien Isorce <julien.isorce@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-rtprtxsend
 *
 * See #GstRtpRtxReceive for examples
 * 
 * The purpose of the sender RTX object is to keep a history of RTP packets up
 * to a configurable limit (max-size-time or max-size-packets). It will listen
 * for upstream custom retransmission events (GstRTPRetransmissionRequest) that
 * comes from downstream (#GstRtpSession). When receiving a request it will
 * look up the requested seqnum in its list of stored packets. If the packet
 * is available, it will create a RTX packet according to RFC 4588 and send
 * this as an auxiliary stream. RTX is SSRC-multiplexed
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>

#include "gstrtprtxsend.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_rtx_send_debug);
#define GST_CAT_DEFAULT gst_rtp_rtx_send_debug

#define DEFAULT_RTX_PAYLOAD_TYPE 0
#define DEFAULT_MAX_SIZE_TIME    0
#define DEFAULT_MAX_SIZE_PACKETS 100

enum
{
  PROP_0,
  PROP_RTX_SSRC,
  PROP_RTX_PAYLOAD_TYPE,
  PROP_MAX_SIZE_TIME,
  PROP_MAX_SIZE_PACKETS,
  PROP_NUM_RTX_REQUESTS,
  PROP_NUM_RTX_PACKETS,
  PROP_LAST
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static gboolean gst_rtp_rtx_send_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_rtp_rtx_send_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_rtp_rtx_send_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

static GstStateChangeReturn gst_rtp_rtx_send_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_rtx_send_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_rtx_send_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_rtx_send_finalize (GObject * object);

G_DEFINE_TYPE (GstRtpRtxSend, gst_rtp_rtx_send, GST_TYPE_ELEMENT);

typedef struct
{
  guint16 seqnum;
  guint32 timestamp;
  GstBuffer *buffer;
} BufferQueueItem;

static void
buffer_queue_item_free (BufferQueueItem * item)
{
  gst_buffer_unref (item->buffer);
  g_free (item);
}

static void
gst_rtp_rtx_send_class_init (GstRtpRtxSendClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_rtp_rtx_send_get_property;
  gobject_class->set_property = gst_rtp_rtx_send_set_property;
  gobject_class->finalize = gst_rtp_rtx_send_finalize;

  g_object_class_install_property (gobject_class, PROP_RTX_SSRC,
      g_param_spec_uint ("rtx-ssrc", "Retransmission SSRC",
          "SSRC of the retransmission stream for SSRC-multiplexed mode "
          "(default = random)", 0, G_MAXUINT, -1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RTX_PAYLOAD_TYPE,
      g_param_spec_uint ("rtx-payload-type", "RTX Payload Type",
          "Payload type of the retransmission stream (fmtp in SDP)", 0,
          G_MAXUINT, DEFAULT_RTX_PAYLOAD_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint ("max-size-time", "Max Size Time",
          "Amount of ms to queue (0 = unlimited)", 0, G_MAXUINT,
          DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_PACKETS,
      g_param_spec_uint ("max-size-packets", "Max Size Packets",
          "Amount of packets to queue (0 = unlimited)", 0, G_MAXINT16,
          DEFAULT_MAX_SIZE_PACKETS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RTX_REQUESTS,
      g_param_spec_uint ("num-rtx-requests", "Num RTX Requests",
          "Number of retransmission events received", 0, G_MAXUINT,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RTX_PACKETS,
      g_param_spec_uint ("num-rtx-packets", "Num RTX Packets",
          " Number of retransmission packets sent", 0, G_MAXUINT,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Retransmission Sender", "Codec",
      "Retransmit RTP packets when needed, according to RFC4588",
      "Julien Isorce <julien.isorce@collabora.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_send_change_state);
}

static void
gst_rtp_rtx_send_reset (GstRtpRtxSend * rtx, gboolean full)
{
  g_mutex_lock (&rtx->lock);
  g_sequence_remove_range (g_sequence_get_begin_iter (rtx->queue),
      g_sequence_get_end_iter (rtx->queue));
  g_queue_foreach (rtx->pending, (GFunc) gst_buffer_unref, NULL);
  g_queue_clear (rtx->pending);
  rtx->master_ssrc = 0;
  rtx->next_seqnum = g_random_int_range (0, G_MAXUINT16);
  rtx->rtx_ssrc = g_random_int ();
  rtx->num_rtx_requests = 0;
  rtx->num_rtx_packets = 0;
  g_mutex_unlock (&rtx->lock);
}

static void
gst_rtp_rtx_send_finalize (GObject * object)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (object);

  gst_rtp_rtx_send_reset (rtx, TRUE);
  g_sequence_free (rtx->queue);
  g_queue_free (rtx->pending);
  g_mutex_clear (&rtx->lock);

  G_OBJECT_CLASS (gst_rtp_rtx_send_parent_class)->finalize (object);
}

static void
gst_rtp_rtx_send_init (GstRtpRtxSend * rtx)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (rtx);

  rtx->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  GST_PAD_SET_PROXY_CAPS (rtx->srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (rtx->srcpad);
  gst_pad_set_event_function (rtx->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_send_src_event));
  gst_element_add_pad (GST_ELEMENT (rtx), rtx->srcpad);

  rtx->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  GST_PAD_SET_PROXY_CAPS (rtx->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (rtx->sinkpad);
  gst_pad_set_event_function (rtx->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_send_sink_event));
  gst_pad_set_chain_function (rtx->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_send_chain));
  gst_element_add_pad (GST_ELEMENT (rtx), rtx->sinkpad);

  rtx->queue = g_sequence_new ((GDestroyNotify) buffer_queue_item_free);
  rtx->pending = g_queue_new ();
  g_mutex_init (&rtx->lock);

  rtx->next_seqnum = g_random_int_range (0, G_MAXUINT16);
  rtx->rtx_ssrc = g_random_int ();

  rtx->max_size_time = DEFAULT_MAX_SIZE_TIME;
  rtx->max_size_packets = DEFAULT_MAX_SIZE_PACKETS;
}

static guint32
choose_ssrc (GstRtpRtxSend * rtx)
{
  guint32 ssrc;

  while (TRUE) {
    ssrc = g_random_int ();

    /* make sure to be different than master */
    if (ssrc != rtx->master_ssrc)
      break;
  }
  return ssrc;
}

static gint
buffer_queue_items_cmp (BufferQueueItem * a, BufferQueueItem * b,
    gpointer user_data)
{
  /* gst_rtp_buffer_compare_seqnum returns the opposite of what we want,
   * it returns negative when seqnum1 > seqnum2 and we want negative
   * when b > a, i.e. a is smaller, so it comes first in the sequence */
  return gst_rtp_buffer_compare_seqnum (b->seqnum, a->seqnum);
}

static gboolean
gst_rtp_rtx_send_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (parent);
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s = gst_event_get_structure (event);

      /* This event usually comes from the downstream gstrtpsession */
      if (gst_structure_has_name (s, "GstRTPRetransmissionRequest")) {
        guint32 seqnum = 0;
        guint ssrc = 0;

        /* retrieve seqnum of the packet that need to be restransmisted */
        if (!gst_structure_get_uint (s, "seqnum", &seqnum))
          seqnum = -1;

        /* retrieve ssrc of the packet that need to be restransmisted */
        if (!gst_structure_get_uint (s, "ssrc", &ssrc))
          ssrc = -1;

        GST_DEBUG_OBJECT (rtx,
            "request seqnum: %" G_GUINT16_FORMAT ", ssrc: %" G_GUINT32_FORMAT,
            seqnum, ssrc);

        g_mutex_lock (&rtx->lock);
        /* check if request is for us */
        if (rtx->master_ssrc == ssrc) {
          GSequenceIter *iter;
          BufferQueueItem search_item;

          /* update statistics */
          ++rtx->num_rtx_requests;

          search_item.seqnum = seqnum;
          iter = g_sequence_lookup (rtx->queue, &search_item,
              (GCompareDataFunc) buffer_queue_items_cmp, NULL);
          if (iter) {
            BufferQueueItem *item = g_sequence_get (iter);
            GST_DEBUG_OBJECT (rtx, "found %" G_GUINT16_FORMAT, item->seqnum);
            g_queue_push_tail (rtx->pending, gst_buffer_ref (item->buffer));
          }
        }
        g_mutex_unlock (&rtx->lock);

        gst_event_unref (event);
        res = TRUE;

        /* This event usually comes from the downstream gstrtpsession */
      } else if (gst_structure_has_name (s, "GstRTPCollision")) {
        guint ssrc = 0;

        if (!gst_structure_get_uint (s, "ssrc", &ssrc))
          ssrc = -1;

        GST_DEBUG_OBJECT (rtx, "collision ssrc: %" G_GUINT32_FORMAT, ssrc);

        g_mutex_lock (&rtx->lock);

        /* choose another ssrc for our retransmited stream */
        if (ssrc == rtx->rtx_ssrc) {
          rtx->rtx_ssrc = choose_ssrc (rtx);

          /* clear buffers we already saved */
          g_sequence_remove_range (g_sequence_get_begin_iter (rtx->queue),
              g_sequence_get_end_iter (rtx->queue));

          /* clear buffers that are about to be retransmited */
          g_queue_foreach (rtx->pending, (GFunc) gst_buffer_unref, NULL);
          g_queue_clear (rtx->pending);

          g_mutex_unlock (&rtx->lock);

          /* no need to forward to payloader because we make sure to have
           * a different ssrc
           */
          gst_event_unref (event);
          res = TRUE;
        } else {
          g_mutex_unlock (&rtx->lock);

          /* forward event to payloader in case collided ssrc is
           * master stream */
          res = gst_pad_event_default (pad, parent, event);
        }
      } else {
        res = gst_pad_event_default (pad, parent, event);
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static gboolean
gst_rtp_rtx_send_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstStructure *s;

      gst_event_parse_caps (event, &caps);
      g_assert (gst_caps_is_fixed (caps));

      s = gst_caps_get_structure (caps, 0);
      gst_structure_get_int (s, "clock-rate", &rtx->clock_rate);

      GST_DEBUG_OBJECT (rtx, "got clock-rate from caps: %d", rtx->clock_rate);

      break;
    }
    default:
      break;
  }
  return gst_pad_event_default (pad, parent, event);
}

/* like rtp_jitter_buffer_get_ts_diff() */
static guint32
gst_rtp_rtx_send_get_ts_diff (GstRtpRtxSend * self)
{
  guint64 high_ts, low_ts;
  BufferQueueItem *high_buf, *low_buf;
  guint32 result;

  high_buf =
      g_sequence_get (g_sequence_iter_prev (g_sequence_get_end_iter
          (self->queue)));
  low_buf = g_sequence_get (g_sequence_get_begin_iter (self->queue));

  if (!high_buf || !low_buf || high_buf == low_buf)
    return 0;

  high_ts = high_buf->timestamp;
  low_ts = low_buf->timestamp;

  /* it needs to work if ts wraps */
  if (high_ts >= low_ts) {
    result = (guint32) (high_ts - low_ts);
  } else {
    result = (guint32) (high_ts + G_MAXUINT32 + 1 - low_ts);
  }

  /* return value in ms instead of clock ticks */
  return (guint32) gst_util_uint64_scale_int (result, 1000, self->clock_rate);
}

/* Copy fixed header and extension. Add OSN before to copy payload
 * Copy memory to avoid to manually copy each rtp buffer field.
 */
static GstBuffer *
_gst_rtp_rtx_buffer_new (GstBuffer * buffer, guint32 ssrc, guint16 seqnum,
    guint8 fmtp)
{
  GstMemory *mem = NULL;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstRTPBuffer new_rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *new_buffer = gst_buffer_new ();
  GstMapInfo map;
  guint payload_len = 0;

  gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);

  /* gst_rtp_buffer_map does not map the payload so do it now */
  gst_rtp_buffer_get_payload (&rtp);

  /* If payload type is not set through SDP/property then
   * just bump the value */
  if (fmtp < 96)
    fmtp = gst_rtp_buffer_get_payload_type (&rtp) + 1;

  /* copy fixed header */
  mem = gst_memory_copy (rtp.map[0].memory, 0, rtp.size[0]);
  gst_buffer_append_memory (new_buffer, mem);

  /* copy extension if any */
  if (rtp.size[1]) {
    mem = gst_memory_copy (rtp.map[1].memory, 0, rtp.size[1]);
    gst_buffer_append_memory (new_buffer, mem);
  }

  /* copy payload and add OSN just before */
  payload_len = 2 + rtp.size[2];
  mem = gst_allocator_alloc (NULL, payload_len, NULL);

  gst_memory_map (mem, &map, GST_MAP_WRITE);
  GST_WRITE_UINT16_BE (map.data, gst_rtp_buffer_get_seq (&rtp));
  if (rtp.size[2])
    memcpy (map.data + 2, rtp.data[2], rtp.size[2]);
  gst_memory_unmap (mem, &map);
  gst_buffer_append_memory (new_buffer, mem);

  /* everything needed is copied */
  gst_rtp_buffer_unmap (&rtp);

  /* set ssrc, seqnum and fmtp */
  gst_rtp_buffer_map (new_buffer, GST_MAP_WRITE, &new_rtp);
  gst_rtp_buffer_set_ssrc (&new_rtp, ssrc);
  gst_rtp_buffer_set_seq (&new_rtp, seqnum);
  gst_rtp_buffer_set_payload_type (&new_rtp, fmtp);
  /* RFC 4588: let other elements do the padding, as normal */
  gst_rtp_buffer_set_padding (&new_rtp, FALSE);
  gst_rtp_buffer_unmap (&new_rtp);

  return new_buffer;
}

/* push pending retransmission packet.
 * it constructs rtx packet from original paclets */
static void
do_push (GstBuffer * buffer, GstRtpRtxSend * rtx)
{
  /* RFC4588 two streams multiplexed by sending them in the same session using
   * different SSRC values, i.e., SSRC-multiplexing.  */
  GST_DEBUG_OBJECT (rtx,
      "retransmit seqnum: %" G_GUINT16_FORMAT ", ssrc: %" G_GUINT32_FORMAT,
      rtx->next_seqnum, rtx->rtx_ssrc);
  gst_pad_push (rtx->srcpad, _gst_rtp_rtx_buffer_new (buffer, rtx->rtx_ssrc,
          rtx->next_seqnum++, rtx->rtx_payload_type));
}

static GstFlowReturn
gst_rtp_rtx_send_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (parent);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GQueue *pending = NULL;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  BufferQueueItem *item;
  guint16 seqnum;
  guint32 ssrc, rtptime;

  rtx = GST_RTP_RTX_SEND (parent);

  /* read the information we want from the buffer */
  gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
  seqnum = gst_rtp_buffer_get_seq (&rtp);
  ssrc = gst_rtp_buffer_get_ssrc (&rtp);
  rtptime = gst_rtp_buffer_get_timestamp (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  g_mutex_lock (&rtx->lock);

  /* retrieve master stream ssrc */
  rtx->master_ssrc = ssrc;
  /* check if our initial aux ssrc is equal to master */
  if (rtx->rtx_ssrc == rtx->master_ssrc)
    choose_ssrc (rtx);

  /* add current rtp buffer to queue history */
  item = g_new0 (BufferQueueItem, 1);
  item->seqnum = seqnum;
  item->timestamp = rtptime;
  item->buffer = gst_buffer_ref (buffer);
  g_sequence_append (rtx->queue, item);

  /* remove oldest packets from history if they are too many */
  if (rtx->max_size_packets) {
    while (g_sequence_get_length (rtx->queue) > rtx->max_size_packets)
      g_sequence_remove (g_sequence_get_begin_iter (rtx->queue));
  }
  if (rtx->max_size_time) {
    while (gst_rtp_rtx_send_get_ts_diff (rtx) > rtx->max_size_time)
      g_sequence_remove (g_sequence_get_begin_iter (rtx->queue));
  }

  /* within lock, get packets that have to be retransmited */
  if (g_queue_get_length (rtx->pending) > 0) {
    pending = rtx->pending;
    rtx->pending = g_queue_new ();

    /* update statistics - assume we will succeed to retransmit those packets */
    rtx->num_rtx_packets += g_queue_get_length (pending);
  }

  /* transfer payload type while holding the lock */
  rtx->rtx_payload_type = rtx->rtx_payload_type_pending;

  /* no need to hold the lock to push rtx packets */
  g_mutex_unlock (&rtx->lock);

  /* retransmit requested packets */
  if (pending) {
    g_queue_foreach (pending, (GFunc) do_push, rtx);
    g_queue_free_full (pending, (GDestroyNotify) gst_buffer_unref);
  }

  GST_LOG_OBJECT (rtx,
      "push seqnum: %" G_GUINT16_FORMAT ", ssrc: %" G_GUINT32_FORMAT, seqnum,
      rtx->master_ssrc);

  /* push current rtp packet */
  ret = gst_pad_push (rtx->srcpad, buffer);

  return ret;
}

static void
gst_rtp_rtx_send_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (object);

  switch (prop_id) {
    case PROP_RTX_SSRC:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->rtx_ssrc);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_RTX_PAYLOAD_TYPE:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->rtx_payload_type_pending);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_MAX_SIZE_TIME:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->max_size_time);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_MAX_SIZE_PACKETS:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->max_size_packets);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_NUM_RTX_REQUESTS:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->num_rtx_requests);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_NUM_RTX_PACKETS:
      g_mutex_lock (&rtx->lock);
      g_value_set_uint (value, rtx->num_rtx_packets);
      g_mutex_unlock (&rtx->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_rtx_send_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpRtxSend *rtx = GST_RTP_RTX_SEND (object);

  switch (prop_id) {
    case PROP_RTX_SSRC:
      g_mutex_lock (&rtx->lock);
      rtx->rtx_ssrc = g_value_get_uint (value);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_RTX_PAYLOAD_TYPE:
      g_mutex_lock (&rtx->lock);
      rtx->rtx_payload_type_pending = g_value_get_uint (value);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_MAX_SIZE_TIME:
      g_mutex_lock (&rtx->lock);
      rtx->max_size_time = g_value_get_uint (value);
      g_mutex_unlock (&rtx->lock);
      break;
    case PROP_MAX_SIZE_PACKETS:
      g_mutex_lock (&rtx->lock);
      rtx->max_size_packets = g_value_get_uint (value);
      g_mutex_unlock (&rtx->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_rtx_send_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpRtxSend *rtx;

  rtx = GST_RTP_RTX_SEND (element);

  switch (transition) {
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_rtp_rtx_send_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_rtx_send_reset (rtx, TRUE);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_rtp_rtx_send_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_rtx_send_debug, "rtprtxsend", 0,
      "rtp retransmission sender");

  return gst_element_register (plugin, "rtprtxsend", GST_RANK_NONE,
      GST_TYPE_RTP_RTX_SEND);
}
