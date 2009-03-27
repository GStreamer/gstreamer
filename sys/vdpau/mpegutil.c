/* GStreamer
 * Copyright (C) 2007 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#include "mpegutil.h"

guint8 bits[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

guint32
read_bits (guint8 * buf, gint start_bit, gint n_bits)
{
  gint i;
  guint32 ret = 0x00;

  buf += start_bit / 8;
  start_bit %= 8;

  for (i = 0; i < n_bits; i++) {
    guint32 tmp;

    tmp = ((*buf & bits[start_bit]) >> (7 - start_bit));
    ret = (ret | (tmp << (n_bits - i - 1)));
    if (++start_bit == 8) {
      buf += 1;
      start_bit = 0;
    }
  }

  return ret;
}

guint8 *
mpeg_util_find_start_code (guint32 * sync_word, guint8 * cur, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY (cur == NULL))
    return NULL;

  code = *sync_word;

  while (cur < end) {
    code <<= 8;

    if (code == 0x00000100) {
      /* Reset the sync word accumulator */
      *sync_word = 0xffffffff;
      return cur;
    }

    /* Add the next available byte to the collected sync word */
    code |= *cur++;
  }

  *sync_word = code;
  return NULL;
}

static void
set_fps_from_code (MPEGSeqHdr * hdr, guint8 fps_code)
{
  const gint framerates[][2] = {
    {30, 1}, {24000, 1001}, {24, 1}, {25, 1},
    {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001},
    {60, 1}, {30, 1}
  };

  if (fps_code < 10) {
    hdr->fps_n = framerates[fps_code][0];
    hdr->fps_d = framerates[fps_code][1];
  } else {
    /* Force a valid framerate */
    hdr->fps_n = 30000;
    hdr->fps_d = 1001;
  }
}

/* Set the Pixel Aspect Ratio in our hdr from a DAR code in the data */
static void
set_par_from_dar (MPEGSeqHdr * hdr, guint8 asr_code)
{
  /* Pixel_width = DAR_width * display_vertical_size */
  /* Pixel_height = DAR_height * display_horizontal_size */
  switch (asr_code) {
    case 0x02:                 /* 3:4 DAR = 4:3 pixels */
      hdr->par_w = 4 * hdr->height;
      hdr->par_h = 3 * hdr->width;
      break;
    case 0x03:                 /* 9:16 DAR */
      hdr->par_w = 16 * hdr->height;
      hdr->par_h = 9 * hdr->width;
      break;
    case 0x04:                 /* 1:2.21 DAR */
      hdr->par_w = 221 * hdr->height;
      hdr->par_h = 100 * hdr->width;
      break;
    case 0x01:                 /* Square pixels */
    default:
      hdr->par_w = hdr->par_h = 1;
      break;
  }
}

static gboolean
mpeg_util_parse_extension_packet (MPEGSeqHdr * hdr, guint8 * data, guint8 * end)
{
  guint8 ext_code;

  if (G_UNLIKELY (data >= end))
    return FALSE;               /* short extension packet */

  ext_code = data[0] >> 4;

  switch (ext_code) {
    case MPEG_PACKET_EXT_SEQUENCE:
    {
      /* Parse a Sequence Extension */
      guint8 horiz_size_ext, vert_size_ext;
      guint8 fps_n_ext, fps_d_ext;

      if (G_UNLIKELY ((end - data) < 6))
        /* need at least 10 bytes, minus 4 for the start code 000001b5 */
        return FALSE;

      horiz_size_ext = ((data[1] << 1) & 0x02) | ((data[2] >> 7) & 0x01);
      vert_size_ext = (data[2] >> 5) & 0x03;
      hdr->profile = read_bits (data, 7, 3);
      fps_n_ext = (data[5] >> 5) & 0x03;
      fps_d_ext = data[5] & 0x1f;

      hdr->fps_n *= (fps_n_ext + 1);
      hdr->fps_d *= (fps_d_ext + 1);
      hdr->width += (horiz_size_ext << 12);
      hdr->height += (vert_size_ext << 12);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

gboolean
mpeg_util_parse_sequence_hdr (MPEGSeqHdr * hdr, guint8 * data, guint8 * end)
{
  guint32 code;
  guint8 dar_idx, fps_idx;
  guint32 sync_word = 0xffffffff;
  gboolean constrained_flag;
  gboolean load_intra_flag;
  gboolean load_non_intra_flag;

  if (G_UNLIKELY ((end - data) < 12))
    return FALSE;               /* Too small to be a sequence header */

  code = GST_READ_UINT32_BE (data);
  if (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_SEQUENCE)))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  /* Parse the MPEG 1 bits */
  hdr->mpeg_version = 1;

  code = GST_READ_UINT32_BE (data);
  hdr->width = (code >> 20) & 0xfff;
  hdr->height = (code >> 8) & 0xfff;

  dar_idx = (code >> 4) & 0xf;
  set_par_from_dar (hdr, dar_idx);
  fps_idx = code & 0xf;
  set_fps_from_code (hdr, fps_idx);

  constrained_flag = (data[7] >> 2) & 0x01;
  load_intra_flag = (data[7] >> 1) & 0x01;
  if (load_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    data += 64;
  }

  load_non_intra_flag = data[7] & 0x01;
  if (load_non_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    data += 64;
  }

  /* Advance past the rest of the MPEG-1 header */
  data += 8;

  /* Read MPEG-2 sequence extensions */
  data = mpeg_util_find_start_code (&sync_word, data, end);
  while (data != NULL) {
    if (G_UNLIKELY (data >= end))
      return FALSE;

    /* data points at the last byte of the start code */
    if (data[0] == MPEG_PACKET_EXTENSION) {
      if (!mpeg_util_parse_extension_packet (hdr, data + 1, end))
        return FALSE;

      hdr->mpeg_version = 2;
    }
    data = mpeg_util_find_start_code (&sync_word, data, end);
  }

  return TRUE;
}
