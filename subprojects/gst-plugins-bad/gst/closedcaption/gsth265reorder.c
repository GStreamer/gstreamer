/* GStreamer
 * Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265reorder.h"
#include "gsth264reorder.h"
#include <gst/codecs/gsth265picture.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h265_reorder_debug);
#define GST_CAT_DEFAULT gst_h265_reorder_debug

struct _GstH265Reorder
{
  GstObject parent;

  gboolean need_reorder;

  gint width;
  gint height;

  guint8 conformance_window_flag;
  gint crop_rect_width;
  gint crop_rect_height;
  gint crop_rect_x;
  gint crop_rect_y;
  gint fps_n;
  gint fps_d;

  guint nal_length_size;
  gboolean is_hevc;
  GstH265Parser *parser;
  GstH265Parser *preproc_parser;
  GstH265Dpb *dpb;

  guint8 field_seq_flag;
  guint8 progressive_source_flag;
  guint8 interlaced_source_flag;

  GstH265SEIPicStructType cur_pic_struct;
  guint8 cur_source_scan_type;
  guint8 cur_duplicate_flag;

  gboolean no_output_of_prior_pics_flag;

  /* vps/sps/pps of the current slice */
  const GstH265VPS *active_vps;
  const GstH265SPS *active_sps;
  const GstH265PPS *active_pps;

  guint32 SpsMaxLatencyPictures;

  GstH265Picture *current_picture;
  GstVideoCodecFrame *current_frame;

  /* Slice (slice header + nalu) currently being processed/decoded */
  GstH265Slice current_slice;
  GstH265Slice prev_slice;
  GstH265Slice prev_independent_slice;

  GstH265Picture *RefPicSetStCurrBefore[16];
  GstH265Picture *RefPicSetStCurrAfter[16];
  GstH265Picture *RefPicSetStFoll[16];
  GstH265Picture *RefPicSetLtCurr[16];
  GstH265Picture *RefPicSetLtFoll[16];

  guint NumPocStCurrBefore;
  guint NumPocStCurrAfter;
  guint NumPocStFoll;
  guint NumPocLtCurr;
  guint NumPocLtFoll;
  guint NumPicTotalCurr;

  gint32 poc;                   // PicOrderCntVal
  gint32 poc_msb;               // PicOrderCntMsb
  gint32 poc_lsb;               // pic_order_cnt_lsb (from slice_header())
  gint32 prev_poc_msb;          // prevPicOrderCntMsb
  gint32 prev_poc_lsb;          // prevPicOrderCntLsb
  gint32 prev_tid0pic_poc_lsb;
  gint32 prev_tid0pic_poc_msb;
  gint32 PocStCurrBefore[16];
  gint32 PocStCurrAfter[16];
  gint32 PocStFoll[16];
  gint32 PocLtCurr[16];
  gint32 PocLtFoll[16];

  /* PicOrderCount of the previously outputted frame */
  gint last_output_poc;

  gboolean associated_irap_NoRaslOutputFlag;
  gboolean new_bitstream;
  gboolean prev_nal_is_eos;

  GArray *nalu;

  /* Split packetized data into actual nal chunks (for malformed stream) */
  GArray *split_nalu;

  GArray *au_nalus;

  GPtrArray *frame_queue;
  GPtrArray *output_queue;
  guint32 system_num;
  guint32 present_num;

  GstClockTime latency;
};

typedef struct
{
  union
  {
    GstH265VPS vps;
    GstH265SPS sps;
    GstH265PPS pps;
    GstH265Slice slice;
  } unit;
  GstH265NalUnitType nalu_type;
} GstH265ReorderNalUnit;

static void gst_h265_reorder_finalize (GObject * object);

static gboolean gst_h265_reorder_start_current_picture (GstH265Reorder * self);

#define gst_h265_reorder_parent_class parent_class
G_DEFINE_TYPE (GstH265Reorder, gst_h265_reorder, GST_TYPE_OBJECT);

static void
gst_h265_reorder_class_init (GstH265ReorderClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_h265_reorder_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_h265_reorder_debug, "h265reorder", 0,
      "h265reorder");
}

static inline gboolean
is_slice_nalu (GstH265NalUnitType type)
{
  if ((type >= GST_H265_NAL_SLICE_TRAIL_N &&
          type <= GST_H265_NAL_SLICE_RASL_R) ||
      (type >= GST_H265_NAL_SLICE_BLA_W_LP &&
          type <= GST_H265_NAL_SLICE_CRA_NUT)) {
    return TRUE;
  }

  return FALSE;
}

static void
gst_h265_reorder_clear_nalu (GstH265ReorderNalUnit * nalu)
{
  if (!nalu)
    return;

  if (is_slice_nalu (nalu->nalu_type))
    gst_h265_slice_hdr_free (&nalu->unit.slice.header);

  memset (nalu, 0, sizeof (GstH265ReorderNalUnit));
}

static void
gst_h265_reorder_init (GstH265Reorder * self)
{
  self->parser = gst_h265_parser_new ();
  self->preproc_parser = gst_h265_parser_new ();
  self->dpb = gst_h265_dpb_new ();
  self->frame_queue =
      g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_video_codec_frame_unref);
  self->output_queue =
      g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_video_codec_frame_unref);

  self->nalu = g_array_sized_new (FALSE, TRUE, sizeof (GstH265ReorderNalUnit),
      8);
  g_array_set_clear_func (self->nalu,
      (GDestroyNotify) gst_h265_reorder_clear_nalu);
  self->split_nalu = g_array_new (FALSE, FALSE, sizeof (GstH265NalUnit));
  self->au_nalus = g_array_new (FALSE, FALSE, sizeof (GstH265NalUnit));
  self->fps_n = 25;
  self->fps_d = 1;
}

static void
gst_h265_reorder_clear_ref_pic_sets (GstH265Reorder * self)
{
  guint i;

  for (i = 0; i < 16; i++) {
    gst_clear_h265_picture (&self->RefPicSetLtCurr[i]);
    gst_clear_h265_picture (&self->RefPicSetLtFoll[i]);
    gst_clear_h265_picture (&self->RefPicSetStCurrBefore[i]);
    gst_clear_h265_picture (&self->RefPicSetStCurrAfter[i]);
    gst_clear_h265_picture (&self->RefPicSetStFoll[i]);
  }
}

static void
gst_h265_reorder_finalize (GObject * object)
{
  GstH265Reorder *self = GST_H265_REORDER (object);

  gst_h265_parser_free (self->parser);
  gst_h265_parser_free (self->preproc_parser);
  g_ptr_array_unref (self->frame_queue);
  g_ptr_array_unref (self->output_queue);
  g_array_unref (self->nalu);
  g_array_unref (self->split_nalu);
  g_array_unref (self->au_nalus);
  gst_h265_reorder_clear_ref_pic_sets (self);
  gst_h265_dpb_free (self->dpb);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h265_reorder_is_crop_rect_changed (GstH265Reorder * self, GstH265SPS * sps)
{
  if (self->conformance_window_flag != sps->conformance_window_flag)
    return TRUE;
  if (self->crop_rect_width != sps->crop_rect_width)
    return TRUE;
  if (self->crop_rect_height != sps->crop_rect_height)
    return TRUE;
  if (self->crop_rect_x != sps->crop_rect_x)
    return TRUE;
  if (self->crop_rect_y != sps->crop_rect_y)
    return TRUE;

  return FALSE;
}

typedef struct
{
  const gchar *level_name;
  guint8 level_idc;
  guint32 MaxLumaPs;
} GstH265LevelLimits;

/* *INDENT-OFF* */
/* Table A.8 - General tier and level limits */
static const GstH265LevelLimits level_limits[] = {
  /* level    idc                   MaxLumaPs */
  {  "1",     GST_H265_LEVEL_L1,    36864    },
  {  "2",     GST_H265_LEVEL_L2,    122880   },
  {  "2.1",   GST_H265_LEVEL_L2_1,  245760   },
  {  "3",     GST_H265_LEVEL_L3,    552960   },
  {  "3.1",   GST_H265_LEVEL_L3_1,  983040   },
  {  "4",     GST_H265_LEVEL_L4,    2228224  },
  {  "4.1",   GST_H265_LEVEL_L4_1,  2228224  },
  {  "5",     GST_H265_LEVEL_L5,    8912896  },
  {  "5.1",   GST_H265_LEVEL_L5_1,  8912896  },
  {  "5.2",   GST_H265_LEVEL_L5_2,  8912896  },
  {  "6",     GST_H265_LEVEL_L6,    35651584 },
  {  "6.1",   GST_H265_LEVEL_L6_1,  35651584 },
  {  "6.2",   GST_H265_LEVEL_L6_2,  35651584 },
};
/* *INDENT-ON* */

static gint
gst_h265_reorder_get_max_dpb_size_from_sps (GstH265Reorder * self,
    GstH265SPS * sps)
{
  guint i;
  guint PicSizeInSamplesY;
  /* Default is the worst case level 6.2 */
  guint32 MaxLumaPS = G_MAXUINT32;
  gint MaxDpbPicBuf = 6;
  gint max_dpb_size;

  /* A.4.2, maxDpbPicBuf is equal to 6 for all profiles where the value of
   * sps_curr_pic_ref_enabled_flag is required to be equal to 0 and 7 for all
   * profiles where the value of sps_curr_pic_ref_enabled_flag is not required
   * to be equal to 0  */
  if (sps->sps_scc_extension_flag) {
    /* sps_curr_pic_ref_enabled_flag could be non-zero only if profile is SCC */
    MaxDpbPicBuf = 7;
  }

  /* Unknown level */
  if (sps->profile_tier_level.level_idc == 0)
    return 16;

  PicSizeInSamplesY = sps->width * sps->height;
  for (i = 0; i < G_N_ELEMENTS (level_limits); i++) {
    if (sps->profile_tier_level.level_idc <= level_limits[i].level_idc) {
      if (PicSizeInSamplesY <= level_limits[i].MaxLumaPs) {
        MaxLumaPS = level_limits[i].MaxLumaPs;
      } else {
        GST_DEBUG_OBJECT (self,
            "%u (%dx%d) exceeds allowed max luma sample for level \"%s\" %u",
            PicSizeInSamplesY, sps->width, sps->height,
            level_limits[i].level_name, level_limits[i].MaxLumaPs);
      }
      break;
    }
  }

  /* Unknown level */
  if (MaxLumaPS == G_MAXUINT32)
    return 16;

  /* A.4.2 */
  if (PicSizeInSamplesY <= (MaxLumaPS >> 2))
    max_dpb_size = MaxDpbPicBuf * 4;
  else if (PicSizeInSamplesY <= (MaxLumaPS >> 1))
    max_dpb_size = MaxDpbPicBuf * 2;
  else if (PicSizeInSamplesY <= ((3 * MaxLumaPS) >> 2))
    max_dpb_size = (MaxDpbPicBuf * 4) / 3;
  else
    max_dpb_size = MaxDpbPicBuf;

  max_dpb_size = MIN (max_dpb_size, 16);

  /* MaxDpbSize is not an actual maximum required buffer size.
   * Instead, it indicates upper bound for other syntax elements, such as
   * sps_max_dec_pic_buffering_minus1. If this bitstream can satisfy
   * the requirement, use this as our dpb size */
  if (sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] + 1 <=
      max_dpb_size) {
    GST_DEBUG_OBJECT (self, "max_dec_pic_buffering_minus1 %d < MaxDpbSize %d",
        sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1],
        max_dpb_size);
    max_dpb_size =
        sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] + 1;
  } else {
    /* not reliable values, use 16 */
    max_dpb_size = 16;
  }

  return max_dpb_size;
}

static gboolean
gst_h265_reorder_process_sps (GstH265Reorder * self, GstH265SPS * sps)
{
  gint max_dpb_size;
  gint prev_max_dpb_size;
  guint8 field_seq_flag = 0;
  guint8 progressive_source_flag = 0;
  guint8 interlaced_source_flag = 0;
  guint frames_delay;

  max_dpb_size = gst_h265_reorder_get_max_dpb_size_from_sps (self, sps);

  if (sps->vui_parameters_present_flag)
    field_seq_flag = sps->vui_params.field_seq_flag;

  progressive_source_flag = sps->profile_tier_level.progressive_source_flag;
  interlaced_source_flag = sps->profile_tier_level.interlaced_source_flag;

  prev_max_dpb_size = gst_h265_dpb_get_max_num_pics (self->dpb);
  if (self->width != sps->width || self->height != sps->height ||
      prev_max_dpb_size != max_dpb_size ||
      self->field_seq_flag != field_seq_flag ||
      self->progressive_source_flag != progressive_source_flag ||
      self->interlaced_source_flag != interlaced_source_flag ||
      gst_h265_reorder_is_crop_rect_changed (self, sps)) {

    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d, "
        "field_seq_flag: %d -> %d, progressive_source_flag: %d -> %d, "
        "interlaced_source_flag: %d -> %d",
        self->width, self->height, sps->width, sps->height,
        prev_max_dpb_size, max_dpb_size, self->field_seq_flag, field_seq_flag,
        self->progressive_source_flag, progressive_source_flag,
        self->interlaced_source_flag, interlaced_source_flag);

    gst_h265_reorder_drain (self);

    self->width = sps->width;
    self->height = sps->height;
    self->conformance_window_flag = sps->conformance_window_flag;
    self->crop_rect_width = sps->crop_rect_width;
    self->crop_rect_height = sps->crop_rect_height;
    self->crop_rect_x = sps->crop_rect_x;
    self->crop_rect_y = sps->crop_rect_y;
    self->field_seq_flag = field_seq_flag;
    self->progressive_source_flag = progressive_source_flag;
    self->interlaced_source_flag = interlaced_source_flag;

    gst_h265_dpb_set_max_num_pics (self->dpb, max_dpb_size);

    GST_DEBUG_OBJECT (self, "Set DPB max size %d", max_dpb_size);
  }

  if (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1]) {
    self->SpsMaxLatencyPictures =
        sps->max_num_reorder_pics[sps->max_sub_layers_minus1] +
        sps->max_latency_increase_plus1[sps->max_sub_layers_minus1] - 1;
  } else {
    self->SpsMaxLatencyPictures = 0;
  }

  frames_delay = sps->max_num_reorder_pics[sps->max_sub_layers_minus1];
  self->latency = gst_util_uint64_scale_int (frames_delay * GST_SECOND,
      self->fps_d, self->fps_n);

  return TRUE;
}

static GstH265ParserResult
gst_h265_reorder_parse_sei (GstH265Reorder * self, GstH265NalUnit * nalu)
{
  GstH265ParserResult pres;
  GArray *messages = NULL;
  guint i;

  pres = gst_h265_parser_parse_sei (self->preproc_parser, nalu, &messages);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SEI, result %d", pres);

    /* XXX: Ignore error from SEI parsing, it might be malformed bitstream,
     * or our fault. But shouldn't be critical  */
    g_clear_pointer (&messages, g_array_unref);
    return GST_H265_PARSER_OK;
  }

  for (i = 0; i < messages->len; i++) {
    GstH265SEIMessage *sei = &g_array_index (messages, GstH265SEIMessage, i);

    switch (sei->payloadType) {
      case GST_H265_SEI_PIC_TIMING:
        self->cur_pic_struct = sei->payload.pic_timing.pic_struct;
        self->cur_source_scan_type = sei->payload.pic_timing.source_scan_type;
        self->cur_duplicate_flag = sei->payload.pic_timing.duplicate_flag;

        GST_TRACE_OBJECT (self,
            "Picture Timing SEI, pic_struct: %d, source_scan_type: %d, "
            "duplicate_flag: %d", self->cur_pic_struct,
            self->cur_source_scan_type, self->cur_duplicate_flag);
        break;
      default:
        break;
    }
  }

  g_array_free (messages, TRUE);
  GST_LOG_OBJECT (self, "SEI parsed");

  return GST_H265_PARSER_OK;
}

static gboolean
gst_h265_reorder_preprocess_slice (GstH265Reorder * self, GstH265Slice * slice)
{
  const GstH265SliceHdr *slice_hdr = &slice->header;

  if (self->current_picture && slice_hdr->first_slice_segment_in_pic_flag) {
    GST_WARNING_OBJECT (self,
        "Current picture is not finished but slice header has "
        "first_slice_segment_in_pic_flag");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h265_reorder_process_slice (GstH265Reorder * self, GstH265Slice * slice)
{
  self->current_slice = *slice;

  if (self->current_slice.header.dependent_slice_segment_flag) {
    GstH265SliceHdr *slice_hdr = &self->current_slice.header;
    GstH265SliceHdr *indep_slice_hdr = &self->prev_independent_slice.header;

    memcpy (&slice_hdr->type, &indep_slice_hdr->type,
        G_STRUCT_OFFSET (GstH265SliceHdr, num_entry_point_offsets) -
        G_STRUCT_OFFSET (GstH265SliceHdr, type));
  } else {
    self->prev_independent_slice = self->current_slice;
    memset (&self->prev_independent_slice.nalu, 0, sizeof (GstH265NalUnit));
  }

  if (!gst_h265_reorder_preprocess_slice (self, &self->current_slice))
    return FALSE;

  /* The used SPS may not be the latest parsed one, make
   * sure we have updated it before decode the frame */
  if (!gst_h265_reorder_process_sps (self, self->current_slice.header.pps->sps)) {
    GST_WARNING_OBJECT (self, "Failed to process sps");
    return FALSE;
  }

  self->active_pps = self->current_slice.header.pps;
  self->active_sps = self->active_pps->sps;

  if (!self->current_picture) {
    GstH265Picture *picture;

    g_assert (self->current_frame);

    picture = gst_h265_picture_new ();
    /* This allows accessing the frame from the picture. */
    GST_CODEC_PICTURE_FRAME_NUMBER (picture) =
        self->current_frame->system_frame_number;

    self->current_picture = picture;

    if (!gst_h265_reorder_start_current_picture (self)) {
      GST_WARNING_OBJECT (self, "start picture failed");
      return FALSE;
    }
  }

  return TRUE;
}

static GstH265ParserResult
gst_h265_reorder_parse_slice (GstH265Reorder * self, GstH265NalUnit * nalu)
{
  GstH265ParserResult pres;
  GstH265Slice slice;
  GstH265ReorderNalUnit decoder_nalu;

  memset (&slice, 0, sizeof (GstH265Slice));

  pres = gst_h265_parser_parse_slice_hdr (self->preproc_parser,
      nalu, &slice.header);
  if (pres != GST_H265_PARSER_OK)
    return pres;

  slice.nalu = *nalu;

  if (nalu->type >= GST_H265_NAL_SLICE_BLA_W_LP &&
      nalu->type <= GST_H265_NAL_SLICE_CRA_NUT) {
    slice.rap_pic_flag = TRUE;
  }

  /* NoRaslOutputFlag == 1 if the current picture is
   * 1) an IDR picture
   * 2) a BLA picture
   * 3) a CRA picture that is the first access unit in the bitstream
   * 4) first picture that follows an end of sequence NAL unit in decoding order
   * 5) has HandleCraAsBlaFlag == 1 (set by external means, so not considering )
   */
  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type) ||
      GST_H265_IS_NAL_TYPE_BLA (nalu->type) ||
      (GST_H265_IS_NAL_TYPE_CRA (nalu->type) && self->new_bitstream) ||
      self->prev_nal_is_eos) {
    slice.no_rasl_output_flag = TRUE;
  }

  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type)) {
    slice.intra_pic_flag = TRUE;

    if (slice.no_rasl_output_flag && !self->new_bitstream) {
      /* C 3.2 */
      slice.clear_dpb = TRUE;
      if (nalu->type == GST_H265_NAL_SLICE_CRA_NUT) {
        slice.no_output_of_prior_pics_flag = TRUE;
      } else {
        slice.no_output_of_prior_pics_flag =
            slice.header.no_output_of_prior_pics_flag;
      }
    }
  }

  if (slice.no_output_of_prior_pics_flag)
    self->no_output_of_prior_pics_flag = TRUE;

  decoder_nalu.unit.slice = slice;
  decoder_nalu.nalu_type = nalu->type;

  g_array_append_val (self->nalu, decoder_nalu);

  return GST_H265_PARSER_OK;
}

static GstH265ParserResult
gst_h265_reorder_parse_nalu (GstH265Reorder * self, GstH265NalUnit * nalu)
{
  GstH265VPS vps;
  GstH265SPS sps;
  GstH265PPS pps;
  GstH265ParserResult ret = GST_H265_PARSER_OK;
  GstH265ReorderNalUnit decoder_nalu;

  GST_LOG_OBJECT (self, "Parsed nal type: %d, offset %d, size %d",
      nalu->type, nalu->offset, nalu->size);

  memset (&decoder_nalu, 0, sizeof (GstH265ReorderNalUnit));
  decoder_nalu.nalu_type = nalu->type;

  switch (nalu->type) {
    case GST_H265_NAL_VPS:
      ret = gst_h265_parser_parse_vps (self->preproc_parser, nalu, &vps);
      if (ret != GST_H265_PARSER_OK)
        break;

      decoder_nalu.unit.vps = vps;
      g_array_append_val (self->nalu, decoder_nalu);
      break;
    case GST_H265_NAL_SPS:
      ret = gst_h265_parser_parse_sps (self->preproc_parser, nalu, &sps, TRUE);
      if (ret != GST_H265_PARSER_OK)
        break;

      decoder_nalu.unit.sps = sps;
      g_array_append_val (self->nalu, decoder_nalu);
      break;
    case GST_H265_NAL_PPS:
      ret = gst_h265_parser_parse_pps (self->preproc_parser, nalu, &pps);
      if (ret != GST_H265_PARSER_OK)
        break;

      decoder_nalu.unit.pps = pps;
      g_array_append_val (self->nalu, decoder_nalu);
      break;
    case GST_H265_NAL_PREFIX_SEI:
    case GST_H265_NAL_SUFFIX_SEI:
      ret = gst_h265_reorder_parse_sei (self, nalu);
      break;
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
      ret = gst_h265_reorder_parse_slice (self, nalu);
      self->new_bitstream = FALSE;
      self->prev_nal_is_eos = FALSE;
      break;
    case GST_H265_NAL_EOB:
      self->new_bitstream = TRUE;
      break;
    case GST_H265_NAL_EOS:
      self->prev_nal_is_eos = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_h265_reorder_decode_nalu (GstH265Reorder * self,
    GstH265ReorderNalUnit * nalu)
{
  GstH265ParserResult rst;

  switch (nalu->nalu_type) {
    case GST_H265_NAL_VPS:
      gst_h265_parser_update_vps (self->parser, &nalu->unit.vps);
      return TRUE;
    case GST_H265_NAL_SPS:
      gst_h265_parser_update_sps (self->parser, &nalu->unit.sps);
      return TRUE;
    case GST_H265_NAL_PPS:
      gst_h265_parser_update_pps (self->parser, &nalu->unit.pps);
      return TRUE;
    default:
      if (!is_slice_nalu (nalu->nalu_type)) {
        GST_WARNING_OBJECT (self, "Unexpected nal type %d", nalu->nalu_type);
        return TRUE;
      }
      break;
  }

  rst = gst_h265_parser_link_slice_hdr (self->parser, &nalu->unit.slice.header);

  if (rst != GST_H265_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Couldn't update slice header");
    return FALSE;
  }

  return gst_h265_reorder_process_slice (self, &nalu->unit.slice);
}

static gboolean
gst_h265_reorder_parse_codec_data (GstH265Reorder * self, const guint8 * data,
    gsize size)
{
  GstH265Parser *parser = self->parser;
  GstH265ParserResult pres;
  gboolean ret = FALSE;
  GstH265VPS vps;
  GstH265SPS sps;
  GstH265PPS pps;
  GstH265DecoderConfigRecord *config = NULL;
  guint i, j;

  pres = gst_h265_parser_parse_decoder_config_record (parser,
      data, size, &config);
  if (pres != GST_H265_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse hvcC data");
    return FALSE;
  }

  self->nal_length_size = config->length_size_minus_one + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", self->nal_length_size);

  for (i = 0; i < config->nalu_array->len; i++) {
    GstH265DecoderConfigRecordNalUnitArray *array =
        &g_array_index (config->nalu_array,
        GstH265DecoderConfigRecordNalUnitArray, i);

    for (j = 0; j < array->nalu->len; j++) {
      GstH265NalUnit *nalu = &g_array_index (array->nalu, GstH265NalUnit, j);

      switch (nalu->type) {
        case GST_H265_NAL_VPS:
          pres = gst_h265_parser_parse_vps (parser, nalu, &vps);
          if (pres != GST_H265_PARSER_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse VPS");
            goto out;
          }
          gst_h265_parser_update_vps (self->preproc_parser, &vps);
          break;
        case GST_H265_NAL_SPS:
          pres = gst_h265_parser_parse_sps (parser, nalu, &sps, TRUE);
          if (pres != GST_H265_PARSER_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse SPS");
            goto out;
          }
          gst_h265_parser_update_sps (self->preproc_parser, &sps);
          break;
        case GST_H265_NAL_PPS:
          pres = gst_h265_parser_parse_pps (parser, nalu, &pps);
          if (pres != GST_H265_PARSER_OK) {
            GST_WARNING_OBJECT (self, "Failed to parse PPS");
            goto out;
          }
          gst_h265_parser_update_pps (self->preproc_parser, &pps);
          break;
        default:
          break;
      }
    }
  }

  ret = TRUE;

out:
  gst_h265_decoder_config_record_free (config);
  return ret;
}

gboolean
gst_h265_reorder_set_caps (GstH265Reorder * self, GstCaps * caps,
    GstClockTime * latency)
{
  GstStructure *s;
  const gchar *str;
  const GValue *codec_data;
  gboolean ret = TRUE;
  gint fps_n, fps_d;

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  self->nal_length_size = 4;
  self->is_hevc = FALSE;

  s = gst_caps_get_structure (caps, 0);
  str = gst_structure_get_string (s, "stream-format");
  if (str && (g_strcmp0 (str, "hvc1") == 0 || g_strcmp0 (str, "hev1") == 0))
    self->is_hevc = TRUE;

  if (gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d) &&
      fps_n > 0 && fps_d > 0) {
    self->fps_n = fps_n;
    self->fps_d = fps_d;
  } else {
    self->fps_n = 25;
    self->fps_d = 1;
  }

  codec_data = gst_structure_get_value (s, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    GstBuffer *buf = gst_value_get_buffer (codec_data);
    GstMapInfo info;
    if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
      ret = gst_h265_reorder_parse_codec_data (self, info.data, info.size);
      gst_buffer_unmap (buf, &info);
    } else {
      GST_ERROR_OBJECT (self, "Couldn't map codec data");
      ret = FALSE;
    }
  }

  if (self->need_reorder)
    *latency = self->latency;
  else
    *latency = 0;

  return ret;
}

static gboolean
gst_h265_reorder_fill_picture_from_slice (GstH265Reorder * self,
    const GstH265Slice * slice, GstH265Picture * picture)
{
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;

  picture->RapPicFlag = slice->rap_pic_flag;
  picture->NoRaslOutputFlag = slice->no_rasl_output_flag;
  picture->IntraPicFlag = slice->intra_pic_flag;
  picture->NoOutputOfPriorPicsFlag = slice->no_output_of_prior_pics_flag;
  if (picture->IntraPicFlag) {
    self->associated_irap_NoRaslOutputFlag = picture->NoRaslOutputFlag;
  }

  if (GST_H265_IS_NAL_TYPE_RASL (nalu->type) &&
      self->associated_irap_NoRaslOutputFlag) {
    picture->output_flag = FALSE;
  } else {
    picture->output_flag = slice_hdr->pic_output_flag;
  }

  return TRUE;
}

#define RSV_VCL_N10 10
#define RSV_VCL_N12 12
#define RSV_VCL_N14 14

static gboolean
nal_is_ref (guint8 nal_type)
{
  gboolean ret = FALSE;
  switch (nal_type) {
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RASL_N:
    case RSV_VCL_N10:
    case RSV_VCL_N12:
    case RSV_VCL_N14:
      ret = FALSE;
      break;
    default:
      ret = TRUE;
      break;
  }
  return ret;
}

static gboolean
gst_h265_reorder_calculate_poc (GstH265Reorder * self,
    const GstH265Slice * slice, GstH265Picture * picture)
{
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;
  const GstH265SPS *sps = self->active_sps;
  gint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gboolean is_irap;

  self->prev_poc_lsb = self->poc_lsb;
  self->prev_poc_msb = self->poc_msb;

  is_irap = GST_H265_IS_NAL_TYPE_IRAP (nalu->type);

  if (!(is_irap && picture->NoRaslOutputFlag)) {
    self->prev_poc_lsb = self->prev_tid0pic_poc_lsb;
    self->prev_poc_msb = self->prev_tid0pic_poc_msb;
  }

  /* Finding PicOrderCntMsb */
  if (is_irap && picture->NoRaslOutputFlag) {
    self->poc_msb = 0;
  } else {
    /* (8-1) */
    if ((slice_hdr->pic_order_cnt_lsb < self->prev_poc_lsb) &&
        ((self->prev_poc_lsb - slice_hdr->pic_order_cnt_lsb) >=
            (MaxPicOrderCntLsb / 2)))
      self->poc_msb = self->prev_poc_msb + MaxPicOrderCntLsb;

    else if ((slice_hdr->pic_order_cnt_lsb > self->prev_poc_lsb) &&
        ((slice_hdr->pic_order_cnt_lsb - self->prev_poc_lsb) >
            (MaxPicOrderCntLsb / 2)))
      self->poc_msb = self->prev_poc_msb - MaxPicOrderCntLsb;

    else
      self->poc_msb = self->prev_poc_msb;
  }

  /* (8-2) */
  self->poc = picture->pic_order_cnt =
      self->poc_msb + slice_hdr->pic_order_cnt_lsb;
  self->poc_lsb = picture->pic_order_cnt_lsb = slice_hdr->pic_order_cnt_lsb;

  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type)) {
    picture->pic_order_cnt = 0;
    picture->pic_order_cnt_lsb = 0;
    self->poc_lsb = 0;
    self->poc_msb = 0;
    self->prev_poc_lsb = 0;
    self->prev_poc_msb = 0;
    self->prev_tid0pic_poc_lsb = 0;
    self->prev_tid0pic_poc_msb = 0;
  }

  GST_LOG_OBJECT (self,
      "PicOrderCntVal %d, (lsb %d)", picture->pic_order_cnt,
      picture->pic_order_cnt_lsb);

  if (nalu->temporal_id_plus1 == 1 && !GST_H265_IS_NAL_TYPE_RASL (nalu->type) &&
      !GST_H265_IS_NAL_TYPE_RADL (nalu->type) && nal_is_ref (nalu->type)) {
    self->prev_tid0pic_poc_lsb = slice_hdr->pic_order_cnt_lsb;
    self->prev_tid0pic_poc_msb = self->poc_msb;
  }

  return TRUE;
}

static gboolean
gst_h265_reorder_init_current_picture (GstH265Reorder * self)
{
  if (!gst_h265_reorder_fill_picture_from_slice (self, &self->current_slice,
          self->current_picture)) {
    return FALSE;
  }

  if (!gst_h265_reorder_calculate_poc (self,
          &self->current_slice, self->current_picture))
    return FALSE;

  /* Use picture struct parsed from picture timing SEI */
  self->current_picture->pic_struct = self->cur_pic_struct;
  self->current_picture->source_scan_type = self->cur_source_scan_type;
  self->current_picture->duplicate_flag = self->cur_duplicate_flag;

  return TRUE;
}

static gboolean
has_entry_in_rps (GstH265Picture * dpb_pic,
    GstH265Picture ** rps_list, guint rps_list_length)
{
  guint i;

  if (!dpb_pic || !rps_list || !rps_list_length)
    return FALSE;

  for (i = 0; i < rps_list_length; i++) {
    if (rps_list[i] && rps_list[i]->pic_order_cnt == dpb_pic->pic_order_cnt)
      return TRUE;
  }
  return FALSE;
}

static void
gst_h265_reorder_derive_and_mark_rps (GstH265Reorder * self,
    GstH265Picture * picture, gint32 * CurrDeltaPocMsbPresentFlag,
    gint32 * FollDeltaPocMsbPresentFlag)
{
  guint i;
  GArray *dpb_array;

  gst_h265_reorder_clear_ref_pic_sets (self);

  /* (8-6) */
  for (i = 0; i < self->NumPocLtCurr; i++) {
    if (!CurrDeltaPocMsbPresentFlag[i]) {
      self->RefPicSetLtCurr[i] =
          gst_h265_dpb_get_ref_by_poc_lsb (self->dpb, self->PocLtCurr[i]);
    } else {
      self->RefPicSetLtCurr[i] =
          gst_h265_dpb_get_ref_by_poc (self->dpb, self->PocLtCurr[i]);
    }
  }

  for (i = 0; i < self->NumPocLtFoll; i++) {
    if (!FollDeltaPocMsbPresentFlag[i]) {
      self->RefPicSetLtFoll[i] =
          gst_h265_dpb_get_ref_by_poc_lsb (self->dpb, self->PocLtFoll[i]);
    } else {
      self->RefPicSetLtFoll[i] =
          gst_h265_dpb_get_ref_by_poc (self->dpb, self->PocLtFoll[i]);
    }
  }

  /* Mark all ref pics in RefPicSetLtCurr and RefPicSetLtFol as long_term_refs */
  for (i = 0; i < self->NumPocLtCurr; i++) {
    if (self->RefPicSetLtCurr[i]) {
      self->RefPicSetLtCurr[i]->ref = TRUE;
      self->RefPicSetLtCurr[i]->long_term = TRUE;
    }
  }

  for (i = 0; i < self->NumPocLtFoll; i++) {
    if (self->RefPicSetLtFoll[i]) {
      self->RefPicSetLtFoll[i]->ref = TRUE;
      self->RefPicSetLtFoll[i]->long_term = TRUE;
    }
  }

  /* (8-7) */
  for (i = 0; i < self->NumPocStCurrBefore; i++) {
    self->RefPicSetStCurrBefore[i] =
        gst_h265_dpb_get_short_ref_by_poc (self->dpb, self->PocStCurrBefore[i]);
  }

  for (i = 0; i < self->NumPocStCurrAfter; i++) {
    self->RefPicSetStCurrAfter[i] =
        gst_h265_dpb_get_short_ref_by_poc (self->dpb, self->PocStCurrAfter[i]);
  }

  for (i = 0; i < self->NumPocStFoll; i++) {
    self->RefPicSetStFoll[i] =
        gst_h265_dpb_get_short_ref_by_poc (self->dpb, self->PocStFoll[i]);
  }

  /* Mark all dpb pics not beloging to RefPicSet*[] as unused for ref */
  dpb_array = gst_h265_dpb_get_pictures_all (self->dpb);
  for (i = 0; i < dpb_array->len; i++) {
    GstH265Picture *dpb_pic = g_array_index (dpb_array, GstH265Picture *, i);

    if (dpb_pic &&
        !has_entry_in_rps (dpb_pic, self->RefPicSetLtCurr, self->NumPocLtCurr)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetLtFoll,
            self->NumPocLtFoll)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStCurrAfter,
            self->NumPocStCurrAfter)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStCurrBefore,
            self->NumPocStCurrBefore)
        && !has_entry_in_rps (dpb_pic, self->RefPicSetStFoll,
            self->NumPocStFoll)) {
      GST_LOG_OBJECT (self, "Mark Picture %p (poc %d) as non-ref", dpb_pic,
          dpb_pic->pic_order_cnt);
      dpb_pic->ref = FALSE;
      dpb_pic->long_term = FALSE;
    }
  }

  g_array_unref (dpb_array);
}

static gboolean
gst_h265_reorder_prepare_rps (GstH265Reorder * self, const GstH265Slice * slice,
    GstH265Picture * picture)
{
  gint32 CurrDeltaPocMsbPresentFlag[16] = { 0, };
  gint32 FollDeltaPocMsbPresentFlag[16] = { 0, };
  const GstH265SliceHdr *slice_hdr = &slice->header;
  const GstH265NalUnit *nalu = &slice->nalu;
  const GstH265SPS *sps = self->active_sps;
  guint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gint i, j, k;

  /* if it is an irap pic, set all ref pics in dpb as unused for ref */
  if (GST_H265_IS_NAL_TYPE_IRAP (nalu->type) && picture->NoRaslOutputFlag) {
    GST_DEBUG_OBJECT (self, "Mark all pictures in DPB as non-ref");
    gst_h265_dpb_mark_all_non_ref (self->dpb);
  }

  /* Reset everything for IDR */
  if (GST_H265_IS_NAL_TYPE_IDR (nalu->type)) {
    memset (self->PocStCurrBefore, 0, sizeof (self->PocStCurrBefore));
    memset (self->PocStCurrAfter, 0, sizeof (self->PocStCurrAfter));
    memset (self->PocStFoll, 0, sizeof (self->PocStFoll));
    memset (self->PocLtCurr, 0, sizeof (self->PocLtCurr));
    memset (self->PocLtFoll, 0, sizeof (self->PocLtFoll));
    self->NumPocStCurrBefore = self->NumPocStCurrAfter = self->NumPocStFoll = 0;
    self->NumPocLtCurr = self->NumPocLtFoll = 0;
  } else {
    const GstH265ShortTermRefPicSet *stRefPic = NULL;
    gint32 num_lt_pics, pocLt;
    gint32 PocLsbLt[16] = { 0, };
    gint32 UsedByCurrPicLt[16] = { 0, };
    gint32 DeltaPocMsbCycleLt[16] = { 0, };
    gint numtotalcurr = 0;

    /* this is based on CurrRpsIdx described in spec */
    if (!slice_hdr->short_term_ref_pic_set_sps_flag)
      stRefPic = &slice_hdr->short_term_ref_pic_sets;
    else if (sps->num_short_term_ref_pic_sets)
      stRefPic =
          &sps->short_term_ref_pic_set[slice_hdr->short_term_ref_pic_set_idx];

    if (stRefPic == NULL)
      return FALSE;

    GST_LOG_OBJECT (self,
        "NumDeltaPocs: %d, NumNegativePics: %d, NumPositivePics %d",
        stRefPic->NumDeltaPocs, stRefPic->NumNegativePics,
        stRefPic->NumPositivePics);

    for (i = 0, j = 0, k = 0; i < stRefPic->NumNegativePics; i++) {
      if (stRefPic->UsedByCurrPicS0[i]) {
        self->PocStCurrBefore[j++] =
            picture->pic_order_cnt + stRefPic->DeltaPocS0[i];
        numtotalcurr++;
      } else
        self->PocStFoll[k++] = picture->pic_order_cnt + stRefPic->DeltaPocS0[i];
    }
    self->NumPocStCurrBefore = j;
    for (i = 0, j = 0; i < stRefPic->NumPositivePics; i++) {
      if (stRefPic->UsedByCurrPicS1[i]) {
        self->PocStCurrAfter[j++] =
            picture->pic_order_cnt + stRefPic->DeltaPocS1[i];
        numtotalcurr++;
      } else
        self->PocStFoll[k++] = picture->pic_order_cnt + stRefPic->DeltaPocS1[i];
    }
    self->NumPocStCurrAfter = j;
    self->NumPocStFoll = k;
    num_lt_pics = slice_hdr->num_long_term_sps + slice_hdr->num_long_term_pics;
    /* The variables PocLsbLt[i] and UsedByCurrPicLt[i] are derived as follows: */
    for (i = 0; i < num_lt_pics; i++) {
      if (i < slice_hdr->num_long_term_sps) {
        PocLsbLt[i] = sps->lt_ref_pic_poc_lsb_sps[slice_hdr->lt_idx_sps[i]];
        UsedByCurrPicLt[i] =
            sps->used_by_curr_pic_lt_sps_flag[slice_hdr->lt_idx_sps[i]];
      } else {
        PocLsbLt[i] = slice_hdr->poc_lsb_lt[i];
        UsedByCurrPicLt[i] = slice_hdr->used_by_curr_pic_lt_flag[i];
      }
      if (UsedByCurrPicLt[i])
        numtotalcurr++;
    }

    self->NumPicTotalCurr = numtotalcurr;

    /* The variable DeltaPocMsbCycleLt[i] is derived as follows: (7-38) */
    for (i = 0; i < num_lt_pics; i++) {
      if (i == 0 || i == slice_hdr->num_long_term_sps)
        DeltaPocMsbCycleLt[i] = slice_hdr->delta_poc_msb_cycle_lt[i];
      else
        DeltaPocMsbCycleLt[i] =
            slice_hdr->delta_poc_msb_cycle_lt[i] + DeltaPocMsbCycleLt[i - 1];
    }

    /* (8-5) */
    for (i = 0, j = 0, k = 0; i < num_lt_pics; i++) {
      pocLt = PocLsbLt[i];
      if (slice_hdr->delta_poc_msb_present_flag[i])
        pocLt +=
            picture->pic_order_cnt - DeltaPocMsbCycleLt[i] * MaxPicOrderCntLsb -
            slice_hdr->pic_order_cnt_lsb;
      if (UsedByCurrPicLt[i]) {
        self->PocLtCurr[j] = pocLt;
        CurrDeltaPocMsbPresentFlag[j++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      } else {
        self->PocLtFoll[k] = pocLt;
        FollDeltaPocMsbPresentFlag[k++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      }
    }
    self->NumPocLtCurr = j;
    self->NumPocLtFoll = k;
  }

  GST_LOG_OBJECT (self, "NumPocStCurrBefore: %d", self->NumPocStCurrBefore);
  GST_LOG_OBJECT (self, "NumPocStCurrAfter:  %d", self->NumPocStCurrAfter);
  GST_LOG_OBJECT (self, "NumPocStFoll:       %d", self->NumPocStFoll);
  GST_LOG_OBJECT (self, "NumPocLtCurr:       %d", self->NumPocLtCurr);
  GST_LOG_OBJECT (self, "NumPocLtFoll:       %d", self->NumPocLtFoll);
  GST_LOG_OBJECT (self, "NumPicTotalCurr:    %d", self->NumPicTotalCurr);

  /* the derivation process for the RPS and the picture marking */
  gst_h265_reorder_derive_and_mark_rps (self, picture,
      CurrDeltaPocMsbPresentFlag, FollDeltaPocMsbPresentFlag);

  return TRUE;
}

static void
gst_h265_reorder_set_output_buffer (GstH265Reorder * self, guint frame_num)
{
  gsize i, j;

  for (i = 0; i < self->frame_queue->len; i++) {
    GstVideoCodecFrame *frame = g_ptr_array_index (self->frame_queue, i);
    if (frame->system_frame_number != frame_num)
      continue;

    /* Copy frame at present index to  */
    if (!frame->output_buffer) {
      GST_LOG_OBJECT (self, "decoding order: %u, display order: %u",
          frame_num, self->present_num);
      frame->presentation_frame_number = self->present_num;
      self->present_num++;
      for (j = 0; j < self->frame_queue->len; j++) {
        GstVideoCodecFrame *other_frame =
            g_ptr_array_index (self->frame_queue, j);
        if (other_frame->system_frame_number ==
            frame->presentation_frame_number) {
          frame->output_buffer = gst_buffer_ref (other_frame->input_buffer);
          return;
        }
      }
    }

    break;
  }
}

static void
gst_h265_reorder_output_picture (GstH265Reorder * self,
    GstH265Picture * picture)
{
  guint frame_num = GST_CODEC_PICTURE_FRAME_NUMBER (picture);

  gst_h265_reorder_set_output_buffer (self, frame_num);
  gst_h265_picture_unref (picture);

  /* Move completed frames to output queue */
  while (self->frame_queue->len > 0) {
    GstVideoCodecFrame *frame = g_ptr_array_index (self->frame_queue, 0);
    if (!frame->output_buffer)
      break;

    frame = g_ptr_array_steal_index (self->frame_queue, 0);
    g_ptr_array_add (self->output_queue, frame);
  }
}

GstH265Reorder *
gst_h265_reorder_new (gboolean need_reorder)
{
  GstH265Reorder *self = g_object_new (GST_TYPE_H265_REORDER, NULL);
  gst_object_ref_sink (self);

  self->need_reorder = need_reorder;

  return self;
}

void
gst_h265_reorder_drain (GstH265Reorder * reorder)
{
  GstH265Picture *picture;

  while ((picture = gst_h265_dpb_bump (reorder->dpb, TRUE)) != NULL) {
    gst_h265_reorder_output_picture (reorder, picture);
  }

  gst_h265_dpb_clear (reorder->dpb);

  /* Frame queue should be empty or holding only current frame */
  while (reorder->frame_queue->len > 0) {
    GstVideoCodecFrame *frame = g_ptr_array_index (reorder->frame_queue, 0);
    if (frame == reorder->current_frame)
      break;

    GST_WARNING_OBJECT (reorder, "Remaining frame after drain %" GST_PTR_FORMAT,
        frame->input_buffer);

    /* Move to output queue anyway  */
    frame->output_buffer = gst_buffer_ref (frame->input_buffer);
    frame = g_ptr_array_steal_index (reorder->frame_queue, 0);
    g_ptr_array_add (reorder->output_queue, frame);
  }

  /* presentation number */
  if (reorder->current_frame)
    reorder->present_num = reorder->current_frame->system_frame_number;
  else
    reorder->present_num = reorder->system_num;
}

/* C.5.2.2 */
static gboolean
gst_h265_reorder_dpb_init (GstH265Reorder * self, const GstH265Slice * slice,
    GstH265Picture * picture)
{
  const GstH265SPS *sps = self->active_sps;
  GstH265Picture *to_output;

  /* C 3.2 */
  if (slice->clear_dpb) {
    /* Ignores NoOutputOfPriorPicsFlag and drain all */
    gst_h265_reorder_drain (self);
  } else {
    /* TODO: According to 7.4.3.3.3, TwoVersionsOfCurrDecPicFlag
     * should be considered.
     *
     * NOTE: (See 8.1.3) if TwoVersionsOfCurrDecPicFlag is 1,
     * current picture requires two picture buffers allocated in DPB storage,
     * one is decoded picture *after* in-loop filter, and the other is
     * decoded picture *before* in-loop filter, so that current picture
     * can be used as a reference of the current picture
     * (e.g., intra block copy method in SCC).
     * Here TwoVersionsOfCurrDecPicFlag takes effect in order to ensure
     * at least two empty DPB buffer before starting current picture decoding.
     *
     * However, two DPB picture allocation is not implemented
     * in current baseclass (which would imply that we are doing reference
     * picture management wrongly in case of SCC).
     * Let's ignore TwoVersionsOfCurrDecPicFlag for now */
    guint max_dec_pic_buffering =
        sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] + 1;
    gst_h265_dpb_delete_unused (self->dpb);
    while (gst_h265_dpb_needs_bump (self->dpb,
            sps->max_num_reorder_pics[sps->max_sub_layers_minus1],
            self->SpsMaxLatencyPictures, max_dec_pic_buffering)) {
      to_output = gst_h265_dpb_bump (self->dpb, FALSE);

      /* Something wrong... */
      if (!to_output) {
        GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
        break;
      }

      gst_h265_reorder_output_picture (self, to_output);
    }
  }

  return TRUE;
}

static gboolean
gst_h265_reorder_start_current_picture (GstH265Reorder * self)
{
  g_assert (self->current_picture != NULL);
  g_assert (self->active_sps != NULL);
  g_assert (self->active_pps != NULL);

  if (!gst_h265_reorder_init_current_picture (self))
    return FALSE;

  /* Drop all RASL pictures having NoRaslOutputFlag is TRUE for the
   * associated IRAP picture */
  if (GST_H265_IS_NAL_TYPE_RASL (self->current_slice.nalu.type) &&
      self->associated_irap_NoRaslOutputFlag) {
    GST_DEBUG_OBJECT (self, "Ignores associated_irap_NoRaslOutputFlag");
  }

  if (!gst_h265_reorder_prepare_rps (self, &self->current_slice,
          self->current_picture)) {
    GST_WARNING_OBJECT (self, "Failed to prepare ref pic set");
    gst_clear_h265_picture (&self->current_picture);
    return FALSE;
  }

  if (!gst_h265_reorder_dpb_init (self,
          &self->current_slice, self->current_picture)) {
    GST_WARNING_OBJECT (self, "Failed to init dpb");
    gst_clear_h265_picture (&self->current_picture);
    return FALSE;
  }

  return TRUE;
}

static void
gst_h265_reorder_finish_picture (GstH265Reorder * self,
    GstH265Picture * picture)
{
  const GstH265SPS *sps = self->active_sps;

  GST_LOG_OBJECT (self,
      "Finishing picture %p (poc %d), entries in DPB %d",
      picture, picture->pic_order_cnt, gst_h265_dpb_get_size (self->dpb));

  gst_h265_dpb_delete_unused (self->dpb);

  /* gst_h265_dpb_add() will take care of pic_latency_cnt increment and
   * reference picture marking for this picture */
  gst_h265_dpb_add (self->dpb, picture);

  /* NOTE: As per C.5.2.2, bumping by sps_max_dec_pic_buffering_minus1 is
   * applied only for the output and removal of pictures from the DPB before
   * the decoding of the current picture. So pass zero here */
  while (gst_h265_dpb_needs_bump (self->dpb,
          sps->max_num_reorder_pics[sps->max_sub_layers_minus1],
          self->SpsMaxLatencyPictures, 0)) {
    GstH265Picture *to_output = gst_h265_dpb_bump (self->dpb, FALSE);

    /* Something wrong... */
    if (!to_output) {
      GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
      break;
    }

    gst_h265_reorder_output_picture (self, to_output);
  }
}

static void
gst_h265_reorder_reset_frame_state (GstH265Reorder * self)
{
  /* Clear picture struct information */
  self->cur_pic_struct = GST_H265_SEI_PIC_STRUCT_FRAME;
  self->cur_source_scan_type = 2;
  self->cur_duplicate_flag = 0;
  self->no_output_of_prior_pics_flag = FALSE;
  self->current_frame = NULL;
  g_array_set_size (self->nalu, 0);
}

static GstBuffer *
gst_h265_reorder_remove_caption_sei (GstH265Reorder * self, GstBuffer * buffer)
{
  GstH265ParserResult pres = GST_H265_PARSER_OK;
  GstMapInfo map;
  GstH265NalUnit nalu;
  guint i;
  gboolean have_sei = FALSE;
  GstBuffer *new_buf;

  g_array_set_size (self->au_nalus, 0);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (self->is_hevc) {
    guint offset = 0;
    gsize consumed = 0;
    guint i;

    do {
      pres = gst_h265_parser_identify_and_split_nalu_hevc (self->parser,
          map.data, offset, map.size, self->nal_length_size,
          self->split_nalu, &consumed);
      if (pres != GST_H265_PARSER_OK)
        break;

      for (i = 0; i < self->split_nalu->len; i++) {
        nalu = g_array_index (self->split_nalu, GstH265NalUnit, i);
        g_array_append_val (self->au_nalus, nalu);
      }

      offset += consumed;
    } while (pres == GST_H265_PARSER_OK);
  } else {
    pres = gst_h265_parser_identify_nalu (self->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H265_PARSER_NO_NAL_END)
      pres = GST_H265_PARSER_OK;

    while (pres == GST_H265_PARSER_OK) {
      g_array_append_val (self->au_nalus, nalu);

      pres = gst_h265_parser_identify_nalu (self->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H265_PARSER_NO_NAL_END)
        pres = GST_H265_PARSER_OK;
    }
  }

  /* Fast scan without parsing */
  for (i = 0; i < self->au_nalus->len; i++) {
    GstH265NalUnit *nl = &g_array_index (self->au_nalus, GstH265NalUnit, i);
    switch (nl->type) {
      case GST_H265_NAL_VPS:
      {
        GstH265VPS vps;
        gst_h265_parser_parse_vps (self->parser, nl, &vps);
        break;
      }
      case GST_H265_NAL_SPS:
      {
        GstH265SPS sps;
        gst_h265_parser_parse_sps (self->parser, nl, &sps, TRUE);
        break;
      }
      case GST_H265_NAL_PREFIX_SEI:
      case GST_H265_NAL_SUFFIX_SEI:
        have_sei = TRUE;
        break;
      default:
        break;
    }
  }

  if (!have_sei) {
    GST_LOG_OBJECT (self, "Buffer without SEI, %" GST_PTR_FORMAT, buffer);
    gst_buffer_unmap (buffer, &map);
    g_array_set_size (self->au_nalus, 0);
    return gst_buffer_ref (buffer);
  }

  new_buf = gst_buffer_new ();
  gst_buffer_copy_into (new_buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);

  for (i = 0; i < self->au_nalus->len; i++) {
    GstH265NalUnit *nl = &g_array_index (self->au_nalus, GstH265NalUnit, i);
    GstMemory *mem = NULL;

    if (nl->type == GST_H265_NAL_PREFIX_SEI ||
        nl->type == GST_H265_NAL_SUFFIX_SEI) {
      GArray *msg = NULL;
      gint j;
      gst_h265_parser_parse_sei (self->parser, nl, &msg);
      gboolean have_caption_sei = FALSE;

      for (j = 0; j < (gint) msg->len; j++) {
        GstH265SEIMessage *sei = &g_array_index (msg, GstH265SEIMessage, j);
        GstH265RegisteredUserData *rud;
        if (sei->payloadType != GST_H265_SEI_REGISTERED_USER_DATA)
          continue;

        rud = &sei->payload.registered_user_data;

        if (!gst_h264_reorder_is_cea708_sei (rud->country_code,
                rud->data, rud->size)) {
          continue;
        }

        GST_LOG_OBJECT (self, "Found CEA708 caption SEI");
        have_caption_sei = TRUE;

        g_array_remove_index (msg, j);
        j--;
      }

      if (have_caption_sei) {
        if (msg->len > 0) {
          /* Creates new SEI memory */
          if (self->is_hevc) {
            mem = gst_h265_create_sei_memory_hevc (nl->layer_id,
                nl->temporal_id_plus1, self->nal_length_size, msg);
          } else {
            mem = gst_h265_create_sei_memory (nl->layer_id,
                nl->temporal_id_plus1, 4, msg);
          }

          if (!mem)
            GST_ERROR_OBJECT (self, "Couldn't create SEI memory");
          else
            gst_buffer_append_memory (new_buf, mem);
        }
      } else {
        gsize size = nl->size + (nl->offset - nl->sc_offset);
        gpointer *data = g_memdup2 (nl->data + nl->sc_offset, size);
        mem = gst_memory_new_wrapped (0, data, size, 0, size, data, g_free);
        gst_buffer_append_memory (new_buf, mem);
      }

      g_array_unref (msg);
    } else {
      gsize size = nl->size + (nl->offset - nl->sc_offset);
      gpointer *data = g_memdup2 (nl->data + nl->sc_offset, size);
      mem = gst_memory_new_wrapped (0, data, size, 0, size, data, g_free);
      gst_buffer_append_memory (new_buf, mem);
    }
  }

  gst_buffer_unmap (buffer, &map);
  g_array_set_size (self->au_nalus, 0);

  return new_buf;
}

gboolean
gst_h265_reorder_push (GstH265Reorder * reorder, GstVideoCodecFrame * frame,
    GstClockTime * latency)
{
  GstBuffer *in_buf;
  GstH265NalUnit nalu;
  GstH265ParserResult pres = GST_H265_PARSER_OK;
  GstMapInfo map;
  gboolean decode_ret = TRUE;
  guint i;

  gst_h265_reorder_reset_frame_state (reorder);

  frame->system_frame_number = reorder->system_num;
  frame->decode_frame_number = reorder->system_num;

  GST_LOG_OBJECT (reorder,
      "Push frame %u, frame queue size: %u, output queue size %u",
      frame->system_frame_number, reorder->frame_queue->len,
      reorder->output_queue->len);

  in_buf = gst_h265_reorder_remove_caption_sei (reorder, frame->input_buffer);
  if (in_buf) {
    gst_buffer_unref (frame->input_buffer);
    frame->input_buffer = in_buf;
  } else {
    in_buf = frame->input_buffer;
  }

  reorder->system_num++;

  if (!reorder->need_reorder) {
    g_ptr_array_add (reorder->output_queue, frame);
    *latency = 0;
    return TRUE;
  }

  g_ptr_array_add (reorder->frame_queue, frame);
  reorder->current_frame = frame;

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  if (reorder->is_hevc) {
    guint offset = 0;
    gsize consumed = 0;

    do {
      pres = gst_h265_parser_identify_and_split_nalu_hevc (reorder->parser,
          map.data, offset, map.size, reorder->nal_length_size,
          reorder->split_nalu, &consumed);
      if (pres != GST_H265_PARSER_OK)
        break;

      for (i = 0; i < reorder->split_nalu->len; i++) {
        GstH265NalUnit *nl =
            &g_array_index (reorder->split_nalu, GstH265NalUnit, i);
        pres = gst_h265_reorder_parse_nalu (reorder, nl);
        if (pres != GST_H265_PARSER_OK)
          break;
      }

      if (pres != GST_H265_PARSER_OK)
        break;

      offset += consumed;
    } while (pres == GST_H265_PARSER_OK);
  } else {
    pres = gst_h265_parser_identify_nalu (reorder->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H265_PARSER_NO_NAL_END)
      pres = GST_H265_PARSER_OK;

    while (pres == GST_H265_PARSER_OK) {
      pres = gst_h265_reorder_parse_nalu (reorder, &nalu);
      if (pres != GST_H265_PARSER_OK)
        break;

      pres = gst_h265_parser_identify_nalu (reorder->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);
      if (pres == GST_H265_PARSER_NO_NAL_END)
        pres = GST_H265_PARSER_OK;
    }
  }

  for (i = 0; i < reorder->nalu->len && decode_ret; i++) {
    GstH265ReorderNalUnit *decoder_nalu =
        &g_array_index (reorder->nalu, GstH265ReorderNalUnit, i);
    decode_ret = gst_h265_reorder_decode_nalu (reorder, decoder_nalu);
  }

  gst_buffer_unmap (in_buf, &map);
  gst_h265_reorder_reset_frame_state (reorder);

  if (!decode_ret) {
    GST_ERROR_OBJECT (reorder, "Couldn't decode frame");
    gst_clear_h265_picture (&reorder->current_picture);
    reorder->current_frame = NULL;

    g_ptr_array_remove (reorder->frame_queue, frame);
    reorder->system_num--;

    return FALSE;
  }

  if (!reorder->current_picture) {
    GST_DEBUG_OBJECT (reorder,
        "AU buffer without slice data, current frame %u",
        frame->system_frame_number);

    g_ptr_array_remove (reorder->frame_queue, frame);
    reorder->current_frame = NULL;
    reorder->system_num--;

    return FALSE;
  }

  gst_h265_reorder_finish_picture (reorder, reorder->current_picture);
  reorder->current_picture = NULL;
  reorder->current_frame = NULL;

  *latency = reorder->latency;

  return TRUE;
}

GstVideoCodecFrame *
gst_h265_reorder_pop (GstH265Reorder * reorder)
{
  if (!reorder->output_queue->len) {
    GST_LOG_OBJECT (reorder, "Empty output queue, frames queue size %u",
        reorder->frame_queue->len);
    return NULL;
  }

  return g_ptr_array_steal_index (reorder->output_queue, 0);
}

guint
gst_h265_reorder_get_num_buffered (GstH265Reorder * reorder)
{
  return reorder->frame_queue->len + reorder->output_queue->len;
}

GstBuffer *
gst_h265_reorder_insert_sei (GstH265Reorder * reorder, GstBuffer * au,
    GArray * sei)
{
  GstMemory *mem;
  GstBuffer *new_buf;

  if (reorder->is_hevc)
    mem = gst_h265_create_sei_memory_hevc (0, 1, reorder->nal_length_size, sei);
  else
    mem = gst_h265_create_sei_memory (0, 1, 4, sei);

  if (!mem) {
    GST_ERROR_OBJECT (reorder, "Couldn't create SEI memory");
    return NULL;
  }

  if (reorder->is_hevc) {
    new_buf = gst_h265_parser_insert_sei_hevc (reorder->parser,
        reorder->nal_length_size, au, mem);
  } else {
    new_buf = gst_h265_parser_insert_sei (reorder->parser, au, mem);
  }

  gst_memory_unref (mem);
  return new_buf;
}
