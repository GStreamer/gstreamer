/*
 * gst-rtp-vp8-pay.c - Source for GstRtpVP8Pay
 * Copyright (C) 2011 Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) 2011 Collabora Ltd.
 *   Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
#include "dboolhuff.h"
#include "gstrtpvp8pay.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_vp8_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_vp8_pay_debug

#define DEFAULT_PICTURE_ID_MODE VP8_PAY_PICTURE_ID_7BITS

GST_BOILERPLATE (GstRtpVP8Pay, gst_rtp_vp8_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static GstStaticPadTemplate gst_rtp_vp8_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ","
        "clock-rate = (int) 90000, encoding-name = (string) \"VP8-DRAFT-IETF-01\""));

static GstStaticPadTemplate gst_rtp_vp8_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8"));

static void
gst_rtp_vp8_pay_init (GstRtpVP8Pay * obj, GstRtpVP8PayClass * klass)
{
  /* TODO: Make it configurable */
  obj->picture_id_mode = DEFAULT_PICTURE_ID_MODE;
  if (obj->picture_id_mode == VP8_PAY_PICTURE_ID_7BITS)
    obj->picture_id = g_random_int_range (0, G_MAXUINT8) & 0x7F;
  else if (obj->picture_id_mode == VP8_PAY_PICTURE_ID_15BITS)
    obj->picture_id = g_random_int_range (0, G_MAXUINT16) & 0x7FFF;
}

static GstFlowReturn gst_rtp_vp8_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);
static gboolean gst_rtp_vp8_pay_handle_event (GstPad * pad, GstEvent * event);
static gboolean gst_rtp_vp8_pay_set_caps (GstBaseRTPPayload * payload,
    GstCaps * caps);

static void
gst_rtp_vp8_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp8_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp8_pay_src_template);

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
  pay_class->handle_event = gst_rtp_vp8_pay_handle_event;
  pay_class->set_caps = gst_rtp_vp8_pay_set_caps;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_vp8_pay_debug, "rtpvp8pay", 0,
      "VP8 Video RTP Payloader");
}

static gboolean
gst_rtp_vp8_pay_parse_frame (GstRtpVP8Pay * self, GstBuffer * buffer)
{
  GstBitReader *reader;
  int i;
  gboolean keyframe;
  guint32 partition0_size;
  guint8 version;
  guint8 tmp8 = 0;
  guint8 *data;
  guint8 partitions;
  guint offset;
  BOOL_DECODER bc;

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
  partition0_size = data[2] << 11 | data[1] << 3 | (data[0] >> 5);

  /* Include the uncompressed data blob in the first partition */
  offset = keyframe ? 10 : 3;
  partition0_size += offset;

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

    /* Skip horizontal size code (16 bits) vertical size code (16 bits) */
    if (!gst_bit_reader_skip (reader, 32))
      goto error;
  }

  offset = keyframe ? 10 : 3;
  vp8dx_start_decode (&bc, GST_BUFFER_DATA (buffer) + offset,
      GST_BUFFER_SIZE (buffer) - offset);

  if (keyframe) {
    /* color space (1 bit) and clamping type (1 bit) */
    vp8dx_decode_bool (&bc, 0x80);
    vp8dx_decode_bool (&bc, 0x80);
  }

  /* segmentation_enabled */
  if (vp8dx_decode_bool (&bc, 0x80)) {
    guint8 update_mb_segmentation_map = vp8dx_decode_bool (&bc, 0x80);
    guint8 update_segment_feature_data = vp8dx_decode_bool (&bc, 0x80);

    if (update_segment_feature_data) {
      /* skip segment feature mode */
      vp8dx_decode_bool (&bc, 0x80);

      /* quantizer update */
      for (i = 0; i < 4; i++) {
        /* skip flagged quantizer value (7 bits) and sign (1 bit) */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 8);
      }

      /* loop filter update */
      for (i = 0; i < 4; i++) {
        /* skip flagged lf update value (6 bits) and sign (1 bit) */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 7);
      }
    }

    if (update_mb_segmentation_map) {
      /* segment prob update */
      for (i = 0; i < 3; i++) {
        /* skip flagged segment prob */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 8);
      }
    }
  }

  /* skip filter type (1 bit), loop filter level (6 bits) and
   * sharpness level (3 bits) */
  vp8_decode_value (&bc, 1);
  vp8_decode_value (&bc, 6);
  vp8_decode_value (&bc, 3);

  /* loop_filter_adj_enabled */
  if (vp8dx_decode_bool (&bc, 0x80)) {

    /* delta update */
    if (vp8dx_decode_bool (&bc, 0x80)) {

      for (i = 0; i < 8; i++) {
        /* 8 updates, 1 bit indicate whether there is one and if follow by a
         * 7 bit update */
        if (vp8dx_decode_bool (&bc, 0x80))
          vp8_decode_value (&bc, 7);
      }
    }
  }

  if (vp8dx_bool_error (&bc))
    goto error;

  tmp8 = vp8_decode_value (&bc, 2);

  partitions = 1 << tmp8;

  /* Check if things are still sensible */
  if (partition0_size + (partitions - 1) * 3 >= GST_BUFFER_SIZE (buffer))
    goto error;

  /* partition data is right after the mode partition */
  data = GST_BUFFER_DATA (buffer) + partition0_size;

  /* Set up mapping */
  self->n_partitions = partitions + 1;
  self->partition_offset[0] = 0;
  self->partition_size[0] = partition0_size + (partitions - 1) * 3;

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
gst_rtp_vp8_offset_to_partition (GstRtpVP8Pay * self, guint offset)
{
  int i;

  for (i = 0; i < self->n_partitions; i++) {
    if (offset >= self->partition_offset[i] &&
        offset < self->partition_offset[i + 1])
      return i;
  }

  return i;
}

static gsize
gst_rtp_vp8_calc_header_len (GstRtpVP8Pay * self)
{
  switch (self->picture_id_mode) {
    case VP8_PAY_PICTURE_ID_7BITS:
      return 3;
    case VP8_PAY_PICTURE_ID_15BITS:
      return 4;
    case VP8_PAY_NO_PICTURE_ID:
    default:
      return 1;
  }
}


static gsize
gst_rtp_vp8_calc_payload_len (GstRtpVP8Pay * self)
{
  GstBaseRTPPayload *payload = GST_BASE_RTP_PAYLOAD (self);
  return gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU (payload) -
      gst_rtp_vp8_calc_header_len (self), 0, 0);
}

/* When growing the vp8 header keep gst_rtp_vp8_calc_payload_len in sync */
static GstBuffer *
gst_rtp_vp8_create_header_buffer (GstRtpVP8Pay * self, guint8 partid,
    gboolean start, gboolean mark, GstBuffer * in)
{
  GstBuffer *out;
  guint8 *p;

  out = gst_rtp_buffer_new_allocate (gst_rtp_vp8_calc_header_len (self), 0, 0);
  p = gst_rtp_buffer_get_payload (out);
  /* X=0,R=0,N=0,S=start,PartID=partid */
  p[0] = (start << 4) | partid;
  if (self->picture_id_mode != VP8_PAY_NO_PICTURE_ID) {
    /* Enable X=1 */
    p[0] |= 0x80;
    /* X: I=1,L=0,T=0,RSVA=0 */
    p[1] = 0x80;
    if (self->picture_id_mode == VP8_PAY_PICTURE_ID_7BITS) {
      /* I: 7 bit picture_id */
      p[2] = self->picture_id & 0x7F;
    } else {
      /* I: 15 bit picture_id */
      p[2] = 0x80 | ((self->picture_id & 0x7FFF) >> 8);
      p[3] = self->picture_id & 0xFF;
    }
  }

  gst_rtp_buffer_set_marker (out, mark);

  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in);
  GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (in);

  return out;
}


static guint
gst_rtp_vp8_payload_next (GstRtpVP8Pay * self,
    GstBufferListIterator * it, guint offset, GstBuffer * buffer)
{
  guint partition;
  GstBuffer *header;
  GstBuffer *sub;
  gboolean mark;
  gsize remaining;
  gsize available;

  remaining = GST_BUFFER_SIZE (buffer) - offset;
  available = gst_rtp_vp8_calc_payload_len (self);
  if (available > remaining)
    available = remaining;

  partition = gst_rtp_vp8_offset_to_partition (self, offset);
  g_assert (partition < self->n_partitions);

  mark = (remaining == available);
  /* whole set of partitions, payload them and done */
  header = gst_rtp_vp8_create_header_buffer (self, partition,
      offset == self->partition_offset[partition], mark, buffer);
  sub = gst_buffer_create_sub (buffer, offset, available);

  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, header);
  gst_buffer_list_iterator_add (it, sub);

  return GST_BUFFER_SIZE (sub);
}


static GstFlowReturn
gst_rtp_vp8_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstRtpVP8Pay *self = GST_RTP_VP8_PAY (payload);
  GstFlowReturn ret;
  GstBufferList *list;
  GstBufferListIterator *it;
  guint offset;

  if (G_UNLIKELY (!gst_rtp_vp8_pay_parse_frame (self, buffer))) {
    g_message ("Failed to parse frame");
    return GST_FLOW_ERROR;
  }

  list = gst_buffer_list_new ();
  it = gst_buffer_list_iterate (list);

  for (offset = 0; offset < GST_BUFFER_SIZE (buffer);)
    offset += gst_rtp_vp8_payload_next (self, it, offset, buffer);

  ret = gst_basertppayload_push_list (payload, list);
  gst_buffer_list_iterator_free (it);

  /* Incremenent and wrap the picture id if it overflows */
  if ((self->picture_id_mode == VP8_PAY_PICTURE_ID_7BITS &&
          ++self->picture_id >= 0x80) ||
      (self->picture_id_mode == VP8_PAY_PICTURE_ID_15BITS &&
          ++self->picture_id >= 0x8000))
    self->picture_id = 0;

  return ret;
}

static gboolean
gst_rtp_vp8_pay_handle_event (GstPad * pad, GstEvent * event)
{
  GstRtpVP8Pay *self = GST_RTP_VP8_PAY (gst_pad_get_parent (pad));

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START) {
    if (self->picture_id_mode == VP8_PAY_PICTURE_ID_7BITS)
      self->picture_id = g_random_int_range (0, G_MAXUINT8) & 0x7F;
    else if (self->picture_id_mode == VP8_PAY_PICTURE_ID_15BITS)
      self->picture_id = g_random_int_range (0, G_MAXUINT16) & 0x7FFF;
  }

  gst_object_unref (self);

  return FALSE;
}

static gboolean
gst_rtp_vp8_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "video", TRUE,
      "VP8-DRAFT-IETF-01", 90000);
  return gst_basertppayload_set_outcaps (payload, NULL);
}

gboolean
gst_rtp_vp8_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvp8pay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_VP8_PAY);
}
