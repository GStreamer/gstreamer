/*
 * gst-rtp-vp8-pay.c - Source for GstRtpVP8Pay
 * Copyright (C) 2011 Sjoerd Simons <sjoerd@luon.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtppayloads.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpvp8pay.h"

#define FI_FRAG_UNFRAGMENTED 0x0
#define FI_FRAG_START 0x1
#define FI_FRAG_MIDDLE 0x2
#define FI_FRAG_END 0x3

GST_DEBUG_CATEGORY_STATIC (gst_rtp_vp8_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_vp8_pay_debug

GST_BOILERPLATE (GstRtpVP8Pay, gst_rtp_vp8_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static GstStaticPadTemplate gst_rtp_vp8_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ","
        "clock-rate = (int) 90000, encoding-name = (string) \"VP8-DRAFT-0-3-2\""));

static GstStaticPadTemplate gst_rtp_vp8_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8"));

static void
gst_rtp_vp8_pay_init (GstRtpVP8Pay * obj, GstRtpVP8PayClass * klass)
{
}

static GstFlowReturn gst_rtp_vp8_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);
static gboolean gst_rtp_vp8_pay_set_caps (GstBaseRTPPayload * payload,
    GstCaps * caps);

static void
gst_rtp_vp8_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vp8_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vp8_pay_src_template));

  gst_element_class_set_details_simple (element_class, "RTP VP8 payloader",
      "Codec/Payloader/Network/RTP",
      "Puts VP8 video in RTP packets)", "Sjoerd Simons <sjoerd@luon.net>");
}

static void
gst_rtp_vp8_pay_class_init (GstRtpVP8PayClass * gst_rtp_vp8_pay_class)
{
  GstBaseRTPPayloadClass *pay_class =
      GST_BASE_RTP_PAYLOAD_CLASS (gst_rtp_vp8_pay_class);

  pay_class->handle_buffer = gst_rtp_vp8_pay_handle_buffer;
  pay_class->set_caps = gst_rtp_vp8_pay_set_caps;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_vp8_pay_debug, "rtpvp8pay", 0,
      "VP8 Video RTP Payloader");
}

static gsize
gst_rtp_vp8_calc_payload_len (GstBaseRTPPayload * payload)
{
  return gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU (payload) -
      1, 0, 0);
}

/* When growing the vp8 header keep gst_rtp_vp8_calc_payload_len in sync */
static GstBuffer *
gst_rtp_vp8_create_header_buffer (gboolean start, gboolean mark, guint fi,
    GstBuffer * in)
{
  GstBuffer *out;
  guint8 *p;

  out = gst_rtp_buffer_new_allocate (1, 0, 0);
  p = gst_rtp_buffer_get_payload (out);
  /* Hardcode I = 0 and N = 0, only set FI and B */
  p[0] = (fi & 0x3) << 1 | (start ? 1 : 0);

  gst_rtp_buffer_set_marker (out, mark);

  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in);
  GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (in);

  return out;
}


static gboolean
gst_rtp_vp8_pay_parse_frame (GstRtpVP8Pay * self, GstBuffer * buffer)
{
  GstBitReader *reader;
  int i;
  gboolean keyframe;
  guint32 header_size;
  guint8 version;
  guint8 tmp8 = 0;
  guint8 *data;
  guint8 partitions;

  reader = gst_bit_reader_new_from_buffer (buffer);

  if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < 3))
    goto error;

  data = GST_BUFFER_DATA (buffer);

  self->is_keyframe = keyframe = ((data[0] & 0x1) == 0);
  version = (data[0] >> 1) & 0x7;

  if (G_UNLIKELY (version > 3)) {
    GST_ERROR_OBJECT (self, "Unknown VP8 version %u", version);
    goto error;
  }

  /* keyframe, version and show_frame use 5 bits */
  header_size = data[2] << 11 | data[1] << 3 | (data[0] >> 5);

  /* Include the uncompressed data blob in the header */
  header_size += keyframe ? 10 : 3;

  if (!gst_bit_reader_skip (reader, 24))
    goto error;

  if (keyframe) {
    /* check start tag: 0x9d 0x01 0x2a */
    if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 8) || tmp8 != 0x9d)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 8) || tmp8 != 0x01)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 8) || tmp8 != 0x2a)
      goto error;

    /* Skip horizontal size code (16 bits) vertical size code (16 bits),
     * color space (1 bit) and clamping type (1 bit) */
    if (!gst_bit_reader_skip (reader, 34))
      goto error;
  }

  /* segmentation_enabled */
  if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
    goto error;

  if (tmp8 != 0) {
    gboolean update_mb_segmentation_map;
    gboolean update_segment_feature_data;

    if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 2))
      goto error;

    update_mb_segmentation_map = (tmp8 & 0x2) != 0;
    update_segment_feature_data = (tmp8 & 0x1) != 0;

    if (update_segment_feature_data) {
      /* skip segment feature mode */
      if (!gst_bit_reader_skip (reader, 1))
        goto error;

      for (i = 0; i < 4; i++) {
        /* quantizer update */
        if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
          goto error;

        if (tmp8 != 0) {
          /* skip quantizer value (7 bits) and sign (1 bit) */
          if (!gst_bit_reader_skip (reader, 8))
            goto error;
        }
      }

      for (i = 0; i < 4; i++) {
        /* loop filter update */
        if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
          goto error;

        if (tmp8 != 0) {
          /* skip lf update value (6 bits) and sign (1 bit) */
          if (!gst_bit_reader_skip (reader, 7))
            goto error;
        }
      }
    }

    if (update_mb_segmentation_map) {
      for (i = 0; i < 3; i++) {
        /* segment prob update */
        if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
          goto error;

        if (tmp8 != 0) {
          /* skip segment prob */
          if (!gst_bit_reader_skip (reader, 8))
            goto error;
        }
      }
    }
  }

  /* skip filter type (1 bit), loop filter level (6 bits) and
   * sharpness level (3 bits) */
  if (!gst_bit_reader_skip (reader, 10))
    goto error;

  /* loop_filter_adj_enabled */
  if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
    goto error;

  if (tmp8 != 0) {
    /* loop filter adj enabled */

    /* mode_ref_lf_delta_update */
    if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
      goto error;

    if (tmp8 != 0) {
      /* mode_ref_lf_data_update */
      int i;

      for (i = 0; i < 8; i++) {
        /* 8 updates, 1 bit indicate whether there is one and if follow by a
         * 7 bit update */
        if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 1))
          goto error;

        if (tmp8 != 0) {
          /* skip delta magnitude (6 bits) and sign (1 bit) */
          if (!gst_bit_reader_skip (reader, 7))
            goto error;
        }
      }
    }
  }

  if (!gst_bit_reader_get_bits_uint8 (reader, &tmp8, 2))
    goto error;

  partitions = 1 << tmp8;

  /* Check if things are still sensible */
  if (header_size + (partitions - 1) * 3 >= GST_BUFFER_SIZE (buffer))
    goto error;

  /* partition data is right after the frame header */
  data = GST_BUFFER_DATA (buffer) + header_size;

  /* Set up mapping, count the initial header as a partition to make other
   * sections of the code easier */
  self->n_partitions = partitions + 1;
  self->partition_offset[0] = 0;
  self->partition_size[0] = header_size + (partitions - 1) * 3;

  self->partition_offset[1] = self->partition_size[0];
  for (i = 1; i < partitions; i++) {
    guint size = (data[2] << 16 | data[1] << 8 | data[0]);

    data += 3;
    self->partition_size[i] = size;
    self->partition_offset[i + 1] = self->partition_offset[i] + size;
  }

  /* Check that our partition offsets and sizes don't go outsize the buffer
   * size. */
  if (self->partition_offset[i] >= GST_BUFFER_SIZE (buffer))
    goto error;

  self->partition_size[i] = GST_BUFFER_SIZE (buffer)
      - self->partition_offset[i];

  self->partition_offset[i + 1] = GST_BUFFER_SIZE (buffer);

  gst_bit_reader_free (reader);
  return TRUE;

error:
  GST_DEBUG ("Failed to parse frame");
  gst_bit_reader_free (reader);
  return FALSE;
}

static guint
gst_rtp_vp8_fit_partitions (GstRtpVP8Pay * self, gint first, gsize available)
{
  guint num = 0;
  int i;

  g_assert (first < self->n_partitions);

  for (i = first;
      i < self->n_partitions && self->partition_size[i] < available; i++) {
    num++;
    available -= self->partition_size[i];
  }

  return num;
}

static GstBuffer *
gst_rtp_vp8_create_sub (GstRtpVP8Pay * self,
    GstBuffer * buffer, guint current, guint num)
{
  guint offset = self->partition_offset[current];
  guint size = self->partition_offset[current + num] - offset;

  return gst_buffer_create_sub (buffer, offset, size);
}


static guint
gst_rtp_vp8_payload_next (GstRtpVP8Pay * self,
    GstBufferListIterator * it, guint first, GstBuffer * buffer)
{
  guint num;
  GstBuffer *header;
  GstBuffer *sub;
  gboolean mark;
  gsize available = gst_rtp_vp8_calc_payload_len (GST_BASE_RTP_PAYLOAD (self));

  g_assert (first < 9);

  /* How many partitions can we fit */
  num = gst_rtp_vp8_fit_partitions (self, first, available);

  if (num > 0) {
    mark = (first + num == self->n_partitions);
    /* whole set of partitions, payload them and done */
    header = gst_rtp_vp8_create_header_buffer (first == 0, mark,
        FI_FRAG_UNFRAGMENTED, buffer);
    sub = gst_rtp_vp8_create_sub (self, buffer, first, num);

    gst_buffer_list_iterator_add_group (it);
    gst_buffer_list_iterator_add (it, header);
    gst_buffer_list_iterator_add (it, sub);
  } else {
    /* Fragmented packets */
    guint offset = self->partition_offset[first];
    guint left = self->partition_size[first];
    gboolean start = (first == 0);

    header = gst_rtp_vp8_create_header_buffer (start, FALSE,
        FI_FRAG_START, buffer);
    sub = gst_buffer_create_sub (buffer, offset, available);
    offset += available;

    gst_buffer_list_iterator_add_group (it);
    gst_buffer_list_iterator_add (it, header);
    gst_buffer_list_iterator_add (it, sub);

    left -= available;

    for (; left > available; left -= available) {
      header = gst_rtp_vp8_create_header_buffer (start, FALSE,
          FI_FRAG_MIDDLE, buffer);

      sub = gst_buffer_create_sub (buffer, offset, available);
      offset += available;

      gst_buffer_list_iterator_add_group (it);
      gst_buffer_list_iterator_add (it, header);
      gst_buffer_list_iterator_add (it, sub);
    }

    mark = (first + 1 == self->n_partitions);

    header = gst_rtp_vp8_create_header_buffer (start, mark,
        FI_FRAG_END, buffer);
    sub = gst_buffer_create_sub (buffer, offset, left);

    gst_buffer_list_iterator_add_group (it);
    gst_buffer_list_iterator_add (it, header);
    gst_buffer_list_iterator_add (it, sub);

    return 1;
  }

  return num;
}


static GstFlowReturn
gst_rtp_vp8_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstRtpVP8Pay *self = GST_RTP_VP8_PAY (payload);
  GstFlowReturn ret;
  GstBufferList *list;
  GstBufferListIterator *it;
  guint current;

  if (G_UNLIKELY (!gst_rtp_vp8_pay_parse_frame (self, buffer))) {
    /* FIXME throw flow error */
    g_message ("Failed to parse frame");
    return GST_FLOW_ERROR;
  }

  list = gst_buffer_list_new ();
  it = gst_buffer_list_iterate (list);

  for (current = 0; current < self->n_partitions;) {
    guint n;

    n = gst_rtp_vp8_payload_next (self, it, current, buffer);
    current += n;
  }

  ret = gst_basertppayload_push_list (payload, list);
  gst_buffer_list_iterator_free (it);

  return ret;
}

static gboolean
gst_rtp_vp8_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "video", TRUE,
      "VP8-DRAFT-0-3-2", 90000);
  return gst_basertppayload_set_outcaps (payload, NULL);
}

gboolean
gst_rtp_vp8_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvp8pay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_VP8_PAY);
}
