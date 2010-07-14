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
const guint8 default_intra_quantizer_matrix[64] = {
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

const guint8 mpeg_zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint16 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT32(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint32 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT64(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint64 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
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

  READ_UINT8 (&reader, hdr->profile, 3);
  READ_UINT8 (&reader, hdr->level, 4);

  /* progressive */
  READ_UINT8 (&reader, hdr->progressive, 1);

  /* chroma format */
  READ_UINT8 (&reader, hdr->chroma_format, 2);

  /* resolution extension */
  READ_UINT8 (&reader, hdr->horiz_size_ext, 2);
  READ_UINT8 (&reader, hdr->vert_size_ext, 2);

  READ_UINT16 (&reader, hdr->bitrate_ext, 12);

  /* skip to framerate extension */
  if (!gst_bit_reader_skip (&reader, 9))
    return FALSE;

  /* framerate extension */
  READ_UINT8 (&reader, hdr->fps_n_ext, 2);
  READ_UINT8 (&reader, hdr->fps_d_ext, 2);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Sequence Extension\"");
  return FALSE;
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
  READ_UINT16 (&reader, hdr->width, 12);
  READ_UINT16 (&reader, hdr->height, 12);

  /* aspect ratio */
  READ_UINT8 (&reader, dar_idx, 4);
  set_par_from_dar (hdr, dar_idx);

  /* framerate */
  READ_UINT8 (&reader, par_idx, 4);
  set_fps_from_code (hdr, par_idx);

  /* bitrate */
  READ_UINT32 (&reader, hdr->bitrate, 18);

  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  /* VBV buffer size */
  READ_UINT16 (&reader, hdr->vbv_buffer, 10);

  /* constrained parameters flag */
  READ_UINT8 (&reader, hdr->constrained_parameters_flag, 1);

  /* intra quantizer matrix */
  READ_UINT8 (&reader, load_intra_flag, 1);
  if (load_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++)
      READ_UINT8 (&reader, hdr->intra_quantizer_matrix[mpeg_zigzag_8x8[i]], 8);
  } else
    memcpy (hdr->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  /* non intra quantizer matrix */
  READ_UINT8 (&reader, load_non_intra_flag, 1);
  if (load_non_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++)
      READ_UINT8 (&reader, hdr->non_intra_quantizer_matrix[mpeg_zigzag_8x8[i]],
          8);
  } else
    memset (hdr->non_intra_quantizer_matrix, 16, 64);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Sequence Header\"");
  return FALSE;
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

    READ_UINT8 (&reader, hdr->full_pel_forward_vector, 1);

    READ_UINT8 (&reader, hdr->f_code[0][0], 3);
    hdr->f_code[0][1] = hdr->f_code[0][0];
  } else {
    hdr->full_pel_forward_vector = 0;
    hdr->f_code[0][0] = hdr->f_code[0][1] = 0;
  }

  if (hdr->pic_type == B_FRAME) {
    READ_UINT8 (&reader, hdr->full_pel_backward_vector, 1);

    READ_UINT8 (&reader, hdr->f_code[1][0], 3);
    hdr->f_code[1][1] = hdr->f_code[1][0];
  } else {
    hdr->full_pel_backward_vector = 0;
    hdr->f_code[1][0] = hdr->f_code[1][1] = 0;
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Picture Header\"");
  return FALSE;
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
  READ_UINT8 (&reader, ext->f_code[0][0], 4);
  READ_UINT8 (&reader, ext->f_code[0][1], 4);
  READ_UINT8 (&reader, ext->f_code[1][0], 4);
  READ_UINT8 (&reader, ext->f_code[1][1], 4);

  /* intra DC precision */
  READ_UINT8 (&reader, ext->intra_dc_precision, 2);

  /* picture structure */
  READ_UINT8 (&reader, ext->picture_structure, 2);

  /* top field first */
  READ_UINT8 (&reader, ext->top_field_first, 1);

  /* frame pred frame dct */
  READ_UINT8 (&reader, ext->frame_pred_frame_dct, 1);

  /* concealment motion vectors */
  READ_UINT8 (&reader, ext->concealment_motion_vectors, 1);

  /* q scale type */
  READ_UINT8 (&reader, ext->q_scale_type, 1);

  /* intra vlc format */
  READ_UINT8 (&reader, ext->intra_vlc_format, 1);

  /* alternate scan */
  READ_UINT8 (&reader, ext->alternate_scan, 1);

  /* repeat first field */
  READ_UINT8 (&reader, ext->repeat_first_field, 1);

  /* chroma_420_type */
  READ_UINT8 (&reader, ext->chroma_420_type, 1);

  /* progressive_frame */
  READ_UINT8 (&reader, ext->progressive_frame, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Picture Coding Extension\"");
  return FALSE;
}

gboolean
mpeg_util_parse_gop (MPEGGop * gop, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  READ_UINT8 (&reader, gop->drop_frame_flag, 1);

  READ_UINT8 (&reader, gop->hour, 5);

  READ_UINT8 (&reader, gop->minute, 6);

  /* skip unused bit */
  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  READ_UINT8 (&reader, gop->second, 6);

  READ_UINT8 (&reader, gop->frame, 6);

  READ_UINT8 (&reader, gop->closed_gop, 1);

  READ_UINT8 (&reader, gop->broken_gop, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"GOP\"");
  return FALSE;
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
  READ_UINT8 (&reader, load_intra_flag, 1);
  if (load_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      READ_UINT8 (&reader, qm->intra_quantizer_matrix[mpeg_zigzag_8x8[i]], 8);
    }
  } else
    memcpy (qm->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  /* non intra quantizer matrix */
  READ_UINT8 (&reader, load_non_intra_flag, 1);
  if (load_non_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      READ_UINT8 (&reader, qm->non_intra_quantizer_matrix[mpeg_zigzag_8x8[i]],
          8);
    }
  } else
    memset (qm->non_intra_quantizer_matrix, 16, 64);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Quant Matrix Extension\"");
  return FALSE;

}

#undef READ_UINT8
#undef READ_UINT16
#undef READ_UINT32
#undef READ_UINT64
