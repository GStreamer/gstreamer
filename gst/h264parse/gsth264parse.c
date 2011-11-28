/* GStreamer h264 parser
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
 *           (C) 2009 Mark Nauwelaerts <mnauw users sf net>
 *           (C) 2009 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 *
 * gsth264parse.c:
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

#include <stdlib.h>
#include <string.h>

#include <gst/base/gstbytewriter.h>

#include "gsth264parse.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

GST_DEBUG_CATEGORY_STATIC (h264_parse_debug);
#define GST_CAT_DEFAULT h264_parse_debug

#define DEFAULT_SPLIT_PACKETIZED     FALSE
#define DEFAULT_ACCESS_UNIT          FALSE
#define DEFAULT_OUTPUT_FORMAT        GST_H264_PARSE_FORMAT_INPUT
#define DEFAULT_CONFIG_INTERVAL      (0)

enum
{
  PROP_0,
  PROP_SPLIT_PACKETIZED,
  PROP_ACCESS_UNIT,
  PROP_CONFIG_INTERVAL,
  PROP_OUTPUT_FORMAT,
  PROP_LAST
};

enum
{
  GST_H264_PARSE_FORMAT_SAMPLE = 0,
  GST_H264_PARSE_FORMAT_BYTE,
  GST_H264_PARSE_FORMAT_INPUT
};

#define GST_H264_PARSE_FORMAT_TYPE (gst_h264_parse_format_get_type())
static GType
gst_h264_parse_format_get_type (void)
{
  static GType format_type = 0;

  static const GEnumValue format_types[] = {
    {GST_H264_PARSE_FORMAT_SAMPLE, "AVC Sample Format", "sample"},
    {GST_H264_PARSE_FORMAT_BYTE, "Bytestream Format", "byte"},
    {GST_H264_PARSE_FORMAT_INPUT, "Input Format", "input"},
    {0, NULL, NULL}
  };

  if (!format_type) {
    format_type = g_enum_register_static ("GstH264ParseFormat", format_types);
  }
  return format_type;
}

typedef enum
{
  NAL_UNKNOWN = 0,
  NAL_SLICE = 1,
  NAL_SLICE_DPA = 2,
  NAL_SLICE_DPB = 3,
  NAL_SLICE_DPC = 4,
  NAL_SLICE_IDR = 5,
  NAL_SEI = 6,
  NAL_SPS = 7,
  NAL_PPS = 8,
  NAL_AU_DELIMITER = 9,
  NAL_SEQ_END = 10,
  NAL_STREAM_END = 11,
  NAL_FILTER_DATA = 12
} GstNalUnitType;

/* small linked list implementation to allocate the list entry and the data in
 * one go */
struct _GstNalList
{
  GstNalList *next;

  gint nal_type;
  gint nal_ref_idc;
  gint first_mb_in_slice;
  gint slice_type;
  gboolean slice;
  gboolean i_frame;

  GstBuffer *buffer;
};

static GstNalList *
gst_nal_list_new (GstBuffer * buffer)
{
  GstNalList *new_list;

  new_list = g_slice_new0 (GstNalList);
  new_list->buffer = buffer;

  return new_list;
}

static GstNalList *
gst_nal_list_prepend_link (GstNalList * list, GstNalList * link)
{
  link->next = list;

  return link;
}

static GstNalList *
gst_nal_list_delete_head (GstNalList * list)
{
  if (list) {
    GstNalList *old = list;

    list = list->next;

    g_slice_free (GstNalList, old);
  }
  return list;
}

/* simple bitstream parser, automatically skips over
 * emulation_prevention_three_bytes. */
typedef struct
{
  const guint8 *data;
  const guint8 *end;
  gint head;                    /* bitpos in the cache of next bit */
  guint64 cache;                /* cached bytes */
} GstNalBs;

static void
gst_nal_bs_init (GstNalBs * bs, const guint8 * data, guint size)
{
  bs->data = data;
  bs->end = data + size;
  bs->head = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  bs->cache = 0xffffffff;
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


/* SEI type */
typedef enum
{
  SEI_BUF_PERIOD = 0,
  SEI_PIC_TIMING = 1
      /* and more...  */
} GstSeiPayloadType;

/* SEI pic_struct type */
typedef enum
{
  SEI_PIC_STRUCT_FRAME = 0,     /* 0: %frame */
  SEI_PIC_STRUCT_TOP_FIELD = 1, /* 1: top field */
  SEI_PIC_STRUCT_BOTTOM_FIELD = 2,      /* 2: bottom field */
  SEI_PIC_STRUCT_TOP_BOTTOM = 3,        /* 3: top field, bottom field, in that order */
  SEI_PIC_STRUCT_BOTTOM_TOP = 4,        /* 4: bottom field, top field, in that order */
  SEI_PIC_STRUCT_TOP_BOTTOM_TOP = 5,    /* 5: top field, bottom field, top field repeated, in that order */
  SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM = 6, /* 6: bottom field, top field, bottom field repeated, in that order */
  SEI_PIC_STRUCT_FRAME_DOUBLING = 7,    /* 7: %frame doubling */
  SEI_PIC_STRUCT_FRAME_TRIPLING = 8     /* 8: %frame tripling */
} GstSeiPicStructType;

/* pic_struct to NumClockTS lookup table */
static const guint8 sei_num_clock_ts_table[9] = {
  1, 1, 1, 2, 2, 3, 3, 2, 3
};

#define Extended_SAR 255

/* SPS: sequential parameter sets */
struct _GstH264Sps
{
  guint8 profile_idc;
  guint8 level_idc;

  guint8 sps_id;

  guint8 pic_order_cnt_type;

  guint8 log2_max_frame_num_minus4;
  gboolean frame_mbs_only_flag;
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  gboolean frame_cropping_flag;

  /* VUI parameters */
  gboolean vui_parameters_present_flag;

  gboolean timing_info_present_flag;
  guint32 num_units_in_tick;
  guint32 time_scale;
  gboolean fixed_frame_rate_flag;

  gboolean nal_hrd_parameters_present_flag;
  gboolean vcl_hrd_parameters_present_flag;
  /* hrd parameters */
  guint8 cpb_cnt_minus1;
  gint initial_cpb_removal_delay_length_minus1; /* initial_cpb_removal_delay_length_minus1 */
  gint cpb_removal_delay_length_minus1; /* cpb_removal_delay_length_minus1 */
  gint dpb_output_delay_length_minus1;  /* dpb_output_delay_length_minus1 */
  gboolean time_offset_length_minus1;

  gboolean pic_struct_present_flag;
  /* And more...  */

  /* derived values */
  gint width, height;
};
/* PPS: pic parameter sets */
struct _GstH264Pps
{
  guint8 pps_id;
  guint8 sps_id;
};

static GstH264Sps *
gst_h264_parse_get_sps (GstH264Parse * h, guint8 sps_id)
{
  GstH264Sps *sps;
  g_return_val_if_fail (h != NULL, NULL);

  if (sps_id >= MAX_SPS_COUNT) {
    GST_DEBUG_OBJECT (h, "requested sps_id=%04x out of range", sps_id);
    return NULL;
  }
  sps = h->sps_buffers[sps_id];
  if (sps == NULL) {
    GST_DEBUG_OBJECT (h, "Creating sps with sps_id=%04x", sps_id);
    sps = h->sps_buffers[sps_id] = g_slice_new0 (GstH264Sps);
    if (sps == NULL) {
      GST_DEBUG_OBJECT (h, "Allocation failed!");
    }
  }

  h->sps = h->sps_buffers[sps_id] = sps;
  return sps;
}

static GstH264Pps *
gst_h264_parse_get_pps (GstH264Parse * h, guint8 pps_id)
{
  GstH264Pps *pps;
  g_return_val_if_fail (h != NULL, NULL);

  pps = h->pps_buffers[pps_id];
  if (pps == NULL) {
    GST_DEBUG_OBJECT (h, "Creating pps with pps_id=%04x", pps_id);
    pps = g_slice_new0 (GstH264Pps);
    if (pps == NULL) {
      GST_DEBUG_OBJECT (h, "Failed!");
    }
  }

  h->pps = h->pps_buffers[pps_id] = pps;
  return pps;
}

/* decode hrd parameters */
static gboolean
gst_vui_decode_hrd_parameters (GstH264Parse * h, GstNalBs * bs)
{
  GstH264Sps *sps = h->sps;
  gint sched_sel_idx;

  sps->cpb_cnt_minus1 = gst_nal_bs_read_ue (bs);
  if (sps->cpb_cnt_minus1 > 31U) {
    GST_ERROR_OBJECT (h, "cpb_cnt_minus1 = %d out of range",
        sps->cpb_cnt_minus1);
    return FALSE;
  }

  gst_nal_bs_read (bs, 4);      /* bit_rate_scale */
  gst_nal_bs_read (bs, 4);      /* cpb_size_scale */

  for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1; sched_sel_idx++) {
    gst_nal_bs_read_ue (bs);    /* bit_rate_value_minus1 */
    gst_nal_bs_read_ue (bs);    /* cpb_size_value_minus1 */
    gst_nal_bs_read (bs, 1);    /* cbr_flag */
  }

  sps->initial_cpb_removal_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->cpb_removal_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->dpb_output_delay_length_minus1 = gst_nal_bs_read (bs, 5);
  sps->time_offset_length_minus1 = gst_nal_bs_read (bs, 5);

  return TRUE;
}

/* decode vui parameters */
static gboolean
gst_sps_decode_vui (GstH264Parse * h, GstNalBs * bs)
{
  GstH264Sps *sps = h->sps;

  if (gst_nal_bs_read (bs, 1)) {        /* aspect_ratio_info_present_flag */
    if (gst_nal_bs_read (bs, 8) == Extended_SAR) {      /* aspect_ratio_idc */
      gst_nal_bs_read (bs, 16); /* sar_width */
      gst_nal_bs_read (bs, 16); /* sar_height */
    }
  }

  if (gst_nal_bs_read (bs, 1)) {        /* overscan_info_present_flag */
    gst_nal_bs_read (bs, 1);    /* overscan_appropriate_flag */
  }

  if (gst_nal_bs_read (bs, 1)) {        /* video_signal_type_present_flag */
    gst_nal_bs_read (bs, 3);    /* video_format */
    gst_nal_bs_read (bs, 1);    /* video_full_range_flag */

    if (gst_nal_bs_read (bs, 1)) {      /* colour_description_present_flag */
      gst_nal_bs_read (bs, 8);  /* colour_primaries */
      gst_nal_bs_read (bs, 8);  /* transfer_characteristics */
      gst_nal_bs_read (bs, 8);  /* matrix_coefficients */
    }
  }

  if (gst_nal_bs_read (bs, 1)) {        /* chroma_loc_info_present_flag */
    gst_nal_bs_read_ue (bs);    /* chroma_sample_loc_type_top_field */
    gst_nal_bs_read_ue (bs);    /* chroma_sample_loc_type_bottom_field */
  }

  /*
     GST_DEBUG_OBJECT (h,
     "aspect_ratio_info_present_flag = %d, "
     "overscan_info_present_flag = %d, "
     "video_signal_type_present_flag = %d, "
     "chroma_loc_info_present_flag = %d\n",
     sps->aspect_ratio_info_present_flag, sps->overscan_info_present_flag,
     sps->video_signal_type_present_flag, sps->chroma_loc_info_present_flag);
   */

  sps->timing_info_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->timing_info_present_flag) {
    guint32 num_units_in_tick = gst_nal_bs_read (bs, 32);
    guint32 time_scale = gst_nal_bs_read (bs, 32);

    /* If any of these parameters = 0, discard all timing_info */
    if (time_scale == 0) {
      GST_WARNING_OBJECT (h,
          "time_scale = 0 detected in stream (incompliant to H.264 E.2.1)."
          " Discarding related info.");
    } else if (num_units_in_tick == 0) {
      GST_WARNING_OBJECT (h,
          "num_units_in_tick  = 0 detected in stream (incompliant to H.264 E.2.1)."
          " Discarding related info.");
    } else {
      sps->num_units_in_tick = num_units_in_tick;
      sps->time_scale = time_scale;
      sps->fixed_frame_rate_flag = gst_nal_bs_read (bs, 1);
      GST_DEBUG_OBJECT (h, "timing info: dur=%d/%d fixed=%d",
          num_units_in_tick, time_scale, sps->fixed_frame_rate_flag);
    }
  }

  sps->nal_hrd_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->nal_hrd_parameters_present_flag) {
    gst_vui_decode_hrd_parameters (h, bs);
  }
  sps->vcl_hrd_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->vcl_hrd_parameters_present_flag) {
    gst_vui_decode_hrd_parameters (h, bs);
  }
  if (sps->nal_hrd_parameters_present_flag
      || sps->vcl_hrd_parameters_present_flag) {
    gst_nal_bs_read (bs, 1);    /* low_delay_hrd_flag */
  }

  sps->pic_struct_present_flag = gst_nal_bs_read (bs, 1);


#if 0
  /* Not going down anymore */

  if (gst_nal_bs_read (bs, 1)) {        /* bitstream_restriction_flag */
    gst_nal_bs_read (bs, 1);    /* motion_vectors_over_pic_boundaries_flag */
    gst_nal_bs_read_ue (bs);    /* max_bytes_per_pic_denom */
    gst_nal_bs_read_ue (bs);    /* max_bits_per_mb_denom */
    gst_nal_bs_read_ue (bs);    /* log2_max_mv_length_horizontal */
    gst_nal_bs_read_ue (bs);    /* log2_max_mv_length_vertical */
    gst_nal_bs_read_ue (bs);    /* num_reorder_frames */
    gst_nal_bs_read_ue (bs);    /* max_dec_frame_buffering */
  }
#endif

  return TRUE;
}

/* decode sequential parameter sets */
static gboolean
gst_nal_decode_sps (GstH264Parse * h, GstNalBs * bs)
{
  guint8 profile_idc, level_idc;
  guint8 sps_id;
  GstH264Sps *sps = NULL;
  guint subwc[] = { 1, 2, 2, 1 };
  guint subhc[] = { 1, 2, 1, 1 };
  guint chroma;
  guint fc_top, fc_bottom, fc_left, fc_right;
  gint width, height;

  profile_idc = gst_nal_bs_read (bs, 8);
  gst_nal_bs_read (bs, 1);      /* constraint_set0_flag */
  gst_nal_bs_read (bs, 1);      /* constraint_set1_flag */
  gst_nal_bs_read (bs, 1);      /* constraint_set2_flag */
  gst_nal_bs_read (bs, 1);      /* constraint_set3_flag */
  gst_nal_bs_read (bs, 4);      /* reserved */
  level_idc = gst_nal_bs_read (bs, 8);

  sps_id = gst_nal_bs_read_ue (bs);
  sps = gst_h264_parse_get_sps (h, sps_id);
  if (sps == NULL) {
    return FALSE;
  }
  sps->profile_idc = profile_idc;
  sps->level_idc = level_idc;

  if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122
      || profile_idc == 244 || profile_idc == 44 ||
      profile_idc == 83 || profile_idc == 86) {
    gint scp_flag = 0;

    if ((chroma = gst_nal_bs_read_ue (bs)) == 3) {      /* chroma_format_idc */
      scp_flag = gst_nal_bs_read (bs, 1);       /* separate_colour_plane_flag */
    }
    gst_nal_bs_read_ue (bs);    /* bit_depth_luma_minus8 */
    gst_nal_bs_read_ue (bs);    /* bit_depth_chroma_minus8 */
    gst_nal_bs_read (bs, 1);    /* qpprime_y_zero_transform_bypass_flag */
    if (gst_nal_bs_read (bs, 1)) {      /* seq_scaling_matrix_present_flag */
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

  sps->log2_max_frame_num_minus4 = gst_nal_bs_read_ue (bs);     /* between 0 and 12 */
  if (sps->log2_max_frame_num_minus4 > 12) {
    GST_DEBUG_OBJECT (h, "log2_max_frame_num_minus4 = %d out of range"
        " [0,12]", sps->log2_max_frame_num_minus4);
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

  gst_nal_bs_read_ue (bs);      /* max_num_ref_frames */
  gst_nal_bs_read (bs, 1);      /* gaps_in_frame_num_value_allowed_flag */
  width = gst_nal_bs_read_ue (bs);      /* pic_width_in_mbs_minus1 */
  height = gst_nal_bs_read_ue (bs);     /* pic_height_in_map_units_minus1 */

  sps->frame_mbs_only_flag = gst_nal_bs_read (bs, 1);
  if (!sps->frame_mbs_only_flag) {
    gst_nal_bs_read (bs, 1);    /* mb_adaptive_frame_field_flag */
  }

  width++;
  width *= 16;
  height++;
  height *= 16 * (2 - sps->frame_mbs_only_flag);

  gst_nal_bs_read (bs, 1);      /* direct_8x8_inference_flag */
  if (gst_nal_bs_read (bs, 1)) {        /* frame_cropping_flag */
    fc_left = gst_nal_bs_read_ue (bs);  /* frame_crop_left_offset */
    fc_right = gst_nal_bs_read_ue (bs); /* frame_crop_right_offset */
    fc_top = gst_nal_bs_read_ue (bs);   /* frame_crop_top_offset */
    fc_bottom = gst_nal_bs_read_ue (bs);        /* frame_crop_bottom_offset */
  } else
    fc_left = fc_right = fc_top = fc_bottom = 0;

  GST_DEBUG_OBJECT (h, "Decoding SPS: profile_idc = %d, "
      "level_idc = %d, "
      "sps_id = %d, "
      "pic_order_cnt_type = %d, "
      "frame_mbs_only_flag = %d\n",
      sps->profile_idc,
      sps->level_idc,
      sps_id, sps->pic_order_cnt_type, sps->frame_mbs_only_flag);

  /* calculate width and height */
  GST_DEBUG_OBJECT (h, "initial width=%d, height=%d", width, height);
  GST_DEBUG_OBJECT (h, "crop (%d,%d)(%d,%d)",
      fc_left, fc_top, fc_right, fc_bottom);
  if (chroma > 3) {
    GST_DEBUG_OBJECT (h, "chroma=%d in SPS is out of range", chroma);
    return FALSE;
  }
  width -= (fc_left + fc_right) * subwc[chroma];
  height -=
      (fc_top + fc_bottom) * subhc[chroma] * (2 - sps->frame_mbs_only_flag);
  if (width < 0 || height < 0) {
    GST_DEBUG_OBJECT (h, "invalid width/height in SPS");
    return FALSE;
  }
  GST_DEBUG_OBJECT (h, "final width=%u, height=%u", width, height);
  sps->width = width;
  sps->height = height;

  sps->vui_parameters_present_flag = gst_nal_bs_read (bs, 1);
  if (sps->vui_parameters_present_flag) {
    gst_sps_decode_vui (h, bs);
  }

  return TRUE;
}

/* decode pic parameter set */
static gboolean
gst_nal_decode_pps (GstH264Parse * h, GstNalBs * bs)
{
  gint pps_id;
  GstH264Pps *pps = NULL;

  pps_id = gst_nal_bs_read_ue (bs);
  if (pps_id >= MAX_PPS_COUNT) {
    GST_DEBUG_OBJECT (h, "requested pps_id=%04x out of range", pps_id);
    return FALSE;
  }

  pps = gst_h264_parse_get_pps (h, pps_id);
  if (pps == NULL) {
    return FALSE;
  }
  h->pps = pps;

  pps->sps_id = gst_nal_bs_read_ue (bs);

  /* not parsing the rest for the time being */
  return TRUE;
}

/* decode buffering periods */
static gboolean
gst_sei_decode_buffering_period (GstH264Parse * h, GstNalBs * bs)
{
  guint8 sps_id;
  gint sched_sel_idx;
  GstH264Sps *sps;

  sps_id = gst_nal_bs_read_ue (bs);
  sps = gst_h264_parse_get_sps (h, sps_id);
  if (!sps)
    return FALSE;

  if (sps->nal_hrd_parameters_present_flag) {
    for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1;
        sched_sel_idx++) {
      h->initial_cpb_removal_delay[sched_sel_idx]
          = gst_nal_bs_read (bs,
          sps->initial_cpb_removal_delay_length_minus1 + 1);
      gst_nal_bs_read (bs, sps->initial_cpb_removal_delay_length_minus1 + 1);   /* initial_cpb_removal_delay_offset */
    }
  }
  if (sps->vcl_hrd_parameters_present_flag) {
    for (sched_sel_idx = 0; sched_sel_idx <= sps->cpb_cnt_minus1;
        sched_sel_idx++) {
      h->initial_cpb_removal_delay[sched_sel_idx]
          = gst_nal_bs_read (bs,
          sps->initial_cpb_removal_delay_length_minus1 + 1);
      gst_nal_bs_read (bs, sps->initial_cpb_removal_delay_length_minus1 + 1);   /* initial_cpb_removal_delay_offset */
    }
  }
#if 0
  h->ts_trn_nb = MPEGTIME_TO_GSTTIME (h->initial_cpb_removal_delay[0]); /* Assuming SchedSelIdx=0 */
#endif
  if (h->ts_trn_nb == GST_CLOCK_TIME_NONE || h->dts == GST_CLOCK_TIME_NONE)
    h->ts_trn_nb = 0;
  else
    h->ts_trn_nb = h->dts;

  GST_DEBUG_OBJECT (h, "h->ts_trn_nb updated: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (h->ts_trn_nb));

  return 0;
}

/* decode SEI picture timing message */
static gboolean
gst_sei_decode_picture_timing (GstH264Parse * h, GstNalBs * bs)
{
  GstH264Sps *sps = h->sps;

  if (sps == NULL) {
    GST_WARNING_OBJECT (h, "h->sps=NULL; delayed decoding of picture timing "
        "info not implemented yet");
    return FALSE;
  }

  if (sps->nal_hrd_parameters_present_flag
      || sps->vcl_hrd_parameters_present_flag) {
    h->sei_cpb_removal_delay =
        gst_nal_bs_read (bs, sps->cpb_removal_delay_length_minus1 + 1);
    h->sei_dpb_output_delay =
        gst_nal_bs_read (bs, sps->dpb_output_delay_length_minus1 + 1);
  }
  if (sps->pic_struct_present_flag) {
    guint i, num_clock_ts;
    h->sei_pic_struct = gst_nal_bs_read (bs, 4);
    h->sei_ct_type = 0;

    if (h->sei_pic_struct > SEI_PIC_STRUCT_FRAME_TRIPLING)
      return FALSE;

    num_clock_ts = sei_num_clock_ts_table[h->sei_pic_struct];

    for (i = 0; i < num_clock_ts; i++) {
      if (gst_nal_bs_read (bs, 1)) {    /* clock_timestamp_flag */
        guint full_timestamp_flag;
        h->sei_ct_type |= 1 << gst_nal_bs_read (bs, 2);
        gst_nal_bs_read (bs, 1);        /* nuit_field_based_flag */
        gst_nal_bs_read (bs, 5);        /* counting_type */
        full_timestamp_flag = gst_nal_bs_read (bs, 1);
        gst_nal_bs_read (bs, 1);        /* discontinuity_flag */
        gst_nal_bs_read (bs, 1);        /* cnt_dropped_flag */
        gst_nal_bs_read (bs, 8);        /* n_frames */
        if (full_timestamp_flag) {
          gst_nal_bs_read (bs, 6);      /* seconds_value 0..59 */
          gst_nal_bs_read (bs, 6);      /* minutes_value 0..59 */
          gst_nal_bs_read (bs, 5);      /* hours_value 0..23 */
        } else {
          if (gst_nal_bs_read (bs, 1)) {        /* seconds_flag */
            gst_nal_bs_read (bs, 6);    /* seconds_value range 0..59 */
            if (gst_nal_bs_read (bs, 1)) {      /* minutes_flag */
              gst_nal_bs_read (bs, 6);  /* minutes_value 0..59 */
              if (gst_nal_bs_read (bs, 1))      /* hours_flag */
                gst_nal_bs_read (bs, 5);        /* hours_value 0..23 */
            }
          }
        }
        if (sps->time_offset_length_minus1 >= 0)
          gst_nal_bs_read (bs, sps->time_offset_length_minus1 + 1);     /* time_offset */
      }
    }

    GST_DEBUG_OBJECT (h, "ct_type:%X pic_struct:%d\n", h->sei_ct_type,
        h->sei_pic_struct);
  }
  return 0;
}

/* decode supplimental enhancement information */
static gboolean
gst_nal_decode_sei (GstH264Parse * h, GstNalBs * bs)
{
  guint8 tmp;
  GstSeiPayloadType payloadType = 0;
  gint8 payloadSize = 0;

  do {
    tmp = gst_nal_bs_read (bs, 8);
    payloadType += tmp;
  } while (tmp == 255);
  do {
    tmp = gst_nal_bs_read (bs, 8);
    payloadSize += tmp;
  } while (tmp == 255);
  GST_DEBUG_OBJECT (h,
      "SEI message received: payloadType = %d, payloadSize = %d bytes",
      payloadType, payloadSize);

  switch (payloadType) {
    case SEI_BUF_PERIOD:
      if (!gst_sei_decode_buffering_period (h, bs))
        return FALSE;
      break;
    case SEI_PIC_TIMING:
      /* TODO: According to H264 D2.2 Note1, it might be the case that the
       * picture timing SEI message is encountered before the corresponding SPS
       * is specified. Need to hold down the message and decode it later.  */
      if (!gst_sei_decode_picture_timing (h, bs))
        return FALSE;
      break;
    default:
      GST_DEBUG_OBJECT (h, "SEI message of payloadType = %d is recieved but not"
          " parsed", payloadType);
  }

  return TRUE;
}

/* decode slice header */
static gboolean
gst_nal_decode_slice_header (GstH264Parse * h, GstNalBs * bs)
{
  guint8 pps_id, sps_id;
  h->first_mb_in_slice = gst_nal_bs_read_ue (bs);
  h->slice_type = gst_nal_bs_read_ue (bs);

  pps_id = gst_nal_bs_read_ue (bs);
  h->pps = gst_h264_parse_get_pps (h, pps_id);
  if (!h->pps)
    return FALSE;
  /* FIXME: note that pps might be uninitialized */
  sps_id = h->pps->sps_id;
  h->sps = gst_h264_parse_get_sps (h, sps_id);
  if (!h->sps)
    return FALSE;
  /* FIXME: in some streams sps/pps may not be ready before the first slice
   * header. In this case it is not a good idea to _get_sps()/_pps() at this
   * point
   * TODO: scan one round beforehand for SPS/PPS before decoding slice headers?
   * */

  /* TODO: separate_color_plane_flag: from SPS, not implemented yet, assumed to
   * be false */

  h->frame_num =
      gst_nal_bs_read (bs, h->sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

  if (!h->sps && !h->sps->frame_mbs_only_flag) {
    h->field_pic_flag = gst_nal_bs_read (bs, 1);
    if (h->field_pic_flag)
      h->bottom_field_flag = gst_nal_bs_read (bs, 1);
  }

  /* not parsing the rest for the time being */
  return TRUE;
}

typedef GstH264Parse GstLegacyH264Parse;
typedef GstH264ParseClass GstLegacyH264ParseClass;
GST_BOILERPLATE (GstLegacyH264Parse, gst_h264_parse, GstElement,
    GST_TYPE_ELEMENT);

static void gst_h264_parse_reset (GstH264Parse * h264parse);
static void gst_h264_parse_finalize (GObject * object);
static void gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h264_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_h264_parse_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_h264_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_h264_parse_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstStateChangeReturn gst_h264_parse_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_h264_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class,
      &sinktemplate);
  gst_element_class_set_details_simple (gstelement_class, "H264Parse",
      "Codec/Parser/Video",
      "Parses raw h264 stream",
      "Michal Benes <michal.benes@itonis.tv>,"
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (h264_parse_debug, "legacy h264parse", 0,
      "legacy h264 parser");
}

static void
gst_h264_parse_class_init (GstH264ParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_h264_parse_finalize);
  gobject_class->set_property = gst_h264_parse_set_property;
  gobject_class->get_property = gst_h264_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_SPLIT_PACKETIZED,
      g_param_spec_boolean ("split-packetized", "Split packetized",
          "Split NAL units of packetized streams", DEFAULT_SPLIT_PACKETIZED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ACCESS_UNIT,
      g_param_spec_boolean ("access-unit", "Access Units",
          "Output Acess Units rather than NALUs", DEFAULT_ACCESS_UNIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_FORMAT,
      g_param_spec_enum ("output-format", "Output Format",
          "Output Format of stream (bytestream or otherwise)",
          GST_H264_PARSE_FORMAT_TYPE, DEFAULT_OUTPUT_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONFIG_INTERVAL,
      g_param_spec_uint ("config-interval",
          "SPS PPS Send Interval",
          "Send SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.) (0 = disabled)",
          0, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_h264_parse_change_state;
}

static void
gst_h264_parse_init (GstH264Parse * h264parse, GstH264ParseClass * g_class)
{
  h264parse->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_chain));
  gst_pad_set_event_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_sink_event));
  gst_pad_set_setcaps_function (h264parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_h264_parse_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->sinkpad);

  h264parse->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_element_add_pad (GST_ELEMENT (h264parse), h264parse->srcpad);

  h264parse->split_packetized = DEFAULT_SPLIT_PACKETIZED;
  h264parse->adapter = gst_adapter_new ();

  h264parse->merge = DEFAULT_ACCESS_UNIT;
  h264parse->picture_adapter = gst_adapter_new ();

  h264parse->interval = DEFAULT_CONFIG_INTERVAL;
  h264parse->last_report = GST_CLOCK_TIME_NONE;

  h264parse->format = GST_H264_PARSE_FORMAT_INPUT;

  gst_h264_parse_reset (h264parse);
}

static void
gst_h264_parse_reset (GstH264Parse * h264parse)
{
  gint i;
  GSList *list;

  for (i = 0; i < MAX_SPS_COUNT; i++) {
    if (h264parse->sps_buffers[i])
      g_slice_free (GstH264Sps, h264parse->sps_buffers[i]);
    h264parse->sps_buffers[i] = NULL;
    gst_buffer_replace (&h264parse->sps_nals[i], NULL);
  }
  for (i = 0; i < MAX_PPS_COUNT; i++) {
    if (h264parse->pps_buffers[i])
      g_slice_free (GstH264Pps, h264parse->pps_buffers[i]);
    h264parse->pps_buffers[i] = NULL;
    gst_buffer_replace (&h264parse->pps_nals[i], NULL);
  }
  h264parse->sps = NULL;
  h264parse->pps = NULL;

  h264parse->first_mb_in_slice = -1;
  h264parse->slice_type = -1;
  h264parse->pps_id = -1;
  h264parse->frame_num = -1;
  h264parse->field_pic_flag = FALSE;
  h264parse->bottom_field_flag = FALSE;

  for (i = 0; i < 32; i++)
    h264parse->initial_cpb_removal_delay[i] = -1;
  h264parse->sei_cpb_removal_delay = 0;
  h264parse->sei_dpb_output_delay = 0;
  h264parse->sei_pic_struct = -1;
  h264parse->sei_ct_type = -1;

  h264parse->dts = GST_CLOCK_TIME_NONE;
  h264parse->ts_trn_nb = GST_CLOCK_TIME_NONE;
  h264parse->cur_duration = 0;
  h264parse->last_outbuf_dts = GST_CLOCK_TIME_NONE;

  list = h264parse->codec_nals;
  g_slist_foreach (list, (GFunc) gst_buffer_unref, NULL);
  g_slist_free (h264parse->codec_nals);
  h264parse->codec_nals = NULL;
  h264parse->picture_start = FALSE;
  h264parse->idr_offset = -1;

  if (h264parse->pending_segment)
    gst_event_unref (h264parse->pending_segment);
  h264parse->pending_segment = NULL;

  g_list_foreach (h264parse->pending_events, (GFunc) gst_event_unref, NULL);
  g_list_free (h264parse->pending_events);
  h264parse->pending_events = NULL;

  gst_caps_replace (&h264parse->src_caps, NULL);
}

static void
gst_h264_parse_finalize (GObject * object)
{
  GstH264Parse *h264parse;

  h264parse = GST_H264PARSE (object);

  gst_h264_parse_reset (h264parse);

  g_object_unref (h264parse->adapter);
  g_object_unref (h264parse->picture_adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h264_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264PARSE (object);

  switch (prop_id) {
    case PROP_SPLIT_PACKETIZED:
      parse->split_packetized = g_value_get_boolean (value);
      break;
    case PROP_ACCESS_UNIT:
      parse->merge = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_FORMAT:
      parse->format = g_value_get_enum (value);
      break;
    case PROP_CONFIG_INTERVAL:
      parse->interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h264_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstH264Parse *parse;

  parse = GST_H264PARSE (object);

  switch (prop_id) {
    case PROP_SPLIT_PACKETIZED:
      g_value_set_boolean (value, parse->split_packetized);
      break;
    case PROP_ACCESS_UNIT:
      g_value_set_boolean (value, parse->merge);
      break;
    case PROP_OUTPUT_FORMAT:
      g_value_set_enum (value, parse->format);
      break;
    case PROP_CONFIG_INTERVAL:
      g_value_set_uint (value, parse->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* make a buffer consisting of a 4-byte start code following by
 * (a copy of) given nal data */
static GstBuffer *
gst_h264_parse_make_nal (GstH264Parse * h264parse, const guint8 * data,
    guint len)
{
  GstBuffer *buf;

  buf = gst_buffer_new_and_alloc (4 + len);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), 1);
  memcpy (GST_BUFFER_DATA (buf) + 4, data, len);

  return buf;
}

/* byte together avc codec data based on collected pps and sps so far */
static GstBuffer *
gst_h264_parse_make_codec_data (GstH264Parse * h264parse)
{
  GstBuffer *buf, *nal;
  gint i, sps_size = 0, pps_size = 0, num_sps = 0, num_pps = 0;
  guint8 profile_idc = 0, profile_comp = 0, level_idc = 0;
  gboolean found = FALSE;
  guint8 *data;

  /* sps_nals and pps_nals contain start code */

  for (i = 0; i < MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->sps_nals[i])) {
      num_sps++;
      /* size bytes also count */
      sps_size += GST_BUFFER_SIZE (nal) - 4 + 2;
      if (GST_BUFFER_SIZE (nal) >= 8) {
        found = TRUE;
        profile_idc = (GST_BUFFER_DATA (nal))[5];
        profile_comp = (GST_BUFFER_DATA (nal))[6];
        level_idc = (GST_BUFFER_DATA (nal))[7];
      }
    }
  }
  for (i = 0; i < MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->pps_nals[i])) {
      num_pps++;
      /* size bytes also count */
      pps_size += GST_BUFFER_SIZE (nal) - 4 + 2;
    }
  }

  GST_DEBUG_OBJECT (h264parse,
      "constructing codec_data: num_sps=%d, num_pps=%d", num_sps, num_pps);

  if (!found || !num_pps)
    return NULL;

  buf = gst_buffer_new_and_alloc (5 + 1 + sps_size + 1 + pps_size);
  data = GST_BUFFER_DATA (buf);

  data[0] = 1;                  /* AVC Decoder Configuration Record ver. 1 */
  data[1] = profile_idc;        /* profile_idc                             */
  data[2] = profile_comp;       /* profile_compability                     */
  data[3] = level_idc;          /* level_idc                               */
  data[4] = 0xfc | (4 - 1);     /* nal_length_size_minus1                  */
  data[5] = 0xe0 | num_sps;     /* number of SPSs */

  data += 6;
  for (i = 0; i < MAX_SPS_COUNT; i++) {
    if ((nal = h264parse->sps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal) - 4);
      memcpy (data + 2, GST_BUFFER_DATA (nal) + 4, GST_BUFFER_SIZE (nal) - 4);
      data += 2 + GST_BUFFER_SIZE (nal) - 4;
    }
  }

  data[0] = num_pps;
  data++;
  for (i = 0; i < MAX_PPS_COUNT; i++) {
    if ((nal = h264parse->pps_nals[i])) {
      GST_WRITE_UINT16_BE (data, GST_BUFFER_SIZE (nal) - 4);
      memcpy (data + 2, GST_BUFFER_DATA (nal) + 4, GST_BUFFER_SIZE (nal) - 4);
      data += 2 + GST_BUFFER_SIZE (nal) - 4;
    }
  }

  return buf;
}

static guint
gst_h264_parse_parse_stream_format (GstH264Parse * h264parse,
    const gchar * stream_format)
{
  if (strcmp (stream_format, "avc") == 0) {
    return GST_H264_PARSE_FORMAT_SAMPLE;
  } else if (strcmp (stream_format, "byte-stream") == 0) {
    return GST_H264_PARSE_FORMAT_BYTE;
  }
  return GST_H264_PARSE_FORMAT_INPUT;   /* this means we don't know */
}

static gboolean
gst_h264_parse_update_src_caps (GstH264Parse * h264parse, GstCaps * caps)
{
  GstH264Sps *sps = NULL;
  GstCaps *src_caps = NULL;
  GstStructure *structure;
  gboolean modified = FALSE;
  const gchar *stream_format, *alignment;

  /* current PPS dictates which SPS to use */
  if (h264parse->pps && h264parse->pps->sps_id < MAX_SPS_COUNT) {
    sps = h264parse->sps_buffers[h264parse->pps->sps_id];
  }
  /* failing that, we'll take most recent SPS we can get */
  if (!sps) {
    sps = h264parse->sps;
  }

  if (G_UNLIKELY (h264parse->src_caps == NULL)) {
    src_caps = gst_caps_copy (caps);
    modified = TRUE;
  } else {
    src_caps = gst_caps_ref (h264parse->src_caps);
  }
  src_caps = gst_caps_make_writable (src_caps);

  g_return_val_if_fail (src_caps != NULL, FALSE);

  /* if some upstream metadata missing, fill in from parsed stream */
  /* width / height */
  if (sps && (sps->width > 0 && sps->height > 0) &&
      (h264parse->width != sps->width || h264parse->height != sps->height)) {
    gint width, height;

    width = h264parse->width = sps->width;
    height = h264parse->height = sps->height;

    GST_DEBUG_OBJECT (h264parse, "updating caps w/h %dx%d", width, height);
    gst_caps_set_simple (src_caps, "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, NULL);
    modified = TRUE;
  }

  /* framerate */
  if (sps && (sps->time_scale > 0 && sps->num_units_in_tick > 0) &&
      (h264parse->fps_num != sps->time_scale ||
          h264parse->fps_den != sps->num_units_in_tick)) {
    gint fps_num, fps_den;

    fps_num = h264parse->fps_num = sps->time_scale;
    fps_den = h264parse->fps_den = sps->num_units_in_tick;

    /* FIXME verify / also handle other cases */
    if (sps->fixed_frame_rate_flag && sps->frame_mbs_only_flag &&
        !sps->pic_struct_present_flag) {
      fps_den *= 2;             /* picture is a frame = 2 fields */
      GST_DEBUG_OBJECT (h264parse, "updating caps fps %d/%d", fps_num, fps_den);
      gst_caps_set_simple (src_caps,
          "framerate", GST_TYPE_FRACTION, fps_num, fps_den, NULL);
      modified = TRUE;
    }
  }

  structure = gst_caps_get_structure (src_caps, 0);

  /* we replace the stream-format on caps if needed */
  stream_format = gst_structure_get_string (structure, "stream-format");
  if (stream_format) {
    guint input_format;
    guint output_format;

    input_format = gst_h264_parse_parse_stream_format (h264parse,
        stream_format);
    output_format = h264parse->format;

    if (output_format == GST_H264_PARSE_FORMAT_INPUT) {
      if (h264parse->packetized) {
        output_format = GST_H264_PARSE_FORMAT_SAMPLE;
      } else {
        output_format = GST_H264_PARSE_FORMAT_BYTE;
      }
    }

    if (input_format != output_format) {
      /* we need to replace it */
      stream_format = NULL;
    }
  }

  /* we need to add a new stream-format */
  if (stream_format == NULL) {
    gst_structure_remove_field (structure, "stream-format");
    if (h264parse->format == GST_H264_PARSE_FORMAT_SAMPLE) {
      stream_format = "avc";
    } else if (h264parse->format == GST_H264_PARSE_FORMAT_BYTE) {
      stream_format = "byte-stream";
    } else {
      if (h264parse->packetized) {
        stream_format = "avc";
      } else {
        stream_format = "byte-stream";
      }
    }
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, stream_format,
        NULL);
    modified = TRUE;
  }

  /* set alignment field */
  if (h264parse->merge) {
    alignment = "au";
  } else {
    if (h264parse->packetized) {
      if (h264parse->split_packetized)
        alignment = "nal";
      else {
        /* if packetized input is not split,
         * take upstream alignment if validly provided,
         * otherwise assume au aligned ... */
        alignment = gst_structure_get_string (structure, "alignment");
        if (!alignment || (alignment &&
                strcmp (alignment, "au") != 0 &&
                strcmp (alignment, "nal") != 0)) {
          alignment = "au";
        }
      }
    } else {
      alignment = "nal";
    }
  }
  /* now only set if changed */
  {
    const gchar *old_alignment;

    old_alignment = gst_structure_get_string (structure, "alignment");
    if (!old_alignment || strcmp (alignment, old_alignment) != 0) {
      gst_structure_set (structure, "alignment", G_TYPE_STRING, alignment,
          NULL);
      modified = TRUE;
    }
  }

  /* transforming to non-bytestream needs to make codec-data */
  if (h264parse->format == GST_H264_PARSE_FORMAT_SAMPLE) {
    GstBuffer *buf;
    const GValue *value = NULL;
    const GstBuffer *codec_data = NULL;

    value = gst_structure_get_value (structure, "codec_data");
    if (value != NULL)
      codec_data = gst_value_get_buffer (value);
    buf = gst_h264_parse_make_codec_data (h264parse);
    if (buf) {
      if (!codec_data || GST_BUFFER_SIZE (buf) != GST_BUFFER_SIZE (codec_data)
          || memcmp (GST_BUFFER_DATA (buf), GST_BUFFER_DATA (codec_data),
              GST_BUFFER_SIZE (buf))) {
        GST_DEBUG_OBJECT (h264parse, "setting new codec_data");
        gst_caps_set_simple (src_caps, "codec_data", GST_TYPE_BUFFER, buf,
            NULL);
        modified = TRUE;
      }
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (h264parse, "no codec_data yet");
    }
  } else if (h264parse->format == GST_H264_PARSE_FORMAT_BYTE) {
    /* need to remove the codec_data */
    if (gst_structure_has_field (structure, "codec_data")) {
      gst_structure_remove_field (structure, "codec_data");
      modified = TRUE;
    }
  }

  /* save as new caps, caps will be set when pushing data */
  /* avoid replacing caps by a mere identical copy, thereby triggering
   * negotiating (which e.g. some container might not appreciate) */
  if (modified)
    gst_caps_replace (&h264parse->src_caps, src_caps);
  gst_caps_unref (src_caps);

  return TRUE;
}

static gboolean
gst_h264_parse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstH264Parse *h264parse;
  GstStructure *str;
  const GValue *value;
  guint8 *data;
  guint size, num_sps, num_pps;

  h264parse = GST_H264PARSE (GST_PAD_PARENT (pad));

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &h264parse->width);
  gst_structure_get_int (str, "height", &h264parse->height);
  gst_structure_get_fraction (str, "framerate", &h264parse->fps_num,
      &h264parse->fps_den);

  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    GstBuffer *buffer;
    gint profile;
    GstNalBs bs;
    gint i, len;
    GSList *nlist = NULL;

    GST_DEBUG_OBJECT (h264parse, "have packetized h264");
    h264parse->packetized = TRUE;

    buffer = gst_value_get_buffer (value);
    data = GST_BUFFER_DATA (buffer);
    size = GST_BUFFER_SIZE (buffer);

    /* parse the avcC data */
    if (size < 7)
      goto avcc_too_small;
    /* parse the version, this must be 1 */
    if (data[0] != 1)
      goto wrong_version;

    /* AVCProfileIndication */
    /* profile_compat */
    /* AVCLevelIndication */
    profile = (data[1] << 16) | (data[2] << 8) | data[3];
    GST_DEBUG_OBJECT (h264parse, "profile %06x", profile);

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    /* this is the number of bytes in front of the NAL units to mark their
     * length */
    h264parse->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (h264parse, "nal length %u", h264parse->nal_length_size);

    num_sps = data[5] & 0x1f;
    data += 6;
    size -= 6;
    for (i = 0; i < num_sps; i++) {
      len = GST_READ_UINT16_BE (data);
      if (size < len + 2)
        goto avcc_too_small;
      gst_nal_bs_init (&bs, data + 2 + 1, len - 1);
      gst_nal_decode_sps (h264parse, &bs);
      /* store for later use, e.g. insertion */
      if (h264parse->sps) {
        h264parse->sps_nals[h264parse->sps->sps_id] =
            gst_h264_parse_make_nal (h264parse, data + 2, len);
      }
      if (h264parse->format == GST_H264_PARSE_FORMAT_BYTE)
        nlist = g_slist_append (nlist,
            gst_h264_parse_make_nal (h264parse, data + 2, len));
      data += len + 2;
      size -= len + 2;
    }
    num_pps = data[0];
    data++;
    size++;
    for (i = 0; i < num_pps; i++) {
      len = GST_READ_UINT16_BE (data);
      if (size < len + 2)
        goto avcc_too_small;
      gst_nal_bs_init (&bs, data + 2 + 1, len - 1);
      gst_nal_decode_pps (h264parse, &bs);
      /* store for later use, e.g. insertion */
      if (h264parse->pps) {
        h264parse->pps_nals[h264parse->pps->pps_id] =
            gst_h264_parse_make_nal (h264parse, data + 2, len);
      }
      if (h264parse->format == GST_H264_PARSE_FORMAT_BYTE)
        nlist = g_slist_append (nlist,
            gst_h264_parse_make_nal (h264parse, data + 2, len));
      data += len + 2;
      size -= len + 2;
    }
    h264parse->codec_nals = nlist;
  } else {
    GST_DEBUG_OBJECT (h264parse, "have bytestream h264");
    h264parse->packetized = FALSE;
    /* we have 4 sync bytes */
    h264parse->nal_length_size = 4;
  }

  /* forward the caps */
  return gst_h264_parse_update_src_caps (h264parse, caps);

  /* ERRORS */
avcc_too_small:
  {
    GST_ERROR_OBJECT (h264parse, "avcC size %u < 7", size);
    return FALSE;
  }
wrong_version:
  {
    GST_ERROR_OBJECT (h264parse, "wrong avcC version");
    return FALSE;
  }
}

/* if forced output mode,
 * ensures that NALU @nal starts with start code or length
 * takes ownership of nal and returns buffer
 */
static GstBuffer *
gst_h264_parse_write_nal_prefix (GstH264Parse * h264parse, GstBuffer * nal)
{
  guint nal_length = h264parse->nal_length_size;
  gint i;

  g_assert (nal_length <= 4);

  /* ensure proper transformation on prefix if needed */
  if (h264parse->format == GST_H264_PARSE_FORMAT_SAMPLE) {
    nal = gst_buffer_make_writable (nal);
    switch (nal_length) {
      case 1:
        GST_WRITE_UINT8 (GST_BUFFER_DATA (nal),
            GST_BUFFER_SIZE (nal) - nal_length);
        break;
      case 2:
        GST_WRITE_UINT16_BE (GST_BUFFER_DATA (nal),
            GST_BUFFER_SIZE (nal) - nal_length);
        break;
      case 3:
        GST_WRITE_UINT24_BE (GST_BUFFER_DATA (nal),
            GST_BUFFER_SIZE (nal) - nal_length);
        break;
      case 4:
        GST_WRITE_UINT32_BE (GST_BUFFER_DATA (nal),
            GST_BUFFER_SIZE (nal) - nal_length);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  } else if (h264parse->format == GST_H264_PARSE_FORMAT_BYTE) {
    gint offset = 0;
    guint nalu_size = 0;

    if (nal_length == 4) {
      nal = gst_buffer_make_writable (nal);
      while (offset + 4 <= GST_BUFFER_SIZE (nal)) {
        nalu_size = GST_READ_UINT32_BE (GST_BUFFER_DATA (nal) + offset);
        /* input may already be in byte-stream */
        if (nalu_size == 1)
          break;
        GST_WRITE_UINT32_BE (GST_BUFFER_DATA (nal) + offset, 0x01);
        offset += nalu_size + 4;
      }
    } else {
      GstAdapter *adapter = gst_adapter_new ();
      GstBuffer *sub;
      while (offset + nal_length <= GST_BUFFER_SIZE (nal)) {
        nalu_size = 0;
        for (i = 0; i < nal_length; i++)
          nalu_size = (nalu_size << 8) | GST_BUFFER_DATA (nal)[i];
        if (nalu_size > GST_BUFFER_SIZE (nal) - nal_length - offset) {
          GST_WARNING_OBJECT (h264parse, "NAL size %u is larger than buffer, "
              "reducing it to the buffer size: %u", nalu_size,
              GST_BUFFER_SIZE (nal) - nal_length - offset);
          nalu_size = GST_BUFFER_SIZE (nal) - nal_length - offset;
        }

        sub = gst_h264_parse_make_nal (h264parse,
            GST_BUFFER_DATA (nal) + nal_length + offset, nalu_size);
        gst_adapter_push (adapter, sub);
        offset += nalu_size + nal_length;
      }
      sub = gst_adapter_take_buffer (adapter, gst_adapter_available (adapter));
      gst_buffer_copy_metadata (sub, nal, GST_BUFFER_COPY_ALL);
      gst_buffer_unref (nal);
      nal = sub;
      g_object_unref (adapter);
    }
  }

  /* in any case, ensure metadata can be messed with later on */
  nal = gst_buffer_make_metadata_writable (nal);

  return nal;
}

/* sends a codec NAL downstream, decorating and transforming as needed.
 * No ownership is taken of @nal */
static GstFlowReturn
gst_h264_parse_push_codec_buffer (GstH264Parse * h264parse, GstBuffer * nal,
    GstClockTime ts)
{
  nal = gst_buffer_copy (nal);
  nal = gst_h264_parse_write_nal_prefix (h264parse, nal);

  GST_BUFFER_TIMESTAMP (nal) = ts;
  GST_BUFFER_DURATION (nal) = 0;

  gst_buffer_set_caps (nal, h264parse->src_caps);

  return gst_pad_push (h264parse->srcpad, nal);
}

/* sends buffer downstream, inserting codec_data NALUs if needed */
static GstFlowReturn
gst_h264_parse_push_buffer (GstH264Parse * h264parse, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;

  /* We can send pending events if this is the first call, since we now have
   * caps for the srcpad */
  if (G_UNLIKELY (h264parse->pending_segment != NULL)) {
    gst_pad_push_event (h264parse->srcpad, h264parse->pending_segment);
    h264parse->pending_segment = NULL;

    if (G_UNLIKELY (h264parse->pending_events != NULL)) {
      GList *l;

      for (l = h264parse->pending_events; l != NULL; l = l->next)
        gst_pad_push_event (h264parse->srcpad, GST_EVENT (l->data));

      g_list_free (h264parse->pending_events);
      h264parse->pending_events = NULL;
    }
  }

  if (G_UNLIKELY (h264parse->width == 0 || h264parse->height == 0)) {
    GST_DEBUG ("Delaying actual push until we are configured");
    h264parse->gather = g_list_append (h264parse->gather, buf);
    goto beach;
  }

  if (G_UNLIKELY (h264parse->gather)) {
    GList *pendingbuffers = h264parse->gather;
    GList *tmp;

    GST_DEBUG ("Pushing out pending buffers");

    /* Yes, we're recursively calling in... */
    h264parse->gather = NULL;
    for (tmp = pendingbuffers; tmp; tmp = tmp->next) {
      res = gst_h264_parse_push_buffer (h264parse, (GstBuffer *) tmp->data);
      if (res != GST_FLOW_OK && res != GST_FLOW_NOT_LINKED)
        break;
    }
    g_list_free (pendingbuffers);

    if (res != GST_FLOW_OK && res != GST_FLOW_NOT_LINKED) {
      gst_buffer_unref (buf);
      goto beach;
    }
  }

  /* start of picture is good time to slip in codec_data NALUs
   * (when outputting NALS and transforming to bytestream) */
  if (G_UNLIKELY (h264parse->codec_nals && h264parse->picture_start)) {
    GSList *nals = h264parse->codec_nals;
    while (nals) {
      GST_DEBUG_OBJECT (h264parse, "pushing codec_nal of size %d",
          GST_BUFFER_SIZE (nals->data));
      GST_BUFFER_TIMESTAMP (nals->data) = GST_BUFFER_TIMESTAMP (buf);
      GST_BUFFER_DURATION (nals->data) = 0;

      gst_buffer_set_caps (nals->data, h264parse->src_caps);
      (void) gst_pad_push (h264parse->srcpad, nals->data);
      nals = g_slist_delete_link (nals, nals);
    }
    h264parse->codec_nals = NULL;
  }

  /* periodic SPS/PPS sending */
  if (h264parse->interval > 0) {
    gint nal_type = 0;
    guint8 *data = GST_BUFFER_DATA (buf);
    guint nal_length = h264parse->nal_length_size;
    guint64 diff;
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buf);

    /* init */
    if (!GST_CLOCK_TIME_IS_VALID (h264parse->last_report)) {
      h264parse->last_report = timestamp;
    }

    if (!h264parse->merge) {
      nal_type = data[nal_length] & 0x1f;
      GST_LOG_OBJECT (h264parse, "- nal type: %d", nal_type);
    } else if (h264parse->idr_offset >= 0) {
      GST_LOG_OBJECT (h264parse, "AU has IDR nal at offset %d",
          h264parse->idr_offset);
      nal_type = 5;
    }

    /* insert on IDR */
    if (G_UNLIKELY (nal_type == 5)) {
      if (timestamp > h264parse->last_report)
        diff = timestamp - h264parse->last_report;
      else
        diff = 0;

      GST_LOG_OBJECT (h264parse,
          "now %" GST_TIME_FORMAT ", last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (h264parse->last_report));

      GST_DEBUG_OBJECT (h264parse,
          "interval since last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff));

      if (GST_TIME_AS_SECONDS (diff) >= h264parse->interval) {
        gint i;

        if (!h264parse->merge) {
          /* send separate config NAL buffers */
          GST_DEBUG_OBJECT (h264parse, "- sending SPS/PPS");
          for (i = 0; i < MAX_SPS_COUNT; i++) {
            if (h264parse->sps_nals[i]) {
              GST_DEBUG_OBJECT (h264parse, "sending SPS nal");
              gst_h264_parse_push_codec_buffer (h264parse,
                  h264parse->sps_nals[i], timestamp);
              h264parse->last_report = timestamp;
            }
          }
          for (i = 0; i < MAX_PPS_COUNT; i++) {
            if (h264parse->pps_nals[i]) {
              GST_DEBUG_OBJECT (h264parse, "sending PPS nal");
              gst_h264_parse_push_codec_buffer (h264parse,
                  h264parse->pps_nals[i], timestamp);
              h264parse->last_report = timestamp;
            }
          }
        } else {
          /* insert config NALs into AU */
          GstByteWriter bw;
          GstBuffer *codec_nal, *new_buf;

          gst_byte_writer_init_with_size (&bw, GST_BUFFER_SIZE (buf), FALSE);
          gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (buf),
              h264parse->idr_offset);
          GST_DEBUG_OBJECT (h264parse, "- inserting SPS/PPS");
          for (i = 0; i < MAX_SPS_COUNT; i++) {
            if (h264parse->sps_nals[i]) {
              GST_DEBUG_OBJECT (h264parse, "inserting SPS nal");
              codec_nal = gst_buffer_copy (h264parse->sps_nals[i]);
              codec_nal =
                  gst_h264_parse_write_nal_prefix (h264parse, codec_nal);
              gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (codec_nal),
                  GST_BUFFER_SIZE (codec_nal));
              h264parse->last_report = timestamp;
            }
          }
          for (i = 0; i < MAX_PPS_COUNT; i++) {
            if (h264parse->pps_nals[i]) {
              GST_DEBUG_OBJECT (h264parse, "inserting PPS nal");
              codec_nal = gst_buffer_copy (h264parse->pps_nals[i]);
              codec_nal =
                  gst_h264_parse_write_nal_prefix (h264parse, codec_nal);
              gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (codec_nal),
                  GST_BUFFER_SIZE (codec_nal));
              h264parse->last_report = timestamp;
            }
          }
          gst_byte_writer_put_data (&bw,
              GST_BUFFER_DATA (buf) + h264parse->idr_offset,
              GST_BUFFER_SIZE (buf) - h264parse->idr_offset);
          /* collect result and push */
          new_buf = gst_byte_writer_reset_and_get_buffer (&bw);
          gst_buffer_copy_metadata (new_buf, buf, GST_BUFFER_COPY_ALL);
          gst_buffer_unref (buf);
          buf = new_buf;
        }
      }
    }
  }

  gst_buffer_set_caps (buf, h264parse->src_caps);
  res = gst_pad_push (h264parse->srcpad, buf);

beach:
  return res;
}

/* takes over ownership of nal and returns fresh buffer */
static GstBuffer *
gst_h264_parse_push_nal (GstH264Parse * h264parse, GstBuffer * nal,
    guint8 * next_nal, gboolean * _start)
{
  gint nal_type;
  guint8 *data;
  GstBuffer *outbuf = NULL;
  guint outsize, size, nal_length = h264parse->nal_length_size;
  gboolean start;
  gboolean complete;

  data = GST_BUFFER_DATA (nal);
  size = GST_BUFFER_SIZE (nal);

  /* deal with 3-byte start code by normalizing to 4-byte here */
  if (!h264parse->packetized && data[2] == 0x01) {
    GstBuffer *tmp;

    /* ouch, copy */
    GST_DEBUG_OBJECT (h264parse, "replacing 3-byte startcode");
    tmp = gst_buffer_new_and_alloc (1);
    GST_BUFFER_DATA (tmp)[0] = 0;
    gst_buffer_ref (nal);
    tmp = gst_buffer_join (tmp, nal);
    GST_BUFFER_TIMESTAMP (tmp) = GST_BUFFER_TIMESTAMP (nal);
    gst_buffer_unref (nal);
    nal = tmp;

    data = GST_BUFFER_DATA (nal);
    size = GST_BUFFER_SIZE (nal);
  }

  /* caller ensures number of bytes available */
  g_return_val_if_fail (size >= nal_length + 1, NULL);

  /* determine if AU complete */
  nal_type = data[nal_length] & 0x1f;
  GST_LOG_OBJECT (h264parse, "nal type: %d", nal_type);
  h264parse->picture_start |= (nal_type == 1 || nal_type == 2 || nal_type == 5);
  /* first_mb_in_slice == 0 considered start of frame */
  start = h264parse->picture_start && (data[nal_length + 1] & 0x80);
  if (G_UNLIKELY (!next_nal)) {
    complete = TRUE;
  } else {
    /* consider a coded slices (IDR or not) to start a picture,
     * (so ending the previous one) if first_mb_in_slice == 0
     * (non-0 is part of previous one) */
    /* NOTE this is not entirely according to Access Unit specs in 7.4.1.2.4,
     * but in practice it works in sane cases, needs not much parsing,
     * and also works with broken frame_num in NAL
     * (where spec-wise would fail) */
    nal_type = next_nal[nal_length] & 0x1f;
    GST_LOG_OBJECT (h264parse, "next nal type: %d", nal_type);
    complete = h264parse->picture_start && (nal_type >= 6 && nal_type <= 9);
    complete |= h264parse->picture_start &&
        (nal_type == 1 || nal_type == 2 || nal_type == 5) &&
        (next_nal[nal_length + 1] & 0x80);
  }

  /* collect SPS and PPS NALUs to make up codec_data, if so needed */
  nal_type = data[nal_length] & 0x1f;
  if (G_UNLIKELY (nal_type == NAL_SPS)) {
    GstNalBs bs;
    guint id;

    gst_nal_bs_init (&bs, data + nal_length + 1, size - nal_length - 1);
    gst_nal_bs_read (&bs, 24);  /* profile_idc, profile_compatibility, level_idc */
    id = gst_nal_bs_read_ue (&bs);
    if (!gst_nal_bs_eos (&bs) && id < MAX_SPS_COUNT) {
      GST_DEBUG_OBJECT (h264parse, "storing SPS id %d", id);
      gst_buffer_replace (&h264parse->sps_nals[id], NULL);
      h264parse->sps_nals[id] =
          gst_h264_parse_make_nal (h264parse, data + nal_length,
          size - nal_length);
      gst_h264_parse_update_src_caps (h264parse, NULL);
    }
  } else if (G_UNLIKELY (nal_type == NAL_PPS)) {
    GstNalBs bs;
    guint id;

    gst_nal_bs_init (&bs, data + nal_length + 1, size - nal_length - 1);
    id = gst_nal_bs_read_ue (&bs);
    if (!gst_nal_bs_eos (&bs) && id < MAX_PPS_COUNT) {
      GST_DEBUG_OBJECT (h264parse, "storing PPS id %d", id);
      gst_buffer_replace (&h264parse->pps_nals[id], NULL);
      h264parse->pps_nals[id] =
          gst_h264_parse_make_nal (h264parse, data + nal_length,
          size - nal_length);
      gst_h264_parse_update_src_caps (h264parse, NULL);
    }
  }

  if (h264parse->merge) {
    /* clear IDR mark state */
    if (gst_adapter_available (h264parse->picture_adapter) == 0)
      h264parse->idr_offset = -1;

    /* proper prefix */
    nal = gst_h264_parse_write_nal_prefix (h264parse, nal);

    /* start of a picture is a good time to insert codec SPS and PPS */
    if (G_UNLIKELY (h264parse->codec_nals && h264parse->picture_start)) {
      while (h264parse->codec_nals) {
        GST_DEBUG_OBJECT (h264parse, "inserting codec_nal of size %d into AU",
            GST_BUFFER_SIZE (h264parse->codec_nals->data));
        gst_adapter_push (h264parse->picture_adapter,
            h264parse->codec_nals->data);
        h264parse->codec_nals =
            g_slist_delete_link (h264parse->codec_nals, h264parse->codec_nals);
      }
    }

    /* mark IDR nal location for later possible config insertion */
    if (nal_type == 5 && h264parse->idr_offset < 0)
      h264parse->idr_offset =
          gst_adapter_available (h264parse->picture_adapter);

    /* regardless, collect this NALU */
    gst_adapter_push (h264parse->picture_adapter, nal);

    if (complete) {
      GstClockTime ts;

      h264parse->picture_start = FALSE;
      ts = gst_adapter_prev_timestamp (h264parse->picture_adapter, NULL);
      outsize = gst_adapter_available (h264parse->picture_adapter);
      outbuf = gst_adapter_take_buffer (h264parse->picture_adapter, outsize);
      outbuf = gst_buffer_make_metadata_writable (outbuf);
      GST_BUFFER_TIMESTAMP (outbuf) = ts;

      /* AU always starts a frame */
      start = TRUE;
    }
  } else {
    outbuf = gst_h264_parse_write_nal_prefix (h264parse, nal);
  }

  if (_start)
    *_start = start;

  return outbuf;
}

static void
gst_h264_parse_clear_queues (GstH264Parse * h264parse)
{
  g_list_foreach (h264parse->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (h264parse->gather);
  h264parse->gather = NULL;
  while (h264parse->decode) {
    gst_buffer_unref (h264parse->decode->buffer);
    h264parse->decode = gst_nal_list_delete_head (h264parse->decode);
  }
  h264parse->decode = NULL;
  h264parse->decode_len = 0;
  if (h264parse->prev) {
    gst_buffer_unref (h264parse->prev);
    h264parse->prev = NULL;
  }
  gst_adapter_clear (h264parse->adapter);
  h264parse->have_i_frame = FALSE;
  gst_adapter_clear (h264parse->picture_adapter);
  h264parse->picture_start = FALSE;
}

static GstFlowReturn
gst_h264_parse_chain_forward (GstH264Parse * h264parse, gboolean discont,
    GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  const guint8 *data;

  if (discont) {
    gst_adapter_clear (h264parse->adapter);
    h264parse->discont = TRUE;
  }

  gst_adapter_push (h264parse->adapter, buffer);

  while (res == GST_FLOW_OK) {
    gint i;
    gint next_nalu_pos = -1;
    gint avail;
    gboolean delta_unit = FALSE;
    gboolean got_frame = FALSE;

    avail = gst_adapter_available (h264parse->adapter);
    if (avail < h264parse->nal_length_size + 2)
      break;
    data = gst_adapter_peek (h264parse->adapter, avail);

    if (!h264parse->packetized) {
      /* Bytestream format, first 3/4 bytes are sync code */
      /* re-sync; locate initial startcode */
      if (G_UNLIKELY (h264parse->discont)) {
        guint32 value;

        /* check for initial 00 00 01 */
        i = gst_adapter_masked_scan_uint32 (h264parse->adapter, 0xffffff00,
            0x00000100, 0, 4);
        if (i < 0) {
          i = gst_adapter_masked_scan_uint32_peek (h264parse->adapter,
              0x00ffffff, 0x01, 0, avail, &value);
          if (i < 0) {
            /* no sync code, flush and try next time */
            gst_adapter_flush (h264parse->adapter, avail - 2);
            break;
          } else {
            if (value >> 24 != 00)
              /* so a 3 byte startcode */
              i++;
            gst_adapter_flush (h264parse->adapter, i);
            avail -= i;
            data = gst_adapter_peek (h264parse->adapter, avail);
          }
        }
        GST_DEBUG_OBJECT (h264parse, "re-sync found startcode at %d", i);
      }
      /* Find next NALU header, might be 3 or 4 bytes */
      for (i = 1; i < avail - 4; ++i) {
        if (data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
          if (data[i + 0] == 0)
            next_nalu_pos = i;
          else
            next_nalu_pos = i + 1;
          break;
        }
      }
      /* skip sync */
      if (data[2] == 0x1) {
        data += 3;
        avail -= 3;
      } else {
        data += 4;
        avail -= 4;
      }
    } else {
      guint32 nalu_size;

      nalu_size = 0;
      for (i = 0; i < h264parse->nal_length_size; i++)
        nalu_size = (nalu_size << 8) | data[i];

      GST_LOG_OBJECT (h264parse, "got NALU size %u", nalu_size);

      /* check for invalid NALU sizes, assume the size if the available bytes
       * when something is fishy */
      if (nalu_size <= 1 || nalu_size + h264parse->nal_length_size > avail) {
        nalu_size = avail - h264parse->nal_length_size;
        GST_DEBUG_OBJECT (h264parse, "fixing invalid NALU size to %u",
            nalu_size);
      }

      /* Packetized format, see if we have to split it, usually splitting is not
       * a good idea as decoders have no way of handling it. */
      if (h264parse->split_packetized) {
        if (nalu_size + h264parse->nal_length_size <= avail)
          next_nalu_pos = nalu_size + h264parse->nal_length_size;
      } else {
        next_nalu_pos = avail;
      }
      /* skip nalu_size bytes */
      data += h264parse->nal_length_size;
      avail -= h264parse->nal_length_size;
    }

    /* Figure out if this is a delta unit */
    {
      GstNalUnitType nal_type;
      gint nal_ref_idc;
      GstNalBs bs;

      nal_type = (data[0] & 0x1f);
      nal_ref_idc = (data[0] & 0x60) >> 5;

      GST_DEBUG_OBJECT (h264parse, "NAL type: %d, ref_idc: %d", nal_type,
          nal_ref_idc);

      gst_nal_bs_init (&bs, data + 1, avail - 1);

      /* first parse some things needed to get to the frame type */
      switch (nal_type) {
        case NAL_SLICE:
        case NAL_SLICE_DPA:
        case NAL_SLICE_DPB:
        case NAL_SLICE_DPC:
        case NAL_SLICE_IDR:
        {
          gint first_mb_in_slice, slice_type;

          gst_nal_decode_slice_header (h264parse, &bs);
          first_mb_in_slice = h264parse->first_mb_in_slice;
          slice_type = h264parse->slice_type;

          GST_DEBUG_OBJECT (h264parse, "first MB: %d, slice type: %d",
              first_mb_in_slice, slice_type);

          switch (slice_type) {
            case 0:
            case 5:
            case 3:
            case 8:            /* SP */
              /* P frames */
              GST_DEBUG_OBJECT (h264parse, "we have a P slice");
              delta_unit = TRUE;
              break;
            case 1:
            case 6:
              /* B frames */
              GST_DEBUG_OBJECT (h264parse, "we have a B slice");
              delta_unit = TRUE;
              break;
            case 2:
            case 7:
            case 4:
            case 9:
              /* I frames */
              GST_DEBUG_OBJECT (h264parse, "we have an I slice");
              got_frame = TRUE;
              break;
          }
          break;

        }
        case NAL_SEI:
          GST_DEBUG_OBJECT (h264parse, "we have an SEI NAL");
          gst_nal_decode_sei (h264parse, &bs);
          break;
        case NAL_SPS:
          GST_DEBUG_OBJECT (h264parse, "we have an SPS NAL");
          gst_nal_decode_sps (h264parse, &bs);
          break;
        case NAL_PPS:
          GST_DEBUG_OBJECT (h264parse, "we have a PPS NAL");
          gst_nal_decode_pps (h264parse, &bs);
          break;
        case NAL_AU_DELIMITER:
          GST_DEBUG_OBJECT (h264parse, "we have an access unit delimiter.");
          break;
        default:
          GST_DEBUG_OBJECT (h264parse,
              "NAL of nal_type = %d encountered but not parsed", nal_type);
      }
    }

    /* we have a packet */
    if (next_nalu_pos > 0) {
      GstBuffer *outbuf;
      GstClockTime outbuf_dts = GST_CLOCK_TIME_NONE;
      gboolean start;
      guint8 *next_data;

      outbuf_dts = gst_adapter_prev_timestamp (h264parse->adapter, NULL);       /* Better value for the second parameter? */
      outbuf = gst_adapter_take_buffer (h264parse->adapter, next_nalu_pos);

      /* packetized will have no next data, which serves fine here */
      next_data = (guint8 *) gst_adapter_peek (h264parse->adapter, 6);
      outbuf = gst_h264_parse_push_nal (h264parse, outbuf, next_data, &start);
      if (!outbuf) {
        /* no complete unit yet, go for next round */
        continue;
      }

      /* Ignore upstream dts that stalls or goes backward. Upstream elements
       * like filesrc would keep on writing timestamp=0.  XXX: is this correct?
       * TODO: better way to detect whether upstream timstamps are useful */
      if (h264parse->last_outbuf_dts != GST_CLOCK_TIME_NONE
          && outbuf_dts != GST_CLOCK_TIME_NONE
          && outbuf_dts <= h264parse->last_outbuf_dts)
        outbuf_dts = GST_CLOCK_TIME_NONE;

      if ((got_frame || delta_unit) && start) {
        GstH264Sps *sps = h264parse->sps;
        gint duration = 1;

        if (!sps) {
          GST_DEBUG_OBJECT (h264parse, "referred SPS invalid");
          goto TIMESTAMP_FINISH;
        } else if (!sps->timing_info_present_flag) {
          GST_DEBUG_OBJECT (h264parse,
              "unable to compute timestamp: timing info not present");
          goto TIMESTAMP_FINISH;
        } else if (sps->time_scale == 0) {
          GST_DEBUG_OBJECT (h264parse,
              "unable to compute timestamp: time_scale = 0 "
              "(this is forbidden in spec; bitstream probably contains error)");
          goto TIMESTAMP_FINISH;
        }

        if (sps->pic_struct_present_flag
            && h264parse->sei_pic_struct != (guint8) - 1) {
          /* Note that when h264parse->sei_pic_struct == -1 (unspecified), there
           * are ways to infer its value. This is related to computing the
           * TopFieldOrderCnt and BottomFieldOrderCnt, which looks
           * complicated and thus not implemented for the time being. Yet
           * the value we have here is correct for many applications
           */
          switch (h264parse->sei_pic_struct) {
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
              GST_DEBUG_OBJECT (h264parse,
                  "h264parse->sei_pic_struct of unknown value %d. Not parsed",
                  h264parse->sei_pic_struct);
          }
        } else {
          duration = h264parse->field_pic_flag ? 1 : 2;
        }

        /*
         * h264parse.264 C.1.2 Timing of coded picture removal (equivalent to DTS):
         * Tr,n(0) = initial_cpb_removal_delay[ SchedSelIdx ] / 90000
         * Tr,n(n) = Tr,n(nb) + Tc * cpb_removal_delay(n)
         * where
         * Tc = num_units_in_tick / time_scale
         */

        if (h264parse->ts_trn_nb != GST_CLOCK_TIME_NONE) {
          /* buffering period is present */
          if (outbuf_dts != GST_CLOCK_TIME_NONE) {
            /* If upstream timestamp is valid, we respect it and adjust current
             * reference point */
            h264parse->ts_trn_nb = outbuf_dts -
                (GstClockTime) gst_util_uint64_scale_int
                (h264parse->sei_cpb_removal_delay * GST_SECOND,
                sps->num_units_in_tick, sps->time_scale);
          } else {
            /* If no upstream timestamp is given, we write in new timestamp */
            h264parse->dts = h264parse->ts_trn_nb +
                (GstClockTime) gst_util_uint64_scale_int
                (h264parse->sei_cpb_removal_delay * GST_SECOND,
                sps->num_units_in_tick, sps->time_scale);
          }
        } else {
          /* naive method: no removal delay specified, use best guess (add prev
           * frame duration) */
          if (outbuf_dts != GST_CLOCK_TIME_NONE)
            h264parse->dts = outbuf_dts;
          else if (h264parse->dts != GST_CLOCK_TIME_NONE)
            h264parse->dts += (GstClockTime)
                gst_util_uint64_scale_int (h264parse->cur_duration * GST_SECOND,
                sps->num_units_in_tick, sps->time_scale);
          else
            h264parse->dts = 0; /* initialization */

          /* TODO: better approach: construct a buffer queue and put all these
           * NALs into the buffer. Wait until we are able to get any valid dts
           * or such like, and dump the buffer and estimate the timestamps of
           * the NALs by their duration.
           */
        }

        h264parse->cur_duration = duration;
        h264parse->frame_cnt += 1;
        if (outbuf_dts != GST_CLOCK_TIME_NONE)
          h264parse->last_outbuf_dts = outbuf_dts;
      }

      if (outbuf_dts == GST_CLOCK_TIME_NONE)
        outbuf_dts = h264parse->dts;
      else
        h264parse->dts = outbuf_dts;

    TIMESTAMP_FINISH:
      GST_BUFFER_TIMESTAMP (outbuf) = outbuf_dts;

      GST_DEBUG_OBJECT (h264parse,
          "pushing buffer %p, size %u, ts %" GST_TIME_FORMAT, outbuf,
          next_nalu_pos, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

      if (h264parse->discont) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        h264parse->discont = FALSE;
      }

      if (delta_unit)
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
      else
        GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

      res = gst_h264_parse_push_buffer (h264parse, outbuf);
    } else {
      /* NALU can not be parsed yet, we wait for more data in the adapter. */
      break;
    }
  }
  return res;
}

static GstFlowReturn
gst_h264_parse_flush_decode (GstH264Parse * h264parse)
{
  GstFlowReturn res = GST_FLOW_OK;
  gboolean first = TRUE;

  while (h264parse->decode) {
    GstNalList *link;
    GstBuffer *buf;

    link = h264parse->decode;
    buf = link->buffer;

    h264parse->decode = gst_nal_list_delete_head (h264parse->decode);
    h264parse->decode_len--;

    GST_DEBUG_OBJECT (h264parse, "have type: %d, I frame: %d", link->nal_type,
        link->i_frame);

    buf = gst_h264_parse_push_nal (h264parse, buf,
        h264parse->decode ? GST_BUFFER_DATA (h264parse->decode->buffer) : NULL,
        NULL);
    if (!buf)
      continue;

    if (first) {
      /* first buffer has discont */
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      first = FALSE;
    } else {
      /* next buffers are not discont */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
    }

    if (link->i_frame)
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    GST_DEBUG_OBJECT (h264parse, "pushing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    gst_buffer_set_caps (buf, h264parse->src_caps);
    res = gst_pad_push (h264parse->srcpad, buf);

  }
  /* the i frame is gone now */
  h264parse->have_i_frame = FALSE;

  return res;
}

/* check that the decode queue contains a valid sync code that should be pushed
 * out before adding @buffer to the decode queue */
static GstFlowReturn
gst_h264_parse_queue_buffer (GstH264Parse * parse, GstBuffer * buffer)
{
  guint8 *data;
  guint size;
  guint32 nalu_size;
  GstNalBs bs;
  GstNalList *link;
  GstFlowReturn res = GST_FLOW_OK;
  GstClockTime timestamp;

  /* create new NALU link */
  link = gst_nal_list_new (buffer);

  /* first parse the buffer */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  link->slice = FALSE;
  link->i_frame = FALSE;

  GST_DEBUG_OBJECT (parse,
      "analyse buffer of size %u, timestamp %" GST_TIME_FORMAT, size,
      GST_TIME_ARGS (timestamp));

  /* now parse all the NAL units in this buffer, for bytestream we only have one
   * NAL unit but for packetized streams we can have multiple ones */
  while (size >= parse->nal_length_size + 1) {
    gint i;

    nalu_size = 0;
    if (parse->packetized) {
      for (i = 0; i < parse->nal_length_size; i++)
        nalu_size = (nalu_size << 8) | data[i];
    }

    /* skip nalu_size or sync bytes */
    data += parse->nal_length_size;
    size -= parse->nal_length_size;

    link->nal_ref_idc = (data[0] & 0x60) >> 5;
    link->nal_type = (data[0] & 0x1f);

    /* nalu_size is 0 for bytestream, we have a complete packet */
    GST_DEBUG_OBJECT (parse, "size: %u, NAL type: %d, ref_idc: %d",
        nalu_size, link->nal_type, link->nal_ref_idc);

    /* first parse some things needed to get to the frame type */
    if (link->nal_type >= NAL_SLICE && link->nal_type <= NAL_SLICE_IDR) {
      gst_nal_bs_init (&bs, data + 1, size - 1);

      link->first_mb_in_slice = gst_nal_bs_read_ue (&bs);
      link->slice_type = gst_nal_bs_read_ue (&bs);
      link->slice = TRUE;

      GST_DEBUG_OBJECT (parse, "first MB: %d, slice type: %d",
          link->first_mb_in_slice, link->slice_type);

      switch (link->slice_type) {
        case 0:
        case 5:
        case 3:
        case 8:                /* SP */
          /* P frames */
          GST_DEBUG_OBJECT (parse, "we have a P slice");
          break;
        case 1:
        case 6:
          /* B frames */
          GST_DEBUG_OBJECT (parse, "we have a B slice");
          break;
        case 2:
        case 7:
        case 4:
        case 9:
          /* I frames */
          GST_DEBUG_OBJECT (parse, "we have an I slice");
          link->i_frame = TRUE;
          break;
      }
    }
    /* bytestream, we can exit now */
    if (!parse->packetized)
      break;

    /* packetized format, continue parsing all packets, skip size, we already
     * skipped the nal_length_size bytes */
    data += nalu_size;
    size -= nalu_size;
  }

  /* we have an I frame in the queue, this new NAL unit is a slice but not
   * an I frame, output the decode queue */
  GST_DEBUG_OBJECT (parse, "have_I_frame: %d, I_frame: %d, slice: %d",
      parse->have_i_frame, link->i_frame, link->slice);
  if (parse->have_i_frame && !link->i_frame && link->slice) {
    GST_DEBUG_OBJECT (parse, "flushing decode queue");
    res = gst_h264_parse_flush_decode (parse);
  }
  if (link->i_frame)
    /* we're going to add a new I-frame in the queue */
    parse->have_i_frame = TRUE;

  parse->decode = gst_nal_list_prepend_link (parse->decode, link);
  parse->decode_len++;
  GST_DEBUG_OBJECT (parse,
      "copied %d bytes of NAL to decode queue. queue size %d", size,
      parse->decode_len);

  return res;
}

static guint
gst_h264_parse_find_start_reverse (GstH264Parse * parse, guint8 * data,
    guint size, guint32 * code)
{
  guint32 search = *code;

  while (size > 0) {
    /* the sync code is kept in reverse */
    search = (search << 8) | (data[size - 1]);
    if (search == 0x01000000)
      break;

    size--;
  }
  *code = search;

  return size - 1;
}

static GstFlowReturn
gst_h264_parse_chain_reverse (GstH264Parse * h264parse, gboolean discont,
    GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer *gbuf = NULL;

  /* if we have a discont, move buffers to the decode list */
  if (G_UNLIKELY (discont)) {
    guint start, last;
    guint32 code;
    GstBuffer *prev;
    GstClockTime timestamp;

    GST_DEBUG_OBJECT (h264parse,
        "received discont, copy gathered buffers for decoding");

    /* init start code accumulator */
    prev = h264parse->prev;
    h264parse->prev = NULL;

    while (h264parse->gather) {
      guint8 *data;

      /* get new buffer and init the start code search to the end position */
      if (gbuf != NULL)
        gst_buffer_unref (gbuf);
      gbuf = GST_BUFFER_CAST (h264parse->gather->data);

      /* remove from the gather list, they are in reverse order */
      h264parse->gather =
          g_list_delete_link (h264parse->gather, h264parse->gather);

      if (h264parse->packetized) {
        /* packetized the packets are already split, we can just parse and 
         * store them */
        GST_DEBUG_OBJECT (h264parse, "copied packetized buffer");
        res = gst_h264_parse_queue_buffer (h264parse, gbuf);
        gbuf = NULL;
      } else {
        /* bytestream, we have to split the NALUs on the sync markers */
        code = 0xffffffff;
        if (prev) {
          /* if we have a previous buffer or a leftover, merge them together
           * now */
          GST_DEBUG_OBJECT (h264parse, "merging previous buffer");
          gbuf = gst_buffer_join (gbuf, prev);
          prev = NULL;
        }

        last = GST_BUFFER_SIZE (gbuf);
        data = GST_BUFFER_DATA (gbuf);
        timestamp = GST_BUFFER_TIMESTAMP (gbuf);

        GST_DEBUG_OBJECT (h264parse,
            "buffer size: %u, timestamp %" GST_TIME_FORMAT, last,
            GST_TIME_ARGS (timestamp));

        while (last > 0) {
          GST_DEBUG_OBJECT (h264parse, "scan from %u", last);
          /* find a start code searching backwards in this buffer */
          start =
              gst_h264_parse_find_start_reverse (h264parse, data, last, &code);
          if (start != -1) {
            GstBuffer *decode;

            GST_DEBUG_OBJECT (h264parse, "found start code at %u", start);

            /* we found a start code, copy everything starting from it to the
             * decode queue. */
            decode = gst_buffer_create_sub (gbuf, start, last - start);

            GST_BUFFER_TIMESTAMP (decode) = timestamp;

            /* see what we have here */
            res = gst_h264_parse_queue_buffer (h264parse, decode);

            last = start;
          } else {
            /* no start code found, keep the buffer and merge with potential next
             * buffer. */
            GST_DEBUG_OBJECT (h264parse, "no start code, keeping buffer to %u",
                last);
            prev = gst_buffer_create_sub (gbuf, 0, last);
            gst_buffer_unref (gbuf);
            gbuf = NULL;
            break;
          }
        }
      }
    }
    if (prev) {
      GST_DEBUG_OBJECT (h264parse, "keeping buffer");
      h264parse->prev = prev;
    }
  }
  if (buffer) {
    /* add buffer to gather queue */
    GST_DEBUG_OBJECT (h264parse, "gathering buffer %p, size %u", buffer,
        GST_BUFFER_SIZE (buffer));
    h264parse->gather = g_list_prepend (h264parse->gather, buffer);
  }

  if (gbuf) {
    gst_buffer_unref (gbuf);
    gbuf = NULL;
  }

  return res;
}

static GstFlowReturn
gst_h264_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstH264Parse *h264parse;
  gboolean discont;
  GstCaps *caps;

  h264parse = GST_H264PARSE (GST_PAD_PARENT (pad));

  if (!h264parse->src_caps) {
    /* Set default caps if the sink caps were not negotiated, this is when we
     * are reading from a file or so */
    caps = gst_caps_new_simple ("video/x-h264", NULL);

    /* we assume the bytestream format. If the data turns out to be packetized,
     * we have a problem because we don't know the length of the nalu_size
     * indicator. Packetized input MUST set the codec_data. */
    h264parse->packetized = FALSE;
    h264parse->nal_length_size = 4;

    h264parse->src_caps = caps;
  }

  discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_DEBUG_OBJECT (h264parse, "received buffer of size %u",
      GST_BUFFER_SIZE (buffer));

  if (h264parse->segment.rate > 0.0)
    res = gst_h264_parse_chain_forward (h264parse, discont, buffer);
  else
    res = gst_h264_parse_chain_reverse (h264parse, discont, buffer);

  return res;
}

static gboolean
gst_h264_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstH264Parse *h264parse;
  gboolean res;

  h264parse = GST_H264PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (h264parse, "received FLUSH stop");
      gst_segment_init (&h264parse->segment, GST_FORMAT_UNDEFINED);
      gst_h264_parse_clear_queues (h264parse);
      h264parse->last_outbuf_dts = GST_CLOCK_TIME_NONE;
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (h264parse, "received EOS");
      if (h264parse->pending_segment) {
        /* Send pending newsegment before EOS */
        gst_pad_push_event (h264parse->srcpad, h264parse->pending_segment);
        h264parse->pending_segment = NULL;
      }
      if (h264parse->segment.rate < 0.0) {
        gst_h264_parse_chain_reverse (h264parse, TRUE, NULL);
        gst_h264_parse_flush_decode (h264parse);
      }
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, pos;
      gboolean update;
      GstEvent **ev;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      /* now configure the values */
      gst_segment_set_newsegment_full (&h264parse->segment, update,
          rate, applied_rate, format, start, stop, pos);

      GST_DEBUG_OBJECT (h264parse,
          "Keeping newseg rate %g, applied rate %g, format %d, start %"
          G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT ", pos %" G_GINT64_FORMAT,
          rate, applied_rate, format, start, stop, pos);

      ev = &h264parse->pending_segment;
      gst_event_replace (ev, event);
      gst_event_unref (event);
      res = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_START:
    {
      res = gst_pad_push_event (h264parse->srcpad, event);
      break;
    }
    default:
    {
      if (G_UNLIKELY (h264parse->src_caps == NULL ||
              h264parse->pending_segment)) {
        /* We don't yet have enough data to set caps on the srcpad, so collect
         * non-critical events till we do */
        h264parse->pending_events = g_list_append (h264parse->pending_events,
            event);
        res = TRUE;
      } else
        res = gst_pad_push_event (h264parse->srcpad, event);

      break;
    }
  }
  gst_object_unref (h264parse);

  return res;
}

static GstStateChangeReturn
gst_h264_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstH264Parse *h264parse;
  GstStateChangeReturn ret;

  h264parse = GST_H264PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&h264parse->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_h264_parse_clear_queues (h264parse);
      gst_h264_parse_reset (h264parse);
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "legacyh264parse",
      GST_RANK_NONE, GST_TYPE_H264PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "h264parse",
    "Element parsing raw h264 streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
