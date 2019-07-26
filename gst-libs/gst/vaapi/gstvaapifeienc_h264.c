/*
 *  gstvaapifeienc_h264.c - H264 FEI ENC
 *
 *  Copyright (C) 2016-2018 Intel Corporation
 *    Author: Leilei Shang <leilei.shang@intel.com>
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/* GValueArray has deprecated without providing an alternative in glib >= 2.32
 * See https://bugzilla.gnome.org/show_bug.cgi?id=667228
 */

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "sysdeps.h"
#include <va/va.h>
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapifeienc_h264.h"
#include "gstvaapiutils_h264_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapisurface.h"
#include "gstvaapiutils.h"
#include <unistd.h>
#define DEBUG 1
#include "gstvaapidebug.h"

/* Define the maximum number of views supported */
#define MAX_NUM_VIEWS 10

/* Define the maximum value for view-id */
#define MAX_VIEW_ID 1023

/* Default CPB length (in milliseconds) */
#define DEFAULT_CPB_LENGTH 1500

/* Scale factor for CPB size (HRD cpb_size_scale: min = 4) */
#define SX_CPB_SIZE 4

/* Scale factor for bitrate (HRD bit_rate_scale: min = 6) */
#define SX_BITRATE 6

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                          \
  (GST_VAAPI_RATECONTROL_MASK (CQP)  |                  \
   GST_VAAPI_RATECONTROL_MASK (CBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR_CONSTRAINED))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS                          \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE) |                 \
   GST_VAAPI_ENCODER_TUNE_MASK (HIGH_COMPRESSION) |     \
   GST_VAAPI_ENCODER_TUNE_MASK (LOW_POWER))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  VA_ENC_PACKED_HEADER_NONE

typedef struct
{
  GstVaapiSurfaceProxy *pic;
  guint poc;
  guint frame_num;
} GstVaapiFeiEncH264Ref;

typedef enum
{
  GST_VAAPI_ENC_H264_REORD_NONE = 0,
  GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES = 1,
  GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES = 2
} GstVaapiEncH264ReorderState;

typedef struct _GstVaapiH264ViewRefPool
{
  GQueue ref_list;
  guint max_ref_frames;
  guint max_reflist0_count;
  guint max_reflist1_count;
} GstVaapiH264ViewRefPool;

typedef struct _GstVaapiH264ViewReorderPool
{
  GQueue reorder_frame_list;
  guint reorder_state;
  guint frame_index;
  guint frame_count;            /* monotonically increasing with in every idr period */
  guint cur_frame_num;
  guint cur_present_index;
} GstVaapiH264ViewReorderPool;

static inline gboolean
_poc_greater_than (guint poc1, guint poc2, guint max_poc)
{
  return (((poc1 - poc2) & (max_poc - 1)) < max_poc / 2);
}

/* Get slice_type value for H.264 specification */
static guint8
h264_get_slice_type (GstVaapiPictureType type)
{
  switch (type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      return GST_H264_I_SLICE;
    case GST_VAAPI_PICTURE_TYPE_P:
      return GST_H264_P_SLICE;
    case GST_VAAPI_PICTURE_TYPE_B:
      return GST_H264_B_SLICE;
    default:
      break;
  }
  return -1;
}

/* Get log2_max_frame_num value for H.264 specification */
static guint
h264_get_log2_max_frame_num (guint num)
{
  guint ret = 0;

  while (num) {
    ++ret;
    num >>= 1;
  }
  if (ret <= 4)
    ret = 4;
  else if (ret > 10)
    ret = 10;
  /* must be greater than 4 */
  return ret;
}

/* Determines the cpbBrNalFactor based on the supplied profile */
static guint
h264_get_cpb_nal_factor (GstVaapiProfile profile)
{
  guint f;

  /* Table A-2 */
  switch (profile) {
    case GST_VAAPI_PROFILE_H264_HIGH:
      f = 1500;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH10:
      f = 3600;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH_422:
    case GST_VAAPI_PROFILE_H264_HIGH_444:
      f = 4800;
      break;
    case GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH:
    case GST_VAAPI_PROFILE_H264_STEREO_HIGH:
      f = 1500;                 /* H.10.2.1 (r) */
      break;
    default:
      f = 1200;
      break;
  }
  return f;
}

/* ------------------------------------------------------------------------- */
/* --- FEI Enc                                                           --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_FEI_ENC_H264_CAST(feienc) \
    ((GstVaapiFeiEncH264 *)(feienc))

struct _GstVaapiFeiEncH264
{
  GstVaapiEncoder parent_instance;

  GstVaapiProfile profile;
  GstVaapiLevelH264 level;
  GstVaapiEntrypoint entrypoint;
  guint8 profile_idc;
  guint8 max_profile_idc;
  guint8 hw_max_profile_idc;
  guint8 level_idc;
  guint32 idr_period;
  guint32 init_qp;
  guint32 min_qp;
  guint32 num_slices;
  guint32 num_bframes;
  guint32 mb_width;
  guint32 mb_height;
  gboolean use_cabac;
  gboolean use_dct8x8;
  GstClockTime cts_offset;
  gboolean config_changed;

  /* frame, poc */
  guint32 max_frame_num;
  guint32 log2_max_frame_num;
  guint32 max_pic_order_cnt;
  guint32 log2_max_pic_order_cnt;
  guint32 idr_num;
  guint8 pic_order_cnt_type;
  guint8 delta_pic_order_always_zero_flag;

  GstBuffer *sps_data;
  GstBuffer *subset_sps_data;
  GstBuffer *pps_data;

  guint bitrate_bits;           // bitrate (bits)
  guint cpb_length;             // length of CPB buffer (ms)
  guint cpb_length_bits;        // length of CPB buffer (bits)
  guint32 num_ref_frames;       // set reference frame num

  /* MVC */
  gboolean is_mvc;
  guint32 view_idx;             /* View Order Index (VOIdx) */
  guint32 num_views;
  guint16 view_ids[MAX_NUM_VIEWS];
  GstVaapiH264ViewRefPool ref_pools[MAX_NUM_VIEWS];
  GstVaapiH264ViewReorderPool reorder_pools[MAX_NUM_VIEWS];

  /*Fei frame level control */
  guint search_window;
  guint len_sp;
  guint search_path;
  guint ref_width;
  guint ref_height;
  guint submb_part_mask;
  guint subpel_mode;
  guint intra_part_mask;
  guint intra_sad;
  guint inter_sad;
  guint num_mv_predictors_l0;
  guint num_mv_predictors_l1;
  guint adaptive_search;
  guint multi_predL0;
  guint multi_predL1;
};

/* Determines the largest supported profile by the underlying hardware */
static gboolean
ensure_hw_profile_limits (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (feienc);
  GArray *profiles;
  guint i, profile_idc, max_profile_idc;

  if (feienc->hw_max_profile_idc)
    return TRUE;

  profiles = gst_vaapi_display_get_encode_profiles (display);
  if (!profiles)
    return FALSE;

  max_profile_idc = 0;
  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);
    profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
    if (!profile_idc)
      continue;
    if (max_profile_idc < profile_idc)
      max_profile_idc = profile_idc;
  }
  g_array_unref (profiles);

  feienc->hw_max_profile_idc = max_profile_idc;
  return TRUE;
}

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (feienc);
  GstVaapiEntrypoint entrypoint = feienc->entrypoint;
  GstVaapiProfile profile, profiles[4];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = feienc->profile;
  switch (feienc->profile) {
    case GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_BASELINE;
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_MAIN;
      // fall-through
    case GST_VAAPI_PROFILE_H264_MAIN:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_HIGH;
      break;
    default:
      break;
  }

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (i = 0; i < num_profiles; i++) {
    if (gst_vaapi_display_has_encoder (display, profiles[i], entrypoint)) {
      profile = profiles[i];
      break;
    }
  }
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    goto error_unsupported_profile;

  GST_VAAPI_ENCODER_CAST (feienc)->profile = profile;
  return TRUE;

  /* ERRORS */
error_unsupported_profile:
  {
    GST_ERROR ("unsupported HW profile (0x%08x)", feienc->profile);
    return FALSE;
  }
}

/* Check target decoder constraints */
static gboolean
ensure_profile_limits (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiProfile profile;

  if (!feienc->max_profile_idc
      || feienc->profile_idc <= feienc->max_profile_idc)
    return TRUE;

  GST_WARNING ("lowering coding tools to meet target decoder constraints");

  profile = GST_VAAPI_PROFILE_UNKNOWN;

  /* Try Main profile coding tools */
  if (feienc->max_profile_idc < 100) {
    feienc->use_dct8x8 = FALSE;
    profile = GST_VAAPI_PROFILE_H264_MAIN;
  }

  /* Try Constrained Baseline profile coding tools */
  if (feienc->max_profile_idc < 77) {
    feienc->num_bframes = 0;
    feienc->use_cabac = FALSE;
    profile = GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;
  }

  if (profile) {
    feienc->profile = profile;
    feienc->profile_idc = feienc->max_profile_idc;
  }
  return TRUE;
}

/* Derives the minimum profile from the active coding tools */
static gboolean
ensure_profile (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiProfile profile;

  /* Always start from "constrained-baseline" profile for maximum
     compatibility */
  profile = GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;

  /* Main profile coding tools */
  if (feienc->num_bframes > 0 || feienc->use_cabac)
    profile = GST_VAAPI_PROFILE_H264_MAIN;

  /* High profile coding tools */
  if (feienc->use_dct8x8)
    profile = GST_VAAPI_PROFILE_H264_HIGH;

  /* MVC profiles coding tools */
  if (feienc->num_views == 2)
    profile = GST_VAAPI_PROFILE_H264_STEREO_HIGH;
  else if (feienc->num_views > 2)
    profile = GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH;

  feienc->profile = profile;
  feienc->profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
  return TRUE;
}

/* Derives the level from the currently set limits */
static gboolean
ensure_level (GstVaapiFeiEncH264 * feienc)
{
  const guint cpb_factor = h264_get_cpb_nal_factor (feienc->profile);
  const GstVaapiH264LevelLimits *limits_table;
  guint i, num_limits, PicSizeMbs, MaxDpbMbs, MaxMBPS;

  PicSizeMbs = feienc->mb_width * feienc->mb_height;
  MaxDpbMbs = PicSizeMbs * ((feienc->num_bframes) ? 2 : 1);
  MaxMBPS = gst_util_uint64_scale_int_ceil (PicSizeMbs,
      GST_VAAPI_ENCODER_FPS_N (feienc), GST_VAAPI_ENCODER_FPS_D (feienc));

  limits_table = gst_vaapi_utils_h264_get_level_limits_table (&num_limits);
  for (i = 0; i < num_limits; i++) {
    const GstVaapiH264LevelLimits *const limits = &limits_table[i];
    if (PicSizeMbs <= limits->MaxFS &&
        MaxDpbMbs <= limits->MaxDpbMbs &&
        MaxMBPS <= limits->MaxMBPS && (!feienc->bitrate_bits
            || feienc->bitrate_bits <= (limits->MaxBR * cpb_factor)) &&
        (!feienc->cpb_length_bits ||
            feienc->cpb_length_bits <= (limits->MaxCPB * cpb_factor)))
      break;
  }
  if (i == num_limits)
    goto error_unsupported_level;

  feienc->level = limits_table[i].level;
  feienc->level_idc = limits_table[i].level_idc;
  return TRUE;

  /* ERRORS */
error_unsupported_level:
  {
    GST_ERROR ("failed to find a suitable level matching codec config");
    return FALSE;
  }
}

/* Enable "high-compression" tuning options */
static gboolean
ensure_tuning_high_compression (GstVaapiFeiEncH264 * feienc)
{
  guint8 profile_idc;

  if (!ensure_hw_profile_limits (feienc))
    return FALSE;

  profile_idc = feienc->hw_max_profile_idc;
  if (feienc->max_profile_idc && feienc->max_profile_idc < profile_idc)
    profile_idc = feienc->max_profile_idc;

  /* Tuning options to enable Main profile */
  if (profile_idc >= 77 && profile_idc != 88) {
    feienc->use_cabac = TRUE;
    if (!feienc->num_bframes)
      feienc->num_bframes = 1;
  }

  /* Tuning options to enable High profile */
  if (profile_idc >= 100) {
    feienc->use_dct8x8 = TRUE;
  }
  return TRUE;
}

/* Ensure tuning options */
static gboolean
ensure_tuning (GstVaapiFeiEncH264 * feienc)
{
  gboolean success;

  switch (GST_VAAPI_ENCODER_TUNE (feienc)) {
    case GST_VAAPI_ENCODER_TUNE_HIGH_COMPRESSION:
      success = ensure_tuning_high_compression (feienc);
      break;
    case GST_VAAPI_ENCODER_TUNE_LOW_POWER:
      /* Set low-power encode entry point. If hardware doesn't have
       * support, it will fail in ensure_hw_profile() in later stage.
       * So not duplicating the profile/entrypont query mechanism
       * here as a part of optimization */
      feienc->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP;
      success = TRUE;
      break;
    default:
      success = TRUE;
      break;
  }
  return success;
}

/* Handle new GOP starts */
static void
reset_gop_start (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiH264ViewReorderPool *const reorder_pool =
      &feienc->reorder_pools[feienc->view_idx];

  reorder_pool->frame_index = 1;
  reorder_pool->cur_frame_num = 0;
  reorder_pool->cur_present_index = 0;
  ++feienc->idr_num;
}

/* Marks the supplied picture as a B-frame */
static void
set_b_frame (GstVaapiEncPicture * pic, GstVaapiFeiEncH264 * feienc)
{
  GstVaapiH264ViewReorderPool *const reorder_pool =
      &feienc->reorder_pools[feienc->view_idx];

  g_assert (pic && feienc);
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_B;
  pic->frame_num = (reorder_pool->cur_frame_num % feienc->max_frame_num);
}

/* Marks the supplied picture as a P-frame */
static void
set_p_frame (GstVaapiEncPicture * pic, GstVaapiFeiEncH264 * feienc)
{
  GstVaapiH264ViewReorderPool *const reorder_pool =
      &feienc->reorder_pools[feienc->view_idx];

  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_P;
  pic->frame_num = (reorder_pool->cur_frame_num % feienc->max_frame_num);
}

/* Marks the supplied picture as an I-frame */
static void
set_i_frame (GstVaapiEncPicture * pic, GstVaapiFeiEncH264 * feienc)
{
  GstVaapiH264ViewReorderPool *const reorder_pool =
      &feienc->reorder_pools[feienc->view_idx];

  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;
  pic->frame_num = (reorder_pool->cur_frame_num % feienc->max_frame_num);

  g_assert (pic->frame);
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (pic->frame);
}

/* Marks the supplied picture as an IDR frame */
static void
set_idr_frame (GstVaapiEncPicture * pic, GstVaapiFeiEncH264 * feienc)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;
  pic->frame_num = 0;
  pic->poc = 0;
  GST_VAAPI_ENC_PICTURE_FLAG_SET (pic, GST_VAAPI_ENC_PICTURE_FLAG_IDR);

  g_assert (pic->frame);
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (pic->frame);
}

/* Marks the supplied picture a a key-frame */
static void
set_key_frame (GstVaapiEncPicture * picture,
    GstVaapiFeiEncH264 * feienc, gboolean is_idr)
{
  if (is_idr) {
    reset_gop_start (feienc);
    set_idr_frame (picture, feienc);
  } else
    set_i_frame (picture, feienc);
}

/* Fills in VA HRD parameters */
static void
fill_hrd_params (GstVaapiFeiEncH264 * feienc, VAEncMiscParameterHRD * hrd)
{
  if (feienc->bitrate_bits > 0) {
    hrd->buffer_size = feienc->cpb_length_bits;
    hrd->initial_buffer_fullness = hrd->buffer_size / 2;
  } else {
    hrd->buffer_size = 0;
    hrd->initial_buffer_fullness = 0;
  }
}

/* Reference list */
static gboolean
reference_list_init (GstVaapiFeiEncH264 * feienc,
    GstVaapiEncPicture * picture,
    GstVaapiFeiEncH264Ref ** reflist_0,
    guint * reflist_0_count,
    GstVaapiFeiEncH264Ref ** reflist_1, guint * reflist_1_count)
{
  GstVaapiFeiEncH264Ref *tmp;
  GstVaapiH264ViewRefPool *const ref_pool =
      &feienc->ref_pools[feienc->view_idx];
  GList *iter, *list_0_start = NULL, *list_1_start = NULL;
  guint count;

  *reflist_0_count = 0;
  *reflist_1_count = 0;
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  iter = g_queue_peek_tail_link (&ref_pool->ref_list);
  for (; iter; iter = g_list_previous (iter)) {
    tmp = (GstVaapiFeiEncH264Ref *) iter->data;
    g_assert (tmp && tmp->poc != picture->poc);
    if (_poc_greater_than (picture->poc, tmp->poc, feienc->max_pic_order_cnt)) {
      list_0_start = iter;
      list_1_start = g_list_next (iter);
      break;
    }
  }

  /* order reflist_0 */
  g_assert (list_0_start);
  iter = list_0_start;
  count = 0;
  for (; iter; iter = g_list_previous (iter)) {
    reflist_0[count] = (GstVaapiFeiEncH264Ref *) iter->data;
    ++count;
  }
  *reflist_0_count = count;

  if (picture->type != GST_VAAPI_PICTURE_TYPE_B)
    return TRUE;

  /* order reflist_1 */
  count = 0;
  iter = list_1_start;
  for (; iter; iter = g_list_next (iter)) {
    reflist_1[count] = (GstVaapiFeiEncH264Ref *) iter->data;
    ++count;
  }
  *reflist_1_count = count;
  return TRUE;
}

/* Fills in VA sequence parameter buffer */
static gboolean
fill_sequence (GstVaapiFeiEncH264 * feienc, GstVaapiEncSequence * sequence)
{
  VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;
  GstVaapiH264ViewRefPool *const ref_pool =
      &feienc->ref_pools[feienc->view_idx];

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferH264));
  seq_param->seq_parameter_set_id = feienc->view_idx;
  seq_param->level_idc = feienc->level_idc;
  seq_param->intra_period = GST_VAAPI_ENCODER_KEYFRAME_PERIOD (feienc);
  seq_param->intra_idr_period = GST_VAAPI_ENCODER_KEYFRAME_PERIOD (feienc);
  seq_param->ip_period = seq_param->intra_period > 1 ?
      (1 + feienc->num_bframes) : 0;
  seq_param->bits_per_second = feienc->bitrate_bits;

  seq_param->max_num_ref_frames = ref_pool->max_ref_frames;
  seq_param->picture_width_in_mbs = feienc->mb_width;
  seq_param->picture_height_in_mbs = feienc->mb_height;

  /*sequence field values */
  seq_param->seq_fields.value = 0;
  seq_param->seq_fields.bits.chroma_format_idc = 1;
  seq_param->seq_fields.bits.frame_mbs_only_flag = 1;
  seq_param->seq_fields.bits.mb_adaptive_frame_field_flag = FALSE;
  seq_param->seq_fields.bits.seq_scaling_matrix_present_flag = FALSE;
  /* direct_8x8_inference_flag default false */
  seq_param->seq_fields.bits.direct_8x8_inference_flag = FALSE;
  g_assert (feienc->log2_max_frame_num >= 4);
  seq_param->seq_fields.bits.log2_max_frame_num_minus4 =
      feienc->log2_max_frame_num - 4;
  /* picture order count */
  feienc->pic_order_cnt_type = seq_param->seq_fields.bits.pic_order_cnt_type =
      0;
  g_assert (feienc->log2_max_pic_order_cnt >= 4);
  seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 =
      feienc->log2_max_pic_order_cnt - 4;

  seq_param->bit_depth_luma_minus8 = 0;
  seq_param->bit_depth_chroma_minus8 = 0;

  /* not used if pic_order_cnt_type == 0 */
  if (seq_param->seq_fields.bits.pic_order_cnt_type == 1) {
    feienc->delta_pic_order_always_zero_flag =
        seq_param->seq_fields.bits.delta_pic_order_always_zero_flag = TRUE;
    seq_param->num_ref_frames_in_pic_order_cnt_cycle = 0;
    seq_param->offset_for_non_ref_pic = 0;
    seq_param->offset_for_top_to_bottom_field = 0;
    memset (seq_param->offset_for_ref_frame, 0,
        sizeof (seq_param->offset_for_ref_frame));
  }

  /* frame_cropping_flag */
  if ((GST_VAAPI_ENCODER_WIDTH (feienc) & 15) ||
      (GST_VAAPI_ENCODER_HEIGHT (feienc) & 15)) {
    static const guint SubWidthC[] = { 1, 2, 2, 1 };
    static const guint SubHeightC[] = { 1, 2, 1, 1 };
    const guint CropUnitX =
        SubWidthC[seq_param->seq_fields.bits.chroma_format_idc];
    const guint CropUnitY =
        SubHeightC[seq_param->seq_fields.bits.chroma_format_idc] *
        (2 - seq_param->seq_fields.bits.frame_mbs_only_flag);

    seq_param->frame_cropping_flag = 1;
    seq_param->frame_crop_left_offset = 0;
    seq_param->frame_crop_right_offset =
        (16 * feienc->mb_width - GST_VAAPI_ENCODER_WIDTH (feienc)) / CropUnitX;
    seq_param->frame_crop_top_offset = 0;
    seq_param->frame_crop_bottom_offset =
        (16 * feienc->mb_height -
        GST_VAAPI_ENCODER_HEIGHT (feienc)) / CropUnitY;
  }

  /* VUI parameters are always set, at least for timing_info (framerate) */
  seq_param->vui_parameters_present_flag = TRUE;
  if (seq_param->vui_parameters_present_flag) {
    seq_param->vui_fields.bits.aspect_ratio_info_present_flag = TRUE;
    if (seq_param->vui_fields.bits.aspect_ratio_info_present_flag) {
      const GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (feienc);
      seq_param->aspect_ratio_idc = 0xff;
      seq_param->sar_width = GST_VIDEO_INFO_PAR_N (vip);
      seq_param->sar_height = GST_VIDEO_INFO_PAR_D (vip);
    }
    seq_param->vui_fields.bits.bitstream_restriction_flag = FALSE;
    /* if vui_parameters_present_flag is TRUE and sps data belongs to
     * subset sps, timing_info_preset_flag should be zero (H.7.4.2.1.1) */
    seq_param->vui_fields.bits.timing_info_present_flag = !feienc->view_idx;
    if (seq_param->vui_fields.bits.timing_info_present_flag) {
      seq_param->num_units_in_tick = GST_VAAPI_ENCODER_FPS_D (feienc);
      seq_param->time_scale = GST_VAAPI_ENCODER_FPS_N (feienc) * 2;
    }
  }

  return TRUE;
}

/* Fills in VA picture parameter buffer */
static gboolean
fill_picture (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture,
    GstVaapiSurfaceProxy * surface, GstVaapiCodedBuffer * const codedbuf)
{
  VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  GstVaapiH264ViewRefPool *const ref_pool =
      &feienc->ref_pools[feienc->view_idx];
  GstVaapiFeiEncH264Ref *ref_pic;
  GList *reflist;
  guint i;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferH264));

  /* reference list,  */
  pic_param->CurrPic.picture_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->CurrPic.TopFieldOrderCnt = picture->poc;
  pic_param->CurrPic.frame_idx = picture->frame_num;
  i = 0;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
    for (reflist = g_queue_peek_head_link (&ref_pool->ref_list);
        reflist; reflist = g_list_next (reflist)) {
      ref_pic = reflist->data;
      g_assert (ref_pic && ref_pic->pic &&
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic) != VA_INVALID_ID);

      pic_param->ReferenceFrames[i].picture_id =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic);
      pic_param->ReferenceFrames[i].TopFieldOrderCnt = ref_pic->poc;
      pic_param->ReferenceFrames[i].flags |=
          VA_PICTURE_H264_SHORT_TERM_REFERENCE;
      pic_param->ReferenceFrames[i].frame_idx = ref_pic->frame_num;
      ++i;
    }
    g_assert (i <= 16 && i <= ref_pool->max_ref_frames);
  }
  for (; i < 16; ++i) {
    pic_param->ReferenceFrames[i].picture_id = VA_INVALID_ID;
    pic_param->ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
  }

  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  pic_param->pic_parameter_set_id = feienc->view_idx;
  pic_param->seq_parameter_set_id = feienc->view_idx ? 1 : 0;
  pic_param->last_picture = 0;  /* means last encoding picture */
  pic_param->frame_num = picture->frame_num;
  pic_param->pic_init_qp = feienc->init_qp;
  pic_param->num_ref_idx_l0_active_minus1 =
      (ref_pool->max_reflist0_count ? (ref_pool->max_reflist0_count - 1) : 0);
  pic_param->num_ref_idx_l1_active_minus1 =
      (ref_pool->max_reflist1_count ? (ref_pool->max_reflist1_count - 1) : 0);
  pic_param->chroma_qp_index_offset = 0;
  pic_param->second_chroma_qp_index_offset = 0;

  /* set picture fields */
  pic_param->pic_fields.value = 0;
  pic_param->pic_fields.bits.idr_pic_flag =
      GST_VAAPI_ENC_PICTURE_IS_IDR (picture);
  pic_param->pic_fields.bits.reference_pic_flag =
      (picture->type != GST_VAAPI_PICTURE_TYPE_B);
  pic_param->pic_fields.bits.entropy_coding_mode_flag = feienc->use_cabac;
  pic_param->pic_fields.bits.weighted_pred_flag = FALSE;
  pic_param->pic_fields.bits.weighted_bipred_idc = 0;
  pic_param->pic_fields.bits.constrained_intra_pred_flag = 0;
  pic_param->pic_fields.bits.transform_8x8_mode_flag = feienc->use_dct8x8;
  /* enable debloking */
  pic_param->pic_fields.bits.deblocking_filter_control_present_flag = TRUE;
  pic_param->pic_fields.bits.redundant_pic_cnt_present_flag = FALSE;
  /* bottom_field_pic_order_in_frame_present_flag */
  pic_param->pic_fields.bits.pic_order_present_flag = FALSE;
  pic_param->pic_fields.bits.pic_scaling_matrix_present_flag = FALSE;

  return TRUE;
}

/* Adds slice headers to picture */
static gboolean
add_slice_headers (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture,
    GstVaapiFeiEncH264Ref ** reflist_0, guint reflist_0_count,
    GstVaapiFeiEncH264Ref ** reflist_1, guint reflist_1_count,
    GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  VAEncSliceParameterBufferH264 *slice_param;
  GstVaapiEncSlice *slice;
  GArray *h264_slice_params;
  guint slice_of_mbs, slice_mod_mbs, cur_slice_mbs;
  guint mb_size;
  guint last_mb_index;
  guint i_slice, i_ref;

  g_assert (picture);

  mb_size = feienc->mb_width * feienc->mb_height;

  g_assert (feienc->num_slices && feienc->num_slices < mb_size);
  slice_of_mbs = mb_size / feienc->num_slices;
  slice_mod_mbs = mb_size % feienc->num_slices;
  last_mb_index = 0;
  h264_slice_params =
      g_array_new (FALSE, TRUE, sizeof (VAEncSliceParameterBufferH264));
  for (i_slice = 0; i_slice < feienc->num_slices; ++i_slice) {
    cur_slice_mbs = slice_of_mbs;
    if (slice_mod_mbs) {
      ++cur_slice_mbs;
      --slice_mod_mbs;
    }
    slice = GST_VAAPI_ENC_SLICE_NEW (H264, feienc);
    g_assert (slice && slice->param_id != VA_INVALID_ID);
    slice_param = slice->param;

    memset (slice_param, 0, sizeof (VAEncSliceParameterBufferH264));
    slice_param->macroblock_address = last_mb_index;
    slice_param->num_macroblocks = cur_slice_mbs;
    slice_param->macroblock_info = VA_INVALID_ID;
    slice_param->slice_type = h264_get_slice_type (picture->type);
    g_assert ((gint8) slice_param->slice_type != -1);
    slice_param->pic_parameter_set_id = feienc->view_idx;
    slice_param->idr_pic_id = feienc->idr_num;
    slice_param->pic_order_cnt_lsb = picture->poc;

    /* not used if pic_order_cnt_type = 0 */
    slice_param->delta_pic_order_cnt_bottom = 0;
    memset (slice_param->delta_pic_order_cnt, 0,
        sizeof (slice_param->delta_pic_order_cnt));

    /* only works for B frames */
    if (slice_param->slice_type == GST_H264_B_SLICE)
      slice_param->direct_spatial_mv_pred_flag = TRUE;
    /* default equal to picture parameters */
    slice_param->num_ref_idx_active_override_flag = TRUE;
    if (picture->type != GST_VAAPI_PICTURE_TYPE_I && reflist_0_count > 0)
      slice_param->num_ref_idx_l0_active_minus1 = reflist_0_count - 1;
    else
      slice_param->num_ref_idx_l0_active_minus1 = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B && reflist_1_count > 0)
      slice_param->num_ref_idx_l1_active_minus1 = reflist_1_count - 1;
    else
      slice_param->num_ref_idx_l1_active_minus1 = 0;
    g_assert (slice_param->num_ref_idx_l0_active_minus1 >= 0);
    g_assert (slice_param->num_ref_idx_l1_active_minus1 == 0);

    i_ref = 0;
    if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
      for (; i_ref < reflist_0_count; ++i_ref) {
        slice_param->RefPicList0[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_0[i_ref]->pic);
        slice_param->RefPicList0[i_ref].TopFieldOrderCnt =
            reflist_0[i_ref]->poc;
        slice_param->RefPicList0[i_ref].flags |=
            VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        slice_param->RefPicList0[i_ref].frame_idx = reflist_0[i_ref]->frame_num;
      }
      g_assert (i_ref >= 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList0); ++i_ref) {
      slice_param->RefPicList0[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->RefPicList0[i_ref].flags = VA_PICTURE_H264_INVALID;
    }

    i_ref = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
      for (; i_ref < reflist_1_count; ++i_ref) {
        slice_param->RefPicList1[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_1[i_ref]->pic);
        slice_param->RefPicList1[i_ref].TopFieldOrderCnt =
            reflist_1[i_ref]->poc;
        slice_param->RefPicList1[i_ref].flags |=
            VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        slice_param->RefPicList1[i_ref].frame_idx = reflist_1[i_ref]->frame_num;
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList1); ++i_ref) {
      slice_param->RefPicList1[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->RefPicList1[i_ref].flags = VA_PICTURE_H264_INVALID;
    }

    /* not used if  pic_param.pic_fields.bits.weighted_pred_flag == FALSE */
    slice_param->luma_log2_weight_denom = 0;
    slice_param->chroma_log2_weight_denom = 0;
    slice_param->luma_weight_l0_flag = FALSE;
    memset (slice_param->luma_weight_l0, 0,
        sizeof (slice_param->luma_weight_l0));
    memset (slice_param->luma_offset_l0, 0,
        sizeof (slice_param->luma_offset_l0));
    slice_param->chroma_weight_l0_flag = FALSE;
    memset (slice_param->chroma_weight_l0, 0,
        sizeof (slice_param->chroma_weight_l0));
    memset (slice_param->chroma_offset_l0, 0,
        sizeof (slice_param->chroma_offset_l0));
    slice_param->luma_weight_l1_flag = FALSE;
    memset (slice_param->luma_weight_l1, 0,
        sizeof (slice_param->luma_weight_l1));
    memset (slice_param->luma_offset_l1, 0,
        sizeof (slice_param->luma_offset_l1));
    slice_param->chroma_weight_l1_flag = FALSE;
    memset (slice_param->chroma_weight_l1, 0,
        sizeof (slice_param->chroma_weight_l1));
    memset (slice_param->chroma_offset_l1, 0,
        sizeof (slice_param->chroma_offset_l1));

    slice_param->cabac_init_idc = 0;
    slice_param->slice_qp_delta = feienc->init_qp - feienc->min_qp;
    if (slice_param->slice_qp_delta > 4)
      slice_param->slice_qp_delta = 4;
    slice_param->disable_deblocking_filter_idc = 0;
    slice_param->slice_alpha_c0_offset_div2 = 2;
    slice_param->slice_beta_offset_div2 = 2;

    /* set calculation for next slice */
    last_mb_index += cur_slice_mbs;

    g_array_append_val (h264_slice_params, *slice_param);

    gst_vaapi_enc_picture_add_slice (picture, slice);
    gst_vaapi_codec_object_replace (&slice, NULL);
  }
  g_assert (last_mb_index == mb_size);

  info_to_pak->h264_slice_headers = h264_slice_params;

  return TRUE;

}

/* Generates and submits SPS header accordingly into the bitstream */
static gboolean
ensure_sequence (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture,
    GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  GstVaapiEncSequence *sequence = NULL;
  VAEncSequenceParameterBufferH264 *seq_param;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (H264, feienc);
  if (!sequence || !fill_sequence (feienc, sequence))
    goto error_create_seq_param;

  seq_param = sequence->param;
  info_to_pak->h264_enc_sps = *seq_param;

  if (sequence) {
    gst_vaapi_enc_picture_set_sequence (picture, sequence);
    gst_vaapi_codec_object_replace (&sequence, NULL);
  }

  if (!feienc->is_mvc || feienc->view_idx > 0)
    feienc->config_changed = FALSE;
  return TRUE;

  /* ERRORS */
error_create_seq_param:
  {
    GST_ERROR ("failed to create sequence parameter buffer (SPS)");
    gst_vaapi_codec_object_replace (&sequence, NULL);
    return FALSE;
  }
}

/* Generates additional fei control parameters */
static gboolean
ensure_fei_misc_params (GstVaapiFeiEncH264 * feienc,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf_proxy)
{
  GstVaapiEncMiscParam *misc = NULL;
  GstVaapiSurfaceProxy *surface_proxy = picture->proxy;
  VAEncMiscParameterFEIFrameControlH264 *misc_fei_pic_control_param;
  guint mbcode_size = 0;
  guint mv_size = 0;
  guint dist_size = 0;

  /*fei pic control params */
  misc = GST_VAAPI_ENC_FEI_MISC_PARAM_NEW (H264, feienc);
  g_assert (misc);
  if (!misc)
    return FALSE;
  misc_fei_pic_control_param = misc->data;
  surface_proxy = picture->proxy;

  misc_fei_pic_control_param->function = VA_FEI_FUNCTION_ENC;
  misc_fei_pic_control_param->search_path = feienc->search_path;
  misc_fei_pic_control_param->num_mv_predictors_l0 =
      feienc->num_mv_predictors_l0;
  misc_fei_pic_control_param->num_mv_predictors_l1 =
      feienc->num_mv_predictors_l1;
  misc_fei_pic_control_param->len_sp = feienc->len_sp;
  misc_fei_pic_control_param->sub_mb_part_mask = feienc->submb_part_mask;
  if (!feienc->use_dct8x8)
    misc_fei_pic_control_param->intra_part_mask = feienc->intra_part_mask | 2;
  misc_fei_pic_control_param->multi_pred_l0 = feienc->multi_predL0;
  misc_fei_pic_control_param->multi_pred_l1 = feienc->multi_predL1;
  misc_fei_pic_control_param->sub_pel_mode = feienc->subpel_mode;
  misc_fei_pic_control_param->inter_sad = feienc->inter_sad;
  misc_fei_pic_control_param->intra_sad = feienc->intra_sad;
  misc_fei_pic_control_param->distortion_type = 0;
  misc_fei_pic_control_param->repartition_check_enable = 0;
  misc_fei_pic_control_param->adaptive_search = feienc->adaptive_search;
  misc_fei_pic_control_param->mb_size_ctrl = 0;
  misc_fei_pic_control_param->ref_width = feienc->ref_width;
  misc_fei_pic_control_param->ref_height = feienc->ref_height;
  misc_fei_pic_control_param->search_window = feienc->search_window;

  /*****  ENC input: mv_predictor *****/
  if (surface_proxy->mvpred) {
    misc_fei_pic_control_param->mv_predictor =
        GST_VAAPI_FEI_CODEC_OBJECT (surface_proxy->mvpred)->param_id;
    misc_fei_pic_control_param->mv_predictor_enable = TRUE;
    gst_vaapi_codec_object_replace (&picture->mvpred, surface_proxy->mvpred);
  } else {
    misc_fei_pic_control_param->mv_predictor = VA_INVALID_ID;
    misc_fei_pic_control_param->mv_predictor_enable = FALSE;
    picture->mvpred = NULL;
  }

    /*****  ENC input: qp ******/
  if (surface_proxy->qp) {
    misc_fei_pic_control_param->qp =
        GST_VAAPI_FEI_CODEC_OBJECT (surface_proxy->qp)->param_id;
    misc_fei_pic_control_param->mb_qp = TRUE;
    gst_vaapi_codec_object_replace (&picture->qp, surface_proxy->qp);
  } else {
    misc_fei_pic_control_param->qp = VA_INVALID_ID;
    misc_fei_pic_control_param->mb_qp = FALSE;
    picture->qp = NULL;
  }
    /*****  ENC input: mb_control ******/
  if (surface_proxy->mbcntrl) {
    misc_fei_pic_control_param->mb_ctrl =
        GST_VAAPI_FEI_CODEC_OBJECT (surface_proxy->mbcntrl)->param_id;
    misc_fei_pic_control_param->mb_input = TRUE;
    gst_vaapi_codec_object_replace (&picture->mbcntrl, surface_proxy->mbcntrl);
  } else {
    misc_fei_pic_control_param->mb_ctrl = VA_INVALID_ID;
    misc_fei_pic_control_param->mb_input = FALSE;
    picture->mbcntrl = NULL;
  }

  mbcode_size = sizeof (VAEncFEIMBCodeH264) *
      feienc->mb_width * feienc->mb_height;
  mv_size = sizeof (VAMotionVector) * 16 * feienc->mb_width * feienc->mb_height;
  dist_size = sizeof (VAEncFEIDistortionH264) *
      feienc->mb_width * feienc->mb_height;
  /***** ENC_PAK/ENC output: macroblock code buffer *****/
  codedbuf_proxy->mbcode =
      gst_vaapi_enc_fei_mb_code_new (GST_VAAPI_ENCODER_CAST (feienc),
      NULL, mbcode_size);
  misc_fei_pic_control_param->mb_code_data =
      GST_VAAPI_FEI_CODEC_OBJECT (codedbuf_proxy->mbcode)->param_id;
  picture->mbcode = gst_vaapi_codec_object_ref (codedbuf_proxy->mbcode);

  /***** ENC_PAK/ENC output: motion vector buffer *****/
  codedbuf_proxy->mv =
      gst_vaapi_enc_fei_mv_new (GST_VAAPI_ENCODER_CAST (feienc), NULL, mv_size);
  misc_fei_pic_control_param->mv_data =
      GST_VAAPI_FEI_CODEC_OBJECT (codedbuf_proxy->mv)->param_id;
  picture->mv = gst_vaapi_codec_object_ref (codedbuf_proxy->mv);

  /* Fixme: a copy needed in coded_buf proxy */
  /***** ENC_PAK/ENC output: distortion buffer *****/
  picture->dist =
      gst_vaapi_enc_fei_distortion_new (GST_VAAPI_ENCODER_CAST (feienc),
      NULL, dist_size);
  misc_fei_pic_control_param->distortion =
      GST_VAAPI_FEI_CODEC_OBJECT (picture->dist)->param_id;
  codedbuf_proxy->dist = gst_vaapi_codec_object_ref (picture->dist);

  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);
  return TRUE;
}

/* Generates additional control parameters */
static gboolean
ensure_misc_params (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture)
{
  GstVaapiEncMiscParam *misc = NULL;
  VAEncMiscParameterRateControl *rate_control;

  /* HRD params */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, feienc);
  g_assert (misc);
  if (!misc)
    return FALSE;
  fill_hrd_params (feienc, misc->data);
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  /* RateControl params */
  if (GST_VAAPI_ENCODER_RATE_CONTROL (feienc) == GST_VAAPI_RATECONTROL_CBR ||
      GST_VAAPI_ENCODER_RATE_CONTROL (feienc) == GST_VAAPI_RATECONTROL_VBR) {
    misc = GST_VAAPI_ENC_MISC_PARAM_NEW (RateControl, feienc);
    g_assert (misc);
    if (!misc)
      return FALSE;
    rate_control = misc->data;
    memset (rate_control, 0, sizeof (VAEncMiscParameterRateControl));
    rate_control->bits_per_second = feienc->bitrate_bits;
    rate_control->target_percentage = 70;
    rate_control->window_size = feienc->cpb_length;
    rate_control->initial_qp = feienc->init_qp;
    rate_control->min_qp = feienc->min_qp;
    rate_control->basic_unit_size = 0;
    gst_vaapi_enc_picture_add_misc_param (picture, misc);
    gst_vaapi_codec_object_replace (&misc, NULL);

  }
  return TRUE;

}

/* Generates and submits PPS header accordingly into the bitstream */
static gboolean
ensure_picture (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture,
    GstVaapiSurfaceProxy * surface, GstVaapiCodedBufferProxy * codedbuf_proxy,
    GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  gboolean res = FALSE;
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);
  VAEncPictureParameterBufferH264 *const pic_param = picture->param;

  res = fill_picture (feienc, picture, surface, codedbuf);

  if (!res)
    return FALSE;

  info_to_pak->h264_enc_pps = *pic_param;

  return TRUE;
}

/* Generates slice headers */
static gboolean
ensure_slices (GstVaapiFeiEncH264 * feienc, GstVaapiEncPicture * picture,
    GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  GstVaapiFeiEncH264Ref *reflist_0[16];
  GstVaapiFeiEncH264Ref *reflist_1[16];
  GstVaapiH264ViewRefPool *const ref_pool =
      &feienc->ref_pools[feienc->view_idx];
  guint reflist_0_count = 0, reflist_1_count = 0;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I &&
      !reference_list_init (feienc, picture,
          reflist_0, &reflist_0_count, reflist_1, &reflist_1_count)) {
    GST_ERROR ("reference list reorder failed");
    return FALSE;
  }

  g_assert (reflist_0_count + reflist_1_count <= ref_pool->max_ref_frames);
  if (reflist_0_count > ref_pool->max_reflist0_count)
    reflist_0_count = ref_pool->max_reflist0_count;
  if (reflist_1_count > ref_pool->max_reflist1_count)
    reflist_1_count = ref_pool->max_reflist1_count;

  if (!add_slice_headers (feienc, picture,
          reflist_0, reflist_0_count, reflist_1, reflist_1_count, info_to_pak))
    return FALSE;

  return TRUE;
}

/* Normalizes bitrate (and CPB size) for HRD conformance */
static void
ensure_bitrate_hrd (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (feienc);
  guint bitrate, cpb_size;

  if (!base_encoder->bitrate) {
    feienc->bitrate_bits = 0;
    return;
  }

  /* Round down bitrate. This is a hard limit mandated by the user */
  g_assert (SX_BITRATE >= 6);
  bitrate = (base_encoder->bitrate * 1000) & ~((1U << SX_BITRATE) - 1);
  if (bitrate != feienc->bitrate_bits) {
    GST_DEBUG ("HRD bitrate: %u bits/sec", bitrate);
    feienc->bitrate_bits = bitrate;
    feienc->config_changed = TRUE;
  }

  /* Round up CPB size. This is an HRD compliance detail */
  g_assert (SX_CPB_SIZE >= 4);
  cpb_size = gst_util_uint64_scale (bitrate, feienc->cpb_length, 1000) &
      ~((1U << SX_CPB_SIZE) - 1);
  if (cpb_size != feienc->cpb_length_bits) {
    GST_DEBUG ("HRD CPB size: %u bits", cpb_size);
    feienc->cpb_length_bits = cpb_size;
    feienc->config_changed = TRUE;
  }
}

/* Estimates a good enough bitrate if none was supplied */
static void
ensure_bitrate (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (feienc);

  /* Default compression: 48 bits per macroblock in "high-compression" mode */
  switch (GST_VAAPI_ENCODER_RATE_CONTROL (feienc)) {
    case GST_VAAPI_RATECONTROL_CBR:
    case GST_VAAPI_RATECONTROL_VBR:
    case GST_VAAPI_RATECONTROL_VBR_CONSTRAINED:
      if (!base_encoder->bitrate) {
        /* According to the literature and testing, CABAC entropy coding
           mode could provide for +10% to +18% improvement in general,
           thus estimating +15% here ; and using adaptive 8x8 transforms
           in I-frames could bring up to +10% improvement. */
        guint bits_per_mb = 48;
        if (!feienc->use_cabac)
          bits_per_mb += (bits_per_mb * 15) / 100;
        if (!feienc->use_dct8x8)
          bits_per_mb += (bits_per_mb * 10) / 100;

        base_encoder->bitrate =
            feienc->mb_width * feienc->mb_height * bits_per_mb *
            GST_VAAPI_ENCODER_FPS_N (feienc) /
            GST_VAAPI_ENCODER_FPS_D (feienc) / 1000;
        GST_INFO ("target bitrate computed to %u kbps", base_encoder->bitrate);
      }
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
  ensure_bitrate_hrd (feienc);
}

/* Constructs profile and level information based on user-defined limits */
static GstVaapiEncoderStatus
ensure_profile_and_level (GstVaapiFeiEncH264 * feienc)
{
  const GstVaapiProfile profile = feienc->profile;
  const GstVaapiLevelH264 level = feienc->level;

  if (!ensure_tuning (feienc))
    GST_WARNING ("Failed to set some of the tuning option as expected! ");

  if (!ensure_profile (feienc) || !ensure_profile_limits (feienc))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  /* Check HW constraints */
  if (!ensure_hw_profile_limits (feienc))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  if (feienc->profile_idc > feienc->hw_max_profile_idc)
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  /* Ensure bitrate if not set already and derive the right level to use */
  ensure_bitrate (feienc);
  if (!ensure_level (feienc))
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;

  if (feienc->profile != profile || feienc->level != level) {
    GST_DEBUG ("selected %s profile at level %s",
        gst_vaapi_utils_h264_get_profile_string (feienc->profile),
        gst_vaapi_utils_h264_get_level_string (feienc->level));
    feienc->config_changed = TRUE;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static void
reset_properties (GstVaapiFeiEncH264 * feienc)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (feienc);
  guint mb_size, i;
  guint max_reflist0_count;

  if (feienc->idr_period < base_encoder->keyframe_period)
    feienc->idr_period = base_encoder->keyframe_period;

  if (feienc->min_qp > feienc->init_qp ||
      (GST_VAAPI_ENCODER_RATE_CONTROL (feienc) == GST_VAAPI_RATECONTROL_CQP &&
          feienc->min_qp < feienc->init_qp))
    feienc->min_qp = feienc->init_qp;

  mb_size = feienc->mb_width * feienc->mb_height;
  if (feienc->num_slices > (mb_size + 1) / 2)
    feienc->num_slices = (mb_size + 1) / 2;
  g_assert (feienc->num_slices);

  if (feienc->num_bframes > (base_encoder->keyframe_period + 1) / 2)
    feienc->num_bframes = (base_encoder->keyframe_period + 1) / 2;

  /* Workaround : vaapi-intel-driver doesn't have support for
   * B-frame encode when utilizing low-power encode hardware block.
   * So Disabling b-frame encoding in low-pwer encode.
   *
   * Fixme :We should query the VAConfigAttribEncMaxRefFrames
   * instead of blindly disabling b-frame support and set b/p frame count,
   * buffer pool size etc based on that.*/
  if ((feienc->num_bframes > 0)
      && (feienc->entrypoint == GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP)) {
    GST_WARNING
        ("Disabling b-frame since the driver doesn't supporting it in low-power encode");
    feienc->num_bframes = 0;
  }

  if (feienc->num_bframes > 0 && GST_VAAPI_ENCODER_FPS_N (feienc) > 0)
    feienc->cts_offset = gst_util_uint64_scale (GST_SECOND,
        GST_VAAPI_ENCODER_FPS_D (feienc), GST_VAAPI_ENCODER_FPS_N (feienc));
  else
    feienc->cts_offset = 0;

  /* init max_frame_num, max_poc */
  feienc->log2_max_frame_num = h264_get_log2_max_frame_num (feienc->idr_period);
  g_assert (feienc->log2_max_frame_num >= 4);
  feienc->max_frame_num = (1 << feienc->log2_max_frame_num);
  feienc->log2_max_pic_order_cnt = feienc->log2_max_frame_num + 1;
  feienc->max_pic_order_cnt = (1 << feienc->log2_max_pic_order_cnt);
  feienc->idr_num = 0;

  if (feienc->num_bframes > 0) {
    if (feienc->num_ref_frames == 1) {
      GST_INFO ("num ref frames is modified as 2 as b frame is set");
      feienc->num_ref_frames = 2;
    }
    max_reflist0_count = feienc->num_ref_frames - 1;
  } else {
    max_reflist0_count = feienc->num_ref_frames;
  }
  max_reflist0_count = max_reflist0_count > 5 ? 5 : max_reflist0_count;

  for (i = 0; i < feienc->num_views; i++) {
    GstVaapiH264ViewRefPool *const ref_pool = &feienc->ref_pools[i];
    GstVaapiH264ViewReorderPool *const reorder_pool = &feienc->reorder_pools[i];

    ref_pool->max_reflist0_count = max_reflist0_count;
    ref_pool->max_reflist1_count = feienc->num_bframes > 0;
    ref_pool->max_ref_frames = ref_pool->max_reflist0_count
        + ref_pool->max_reflist1_count;

    reorder_pool->frame_index = 0;
  }

}

/* only for vaapi encoder framework checking */
static GstVaapiEncoderStatus
gst_vaapi_feienc_h264_fake_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * reconstruct,
    GstVaapiCodedBufferProxy * codedbuf_proxy,
    GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;

  if (!reconstruct || !codedbuf_proxy)
    return ret;

  if (!ensure_sequence (feienc, picture, info_to_pak))
    goto error;
  if (!ensure_misc_params (feienc, picture))
    goto error;
  if (!ensure_fei_misc_params (feienc, picture, codedbuf_proxy))
    goto error;
  if (!ensure_picture (feienc, picture, reconstruct, codedbuf_proxy,
          info_to_pak))
    goto error;
  if (!ensure_slices (feienc, picture, info_to_pak))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

error:
  g_slice_free (GstVaapiFeiInfoToPakH264, info_to_pak);
  return ret;
}

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);
  GstVaapiH264ViewReorderPool *reorder_pool;
  GstVaapiEncPicture *pic;
  guint i;

  for (i = 0; i < feienc->num_views; i++) {
    reorder_pool = &feienc->reorder_pools[i];
    reorder_pool->frame_index = 0;
    reorder_pool->cur_frame_num = 0;
    reorder_pool->cur_present_index = 0;

    while (!g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      pic = (GstVaapiEncPicture *)
          g_queue_pop_head (&reorder_pool->reorder_frame_list);
      gst_vaapi_enc_picture_unref (pic);
    }
    g_queue_clear (&reorder_pool->reorder_frame_list);
  }

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Generate "codec-data" buffer */
static GstVaapiEncoderStatus
gst_vaapi_feienc_h264_get_codec_data (GstVaapiEncoder * base_encoder,
    GstBuffer ** out_buffer_ptr)
{
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);
  GstVaapiH264ViewReorderPool *reorder_pool = NULL;
  GstVaapiEncPicture *picture;
  gboolean is_idr = FALSE;

  *output = NULL;

  /* encoding views alternatively for MVC */
  if (feienc->is_mvc) {
    /* FIXME: Use first-in-bundle flag on buffers to reset view idx? */
    if (frame)
      feienc->view_idx = frame->system_frame_number % feienc->num_views;
    else
      feienc->view_idx = (feienc->view_idx + 1) % feienc->num_views;
  }
  reorder_pool = &feienc->reorder_pools[feienc->view_idx];

  if (!frame) {
    if (reorder_pool->reorder_state != GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES)
      return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

    /* reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES
       dump B frames from queue, sometime, there may also have P frame or I frame */
    g_assert (feienc->num_bframes > 0);
    g_return_val_if_fail (!g_queue_is_empty (&reorder_pool->reorder_frame_list),
        GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN);
    picture = g_queue_pop_head (&reorder_pool->reorder_frame_list);
    g_assert (picture);
    if (g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      reorder_pool->reorder_state = GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new frame coming */
  picture = GST_VAAPI_ENC_PICTURE_NEW (H264, feienc, frame);
  if (!picture) {
    GST_WARNING ("create H264 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  ++reorder_pool->cur_present_index;
  picture->poc = ((reorder_pool->cur_present_index * 2) %
      feienc->max_pic_order_cnt);

  is_idr = (reorder_pool->frame_index == 0 ||
      reorder_pool->frame_index >= feienc->idr_period);

  /* check key frames */
  if (is_idr || GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame) ||
      (reorder_pool->frame_index %
          GST_VAAPI_ENCODER_KEYFRAME_PERIOD (feienc)) == 0) {
    ++reorder_pool->cur_frame_num;
    ++reorder_pool->frame_index;

    /* b frame enabled,  check queue of reorder_frame_list */
    if (feienc->num_bframes
        && !g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      GstVaapiEncPicture *p_pic;

      p_pic = g_queue_pop_tail (&reorder_pool->reorder_frame_list);
      set_p_frame (p_pic, feienc);
      g_queue_foreach (&reorder_pool->reorder_frame_list,
          (GFunc) set_b_frame, feienc);
      ++reorder_pool->cur_frame_num;
      set_key_frame (picture, feienc, is_idr);
      g_queue_push_tail (&reorder_pool->reorder_frame_list, picture);
      picture = p_pic;
      reorder_pool->reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES;
    } else {                    /* no b frames in queue */
      set_key_frame (picture, feienc, is_idr);
      g_assert (g_queue_is_empty (&reorder_pool->reorder_frame_list));
      if (feienc->num_bframes)
        reorder_pool->reorder_state = GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new p/b frames coming */
  ++reorder_pool->frame_index;
  if (reorder_pool->reorder_state == GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES &&
      g_queue_get_length (&reorder_pool->reorder_frame_list) <
      feienc->num_bframes) {
    g_queue_push_tail (&reorder_pool->reorder_frame_list, picture);
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;
  }

  ++reorder_pool->cur_frame_num;
  set_p_frame (picture, feienc);

  if (reorder_pool->reorder_state == GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES) {
    g_queue_foreach (&reorder_pool->reorder_frame_list, (GFunc) set_b_frame,
        feienc);
    reorder_pool->reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES;
    g_assert (!g_queue_is_empty (&reorder_pool->reorder_frame_list));
  }

end:
  g_assert (picture);
  frame = picture->frame;
  if (GST_CLOCK_TIME_IS_VALID (frame->pts))
    frame->pts += feienc->cts_offset;
  *output = picture;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (feienc);
  const guint DEFAULT_SURFACES_COUNT = 3;

  /* Maximum sizes for common headers (in bits) */
  enum
  {
    MAX_SPS_HDR_SIZE = 16473,
    MAX_VUI_PARAMS_SIZE = 210,
    MAX_HRD_PARAMS_SIZE = 4103,
    MAX_PPS_HDR_SIZE = 101,
    MAX_SLICE_HDR_SIZE = 397 + 2572 + 6670 + 2402,
  };

  if (!ensure_hw_profile (feienc))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames =
      (feienc->num_ref_frames + DEFAULT_SURFACES_COUNT) * feienc->num_views;

  /* Only YUV 4:2:0 formats are supported for now. This means that we
     have a limit of 3200 bits per macroblock. */
  /* XXX: check profile and compute RawMbBits */
  base_encoder->codedbuf_size = (GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) / 256) * 400;

  /* Account for SPS header */
  /* XXX: exclude scaling lists, MVC/SVC extensions */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE +
      MAX_VUI_PARAMS_SIZE + 2 * MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  /* XXX: exclude slice groups, scaling lists, MVC/SVC extensions */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  base_encoder->codedbuf_size += feienc->num_slices * (4 +
      GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE) / 8);

  base_encoder->context_info.entrypoint = feienc->entrypoint;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (feienc);
  GstVaapiEncoderStatus status;
  guint mb_width, mb_height;

  mb_width = (GST_VAAPI_ENCODER_WIDTH (feienc) + 15) / 16;
  mb_height = (GST_VAAPI_ENCODER_HEIGHT (feienc) + 15) / 16;
  if (mb_width != feienc->mb_width || mb_height != feienc->mb_height) {
    GST_DEBUG ("resolution: %dx%d", GST_VAAPI_ENCODER_WIDTH (feienc),
        GST_VAAPI_ENCODER_HEIGHT (feienc));
    feienc->mb_width = mb_width;
    feienc->mb_height = mb_height;
    feienc->config_changed = TRUE;
  }

  /* Take number of MVC views from input caps if provided */
  if (GST_VIDEO_INFO_MULTIVIEW_MODE (vip) ==
      GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME
      || GST_VIDEO_INFO_MULTIVIEW_MODE (vip) ==
      GST_VIDEO_MULTIVIEW_MODE_MULTIVIEW_FRAME_BY_FRAME)
    feienc->num_views = GST_VIDEO_INFO_VIEWS (vip);

  feienc->is_mvc = feienc->num_views > 1;

  status = ensure_profile_and_level (feienc);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  reset_properties (feienc);
  status = set_context_info (base_encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

struct _GstVaapiFeiEncH264Class
{
  GstVaapiEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiFeiEncH264, gst_vaapi_feienc_h264,
    GST_TYPE_VAAPI_ENCODER);

static void
gst_vaapi_feienc_h264_init (GstVaapiFeiEncH264 * feienc)
{
  guint32 i;

  /* Default encoding entrypoint */
  feienc->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  feienc->search_path = GST_VAAPI_FEI_H264_SEARCH_PATH_DEFAULT;
  feienc->len_sp = GST_VAAPI_FEI_H264_SEARCH_PATH_LENGTH_DEFAULT;
  feienc->ref_width = GST_VAAPI_FEI_H264_REF_WIDTH_DEFAULT;
  feienc->ref_height = GST_VAAPI_FEI_H264_REF_HEIGHT_DEFAULT;
  feienc->intra_part_mask = GST_VAAPI_FEI_H264_INTRA_PART_MASK_DEFAULT;
  feienc->submb_part_mask = GST_VAAPI_FEI_H264_SUB_MB_PART_MASK_DEFAULT;

  /* Multi-view coding information */
  feienc->is_mvc = FALSE;
  feienc->num_views = 1;
  feienc->view_idx = 0;

  /* default num ref frames */
  feienc->num_ref_frames = 1;
  memset (feienc->view_ids, 0, sizeof (feienc->view_ids));

  feienc->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_FEI;

  /* re-ordering  list initialize */
  for (i = 0; i < MAX_NUM_VIEWS; i++) {
    GstVaapiH264ViewReorderPool *const reorder_pool = &feienc->reorder_pools[i];
    g_queue_init (&reorder_pool->reorder_frame_list);
    reorder_pool->reorder_state = GST_VAAPI_ENC_H264_REORD_NONE;
    reorder_pool->frame_index = 0;
    reorder_pool->cur_frame_num = 0;
    reorder_pool->cur_present_index = 0;
  }
}

static void
gst_vaapi_feienc_h264_finalize (GObject * object)
{
  /*free private buffers */
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264 (object);
  GstVaapiEncPicture *pic;
  guint32 i;

  gst_buffer_replace (&feienc->sps_data, NULL);
  gst_buffer_replace (&feienc->subset_sps_data, NULL);
  gst_buffer_replace (&feienc->pps_data, NULL);

  /* re-ordering  list initialize */
  for (i = 0; i < MAX_NUM_VIEWS; i++) {
    GstVaapiH264ViewReorderPool *const reorder_pool = &feienc->reorder_pools[i];
    while (!g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      pic = (GstVaapiEncPicture *)
          g_queue_pop_head (&reorder_pool->reorder_frame_list);
      gst_vaapi_enc_picture_unref (pic);
    }
    g_queue_clear (&reorder_pool->reorder_frame_list);
  }

  G_OBJECT_CLASS (gst_vaapi_feienc_h264_parent_class)->finalize (object);
}

static void
set_view_ids (GstVaapiFeiEncH264 * const encoder, const GValue * value)
{
  guint i, j;
  guint len = gst_value_array_get_size (value);

  if (len == 0)
    goto set_default_ids;

  if (len != encoder->num_views) {
    GST_WARNING ("The view number is %d, but %d view IDs are provided. Just "
        "fallback to use default view IDs.", encoder->num_views, len);
    goto set_default_ids;
  }

  for (i = 0; i < len; i++) {
    const GValue *val = gst_value_array_get_value (value, i);
    encoder->view_ids[i] = g_value_get_uint (val);
  }

  /* check whether duplicated ID */
  for (i = 0; i < len; i++) {
    for (j = i + 1; j < len; j++) {
      if (encoder->view_ids[i] == encoder->view_ids[j]) {
        GST_WARNING ("The view %d and view %d have same view ID %d. Just "
            "fallback to use default view IDs.", i, j, encoder->view_ids[i]);
        goto set_default_ids;
      }
    }
  }

  return;

set_default_ids:
  {
    for (i = 0; i < encoder->num_views; i++)
      encoder->view_ids[i] = i;
  }
}

GstVaapiEncoderStatus
gst_vaapi_feienc_h264_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  GstVaapiFeiEncH264 *const feienc = GST_VAAPI_FEI_ENC_H264_CAST (base_encoder);

  switch (prop_id) {
    case GST_VAAPI_FEI_H264_ENC_PROP_MAX_BFRAMES:
      feienc->num_bframes = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_INIT_QP:
      feienc->init_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_MIN_QP:
      feienc->min_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_NUM_SLICES:
      feienc->num_slices = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_CABAC:
      feienc->use_cabac = g_value_get_boolean (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_DCT8X8:
      feienc->use_dct8x8 = g_value_get_boolean (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_CPB_LENGTH:
      feienc->cpb_length = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_NUM_VIEWS:
      feienc->num_views = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_VIEW_IDS:
      set_view_ids (feienc, value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_NUM_REF:
      feienc->num_ref_frames = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_NUM_MV_PREDICT_L0:
      feienc->num_mv_predictors_l0 = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_NUM_MV_PREDICT_L1:
      feienc->num_mv_predictors_l1 = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_SEARCH_WINDOW:
      feienc->search_window = g_value_get_enum (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_LEN_SP:
      feienc->len_sp = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_SEARCH_PATH:
      feienc->search_path = g_value_get_enum (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_REF_WIDTH:
      feienc->ref_width = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_REF_HEIGHT:
      feienc->ref_height = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_SUBMB_MASK:
      feienc->submb_part_mask = g_value_get_flags (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_SUBPEL_MODE:
      feienc->subpel_mode = g_value_get_enum (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_INTRA_PART_MASK:
      feienc->intra_part_mask = g_value_get_flags (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_INTRA_SAD:
      feienc->intra_sad = g_value_get_enum (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_INTER_SAD:
      feienc->inter_sad = g_value_get_enum (value);
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_ADAPT_SEARCH:
      feienc->adaptive_search = g_value_get_boolean (value) ? 1 : 0;
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_MULTI_PRED_L0:
      feienc->multi_predL0 = g_value_get_boolean (value) ? 1 : 0;
      break;
    case GST_VAAPI_FEI_H264_ENC_PROP_MULTI_PRED_L1:
      feienc->multi_predL1 = g_value_get_boolean (value) ? 1 : 0;
      break;
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static const GstVaapiEncoderClassData fei_enc_class_data = {
  .codec = GST_VAAPI_CODEC_H264,
  .packed_headers = SUPPORTED_PACKED_HEADERS,
  .rate_control_get_type = gst_vaapi_rate_control_get_type,
  .default_rate_control = DEFAULT_RATECONTROL,
  .rate_control_mask = SUPPORTED_RATECONTROLS,
  .encoder_tune_get_type = gst_vaapi_encoder_tune_get_type,
  .default_encoder_tune = GST_VAAPI_ENCODER_TUNE_NONE,
  .encoder_tune_mask = SUPPORTED_TUNE_OPTIONS,
};

static void
gst_vaapi_feienc_h264_class_init (GstVaapiFeiEncH264Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiEncoderClass *const encoder_class = GST_VAAPI_ENCODER_CLASS (klass);

  encoder_class->class_data = &fei_enc_class_data;
  encoder_class->reconfigure = gst_vaapi_feienc_h264_reconfigure;
  encoder_class->get_default_properties =
      gst_vaapi_feienc_h264_get_default_properties;
  encoder_class->reordering = gst_vaapi_feienc_h264_reordering;
  encoder_class->encode = gst_vaapi_feienc_h264_fake_encode;
  encoder_class->flush = gst_vaapi_feienc_h264_flush;
  encoder_class->set_property = gst_vaapi_feienc_h264_set_property;
  encoder_class->get_codec_data = gst_vaapi_feienc_h264_get_codec_data;
  object_class->finalize = gst_vaapi_feienc_h264_finalize;
}

/**
 * gst_vaapi_feienc_h264_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for H.264 encoding. Note that the
 * only supported output stream format is "byte-stream" format.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_feienc_h264_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_TYPE_VAAPI_FEI_ENC_H264, "display", display, NULL);
}

/**
 * gst_vaapi_feienc_h264_get_fei_properties:
 *
 * Determines the set of common and H.264 Fei specific feienc properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of feienc properties for #GstVaapiFeiEncH264,
 *   or %NULL if an error occurred.
 */
static GPtrArray *
gst_vaapi_feienc_h264_get_fei_properties (GPtrArray * props)
{
  /**
    * GstVaapiFeiEncH264:num_mv_predictors_l0:
    *
    * The number of mv predict
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_NUM_MV_PREDICT_L0,
      g_param_spec_uint ("num-mvpredict-l0",
          "Num mv predict l0",
          "Indicate how many predictors should be used for l0",
          0, 3, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:num_mv_predictors_l1:
    *
    * The number of mv predict
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_NUM_MV_PREDICT_L1,
      g_param_spec_uint ("num-mvpredict-l1",
          "Num mv predict l1",
          "Indicate how many predictors should be used for l1",
          0, 3, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:search-window:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_SEARCH_WINDOW,
      g_param_spec_enum ("search-window",
          "search window",
          "Specify one of the predefined search path",
          GST_VAAPI_TYPE_FEI_H264_SEARCH_WINDOW,
          GST_VAAPI_FEI_H264_SEARCH_WINDOW_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:len-sp:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_LEN_SP,
      g_param_spec_uint ("len-sp",
          "len sp",
          "This value defines number of search units in search path",
          1, 63, 32, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:search-path:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_SEARCH_PATH,
      g_param_spec_enum ("search-path",
          "search path",
          "Specify search path",
          GST_VAAPI_TYPE_FEI_H264_SEARCH_PATH,
          GST_VAAPI_FEI_H264_SEARCH_PATH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:ref-width:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_REF_WIDTH,
      g_param_spec_uint ("ref-width",
          "ref width",
          "Width of search region in pixel, must be multiple of 4",
          4, 64, 32, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:ref-height:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_REF_HEIGHT,
      g_param_spec_uint ("ref-height",
          "ref height",
          "Height of search region in pixel, must be multiple of 4",
          4, 32, 32, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:submb-mask:
    * Defines the bit-mask for disabling sub-partition
    *
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_SUBMB_MASK,
      g_param_spec_flags ("submbpart-mask",
          "submb part mask",
          "defines the bit-mask for disabling sub mb partition",
          GST_VAAPI_TYPE_FEI_H264_SUB_MB_PART_MASK,
          GST_VAAPI_FEI_H264_SUB_MB_PART_MASK_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:subpel-mode:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_SUBPEL_MODE,
      g_param_spec_enum ("subpel-mode",
          "subpel mode",
          "Sub pixel precision for motion estimation",
          GST_VAAPI_TYPE_FEI_H264_SUB_PEL_MODE,
          GST_VAAPI_FEI_H264_SUB_PEL_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:intrapart-mask:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_INTRA_PART_MASK,
      g_param_spec_flags ("intrapart-mask",
          "intra part mask",
          "What block and sub-block partitions are disabled for intra MBs",
          GST_VAAPI_TYPE_FEI_H264_INTRA_PART_MASK,
          GST_VAAPI_FEI_H264_INTRA_PART_MASK_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
    * GstVaapiFeiEncH264:intra-sad:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_INTRA_SAD,
      g_param_spec_enum ("intra-sad",
          "intra sad",
          "Specifies distortion measure adjustments used in the motion search SAD comparison for intra MB",
          GST_VAAPI_TYPE_FEI_H264_SAD_MODE, GST_VAAPI_FEI_H264_SAD_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:inter-sad:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_INTER_SAD,
      g_param_spec_enum ("inter-sad",
          "inter sad",
          "Specifies distortion measure adjustments used in the motion search SAD comparison for inter MB",
          GST_VAAPI_TYPE_FEI_H264_SAD_MODE, GST_VAAPI_FEI_H264_SAD_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:adaptive-search:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_ADAPT_SEARCH,
      g_param_spec_boolean ("adaptive-search",
          "adaptive-search",
          "Enable adaptive search",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:multi-predL0:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_MULTI_PRED_L0,
      g_param_spec_boolean ("multi-predL0",
          "multi predL0",
          "Enable multi prediction for ref L0 list",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
    * GstVaapiFeiEncH264:multi-predL0:
    */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_MULTI_PRED_L1,
      g_param_spec_boolean ("multi-predL1",
          "multi predL1",
          "Enable multi prediction for ref L1 list",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  return props;

}

/**
 * gst_vaapi_feienc_h264_get_default_properties:
 *
 * Determines the set of common and H.264 specific feienc properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of feienc properties for #GstVaapiFeiEncH264,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_feienc_h264_get_default_properties (void)
{
  const GstVaapiEncoderClassData *class_data = &fei_enc_class_data;
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (class_data);

  if (!props)
    return NULL;

  /**
   * GstVaapiFeiEncH264:max-bframes:
   *
   * The number of B-frames between I and P.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames", "Number of B-frames between I and P", 0, 10, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:init-qp:
   *
   * The initial quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_INIT_QP,
      g_param_spec_uint ("init-qp",
          "Initial QP", "Initial quantizer value", 1, 51, 26,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:min-qp:
   *
   * The minimum quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_MIN_QP,
      g_param_spec_uint ("min-qp",
          "Minimum QP", "Minimum quantizer value", 1, 51, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:num-slices:
   *
   * The number of slices per frame.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices",
          "Number of Slices",
          "Number of slices per frame",
          1, 200, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:cabac:
   *
   * Enable CABAC entropy coding mode for improved compression ratio,
   * at the expense that the minimum target profile is Main. Default
   * is CAVLC entropy coding mode.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_CABAC,
      g_param_spec_boolean ("cabac",
          "Enable CABAC",
          "Enable CABAC entropy coding mode",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:dct8x8:
   *
   * Enable adaptive use of 8x8 transforms in I-frames. This improves
   * the compression ratio by the minimum target profile is High.
   * Default is to use 4x4 DCT only.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_DCT8X8,
      g_param_spec_boolean ("dct8x8",
          "Enable 8x8 DCT",
          "Enable adaptive use of 8x8 transforms in I-frames",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:cpb-length:
   *
   * The size of the CPB buffer in milliseconds.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_CPB_LENGTH,
      g_param_spec_uint ("cpb-length",
          "CPB Length", "Length of the CPB buffer in milliseconds",
          1, 10000, DEFAULT_CPB_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiFeiEncH264:num-views:
   *
   * The number of views for MVC encoding .
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_NUM_VIEWS,
      g_param_spec_uint ("num-views",
          "Number of Views",
          "Number of Views for MVC encoding",
          1, MAX_NUM_VIEWS, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstVaapiFeiEncH264:view-ids:
   *
   * The view ids for MVC encoding .
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_VIEW_IDS,
      gst_param_spec_array ("view-ids",
          "View IDs", "Set of View Ids used for MVC encoding",
          g_param_spec_uint ("view-id-value", "View id value",
              "view id values used for mvc encoding", 0, MAX_VIEW_ID, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstVaapiFeiEncH264:num-ref:
   *
   * The number of reference frames.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_FEI_H264_ENC_PROP_NUM_REF,
      g_param_spec_uint ("num-ref",
          "Num Ref",
          "reference frame number",
          1, 6, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  props = gst_vaapi_feienc_h264_get_fei_properties (props);

  return props;
}

/**
 * gst_vaapi_feienc_h264_set_max_profile:
 * @feienc: a #GstVaapiFeiEncH264
 * @profile: an H.264 #GstVaapiProfile
 *
 * Notifies the @feienc to use coding tools from the supplied
 * @profile at most.
 *
 * This means that if the minimal profile derived to
 * support the specified coding tools is greater than this @profile,
 * then an error is returned when the @feienc is configured.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_feienc_h264_set_max_profile (GstVaapiFeiEncH264 * feienc,
    GstVaapiProfile profile)
{
  guint8 profile_idc;

  g_return_val_if_fail (feienc != NULL, FALSE);
  g_return_val_if_fail (profile != GST_VAAPI_PROFILE_UNKNOWN, FALSE);

  if (gst_vaapi_profile_get_codec (profile) != GST_VAAPI_CODEC_H264)
    return FALSE;

  profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
  if (!profile_idc)
    return FALSE;

  feienc->max_profile_idc = profile_idc;
  return TRUE;
}

gboolean
gst_vaapi_feienc_h264_set_ref_pool (GstVaapiFeiEncH264 * feienc,
    gpointer ref_pool_ptr)
{
  g_return_val_if_fail (feienc != NULL, FALSE);

  if (!ref_pool_ptr)
    return FALSE;

  memcpy (&feienc->ref_pools[0], ref_pool_ptr,
      sizeof (GstVaapiH264ViewRefPool) * MAX_NUM_VIEWS);

  return TRUE;
}

/**
 * gst_vaapi_feienc_h264_get_profile_and_level
 * @feienc: a #GstVaapiFeiEncH264
 * @out_profile_ptr: return location for the #GstVaapiProfile
 * @out_profile_idc_ptr: return location for the #GstVaapiLevelH264
 *
 * Queries the H.264 @feienc for the active profile and level. That
 * information is only constructed and valid after the feienc is
 * configured, i.e. after the gst_vaapi_feienc_set_codec_state()
 * function is called.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_feienc_h264_get_profile_and_idc (GstVaapiFeiEncH264 * feienc,
    GstVaapiProfile * out_profile_ptr, guint8 * out_profile_idc_ptr)
{
  g_return_val_if_fail (feienc != NULL, FALSE);

  if (!feienc->profile || !feienc->profile_idc)
    return FALSE;

  if (out_profile_ptr)
    *out_profile_ptr = feienc->profile;
  if (out_profile_idc_ptr)
    *out_profile_idc_ptr = feienc->profile_idc;
  return TRUE;
}
