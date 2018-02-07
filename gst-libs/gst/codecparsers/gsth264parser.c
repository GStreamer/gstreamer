/* Gstreamer
 * Copyright (C) <2011> Intel Corporation
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
 *
 * Some bits C-c,C-v'ed and s/4/3 from h264parse and videoparsers/h264parse.c:
 *    Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 *    Copyright (C) <2010> Collabora Multimedia
 *    Copyright (C) <2010> Nokia Corporation
 *
 *    (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *    (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:gsth264parser
 * @title: GstH264Parser
 * @short_description: Convenience library for h264 video
 * bitstream parsing.
 *
 * It offers bitstream parsing in both AVC (length-prefixed) and Annex B
 * (0x000001 start code prefix) format. To identify a NAL unit in a bitstream
 * and parse its headers, first call:
 *
 *   * #gst_h264_parser_identify_nalu to identify a NAL unit in an Annex B type bitstream
 *
 *   * #gst_h264_parser_identify_nalu_avc to identify a NAL unit in an AVC type bitstream
 *
 * The following functions are then available for parsing the structure of the
 * #GstH264NalUnit, depending on the #GstH264NalUnitType:
 *
 *   * From #GST_H264_NAL_SLICE to #GST_H264_NAL_SLICE_IDR: #gst_h264_parser_parse_slice_hdr
 *
 *   * #GST_H264_NAL_SEI: #gst_h264_parser_parse_sei
 *
 *   * #GST_H264_NAL_SPS: #gst_h264_parser_parse_sps
 *
 *   * #GST_H264_NAL_PPS: #gst_h264_parser_parse_pps
 *
 *   * Any other: #gst_h264_parser_parse_nal
 *
 * One of these functions *must* be called on every NAL unit in the bitstream,
 * in order to keep the internal structures of the #GstH264NalParser up to
 * date. It is legal to call #gst_h264_parser_parse_nal on NAL units of any
 * type, if no special parsing of the current NAL unit is required by the
 * application.
 *
 * For more details about the structures, look at the ITU-T H.264 and ISO/IEC 14496-10 â€“ MPEG-4
 * Part 10 specifications, available at:
 *
 *   * ITU-T H.264: http://www.itu.int/rec/T-REC-H.264
 *
 *   * ISO/IEC 14496-10: http://www.iso.org/iso/iso_catalogue/catalogue_tc/catalogue_detail.htm?csnumber=56538
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "nalutils.h"
#include "gsth264parser.h"

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (h264_parser_debug);
#define GST_CAT_DEFAULT h264_parser_debug

static gboolean initialized = FALSE;
#define INITIALIZE_DEBUG_CATEGORY \
  if (!initialized) { \
    GST_DEBUG_CATEGORY_INIT (h264_parser_debug, "codecparsers_h264", 0, \
        "h264 parser library"); \
    initialized = TRUE; \
  }

/**** Default scaling_lists according to Table 7-2 *****/
static const guint8 default_4x4_intra[16] = {
  6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32,
  32, 37, 37, 42
};

static const guint8 default_4x4_inter[16] = {
  10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27,
  27, 30, 30, 34
};

static const guint8 default_8x8_intra[64] = {
  6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18,
  18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27,
  27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31, 33,
  33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

static const guint8 default_8x8_inter[64] = {
  9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19,
  19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24,
  24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};

static const guint8 zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static const guint8 zigzag_4x4[16] = {
  0, 1, 4, 8,
  5, 2, 3, 6,
  9, 12, 13, 10,
  7, 11, 14, 15,
};

typedef struct
{
  guint par_n, par_d;
} PAR;

/* Table E-1 - Meaning of sample aspect ratio indicator (1..16) */
static const PAR aspect_ratios[17] = {
  {0, 0},
  {1, 1},
  {12, 11},
  {10, 11},
  {16, 11},
  {40, 33},
  {24, 11},
  {20, 11},
  {32, 11},
  {80, 33},
  {18, 11},
  {15, 11},
  {64, 33},
  {160, 99},
  {4, 3},
  {3, 2},
  {2, 1}
};

/*****  Utils ****/
#define EXTENDED_SAR 255

static GstH264SPS *
gst_h264_parser_get_sps (GstH264NalParser * nalparser, guint8 sps_id)
{
  GstH264SPS *sps;

  sps = &nalparser->sps[sps_id];

  if (sps->valid)
    return sps;

  return NULL;
}

static GstH264PPS *
gst_h264_parser_get_pps (GstH264NalParser * nalparser, guint8 pps_id)
{
  GstH264PPS *pps;

  pps = &nalparser->pps[pps_id];

  if (pps->valid)
    return pps;

  return NULL;
}

static gboolean
gst_h264_parse_nalu_header (GstH264NalUnit * nalu)
{
  guint8 *data = nalu->data + nalu->offset;
  guint8 svc_extension_flag;
  GstBitReader br;

  if (nalu->size < 1)
    return FALSE;

  nalu->type = (data[0] & 0x1f);
  nalu->ref_idc = (data[0] & 0x60) >> 5;
  nalu->idr_pic_flag = (nalu->type == 5 ? 1 : 0);
  nalu->header_bytes = 1;

  nalu->extension_type = GST_H264_NAL_EXTENSION_NONE;

  switch (nalu->type) {
    case GST_H264_NAL_PREFIX_UNIT:
    case GST_H264_NAL_SLICE_EXT:
      if (nalu->size < 4)
        return FALSE;
      gst_bit_reader_init (&br, nalu->data + nalu->offset + nalu->header_bytes,
          nalu->size - nalu->header_bytes);

      svc_extension_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
      if (svc_extension_flag) { /* SVC */

        nalu->extension_type = GST_H264_NAL_EXTENSION_SVC;

      } else {                  /* MVC */
        GstH264NalUnitExtensionMVC *const mvc = &nalu->extension.mvc;

        nalu->extension_type = GST_H264_NAL_EXTENSION_MVC;
        mvc->non_idr_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
        mvc->priority_id = gst_bit_reader_get_bits_uint8_unchecked (&br, 6);
        mvc->view_id = gst_bit_reader_get_bits_uint16_unchecked (&br, 10);
        mvc->temporal_id = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
        mvc->anchor_pic_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
        mvc->inter_view_flag = gst_bit_reader_get_bits_uint8_unchecked (&br, 1);

        /* Update IdrPicFlag (H.7.4.1.1) */
        nalu->idr_pic_flag = !mvc->non_idr_flag;
      }
      nalu->header_bytes += 3;
      break;
    default:
      break;
  }

  GST_DEBUG ("Nal type %u, ref_idc %u", nalu->type, nalu->ref_idc);
  return TRUE;
}

/*
 * gst_h264_pps_copy:
 * @dst_pps: The destination #GstH264PPS to copy into
 * @src_pps: The source #GstH264PPS to copy from
 *
 * Copies @src_pps into @dst_pps.
 *
 * Returns: %TRUE if everything went fine, %FALSE otherwise
 */
static gboolean
gst_h264_pps_copy (GstH264PPS * dst_pps, const GstH264PPS * src_pps)
{
  g_return_val_if_fail (dst_pps != NULL, FALSE);
  g_return_val_if_fail (src_pps != NULL, FALSE);

  gst_h264_pps_clear (dst_pps);

  *dst_pps = *src_pps;

  if (src_pps->slice_group_id)
    dst_pps->slice_group_id = g_memdup (src_pps->slice_group_id,
        src_pps->pic_size_in_map_units_minus1 + 1);

  return TRUE;
}

/* Copy MVC-specific data for subset SPS header */
static gboolean
gst_h264_sps_mvc_copy (GstH264SPS * dst_sps, const GstH264SPS * src_sps)
{
  GstH264SPSExtMVC *const dst_mvc = &dst_sps->extension.mvc;
  const GstH264SPSExtMVC *const src_mvc = &src_sps->extension.mvc;
  guint i, j, k;

  g_assert (dst_sps->extension_type == GST_H264_NAL_EXTENSION_MVC);

  dst_mvc->num_views_minus1 = src_mvc->num_views_minus1;
  dst_mvc->view = g_new0 (GstH264SPSExtMVCView, dst_mvc->num_views_minus1 + 1);
  if (!dst_mvc->view)
    return FALSE;

  dst_mvc->view[0].view_id = src_mvc->view[0].view_id;

  for (i = 1; i <= dst_mvc->num_views_minus1; i++) {
    GstH264SPSExtMVCView *const dst_view = &dst_mvc->view[i];
    const GstH264SPSExtMVCView *const src_view = &src_mvc->view[i];

    dst_view->view_id = src_view->view_id;

    dst_view->num_anchor_refs_l0 = src_view->num_anchor_refs_l0;
    for (j = 0; j < dst_view->num_anchor_refs_l0; j++)
      dst_view->anchor_ref_l0[j] = src_view->anchor_ref_l0[j];

    dst_view->num_anchor_refs_l1 = src_view->num_anchor_refs_l1;
    for (j = 0; j < dst_view->num_anchor_refs_l1; j++)
      dst_view->anchor_ref_l1[j] = src_view->anchor_ref_l1[j];

    dst_view->num_non_anchor_refs_l0 = src_view->num_non_anchor_refs_l0;
    for (j = 0; j < dst_view->num_non_anchor_refs_l0; j++)
      dst_view->non_anchor_ref_l0[j] = src_view->non_anchor_ref_l0[j];

    dst_view->num_non_anchor_refs_l1 = src_view->num_non_anchor_refs_l1;
    for (j = 0; j < dst_view->num_non_anchor_refs_l1; j++)
      dst_view->non_anchor_ref_l1[j] = src_view->non_anchor_ref_l1[j];
  }

  dst_mvc->num_level_values_signalled_minus1 =
      src_mvc->num_level_values_signalled_minus1;
  dst_mvc->level_value = g_new0 (GstH264SPSExtMVCLevelValue,
      dst_mvc->num_level_values_signalled_minus1 + 1);
  if (!dst_mvc->level_value)
    return FALSE;

  for (i = 0; i <= dst_mvc->num_level_values_signalled_minus1; i++) {
    GstH264SPSExtMVCLevelValue *const dst_value = &dst_mvc->level_value[i];
    const GstH264SPSExtMVCLevelValue *const src_value =
        &src_mvc->level_value[i];

    dst_value->level_idc = src_value->level_idc;

    dst_value->num_applicable_ops_minus1 = src_value->num_applicable_ops_minus1;
    dst_value->applicable_op = g_new0 (GstH264SPSExtMVCLevelValueOp,
        dst_value->num_applicable_ops_minus1 + 1);
    if (!dst_value->applicable_op)
      return FALSE;

    for (j = 0; j <= dst_value->num_applicable_ops_minus1; j++) {
      GstH264SPSExtMVCLevelValueOp *const dst_op = &dst_value->applicable_op[j];
      const GstH264SPSExtMVCLevelValueOp *const src_op =
          &src_value->applicable_op[j];

      dst_op->temporal_id = src_op->temporal_id;
      dst_op->num_target_views_minus1 = src_op->num_target_views_minus1;
      dst_op->target_view_id =
          g_new (guint16, dst_op->num_target_views_minus1 + 1);
      if (!dst_op->target_view_id)
        return FALSE;

      for (k = 0; k <= dst_op->num_target_views_minus1; k++)
        dst_op->target_view_id[k] = src_op->target_view_id[k];
      dst_op->num_views_minus1 = src_op->num_views_minus1;
    }
  }
  return TRUE;
}

/*
 * gst_h264_sps_copy:
 * @dst_sps: The destination #GstH264SPS to copy into
 * @src_sps: The source #GstH264SPS to copy from
 *
 * Copies @src_sps into @dst_sps.
 *
 * Returns: %TRUE if everything went fine, %FALSE otherwise
 */
static gboolean
gst_h264_sps_copy (GstH264SPS * dst_sps, const GstH264SPS * src_sps)
{
  g_return_val_if_fail (dst_sps != NULL, FALSE);
  g_return_val_if_fail (src_sps != NULL, FALSE);

  gst_h264_sps_clear (dst_sps);

  *dst_sps = *src_sps;

  switch (dst_sps->extension_type) {
    case GST_H264_NAL_EXTENSION_MVC:
      if (!gst_h264_sps_mvc_copy (dst_sps, src_sps))
        return FALSE;
      break;
  }
  return TRUE;
}

/****** Parsing functions *****/

static gboolean
gst_h264_parse_hrd_parameters (GstH264HRDParams * hrd, NalReader * nr)
{
  guint sched_sel_idx;

  GST_DEBUG ("parsing \"HRD Parameters\"");

  READ_UE_MAX (nr, hrd->cpb_cnt_minus1, 31);
  READ_UINT8 (nr, hrd->bit_rate_scale, 4);
  READ_UINT8 (nr, hrd->cpb_size_scale, 4);

  for (sched_sel_idx = 0; sched_sel_idx <= hrd->cpb_cnt_minus1; sched_sel_idx++) {
    READ_UE (nr, hrd->bit_rate_value_minus1[sched_sel_idx]);
    READ_UE (nr, hrd->cpb_size_value_minus1[sched_sel_idx]);
    READ_UINT8 (nr, hrd->cbr_flag[sched_sel_idx], 1);
  }

  READ_UINT8 (nr, hrd->initial_cpb_removal_delay_length_minus1, 5);
  READ_UINT8 (nr, hrd->cpb_removal_delay_length_minus1, 5);
  READ_UINT8 (nr, hrd->dpb_output_delay_length_minus1, 5);
  READ_UINT8 (nr, hrd->time_offset_length, 5);

  return TRUE;

error:
  GST_WARNING ("error parsing \"HRD Parameters\"");
  return FALSE;
}

static gboolean
gst_h264_parse_vui_parameters (GstH264SPS * sps, NalReader * nr)
{
  GstH264VUIParams *vui = &sps->vui_parameters;

  GST_DEBUG ("parsing \"VUI Parameters\"");

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  vui->video_format = 5;
  vui->colour_primaries = 2;
  vui->transfer_characteristics = 2;
  vui->matrix_coefficients = 2;

  READ_UINT8 (nr, vui->aspect_ratio_info_present_flag, 1);
  if (vui->aspect_ratio_info_present_flag) {
    READ_UINT8 (nr, vui->aspect_ratio_idc, 8);
    if (vui->aspect_ratio_idc == EXTENDED_SAR) {
      READ_UINT16 (nr, vui->sar_width, 16);
      READ_UINT16 (nr, vui->sar_height, 16);
      vui->par_n = vui->sar_width;
      vui->par_d = vui->sar_height;
    } else if (vui->aspect_ratio_idc <= 16) {
      vui->par_n = aspect_ratios[vui->aspect_ratio_idc].par_n;
      vui->par_d = aspect_ratios[vui->aspect_ratio_idc].par_d;
    }
  }

  READ_UINT8 (nr, vui->overscan_info_present_flag, 1);
  if (vui->overscan_info_present_flag)
    READ_UINT8 (nr, vui->overscan_appropriate_flag, 1);

  READ_UINT8 (nr, vui->video_signal_type_present_flag, 1);
  if (vui->video_signal_type_present_flag) {

    READ_UINT8 (nr, vui->video_format, 3);
    READ_UINT8 (nr, vui->video_full_range_flag, 1);
    READ_UINT8 (nr, vui->colour_description_present_flag, 1);
    if (vui->colour_description_present_flag) {
      READ_UINT8 (nr, vui->colour_primaries, 8);
      READ_UINT8 (nr, vui->transfer_characteristics, 8);
      READ_UINT8 (nr, vui->matrix_coefficients, 8);
    }
  }

  READ_UINT8 (nr, vui->chroma_loc_info_present_flag, 1);
  if (vui->chroma_loc_info_present_flag) {
    READ_UE_MAX (nr, vui->chroma_sample_loc_type_top_field, 5);
    READ_UE_MAX (nr, vui->chroma_sample_loc_type_bottom_field, 5);
  }

  READ_UINT8 (nr, vui->timing_info_present_flag, 1);
  if (vui->timing_info_present_flag) {
    READ_UINT32 (nr, vui->num_units_in_tick, 32);
    if (vui->num_units_in_tick == 0)
      GST_WARNING ("num_units_in_tick = 0 detected in stream "
          "(incompliant to H.264 E.2.1).");

    READ_UINT32 (nr, vui->time_scale, 32);
    if (vui->time_scale == 0)
      GST_WARNING ("time_scale = 0 detected in stream "
          "(incompliant to H.264 E.2.1).");

    READ_UINT8 (nr, vui->fixed_frame_rate_flag, 1);
  }

  READ_UINT8 (nr, vui->nal_hrd_parameters_present_flag, 1);
  if (vui->nal_hrd_parameters_present_flag) {
    if (!gst_h264_parse_hrd_parameters (&vui->nal_hrd_parameters, nr))
      goto error;
  }

  READ_UINT8 (nr, vui->vcl_hrd_parameters_present_flag, 1);
  if (vui->vcl_hrd_parameters_present_flag) {
    if (!gst_h264_parse_hrd_parameters (&vui->vcl_hrd_parameters, nr))
      goto error;
  }

  if (vui->nal_hrd_parameters_present_flag ||
      vui->vcl_hrd_parameters_present_flag)
    READ_UINT8 (nr, vui->low_delay_hrd_flag, 1);

  READ_UINT8 (nr, vui->pic_struct_present_flag, 1);
  READ_UINT8 (nr, vui->bitstream_restriction_flag, 1);
  if (vui->bitstream_restriction_flag) {
    READ_UINT8 (nr, vui->motion_vectors_over_pic_boundaries_flag, 1);
    READ_UE (nr, vui->max_bytes_per_pic_denom);
    READ_UE_MAX (nr, vui->max_bits_per_mb_denom, 16);
    READ_UE_MAX (nr, vui->log2_max_mv_length_horizontal, 16);
    READ_UE_MAX (nr, vui->log2_max_mv_length_vertical, 16);
    READ_UE (nr, vui->num_reorder_frames);
    READ_UE (nr, vui->max_dec_frame_buffering);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"VUI Parameters\"");
  return FALSE;
}

static gboolean
gst_h264_parser_parse_scaling_list (NalReader * nr,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    const guint8 fallback_4x4_inter[16], const guint8 fallback_4x4_intra[16],
    const guint8 fallback_8x8_inter[64], const guint8 fallback_8x8_intra[64],
    guint8 n_lists)
{
  guint i;

  static const guint8 *default_lists[12] = {
    default_4x4_intra, default_4x4_intra, default_4x4_intra,
    default_4x4_inter, default_4x4_inter, default_4x4_inter,
    default_8x8_intra, default_8x8_inter,
    default_8x8_intra, default_8x8_inter,
    default_8x8_intra, default_8x8_inter
  };

  GST_DEBUG ("parsing scaling lists");

  for (i = 0; i < 12; i++) {
    gboolean use_default = FALSE;

    if (i < n_lists) {
      guint8 scaling_list_present_flag;

      READ_UINT8 (nr, scaling_list_present_flag, 1);
      if (scaling_list_present_flag) {
        guint8 *scaling_list;
        guint size;
        guint j;
        guint8 last_scale, next_scale;

        if (i < 6) {
          scaling_list = scaling_lists_4x4[i];
          size = 16;
        } else {
          scaling_list = scaling_lists_8x8[i - 6];
          size = 64;
        }

        last_scale = 8;
        next_scale = 8;
        for (j = 0; j < size; j++) {
          if (next_scale != 0) {
            gint32 delta_scale;

            READ_SE (nr, delta_scale);
            next_scale = (last_scale + delta_scale) & 0xff;
          }
          if (j == 0 && next_scale == 0) {
            /* Use default scaling lists (7.4.2.1.1.1) */
            memcpy (scaling_list, default_lists[i], size);
            break;
          }
          last_scale = scaling_list[j] =
              (next_scale == 0) ? last_scale : next_scale;
        }
      } else
        use_default = TRUE;
    } else
      use_default = TRUE;

    if (use_default) {
      switch (i) {
        case 0:
          memcpy (scaling_lists_4x4[0], fallback_4x4_intra, 16);
          break;
        case 1:
          memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
          break;
        case 2:
          memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
          break;
        case 3:
          memcpy (scaling_lists_4x4[3], fallback_4x4_inter, 16);
          break;
        case 4:
          memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
          break;
        case 5:
          memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
          break;
        case 6:
          memcpy (scaling_lists_8x8[0], fallback_8x8_intra, 64);
          break;
        case 7:
          memcpy (scaling_lists_8x8[1], fallback_8x8_inter, 64);
          break;
        case 8:
          memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
          break;
        case 9:
          memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
          break;
        case 10:
          memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
          break;
        case 11:
          memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
          break;

        default:
          break;
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing scaling lists");
  return FALSE;
}

static gboolean
slice_parse_ref_pic_list_modification_1 (GstH264SliceHdr * slice,
    NalReader * nr, guint list, gboolean is_mvc)
{
  GstH264RefPicListModification *entries;
  guint8 *ref_pic_list_modification_flag, *n_ref_pic_list_modification;
  guint32 modification_of_pic_nums_idc;
  gsize max_entries;
  guint i = 0;

  if (list == 0) {
    entries = slice->ref_pic_list_modification_l0;
    max_entries = G_N_ELEMENTS (slice->ref_pic_list_modification_l0);
    ref_pic_list_modification_flag = &slice->ref_pic_list_modification_flag_l0;
    n_ref_pic_list_modification = &slice->n_ref_pic_list_modification_l0;
  } else {
    entries = slice->ref_pic_list_modification_l1;
    max_entries = G_N_ELEMENTS (slice->ref_pic_list_modification_l1);
    ref_pic_list_modification_flag = &slice->ref_pic_list_modification_flag_l1;
    n_ref_pic_list_modification = &slice->n_ref_pic_list_modification_l1;
  }

  READ_UINT8 (nr, *ref_pic_list_modification_flag, 1);
  if (*ref_pic_list_modification_flag) {
    while (1) {
      READ_UE (nr, modification_of_pic_nums_idc);
      if (modification_of_pic_nums_idc == 0 ||
          modification_of_pic_nums_idc == 1) {
        READ_UE_MAX (nr, entries[i].value.abs_diff_pic_num_minus1,
            slice->max_pic_num - 1);
      } else if (modification_of_pic_nums_idc == 2) {
        READ_UE (nr, entries[i].value.long_term_pic_num);
      } else if (is_mvc && (modification_of_pic_nums_idc == 4 ||
              modification_of_pic_nums_idc == 5)) {
        READ_UE (nr, entries[i].value.abs_diff_view_idx_minus1);
      }
      entries[i++].modification_of_pic_nums_idc = modification_of_pic_nums_idc;
      if (modification_of_pic_nums_idc == 3)
        break;
      if (i >= max_entries)
        goto error;
    }
  }
  *n_ref_pic_list_modification = i;
  return TRUE;

error:
  GST_WARNING ("error parsing \"Reference picture list %u modification\"",
      list);
  return FALSE;
}

static gboolean
slice_parse_ref_pic_list_modification (GstH264SliceHdr * slice, NalReader * nr,
    gboolean is_mvc)
{
  if (!GST_H264_IS_I_SLICE (slice) && !GST_H264_IS_SI_SLICE (slice)) {
    if (!slice_parse_ref_pic_list_modification_1 (slice, nr, 0, is_mvc))
      return FALSE;
  }

  if (GST_H264_IS_B_SLICE (slice)) {
    if (!slice_parse_ref_pic_list_modification_1 (slice, nr, 1, is_mvc))
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_h264_slice_parse_dec_ref_pic_marking (GstH264SliceHdr * slice,
    GstH264NalUnit * nalu, NalReader * nr)
{
  GstH264DecRefPicMarking *dec_ref_pic_m;

  GST_DEBUG ("parsing \"Decoded reference picture marking\"");

  dec_ref_pic_m = &slice->dec_ref_pic_marking;

  if (nalu->idr_pic_flag) {
    READ_UINT8 (nr, dec_ref_pic_m->no_output_of_prior_pics_flag, 1);
    READ_UINT8 (nr, dec_ref_pic_m->long_term_reference_flag, 1);
  } else {
    READ_UINT8 (nr, dec_ref_pic_m->adaptive_ref_pic_marking_mode_flag, 1);
    if (dec_ref_pic_m->adaptive_ref_pic_marking_mode_flag) {
      guint32 mem_mgmt_ctrl_op;
      GstH264RefPicMarking *refpicmarking;

      dec_ref_pic_m->n_ref_pic_marking = 0;
      while (1) {
        refpicmarking =
            &dec_ref_pic_m->ref_pic_marking[dec_ref_pic_m->n_ref_pic_marking];

        READ_UE (nr, mem_mgmt_ctrl_op);
        if (mem_mgmt_ctrl_op == 0)
          break;

        refpicmarking->memory_management_control_operation = mem_mgmt_ctrl_op;

        if (mem_mgmt_ctrl_op == 1 || mem_mgmt_ctrl_op == 3)
          READ_UE (nr, refpicmarking->difference_of_pic_nums_minus1);

        if (mem_mgmt_ctrl_op == 2)
          READ_UE (nr, refpicmarking->long_term_pic_num);

        if (mem_mgmt_ctrl_op == 3 || mem_mgmt_ctrl_op == 6)
          READ_UE (nr, refpicmarking->long_term_frame_idx);

        if (mem_mgmt_ctrl_op == 4)
          READ_UE (nr, refpicmarking->max_long_term_frame_idx_plus1);

        dec_ref_pic_m->n_ref_pic_marking++;
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Decoded reference picture marking\"");
  return FALSE;
}

static gboolean
gst_h264_slice_parse_pred_weight_table (GstH264SliceHdr * slice,
    NalReader * nr, guint8 chroma_array_type)
{
  GstH264PredWeightTable *p;
  gint16 default_luma_weight, default_chroma_weight;
  gint i;

  GST_DEBUG ("parsing \"Prediction weight table\"");

  p = &slice->pred_weight_table;

  READ_UE_MAX (nr, p->luma_log2_weight_denom, 7);
  /* set default values */
  default_luma_weight = 1 << p->luma_log2_weight_denom;
  for (i = 0; i < G_N_ELEMENTS (p->luma_weight_l0); i++)
    p->luma_weight_l0[i] = default_luma_weight;
  if (GST_H264_IS_B_SLICE (slice)) {
    for (i = 0; i < G_N_ELEMENTS (p->luma_weight_l1); i++)
      p->luma_weight_l1[i] = default_luma_weight;
  }

  if (chroma_array_type != 0) {
    READ_UE_MAX (nr, p->chroma_log2_weight_denom, 7);
    /* set default values */
    default_chroma_weight = 1 << p->chroma_log2_weight_denom;
    for (i = 0; i < G_N_ELEMENTS (p->chroma_weight_l0); i++) {
      p->chroma_weight_l0[i][0] = default_chroma_weight;
      p->chroma_weight_l0[i][1] = default_chroma_weight;
    }
    if (GST_H264_IS_B_SLICE (slice)) {
      for (i = 0; i < G_N_ELEMENTS (p->chroma_weight_l1); i++) {
        p->chroma_weight_l1[i][0] = default_chroma_weight;
        p->chroma_weight_l1[i][1] = default_chroma_weight;
      }
    }
  }

  for (i = 0; i <= slice->num_ref_idx_l0_active_minus1; i++) {
    guint8 luma_weight_l0_flag;

    READ_UINT8 (nr, luma_weight_l0_flag, 1);
    if (luma_weight_l0_flag) {
      READ_SE_ALLOWED (nr, p->luma_weight_l0[i], -128, 127);
      READ_SE_ALLOWED (nr, p->luma_offset_l0[i], -128, 127);
    }
    if (chroma_array_type != 0) {
      guint8 chroma_weight_l0_flag;
      gint j;

      READ_UINT8 (nr, chroma_weight_l0_flag, 1);
      if (chroma_weight_l0_flag) {
        for (j = 0; j < 2; j++) {
          READ_SE_ALLOWED (nr, p->chroma_weight_l0[i][j], -128, 127);
          READ_SE_ALLOWED (nr, p->chroma_offset_l0[i][j], -128, 127);
        }
      }
    }
  }

  if (GST_H264_IS_B_SLICE (slice)) {
    for (i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++) {
      guint8 luma_weight_l1_flag;

      READ_UINT8 (nr, luma_weight_l1_flag, 1);
      if (luma_weight_l1_flag) {
        READ_SE_ALLOWED (nr, p->luma_weight_l1[i], -128, 127);
        READ_SE_ALLOWED (nr, p->luma_offset_l1[i], -128, 127);
      }
      if (chroma_array_type != 0) {
        guint8 chroma_weight_l1_flag;
        gint j;

        READ_UINT8 (nr, chroma_weight_l1_flag, 1);
        if (chroma_weight_l1_flag) {
          for (j = 0; j < 2; j++) {
            READ_SE_ALLOWED (nr, p->chroma_weight_l1[i][j], -128, 127);
            READ_SE_ALLOWED (nr, p->chroma_offset_l1[i][j], -128, 127);
          }
        }
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Prediction weight table\"");
  return FALSE;
}

static GstH264ParserResult
gst_h264_parser_parse_buffering_period (GstH264NalParser * nalparser,
    GstH264BufferingPeriod * per, NalReader * nr)
{
  GstH264SPS *sps;
  guint8 sps_id;

  GST_DEBUG ("parsing \"Buffering period\"");

  READ_UE_MAX (nr, sps_id, GST_H264_MAX_SPS_COUNT - 1);
  sps = gst_h264_parser_get_sps (nalparser, sps_id);
  if (!sps) {
    GST_WARNING ("couldn't find associated sequence parameter set with id: %d",
        sps_id);
    return GST_H264_PARSER_BROKEN_LINK;
  }
  per->sps = sps;

  if (sps->vui_parameters_present_flag) {
    GstH264VUIParams *vui = &sps->vui_parameters;

    if (vui->nal_hrd_parameters_present_flag) {
      GstH264HRDParams *hrd = &vui->nal_hrd_parameters;
      const guint8 nbits = hrd->initial_cpb_removal_delay_length_minus1 + 1;
      guint8 sched_sel_idx;

      for (sched_sel_idx = 0; sched_sel_idx <= hrd->cpb_cnt_minus1;
          sched_sel_idx++) {
        READ_UINT32 (nr, per->nal_initial_cpb_removal_delay[sched_sel_idx],
            nbits);
        READ_UINT32 (nr,
            per->nal_initial_cpb_removal_delay_offset[sched_sel_idx], nbits);
      }
    }

    if (vui->vcl_hrd_parameters_present_flag) {
      GstH264HRDParams *hrd = &vui->vcl_hrd_parameters;
      const guint8 nbits = hrd->initial_cpb_removal_delay_length_minus1 + 1;
      guint8 sched_sel_idx;

      for (sched_sel_idx = 0; sched_sel_idx <= hrd->cpb_cnt_minus1;
          sched_sel_idx++) {
        READ_UINT32 (nr, per->vcl_initial_cpb_removal_delay[sched_sel_idx],
            nbits);
        READ_UINT32 (nr,
            per->vcl_initial_cpb_removal_delay_offset[sched_sel_idx], nbits);
      }
    }
  }

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Buffering period\"");
  return GST_H264_PARSER_ERROR;
}

static gboolean
gst_h264_parse_clock_timestamp (GstH264ClockTimestamp * tim,
    GstH264VUIParams * vui, NalReader * nr)
{
  guint8 full_timestamp_flag;
  guint8 time_offset_length;

  GST_DEBUG ("parsing \"Clock timestamp\"");

  /* defalt values */
  tim->time_offset = 0;

  READ_UINT8 (nr, tim->ct_type, 2);
  READ_UINT8 (nr, tim->nuit_field_based_flag, 1);
  READ_UINT8 (nr, tim->counting_type, 5);
  READ_UINT8 (nr, full_timestamp_flag, 1);
  READ_UINT8 (nr, tim->discontinuity_flag, 1);
  READ_UINT8 (nr, tim->cnt_dropped_flag, 1);
  READ_UINT8 (nr, tim->n_frames, 8);

  if (full_timestamp_flag) {
    tim->seconds_flag = TRUE;
    READ_UINT8 (nr, tim->seconds_value, 6);

    tim->minutes_flag = TRUE;
    READ_UINT8 (nr, tim->minutes_value, 6);

    tim->hours_flag = TRUE;
    READ_UINT8 (nr, tim->hours_value, 5);
  } else {
    READ_UINT8 (nr, tim->seconds_flag, 1);
    if (tim->seconds_flag) {
      READ_UINT8 (nr, tim->seconds_value, 6);
      READ_UINT8 (nr, tim->minutes_flag, 1);
      if (tim->minutes_flag) {
        READ_UINT8 (nr, tim->minutes_value, 6);
        READ_UINT8 (nr, tim->hours_flag, 1);
        if (tim->hours_flag)
          READ_UINT8 (nr, tim->hours_value, 5);
      }
    }
  }

  time_offset_length = 24;
  if (vui->nal_hrd_parameters_present_flag)
    time_offset_length = vui->nal_hrd_parameters.time_offset_length;
  else if (vui->vcl_hrd_parameters_present_flag)
    time_offset_length = vui->vcl_hrd_parameters.time_offset_length;

  if (time_offset_length > 0)
    READ_UINT32 (nr, tim->time_offset, time_offset_length);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Clock timestamp\"");
  return FALSE;
}

static GstH264ParserResult
gst_h264_parser_parse_pic_timing (GstH264NalParser * nalparser,
    GstH264PicTiming * tim, NalReader * nr)
{
  GST_DEBUG ("parsing \"Picture timing\"");
  if (!nalparser->last_sps || !nalparser->last_sps->valid) {
    GST_WARNING ("didn't get the associated sequence paramater set for the "
        "current access unit");
    goto error;
  }

  if (nalparser->last_sps->vui_parameters_present_flag) {
    GstH264VUIParams *vui = &nalparser->last_sps->vui_parameters;

    if (vui->nal_hrd_parameters_present_flag) {
      READ_UINT32 (nr, tim->cpb_removal_delay,
          vui->nal_hrd_parameters.cpb_removal_delay_length_minus1 + 1);
      READ_UINT32 (nr, tim->dpb_output_delay,
          vui->nal_hrd_parameters.dpb_output_delay_length_minus1 + 1);
    } else if (vui->vcl_hrd_parameters_present_flag) {
      READ_UINT32 (nr, tim->cpb_removal_delay,
          vui->vcl_hrd_parameters.cpb_removal_delay_length_minus1 + 1);
      READ_UINT32 (nr, tim->dpb_output_delay,
          vui->vcl_hrd_parameters.dpb_output_delay_length_minus1 + 1);
    }

    if (vui->pic_struct_present_flag) {
      const guint8 num_clock_ts_table[9] = {
        1, 1, 1, 2, 2, 3, 3, 2, 3
      };
      guint8 num_clock_num_ts;
      guint i;

      tim->pic_struct_present_flag = TRUE;
      READ_UINT8 (nr, tim->pic_struct, 4);
      CHECK_ALLOWED ((gint8) tim->pic_struct, 0, 8);

      num_clock_num_ts = num_clock_ts_table[tim->pic_struct];
      for (i = 0; i < num_clock_num_ts; i++) {
        READ_UINT8 (nr, tim->clock_timestamp_flag[i], 1);
        if (tim->clock_timestamp_flag[i]) {
          if (!gst_h264_parse_clock_timestamp (&tim->clock_timestamp[i], vui,
                  nr))
            goto error;
        }
      }
    }
  }

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Picture timing\"");
  return GST_H264_PARSER_ERROR;
}

static GstH264ParserResult
gst_h264_parser_parse_recovery_point (GstH264NalParser * nalparser,
    GstH264RecoveryPoint * rp, NalReader * nr)
{
  GstH264SPS *const sps = nalparser->last_sps;

  GST_DEBUG ("parsing \"Recovery point\"");
  if (!sps || !sps->valid) {
    GST_WARNING ("didn't get the associated sequence paramater set for the "
        "current access unit");
    goto error;
  }

  READ_UE_MAX (nr, rp->recovery_frame_cnt, sps->max_frame_num - 1);
  READ_UINT8 (nr, rp->exact_match_flag, 1);
  READ_UINT8 (nr, rp->broken_link_flag, 1);
  READ_UINT8 (nr, rp->changing_slice_group_idc, 2);

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Recovery point\"");
  return GST_H264_PARSER_ERROR;
}

/* Parse SEI stereo_video_info() message */
static GstH264ParserResult
gst_h264_parser_parse_stereo_video_info (GstH264NalParser * nalparser,
    GstH264StereoVideoInfo * info, NalReader * nr)
{
  GST_DEBUG ("parsing \"Stereo Video info\"");

  READ_UINT8 (nr, info->field_views_flag, 1);
  if (info->field_views_flag) {
    READ_UINT8 (nr, info->top_field_is_left_view_flag, 1);
  } else {
    READ_UINT8 (nr, info->current_frame_is_left_view_flag, 1);
    READ_UINT8 (nr, info->next_frame_is_second_view_flag, 1);
  }
  READ_UINT8 (nr, info->left_view_self_contained_flag, 1);
  READ_UINT8 (nr, info->right_view_self_contained_flag, 1);

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Stereo Video info\"");
  return GST_H264_PARSER_ERROR;
}

/* Parse SEI frame_packing_arrangement() message */
static GstH264ParserResult
gst_h264_parser_parse_frame_packing (GstH264NalParser * nalparser,
    GstH264FramePacking * frame_packing, NalReader * nr, guint payload_size)
{
  guint8 frame_packing_extension_flag;
  guint start_pos;

  GST_DEBUG ("parsing \"Frame Packing Arrangement\"");

  start_pos = nal_reader_get_pos (nr);
  READ_UE (nr, frame_packing->frame_packing_id);
  READ_UINT8 (nr, frame_packing->frame_packing_cancel_flag, 1);

  if (!frame_packing->frame_packing_cancel_flag) {
    READ_UINT8 (nr, frame_packing->frame_packing_type, 7);
    READ_UINT8 (nr, frame_packing->quincunx_sampling_flag, 1);
    READ_UINT8 (nr, frame_packing->content_interpretation_type, 6);
    READ_UINT8 (nr, frame_packing->spatial_flipping_flag, 1);
    READ_UINT8 (nr, frame_packing->frame0_flipped_flag, 1);
    READ_UINT8 (nr, frame_packing->field_views_flag, 1);
    READ_UINT8 (nr, frame_packing->current_frame_is_frame0_flag, 1);
    READ_UINT8 (nr, frame_packing->frame0_self_contained_flag, 1);
    READ_UINT8 (nr, frame_packing->frame1_self_contained_flag, 1);

    if (!frame_packing->quincunx_sampling_flag &&
        frame_packing->frame_packing_type !=
        GST_H264_FRAME_PACKING_TEMPORAL_INTERLEAVING) {
      READ_UINT8 (nr, frame_packing->frame0_grid_position_x, 4);
      READ_UINT8 (nr, frame_packing->frame0_grid_position_y, 4);
      READ_UINT8 (nr, frame_packing->frame1_grid_position_x, 4);
      READ_UINT8 (nr, frame_packing->frame1_grid_position_y, 4);
    }

    /* Skip frame_packing_arrangement_reserved_byte */
    if (!nal_reader_skip (nr, 8))
      goto error;

    READ_UE_MAX (nr, frame_packing->frame_packing_repetition_period, 16384);
  }

  READ_UINT8 (nr, frame_packing_extension_flag, 1);

  /* All data that follows within a frame packing arrangement SEI message
     after the value 1 for frame_packing_arrangement_extension_flag shall
     be ignored (D.2.25) */
  if (frame_packing_extension_flag) {
    nal_reader_skip_long (nr,
        payload_size - (nal_reader_get_pos (nr) - start_pos));
  }

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Frame Packing Arrangement\"");
  return GST_H264_PARSER_ERROR;
}

static GstH264ParserResult
gst_h264_parser_parse_sei_message (GstH264NalParser * nalparser,
    NalReader * nr, GstH264SEIMessage * sei)
{
  guint32 payloadSize;
  guint8 payload_type_byte, payload_size_byte;
  guint remaining, payload_size;
  GstH264ParserResult res;

  GST_DEBUG ("parsing \"Sei message\"");

  memset (sei, 0, sizeof (*sei));

  do {
    READ_UINT8 (nr, payload_type_byte, 8);
    sei->payloadType += payload_type_byte;
  } while (payload_type_byte == 0xff);

  payloadSize = 0;
  do {
    READ_UINT8 (nr, payload_size_byte, 8);
    payloadSize += payload_size_byte;
  }
  while (payload_size_byte == 0xff);

  remaining = nal_reader_get_remaining (nr);
  payload_size = payloadSize * 8 < remaining ? payloadSize * 8 : remaining;

  GST_DEBUG ("SEI message received: payloadType  %u, payloadSize = %u bits",
      sei->payloadType, payload_size);

  switch (sei->payloadType) {
    case GST_H264_SEI_BUF_PERIOD:
      /* size not set; might depend on emulation_prevention_three_byte */
      res = gst_h264_parser_parse_buffering_period (nalparser,
          &sei->payload.buffering_period, nr);
      break;
    case GST_H264_SEI_PIC_TIMING:
      /* size not set; might depend on emulation_prevention_three_byte */
      res = gst_h264_parser_parse_pic_timing (nalparser,
          &sei->payload.pic_timing, nr);
      break;
    case GST_H264_SEI_RECOVERY_POINT:
      res = gst_h264_parser_parse_recovery_point (nalparser,
          &sei->payload.recovery_point, nr);
      break;
    case GST_H264_SEI_STEREO_VIDEO_INFO:
      res = gst_h264_parser_parse_stereo_video_info (nalparser,
          &sei->payload.stereo_video_info, nr);
      break;
    case GST_H264_SEI_FRAME_PACKING:
      res = gst_h264_parser_parse_frame_packing (nalparser,
          &sei->payload.frame_packing, nr, payload_size);
      break;
    default:
      /* Just consume payloadSize bytes, which does not account for
         emulation prevention bytes */
      if (!nal_reader_skip_long (nr, payload_size))
        goto error;
      res = GST_H264_PARSER_OK;
      break;
  }

  /* When SEI message doesn't end at byte boundary,
   * check remaining bits fit the specification.
   */
  if (!nal_reader_is_byte_aligned (nr)) {
    guint8 bit_equal_to_one;
    READ_UINT8 (nr, bit_equal_to_one, 1);
    if (!bit_equal_to_one)
      GST_WARNING ("Bit non equal to one.");

    while (!nal_reader_is_byte_aligned (nr)) {
      guint8 bit_equal_to_zero;
      READ_UINT8 (nr, bit_equal_to_zero, 1);
      if (bit_equal_to_zero)
        GST_WARNING ("Bit non equal to zero.");
    }
  }

  return res;

error:
  GST_WARNING ("error parsing \"Sei message\"");
  return GST_H264_PARSER_ERROR;
}

/******** API *************/

/**
 * gst_h264_nal_parser_new:
 *
 * Creates a new #GstH264NalParser. It should be freed with
 * gst_h264_nal_parser_free after use.
 *
 * Returns: a new #GstH264NalParser
 */
GstH264NalParser *
gst_h264_nal_parser_new (void)
{
  GstH264NalParser *nalparser;

  nalparser = g_slice_new0 (GstH264NalParser);
  INITIALIZE_DEBUG_CATEGORY;

  return nalparser;
}

/**
 * gst_h264_nal_parser_free:
 * @nalparser: the #GstH264NalParser to free
 *
 * Frees @nalparser and sets it to %NULL
 */
void
gst_h264_nal_parser_free (GstH264NalParser * nalparser)
{
  guint i;

  for (i = 0; i < GST_H264_MAX_SPS_COUNT; i++)
    gst_h264_sps_clear (&nalparser->sps[i]);
  for (i = 0; i < GST_H264_MAX_PPS_COUNT; i++)
    gst_h264_pps_clear (&nalparser->pps[i]);
  g_slice_free (GstH264NalParser, nalparser);

  nalparser = NULL;
}

/**
 * gst_h264_parser_identify_nalu_unchecked:
 * @nalparser: a #GstH264NalParser
 * @data: The data to parse
 * @offset: the offset from which to parse @data
 * @size: the size of @data
 * @nalu: The #GstH264NalUnit where to store parsed nal headers
 *
 * Parses @data and fills @nalu from the next nalu data from @data.
 *
 * This differs from @gst_h264_parser_identify_nalu in that it doesn't
 * check whether the packet is complete or not.
 *
 * Note: Only use this function if you already know the provided @data
 * is a complete NALU, else use @gst_h264_parser_identify_nalu.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_identify_nalu_unchecked (GstH264NalParser * nalparser,
    const guint8 * data, guint offset, gsize size, GstH264NalUnit * nalu)
{
  gint off1;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + 4) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return GST_H264_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data + offset, size - offset);

  if (off1 < 0) {
    GST_DEBUG ("No start code prefix in this buffer");
    return GST_H264_PARSER_NO_NAL;
  }

  if (offset + off1 == size - 1) {
    GST_DEBUG ("Missing data to identify nal unit");

    return GST_H264_PARSER_ERROR;
  }

  nalu->sc_offset = offset + off1;


  nalu->offset = offset + off1 + 3;
  nalu->data = (guint8 *) data;
  nalu->size = size - nalu->offset;

  if (!gst_h264_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return GST_H264_PARSER_BROKEN_DATA;
  }

  nalu->valid = TRUE;

  /* sc might have 2 or 3 0-bytes */
  if (nalu->sc_offset > 0 && data[nalu->sc_offset - 1] == 00
      && (nalu->type == GST_H264_NAL_SPS || nalu->type == GST_H264_NAL_PPS
          || nalu->type == GST_H264_NAL_AU_DELIMITER))
    nalu->sc_offset--;

  if (nalu->type == GST_H264_NAL_SEQ_END ||
      nalu->type == GST_H264_NAL_STREAM_END) {
    GST_DEBUG ("end-of-seq or end-of-stream nal found");
    nalu->size = 1;
    return GST_H264_PARSER_OK;
  }

  return GST_H264_PARSER_OK;
}

/**
 * gst_h264_parser_identify_nalu:
 * @nalparser: a #GstH264NalParser
 * @data: The data to parse, containing an Annex B coded NAL unit
 * @offset: the offset in @data from which to parse the NAL unit
 * @size: the size of @data
 * @nalu: The #GstH264NalUnit to store the identified NAL unit in
 *
 * Parses the headers of an Annex B coded NAL unit from @data and puts the
 * result into @nalu.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_identify_nalu (GstH264NalParser * nalparser,
    const guint8 * data, guint offset, gsize size, GstH264NalUnit * nalu)
{
  GstH264ParserResult res;
  gint off2;

  res =
      gst_h264_parser_identify_nalu_unchecked (nalparser, data, offset, size,
      nalu);

  if (res != GST_H264_PARSER_OK || nalu->size == 1)
    goto beach;

  off2 = scan_for_start_codes (data + nalu->offset, size - nalu->offset);
  if (off2 < 0) {
    GST_DEBUG ("Nal start %d, No end found", nalu->offset);

    return GST_H264_PARSER_NO_NAL_END;
  }

  /* Mini performance improvement:
   * We could have a way to store how many 0s were skipped to avoid
   * parsing them again on the next NAL */
  while (off2 > 0 && data[nalu->offset + off2 - 1] == 00)
    off2--;

  nalu->size = off2;
  if (nalu->size < 2)
    return GST_H264_PARSER_BROKEN_DATA;

  GST_DEBUG ("Complete nal found. Off: %d, Size: %d", nalu->offset, nalu->size);

beach:
  return res;
}


/**
 * gst_h264_parser_identify_nalu_avc:
 * @nalparser: a #GstH264NalParser
 * @data: The data to parse, containing an AVC coded NAL unit
 * @offset: the offset in @data from which to parse the NAL unit
 * @size: the size of @data
 * @nal_length_size: the size in bytes of the AVC nal length prefix.
 * @nalu: The #GstH264NalUnit to store the identified NAL unit in
 *
 * Parses the headers of an AVC coded NAL unit from @data and puts the result
 * into @nalu.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_identify_nalu_avc (GstH264NalParser * nalparser,
    const guint8 * data, guint offset, gsize size, guint8 nal_length_size,
    GstH264NalUnit * nalu)
{
  GstBitReader br;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + nal_length_size) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return GST_H264_PARSER_ERROR;
  }

  size = size - offset;
  gst_bit_reader_init (&br, data + offset, size);

  nalu->size = gst_bit_reader_get_bits_uint32_unchecked (&br,
      nal_length_size * 8);
  nalu->sc_offset = offset;
  nalu->offset = offset + nal_length_size;

  if (size < nalu->size + nal_length_size) {
    nalu->size = 0;

    return GST_H264_PARSER_NO_NAL_END;
  }

  nalu->data = (guint8 *) data;

  if (!gst_h264_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return GST_H264_PARSER_BROKEN_DATA;
  }

  nalu->valid = TRUE;

  return GST_H264_PARSER_OK;
}

/**
 * gst_h264_parser_parse_nal:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GstH264NalUnit to parse
 *
 * This function should be called in the case one doesn't need to
 * parse a specific structure. It is necessary to do so to make
 * sure @nalparser is up to date.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_parse_nal (GstH264NalParser * nalparser, GstH264NalUnit * nalu)
{
  GstH264SPS sps;
  GstH264PPS pps;

  switch (nalu->type) {
    case GST_H264_NAL_SPS:
      return gst_h264_parser_parse_sps (nalparser, nalu, &sps, FALSE);
      break;
    case GST_H264_NAL_PPS:
      return gst_h264_parser_parse_pps (nalparser, nalu, &pps);
  }

  return GST_H264_PARSER_OK;
}

/**
 * gst_h264_parser_parse_sps:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_SPS #GstH264NalUnit to parse
 * @sps: The #GstH264SPS to fill.
 * @parse_vui_params: Whether to parse the vui_params or not
 *
 * Parses @nalu containing a Sequence Parameter Set, and fills @sps.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_parse_sps (GstH264NalParser * nalparser, GstH264NalUnit * nalu,
    GstH264SPS * sps, gboolean parse_vui_params)
{
  GstH264ParserResult res = gst_h264_parse_sps (nalu, sps, parse_vui_params);

  if (res == GST_H264_PARSER_OK) {
    GST_DEBUG ("adding sequence parameter set with id: %d to array", sps->id);

    if (!gst_h264_sps_copy (&nalparser->sps[sps->id], sps))
      return GST_H264_PARSER_ERROR;
    nalparser->last_sps = &nalparser->sps[sps->id];
  }
  return res;
}

/* Parse seq_parameter_set_data() */
static gboolean
gst_h264_parse_sps_data (NalReader * nr, GstH264SPS * sps,
    gboolean parse_vui_params)
{
  gint width, height;
  guint subwc[] = { 1, 2, 2, 1 };
  guint subhc[] = { 1, 2, 1, 1 };

  memset (sps, 0, sizeof (*sps));

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  sps->extension_type = GST_H264_NAL_EXTENSION_NONE;
  sps->chroma_format_idc = 1;
  memset (sps->scaling_lists_4x4, 16, 96);
  memset (sps->scaling_lists_8x8, 16, 384);

  READ_UINT8 (nr, sps->profile_idc, 8);
  READ_UINT8 (nr, sps->constraint_set0_flag, 1);
  READ_UINT8 (nr, sps->constraint_set1_flag, 1);
  READ_UINT8 (nr, sps->constraint_set2_flag, 1);
  READ_UINT8 (nr, sps->constraint_set3_flag, 1);
  READ_UINT8 (nr, sps->constraint_set4_flag, 1);
  READ_UINT8 (nr, sps->constraint_set5_flag, 1);

  /* skip reserved_zero_2bits */
  if (!nal_reader_skip (nr, 2))
    goto error;

  READ_UINT8 (nr, sps->level_idc, 8);

  READ_UE_MAX (nr, sps->id, GST_H264_MAX_SPS_COUNT - 1);

  if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
      sps->profile_idc == 122 || sps->profile_idc == 244 ||
      sps->profile_idc == 44 || sps->profile_idc == 83 ||
      sps->profile_idc == 86 || sps->profile_idc == 118 ||
      sps->profile_idc == 128) {
    READ_UE_MAX (nr, sps->chroma_format_idc, 3);
    if (sps->chroma_format_idc == 3)
      READ_UINT8 (nr, sps->separate_colour_plane_flag, 1);

    READ_UE_MAX (nr, sps->bit_depth_luma_minus8, 6);
    READ_UE_MAX (nr, sps->bit_depth_chroma_minus8, 6);
    READ_UINT8 (nr, sps->qpprime_y_zero_transform_bypass_flag, 1);

    READ_UINT8 (nr, sps->scaling_matrix_present_flag, 1);
    if (sps->scaling_matrix_present_flag) {
      guint8 n_lists;

      n_lists = (sps->chroma_format_idc != 3) ? 8 : 12;
      if (!gst_h264_parser_parse_scaling_list (nr,
              sps->scaling_lists_4x4, sps->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_UE_MAX (nr, sps->log2_max_frame_num_minus4, 12);

  sps->max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);

  READ_UE_MAX (nr, sps->pic_order_cnt_type, 2);
  if (sps->pic_order_cnt_type == 0) {
    READ_UE_MAX (nr, sps->log2_max_pic_order_cnt_lsb_minus4, 12);
  } else if (sps->pic_order_cnt_type == 1) {
    guint i;

    READ_UINT8 (nr, sps->delta_pic_order_always_zero_flag, 1);
    READ_SE (nr, sps->offset_for_non_ref_pic);
    READ_SE (nr, sps->offset_for_top_to_bottom_field);
    READ_UE_MAX (nr, sps->num_ref_frames_in_pic_order_cnt_cycle, 255);

    for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
      READ_SE (nr, sps->offset_for_ref_frame[i]);
  }

  READ_UE (nr, sps->num_ref_frames);
  READ_UINT8 (nr, sps->gaps_in_frame_num_value_allowed_flag, 1);
  READ_UE (nr, sps->pic_width_in_mbs_minus1);
  READ_UE (nr, sps->pic_height_in_map_units_minus1);
  READ_UINT8 (nr, sps->frame_mbs_only_flag, 1);

  if (!sps->frame_mbs_only_flag)
    READ_UINT8 (nr, sps->mb_adaptive_frame_field_flag, 1);

  READ_UINT8 (nr, sps->direct_8x8_inference_flag, 1);
  READ_UINT8 (nr, sps->frame_cropping_flag, 1);
  if (sps->frame_cropping_flag) {
    READ_UE (nr, sps->frame_crop_left_offset);
    READ_UE (nr, sps->frame_crop_right_offset);
    READ_UE (nr, sps->frame_crop_top_offset);
    READ_UE (nr, sps->frame_crop_bottom_offset);
  }

  READ_UINT8 (nr, sps->vui_parameters_present_flag, 1);
  if (sps->vui_parameters_present_flag && parse_vui_params) {
    if (!gst_h264_parse_vui_parameters (sps, nr))
      goto error;
  }

  /* calculate ChromaArrayType */
  if (!sps->separate_colour_plane_flag)
    sps->chroma_array_type = sps->chroma_format_idc;

  /* Calculate  width and height */
  width = (sps->pic_width_in_mbs_minus1 + 1);
  width *= 16;
  height = (sps->pic_height_in_map_units_minus1 + 1);
  height *= 16 * (2 - sps->frame_mbs_only_flag);
  GST_LOG ("initial width=%d, height=%d", width, height);
  if (width < 0 || height < 0) {
    GST_WARNING ("invalid width/height in SPS");
    goto error;
  }

  sps->width = width;
  sps->height = height;

  if (sps->frame_cropping_flag) {
    const guint crop_unit_x = subwc[sps->chroma_format_idc];
    const guint crop_unit_y =
        subhc[sps->chroma_format_idc] * (2 - sps->frame_mbs_only_flag);

    width -= (sps->frame_crop_left_offset + sps->frame_crop_right_offset)
        * crop_unit_x;
    height -= (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset)
        * crop_unit_y;

    sps->crop_rect_width = width;
    sps->crop_rect_height = height;
    sps->crop_rect_x = sps->frame_crop_left_offset * crop_unit_x;
    sps->crop_rect_y = sps->frame_crop_top_offset * crop_unit_y;

    GST_LOG ("crop_rectangle x=%u y=%u width=%u, height=%u", sps->crop_rect_x,
        sps->crop_rect_y, width, height);
  }

  sps->fps_num_removed = 0;
  sps->fps_den_removed = 1;

  return TRUE;

error:
  return FALSE;
}

/* Parse subset_seq_parameter_set() data for MVC */
static gboolean
gst_h264_parse_sps_mvc_data (NalReader * nr, GstH264SPS * sps,
    gboolean parse_vui_params)
{
  GstH264SPSExtMVC *const mvc = &sps->extension.mvc;
  guint8 bit_equal_to_one;
  guint i, j, k;

  READ_UINT8 (nr, bit_equal_to_one, 1);
  if (!bit_equal_to_one)
    return FALSE;

  sps->extension_type = GST_H264_NAL_EXTENSION_MVC;

  READ_UE_MAX (nr, mvc->num_views_minus1, GST_H264_MAX_VIEW_COUNT - 1);

  mvc->view = g_new0 (GstH264SPSExtMVCView, mvc->num_views_minus1 + 1);
  if (!mvc->view)
    goto error_allocation_failed;

  for (i = 0; i <= mvc->num_views_minus1; i++)
    READ_UE_MAX (nr, mvc->view[i].view_id, GST_H264_MAX_VIEW_ID);

  for (i = 1; i <= mvc->num_views_minus1; i++) {
    /* for RefPicList0 */
    READ_UE_MAX (nr, mvc->view[i].num_anchor_refs_l0, 15);
    for (j = 0; j < mvc->view[i].num_anchor_refs_l0; j++) {
      READ_UE_MAX (nr, mvc->view[i].anchor_ref_l0[j], GST_H264_MAX_VIEW_ID);
    }

    /* for RefPicList1 */
    READ_UE_MAX (nr, mvc->view[i].num_anchor_refs_l1, 15);
    for (j = 0; j < mvc->view[i].num_anchor_refs_l1; j++) {
      READ_UE_MAX (nr, mvc->view[i].anchor_ref_l1[j], GST_H264_MAX_VIEW_ID);
    }
  }

  for (i = 1; i <= mvc->num_views_minus1; i++) {
    /* for RefPicList0 */
    READ_UE_MAX (nr, mvc->view[i].num_non_anchor_refs_l0, 15);
    for (j = 0; j < mvc->view[i].num_non_anchor_refs_l0; j++) {
      READ_UE_MAX (nr, mvc->view[i].non_anchor_ref_l0[j], GST_H264_MAX_VIEW_ID);
    }

    /* for RefPicList1 */
    READ_UE_MAX (nr, mvc->view[i].num_non_anchor_refs_l1, 15);
    for (j = 0; j < mvc->view[i].num_non_anchor_refs_l1; j++) {
      READ_UE_MAX (nr, mvc->view[i].non_anchor_ref_l1[j], GST_H264_MAX_VIEW_ID);
    }
  }

  READ_UE_MAX (nr, mvc->num_level_values_signalled_minus1, 63);

  mvc->level_value =
      g_new0 (GstH264SPSExtMVCLevelValue,
      mvc->num_level_values_signalled_minus1 + 1);
  if (!mvc->level_value)
    goto error_allocation_failed;

  for (i = 0; i <= mvc->num_level_values_signalled_minus1; i++) {
    GstH264SPSExtMVCLevelValue *const level_value = &mvc->level_value[i];

    READ_UINT8 (nr, level_value->level_idc, 8);

    READ_UE_MAX (nr, level_value->num_applicable_ops_minus1, 1023);
    level_value->applicable_op =
        g_new0 (GstH264SPSExtMVCLevelValueOp,
        level_value->num_applicable_ops_minus1 + 1);
    if (!level_value->applicable_op)
      goto error_allocation_failed;

    for (j = 0; j <= level_value->num_applicable_ops_minus1; j++) {
      GstH264SPSExtMVCLevelValueOp *const op = &level_value->applicable_op[j];

      READ_UINT8 (nr, op->temporal_id, 3);

      READ_UE_MAX (nr, op->num_target_views_minus1, 1023);
      op->target_view_id = g_new (guint16, op->num_target_views_minus1 + 1);
      if (!op->target_view_id)
        goto error_allocation_failed;

      for (k = 0; k <= op->num_target_views_minus1; k++)
        READ_UE_MAX (nr, op->target_view_id[k], GST_H264_MAX_VIEW_ID);
      READ_UE_MAX (nr, op->num_views_minus1, 1023);
    }
  }
  return TRUE;

error_allocation_failed:
  GST_WARNING ("failed to allocate memory");
  gst_h264_sps_clear (sps);
  return FALSE;

error:
  gst_h264_sps_clear (sps);
  return FALSE;
}

/**
 * gst_h264_parse_sps:
 * @nalu: The #GST_H264_NAL_SPS #GstH264NalUnit to parse
 * @sps: The #GstH264SPS to fill.
 * @parse_vui_params: Whether to parse the vui_params or not
 *
 * Parses @data, and fills the @sps structure.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parse_sps (GstH264NalUnit * nalu, GstH264SPS * sps,
    gboolean parse_vui_params)
{
  NalReader nr;

  INITIALIZE_DEBUG_CATEGORY;
  GST_DEBUG ("parsing SPS");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  if (!gst_h264_parse_sps_data (&nr, sps, parse_vui_params))
    goto error;

  sps->valid = TRUE;

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Sequence parameter set\"");
  sps->valid = FALSE;
  return GST_H264_PARSER_ERROR;
}

/**
 * gst_h264_parser_parse_subset_sps:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_SUBSET_SPS #GstH264NalUnit to parse
 * @sps: The #GstH264SPS to fill.
 * @parse_vui_params: Whether to parse the vui_params or not
 *
 * Parses @data, and fills in the @sps structure.
 *
 * This function fully parses @data and allocates all the necessary
 * data structures needed for MVC extensions. The resulting @sps
 * structure shall be deallocated with gst_h264_sps_clear() when it is
 * no longer needed.
 *
 * Note: if the caller doesn't need any of the MVC-specific data, then
 * gst_h264_parser_parse_sps() is more efficient because those extra
 * syntax elements are not parsed and no extra memory is allocated.
 *
 * Returns: a #GstH264ParserResult
 *
 * Since: 1.6
 */
GstH264ParserResult
gst_h264_parser_parse_subset_sps (GstH264NalParser * nalparser,
    GstH264NalUnit * nalu, GstH264SPS * sps, gboolean parse_vui_params)
{
  GstH264ParserResult res;

  res = gst_h264_parse_subset_sps (nalu, sps, parse_vui_params);
  if (res == GST_H264_PARSER_OK) {
    GST_DEBUG ("adding sequence parameter set with id: %d to array", sps->id);

    if (!gst_h264_sps_copy (&nalparser->sps[sps->id], sps)) {
      gst_h264_sps_clear (sps);
      return GST_H264_PARSER_ERROR;
    }
    nalparser->last_sps = &nalparser->sps[sps->id];
  }
  return res;
}

/**
 * gst_h264_parse_subset_sps:
 * @nalu: The #GST_H264_NAL_SUBSET_SPS #GstH264NalUnit to parse
 * @sps: The #GstH264SPS to fill.
 * @parse_vui_params: Whether to parse the vui_params or not
 *
 * Parses @data, and fills in the @sps structure.
 *
 * This function fully parses @data and allocates all the necessary
 * data structures needed for MVC extensions. The resulting @sps
 * structure shall be deallocated with gst_h264_sps_clear() when it is
 * no longer needed.
 *
 * Note: if the caller doesn't need any of the MVC-specific data, then
 * gst_h264_parser_parse_sps() is more efficient because those extra
 * syntax elements are not parsed and no extra memory is allocated.
 *
 * Returns: a #GstH264ParserResult
 *
 * Since: 1.6
 */
GstH264ParserResult
gst_h264_parse_subset_sps (GstH264NalUnit * nalu, GstH264SPS * sps,
    gboolean parse_vui_params)
{
  NalReader nr;

  INITIALIZE_DEBUG_CATEGORY;
  GST_DEBUG ("parsing Subset SPS");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  if (!gst_h264_parse_sps_data (&nr, sps, TRUE))
    goto error;

  if (sps->profile_idc == GST_H264_PROFILE_MULTIVIEW_HIGH ||
      sps->profile_idc == GST_H264_PROFILE_STEREO_HIGH) {
    if (!gst_h264_parse_sps_mvc_data (&nr, sps, parse_vui_params))
      goto error;
  }

  sps->valid = TRUE;
  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Subset sequence parameter set\"");
  gst_h264_sps_clear (sps);
  sps->valid = FALSE;
  return GST_H264_PARSER_ERROR;
}

/**
 * gst_h264_parse_pps:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_PPS #GstH264NalUnit to parse
 * @pps: The #GstH264PPS to fill.
 *
 * Parses @data, and fills the @pps structure.
 *
 * The resulting @pps data structure shall be deallocated with the
 * gst_h264_pps_clear() function when it is no longer needed, or prior
 * to parsing a new PPS NAL unit.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parse_pps (GstH264NalParser * nalparser, GstH264NalUnit * nalu,
    GstH264PPS * pps)
{
  NalReader nr;
  GstH264SPS *sps;
  gint sps_id;
  guint8 pic_scaling_matrix_present_flag;
  gint qp_bd_offset;

  INITIALIZE_DEBUG_CATEGORY;
  GST_DEBUG ("parsing PPS");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  memset (pps, 0, sizeof (*pps));

  READ_UE_MAX (&nr, pps->id, GST_H264_MAX_PPS_COUNT - 1);
  READ_UE_MAX (&nr, sps_id, GST_H264_MAX_SPS_COUNT - 1);

  sps = gst_h264_parser_get_sps (nalparser, sps_id);
  if (!sps) {
    GST_WARNING ("couldn't find associated sequence parameter set with id: %d",
        sps_id);
    return GST_H264_PARSER_BROKEN_LINK;
  }
  pps->sequence = sps;
  qp_bd_offset = 6 * (sps->bit_depth_luma_minus8 +
      sps->separate_colour_plane_flag);

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  memcpy (&pps->scaling_lists_4x4, &sps->scaling_lists_4x4, 96);
  memcpy (&pps->scaling_lists_8x8, &sps->scaling_lists_8x8, 384);

  READ_UINT8 (&nr, pps->entropy_coding_mode_flag, 1);
  READ_UINT8 (&nr, pps->pic_order_present_flag, 1);
  READ_UE_MAX (&nr, pps->num_slice_groups_minus1, 7);
  if (pps->num_slice_groups_minus1 > 0) {
    READ_UE_MAX (&nr, pps->slice_group_map_type, 6);

    if (pps->slice_group_map_type == 0) {
      gint i;

      for (i = 0; i <= pps->num_slice_groups_minus1; i++)
        READ_UE (&nr, pps->run_length_minus1[i]);
    } else if (pps->slice_group_map_type == 2) {
      gint i;

      for (i = 0; i < pps->num_slice_groups_minus1; i++) {
        READ_UE (&nr, pps->top_left[i]);
        READ_UE (&nr, pps->bottom_right[i]);
      }
    } else if (pps->slice_group_map_type >= 3 && pps->slice_group_map_type <= 5) {
      READ_UINT8 (&nr, pps->slice_group_change_direction_flag, 1);
      READ_UE (&nr, pps->slice_group_change_rate_minus1);
    } else if (pps->slice_group_map_type == 6) {
      gint bits;
      gint i;

      READ_UE (&nr, pps->pic_size_in_map_units_minus1);
      bits = g_bit_storage (pps->num_slice_groups_minus1);

      pps->slice_group_id =
          g_new (guint8, pps->pic_size_in_map_units_minus1 + 1);
      for (i = 0; i <= pps->pic_size_in_map_units_minus1; i++)
        READ_UINT8 (&nr, pps->slice_group_id[i], bits);
    }
  }

  READ_UE_MAX (&nr, pps->num_ref_idx_l0_active_minus1, 31);
  READ_UE_MAX (&nr, pps->num_ref_idx_l1_active_minus1, 31);
  READ_UINT8 (&nr, pps->weighted_pred_flag, 1);
  READ_UINT8 (&nr, pps->weighted_bipred_idc, 2);
  READ_SE_ALLOWED (&nr, pps->pic_init_qp_minus26, -(26 + qp_bd_offset), 25);
  READ_SE_ALLOWED (&nr, pps->pic_init_qs_minus26, -26, 25);
  READ_SE_ALLOWED (&nr, pps->chroma_qp_index_offset, -12, 12);
  pps->second_chroma_qp_index_offset = pps->chroma_qp_index_offset;
  READ_UINT8 (&nr, pps->deblocking_filter_control_present_flag, 1);
  READ_UINT8 (&nr, pps->constrained_intra_pred_flag, 1);
  READ_UINT8 (&nr, pps->redundant_pic_cnt_present_flag, 1);

  if (!nal_reader_has_more_data (&nr))
    goto done;

  READ_UINT8 (&nr, pps->transform_8x8_mode_flag, 1);

  READ_UINT8 (&nr, pic_scaling_matrix_present_flag, 1);
  if (pic_scaling_matrix_present_flag) {
    guint8 n_lists;

    n_lists = 6 + ((sps->chroma_format_idc != 3) ? 2 : 6) *
        pps->transform_8x8_mode_flag;

    if (sps->scaling_matrix_present_flag) {
      if (!gst_h264_parser_parse_scaling_list (&nr,
              pps->scaling_lists_4x4, pps->scaling_lists_8x8,
              sps->scaling_lists_4x4[3], sps->scaling_lists_4x4[0],
              sps->scaling_lists_8x8[3], sps->scaling_lists_8x8[0], n_lists))
        goto error;
    } else {
      if (!gst_h264_parser_parse_scaling_list (&nr,
              pps->scaling_lists_4x4, pps->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_SE_ALLOWED (&nr, pps->second_chroma_qp_index_offset, -12, 12);

done:
  pps->valid = TRUE;
  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Picture parameter set\"");
  pps->valid = FALSE;
  gst_h264_pps_clear (pps);
  return GST_H264_PARSER_ERROR;
}

/**
 * gst_h264_parser_parse_pps:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_PPS #GstH264NalUnit to parse
 * @pps: The #GstH264PPS to fill.
 *
 * Parses @nalu containing a Picture Parameter Set, and fills @pps.
 *
 * The resulting @pps data structure must be deallocated by the caller using
 * gst_h264_pps_clear().
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_parse_pps (GstH264NalParser * nalparser,
    GstH264NalUnit * nalu, GstH264PPS * pps)
{
  GstH264ParserResult res = gst_h264_parse_pps (nalparser, nalu, pps);

  if (res == GST_H264_PARSER_OK) {
    GST_DEBUG ("adding picture parameter set with id: %d to array", pps->id);

    if (!gst_h264_pps_copy (&nalparser->pps[pps->id], pps))
      return GST_H264_PARSER_ERROR;
    nalparser->last_pps = &nalparser->pps[pps->id];
  }

  return res;
}

/**
 * gst_h264_pps_clear:
 * @pps: The #GstH264PPS to free
 *
 * Clears all @pps internal resources.
 *
 * Since: 1.4
 */
void
gst_h264_pps_clear (GstH264PPS * pps)
{
  g_return_if_fail (pps != NULL);

  g_free (pps->slice_group_id);
  pps->slice_group_id = NULL;
}

/**
 * gst_h264_parser_parse_slice_hdr:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_SLICE to #GST_H264_NAL_SLICE_IDR #GstH264NalUnit to parse
 * @slice: The #GstH264SliceHdr to fill.
 * @parse_pred_weight_table: Whether to parse the pred_weight_table or not
 * @parse_dec_ref_pic_marking: Whether to parse the dec_ref_pic_marking or not
 *
 * Parses @nalu containing a coded slice, and fills @slice.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_parse_slice_hdr (GstH264NalParser * nalparser,
    GstH264NalUnit * nalu, GstH264SliceHdr * slice,
    gboolean parse_pred_weight_table, gboolean parse_dec_ref_pic_marking)
{
  NalReader nr;
  gint pps_id;
  GstH264PPS *pps;
  GstH264SPS *sps;

  memset (slice, 0, sizeof (*slice));

  if (!nalu->size) {
    GST_DEBUG ("Invalid Nal Unit");
    return GST_H264_PARSER_ERROR;
  }

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  READ_UE (&nr, slice->first_mb_in_slice);
  READ_UE (&nr, slice->type);

  GST_DEBUG ("parsing \"Slice header\", slice type %u", slice->type);

  READ_UE_MAX (&nr, pps_id, GST_H264_MAX_PPS_COUNT - 1);
  pps = gst_h264_parser_get_pps (nalparser, pps_id);

  if (!pps) {
    GST_WARNING ("couldn't find associated picture parameter set with id: %d",
        pps_id);

    return GST_H264_PARSER_BROKEN_LINK;
  }

  slice->pps = pps;
  sps = pps->sequence;
  if (!sps) {
    GST_WARNING ("couldn't find associated sequence parameter set with id: %d",
        pps->id);
    return GST_H264_PARSER_BROKEN_LINK;
  }

  /* Check we can actually parse this slice (AVC, MVC headers only) */
  if (sps->extension_type && sps->extension_type != GST_H264_NAL_EXTENSION_MVC) {
    GST_WARNING ("failed to parse unsupported slice header");
    return GST_H264_PARSER_BROKEN_DATA;
  }

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  slice->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1;
  slice->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1;

  if (sps->separate_colour_plane_flag)
    READ_UINT8 (&nr, slice->colour_plane_id, 2);

  READ_UINT16 (&nr, slice->frame_num, sps->log2_max_frame_num_minus4 + 4);

  if (!sps->frame_mbs_only_flag) {
    READ_UINT8 (&nr, slice->field_pic_flag, 1);
    if (slice->field_pic_flag)
      READ_UINT8 (&nr, slice->bottom_field_flag, 1);
  }

  /* calculate MaxPicNum */
  if (slice->field_pic_flag)
    slice->max_pic_num = 2 * sps->max_frame_num;
  else
    slice->max_pic_num = sps->max_frame_num;

  if (nalu->idr_pic_flag)
    READ_UE_MAX (&nr, slice->idr_pic_id, G_MAXUINT16);

  if (sps->pic_order_cnt_type == 0) {
    READ_UINT16 (&nr, slice->pic_order_cnt_lsb,
        sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

    if (pps->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&nr, slice->delta_pic_order_cnt_bottom);
  }

  if (sps->pic_order_cnt_type == 1 && !sps->delta_pic_order_always_zero_flag) {
    READ_SE (&nr, slice->delta_pic_order_cnt[0]);
    if (pps->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&nr, slice->delta_pic_order_cnt[1]);
  }

  if (pps->redundant_pic_cnt_present_flag)
    READ_UE_MAX (&nr, slice->redundant_pic_cnt, G_MAXINT8);

  if (GST_H264_IS_B_SLICE (slice))
    READ_UINT8 (&nr, slice->direct_spatial_mv_pred_flag, 1);

  if (GST_H264_IS_P_SLICE (slice) || GST_H264_IS_SP_SLICE (slice) ||
      GST_H264_IS_B_SLICE (slice)) {
    guint8 num_ref_idx_active_override_flag;

    READ_UINT8 (&nr, num_ref_idx_active_override_flag, 1);
    if (num_ref_idx_active_override_flag) {
      READ_UE_MAX (&nr, slice->num_ref_idx_l0_active_minus1, 31);

      if (GST_H264_IS_B_SLICE (slice))
        READ_UE_MAX (&nr, slice->num_ref_idx_l1_active_minus1, 31);
    }
  }

  if (!slice_parse_ref_pic_list_modification (slice, &nr,
          GST_H264_IS_MVC_NALU (nalu)))
    goto error;

  if ((pps->weighted_pred_flag && (GST_H264_IS_P_SLICE (slice)
              || GST_H264_IS_SP_SLICE (slice)))
      || (pps->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE (slice))) {
    if (!gst_h264_slice_parse_pred_weight_table (slice, &nr,
            sps->chroma_array_type))
      goto error;
  }

  if (nalu->ref_idc != 0) {
    if (!gst_h264_slice_parse_dec_ref_pic_marking (slice, nalu, &nr))
      goto error;
  }

  if (pps->entropy_coding_mode_flag && !GST_H264_IS_I_SLICE (slice) &&
      !GST_H264_IS_SI_SLICE (slice))
    READ_UE_MAX (&nr, slice->cabac_init_idc, 2);

  READ_SE_ALLOWED (&nr, slice->slice_qp_delta, -87, 77);

  if (GST_H264_IS_SP_SLICE (slice) || GST_H264_IS_SI_SLICE (slice)) {
    guint8 sp_for_switch_flag;

    if (GST_H264_IS_SP_SLICE (slice))
      READ_UINT8 (&nr, sp_for_switch_flag, 1);
    READ_SE_ALLOWED (&nr, slice->slice_qs_delta, -51, 51);
  }

  if (pps->deblocking_filter_control_present_flag) {
    READ_UE_MAX (&nr, slice->disable_deblocking_filter_idc, 2);
    if (slice->disable_deblocking_filter_idc != 1) {
      READ_SE_ALLOWED (&nr, slice->slice_alpha_c0_offset_div2, -6, 6);
      READ_SE_ALLOWED (&nr, slice->slice_beta_offset_div2, -6, 6);
    }
  }

  if (pps->num_slice_groups_minus1 > 0 &&
      pps->slice_group_map_type >= 3 && pps->slice_group_map_type <= 5) {
    /* Ceil(Log2(PicSizeInMapUnits / SliceGroupChangeRate + 1))  [7-33] */
    guint32 PicWidthInMbs = sps->pic_width_in_mbs_minus1 + 1;
    guint32 PicHeightInMapUnits = sps->pic_height_in_map_units_minus1 + 1;
    guint32 PicSizeInMapUnits = PicWidthInMbs * PicHeightInMapUnits;
    guint32 SliceGroupChangeRate = pps->slice_group_change_rate_minus1 + 1;
    const guint n = ceil_log2 (PicSizeInMapUnits / SliceGroupChangeRate + 1);
    READ_UINT16 (&nr, slice->slice_group_change_cycle, n);
  }

  slice->header_size = nal_reader_get_pos (&nr);
  slice->n_emulation_prevention_bytes = nal_reader_get_epb_count (&nr);

  return GST_H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Slice header\"");
  return GST_H264_PARSER_ERROR;
}

/* Free MVC-specific data from subset SPS header */
static void
gst_h264_sps_mvc_clear (GstH264SPS * sps)
{
  GstH264SPSExtMVC *const mvc = &sps->extension.mvc;
  guint i, j;

  g_assert (sps->extension_type == GST_H264_NAL_EXTENSION_MVC);

  g_free (mvc->view);
  mvc->view = NULL;

  for (i = 0; i <= mvc->num_level_values_signalled_minus1; i++) {
    GstH264SPSExtMVCLevelValue *const level_value = &mvc->level_value[i];

    for (j = 0; j <= level_value->num_applicable_ops_minus1; j++) {
      g_free (level_value->applicable_op[j].target_view_id);
      level_value->applicable_op[j].target_view_id = NULL;
    }
    g_free (level_value->applicable_op);
    level_value->applicable_op = NULL;
  }
  g_free (mvc->level_value);
  mvc->level_value = NULL;

  /* All meaningful MVC info are now gone, just pretend to be a
   * standard AVC struct now */
  sps->extension_type = GST_H264_NAL_EXTENSION_NONE;
}

/**
 * gst_h264_sps_clear:
 * @sps: The #GstH264SPS to free
 *
 * Clears all @sps internal resources.
 *
 * Since: 1.6
 */
void
gst_h264_sps_clear (GstH264SPS * sps)
{
  g_return_if_fail (sps != NULL);

  switch (sps->extension_type) {
    case GST_H264_NAL_EXTENSION_MVC:
      gst_h264_sps_mvc_clear (sps);
      break;
  }
}

/**
 * gst_h264_parser_parse_sei:
 * @nalparser: a #GstH264NalParser
 * @nalu: The #GST_H264_NAL_SEI #GstH264NalUnit to parse
 * @messages: The GArray of #GstH264SEIMessage to fill. The caller must free it when done.
 *
 * Parses @nalu containing one or more Supplementary Enhancement Information messages,
 * and allocates and fills the @messages array.
 *
 * Returns: a #GstH264ParserResult
 */
GstH264ParserResult
gst_h264_parser_parse_sei (GstH264NalParser * nalparser, GstH264NalUnit * nalu,
    GArray ** messages)
{
  NalReader nr;
  GstH264SEIMessage sei;
  GstH264ParserResult res;

  GST_DEBUG ("parsing SEI nal");
  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);
  *messages = g_array_new (FALSE, FALSE, sizeof (GstH264SEIMessage));

  do {
    res = gst_h264_parser_parse_sei_message (nalparser, &nr, &sei);
    if (res == GST_H264_PARSER_OK)
      g_array_append_val (*messages, sei);
    else
      break;
  } while (nal_reader_has_more_data (&nr));

  return res;
}

/**
 * gst_h264_quant_matrix_8x8_get_zigzag_from_raster:
 * @out_quant: (out): The resulting quantization matrix
 * @quant: The source quantization matrix
 *
 * Converts quantization matrix @quant from raster scan order to
 * zigzag scan order and store the resulting factors into @out_quant.
 *
 * Note: it is an error to pass the same table in both @quant and
 * @out_quant arguments.
 *
 * Since: 1.4
 */
void
gst_h264_quant_matrix_8x8_get_zigzag_from_raster (guint8 out_quant[64],
    const guint8 quant[64])
{
  guint i;

  g_return_if_fail (out_quant != quant);

  for (i = 0; i < 64; i++)
    out_quant[i] = quant[zigzag_8x8[i]];
}

/**
 * gst_h264_quant_matrix_8x8_get_raster_from_zigzag:
 * @out_quant: (out): The resulting quantization matrix
 * @quant: The source quantization matrix
 *
 * Converts quantization matrix @quant from zigzag scan order to
 * raster scan order and store the resulting factors into @out_quant.
 *
 * Note: it is an error to pass the same table in both @quant and
 * @out_quant arguments.
 *
 * Since: 1.4
 */
void
gst_h264_quant_matrix_8x8_get_raster_from_zigzag (guint8 out_quant[64],
    const guint8 quant[64])
{
  guint i;

  g_return_if_fail (out_quant != quant);

  for (i = 0; i < 64; i++)
    out_quant[zigzag_8x8[i]] = quant[i];
}

/**
 * gst_h264_quant_matrix_4x4_get_zigzag_from_raster:
 * @out_quant: (out): The resulting quantization matrix
 * @quant: The source quantization matrix
 *
 * Converts quantization matrix @quant from raster scan order to
 * zigzag scan order and store the resulting factors into @out_quant.
 *
 * Note: it is an error to pass the same table in both @quant and
 * @out_quant arguments.
 *
 * Since: 1.4
 */
void
gst_h264_quant_matrix_4x4_get_zigzag_from_raster (guint8 out_quant[16],
    const guint8 quant[16])
{
  guint i;

  g_return_if_fail (out_quant != quant);

  for (i = 0; i < 16; i++)
    out_quant[i] = quant[zigzag_4x4[i]];
}

/**
 * gst_h264_quant_matrix_4x4_get_raster_from_zigzag:
 * @out_quant: (out): The resulting quantization matrix
 * @quant: The source quantization matrix
 *
 * Converts quantization matrix @quant from zigzag scan order to
 * raster scan order and store the resulting factors into @out_quant.
 *
 * Note: it is an error to pass the same table in both @quant and
 * @out_quant arguments.
 *
 * Since: 1.4
 */
void
gst_h264_quant_matrix_4x4_get_raster_from_zigzag (guint8 out_quant[16],
    const guint8 quant[16])
{
  guint i;

  g_return_if_fail (out_quant != quant);

  for (i = 0; i < 16; i++)
    out_quant[zigzag_4x4[i]] = quant[i];
}

/**
 * gst_h264_video_calculate_framerate:
 * @sps: Current Sequence Parameter Set
 * @field_pic_flag: Current @field_pic_flag, obtained from latest slice header
 * @pic_struct: @pic_struct value if available, 0 otherwise
 * @fps_num: (out): The resulting fps numerator
 * @fps_den: (out): The resulting fps denominator
 *
 * Calculate framerate of a video sequence using @sps VUI information,
 * @field_pic_flag from a slice header and @pic_struct from #GstH264PicTiming SEI
 * message.
 *
 * If framerate is variable or can't be determined, @fps_num will be set to 0
 * and @fps_den to 1.
 */
void
gst_h264_video_calculate_framerate (const GstH264SPS * sps,
    guint field_pic_flag, guint pic_struct, gint * fps_num, gint * fps_den)
{
  gint num = 0;
  gint den = 1;

  /* To calculate framerate, we use this formula:
   *          time_scale                1                         1
   * fps = -----------------  x  ---------------  x  ------------------------
   *       num_units_in_tick     DeltaTfiDivisor     (field_pic_flag ? 2 : 1)
   *
   * See H264 specification E2.1 for more details.
   */

  if (sps) {
    if (sps->vui_parameters_present_flag) {
      const GstH264VUIParams *vui = &sps->vui_parameters;
      if (vui->timing_info_present_flag) {
        int delta_tfi_divisor = 1;
        num = vui->time_scale;
        den = vui->num_units_in_tick;

        if (vui->pic_struct_present_flag) {
          switch (pic_struct) {
            case 1:
            case 2:
              delta_tfi_divisor = 1;
              break;
            case 0:
            case 3:
            case 4:
              delta_tfi_divisor = 2;
              break;
            case 5:
            case 6:
              delta_tfi_divisor = 3;
              break;
            case 7:
              delta_tfi_divisor = 4;
              break;
            case 8:
              delta_tfi_divisor = 6;
              break;
          }
        } else {
          delta_tfi_divisor = field_pic_flag ? 1 : 2;
        }
        den *= delta_tfi_divisor;

        /* Picture is two fields ? */
        den *= (field_pic_flag ? 2 : 1);
      }
    }
  }

  *fps_num = num;
  *fps_den = den;
}
