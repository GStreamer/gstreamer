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
    MpegTSPacketizerPacket * packet)
{

  //guint32 i = 0;
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
      return TRUE;
    } else if (*state == PICTURE_START_CODE) {
      Mpeg2PictureHeader hdr = { 0 };
      gboolean success;
      *state = 0xffffffff;
      success = parse_mpeg2_picture_header (&hdr, data, data_end);
      GST_DEBUG ("found picture start code, %sparsed, picture coding type: %d",
          success ? "" : "not ", hdr.picture_coding_type);
      return success && hdr.picture_coding_type == 1;
    }
  }

  return FALSE;
}
