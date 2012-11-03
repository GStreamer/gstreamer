/*
* GStreamer
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "mpeg4util.h"

GST_DEBUG_CATEGORY_EXTERN (gst_vdp_mpeg4_dec_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg4_dec_debug

const guint8 default_intra_quant_mat[64] = {
  8, 17, 18, 19, 21, 23, 25, 27,
  17, 18, 19, 21, 23, 25, 27, 28,
  20, 21, 22, 23, 24, 26, 28, 30,
  21, 22, 23, 24, 26, 28, 30, 32,
  22, 23, 24, 26, 28, 30, 32, 35,
  23, 24, 26, 28, 30, 32, 35, 38,
  25, 26, 28, 30, 32, 35, 38, 41,
  27, 28, 30, 32, 35, 38, 41, 45
};

const guint8 default_non_intra_quant_mat[64] = {
  16, 17, 18, 19, 20, 21, 22, 23,
  17, 18, 19, 20, 21, 22, 23, 24,
  18, 19, 20, 21, 22, 23, 24, 25,
  19, 20, 21, 22, 23, 24, 26, 27,
  20, 21, 22, 23, 25, 26, 27, 28,
  21, 22, 23, 24, 26, 27, 28, 30,
  22, 23, 24, 26, 27, 28, 30, 31,
  23, 24, 25, 27, 28, 30, 31, 33,
};

const guint8 mpeg4_zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

gboolean
mpeg4_util_parse_VOP (GstBuffer * buf, Mpeg4VideoObjectLayer * vol,
    Mpeg4VideoObjectPlane * vop)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);

  guint8 vop_start_code;
  guint8 modulo_time_base;

  /* set default values */
  vop->modulo_time_base = 0;
  vop->rounding_type = 0;
  vop->top_field_first = 1;
  vop->alternate_vertical_scan_flag = 0;
  vop->fcode_forward = 1;
  vop->fcode_backward = 1;

  /* start code prefix */
  SKIP (&reader, 24);

  READ_UINT8 (&reader, vop_start_code, 8);
  if (vop_start_code != MPEG4_PACKET_VOP)
    goto wrong_start_code;

  READ_UINT8 (&reader, vop->coding_type, 2);

  READ_UINT8 (&reader, modulo_time_base, 1);
  while (modulo_time_base) {
    vop->modulo_time_base++;

    READ_UINT8 (&reader, modulo_time_base, 1);
  }

  /* marker bit */
  SKIP (&reader, 1);
  READ_UINT16 (&reader, vop->time_increment, vol->vop_time_increment_bits);
  /* marker bit */
  SKIP (&reader, 1);

  READ_UINT8 (&reader, vop->coded, 1);
  if (!vop->coded)
    return TRUE;

  if (vop->coding_type == P_VOP)
    READ_UINT8 (&reader, vop->rounding_type, 1);

  READ_UINT8 (&reader, vop->intra_dc_vlc_thr, 3);

  if (vol->interlaced) {
    READ_UINT8 (&reader, vop->top_field_first, 1);
    READ_UINT8 (&reader, vop->alternate_vertical_scan_flag, 1);
  }

  READ_UINT16 (&reader, vop->quant, vol->quant_precision);

  if (vop->coding_type != I_VOP) {
    READ_UINT8 (&reader, vop->fcode_forward, 3);
    CHECK_ALLOWED (vop->fcode_forward, 1, 7);
  }

  if (vop->coding_type == B_VOP) {
    READ_UINT8 (&reader, vop->fcode_backward, 3);
    CHECK_ALLOWED (vop->fcode_backward, 1, 7);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Video Object Plane\"");
  return FALSE;

wrong_start_code:
  GST_WARNING ("got buffer with wrong start code");
  goto error;
}

gboolean
mpeg4_util_parse_GOV (GstBuffer * buf, Mpeg4GroupofVideoObjectPlane * gov)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);

  guint8 gov_start_code;

  /* start code prefix */
  SKIP (&reader, 24);

  READ_UINT8 (&reader, gov_start_code, 8);
  if (gov_start_code != MPEG4_PACKET_GOV)
    goto wrong_start_code;

  READ_UINT8 (&reader, gov->hours, 5);
  READ_UINT8 (&reader, gov->minutes, 6);
  /* marker bit */
  SKIP (&reader, 1);
  READ_UINT8 (&reader, gov->seconds, 6);

  READ_UINT8 (&reader, gov->closed, 1);
  READ_UINT8 (&reader, gov->broken_link, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Group of Video Object Plane\"");
  return FALSE;

wrong_start_code:
  GST_WARNING ("got buffer with wrong start code");
  goto error;
}

static void
mpeg4_util_par_from_info (guint8 aspect_ratio_info, guint8 * par_n,
    guint8 * par_d)
{
  switch (aspect_ratio_info) {
    case 0x02:
      *par_n = 12;
      *par_d = 11;
      break;
    case 0x03:
      *par_n = 10;
      *par_d = 11;
      break;
    case 0x04:
      *par_n = 16;
      *par_d = 11;
      break;
    case 0x05:
      *par_n = 40;
      *par_d = 33;
      break;

    case 0x01:
    default:
      *par_n = 1;
      *par_d = 1;
  }
}

static gboolean
mpeg4_util_parse_quant (GstBitReader * reader, guint8 quant_mat[64],
    const guint8 default_quant_mat[64])
{
  guint8 load_quant_mat;

  READ_UINT8 (reader, load_quant_mat, 1);
  if (load_quant_mat) {
    guint i;
    guint8 val;

    val = 1;
    for (i = 0; i < 64; i++) {

      if (val != 0)
        READ_UINT8 (reader, val, 8);

      if (val == 0) {
        if (i == 0)
          goto invalid_quant_mat;
        quant_mat[mpeg4_zigzag_8x8[i]] = quant_mat[mpeg4_zigzag_8x8[i - 1]];
      } else
        quant_mat[mpeg4_zigzag_8x8[i]] = val;
    }
  } else
    memcpy (quant_mat, default_quant_mat, 64);

  return TRUE;

error:
  GST_WARNING ("error parsing quant matrix");
  return FALSE;

invalid_quant_mat:
  GST_WARNING ("the first value should be non zero");
  goto error;
}

gboolean
mpeg4_util_parse_VOL (GstBuffer * buf, Mpeg4VisualObject * vo,
    Mpeg4VideoObjectLayer * vol)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);

  guint8 video_object_layer_start_code;
  guint8 aspect_ratio_info;
  guint8 control_parameters;
  guint8 not_8_bit;

  /* set default values */
  vol->verid = vo->verid;
  vol->priority = vo->priority;

  vol->low_delay = FALSE;
  vol->chroma_format = 1;
  vol->vbv_parameters = FALSE;
  vol->quant_precision = 5;
  vol->bits_per_pixel = 8;
  vol->quarter_sample = FALSE;

  /* start code prefix */
  SKIP (&reader, 24);

  READ_UINT8 (&reader, video_object_layer_start_code, 8);
  if (!(video_object_layer_start_code >= MPEG4_PACKET_VOL_MIN &&
          video_object_layer_start_code <= MPEG4_PACKET_VOL_MAX))
    goto wrong_start_code;

  READ_UINT8 (&reader, vol->random_accesible_vol, 1);
  READ_UINT8 (&reader, vol->video_object_type_indication, 8);

  READ_UINT8 (&reader, vol->is_object_layer_identifier, 1);
  if (vol->is_object_layer_identifier) {
    READ_UINT8 (&reader, vol->verid, 4);
    READ_UINT8 (&reader, vol->priority, 3);
  }

  READ_UINT8 (&reader, aspect_ratio_info, 4);
  if (aspect_ratio_info != 0xff)
    mpeg4_util_par_from_info (aspect_ratio_info, &vol->par_n, &vol->par_d);

  else {
    READ_UINT8 (&reader, vol->par_n, 8);
    CHECK_ALLOWED (vol->par_n, 1, 255);
    READ_UINT8 (&reader, vol->par_d, 8);
    CHECK_ALLOWED (vol->par_d, 1, 255);
  }

  READ_UINT8 (&reader, control_parameters, 1);
  if (control_parameters) {
    READ_UINT8 (&reader, vol->chroma_format, 2);
    READ_UINT8 (&reader, vol->low_delay, 1);

    READ_UINT8 (&reader, vol->vbv_parameters, 1);
    if (vol->vbv_parameters) {
      guint16 first_half, latter_half;
      guint8 latter_part;

      READ_UINT16 (&reader, first_half, 15);
      SKIP (&reader, 1);
      READ_UINT16 (&reader, latter_half, 15);
      SKIP (&reader, 1);
      vol->bit_rate = (first_half << 15) | latter_half;

      READ_UINT16 (&reader, first_half, 15);
      SKIP (&reader, 1);
      READ_UINT8 (&reader, latter_part, 3);
      SKIP (&reader, 1);
      vol->vbv_buffer_size = (first_half << 15) | latter_part;
    }
  }

  READ_UINT8 (&reader, vol->shape, 2);
  if (vol->shape != 0x0)
    goto invalid_shape;

  /* marker_bit */
  SKIP (&reader, 1);
  READ_UINT16 (&reader, vol->vop_time_increment_resolution, 16);
  CHECK_ALLOWED (vol->vop_time_increment_resolution, 1, G_MAXUINT16);
  vol->vop_time_increment_bits =
      g_bit_storage (vol->vop_time_increment_resolution);
  /* marker_bit */
  SKIP (&reader, 1);

  READ_UINT8 (&reader, vol->fixed_vop_rate, 1);
  if (vol->fixed_vop_rate)
    READ_UINT16 (&reader, vol->fixed_vop_time_increment,
        vol->vop_time_increment_bits);

  /* marker bit */
  SKIP (&reader, 1);
  READ_UINT16 (&reader, vol->width, 13);
  /* marker bit */
  SKIP (&reader, 1);
  READ_UINT16 (&reader, vol->height, 13);
  /* marker bit */
  SKIP (&reader, 1);

  READ_UINT8 (&reader, vol->interlaced, 1);
  READ_UINT8 (&reader, vol->obmc_disable, 1);

  if (vol->verid == 0x1) {
    READ_UINT8 (&reader, vol->sprite_enable, 1);
  } else
    READ_UINT8 (&reader, vol->sprite_enable, 2);

  if (vol->sprite_enable != 0x0)
    goto invalid_sprite_enable;

  READ_UINT8 (&reader, not_8_bit, 1);
  if (not_8_bit) {
    READ_UINT8 (&reader, vol->quant_precision, 4);
    CHECK_ALLOWED (vol->quant_precision, 3, 9);

    READ_UINT8 (&reader, vol->bits_per_pixel, 4);
    CHECK_ALLOWED (vol->bits_per_pixel, 4, 12);
  }


  READ_UINT8 (&reader, vol->quant_type, 1);
  if (vol->quant_type) {
    if (!mpeg4_util_parse_quant (&reader, vol->intra_quant_mat,
            default_intra_quant_mat))
      goto error;

    if (!mpeg4_util_parse_quant (&reader, vol->non_intra_quant_mat,
            default_non_intra_quant_mat))
      goto error;
  } else {
    memset (&vol->intra_quant_mat, 0, 64);
    memset (&vol->non_intra_quant_mat, 0, 64);
  }

  if (vol->verid != 0x1)
    READ_UINT8 (&reader, vol->quarter_sample, 1);

  READ_UINT8 (&reader, vol->complexity_estimation_disable, 1);
  if (!vol->complexity_estimation_disable)
    goto complexity_estimation_error;

  READ_UINT8 (&reader, vol->resync_marker_disable, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Video Object Layer\"");
  return FALSE;

wrong_start_code:
  GST_WARNING ("got buffer with wrong start code");
  goto error;

invalid_shape:
  GST_WARNING ("we only support rectangular shape");
  goto error;

invalid_sprite_enable:
  GST_WARNING ("we only support sprite_enable == 0");
  goto error;

complexity_estimation_error:
  GST_WARNING ("don't support complexity estimation");
  goto error;
}

gboolean
mpeg4_util_parse_VO (GstBuffer * buf, Mpeg4VisualObject * vo)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);

  guint8 visual_object_start_code;
  guint8 is_visual_object_identifier;

  /* set defualt values */
  vo->verid = 0x1;
  vo->priority = 1;

  /* start code prefix */
  SKIP (&reader, 24);

  READ_UINT8 (&reader, visual_object_start_code, 8);
  if (visual_object_start_code != MPEG4_PACKET_VO)
    goto wrong_start_code;

  READ_UINT8 (&reader, is_visual_object_identifier, 1);
  if (is_visual_object_identifier) {
    READ_UINT8 (&reader, vo->verid, 4);
    READ_UINT8 (&reader, vo->priority, 3);
  }

  READ_UINT8 (&reader, vo->type, 4);

  return TRUE;

wrong_start_code:
  GST_WARNING ("got buffer with wrong start code");
  return FALSE;

error:
  GST_WARNING ("error parsing \"Visual Object\"");
  return FALSE;
}

gboolean
mpeg4_util_parse_VOS (GstBuffer * buf, Mpeg4VisualObjectSequence * vos)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);

  guint8 visual_object_sequence_start_code;

  /* start code prefix */
  SKIP (&reader, 24);

  READ_UINT8 (&reader, visual_object_sequence_start_code, 8);
  if (visual_object_sequence_start_code != MPEG4_PACKET_VOS)
    goto wrong_start_code;

  READ_UINT8 (&reader, vos->profile_and_level_indication, 8);

  return TRUE;

wrong_start_code:
  GST_WARNING ("got buffer with wrong start code");
  return FALSE;

error:
  GST_WARNING ("error parsing \"Visual Object\"");
  return FALSE;
}
