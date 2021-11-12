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
 * SECTION:element-rtpst2022-1-fecdec
 * @see_also: #element-rtpst2022-1-fecenc
 *
 * This element takes as input a media stream and up to two FEC
 * streams as described in SMPTE 2022-1: Forward Error Correction
 * for Real-Time Video/Audio Transport Over IP Networks, and makes
 * use of the FEC packets to recover media packets that may have
 * gotten lost.
 *
 * ## Design
 *
 * The approach picked for this element is to proactively reconstruct missing
 * packets as soon as possible. When a FEC packet arrives, the element
 * immediately checks whether a media packet in the row / column it protects
 * can be reconstructed.
 *
 * Similarly, when a media packet comes in, the element checks whether it has
 * already received a corresponding packet in both the column and row the packet
 * belongs to, and if so goes through the first step listed above.
 *
 * This process is repeated recursively, allowing for recoveries over one
 * dimension to unblock recoveries over the other.
 *
 * In perfect networking conditions, this incurs next to no overhead as FEC
 * packets will arrive after the media packets, causing no reconstruction to
 * take place, just a few checks upon chaining.
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

#include "gstrtpst2022-1-fecdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtpst_2022_1_fecdec_debug);
#define GST_CAT_DEFAULT gst_rtpst_2022_1_fecdec_debug

#define DEFAULT_SIZE_TIME (GST_SECOND)

typedef struct
{
  guint16 seq;
  GstBuffer *buffer;
} Item;

static GstFlowReturn store_media_item (GstRTPST_2022_1_FecDec * dec,
    GstRTPBuffer * rtp, Item * item);

static void
free_item (Item * item)
{
  gst_buffer_unref (item->buffer);
  item->buffer = NULL;
  g_free (item);
}

static gint
cmp_items (Item * a, Item * b, gpointer unused)
{
  return gst_rtp_buffer_compare_seqnum (b->seq, a->seq);
}

enum
{
  PROP_0,
  PROP_SIZE_TIME,
};

struct _GstRTPST_2022_1_FecDecClass
{
  GstElementClass class;
};

struct _GstRTPST_2022_1_FecDec
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;
  GList *fec_sinkpads;

  /* All the following field are protected by the OBJECT_LOCK */
  GSequence *packets;
  GHashTable *column_fec_packets;
  GSequence *fec_packets[2];
  /* N columns */
  guint l;
  /* N rows */
  guint d;

  GstClockTime size_time;
  GstClockTime max_arrival_time;
  GstClockTime max_fec_arrival_time[2];
};

#define RTP_CAPS "application/x-rtp"

typedef struct
{
  guint16 seq;
  guint16 len;
  guint8 E;
  guint8 pt;
  guint32 mask;
  guint32 timestamp;
  guint8 N;
  guint8 D;
  guint8 type;
  guint8 index;
  guint8 offset;
  guint8 NA;
  guint8 seq_ext;
  guint8 *payload;
  guint payload_len;
  gboolean marker;
  gboolean padding;
  gboolean extension;
} Rtp2DFecHeader;

static GstStaticPadTemplate fec_sink_template =
GST_STATIC_PAD_TEMPLATE ("fec_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (RTP_CAPS));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RTP_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RTP_CAPS));

#define gst_rtpst_2022_1_fecdec_parent_class parent_class
G_DEFINE_TYPE (GstRTPST_2022_1_FecDec, gst_rtpst_2022_1_fecdec,
    GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE (rtpst2022_1_fecdec, "rtpst2022-1-fecdec",
    GST_RANK_NONE, GST_TYPE_RTPST_2022_1_FECDEC);

static void
trim_items (GstRTPST_2022_1_FecDec * dec)
{
  GSequenceIter *tmp_iter, *iter = NULL;

  for (tmp_iter = g_sequence_get_begin_iter (dec->packets);
      tmp_iter; tmp_iter = g_sequence_iter_next (tmp_iter)) {
    Item *item;

    if (g_sequence_iter_is_end (tmp_iter))
      break;

    item = g_sequence_get (tmp_iter);

    if (dec->max_arrival_time - GST_BUFFER_DTS_OR_PTS (item->buffer) <
        dec->size_time)
      break;

    iter = tmp_iter;
  }

  if (iter) {
    Item *item = g_sequence_get (iter);
    GST_TRACE_OBJECT (dec,
        "Trimming packets up to %" GST_TIME_FORMAT " (seq: %u)",
        GST_TIME_ARGS (GST_BUFFER_DTS_OR_PTS (item->buffer)), item->seq);
    g_sequence_remove_range (g_sequence_get_begin_iter (dec->packets),
        g_sequence_iter_next (iter));
  }
}

static void
trim_fec_items (GstRTPST_2022_1_FecDec * dec, guint D)
{
  GSequenceIter *tmp_iter, *iter = NULL;

  for (tmp_iter = g_sequence_get_begin_iter (dec->fec_packets[D]);
      tmp_iter; tmp_iter = g_sequence_iter_next (tmp_iter)) {
    Item *item;

    if (g_sequence_iter_is_end (tmp_iter))
      break;

    item = g_sequence_get (tmp_iter);

    if (dec->max_fec_arrival_time[D] - GST_BUFFER_DTS_OR_PTS (item->buffer) <
        dec->size_time)
      break;

    if (!D) {
      guint i;
      guint16 seq;

      for (i = 0; i < dec->d; i++) {
        seq = item->seq + i * dec->l;
        g_hash_table_remove (dec->column_fec_packets, GUINT_TO_POINTER (seq));
      }
    }

    iter = tmp_iter;
  }

  if (iter) {
    Item *item = g_sequence_get (iter);
    GST_TRACE_OBJECT (dec,
        "Trimming %s FEC packets up to %" GST_TIME_FORMAT " (seq: %u)",
        D ? "row" : "column",
        GST_TIME_ARGS (GST_BUFFER_DTS_OR_PTS (item->buffer)), item->seq);
    g_sequence_remove_range (g_sequence_get_begin_iter (dec->fec_packets[D]),
        g_sequence_iter_next (iter));
  }
}

static Item *
lookup_media_packet (GstRTPST_2022_1_FecDec * dec, guint16 seqnum)
{
  GSequenceIter *iter;
  Item *ret = NULL;
  Item dummy = { seqnum, NULL };

  iter =
      g_sequence_lookup (dec->packets, &dummy, (GCompareDataFunc) cmp_items,
      NULL);

  if (iter)
    ret = g_sequence_get (iter);

  return ret;
}

static gboolean
parse_header (GstRTPBuffer * rtp, Rtp2DFecHeader * fec)
{
  gboolean ret = FALSE;
  GstBitReader bits;
  guint8 *data = gst_rtp_buffer_get_payload (rtp);
  guint len = gst_rtp_buffer_get_payload_len (rtp);

  if (len < 16)
    goto done;

  gst_bit_reader_init (&bits, data, len);

  fec->marker = gst_rtp_buffer_get_marker (rtp);
  fec->padding = gst_rtp_buffer_get_padding (rtp);
  fec->extension = gst_rtp_buffer_get_extension (rtp);
  fec->seq = gst_bit_reader_get_bits_uint16_unchecked (&bits, 16);
  fec->len = gst_bit_reader_get_bits_uint16_unchecked (&bits, 16);
  fec->E = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->pt = gst_bit_reader_get_bits_uint8_unchecked (&bits, 7);
  fec->mask = gst_bit_reader_get_bits_uint32_unchecked (&bits, 24);
  fec->timestamp = gst_bit_reader_get_bits_uint32_unchecked (&bits, 32);
  fec->N = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->D = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->type = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);
  fec->index = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);
  fec->offset = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->NA = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->seq_ext = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->payload = data + 16;
  fec->payload_len = len - 16;

  ret = TRUE;

done:
  return ret;
}

static Item *
get_row_fec (GstRTPST_2022_1_FecDec * dec, guint16 seqnum)
{
  GSequenceIter *iter;
  Item *ret = NULL;
  Item dummy = { 0, };

  if (dec->l == G_MAXUINT)
    goto done;

  /* Potential underflow is intended */
  dummy.seq = seqnum - dec->l;

  iter =
      g_sequence_search (dec->fec_packets[1], &dummy,
      (GCompareDataFunc) cmp_items, NULL);

  if (!g_sequence_iter_is_end (iter)) {
    gint seqdiff;
    ret = g_sequence_get (iter);

    seqdiff = gst_rtp_buffer_compare_seqnum (ret->seq, seqnum);

    /* Now check whether the fec packet does apply */
    if (seqdiff < 0 || seqdiff >= dec->l)
      ret = NULL;
  }

done:
  return ret;
}

static Item *
get_column_fec (GstRTPST_2022_1_FecDec * dec, guint16 seqnum)
{
  Item *ret = NULL;

  if (dec->l == G_MAXUINT || dec->d == G_MAXUINT)
    goto done;

  ret =
      g_hash_table_lookup (dec->column_fec_packets, GUINT_TO_POINTER (seqnum));

done:
  return ret;
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

static GstFlowReturn
xor_items (GstRTPST_2022_1_FecDec * dec, Rtp2DFecHeader * fec, GList * packets,
    guint16 seqnum)
{
  guint8 *xored;
  guint32 xored_timestamp;
  guint8 xored_pt;
  guint16 xored_payload_len;
  Item *item;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GList *tmp;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer;
  gboolean xored_marker;
  gboolean xored_padding;
  gboolean xored_extension;

  /* Figure out the recovered packet length first */
  xored_payload_len = fec->len;
  for (tmp = packets; tmp; tmp = tmp->next) {
    GstRTPBuffer media_rtp = GST_RTP_BUFFER_INIT;
    Item *item = (Item *) tmp->data;

    gst_rtp_buffer_map (item->buffer, GST_MAP_READ, &media_rtp);
    xored_payload_len ^= gst_rtp_buffer_get_payload_len (&media_rtp);
    gst_rtp_buffer_unmap (&media_rtp);
  }

  if (xored_payload_len > fec->payload_len) {
    GST_WARNING_OBJECT (dec, "FEC payload len %u < length recovery %u",
        fec->payload_len, xored_payload_len);
    goto done;
  }

  item = g_malloc0 (sizeof (Item));
  item->seq = seqnum;
  item->buffer = gst_rtp_buffer_new_allocate (xored_payload_len, 0, 0);
  gst_rtp_buffer_map (item->buffer, GST_MAP_WRITE, &rtp);

  xored = gst_rtp_buffer_get_payload (&rtp);
  memcpy (xored, fec->payload, xored_payload_len);
  xored_timestamp = fec->timestamp;
  xored_pt = fec->pt;
  xored_marker = fec->marker;
  xored_padding = fec->padding;
  xored_extension = fec->extension;

  for (tmp = packets; tmp; tmp = tmp->next) {
    GstRTPBuffer media_rtp = GST_RTP_BUFFER_INIT;
    Item *item = (Item *) tmp->data;

    gst_rtp_buffer_map (item->buffer, GST_MAP_READ, &media_rtp);
    _xor_mem (xored, gst_rtp_buffer_get_payload (&media_rtp),
        MIN (gst_rtp_buffer_get_payload_len (&media_rtp), xored_payload_len));
    xored_timestamp ^= gst_rtp_buffer_get_timestamp (&media_rtp);
    xored_pt ^= gst_rtp_buffer_get_payload_type (&media_rtp);
    xored_marker ^= gst_rtp_buffer_get_marker (&media_rtp);
    xored_padding ^= gst_rtp_buffer_get_padding (&media_rtp);
    xored_extension ^= gst_rtp_buffer_get_extension (&media_rtp);

    gst_rtp_buffer_unmap (&media_rtp);
  }

  GST_DEBUG_OBJECT (dec,
      "Recovered buffer through %s FEC with seqnum %u, payload len %u and timestamp %u",
      fec->D ? "row" : "column", seqnum, xored_payload_len, xored_timestamp);

  GST_BUFFER_DTS (item->buffer) = dec->max_arrival_time;

  gst_rtp_buffer_set_timestamp (&rtp, xored_timestamp);
  gst_rtp_buffer_set_seq (&rtp, seqnum);
  gst_rtp_buffer_set_payload_type (&rtp, xored_pt);
  gst_rtp_buffer_set_marker (&rtp, xored_marker);
  gst_rtp_buffer_set_padding (&rtp, xored_padding);
  gst_rtp_buffer_set_extension (&rtp, xored_extension);

  gst_rtp_buffer_unmap (&rtp);

  /* Store a ref on item->buffer as store_media_item may
   * recurse and call this method again, potentially releasing
   * the object lock and leaving our item unprotected in
   * dec->packets
   */
  buffer = gst_buffer_ref (item->buffer);

  /* It is right that we should celebrate,
   * for your brother was dead, and is alive again */
  gst_rtp_buffer_map (item->buffer, GST_MAP_READ, &rtp);
  ret = store_media_item (dec, &rtp, item);
  gst_rtp_buffer_unmap (&rtp);

  if (ret == GST_FLOW_OK) {
    /* Unlocking here is safe */
    GST_OBJECT_UNLOCK (dec);
    ret = gst_pad_push (dec->srcpad, buffer);
    GST_OBJECT_LOCK (dec);
  } else {
    gst_buffer_unref (buffer);
  }

done:
  return ret;
}

/* Returns a flow value if we should discard the packet, GST_FLOW_CUSTOM_SUCCESS otherwise */
static GstFlowReturn
check_fec (GstRTPST_2022_1_FecDec * dec, Rtp2DFecHeader * fec)
{
  GList *packets = NULL;
  gint missing_seq = -1;
  guint n_packets = 0;
  guint required_n_packets;
  GstFlowReturn ret = GST_FLOW_OK;

  if (fec->D) {
    guint i = 0;

    required_n_packets = dec->l;

    for (i = 0; i < dec->l; i++) {
      Item *item = lookup_media_packet (dec, fec->seq + i);

      if (item) {
        packets = g_list_prepend (packets, item);
        n_packets += 1;
      } else {
        missing_seq = fec->seq + i;
      }
    }
  } else {
    guint i = 0;

    required_n_packets = dec->d;

    for (i = 0; i < dec->d; i++) {
      Item *item = lookup_media_packet (dec, fec->seq + i * dec->l);

      if (item) {
        packets = g_list_prepend (packets, item);
        n_packets += 1;
      } else {
        missing_seq = fec->seq + i * dec->l;
      }
    }
  }

  if (n_packets == required_n_packets) {
    g_assert (missing_seq == -1);
    GST_LOG_OBJECT (dec,
        "All media packets present, we can discard that FEC packet");
  } else if (n_packets + 1 == required_n_packets) {
    g_assert (missing_seq != -1);
    ret = xor_items (dec, fec, packets, missing_seq);
    GST_LOG_OBJECT (dec, "We have enough info to reconstruct %u", missing_seq);
  } else {
    ret = GST_FLOW_CUSTOM_SUCCESS;
    GST_LOG_OBJECT (dec, "Too many media packets missing, storing FEC packet");
  }
  g_list_free (packets);

  return ret;
}

static GstFlowReturn
check_fec_item (GstRTPST_2022_1_FecDec * dec, Item * item)
{
  Rtp2DFecHeader fec;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstFlowReturn ret;

  gst_rtp_buffer_map (item->buffer, GST_MAP_READ, &rtp);

  parse_header (&rtp, &fec);

  ret = check_fec (dec, &fec);

  gst_rtp_buffer_unmap (&rtp);

  return ret;
}

static GstFlowReturn
store_media_item (GstRTPST_2022_1_FecDec * dec, GstRTPBuffer * rtp, Item * item)
{
  GstFlowReturn ret = GST_FLOW_OK;
  Item *fec_item;
  guint16 seq;

  seq = gst_rtp_buffer_get_seq (rtp);

  g_sequence_insert_sorted (dec->packets, item, (GCompareDataFunc) cmp_items,
      NULL);

  if ((fec_item = get_row_fec (dec, seq))) {
    ret = check_fec_item (dec, fec_item);
    if (ret == GST_FLOW_CUSTOM_SUCCESS)
      ret = GST_FLOW_OK;
  }

  if (ret == GST_FLOW_OK && (fec_item = get_column_fec (dec, seq))) {
    ret = check_fec_item (dec, fec_item);
    if (ret == GST_FLOW_CUSTOM_SUCCESS)
      ret = GST_FLOW_OK;
  }

  return ret;
}

static GstFlowReturn
store_media (GstRTPST_2022_1_FecDec * dec, GstRTPBuffer * rtp,
    GstBuffer * buffer)
{
  Item *item;
  guint16 seq;

  seq = gst_rtp_buffer_get_seq (rtp);
  item = g_malloc0 (sizeof (Item));
  item->buffer = gst_buffer_ref (buffer);
  item->seq = seq;

  return store_media_item (dec, rtp, item);
}

static GstFlowReturn
gst_rtpst_2022_1_fecdec_sink_chain_fec (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (parent);
  Rtp2DFecHeader fec = { 0, };
  guint payload_len;
  guint8 *payload;
  GstFlowReturn ret = GST_FLOW_OK;
  Item *item;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  GST_OBJECT_LOCK (dec);

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)) {
    GST_WARNING_OBJECT (pad, "Chained FEC buffer isn't valid RTP");
    goto discard;
  }

  payload_len = gst_rtp_buffer_get_payload_len (&rtp);
  payload = gst_rtp_buffer_get_payload (&rtp);

  if (!parse_header (&rtp, &fec)) {
    GST_WARNING_OBJECT (pad, "Failed to parse FEC header (payload len: %d)",
        payload_len);
    GST_MEMDUMP_OBJECT (pad, "Invalid payload", payload, payload_len);
    goto discard;
  }

  GST_TRACE_OBJECT
      (pad,
      "Handling FEC buffer with SNBase / N / D / NA / offset %u / %u / %u / %u / %u",
      fec.seq, fec.N, fec.D, fec.NA, fec.offset);

  if (fec.D) {
    if (dec->l == G_MAXUINT) {
      dec->l = fec.NA;
    } else if (fec.NA != dec->l) {
      GST_WARNING_OBJECT (dec, "2D FEC dimensionality cannot change");
      goto discard;
    }

    if (fec.offset != 1) {
      GST_WARNING_OBJECT (pad, "offset must be 1 for row FEC packets");
      goto discard;
    }
  } else {
    if (dec->d == G_MAXUINT) {
      dec->d = fec.NA;
    } else if (fec.NA != dec->d) {
      GST_WARNING_OBJECT (dec, "2D FEC dimensionality cannot change");
      goto discard;
    }

    if (dec->l == G_MAXUINT) {
      dec->l = fec.offset;
    } else if (fec.offset != dec->l) {
      GST_WARNING_OBJECT (dec, "2D FEC dimensionality cannot change");
      goto discard;
    }
  }

  dec->max_fec_arrival_time[fec.D] = GST_BUFFER_DTS_OR_PTS (buffer);
  trim_fec_items (dec, fec.D);

  ret = check_fec (dec, &fec);

  if (ret == GST_FLOW_CUSTOM_SUCCESS) {
    item = g_malloc0 (sizeof (Item));
    item->buffer = buffer;
    item->seq = fec.seq;

    if (!fec.D) {
      guint i;
      guint16 seq;

      for (i = 0; i < dec->d; i++) {
        seq = fec.seq + i * dec->l;
        g_hash_table_insert (dec->column_fec_packets, GUINT_TO_POINTER (seq),
            item);
      }
    }
    g_sequence_insert_sorted (dec->fec_packets[fec.D], item,
        (GCompareDataFunc) cmp_items, NULL);
    ret = GST_FLOW_OK;
  } else {
    goto discard;
  }

  gst_rtp_buffer_unmap (&rtp);

done:
  GST_OBJECT_UNLOCK (dec);
  return ret;

discard:
  if (rtp.buffer != NULL)
    gst_rtp_buffer_unmap (&rtp);

  gst_buffer_unref (buffer);

  goto done;
}

static GstFlowReturn
gst_rtpst_2022_1_fecdec_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)) {
    GST_WARNING_OBJECT (pad, "Chained buffer isn't valid RTP");
    goto error;
  }

  GST_OBJECT_LOCK (dec);
  dec->max_arrival_time =
      MAX (dec->max_arrival_time, GST_BUFFER_DTS_OR_PTS (buffer));
  trim_items (dec);
  ret = store_media (dec, &rtp, buffer);
  GST_OBJECT_UNLOCK (dec);

  gst_rtp_buffer_unmap (&rtp);

  if (ret == GST_FLOW_OK)
    ret = gst_pad_push (dec->srcpad, buffer);

done:
  return ret;

error:
  gst_buffer_unref (buffer);
  goto done;
}

static gboolean
gst_rtpst_2022_1_fecdec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  if (!handled) {
    gst_pad_event_default (pad, parent, event);
  }

  return ret;
}

/* Takes the object lock */
static void
gst_rtpst_2022_1_fecdec_reset (GstRTPST_2022_1_FecDec * dec, gboolean allocate)
{
  guint i;

  GST_OBJECT_LOCK (dec);

  if (dec->packets) {
    g_sequence_free (dec->packets);
    dec->packets = NULL;
  }

  if (dec->column_fec_packets) {
    g_hash_table_unref (dec->column_fec_packets);
    dec->column_fec_packets = NULL;
  }

  if (allocate) {
    dec->packets = g_sequence_new ((GDestroyNotify) free_item);
    dec->column_fec_packets = g_hash_table_new (g_direct_hash, g_direct_equal);
  }

  for (i = 0; i < 2; i++) {
    if (dec->fec_packets[i]) {
      g_sequence_free (dec->fec_packets[i]);
      dec->fec_packets[i] = NULL;
    }

    if (allocate)
      dec->fec_packets[i] = g_sequence_new ((GDestroyNotify) free_item);
  }

  dec->d = G_MAXUINT;
  dec->l = G_MAXUINT;

  GST_OBJECT_UNLOCK (dec);
}

static GstStateChangeReturn
gst_rtpst_2022_1_fecdec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtpst_2022_1_fecdec_reset (dec, TRUE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtpst_2022_1_fecdec_reset (dec, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtpst_2022_1_fecdec_finalize (GObject * object)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (object);

  gst_rtpst_2022_1_fecdec_reset (dec, FALSE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtpst_2022_1_fecdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (object);

  switch (prop_id) {
    case PROP_SIZE_TIME:
      dec->size_time = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpst_2022_1_fecdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (object);

  switch (prop_id) {
    case PROP_SIZE_TIME:
      g_value_set_uint64 (value, dec->size_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_2d_fec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (parent);
  gboolean ret;

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP)
    gst_rtpst_2022_1_fecdec_reset (dec, TRUE);

  ret = gst_pad_event_default (pad, parent, event);

  return ret;
}

static GstIterator *
gst_rtpst_2022_1_fecdec_iterate_linked_pads (GstPad * pad, GstObject * parent)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (parent);
  GstPad *otherpad = NULL;
  GstIterator *it = NULL;
  GValue val = { 0, };

  if (pad == dec->srcpad)
    otherpad = dec->sinkpad;
  else if (pad == dec->sinkpad)
    otherpad = dec->srcpad;

  if (otherpad) {
    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, otherpad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);
  }

  return it;
}

static GstPad *
gst_rtpst_2022_1_fecdec_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (element);
  GstPad *sinkpad = NULL;

  GST_DEBUG_OBJECT (element, "requesting pad");

  if (g_list_length (dec->fec_sinkpads) > 1) {
    GST_ERROR_OBJECT (dec, "not accepting more than two fec streams");
    goto done;
  }

  sinkpad = gst_pad_new_from_template (templ, name);
  gst_pad_set_chain_function (sinkpad, gst_rtpst_2022_1_fecdec_sink_chain_fec);
  gst_element_add_pad (GST_ELEMENT (dec), sinkpad);
  gst_pad_set_iterate_internal_links_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_iterate_linked_pads));

  gst_pad_set_active (sinkpad, TRUE);

  GST_DEBUG_OBJECT (element, "requested pad %s:%s",
      GST_DEBUG_PAD_NAME (sinkpad));

done:
  return sinkpad;
}

static void
gst_rtpst_2022_1_fecdec_release_pad (GstElement * element, GstPad * pad)
{
  GstRTPST_2022_1_FecDec *dec = GST_RTPST_2022_1_FECDEC_CAST (element);

  GST_DEBUG_OBJECT (element, "releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  dec->fec_sinkpads = g_list_remove (dec->fec_sinkpads, pad);

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (dec), pad);
}

static void
gst_rtpst_2022_1_fecdec_class_init (GstRTPST_2022_1_FecDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_get_property);
  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_finalize);

  g_object_class_install_property (gobject_class, PROP_SIZE_TIME,
      g_param_spec_uint64 ("size-time", "Storage size (in ns)",
          "The amount of data to store (in ns, 0-disable)", 0,
          G_MAXUINT64, DEFAULT_SIZE_TIME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_release_pad);

  gst_element_class_set_static_metadata (gstelement_class,
      "SMPTE 2022-1 FEC decoder", "SMPTE 2022-1 FEC decoding",
      "performs FEC as described by SMPTE 2022-1",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &fec_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  GST_DEBUG_CATEGORY_INIT (gst_rtpst_2022_1_fecdec_debug,
      "rtpst2022-1-fecdec", 0, "SMPTE 2022-1 FEC decoder element");
}

static void
gst_rtpst_2022_1_fecdec_init (GstRTPST_2022_1_FecDec * dec)
{
  dec->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  GST_PAD_SET_PROXY_CAPS (dec->srcpad);
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_src_event));
  gst_pad_set_iterate_internal_links_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_iterate_linked_pads));
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  GST_PAD_SET_PROXY_CAPS (dec->sinkpad);
  gst_pad_set_chain_function (dec->sinkpad, gst_rtpst_2022_1_fecdec_sink_chain);
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_2d_fec_sink_event));
  gst_pad_set_iterate_internal_links_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpst_2022_1_fecdec_iterate_linked_pads));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->d = G_MAXUINT;
  dec->l = G_MAXUINT;
}
