/*
 * gstrtpvp8pay.c - Source for GstRtpVP8Pay
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtppayloads.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>
#include "gstrtpelements.h"
#include "dboolhuff.h"
#include "gstrtpvp8pay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_vp8_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_vp8_pay_debug

#define DEFAULT_PICTURE_ID_MODE VP8_PAY_NO_PICTURE_ID
#define DEFAULT_PICTURE_ID_OFFSET (-1)

enum
{
  PROP_0,
  PROP_PICTURE_ID,
  PROP_PICTURE_ID_MODE,
  PROP_PICTURE_ID_OFFSET
};

#define GST_TYPE_RTP_VP8_PAY_PICTURE_ID_MODE (gst_rtp_vp8_pay_picture_id_mode_get_type())
static GType
gst_rtp_vp8_pay_picture_id_mode_get_type (void)
{
  static GType mode_type = 0;
  static const GEnumValue modes[] = {
    {VP8_PAY_NO_PICTURE_ID, "No Picture ID", "none"},
    {VP8_PAY_PICTURE_ID_7BITS, "7-bit Picture ID", "7-bit"},
    {VP8_PAY_PICTURE_ID_15BITS, "15-bit Picture ID", "15-bit"},
    {0, NULL, NULL},
  };

  if (!mode_type) {
    mode_type = g_enum_register_static ("GstVP8RTPPayMode", modes);
  }
  return mode_type;
}

static void gst_rtp_vp8_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_vp8_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_rtp_vp8_pay_handle_buffer (GstRTPBasePayload * payload,
    GstBuffer * buffer);
static gboolean gst_rtp_vp8_pay_sink_event (GstRTPBasePayload * payload,
    GstEvent * event);
static gboolean gst_rtp_vp8_pay_set_caps (GstRTPBasePayload * payload,
    GstCaps * caps);

G_DEFINE_TYPE (GstRtpVP8Pay, gst_rtp_vp8_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpvp8pay, "rtpvp8pay",
    GST_RANK_MARGINAL, GST_TYPE_RTP_VP8_PAY, rtp_element_init (plugin));

static GstStaticPadTemplate gst_rtp_vp8_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ","
        "clock-rate = (int) 90000, encoding-name = (string) { \"VP8\", \"VP8-DRAFT-IETF-01\" }"));

static GstStaticPadTemplate gst_rtp_vp8_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8"));

static gint
picture_id_field_len (GstVP8RtpPayPictureIDMode mode)
{
  if (VP8_PAY_NO_PICTURE_ID == mode)
    return 0;
  if (VP8_PAY_PICTURE_ID_7BITS == mode)
    return 7;
  return 15;
}

static void
gst_rtp_vp8_pay_picture_id_reset (GstRtpVP8Pay * self)
{
  gint nbits;
  gint old_picture_id = self->picture_id;
  gint picture_id = 0;

  if (self->picture_id_mode != VP8_PAY_NO_PICTURE_ID) {
    if (self->picture_id_offset == -1) {
      picture_id = g_random_int ();
    } else {
      picture_id = self->picture_id_offset;
    }
    nbits = picture_id_field_len (self->picture_id_mode);
    picture_id &= (1 << nbits) - 1;
  }
  g_atomic_int_set (&self->picture_id, picture_id);

  GST_LOG_OBJECT (self, "picture-id reset %d -> %d",
      old_picture_id, picture_id);
}

static void
gst_rtp_vp8_pay_picture_id_increment (GstRtpVP8Pay * self)
{
  gint nbits;

  if (self->picture_id_mode == VP8_PAY_NO_PICTURE_ID)
    return;

  /* Atomically increment and wrap the picture id if it overflows */
  nbits = picture_id_field_len (self->picture_id_mode);
  gint picture_id = g_atomic_int_get (&self->picture_id);
  picture_id++;
  picture_id &= (1 << nbits) - 1;
  g_atomic_int_set (&self->picture_id, picture_id);
}

static void
gst_rtp_vp8_pay_reset (GstRtpVP8Pay * obj)
{
  gst_rtp_vp8_pay_picture_id_reset (obj);
  /* tl0picidx MAY start at a random value, but there's no point. Initialize
   * so that first packet will use 0 for convenience */
  obj->tl0picidx = -1;
  obj->temporal_scalability_fields_present = FALSE;
}

static void
gst_rtp_vp8_pay_init (GstRtpVP8Pay * obj)
{
  obj->picture_id_mode = DEFAULT_PICTURE_ID_MODE;
  obj->picture_id_offset = DEFAULT_PICTURE_ID_OFFSET;
  gst_rtp_vp8_pay_reset (obj);
}

static void
gst_rtp_vp8_pay_class_init (GstRtpVP8PayClass * gst_rtp_vp8_pay_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (gst_rtp_vp8_pay_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (gst_rtp_vp8_pay_class);
  GstRTPBasePayloadClass *pay_class =
      GST_RTP_BASE_PAYLOAD_CLASS (gst_rtp_vp8_pay_class);

  gobject_class->set_property = gst_rtp_vp8_pay_set_property;
  gobject_class->get_property = gst_rtp_vp8_pay_get_property;

  /**
   * rtpvp8pay:picture-id:
   *
   * Currently used picture-id
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_PICTURE_ID,
      g_param_spec_int ("picture-id", "Picture ID",
          "Currently used picture-id for payloading", 0, 0x7FFF, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PICTURE_ID_MODE,
      g_param_spec_enum ("picture-id-mode", "Picture ID Mode",
          "The picture ID mode for payloading",
          GST_TYPE_RTP_VP8_PAY_PICTURE_ID_MODE, DEFAULT_PICTURE_ID_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * rtpvp8pay:picture-id-offset:
   *
   * Offset to add to the initial picture-id (-1 = random)
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_PICTURE_ID_OFFSET,
      g_param_spec_int ("picture-id-offset", "Picture ID offset",
          "Offset to add to the initial picture-id (-1 = random)",
          -1, 0x7FFF, DEFAULT_PICTURE_ID_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp8_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp8_pay_src_template);

  gst_element_class_set_static_metadata (element_class, "RTP VP8 payloader",
      "Codec/Payloader/Network/RTP",
      "Puts VP8 video in RTP packets", "Sjoerd Simons <sjoerd@luon.net>");

  pay_class->handle_buffer = gst_rtp_vp8_pay_handle_buffer;
  pay_class->sink_event = gst_rtp_vp8_pay_sink_event;
  pay_class->set_caps = gst_rtp_vp8_pay_set_caps;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_vp8_pay_debug, "rtpvp8pay", 0,
      "VP8 Video RTP Payloader");

  gst_type_mark_as_plugin_api (GST_TYPE_RTP_VP8_PAY_PICTURE_ID_MODE, 0);
}

static void
gst_rtp_vp8_pay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpVP8Pay *rtpvp8pay = GST_RTP_VP8_PAY (object);

  switch (prop_id) {
    case PROP_PICTURE_ID_MODE:
      rtpvp8pay->picture_id_mode = g_value_get_enum (value);
      gst_rtp_vp8_pay_picture_id_reset (rtpvp8pay);
      break;
    case PROP_PICTURE_ID_OFFSET:
      rtpvp8pay->picture_id_offset = g_value_get_int (value);
      gst_rtp_vp8_pay_picture_id_reset (rtpvp8pay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_vp8_pay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpVP8Pay *rtpvp8pay = GST_RTP_VP8_PAY (object);

  switch (prop_id) {
    case PROP_PICTURE_ID:
      g_value_set_int (value, g_atomic_int_get (&rtpvp8pay->picture_id));
      break;
    case PROP_PICTURE_ID_MODE:
      g_value_set_enum (value, rtpvp8pay->picture_id_mode);
      break;
    case PROP_PICTURE_ID_OFFSET:
      g_value_set_int (value, rtpvp8pay->picture_id_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rtp_vp8_pay_parse_frame (GstRtpVP8Pay * self, GstBuffer * buffer,
    gsize buffer_size)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstBitReader reader;
  guint8 *data;
  gsize size;
  int i;
  gboolean keyframe;
  guint32 partition0_size;
  guint8 version;
  guint8 tmp8 = 0;
  guint8 partitions;
  guint offset;
  BOOL_DECODER bc;
  guint8 *pdata;

  if (G_UNLIKELY (buffer_size < 3))
    goto error;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ) || !map.data)
    goto error;

  data = map.data;
  size = map.size;

  gst_bit_reader_init (&reader, data, size);

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

  if (!gst_bit_reader_skip (&reader, 24))
    goto error;

  if (keyframe) {
    /* check start tag: 0x9d 0x01 0x2a */
    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x9d)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x01)
      goto error;

    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp8, 8) || tmp8 != 0x2a)
      goto error;

    /* Skip horizontal size code (16 bits) vertical size code (16 bits) */
    if (!gst_bit_reader_skip (&reader, 32))
      goto error;
  }

  offset = keyframe ? 10 : 3;
  vp8dx_start_decode (&bc, data + offset, size - offset);

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
  if (partition0_size + (partitions - 1) * 3 >= size)
    goto error;

  /* partition data is right after the mode partition */
  pdata = data + partition0_size;

  /* Set up mapping */
  self->n_partitions = partitions + 1;
  self->partition_offset[0] = 0;
  self->partition_size[0] = partition0_size + (partitions - 1) * 3;

  self->partition_offset[1] = self->partition_size[0];
  for (i = 1; i < partitions; i++) {
    guint psize = (pdata[2] << 16 | pdata[1] << 8 | pdata[0]);

    pdata += 3;
    self->partition_size[i] = psize;
    self->partition_offset[i + 1] = self->partition_offset[i] + psize;
  }

  /* Check that our partition offsets and sizes don't go outsize the buffer
   * size. */
  if (self->partition_offset[i] >= size)
    goto error;

  self->partition_size[i] = size - self->partition_offset[i];

  self->partition_offset[i + 1] = size;

  gst_buffer_unmap (buffer, &map);

  if (keyframe)
    GST_DEBUG_OBJECT (self, "Parsed keyframe");

  return TRUE;

error:
  GST_DEBUG ("Failed to parse frame");
  if (map.memory != NULL) {
    gst_buffer_unmap (buffer, &map);
  }
  return FALSE;
}

static guint
gst_rtp_vp8_offset_to_partition (GstRtpVP8Pay * self, guint offset)
{
  int i;

  for (i = 1; i < self->n_partitions; i++) {
    if (offset < self->partition_offset[i])
      return i - 1;
  }

  return i - 1;
}

static gsize
gst_rtp_vp8_calc_header_len (GstRtpVP8Pay * self)
{
  gsize len;

  switch (self->picture_id_mode) {
    case VP8_PAY_PICTURE_ID_7BITS:
      len = 1;
      break;
    case VP8_PAY_PICTURE_ID_15BITS:
      len = 2;
      break;
    case VP8_PAY_NO_PICTURE_ID:
    default:
      len = 0;
      break;
  }

  if (self->temporal_scalability_fields_present) {
    /* Add on space for TL0PICIDX and TID/Y/KEYIDX */
    len += 2;
  }

  if (len > 0) {
    /* All fields above are extension, so allocate space for the ECB field */
    len++;
  }

  return len + 1;               /* computed + fixed size header */
}

/* When growing the vp8 header keep max payload len calculation in sync */
static GstBuffer *
gst_rtp_vp8_create_header_buffer (GstRtpVP8Pay * self, guint8 partid,
    gboolean start, gboolean mark, GstBuffer * in, GstCustomMeta * meta)
{
  GstBuffer *out;
  guint8 *p;
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

  out =
      gst_rtp_base_payload_allocate_output_buffer (GST_RTP_BASE_PAYLOAD_CAST
      (self), gst_rtp_vp8_calc_header_len (self), 0, 0);
  gst_rtp_buffer_map (out, GST_MAP_READWRITE, &rtpbuffer);
  p = gst_rtp_buffer_get_payload (&rtpbuffer);

  /* X=0,R=0,N=0,S=start,PartID=partid */
  p[0] = (start << 4) | partid;
  if (GST_BUFFER_FLAG_IS_SET (in, GST_BUFFER_FLAG_DROPPABLE)) {
    /* Enable N=1 */
    p[0] |= 0x20;
  }

  if (self->picture_id_mode != VP8_PAY_NO_PICTURE_ID ||
      self->temporal_scalability_fields_present) {
    gint index;

    /* Enable X=1 */
    p[0] |= 0x80;

    /* X: I=0,L=0,T=0,K=0,RSV=0 */
    p[1] = 0x00;
    if (self->picture_id_mode != VP8_PAY_NO_PICTURE_ID) {
      /* Set I bit */
      p[1] |= 0x80;
    }
    if (self->temporal_scalability_fields_present) {
      /* Set L and T bits */
      p[1] |= 0x60;
    }

    /* Insert picture ID */
    if (self->picture_id_mode == VP8_PAY_PICTURE_ID_7BITS) {
      /* I: 7 bit picture_id */
      p[2] = self->picture_id & 0x7F;
      index = 3;
    } else if (self->picture_id_mode == VP8_PAY_PICTURE_ID_15BITS) {
      /* I: 15 bit picture_id */
      p[2] = 0x80 | ((self->picture_id & 0x7FFF) >> 8);
      p[3] = self->picture_id & 0xFF;
      index = 4;
    } else {
      index = 2;
    }

    /* Insert TL0PICIDX and TID/Y/KEYIDX */
    if (self->temporal_scalability_fields_present) {
      /* The meta contains tl0picidx from the encoder, but we need to ensure
       * that tl0picidx is increasing correctly. The encoder may reset it's
       * state and counter, but we cannot. Therefore, we cannot simply copy
       * the value into the header.*/
      guint temporal_layer = 0;
      gboolean layer_sync = FALSE;
      gboolean use_temporal_scaling = FALSE;

      if (meta) {
        gst_structure_get_boolean (meta->structure, "use-temporal-scaling",
            &use_temporal_scaling);

        if (use_temporal_scaling)
          gst_structure_get (meta->structure, "layer-id", G_TYPE_UINT,
              &temporal_layer, "layer-sync", G_TYPE_BOOLEAN, &layer_sync, NULL);
      }

      /* FIXME: Support a prediction structure where higher layers don't
       * necessarily refer to the last base layer frame, ie they use an older
       * tl0picidx as signalled in the meta */
      if (temporal_layer == 0 && start)
        self->tl0picidx++;
      p[index] = self->tl0picidx & 0xFF;
      p[index + 1] = ((temporal_layer << 6) | (layer_sync << 5)) & 0xFF;
    }
  }

  gst_rtp_buffer_set_marker (&rtpbuffer, mark);
  if (mark)
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_MARKER);

  gst_rtp_buffer_unmap (&rtpbuffer);

  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in);
  GST_BUFFER_PTS (out) = GST_BUFFER_PTS (in);

  return out;
}

static gboolean
foreach_metadata_drop (GstBuffer * buf, GstMeta ** meta, gpointer user_data)
{
  GstElement *element = user_data;
  const GstMetaInfo *info = (*meta)->info;

  if (gst_meta_info_is_custom (info) &&
      gst_custom_meta_has_name ((GstCustomMeta *) * meta, "GstVP8Meta")) {
    GST_DEBUG_OBJECT (element, "dropping GstVP8Meta");
    *meta = NULL;
  }

  return TRUE;
}

static void
gst_rtp_vp8_drop_vp8_meta (gpointer element, GstBuffer * buf)
{
  gst_buffer_foreach_meta (buf, foreach_metadata_drop, element);
}

static guint
gst_rtp_vp8_payload_next (GstRtpVP8Pay * self, GstBufferList * list,
    guint offset, GstBuffer * buffer, gsize buffer_size, gsize max_payload_len,
    GstCustomMeta * meta, gboolean delta_unit)
{
  guint partition;
  GstBuffer *header;
  GstBuffer *sub;
  GstBuffer *out;
  gboolean mark;
  gboolean start;
  gsize remaining;
  gsize available;

  remaining = buffer_size - offset;
  available = max_payload_len;
  if (available > remaining)
    available = remaining;

  if (meta) {
    /* If meta is present, then we have no partition offset information,
     * so always emit PID 0 and set the start bit for the first packet
     * of a frame only (c.f. RFC7741 $4.4)
     */
    partition = 0;
    start = (offset == 0);
  } else {
    partition = gst_rtp_vp8_offset_to_partition (self, offset);
    g_assert (partition < self->n_partitions);
    start = (offset == self->partition_offset[partition]);
  }

  mark = (remaining == available);
  /* whole set of partitions, payload them and done */
  header = gst_rtp_vp8_create_header_buffer (self, partition,
      start, mark, buffer, meta);
  sub = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, offset, available);

  gst_rtp_copy_video_meta (self, header, buffer);
  gst_rtp_vp8_drop_vp8_meta (self, header);

  out = gst_buffer_append (header, sub);

  if (delta_unit)
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DELTA_UNIT);

  gst_buffer_list_insert (list, -1, out);

  return available;
}


static GstFlowReturn
gst_rtp_vp8_pay_handle_buffer (GstRTPBasePayload * payload, GstBuffer * buffer)
{
  GstRtpVP8Pay *self = GST_RTP_VP8_PAY (payload);
  GstFlowReturn ret;
  GstBufferList *list;
  GstCustomMeta *meta;
  gsize size, max_paylen;
  guint offset, mtu, vp8_hdr_len;
  gboolean delta_unit;

  size = gst_buffer_get_size (buffer);
  meta = gst_buffer_get_custom_meta (buffer, "GstVP8Meta");
  delta_unit = GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  if (G_UNLIKELY (!gst_rtp_vp8_pay_parse_frame (self, buffer, size))) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
        ("Failed to parse VP8 frame"));
    return GST_FLOW_ERROR;
  }

  if (meta) {
    gboolean use_temporal_scaling;
    /* For interop it's most likely better to keep the temporal scalability
     * fields present if the stream previously had them present. Alternating
     * whether these fields are present or not may confuse the receiver. */

    gst_structure_get_boolean (meta->structure, "use-temporal-scaling",
        &use_temporal_scaling);
    if (use_temporal_scaling)
      self->temporal_scalability_fields_present = TRUE;
  }

  mtu = GST_RTP_BASE_PAYLOAD_MTU (payload);
  vp8_hdr_len = gst_rtp_vp8_calc_header_len (self);
  max_paylen = gst_rtp_buffer_calc_payload_len (mtu - vp8_hdr_len, 0,
      gst_rtp_base_payload_get_source_count (payload, buffer));

  list = gst_buffer_list_new_sized ((size / max_paylen) + 1);

  offset = 0;
  while (offset < size) {
    offset +=
        gst_rtp_vp8_payload_next (self, list, offset, buffer, size,
        max_paylen, meta, delta_unit);

    /* only the first outgoing packet should not have the DELTA_UNIT flag */
    if (!delta_unit)
      delta_unit = TRUE;
  }

  ret = gst_rtp_base_payload_push_list (payload, list);

  gst_rtp_vp8_pay_picture_id_increment (self);

  gst_buffer_unref (buffer);

  return ret;
}

static gboolean
gst_rtp_vp8_pay_sink_event (GstRTPBasePayload * payload, GstEvent * event)
{
  GstRtpVP8Pay *self = GST_RTP_VP8_PAY (payload);
  GstEventType event_type = GST_EVENT_TYPE (event);

  if (event_type == GST_EVENT_GAP || event_type == GST_EVENT_FLUSH_START) {
    gint picture_id = self->picture_id;
    gst_rtp_vp8_pay_picture_id_increment (self);
    GST_DEBUG_OBJECT (payload, "Incrementing picture ID on %s event %d -> %d",
        GST_EVENT_TYPE_NAME (event), picture_id, self->picture_id);
  }

  return GST_RTP_BASE_PAYLOAD_CLASS (gst_rtp_vp8_pay_parent_class)->sink_event
      (payload, event);
}

static gboolean
gst_rtp_vp8_pay_set_caps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstCaps *src_caps;
  const char *encoding_name = "VP8";

  src_caps = gst_pad_get_allowed_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload));
  if (src_caps) {
    GstStructure *s;
    const GValue *value;

    s = gst_caps_get_structure (src_caps, 0);

    if (gst_structure_has_field (s, "encoding-name")) {
      GValue default_value = G_VALUE_INIT;

      g_value_init (&default_value, G_TYPE_STRING);
      g_value_set_static_string (&default_value, encoding_name);

      value = gst_structure_get_value (s, "encoding-name");
      if (!gst_value_can_intersect (&default_value, value))
        encoding_name = "VP8-DRAFT-IETF-01";
    }
    gst_caps_unref (src_caps);
  }

  gst_rtp_base_payload_set_options (payload, "video", TRUE,
      encoding_name, 90000);

  return gst_rtp_base_payload_set_outcaps (payload, NULL);
}
