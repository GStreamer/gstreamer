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

#include <gst/base/gstbitreader.h>
#include <string.h>

#include "mpegutil.h"

/* default intra quant matrix, in zig-zag order */
static const guint8 default_intra_quantizer_matrix[64] = {
  8,
  16, 16,
  19, 16, 19,
  22, 22, 22, 22,
  22, 22, 26, 24, 26,
  27, 27, 27, 26, 26, 26,
  26, 27, 27, 27, 29, 29, 29,
  34, 34, 34, 29, 29, 29, 27, 27,
  29, 29, 32, 32, 34, 34, 37,
  38, 37, 35, 35, 34, 35,
  38, 38, 40, 40, 40,
  48, 48, 46, 46,
  56, 56, 58,
  69, 69,
  83
};

guint8 mpeg2_scan[64] = {
  /* Zig-Zag scan pattern */
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

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

gboolean
mpeg_util_parse_sequence_extension (MPEGSeqExtHdr * hdr, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);;

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* skip extension code */
  if (!gst_bit_reader_skip (&reader, 4))
    return FALSE;

  /* skip profile and level escape bit */
  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->profile, 3))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->level, 4))
    return FALSE;

  /* progressive */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->progressive, 1))
    return FALSE;

  /* chroma format */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->chroma_format, 2))
    return FALSE;

  /* resolution extension */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->horiz_size_ext, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->vert_size_ext, 2))
    return FALSE;

  /* skip to framerate extension */
  if (!gst_bit_reader_skip (&reader, 22))
    return FALSE;

  /* framerate extension */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->fps_n_ext, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->fps_d_ext, 2))
    return FALSE;

  return TRUE;
}

gboolean
mpeg_util_parse_sequence_hdr (MPEGSeqHdr * hdr, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);
  guint8 dar_idx, par_idx;
  guint8 load_intra_flag, load_non_intra_flag;

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* resolution */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->width, 12))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->height, 12))
    return FALSE;

  /* aspect ratio */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &dar_idx, 4))
    return FALSE;
  set_par_from_dar (hdr, dar_idx);

  /* framerate */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &par_idx, 4))
    return FALSE;
  set_fps_from_code (hdr, par_idx);

  /* bitrate */
  if (!gst_bit_reader_get_bits_uint32 (&reader, &hdr->bitrate, 18))
    return FALSE;

  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  /* VBV buffer size */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->vbv_buffer, 10))
    return FALSE;

  /* constrained parameters flag */
  if (!gst_bit_reader_get_bits_uint8 (&reader,
          &hdr->constrained_parameters_flag, 1))
    return FALSE;

  /* intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_intra_flag, 1))
    return FALSE;
  if (load_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &hdr->intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memcpy (hdr->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  /* non intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_non_intra_flag, 1))
    return FALSE;
  if (load_non_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &hdr->non_intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memset (hdr->non_intra_quantizer_matrix, 16, 64);

  return TRUE;
}

gboolean
mpeg_util_parse_picture_hdr (MPEGPictureHdr * hdr, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* temperal sequence number */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->tsn, 10))
    return FALSE;

  /* frame type */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->pic_type, 3))
    return FALSE;

  if (hdr->pic_type == 0 || hdr->pic_type > 4)
    return FALSE;               /* Corrupted picture packet */

  /* VBV delay */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->vbv_delay, 16))
    return FALSE;

  if (hdr->pic_type == P_FRAME || hdr->pic_type == B_FRAME) {

    if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->full_pel_forward_vector,
            1))
      return FALSE;

    if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->f_code[0][0], 3))
      return FALSE;
    hdr->f_code[0][1] = hdr->f_code[0][0];

    if (hdr->pic_type == B_FRAME) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &hdr->full_pel_backward_vector, 1))
        return FALSE;

      if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->f_code[1][0], 3))
        return FALSE;
      hdr->f_code[1][1] = hdr->f_code[1][0];
    } else
      hdr->full_pel_backward_vector = 0;
  } else {
    hdr->full_pel_forward_vector = 0;
    hdr->full_pel_backward_vector = 0;
  }

  return TRUE;
}

gboolean
mpeg_util_parse_picture_coding_extension (MPEGPictureExt * ext,
    GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* skip extension code */
  if (!gst_bit_reader_skip (&reader, 4))
    return FALSE;

  /* f_code */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->f_code[0][0], 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->f_code[0][1], 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->f_code[1][0], 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->f_code[1][1], 4))
    return FALSE;

  /* intra DC precision */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->intra_dc_precision, 2))
    return FALSE;

  /* picture structure */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->picture_structure, 2))
    return FALSE;

  /* top field first */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->top_field_first, 1))
    return FALSE;

  /* frame pred frame dct */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->frame_pred_frame_dct, 1))
    return FALSE;

  /* concealment motion vectors */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->concealment_motion_vectors,
          1))
    return FALSE;

  /* q scale type */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->q_scale_type, 1))
    return FALSE;

  /* intra vlc format */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->intra_vlc_format, 1))
    return FALSE;

  /* alternate scan */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->alternate_scan, 1))
    return FALSE;

  /* repeat first field */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &ext->repeat_first_field, 1))
    return FALSE;

  return TRUE;
}

gboolean
mpeg_util_parse_gop (MPEGGop * gop, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->drop_frame_flag, 1))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->hour, 5))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->minute, 6))
    return FALSE;

  /* skip unused bit */
  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->second, 6))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->frame, 6))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->closed_gop, 1))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &gop->broken_gop, 1))
    return FALSE;

  return TRUE;
}

gboolean
mpeg_util_parse_quant_matrix (MPEGQuantMatrix * qm, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);
  guint8 load_intra_flag, load_non_intra_flag;

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* skip extension code */
  if (!gst_bit_reader_skip (&reader, 4))
    return FALSE;

  /* intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_intra_flag, 1))
    return FALSE;
  if (load_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &qm->intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memcpy (qm->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  /* non intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_non_intra_flag, 1))
    return FALSE;
  if (load_non_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &qm->non_intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memset (qm->non_intra_quantizer_matrix, 16, 64);

  return TRUE;
}
