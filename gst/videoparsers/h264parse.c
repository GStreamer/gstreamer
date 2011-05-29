/* GStreamer H.264 Parser
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
 *
 * Some bits C-c,C-v'ed and s/4/3 from h264parse:
 *           (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "h264parse.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (h264_parse_debug);
#define GST_CAT_DEFAULT h264_parse_debug

/* simple bitstream parser, automatically skips over
 * emulation_prevention_three_bytes. */
typedef struct
{
  const guint8 *orig_data;
  const guint8 *data;
  const guint8 *end;
  /* bitpos in the cache of next bit */
  gint head;
  /* cached bytes */
  guint64 cache;
} GstNalBs;

static void
gst_nal_bs_init (GstNalBs * bs, const guint8 * data, guint size)
{
  bs->orig_data = data;
  bs->data = data;
  bs->end = data + size;
  bs->head = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  bs->cache = 0xffffffff;
}

static inline void
gst_nal_bs_get_data (GstNalBs * bs, const guint8 ** data, guint * size)
{
  *data = bs->orig_data;
  *size = bs->end - bs->orig_data;
}

static guint32
gst_nal_bs_read (GstNalBs * bs, guint n)
{
  guint32 res = 0;
  gint shift;

  if (n == 0)
    return res;

  /* fill up the cache if we need to */
  while (bs->head < n) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (bs->data >= bs->end) {
      /* we're at the end, can't produce more than head number of bits */
      n = bs->head;
      break;
    }
    /* get the byte, this can be an emulation_prevention_three_byte that we need
     * to ignore. */
    byte = *bs->data++;
    if (check_three_byte && byte == 0x03 && ((bs->cache & 0xffff) == 0)) {
      /* next byte goes unconditionally to the cache, even if it's 0x03 */
      check_three_byte = FALSE;
      goto next_byte;
    }
    /* shift bytes in cache, moving the head bits of the cache left */
    bs->cache = (bs->cache << 8) | byte;
    bs->head += 8;
  }

  /* bring the required bits down and truncate */
  if ((shift = bs->head - n) > 0)
    res = bs->cache >> shift;
  else
    res = bs->cache;

  /* mask out required bits */
  if (n < 32)
    res &= (1 << n) - 1;

  bs->head = shift;

  return res;
}

static gboolean
gst_nal_bs_eos (GstNalBs * bs)
{
  return (bs->data >= bs->end) && (bs->head == 0);
}

/* read unsigned Exp-Golomb code */
static gint
gst_nal_bs_read_ue (GstNalBs * bs)
{
  gint i = 0;

  while (gst_nal_bs_read (bs, 1) == 0 && !gst_nal_bs_eos (bs) && i < 32)
    i++;

  return ((1 << i) - 1 + gst_nal_bs_read (bs, i));
}

/* read signed Exp-Golomb code */
static gint
gst_nal_bs_read_se (GstNalBs * bs)
{
  gint i = 0;

  i = gst_nal_bs_read_ue (bs);
  /* (-1)^(i+1) Ceil (i / 2) */
  i = (i + 1) / 2 * (i & 1 ? 1 : -1);

  return i;
}

/* end parser helper */

static void
gst_h264_params_store_nal (GstH264Params * params, GstBuffer ** store,
    gint store_size, gint id, GstNalBs * bs)
{
  const guint8 *data;
  GstBuffer *buf;
  guint size;

  if (id >= store_size) {
    GST_DEBUG_OBJECT (params->el,
        "unable to store nal, id out-of-range %d", id);
    return;
  }

  gst_nal_bs_get_data (bs, &data, &size);
  buf = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buf), data, size);

  if (store[id])
    gst_buffer_unref (store[id]);

  store[id] = buf;
}

static GstH264ParamsSPS *
gst_h264_params_get_sps (GstH264Params * params, guint8 sps_id, gboolean set)
{
  GstH264ParamsSPS *sps;

  g_return_val_if_fail (params != NULL, NULL);

  if (G_UNLIKELY (sps_id >= MAX_SPS_COUNT)) {
    GST_WARNING_OBJECT (params->el,
        "requested sps_id=%04x out of range", sps_id);
    return NULL;
  }

  sps = &params->sps_buffers[sps_id];
  if (set) {
    if (sps->valid) {
      params->sps = sps;
    } else {
      GST_WARNING_OBJECT (params->el, "invalid sps not selected");
      params->sps = NULL;
      sps = NULL;
    }
  }

  return sps;
}

static GstH264ParamsPPS *
gst_h264_params_get_pps (GstH264Params * params, guint8 pps_id, gboolean set)
{
  GstH264ParamsPPS *pps;

  g_return_val_if_fail (params != NULL, NULL);

  pps = &params->pps_buffers[pps_id];
  if (set) {
    if (pps->valid) {
      params->pps = pps;
    } else {
      GST_WARNING_OBJECT (params->el, "invalid pps not selected");
      params->pps = NULL;
      pps = NULL;
    }
  }

  return pps;
}

static gboolean
gst_h264_params_decode_sps_vui_hrd (GstH264Params * params,
    GstH264ParamsSPS * sps, GstNalBs * bs)
{
  gint sched_sel_idx;

  sps->cpb_cnt_minus1 = gst_nal_bs_read_ue (bs);
  if (sps->cpb_cnt_minus1 > 31U) {
    GST_WARNING_OBJECT (params->el, "cpb_cnt_minus1 = %d out of range",
        sps->cpb_cnt_minus1);
    return FALSE;
  }

  /* bit_rate_scale */
  gst_nal_bs_read (bs, 4);
  /* cpb_size_scale */
  gst_nal_bs_read (bs, 4);

  for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1; sched_sel_idx++) {
    /* bit_rate_value_minus1 */
    gst_nal_bs_read_ue (bs);
    /* cpb_size_value_minus1 */
    gst_nal_bs_read_ue (bs);
    /* cbr_flag */
    gst_nal_bs_read (bs, 1);
  }

  sps->initial_cpb_removal_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->cpb_removal_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->dpb_output_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->time_offset_length_minus1 = gst_nal_bs_read (bs, 5);

  return TRUE;
}

static gboolean
gst_h264_params_decode_sps_vui (GstH264Params * params, GstH264ParamsSPS * sps,
    GstNalBs * bs)
{
  if (G_UNLIKELY (!sps))
    return FALSE;

  /* aspect_ratio_info_present_flag */
  if (gst_nal_bs_read (bs, 1)) {
    /* aspect_ratio_idc */
    if (gst_nal_bs_read (bs, 8) == 255) {
      /* Extended_SAR */
      /* sar_width */
      gst_nal_bs_read (bs, 16);
      /* sar_height */
      gst_nal_bs_read (bs, 16);
    }
  }

  /* overscan_info_present_flag */
  if (gst_nal_bs_read (bs, 1)) {
    /* overscan_appropriate_flag */
    gst_nal_bs_read (bs, 1);
  }

  /* video_signal_type_present_flag */
  if (gst_nal_bs_read (bs, 1)) {
    /* video_format */
    gst_nal_bs_read (bs, 3);
    /* video_full_range_flag */
    gst_nal_bs_read (bs, 1);

    /* colour_description_present_flag */
    if (gst_nal_bs_read (bs, 1)) {
      /* colour_primaries */
      gst_nal_bs_read (bs, 8);
      /* transfer_characteristics */
      gst_nal_bs_read (bs, 8);
      /* matrix_coefficients */
      gst_nal_bs_read (bs, 8);
    }
  }

  /* chroma_loc_info_present_flag */
  if (gst_nal_bs_read (bs, 1)) {
    /* chroma_sample_loc_type_top_field */
    gst_nal_bs_read_ue (bs);
    /* chroma_sample_loc_type_bottom_field */
    gst_nal_bs_read_ue (bs);
  }

  sps->timing_info_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->timing_info_present_flag) {
    guint32 num_units_in_tick = gst_nal_bs_read (bs, 32);
    guint32 time_scale = gst_nal_bs_read (bs, 32);

    /* If any of these parameters = 0, discard all timing_info */
    if (time_scale == 0) {
      GST_WARNING_OBJECT (params->el,
          "time_scale = 0 detected in stream (incompliant to H.264 E.2.1)."
          " Discarding related info.");
    } else if (num_units_in_tick == 0) {
      GST_WARNING_OBJECT (params->el,
          "num_units_in_tick  = 0 detected in stream (incompliant to H.264 E.2.1)."
          " Discarding related info.");
    } else {
      sps->num_units_in_tick = num_units_in_tick;
      sps->time_scale = time_scale;
      sps->fixed_frame_rate_flag = gst_nal_bs_read (bs, 1);
      GST_LOG_OBJECT (params->el, "timing info: dur=%d/%d fixed=%d",
          num_units_in_tick, time_scale, sps->fixed_frame_rate_flag);
    }
  }

  sps->nal_hrd_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->nal_hrd_parameters_present_flag) {
    gst_h264_params_decode_sps_vui_hrd (params, sps, bs);
  }
  sps->vcl_hrd_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->vcl_hrd_parameters_present_flag) {
    gst_h264_params_decode_sps_vui_hrd (params, sps, bs);
  }
  if (sps->nal_hrd_parameters_present_flag
      || sps->vcl_hrd_parameters_present_flag) {
    gst_nal_bs_read (bs, 1);    /* low_delay_hrd_flag */
  }

  sps->pic_struct_present_flag = gst_nal_bs_read (bs, 1);

  /* derive framerate */
  /* FIXME verify / also handle other cases */
  if (sps->fixed_frame_rate_flag && sps->frame_mbs_only_flag &&
      !sps->pic_struct_present_flag) {
    sps->fps_num = sps->time_scale;
    sps->fps_den = sps->num_units_in_tick;
    /* picture is a frame = 2 fields */
    sps->fps_den *= 2;
    GST_LOG_OBJECT (params->el, "framerate %d/%d", sps->fps_num, sps->fps_den);
  }

  return TRUE;
}

static gboolean
gst_h264_params_decode_sps (GstH264Params * params, GstNalBs * bs)
{
  guint8 profile_idc, level_idc;
  guint8 sps_id;
  GstH264ParamsSPS *sps = NULL;
  guint subwc[] = { 1, 2, 2, 1 };
  guint subhc[] = { 1, 2, 1, 1 };
  guint chroma;
  guint fc_top, fc_bottom, fc_left, fc_right;
  gint width, height;

  profile_idc = gst_nal_bs_read (bs, 8);
  /* constraint_set0_flag */
  gst_nal_bs_read (bs, 1);
  /* constraint_set1_flag */
  gst_nal_bs_read (bs, 1);
  /* constraint_set1_flag */
  gst_nal_bs_read (bs, 1);
  /* constraint_set1_flag */
  gst_nal_bs_read (bs, 1);
  /* reserved */
  gst_nal_bs_read (bs, 4);
  level_idc = gst_nal_bs_read (bs, 8);

  sps_id = gst_nal_bs_read_ue (bs);
  sps = gst_h264_params_get_sps (params, sps_id, FALSE);
  if (G_UNLIKELY (sps == NULL))
    return FALSE;

  gst_h264_params_store_nal (params, params->sps_nals, MAX_SPS_COUNT, sps_id,
      bs);

  /* could be redefined mid stream, arrange for clear state */
  memset (sps, 0, sizeof (*sps));

  GST_LOG_OBJECT (params->el, "sps id %d", sps_id);
  sps->valid = TRUE;
  /* validate and force activate this one if it is the first SPS we see */
  if (params->sps == NULL)
    params->sps = sps;

  sps->profile_idc = profile_idc;
  sps->level_idc = level_idc;

  if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122
      || profile_idc == 244 || profile_idc == 44 ||
      profile_idc == 83 || profile_idc == 86) {
    gint scp_flag = 0;

    /* chroma_format_idc */
    if ((chroma = gst_nal_bs_read_ue (bs)) == 3) {
      /* separate_colour_plane_flag */
      sps->scp_flag = gst_nal_bs_read (bs, 1);
    }
    /* bit_depth_luma_minus8 */
    gst_nal_bs_read_ue (bs);
    /* bit_depth_chroma_minus8 */
    gst_nal_bs_read_ue (bs);
    /* qpprime_y_zero_transform_bypass_flag */
    gst_nal_bs_read (bs, 1);
    /* seq_scaling_matrix_present_flag */
    if (gst_nal_bs_read (bs, 1)) {
      gint i, j, m, d;

      m = (chroma != 3) ? 8 : 12;
      for (i = 0; i < m; i++) {
        /* seq_scaling_list_present_flag[i] */
        d = gst_nal_bs_read (bs, 1);
        if (d) {
          gint lastScale = 8, nextScale = 8, deltaScale;

          j = (i < 6) ? 16 : 64;
          for (; j > 0; j--) {
            if (nextScale != 0) {
              deltaScale = gst_nal_bs_read_se (bs);
              nextScale = (lastScale + deltaScale + 256) % 256;
            }
            if (nextScale != 0)
              lastScale = nextScale;
          }
        }
      }
    }
    if (scp_flag)
      chroma = 0;
  } else {
    /* inferred value */
    chroma = 1;
  }

  /* between 0 and 12 */
  sps->log2_max_frame_num_minus4 = gst_nal_bs_read_ue (bs);
  if (sps->log2_max_frame_num_minus4 > 12) {
    GST_WARNING_OBJECT (params->el,
        "log2_max_frame_num_minus4 = %d out of range" " [0,12]",
        sps->log2_max_frame_num_minus4);
    return FALSE;
  }

  sps->pic_order_cnt_type = gst_nal_bs_read_ue (bs);
  if (sps->pic_order_cnt_type == 0) {
    sps->log2_max_pic_order_cnt_lsb_minus4 = gst_nal_bs_read_ue (bs);
  } else if (sps->pic_order_cnt_type == 1) {
    gint d;

    /* delta_pic_order_always_zero_flag */
    gst_nal_bs_read (bs, 1);
    /* offset_for_non_ref_pic */
    gst_nal_bs_read_ue (bs);
    /* offset_for_top_to_bottom_field */
    gst_nal_bs_read_ue (bs);
    /* num_ref_frames_in_pic_order_cnt_cycle */
    d = gst_nal_bs_read_ue (bs);
    for (; d > 0; d--) {
      /* offset_for_ref_frame[i] */
      gst_nal_bs_read_ue (bs);
    }
  }

  /* max_num_ref_frames */
  gst_nal_bs_read_ue (bs);
  /* gaps_in_frame_num_value_allowed_flag */
  gst_nal_bs_read (bs, 1);
  /* pic_width_in_mbs_minus1 */
  width = gst_nal_bs_read_ue (bs);
  /* pic_height_in_map_units_minus1 */
  height = gst_nal_bs_read_ue (bs);

  sps->frame_mbs_only_flag = gst_nal_bs_read (bs, 1);
  if (!sps->frame_mbs_only_flag) {
    /* mb_adaptive_frame_field_flag */
    gst_nal_bs_read (bs, 1);
  }

  width++;
  width *= 16;
  height++;
  height *= 16 * (2 - sps->frame_mbs_only_flag);

  /* direct_8x8_inference_flag */
  gst_nal_bs_read (bs, 1);
  /* frame_cropping_flag */
  if (gst_nal_bs_read (bs, 1)) {
    /* frame_crop_left_offset */
    fc_left = gst_nal_bs_read_ue (bs);
    /* frame_crop_right_offset */
    fc_right = gst_nal_bs_read_ue (bs);
    /* frame_crop_top_offset */
    fc_top = gst_nal_bs_read_ue (bs);
    /* frame_crop_bottom_offset */
    fc_bottom = gst_nal_bs_read_ue (bs);
  } else {
    fc_left = fc_right = fc_top = fc_bottom = 0;
  }

  GST_LOG_OBJECT (params->el, "decoding SPS: profile_idc = %d, "
      "level_idc = %d, sps_id = %d, pic_order_cnt_type = %d, "
      "frame_mbs_only_flag = %d",
      sps->profile_idc, sps->level_idc, sps_id, sps->pic_order_cnt_type,
      sps->frame_mbs_only_flag);

  /* calculate width and height */
  GST_LOG_OBJECT (params->el, "initial width=%d, height=%d", width, height);
  GST_LOG_OBJECT (params->el, "crop (%d,%d)(%d,%d)",
      fc_left, fc_top, fc_right, fc_bottom);
  if (chroma > 3) {
    GST_LOG_OBJECT (params->el, "chroma=%d in SPS is out of range", chroma);
    return FALSE;
  }
  width -= (fc_left + fc_right) * subwc[chroma];
  height -=
      (fc_top + fc_bottom) * subhc[chroma] * (2 - sps->frame_mbs_only_flag);
  if (width < 0 || height < 0) {
    GST_WARNING_OBJECT (params->el, "invalid width/height in SPS");
    return FALSE;
  }
  GST_LOG_OBJECT (params->el, "final width=%u, height=%u", width, height);
  sps->width = width;
  sps->height = height;

  sps->vui_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->vui_parameters_present_flag) {
    /* discard parsing problem */
    gst_h264_params_decode_sps_vui (params, sps, bs);
  }

  return TRUE;
}

static gboolean
gst_h264_params_decode_pps (GstH264Params * params, GstNalBs * bs)
{
  gint pps_id;
  GstH264ParamsPPS *pps = NULL;

  pps_id = gst_nal_bs_read_ue (bs);
  if (G_UNLIKELY (pps_id >= MAX_PPS_COUNT)) {
    GST_WARNING_OBJECT (params->el,
        "requested pps_id=%04x out of range", pps_id);
    return FALSE;
  }


  pps = gst_h264_params_get_pps (params, pps_id, FALSE);
  if (G_UNLIKELY (pps == NULL))
    return FALSE;

  /* validate and set */
  pps->valid = TRUE;
  params->pps = pps;

  gst_h264_params_store_nal (params, params->pps_nals, MAX_PPS_COUNT, pps_id,
      bs);

  pps->sps_id = gst_nal_bs_read_ue (bs);
  GST_LOG_OBJECT (params->el, "pps %d referencing sps %d", pps_id, pps->sps_id);

  /* activate referenced sps */
  if (!gst_h264_params_get_sps (params, pps->sps_id, TRUE))
    return FALSE;

  /* not parsing the rest for the time being */
  return TRUE;
}

static gboolean
gst_h264_params_decode_sei_buffering_period (GstH264Params * params,
    GstNalBs * bs)
{
#ifdef EXTRA_PARSE
  guint8 sps_id;
  gint sched_sel_idx;
  GstH264ParamsSPS *sps;

  sps_id = gst_nal_bs_read_ue (bs);
  sps = gst_h264_params_get_sps (params, sps_id, TRUE);
  if (G_UNLIKELY (sps == NULL))
    return FALSE;

  if (sps->nal_hrd_parameters_present_flag) {
    for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1;
        sched_sel_idx++) {
      params->initial_cpb_removal_delay[sched_sel_idx]
          = gst_nal_bs_read (bs,
          sps->initial_cpb_removal_delay_length_minus1 + 1);
      /* initial_cpb_removal_delay_offset */
      gst_nal_bs_read (bs, sps->initial_cpb_removal_delay_length_minus1 + 1);
    }
  }

  if (sps->vcl_hrd_parameters_present_flag) {
    for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1;
        sched_sel_idx++) {
      params->initial_cpb_removal_delay[sched_sel_idx]
          = gst_nal_bs_read (bs,
          sps->initial_cpb_removal_delay_length_minus1 + 1);
      /* initial_cpb_removal_delay_offset */
      gst_nal_bs_read (bs, sps->initial_cpb_removal_delay_length_minus1 + 1);
    }
  }
#endif

  if (params->ts_trn_nb == GST_CLOCK_TIME_NONE ||
      params->dts == GST_CLOCK_TIME_NONE)
    params->ts_trn_nb = 0;
  else
    params->ts_trn_nb = params->dts;

  GST_LOG_OBJECT (params->el,
      "new buffering period; ts_trn_nb updated: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (params->ts_trn_nb));

  return 0;
}

static gboolean
gst_h264_params_decode_sei_picture_timing (GstH264Params * params,
    GstNalBs * bs)
{
  GstH264ParamsSPS *sps = params->sps;

  if (sps == NULL) {
    GST_WARNING_OBJECT (params->el,
        "SPS == NULL; delayed decoding of picture timing info not implemented ");
    return FALSE;
  }

  if (sps->nal_hrd_parameters_present_flag
      || sps->vcl_hrd_parameters_present_flag) {
    params->sei_cpb_removal_delay =
        gst_nal_bs_read (bs, sps->cpb_removal_delay_length_minus1 + 1);
    /* sei_dpb_output_delay */
    gst_nal_bs_read (bs, sps->dpb_output_delay_length_minus1 + 1);
  }

  if (sps->pic_struct_present_flag) {
#ifdef EXTRA_PARSE
    /* pic_struct to NumClockTS lookup table */
    static const guint8 sei_num_clock_ts_table[9] =
        { 1, 1, 1, 2, 2, 3, 3, 2, 3 };
    guint i, num_clock_ts;
    guint sei_ct_type = 0;
#endif

    params->sei_pic_struct = gst_nal_bs_read (bs, 4);
    GST_LOG_OBJECT (params, "pic_struct:%d", params->sei_pic_struct);
    if (params->sei_pic_struct > SEI_PIC_STRUCT_FRAME_TRIPLING)
      return FALSE;

#ifdef EXTRA_PARSE
    num_clock_ts = sei_num_clock_ts_table[params->sei_pic_struct];

    for (i = 0; i < num_clock_ts; i++) {
      /* clock_timestamp_flag */
      if (gst_nal_bs_read (bs, 1)) {
        guint full_timestamp_flag;

        sei_ct_type |= 1 << gst_nal_bs_read (bs, 2);
        /* nuit_field_based_flag */
        gst_nal_bs_read (bs, 1);
        /* counting_type */
        gst_nal_bs_read (bs, 5);
        full_timestamp_flag = gst_nal_bs_read (bs, 1);
        /* discontinuity_flag */
        gst_nal_bs_read (bs, 1);
        /* cnt_dropped_flag */
        gst_nal_bs_read (bs, 1);
        /* n_frames */
        gst_nal_bs_read (bs, 8);
        if (full_timestamp_flag) {
          /* seconds_value 0..59 */
          gst_nal_bs_read (bs, 6);
          /* minutes_value 0..59 */
          gst_nal_bs_read (bs, 6);
          /* hours_value 0..23 */
          gst_nal_bs_read (bs, 5);
        } else {
          /* seconds_flag */
          if (gst_nal_bs_read (bs, 1)) {
            /* seconds_value range 0..59 */
            gst_nal_bs_read (bs, 6);
            /* minutes_flag */
            if (gst_nal_bs_read (bs, 1)) {
              /* minutes_value 0..59 */
              gst_nal_bs_read (bs, 6);
              /* hours_flag */
              if (gst_nal_bs_read (bs, 1))
                /* hours_value 0..23 */
                gst_nal_bs_read (bs, 5);
            }
          }
        }
        if (sps->time_offset_length_minus1 >= 0) {
          /* time_offset */
          gst_nal_bs_read (bs, sps->time_offset_length_minus1 + 1);
        }
      }
    }

    GST_LOG_OBJECT (params, "ct_type:%X", sei_ct_type);
#endif
  }

  return TRUE;
}

static gboolean
gst_h264_params_decode_sei (GstH264Params * params, GstNalBs * bs)
{
  guint8 tmp;
  GstH264ParamsSEIPayloadType payloadType = 0;
  gint8 payloadSize = 0;

  do {
    tmp = gst_nal_bs_read (bs, 8);
    payloadType += tmp;
  } while (tmp == 255);
  do {
    tmp = gst_nal_bs_read (bs, 8);
    payloadSize += tmp;
  } while (tmp == 255);

  GST_LOG_OBJECT (params->el,
      "SEI message received: payloadType = %d, payloadSize = %d bytes",
      payloadType, payloadSize);

  switch (payloadType) {
    case SEI_BUF_PERIOD:
      if (!gst_h264_params_decode_sei_buffering_period (params, bs))
        return FALSE;
      break;
    case SEI_PIC_TIMING:
      /* TODO: According to H264 D2.2 Note1, it might be the case that the
       * picture timing SEI message is encountered before the corresponding SPS
       * is specified. Need to hold down the message and decode it later.  */
      if (!gst_h264_params_decode_sei_picture_timing (params, bs))
        return FALSE;
      break;
    default:
      GST_LOG_OBJECT (params->el,
          "SEI message of payloadType = %d is received but not parsed",
          payloadType);
      break;
  }

  return TRUE;
}

static gboolean
gst_h264_params_decode_slice_header (GstH264Params * params, GstNalBs * bs)
{
  GstH264ParamsSPS *sps;
  GstH264ParamsPPS *pps;
  guint8 pps_id;

  params->first_mb_in_slice = gst_nal_bs_read_ue (bs);
  params->slice_type = gst_nal_bs_read_ue (bs);

  pps_id = gst_nal_bs_read_ue (bs);
  GST_LOG_OBJECT (params->el, "slice header references pps id %d", pps_id);
  pps = gst_h264_params_get_pps (params, pps_id, TRUE);
  if (G_UNLIKELY (pps == NULL))
    return FALSE;
  sps = gst_h264_params_get_sps (params, pps->sps_id, TRUE);
  if (G_UNLIKELY (sps == NULL))
    return FALSE;

  if (sps->scp_flag) {
    /* colour_plane_id */
    gst_nal_bs_read (bs, 2);
  }

  /* frame num */
  gst_nal_bs_read (bs, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

  if (!sps->frame_mbs_only_flag) {
    params->field_pic_flag = gst_nal_bs_read (bs, 1);
    if (params->field_pic_flag)
      params->bottom_field_flag = gst_nal_bs_read (bs, 1);
  }

  /* not parsing the rest for the time being */
  return TRUE;
}

/* only payload in @data */
gboolean
gst_h264_params_parse_nal (GstH264Params * params, guint8 * data, gint size)
{
  GstH264ParamsNalUnitType nal_type;
  GstNalBs bs;
  gint nal_ref_idc;
  gboolean res = TRUE;

  g_return_val_if_fail (params != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size != 0, FALSE);

  nal_type = (data[0] & 0x1f);
  nal_ref_idc = (data[0] & 0x60) >> 5;

  GST_LOG_OBJECT (params->el, "NAL type: %d, ref_idc: %d", nal_type,
      nal_ref_idc);

  gst_nal_bs_init (&bs, data + 1, size - 1);
  /* optimality HACK */
  bs.orig_data = data;

  /* first parse some things needed to get to the frame type */
  switch (nal_type) {
    case NAL_SLICE:
    case NAL_SLICE_DPA:
    case NAL_SLICE_DPB:
    case NAL_SLICE_DPC:
    case NAL_SLICE_IDR:
    {
      gint first_mb_in_slice, slice_type;

      gst_h264_params_decode_slice_header (params, &bs);
      first_mb_in_slice = params->first_mb_in_slice;
      slice_type = params->slice_type;

      GST_LOG_OBJECT (params->el, "first MB: %d, slice type: %d",
          first_mb_in_slice, slice_type);

      switch (slice_type) {
        case 0:
        case 5:
        case 3:
        case 8:                /* SP */
          /* P frames */
          GST_LOG_OBJECT (params->el, "we have a P slice");
          break;
        case 1:
        case 6:
          /* B frames */
          GST_LOG_OBJECT (params->el, "we have a B slice");
          break;
        case 2:
        case 7:
        case 4:
        case 9:
          /* I frames */
          GST_LOG_OBJECT (params->el, "we have an I slice");
          break;
      }
      break;
    }
    case NAL_SEI:
      GST_LOG_OBJECT (params->el, "SEI NAL");
      res = gst_h264_params_decode_sei (params, &bs);
      break;
    case NAL_SPS:
      GST_LOG_OBJECT (params->el, "SPS NAL");
      res = gst_h264_params_decode_sps (params, &bs);
      break;
    case NAL_PPS:
      GST_LOG_OBJECT (params->el, "PPS NAL");
      res = gst_h264_params_decode_pps (params, &bs);
      break;
    case NAL_AU_DELIMITER:
      GST_LOG_OBJECT (params->el, "AU delimiter NAL");
      break;
    default:
      GST_LOG_OBJECT (params->el, "unparsed NAL");
      break;
  }

  return res;
}

void
gst_h264_params_get_timestamp (GstH264Params * params,
    GstClockTime * out_ts, GstClockTime * out_dur, gboolean frame)
{
  GstH264ParamsSPS *sps = params->sps;
  GstClockTime upstream;
  gint duration = 1;

  g_return_if_fail (out_dur != NULL);
  g_return_if_fail (out_ts != NULL);

  upstream = *out_ts;

  if (!frame) {
    GST_LOG_OBJECT (params->el, "no frame data ->  0 duration");
    *out_dur = 0;
    goto exit;
  } else {
    *out_ts = upstream;
  }

  if (!sps) {
    GST_DEBUG_OBJECT (params->el, "referred SPS invalid");
    goto exit;
  } else if (!sps->timing_info_present_flag) {
    GST_DEBUG_OBJECT (params->el,
        "unable to compute timestamp: timing info not present");
    goto exit;
  } else if (sps->time_scale == 0) {
    GST_DEBUG_OBJECT (params->el,
        "unable to compute timestamp: time_scale = 0 "
        "(this is forbidden in spec; bitstream probably contains error)");
    goto exit;
  }

  if (sps->pic_struct_present_flag && params->sei_pic_struct != (guint8) - 1) {
    /* Note that when h264parse->sei_pic_struct == -1 (unspecified), there
     * are ways to infer its value. This is related to computing the
     * TopFieldOrderCnt and BottomFieldOrderCnt, which looks
     * complicated and thus not implemented for the time being. Yet
     * the value we have here is correct for many applications
     */
    switch (params->sei_pic_struct) {
      case SEI_PIC_STRUCT_TOP_FIELD:
      case SEI_PIC_STRUCT_BOTTOM_FIELD:
        duration = 1;
        break;
      case SEI_PIC_STRUCT_FRAME:
      case SEI_PIC_STRUCT_TOP_BOTTOM:
      case SEI_PIC_STRUCT_BOTTOM_TOP:
        duration = 2;
        break;
      case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
      case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
        duration = 3;
        break;
      case SEI_PIC_STRUCT_FRAME_DOUBLING:
        duration = 4;
        break;
      case SEI_PIC_STRUCT_FRAME_TRIPLING:
        duration = 6;
        break;
      default:
        GST_DEBUG_OBJECT (params,
            "h264parse->sei_pic_struct of unknown value %d. Not parsed",
            params->sei_pic_struct);
        break;
    }
  } else {
    duration = params->field_pic_flag ? 1 : 2;
  }

  GST_LOG_OBJECT (params->el, "frame tick duration %d", duration);

  /*
   * h264parse.264 C.1.2 Timing of coded picture removal (equivalent to DTS):
   * Tr,n(0) = initial_cpb_removal_delay[ SchedSelIdx ] / 90000
   * Tr,n(n) = Tr,n(nb) + Tc * cpb_removal_delay(n)
   * where
   * Tc = num_units_in_tick / time_scale
   */

  if (params->ts_trn_nb != GST_CLOCK_TIME_NONE) {
    GST_LOG_OBJECT (params->el, "buffering based ts");
    /* buffering period is present */
    if (upstream != GST_CLOCK_TIME_NONE) {
      /* If upstream timestamp is valid, we respect it and adjust current
       * reference point */
      params->ts_trn_nb = upstream -
          (GstClockTime) gst_util_uint64_scale_int
          (params->sei_cpb_removal_delay * GST_SECOND,
          sps->num_units_in_tick, sps->time_scale);
    } else {
      /* If no upstream timestamp is given, we write in new timestamp */
      upstream = params->dts = params->ts_trn_nb +
          (GstClockTime) gst_util_uint64_scale_int
          (params->sei_cpb_removal_delay * GST_SECOND,
          sps->num_units_in_tick, sps->time_scale);
    }
  } else {
    GstClockTime dur;

    GST_LOG_OBJECT (params->el, "duration based ts");
    /* naive method: no removal delay specified
     * track upstream timestamp and provide best guess frame duration */
    dur = gst_util_uint64_scale_int (duration * GST_SECOND,
        sps->num_units_in_tick, sps->time_scale);
    /* sanity check */
    if (dur < GST_MSECOND) {
      GST_DEBUG_OBJECT (params->el, "discarding dur %" GST_TIME_FORMAT,
          GST_TIME_ARGS (dur));
    } else {
      *out_dur = dur;
    }
  }

exit:
  if (GST_CLOCK_TIME_IS_VALID (upstream))
    *out_ts = params->dts = upstream;

  if (GST_CLOCK_TIME_IS_VALID (*out_dur) &&
      GST_CLOCK_TIME_IS_VALID (params->dts))
    params->dts += *out_dur;
}

void
gst_h264_params_create (GstH264Params ** _params, GstElement * element)
{
  GstH264Params *params;

  g_return_if_fail (_params != NULL);

  params = g_new0 (GstH264Params, 1);
  params->el = element;

  params->dts = GST_CLOCK_TIME_NONE;
  params->ts_trn_nb = GST_CLOCK_TIME_NONE;

  *_params = params;
}

void
gst_h264_params_free (GstH264Params * params)
{
  gint i;

  g_return_if_fail (params != NULL);

  for (i = 0; i < MAX_SPS_COUNT; i++)
    gst_buffer_replace (&params->sps_nals[i], NULL);
  for (i = 0; i < MAX_PPS_COUNT; i++)
    gst_buffer_replace (&params->pps_nals[i], NULL);

  g_free (params);
}
