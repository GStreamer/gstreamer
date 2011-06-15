/*
 * payload_parsers.c
 * Copyright (C) 2011 Janne Grunau
 *
 * Authors:
 *   Janne Grunau <janne.grunau@collabora.co.uk>
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

#include "payload_parsers.h"
#include <gst/base/gstbitreader.h>

#define PICTURE_START_CODE 0x00000100
#define GROUP_START_CODE   0x000001B8

#define SLICE_NAL_UNIT_TYPE     0x01
#define SLICE_IDR_NAL_UNIT_TYPE 0x05
#define SEI_NAL_UNIT_TYPE       0x06

#define SEI_TYPE_RECOVERY_POINT 0x06

typedef struct Mpeg2PictureHeader
{
  guint16 temporal_reference;
  guint8 picture_coding_type;
  guint16 vbv_delay;

  /* picture_coding_type == 2 || picture_coding_type */
  guint8 full_pel_forward_vector;
  guint8 forward_f_code;

  /* picture_coding_type == 3 */
  guint8 full_pel_backward_vector;
  guint8 backward_f_code;
} Mpeg2PictureHeader;

/* shortened slice header */
typedef struct H264SliceHeader
{
  guint32 first_mb_in_slice;
  guint8 slice_type;
} H264SliceHeader;


static guint8 *
find_start_code (guint32 * start_code, guint8 * buffer, guint8 * buffer_end)
{
  if (G_UNLIKELY (buffer == NULL) || G_UNLIKELY (buffer_end == NULL)
      || G_UNLIKELY (start_code == NULL))
    return NULL;

  while (buffer <= buffer_end) {

    *start_code <<= 8;
    *start_code |= *buffer++;

    if ((*start_code & 0xffffff00) == 0x00000100)
      return buffer;
  }

  return NULL;
}

static gboolean
parse_mpeg2_picture_header (Mpeg2PictureHeader * hdr, guint8 * buffer,
    guint8 * buffer_end)
{
  GstBitReader br = GST_BIT_READER_INIT (buffer, buffer_end - buffer);

  if (gst_bit_reader_get_remaining (&br) < 40)
    return FALSE;

  hdr->temporal_reference = gst_bit_reader_get_bits_uint16_unchecked (&br, 10);
  hdr->picture_coding_type = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  hdr->vbv_delay = gst_bit_reader_get_bits_uint16_unchecked (&br, 16);

  if (hdr->picture_coding_type == 2 || hdr->picture_coding_type == 3) {
    hdr->full_pel_forward_vector =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
    hdr->forward_f_code = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  }
  if (hdr->picture_coding_type == 3) {
    hdr->full_pel_backward_vector =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
    hdr->backward_f_code = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  }
  return TRUE;
}

gboolean
gst_tsdemux_has_mpeg2_keyframe (guint32 * state,
    MpegTSPacketizerPacket * packet, gboolean * need_more)
{
  guint8 *data = packet->payload;
  guint8 *data_end = packet->data_end;

  GST_LOG ("state: 0x%08x", *state);

  while (data <= data_end) {

    data = find_start_code (state, data, data_end);

    if (!data)
      return FALSE;

    GST_LOG ("found start code: 0x%08x", *state);

    if (*state == GROUP_START_CODE) {
      GST_DEBUG ("found group start code");
      *state = 0xffffffff;
      *need_more = FALSE;
      return TRUE;
    } else if (*state == PICTURE_START_CODE) {
      Mpeg2PictureHeader hdr = { 0 };
      gboolean success;

      success = parse_mpeg2_picture_header (&hdr, data, data_end);
      GST_DEBUG ("found picture start code, %sparsed, picture coding type: %d",
          success ? "" : "not ", hdr.picture_coding_type);

      *state = 0xffffffff;
      *need_more = FALSE;
      return success && hdr.picture_coding_type == 1;
    }
  }

  return FALSE;
}

/* variable length Exp-Golomb parsing according to H.264 spec 9.1*/
static gboolean
read_golomb (GstBitReader * br, guint32 * value)
{
  guint8 b, leading_zeros = -1;
  *value = 1;

  for (b = 0; !b; leading_zeros++) {
    if (!gst_bit_reader_get_bits_uint8 (br, &b, 1))
      return FALSE;
    *value *= 2;
  }

  *value = (*value >> 1) - 1;
  if (leading_zeros > 0) {
    guint32 tmp = 0;
    if (!gst_bit_reader_get_bits_uint32 (br, &tmp, leading_zeros))
      return FALSE;
    *value += tmp;
  }

  return TRUE;
}

/* just parse the requirred bits of the slice header */
static gboolean
parse_h264_slice_header (H264SliceHeader * hdr, guint8 * buffer,
    guint8 * buffer_end)
{
  guint32 value;
  GstBitReader br = GST_BIT_READER_INIT (buffer, buffer_end - buffer);

  if (!read_golomb (&br, &value))
    return FALSE;
  hdr->first_mb_in_slice = value;

  if (!read_golomb (&br, &value))
    return FALSE;
  hdr->slice_type = value;

  return TRUE;
}

enum H264SliceTypes
{
  h264_p_slice = 0,
  h264_b_slice,
  h264_i_slice,
  h264_sp_slice,
  h264_si_slice,
  h264_p_slice_a,
  h264_b_slice_a,
  h264_i_slice_a,
  h264_sp_slice_a,
  h264_si_slice_a,
};

static gboolean
is_key_slice (guint8 slice_type)
{
  switch (slice_type) {
    case h264_i_slice:
    case h264_si_slice:
    case h264_i_slice_a:
    case h264_si_slice_a:
      return TRUE;
  }
  return FALSE;
}

gboolean
gst_tsdemux_has_h264_keyframe (guint32 * state, MpegTSPacketizerPacket * packet,
    gboolean * need_more)
{
  guint8 *data = packet->payload;
  guint8 *data_end = packet->data_end;

  GST_LOG ("state: 0x%08x", *state);

  while (data <= data_end) {
    guint8 nal_unit_type;
    guint8 *next_data = NULL;

    data = find_start_code (state, data, data_end);

    if (!data)
      goto beach;

    GST_LOG ("found start code: 0x%08x", *state);

    /* determine length */
    nal_unit_type = *state & 0x1f;
    next_data = find_start_code (state, data, data_end);

    if (nal_unit_type == SEI_NAL_UNIT_TYPE && !next_data) {
      GST_WARNING ("NAL unit 0x%02x not completely in ts packet",
          nal_unit_type);
      goto beach;
    }
    next_data -= 4;

    switch (nal_unit_type) {
      case SLICE_IDR_NAL_UNIT_TYPE:
        GST_DEBUG ("found SLICE_IDR NAL unit type");
        *state = 0xffffffff;
        *need_more = FALSE;
        return TRUE;
      case SLICE_NAL_UNIT_TYPE:
      {
        H264SliceHeader hdr = { 0 };
        gboolean success;

        success = parse_h264_slice_header (&hdr, data, data_end);
        GST_DEBUG ("found SLICE NAL unit type with slice type %d",
            hdr.slice_type);

        *state = 0xffffffff;
        *need_more = FALSE;
        return success && is_key_slice (hdr.slice_type);
      }
      case SEI_NAL_UNIT_TYPE:
      {
        guint32 recovery_frame_count;
        GstBitReader br = GST_BIT_READER_INIT (data, next_data - data);

        break;

        /* SEI message is at least 24 bit long */
        while (gst_bit_reader_get_remaining (&br) >= 24) {
          gint type = 0, size = 0;
          guint8 tmp = 0;

          do {
            if (!gst_bit_reader_get_bits_uint8 (&br, &tmp, 8))
              goto beach;
            type += tmp;
          } while (tmp == 255);

          do {
            if (!gst_bit_reader_get_bits_uint8 (&br, &tmp, 8))
              goto beach;
            size += tmp;
          } while (tmp == 255);


          GST_LOG ("found SEI msg type: %d, len: %d", type, size);

          switch (type) {
            case SEI_TYPE_RECOVERY_POINT:
              if (!read_golomb (&br, &recovery_frame_count))
                return FALSE;
              gst_bit_reader_skip (&br, 1);     /* exact_match */
              gst_bit_reader_skip (&br, 1);     /* broken_link_flag */
              gst_bit_reader_skip (&br, 2);     /* changing_slice_group_idc */
              GST_DEBUG ("found SEI with recovery point message, "
                  "recovery_frame_count: %d", recovery_frame_count);
              return TRUE;
            default:
              /* skip all other sei messages */
              gst_bit_reader_skip (&br, size * 8);
          }
        }
      }
        data = next_data;
        *state = 0xffffffff;
    }
  }
beach:
  return FALSE;
}
