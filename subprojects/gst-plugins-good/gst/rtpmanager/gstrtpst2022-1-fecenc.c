/* GStreamer
 * Copyright (C) <2020> Mathieu Duponchelle <mathieu@centricular.com>
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
 * SECTION:element-rtpst2022-1-fecenc
 * @see_also: #element-rtpst2022-1-fecdec
 *
 * This element takes as input a media stream and up to two FEC
 * streams as described in SMPTE 2022-1: Forward Error Correction
 * for Real-Time Video/Audio Transport Over IP Networks, and makes
 * use of the FEC packets to recover media packets that may have
 * gotten lost.
 *
 * ## sender / receiver example
 *
 * ``` shell
 * gst-launch-1.0 \
 *   rtpbin name=rtp fec-encoders='fec,0="rtpst2022-1-fecenc\ rows\=5\ columns\=5";' \
 *   uridecodebin uri=file:///path/to/video/file ! x264enc key-int-max=60 tune=zerolatency ! \
 *     queue ! mpegtsmux ! rtpmp2tpay ssrc=0 ! rtp.send_rtp_sink_0 \
 *   rtp.send_rtp_src_0 ! udpsink host=127.0.0.1 port=5000 \
 *   rtp.send_fec_src_0_0 ! udpsink host=127.0.0.1 port=5002 async=false \
 *   rtp.send_fec_src_0_1 ! udpsink host=127.0.0.1 port=5004 async=false
 * ```
 *
 * ``` shell
 * gst-launch-1.0 \
 *   rtpbin latency=500 fec-decoders='fec,0="rtpst2022-1-fecdec\ size-time\=1000000000";' name=rtp \
 *   udpsrc address=127.0.0.1 port=5002 caps="application/x-rtp, payload=96" ! queue ! rtp.recv_fec_sink_0_0 \
 *   udpsrc address=127.0.0.1 port=5004 caps="application/x-rtp, payload=96" ! queue ! rtp.recv_fec_sink_0_1 \
 *   udpsrc address=127.0.0.1 port=5000 caps="application/x-rtp, media=video, clock-rate=90000, encoding-name=mp2t, payload=33" ! \
 *     queue ! netsim drop-probability=0.05 ! rtp.recv_rtp_sink_0 \
 *   rtp. ! decodebin ! videoconvert ! queue ! autovideosink
 * ```
 *
 * With the above command line, as the media packet size is constant,
 * the fec overhead can be approximated to the number of fec packets
 * per 2-d matrix of media packet, here 10 fec packets for each 25
 * media packets.
 *
 * Increasing the number of rows and columns will decrease the overhead,
 * but obviously increase the likelihood of recovery failure for lost
 * packets on the receiver side.
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/base.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpst2022-1-fecenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtpst_2022_1_fecenc_debug);
#define GST_CAT_DEFAULT gst_rtpst_2022_1_fecenc_debug

enum
{
  PROP_0,
  PROP_COLUMNS,
  PROP_ROWS,
  PROP_PT,
  PROP_ENABLE_COLUMN,
  PROP_ENABLE_ROW,
};

#define DEFAULT_ROWS 0
#define DEFAULT_COLUMNS 0
#define DEFAULT_PT 96
#define DEFAULT_ENABLE_COLUMN TRUE
#define DEFAULT_ENABLE_ROW TRUE

typedef struct
{
  guint16 target_media_seq;     /* The media seqnum we want to send that packet alongside */
  guint16 seq_base;             /* Only used for logging purposes */
  GstBuffer *buffer;
} Item;

typedef struct
{
  guint8 *xored_payload;
  guint32 xored_timestamp;
  guint8 xored_pt;
  guint16 xored_payload_len;
  gboolean xored_marker;
  gboolean xored_padding;
  gboolean xored_extension;

  guint16 seq_base;

  guint16 payload_len;
  guint n_packets;
} FecPacket;

struct _GstRTPST_2022_1_FecEncClass
{
  GstElementClass class;
};

struct _GstRTPST_2022_1_FecEnc
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* These pads do not participate in the flow return of the element,
   * which should continue working even if the sending of FEC packets
   * fails
   */
  GstPad *row_fec_srcpad;
  GstPad *column_fec_srcpad;

  /* The following fields are only accessed on state change or from the
   * streaming thread, and only settable in state < PAUSED */

  /* N columns */
  guint l;
  /* N rows */
  guint d;

  /* Whether we have pushed initial events on the column FEC source pad */
  gboolean column_events_pushed;

  /* The current row FEC packet */
  FecPacket *row;
  /* Tracks the row seqnum */
  guint16 row_seq;
  /* Whether we have pushed initial events on the row FEC source pad */
  gboolean row_events_pushed;

  /* These two fields are used to enforce input seqnum consecutiveness,
   * and to determine when column FEC packets should be pushed */
  gboolean last_media_seqnum_set;
  guint16 last_media_seqnum;

  /* This field is used to timestamp our FEC packets, we just piggy back */
  guint32 last_media_timestamp;

  /* The payload type of the FEC packets */
  gint pt;

  /* The following fields can be changed while PLAYING, and are
   * protected with the OBJECT_LOCK
   */
  /* Tracks the property, can be changed while PLAYING */
  gboolean enable_row;
  /* Tracks the property, can be changed while PLAYING */
  gboolean enable_column;

  /* Array of FecPackets, with size enc->l */
  GPtrArray *columns;
  /* Index of the current column in the array above */
  guint current_column;
  /* Tracks the column seqnum */
  guint16 column_seq;
  /* Column FEC packets must be delayed to make them more resilient
   * to loss bursts, we store them here */
  GQueue queued_column_packets;
};

#define RTP_CAPS "application/x-rtp"

static GstStaticPadTemplate fec_src_template =
GST_STATIC_PAD_TEMPLATE ("fec_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (RTP_CAPS));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RTP_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RTP_CAPS));

#define gst_rtpst_2022_1_fecenc_parent_class parent_class
G_DEFINE_TYPE (GstRTPST_2022_1_FecEnc, gst_rtpst_2022_1_fecenc,
    GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE (rtpst2022_1_fecenc, "rtpst2022-1-fecenc",
    GST_RANK_NONE, GST_TYPE_RTPST_2022_1_FECENC);

static void
free_item (Item * item)
{
  if (item->buffer)
    gst_buffer_unref (item->buffer);

  g_free (item);
}

static void
free_fec_packet (FecPacket * packet)
{
  if (packet->xored_payload)
    g_free (packet->xored_payload);
  g_free (packet);
}

static void
_xor_mem (guint8 * restrict dst, const guint8 * restrict src, gsize length)
{
  guint i;

  for (i = 0; i < (length / sizeof (guint64)); ++i) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    GST_WRITE_UINT64_LE (dst,
        GST_READ_UINT64_LE (dst) ^ GST_READ_UINT64_LE (src));
#else
    GST_WRITE_UINT64_BE (dst,
        GST_READ_UINT64_BE (dst) ^ GST_READ_UINT64_BE (src));
#endif
    dst += sizeof (guint64);
    src += sizeof (guint64);
  }
  for (i = 0; i < (length % sizeof (guint64)); ++i)
    dst[i] ^= src[i];
}

static void
fec_packet_update (FecPacket * fec, GstRTPBuffer * rtp)
{
  if (fec->n_packets == 0) {
    fec->seq_base = gst_rtp_buffer_get_seq (rtp);
    fec->payload_len = gst_rtp_buffer_get_payload_len (rtp);
    fec->xored_payload_len = gst_rtp_buffer_get_payload_len (rtp);
    fec->xored_pt = gst_rtp_buffer_get_payload_type (rtp);
    fec->xored_timestamp = gst_rtp_buffer_get_timestamp (rtp);
    fec->xored_marker = gst_rtp_buffer_get_marker (rtp);
    fec->xored_padding = gst_rtp_buffer_get_padding (rtp);
    fec->xored_extension = gst_rtp_buffer_get_extension (rtp);
    fec->xored_payload = g_malloc (sizeof (guint8) * fec->payload_len);
    memcpy (fec->xored_payload, gst_rtp_buffer_get_payload (rtp),
        fec->payload_len);
  } else {
    guint plen = gst_rtp_buffer_get_payload_len (rtp);

    if (fec->payload_len < plen) {
      fec->xored_payload =
          g_realloc (fec->xored_payload, sizeof (guint8) * plen);
      memset (fec->xored_payload + fec->payload_len, 0,
          plen - fec->payload_len);
      fec->payload_len = plen;
    }

    fec->xored_payload_len ^= plen;
    fec->xored_pt ^= gst_rtp_buffer_get_payload_type (rtp);
    fec->xored_timestamp ^= gst_rtp_buffer_get_timestamp (rtp);
    fec->xored_marker ^= gst_rtp_buffer_get_marker (rtp);
    fec->xored_padding ^= gst_rtp_buffer_get_padding (rtp);
    fec->xored_extension ^= gst_rtp_buffer_get_extension (rtp);
    _xor_mem (fec->xored_payload, gst_rtp_buffer_get_payload (rtp), plen);
  }

  fec->n_packets += 1;
}

static void
push_initial_events (GstRTPST_2022_1_FecEnc * enc, GstPad * pad,
    const gchar * id)
{
  gchar *stream_id;
  GstCaps *caps;
  GstSegment segment;

  stream_id = gst_pad_create_stream_id (pad, GST_ELEMENT (enc), id);
  gst_pad_push_event (pad, gst_event_new_stream_start (stream_id));
  g_free (stream_id);

  caps = gst_caps_new_simple ("application/x-rtp",
      "payload", G_TYPE_UINT, enc->pt, "ssrc", G_TYPE_UINT, 0, NULL);
  gst_pad_push_event (pad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (pad, gst_event_new_segment (&segment));
}

static void
queue_fec_packet (GstRTPST_2022_1_FecEnc * enc, FecPacket * fec, gboolean row)
{
  GstBuffer *buffer = gst_rtp_buffer_new_allocate (fec->payload_len + 16, 0, 0);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBitWriter bits;
  guint8 *data;

  gst_rtp_buffer_map (buffer, GST_MAP_WRITE, &rtp);
  data = gst_rtp_buffer_get_payload (&rtp);
  memset (data, 0x00, 16);

  gst_bit_writer_init_with_data (&bits, data, 17, FALSE);

  gst_bit_writer_put_bits_uint16 (&bits, fec->seq_base, 16);    /* SNBase low bits */
  gst_bit_writer_put_bits_uint16 (&bits, fec->xored_payload_len, 16);   /* Length Recovery */
  gst_bit_writer_put_bits_uint8 (&bits, 1, 1);  /* E */
  gst_bit_writer_put_bits_uint8 (&bits, fec->xored_pt, 7);      /* PT recovery */
  gst_bit_writer_put_bits_uint32 (&bits, 0, 24);        /* Mask */
  gst_bit_writer_put_bits_uint32 (&bits, fec->xored_timestamp, 32);     /* TS recovery */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 1);  /* N */
  gst_bit_writer_put_bits_uint8 (&bits, row ? 1 : 0, 1);        /* D */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 3);  /* type */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 3);  /* index */
  gst_bit_writer_put_bits_uint8 (&bits, row ? 1 : enc->l, 8);   /* Offset */
  gst_bit_writer_put_bits_uint8 (&bits, fec->n_packets, 8);     /* NA */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 8);  /* SNBase ext bits */

  memcpy (data + 16, fec->xored_payload, fec->payload_len);

  gst_bit_writer_reset (&bits);

  gst_rtp_buffer_set_payload_type (&rtp, enc->pt);
  gst_rtp_buffer_set_seq (&rtp, row ? enc->row_seq++ : enc->column_seq++);
  gst_rtp_buffer_set_marker (&rtp, fec->xored_marker);
  gst_rtp_buffer_set_padding (&rtp, fec->xored_padding);
  gst_rtp_buffer_set_extension (&rtp, fec->xored_extension);

  /* We're sending it out immediately */
  if (row)
    gst_rtp_buffer_set_timestamp (&rtp, enc->last_media_timestamp);

  gst_rtp_buffer_unmap (&rtp);

  /* We can send row FEC packets immediately, column packets need
   * delaying by L <= delay < L * D
   */
  if (row) {
    GstFlowReturn ret;

    GST_LOG_OBJECT (enc,
        "Pushing row FEC packet, seq base: %u, media seqnum: %u",
        fec->seq_base, enc->last_media_seqnum);

    /* Safe to unlock here */
    GST_OBJECT_UNLOCK (enc);
    ret = gst_pad_push (enc->row_fec_srcpad, buffer);
    GST_OBJECT_LOCK (enc);

    if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING)
      GST_WARNING_OBJECT (enc->row_fec_srcpad,
          "Failed to push row FEC packet: %s", gst_flow_get_name (ret));
  } else {
    Item *item = g_malloc0 (sizeof (Item));

    item->buffer = buffer;
    item->seq_base = fec->seq_base;
    /* Let's get cute and linearize */
    item->target_media_seq =
        enc->last_media_seqnum + enc->l - enc->current_column +
        enc->d * enc->current_column;

    g_queue_push_tail (&enc->queued_column_packets, item);
  }
}

static void
gst_2d_fec_push_item_unlocked (GstRTPST_2022_1_FecEnc * enc)
{
  GstFlowReturn ret;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  Item *item = g_queue_pop_head (&enc->queued_column_packets);

  GST_LOG_OBJECT (enc,
      "Pushing column FEC packet, target media seq: %u, seq base: %u, "
      "media seqnum: %u", item->target_media_seq, item->seq_base,
      enc->last_media_seqnum);
  gst_rtp_buffer_map (item->buffer, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_timestamp (&rtp, enc->last_media_timestamp);
  gst_rtp_buffer_unmap (&rtp);
  GST_OBJECT_UNLOCK (enc);
  ret = gst_pad_push (enc->column_fec_srcpad, gst_buffer_ref (item->buffer));
  GST_OBJECT_LOCK (enc);

  if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING)
    GST_WARNING_OBJECT (enc->column_fec_srcpad,
        "Failed to push column FEC packet: %s", gst_flow_get_name (ret));

  free_item (item);
}

static GstFlowReturn
gst_rtpst_2022_1_fecenc_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)) {
    GST_ERROR_OBJECT (enc, "Chained buffer isn't valid RTP");
    goto error;
  }

  if (gst_rtp_buffer_get_ssrc (&rtp) != 0) {
    GST_ERROR_OBJECT (enc, "Chained buffer must have SSRC == 0");
    goto error;
  }

  if (enc->last_media_seqnum_set
      && (guint16) (enc->last_media_seqnum + 1) !=
      gst_rtp_buffer_get_seq (&rtp)) {
    GST_ERROR_OBJECT (enc, "consecutive sequence numbers are required");
    goto error;
  }

  if (!enc->row_events_pushed) {
    push_initial_events (enc, enc->row_fec_srcpad, "row-fec");
    enc->row_events_pushed = TRUE;
  }

  if (!enc->column_events_pushed) {
    push_initial_events (enc, enc->column_fec_srcpad, "column-fec");
    enc->column_events_pushed = TRUE;
  }

  enc->last_media_timestamp = gst_rtp_buffer_get_timestamp (&rtp);
  enc->last_media_seqnum = gst_rtp_buffer_get_seq (&rtp);
  enc->last_media_seqnum_set = TRUE;

  GST_OBJECT_LOCK (enc);
  if (enc->enable_row && enc->l) {
    g_assert (enc->row->n_packets < enc->l);
    fec_packet_update (enc->row, &rtp);
    if (enc->row->n_packets == enc->l) {
      queue_fec_packet (enc, enc->row, TRUE);
      g_free (enc->row->xored_payload);
      memset (enc->row, 0x00, sizeof (FecPacket));
    }
  }

  if (enc->enable_column && enc->l && enc->d) {
    FecPacket *column = g_ptr_array_index (enc->columns, enc->current_column);

    fec_packet_update (column, &rtp);
    if (column->n_packets == enc->d) {
      queue_fec_packet (enc, column, FALSE);
      g_free (column->xored_payload);
      memset (column, 0x00, sizeof (FecPacket));
    }

    enc->current_column++;
    enc->current_column %= enc->l;
  }

  gst_rtp_buffer_unmap (&rtp);

  {
    Item *item = g_queue_peek_head (&enc->queued_column_packets);
    if (item && item->target_media_seq == enc->last_media_seqnum)
      gst_2d_fec_push_item_unlocked (enc);
  }

  GST_OBJECT_UNLOCK (enc);

  ret = gst_pad_push (enc->srcpad, buffer);

done:
  return ret;

error:
  if (rtp.buffer)
    gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buffer);
  ret = GST_FLOW_ERROR;
  goto done;
}

static GstIterator *
gst_rtpst_2022_1_fecenc_iterate_linked_pads (GstPad * pad, GstObject * parent)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (parent);
  GstPad *otherpad = NULL;
  GstIterator *it = NULL;
  GValue val = { 0, };

  if (pad == enc->srcpad)
    otherpad = enc->sinkpad;
  else if (pad == enc->sinkpad)
    otherpad = enc->srcpad;

  if (otherpad) {
    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, otherpad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);
  }

  return it;
}

static void
gst_rtpst_2022_1_fecenc_reset (GstRTPST_2022_1_FecEnc * enc, gboolean allocate)
{
  if (enc->row) {
    free_fec_packet (enc->row);
    enc->row = NULL;
  }

  if (enc->columns) {
    g_ptr_array_unref (enc->columns);
    enc->columns = NULL;
  }

  if (enc->row_fec_srcpad) {
    gst_element_remove_pad (GST_ELEMENT (enc), enc->row_fec_srcpad);
    enc->row_fec_srcpad = NULL;
  }

  if (enc->column_fec_srcpad) {
    gst_element_remove_pad (GST_ELEMENT (enc), enc->column_fec_srcpad);
    enc->column_fec_srcpad = NULL;
  }

  g_queue_clear_full (&enc->queued_column_packets, (GDestroyNotify) free_item);

  if (allocate) {
    guint i;

    enc->row = g_malloc0 (sizeof (FecPacket));
    enc->columns =
        g_ptr_array_new_full (enc->l, (GDestroyNotify) free_fec_packet);

    for (i = 0; i < enc->l; i++) {
      g_ptr_array_add (enc->columns, g_malloc0 (sizeof (FecPacket)));
    }

    g_queue_init (&enc->queued_column_packets);

    enc->column_fec_srcpad =
        gst_pad_new_from_static_template (&fec_src_template, "fec_0");
    gst_pad_set_active (enc->column_fec_srcpad, TRUE);
    gst_pad_set_iterate_internal_links_function (enc->column_fec_srcpad,
        GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_iterate_linked_pads));
    gst_element_add_pad (GST_ELEMENT (enc), enc->column_fec_srcpad);

    enc->row_fec_srcpad =
        gst_pad_new_from_static_template (&fec_src_template, "fec_1");
    gst_pad_set_active (enc->row_fec_srcpad, TRUE);
    gst_pad_set_iterate_internal_links_function (enc->row_fec_srcpad,
        GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_iterate_linked_pads));
    gst_element_add_pad (GST_ELEMENT (enc), enc->row_fec_srcpad);

    gst_element_no_more_pads (GST_ELEMENT (enc));
  }

  enc->current_column = 0;
  enc->last_media_seqnum_set = FALSE;
}

static GstStateChangeReturn
gst_rtpst_2022_1_fecenc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtpst_2022_1_fecenc_reset (enc, TRUE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtpst_2022_1_fecenc_reset (enc, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtpst_2022_1_fecenc_finalize (GObject * object)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (object);

  gst_rtpst_2022_1_fecenc_reset (enc, FALSE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtpst_2022_1_fecenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (object);

  if (GST_STATE (enc) > GST_STATE_READY) {
    GST_ERROR_OBJECT (enc,
        "rtpst2022-1-fecenc properties can't be changed in PLAYING or PAUSED state");
    return;
  }

  switch (prop_id) {
    case PROP_COLUMNS:
      enc->l = g_value_get_uint (value);
      break;
    case PROP_ROWS:
      enc->d = g_value_get_uint (value);
      break;
    case PROP_PT:
      enc->pt = g_value_get_int (value);
      break;
    case PROP_ENABLE_COLUMN:
      GST_OBJECT_LOCK (enc);
      enc->enable_column = g_value_get_boolean (value);
      if (!enc->enable_column) {
        guint i;

        if (enc->columns) {
          for (i = 0; i < enc->l; i++) {
            FecPacket *column = g_ptr_array_index (enc->columns, i);
            g_free (column->xored_payload);
            memset (column, 0x00, sizeof (FecPacket));
          }
        }
        enc->current_column = 0;
        enc->column_seq = 0;
        g_queue_clear_full (&enc->queued_column_packets,
            (GDestroyNotify) free_item);
      }
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_ENABLE_ROW:
      GST_OBJECT_LOCK (enc);
      enc->enable_row = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (enc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpst_2022_1_fecenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (object);

  switch (prop_id) {
    case PROP_COLUMNS:
      g_value_set_uint (value, enc->l);
      break;
    case PROP_ROWS:
      g_value_set_uint (value, enc->d);
      break;
    case PROP_PT:
      g_value_set_int (value, enc->pt);
      break;
    case PROP_ENABLE_COLUMN:
      GST_OBJECT_LOCK (enc);
      g_value_set_boolean (value, enc->enable_column);
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_ENABLE_ROW:
      GST_OBJECT_LOCK (enc);
      g_value_set_boolean (value, enc->enable_row);
      GST_OBJECT_UNLOCK (enc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_2d_fec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRTPST_2022_1_FecEnc *enc = GST_RTPST_2022_1_FECENC_CAST (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtpst_2022_1_fecenc_reset (enc, TRUE);
      break;
    case GST_EVENT_EOS:
      gst_pad_push_event (enc->row_fec_srcpad, gst_event_ref (event));
      GST_OBJECT_LOCK (enc);
      while (g_queue_peek_head (&enc->queued_column_packets))
        gst_2d_fec_push_item_unlocked (enc);
      GST_OBJECT_UNLOCK (enc);
      gst_pad_push_event (enc->column_fec_srcpad, gst_event_ref (event));
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, parent, event);

  return ret;
}

static void
gst_rtpst_2022_1_fecenc_class_init (GstRTPST_2022_1_FecEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_get_property);
  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_finalize);

  g_object_class_install_property (gobject_class, PROP_COLUMNS,
      g_param_spec_uint ("columns", "Columns",
          "Number of columns to apply row FEC on, 0=disabled", 0,
          255, DEFAULT_COLUMNS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ROWS,
      g_param_spec_uint ("rows", "Rows",
          "Number of rows to apply column FEC on, 0=disabled", 0,
          255, DEFAULT_ROWS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_PT,
      g_param_spec_int ("pt", "Payload Type",
          "The payload type of FEC packets", 96,
          255, DEFAULT_PT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ENABLE_COLUMN,
      g_param_spec_boolean ("enable-column-fec", "Enable Column FEC",
          "Whether the encoder should compute and send column FEC",
          DEFAULT_ENABLE_COLUMN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_ENABLE_ROW,
      g_param_spec_boolean ("enable-row-fec", "Enable Row FEC",
          "Whether the encoder should compute and send row FEC",
          DEFAULT_ENABLE_ROW,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_change_state);

  gst_element_class_set_static_metadata (gstelement_class,
      "SMPTE 2022-1 FEC encoder", "SMPTE 2022-1 FEC encoding",
      "performs FEC as described by SMPTE 2022-1",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &fec_src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  GST_DEBUG_CATEGORY_INIT (gst_rtpst_2022_1_fecenc_debug,
      "rtpst2022-1-fecenc", 0, "SMPTE 2022-1 FEC encoder element");
}

static void
gst_rtpst_2022_1_fecenc_init (GstRTPST_2022_1_FecEnc * enc)
{
  enc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (enc->srcpad);
  GST_PAD_SET_PROXY_CAPS (enc->srcpad);
  gst_pad_set_iterate_internal_links_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_iterate_linked_pads));
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  GST_PAD_SET_PROXY_CAPS (enc->sinkpad);
  gst_pad_set_chain_function (enc->sinkpad, gst_rtpst_2022_1_fecenc_sink_chain);
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_2d_fec_sink_event));
  gst_pad_set_iterate_internal_links_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecenc_iterate_linked_pads));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->d = 0;
  enc->l = 0;
}
