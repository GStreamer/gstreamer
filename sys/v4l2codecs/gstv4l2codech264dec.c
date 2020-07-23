/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
#include <config.h>
#endif

#include "gstv4l2codecallocator.h"
#include "gstv4l2codech264dec.h"
#include "gstv4l2codecpool.h"
#include "linux/h264-ctrls.h"

GST_DEBUG_CATEGORY_STATIC (v4l2_h264dec_debug);
#define GST_CAT_DEFAULT v4l2_h264dec_debug

enum
{
  PROP_0,
  PROP_LAST = PROP_0
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string) { avc, avc3, byte-stream }, "
        "alignment=(string) au")
    );

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, YUY2, NV12_32L32 }")));

struct _GstV4l2CodecH264Dec
{
  GstH264Decoder parent;
  GstV4l2Decoder *decoder;
  GstVideoCodecState *output_state;
  GstVideoInfo vinfo;
  gint display_width;
  gint display_height;
  gint coded_width;
  gint coded_height;
  guint bitdepth;
  guint chroma_format_idc;

  GstV4l2CodecAllocator *sink_allocator;
  GstV4l2CodecAllocator *src_allocator;
  GstV4l2CodecPool *src_pool;
  gint min_pool_size;
  gboolean has_videometa;
  gboolean need_negotiation;
  gboolean copy_frames;

  struct v4l2_ctrl_h264_sps sps;
  struct v4l2_ctrl_h264_pps pps;
  struct v4l2_ctrl_h264_scaling_matrix scaling_matrix;
  struct v4l2_ctrl_h264_decode_params decode_params;
  GArray *slice_params;

  enum v4l2_mpeg_video_h264_decode_mode decode_mode;
  enum v4l2_mpeg_video_h264_start_code start_code;

  GstMemory *bitstream;
  GstMapInfo bitstream_map;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstV4l2CodecH264Dec,
    gst_v4l2_codec_h264_dec, GST_TYPE_H264_DECODER,
    GST_DEBUG_CATEGORY_INIT (v4l2_h264dec_debug, "v4l2codecs-h264dec", 0,
        "V4L2 stateless h264 decoder"));
#define parent_class gst_v4l2_codec_h264_dec_parent_class

static gboolean
is_frame_based (GstV4l2CodecH264Dec * self)
{
  return self->decode_mode == V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED;
}

static gboolean
is_slice_based (GstV4l2CodecH264Dec * self)
{
  return self->decode_mode == V4L2_MPEG_VIDEO_H264_DECODE_MODE_SLICE_BASED;
}

static gboolean
needs_start_codes (GstV4l2CodecH264Dec * self)
{
  return self->start_code == V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B;
}


static gboolean
gst_v4l2_codec_h264_dec_open (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  /* *INDENT-OFF* */
  struct v4l2_ext_control control[] = {
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_DECODE_MODE,
    },
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_START_CODE,
    },
  };
  /* *INDENT-ON* */

  if (!gst_v4l2_decoder_open (self->decoder)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Failed to open H264 decoder"),
        ("gst_v4l2_decoder_open() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  if (!gst_v4l2_decoder_get_controls (self->decoder, control,
          G_N_ELEMENTS (control))) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Driver did not report framing and start code method."),
        ("gst_v4l2_decoder_get_controls() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  self->decode_mode = control[0].value;
  self->start_code = control[1].value;

  GST_INFO_OBJECT (self, "Opened H264 %s decoder %s",
      is_frame_based (self) ? "frame based" : "slice based",
      needs_start_codes (self) ? "using start-codes" : "without start-codes");
  gst_h264_decoder_set_process_ref_pic_lists (GST_H264_DECODER (self),
      is_slice_based (self));

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_close (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  gst_v4l2_decoder_close (self->decoder);
  return TRUE;
}

static void
gst_v4l2_codec_h264_dec_reset_allocation (GstV4l2CodecH264Dec * self)
{
  if (self->sink_allocator) {
    gst_v4l2_codec_allocator_detach (self->sink_allocator);
    g_clear_object (&self->sink_allocator);
  }

  if (self->src_allocator) {
    gst_v4l2_codec_allocator_detach (self->src_allocator);
    g_clear_object (&self->src_allocator);
    g_clear_object (&self->src_pool);
  }
}

static gboolean
gst_v4l2_codec_h264_dec_stop (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  gst_v4l2_decoder_streamoff (self->decoder, GST_PAD_SINK);
  gst_v4l2_decoder_streamoff (self->decoder, GST_PAD_SRC);

  gst_v4l2_codec_h264_dec_reset_allocation (self);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static gint
get_pixel_bitdepth (GstV4l2CodecH264Dec * self)
{
  gint depth;

  switch (self->chroma_format_idc) {
    case 0:
      /* 4:0:0 */
      depth = self->bitdepth;
      break;
    case 1:
      /* 4:2:0 */
      depth = self->bitdepth + self->bitdepth / 2;
      break;
    case 2:
      /* 4:2:2 */
      depth = 2 * self->bitdepth;
      break;
    case 3:
      /* 4:4:4 */
      depth = 3 * self->bitdepth;
      break;
    default:
      GST_WARNING_OBJECT (self, "Unsupported chroma format %i",
          self->chroma_format_idc);
      depth = 0;
      break;
  }

  return depth;
}

static gboolean
gst_v4l2_codec_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);
  /* *INDENT-OFF* */
  struct v4l2_ext_control control[] = {
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_SPS,
      .ptr = &self->sps,
      .size = sizeof (self->sps),
    },
  };
  /* *INDENT-ON* */
  GstCaps *filter, *caps;

  /* Ignore downstream renegotiation request. */
  if (!self->need_negotiation)
    return TRUE;
  self->need_negotiation = FALSE;

  GST_DEBUG_OBJECT (self, "Negotiate");

  gst_v4l2_codec_h264_dec_reset_allocation (self);

  gst_v4l2_decoder_streamoff (self->decoder, GST_PAD_SINK);
  gst_v4l2_decoder_streamoff (self->decoder, GST_PAD_SRC);

  if (!gst_v4l2_decoder_set_sink_fmt (self->decoder, V4L2_PIX_FMT_H264_SLICE,
          self->coded_width, self->coded_height, get_pixel_bitdepth (self))) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Failed to configure H264 decoder"),
        ("gst_v4l2_decoder_set_sink_fmt() failed: %s", g_strerror (errno)));
    gst_v4l2_decoder_close (self->decoder);
    return FALSE;
  }

  if (!gst_v4l2_decoder_set_controls (self->decoder, NULL, control,
          G_N_ELEMENTS (control))) {
    GST_ELEMENT_ERROR (decoder, RESOURCE, WRITE,
        ("Driver does not support the selected stream."), (NULL));
    return FALSE;
  }

  filter = gst_v4l2_decoder_enum_src_formats (self->decoder);
  if (!filter) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("No supported decoder output formats"), (NULL));
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "Supported output formats: %" GST_PTR_FORMAT, filter);

  caps = gst_pad_peer_query_caps (decoder->srcpad, filter);
  gst_caps_unref (filter);
  GST_DEBUG_OBJECT (self, "Peer supported formats: %" GST_PTR_FORMAT, caps);

  if (!gst_v4l2_decoder_select_src_format (self->decoder, caps, &self->vinfo)) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Unsupported bitdepth/chroma format"),
        ("No support for %ux%u %ubit chroma IDC %i", self->coded_width,
            self->coded_height, self->bitdepth, self->chroma_format_idc));
    gst_caps_unref (caps);
    return FALSE;
  }
  gst_caps_unref (caps);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  self->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      self->vinfo.finfo->format, self->display_width,
      self->display_height, h264dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);

  if (GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder)) {
    if (!gst_v4l2_decoder_streamon (self->decoder, GST_PAD_SINK)) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Could not enable the decoder driver."),
          ("VIDIOC_STREAMON(SINK) failed: %s", g_strerror (errno)));
      return FALSE;
    }

    if (!gst_v4l2_decoder_streamon (self->decoder, GST_PAD_SRC)) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("Could not enable the decoder driver."),
          ("VIDIOC_STREAMON(SRC) failed: %s", g_strerror (errno)));
      return FALSE;
    }

    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_v4l2_codec_h264_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  guint min = 0;

  self->has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  g_clear_object (&self->src_pool);
  g_clear_object (&self->src_allocator);

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, NULL, NULL, &min, NULL);

  min = MAX (2, min);

  self->sink_allocator = gst_v4l2_codec_allocator_new (self->decoder,
      GST_PAD_SINK, self->min_pool_size + 2);
  self->src_allocator = gst_v4l2_codec_allocator_new (self->decoder,
      GST_PAD_SRC, self->min_pool_size + min + 4);
  self->src_pool = gst_v4l2_codec_pool_new (self->src_allocator, &self->vinfo);

  /* Our buffer pool is internal, we will let the base class create a video
   * pool, and use it if we are running out of buffers or if downstream does
   * not support GstVideoMeta */
  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static void
gst_v4l2_codec_h264_dec_fill_sequence (GstV4l2CodecH264Dec * self,
    const GstH264SPS * sps)
{
  gint i;

  /* *INDENT-OFF* */
  self->sps = (struct v4l2_ctrl_h264_sps) {
    .profile_idc = sps->profile_idc,
    .constraint_set_flags = (sps->constraint_set0_flag)
        | (sps->constraint_set1_flag << 1) | (sps->constraint_set2_flag << 2)
        | (sps->constraint_set3_flag << 3) | (sps->constraint_set4_flag << 4)
        | (sps->constraint_set5_flag << 5),
    .level_idc = sps->level_idc,
    .seq_parameter_set_id = sps->id,
    .chroma_format_idc = sps->chroma_format_idc,
    .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
    .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
    .log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4,
    .pic_order_cnt_type = sps->pic_order_cnt_type,
    .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
    .max_num_ref_frames = sps->num_ref_frames,
    .num_ref_frames_in_pic_order_cnt_cycle = sps->num_ref_frames_in_pic_order_cnt_cycle,
    .offset_for_non_ref_pic = sps->offset_for_non_ref_pic,
    .offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field,
    .pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1,
    .pic_height_in_map_units_minus1 = sps->pic_height_in_map_units_minus1,
    .flags = (sps->separate_colour_plane_flag ? V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE : 0)
        | (sps->qpprime_y_zero_transform_bypass_flag ? V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS : 0)
        | (sps->delta_pic_order_always_zero_flag ? V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO : 0)
        | (sps->gaps_in_frame_num_value_allowed_flag ? V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED : 0)
        | (sps->frame_mbs_only_flag ? V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY : 0)
        | (sps->mb_adaptive_frame_field_flag ? V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD : 0)
        | (sps->direct_8x8_inference_flag ? V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE : 0),
  };
  /* *INDENT-ON* */

  for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
    self->sps.offset_for_ref_frame[i] = sps->offset_for_ref_frame[i];
}

static void
gst_v4l2_codec_h264_dec_fill_pps (GstV4l2CodecH264Dec * self, GstH264PPS * pps)
{
  /* *INDENT-OFF* */
  self->pps = (struct v4l2_ctrl_h264_pps) {
    .pic_parameter_set_id = pps->id,
    .seq_parameter_set_id = pps->sequence->id,
    .num_slice_groups_minus1 = pps->num_slice_groups_minus1,
    .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_active_minus1,
    .weighted_bipred_idc = pps->weighted_bipred_idc,
    .pic_init_qp_minus26 = pps->pic_init_qp_minus26,
    .pic_init_qs_minus26 = pps->pic_init_qs_minus26,
    .chroma_qp_index_offset = pps->chroma_qp_index_offset,
    .second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset,
    .flags = 0
        | (pps->entropy_coding_mode_flag ? V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE : 0)
        | (pps->pic_order_present_flag ? V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT : 0)
        | (pps->weighted_pred_flag ? V4L2_H264_PPS_FLAG_WEIGHTED_PRED : 0)
        | (pps->deblocking_filter_control_present_flag ? V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT : 0)
        | (pps->constrained_intra_pred_flag ? V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED : 0)
        | (pps->redundant_pic_cnt_present_flag ? V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT : 0)
        | (pps->transform_8x8_mode_flag ? V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE : 0)
        | (pps->pic_scaling_matrix_present_flag ? V4L2_H264_PPS_FLAG_PIC_SCALING_MATRIX_PRESENT : 0),
  };
  /* *INDENT-ON* */
}

static void
gst_v4l2_codec_h264_dec_fill_scaling_matrix (GstV4l2CodecH264Dec * self,
    GstH264PPS * pps)
{
  gint i, n;

  for (i = 0; i < G_N_ELEMENTS (pps->scaling_lists_4x4); i++)
    gst_h264_quant_matrix_4x4_get_raster_from_zigzag (self->
        scaling_matrix.scaling_list_4x4[i], pps->scaling_lists_4x4[i]);

  /* Avoid uninitialize data passed into ioctl() */
  memset (self->scaling_matrix.scaling_list_8x8, 0,
      sizeof (self->scaling_matrix.scaling_list_8x8));

  /* We need the first 2 entries (Y intra and Y inter for YCbCr 4:2:2 and
   * less, and the full 6 entries for 4:4:4, see Table 7-2 of the spec for
   * more details */
  n = (pps->sequence->chroma_format_idc == 3) ? 6 : 2;
  for (i = 0; i < n; i++)
    gst_h264_quant_matrix_8x8_get_raster_from_zigzag (self->
        scaling_matrix.scaling_list_8x8[i], pps->scaling_lists_8x8[i]);
}

static void
gst_v4l2_codec_h264_dec_fill_decoder_params (GstV4l2CodecH264Dec * self,
    GstH264SliceHdr * slice_hdr, GstH264Picture * picture, GstH264Dpb * dpb)
{
  GArray *refs = gst_h264_dpb_get_pictures_all (dpb);
  gint i;

  /* *INDENT-OFF* */
  self->decode_params = (struct v4l2_ctrl_h264_decode_params) {
    .num_slices = 0,            /* will be incremented as we receive slices */
    .nal_ref_idc = picture->nal_ref_idc,
    .top_field_order_cnt = picture->top_field_order_cnt,
    .bottom_field_order_cnt = picture->bottom_field_order_cnt,
    .flags = picture->idr ? V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC : 0,
  };

  for (i = 0; i < refs->len; i++) {
    GstH264Picture *ref_pic = g_array_index (refs, GstH264Picture *, i);
    gint pic_num = ref_pic->pic_num;

    /* Unwrap pic_num */
    if (pic_num < 0)
      pic_num += slice_hdr->max_pic_num;

    self->decode_params.dpb[i] = (struct v4l2_h264_dpb_entry) {
      /*
       * The reference is multiplied by 1000 because it's wassed as micro
       * seconds and this TS is nanosecond.
       */
      .reference_ts = (guint64) ref_pic->system_frame_number * 1000,
      .frame_num = ref_pic->frame_num,
      .pic_num = pic_num,
      .top_field_order_cnt = ref_pic->pic_order_cnt,
      .bottom_field_order_cnt = ref_pic->bottom_field_order_cnt,
      .flags = V4L2_H264_DPB_ENTRY_FLAG_VALID
          | (ref_pic->ref ? V4L2_H264_DPB_ENTRY_FLAG_ACTIVE : 0)
          | (ref_pic->long_term ? V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM : 0),
    };
  }
  /* *INDENT-ON* */

  g_array_unref (refs);
}

static guint
get_slice_header_bit_size (GstH264Slice * slice)
{
  return 8 * slice->nalu.header_bytes
      + slice->header.header_size - slice->header.n_emulation_prevention_bytes;
}

static void
gst_v4l2_codec_h264_dec_fill_slice_params (GstV4l2CodecH264Dec * self,
    GstH264Slice * slice)
{
  gint n = self->decode_params.num_slices++;
  gsize slice_size = slice->nalu.size;
  struct v4l2_ctrl_h264_slice_params *params;
  gint i, j;

  /* Ensure array is large enough */
  if (self->slice_params->len < self->decode_params.num_slices)
    g_array_set_size (self->slice_params, self->slice_params->len * 2);

  if (needs_start_codes (self))
    slice_size += 3;

  /* *INDENT-OFF* */
  params = &g_array_index (self->slice_params, struct v4l2_ctrl_h264_slice_params, n);
  *params = (struct v4l2_ctrl_h264_slice_params) {
    .size = slice_size,
    .start_byte_offset = self->bitstream_map.size,
    .header_bit_size = get_slice_header_bit_size (slice),
    .first_mb_in_slice = slice->header.first_mb_in_slice,
    .slice_type = slice->header.type % 5,
    .pic_parameter_set_id = slice->header.pps->id,
    .colour_plane_id = slice->header.colour_plane_id,
    .redundant_pic_cnt = slice->header.redundant_pic_cnt,
    .frame_num = slice->header.frame_num,
    .idr_pic_id = slice->header.idr_pic_id,
    .pic_order_cnt_lsb = slice->header.pic_order_cnt_lsb,
    .delta_pic_order_cnt_bottom = slice->header.delta_pic_order_cnt_bottom,
    .delta_pic_order_cnt0 = slice->header.delta_pic_order_cnt[0],
    .delta_pic_order_cnt1 = slice->header.delta_pic_order_cnt[1],
    .pred_weight_table = (struct v4l2_h264_pred_weight_table) {
      .luma_log2_weight_denom = slice->header.pred_weight_table.luma_log2_weight_denom,
      .chroma_log2_weight_denom = slice->header.pred_weight_table.chroma_log2_weight_denom,
    },
    .dec_ref_pic_marking_bit_size = slice->header.dec_ref_pic_marking.bit_size,
    .pic_order_cnt_bit_size = slice->header.pic_order_cnt_bit_size,
    .cabac_init_idc = slice->header.cabac_init_idc,
    .slice_qp_delta = slice->header.slice_qp_delta,
    .slice_qs_delta = slice->header.slice_qs_delta,
    .disable_deblocking_filter_idc = slice->header.disable_deblocking_filter_idc,
    .slice_alpha_c0_offset_div2 = slice->header.slice_alpha_c0_offset_div2,
    .slice_beta_offset_div2 = slice->header.slice_beta_offset_div2,
    .num_ref_idx_l0_active_minus1 = slice->header.num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_active_minus1 = slice->header.num_ref_idx_l1_active_minus1,
    .slice_group_change_cycle = slice->header.slice_group_change_cycle,

    .flags = (slice->header.field_pic_flag ? V4L2_H264_SLICE_FLAG_FIELD_PIC : 0) |
             (slice->header.bottom_field_flag ? V4L2_H264_SLICE_FLAG_BOTTOM_FIELD : 0) |
             (slice->header.direct_spatial_mv_pred_flag ? V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED : 0) |
             (slice->header.sp_for_switch_flag ? V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH : 0),
  };
  /* *INDENT-ON* */

  for (i = 0; i <= slice->header.num_ref_idx_l0_active_minus1; i++) {
    params->pred_weight_table.weight_factors[0].luma_weight[i] =
        slice->header.pred_weight_table.luma_weight_l0[i];
    params->pred_weight_table.weight_factors[0].luma_offset[i] =
        slice->header.pred_weight_table.luma_offset_l0[i];
  }

  if (slice->header.pps->sequence->chroma_array_type != 0) {
    for (i = 0; i <= slice->header.num_ref_idx_l0_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        params->pred_weight_table.weight_factors[0].chroma_weight[i][j] =
            slice->header.pred_weight_table.chroma_weight_l0[i][j];
        params->pred_weight_table.weight_factors[0].chroma_offset[i][j] =
            slice->header.pred_weight_table.chroma_offset_l0[i][j];
      }
    }
  }

  /* Skip l1 if this is not a B-Frames. */
  if (slice->header.type % 5 != GST_H264_B_SLICE)
    return;

  for (i = 0; i <= slice->header.num_ref_idx_l0_active_minus1; i++) {
    params->pred_weight_table.weight_factors[0].luma_weight[i] =
        slice->header.pred_weight_table.luma_weight_l0[i];
    params->pred_weight_table.weight_factors[0].luma_offset[i] =
        slice->header.pred_weight_table.luma_offset_l0[i];
  }

  if (slice->header.pps->sequence->chroma_array_type != 0) {
    for (i = 0; i <= slice->header.num_ref_idx_l1_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        params->pred_weight_table.weight_factors[1].chroma_weight[i][j] =
            slice->header.pred_weight_table.chroma_weight_l1[i][j];
        params->pred_weight_table.weight_factors[1].chroma_offset[i][j] =
            slice->header.pred_weight_table.chroma_offset_l1[i][j];
      }
    }
  }
}

static guint8
lookup_dpb_index (struct v4l2_h264_dpb_entry dpb[16], GstH264Picture * ref_pic)
{
  guint64 ref_ts;
  gint i;

  /* Reference list may have wholes in case a ref is missing, we should mark
   * the whole and avoid moving items in the list */
  if (!ref_pic)
    return 0xff;

  ref_ts = (guint64) ref_pic->system_frame_number * 1000;
  for (i = 0; i < 16; i++) {
    if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE
        && dpb[i].reference_ts == ref_ts)
      return i;
  }

  return 0xff;
}

static void
gst_v4l2_codec_h264_dec_fill_references (GstV4l2CodecH264Dec * self,
    GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  struct v4l2_ctrl_h264_slice_params *slice_params;
  gint i;

  slice_params = &g_array_index (self->slice_params,
      struct v4l2_ctrl_h264_slice_params, 0);

  memset (slice_params->ref_pic_list0, 0xff,
      sizeof (slice_params->ref_pic_list0));
  memset (slice_params->ref_pic_list1, 0xff,
      sizeof (slice_params->ref_pic_list1));

  for (i = 0; i < ref_pic_list0->len; i++) {
    GstH264Picture *ref_pic =
        g_array_index (ref_pic_list0, GstH264Picture *, i);
    slice_params->ref_pic_list0[i] =
        lookup_dpb_index (self->decode_params.dpb, ref_pic);
  }

  for (i = 0; i < ref_pic_list1->len; i++) {
    GstH264Picture *ref_pic =
        g_array_index (ref_pic_list1, GstH264Picture *, i);
    slice_params->ref_pic_list1[i] =
        lookup_dpb_index (self->decode_params.dpb, ref_pic);
  }
}

static gboolean
gst_v4l2_codec_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  gint crop_width = sps->width;
  gint crop_height = sps->height;
  gboolean negotiation_needed = FALSE;

  if (self->vinfo.finfo->format == GST_VIDEO_FORMAT_UNKNOWN)
    negotiation_needed = TRUE;

  /* TODO check if CREATE_BUFS is supported, and simply grow the pool */
  if (self->min_pool_size < max_dpb_size) {
    self->min_pool_size = max_dpb_size;
    negotiation_needed = TRUE;
  }

  if (sps->frame_cropping_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  }

  /* TODO Check if current buffers are large enough, and reuse them */
  if (self->display_width != crop_width || self->display_height != crop_height
      || self->coded_width != sps->width || self->coded_height != sps->height) {
    self->display_width = crop_width;
    self->display_height = crop_height;
    self->coded_width = sps->width;
    self->coded_height = sps->height;
    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Resolution changed to %dx%d (%ix%i)",
        self->display_width, self->display_height,
        self->coded_width, self->coded_height);
  }

  if (self->bitdepth != sps->bit_depth_luma_minus8 + 8) {
    self->bitdepth = sps->bit_depth_luma_minus8 + 8;
    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Bitdepth changed to %u", self->bitdepth);
  }

  if (self->chroma_format_idc != sps->chroma_format_idc) {
    self->chroma_format_idc = sps->chroma_format_idc;
    negotiation_needed = TRUE;
    GST_INFO_OBJECT (self, "Chroma format changed to %i",
        self->chroma_format_idc);
  }

  gst_v4l2_codec_h264_dec_fill_sequence (self, sps);

  if (negotiation_needed) {
    self->need_negotiation = TRUE;
    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  /* Check if we can zero-copy buffers */
  if (!self->has_videometa) {
    GstVideoInfo ref_vinfo;
    gint i;

    gst_video_info_set_format (&ref_vinfo, GST_VIDEO_INFO_FORMAT (&self->vinfo),
        self->display_width, self->display_height);

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&self->vinfo); i++) {
      if (self->vinfo.stride[i] != ref_vinfo.stride[i] ||
          self->vinfo.offset[i] != ref_vinfo.offset[i]) {
        GST_WARNING_OBJECT (self,
            "GstVideoMeta support required, copying frames.");
        self->copy_frames = TRUE;
        break;
      }
    }
  } else {
    self->copy_frames = FALSE;
  }

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_ensure_bitstream (GstV4l2CodecH264Dec * self)
{
  if (self->bitstream)
    goto done;

  self->bitstream = gst_v4l2_codec_allocator_alloc (self->sink_allocator);

  if (!self->bitstream) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Not enough memory to decode H264 stream."), (NULL));
    return FALSE;
  }

  if (!gst_memory_map (self->bitstream, &self->bitstream_map, GST_MAP_WRITE)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Could not access bitstream memory for writing"), (NULL));
    g_clear_pointer (&self->bitstream, gst_memory_unref);
    return FALSE;
  }

done:
  /* We use this field to track how much we have written */
  self->bitstream_map.size = 0;

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  /* FIXME base class should not call us if negotiation failed */
  if (!self->sink_allocator)
    return FALSE;

  if (!gst_v4l2_codec_h264_dec_ensure_bitstream (self))
    return FALSE;

  gst_v4l2_codec_h264_dec_fill_pps (self, slice->header.pps);
  gst_v4l2_codec_h264_dec_fill_scaling_matrix (self, slice->header.pps);
  gst_v4l2_codec_h264_dec_fill_decoder_params (self, &slice->header, picture,
      dpb);

  if (is_frame_based (self))
    gst_v4l2_codec_h264_dec_fill_slice_params (self, slice);

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_copy_output_buffer (GstV4l2CodecH264Dec * self,
    GstVideoCodecFrame * codec_frame)
{
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstVideoInfo dest_vinfo;
  GstBuffer *buffer;

  gst_video_info_set_format (&dest_vinfo, GST_VIDEO_INFO_FORMAT (&self->vinfo),
      self->display_width, self->display_height);

  buffer = gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));
  if (!buffer)
    goto fail;

  if (!gst_video_frame_map (&src_frame, &self->vinfo,
          codec_frame->output_buffer, GST_MAP_READ))
    goto fail;

  if (!gst_video_frame_map (&dest_frame, &dest_vinfo, buffer, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  /* gst_video_frame_copy can crop this, but does not know, so let make it
   * think it's all right */
  GST_VIDEO_INFO_WIDTH (&src_frame.info) = self->display_width;
  GST_VIDEO_INFO_HEIGHT (&src_frame.info) = self->display_height;

  if (!gst_video_frame_copy (&dest_frame, &src_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);
  gst_buffer_replace (&codec_frame->output_buffer, buffer);
  gst_buffer_unref (buffer);

  return TRUE;

fail:
  GST_ERROR_OBJECT (self, "Failed copy output buffer.");
  return FALSE;
}

static gboolean
gst_v4l2_codec_h264_dec_wait (GstV4l2CodecH264Dec * self,
    GstV4l2Request * request)
{
  gint ret = gst_v4l2_request_poll (request, GST_SECOND);
  if (ret == 0) {
    GST_ELEMENT_ERROR (self, STREAM, DECODE,
        ("Decoding frame took too long"), (NULL));
    return FALSE;
  } else if (ret < 0) {
    GST_ELEMENT_ERROR (self, STREAM, DECODE,
        ("Decoding request failed: %s", g_strerror (errno)), (NULL));
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_v4l2_codec_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstV4l2Request *request = gst_h264_picture_get_user_data (picture);
  guint32 frame_num;
  GstH264Picture *other_pic;
  GstV4l2Request *other_request;

  GST_DEBUG_OBJECT (self, "Output picture %u", picture->system_frame_number);

  if (gst_v4l2_request_is_done (request))
    goto finish_frame;

  if (!gst_v4l2_codec_h264_dec_wait (self, request))
    goto error;

  while (TRUE) {
    if (!gst_v4l2_decoder_dequeue_src (self->decoder, &frame_num)) {
      GST_ELEMENT_ERROR (self, STREAM, DECODE,
          ("Decoder did not produce a frame"), (NULL));
      goto error;
    }

    if (frame_num == picture->system_frame_number)
      break;

    other_pic = gst_h264_decoder_get_picture (decoder, frame_num);
    if (other_pic) {
      other_request = gst_h264_picture_get_user_data (other_pic);
      gst_v4l2_request_set_done (other_request);
      gst_h264_picture_unref (other_pic);
    }
  }

finish_frame:
  gst_v4l2_request_set_done (request);
  g_return_val_if_fail (frame->output_buffer, GST_FLOW_ERROR);

  /* Hold on reference buffers for the rest of the picture lifetime */
  gst_h264_picture_set_user_data (picture,
      gst_buffer_ref (frame->output_buffer), (GDestroyNotify) gst_buffer_unref);

  if (self->copy_frames)
    gst_v4l2_codec_h264_dec_copy_output_buffer (self, frame);

  gst_h264_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_h264_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static void
gst_v4l2_codec_h264_dec_reset_picture (GstV4l2CodecH264Dec * self)
{
  if (self->bitstream) {
    if (self->bitstream_map.memory)
      gst_memory_unmap (self->bitstream, &self->bitstream_map);
    g_clear_pointer (&self->bitstream, gst_memory_unref);
    self->bitstream_map = (GstMapInfo) GST_MAP_INFO_INIT;
  }

  self->decode_params.num_slices = 0;
}

static gboolean
gst_v4l2_codec_h264_dec_ensure_output_buffer (GstV4l2CodecH264Dec * self,
    GstVideoCodecFrame * frame)
{
  GstBuffer *buffer;
  GstFlowReturn flow_ret;

  if (frame->output_buffer)
    return TRUE;

  flow_ret = gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (self->src_pool),
      &buffer, NULL);
  if (flow_ret != GST_FLOW_OK) {
    if (flow_ret == GST_FLOW_FLUSHING)
      GST_DEBUG_OBJECT (self, "Frame decoding aborted, we are flushing.");
    else
      GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
          ("No more picture buffer available."), (NULL));
    return FALSE;
  }

  if (!gst_v4l2_decoder_queue_src_buffer (self->decoder, buffer,
          frame->system_frame_number)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not accept the picture buffer."), (NULL));
    gst_buffer_unref (buffer);
    return FALSE;
  }

  frame->output_buffer = buffer;
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_submit_bitstream (GstV4l2CodecH264Dec * self,
    GstH264Picture * picture, guint flags)
{
  GstVideoCodecFrame *frame;
  GstV4l2Request *prev_request, *request;
  gsize bytesused;
  gboolean ret = FALSE;

  /* *INDENT-OFF* */
  struct v4l2_ext_control control[] = {
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_SPS,
      .ptr = &self->sps,
      .size = sizeof (self->sps),
    },
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_PPS,
      .ptr = &self->pps,
      .size = sizeof (self->pps),
    },
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX,
      .ptr = &self->scaling_matrix,
      .size = sizeof (self->scaling_matrix),
    },
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAMS,
      .ptr = self->slice_params->data,
      .size = g_array_get_element_size (self->slice_params)
              * self->decode_params.num_slices,
    },
    {
      .id = V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAMS,
      .ptr = &self->decode_params,
      .size = sizeof (self->decode_params),
    },
  };
  /* *INDENT-ON* */

  request = gst_v4l2_decoder_alloc_request (self->decoder);
  if (!request) {
    GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
        ("Failed to allocate a media request object."), (NULL));
    goto done;
  }


  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (self),
      picture->system_frame_number);
  g_return_val_if_fail (frame, FALSE);

  if (!gst_v4l2_codec_h264_dec_ensure_output_buffer (self, frame))
    goto done;

  gst_video_codec_frame_unref (frame);

  if (!gst_v4l2_decoder_set_controls (self->decoder, request, control,
          G_N_ELEMENTS (control))) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not accept the bitstream parameters."), (NULL));
    goto done;
  }

  bytesused = self->bitstream_map.size;
  gst_memory_unmap (self->bitstream, &self->bitstream_map);
  self->bitstream_map = (GstMapInfo) GST_MAP_INFO_INIT;

  if (!gst_v4l2_decoder_queue_sink_mem (self->decoder, request, self->bitstream,
          picture->system_frame_number, bytesused, flags)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not accept the bitstream data."), (NULL));
    goto done;
  }

  if (!gst_v4l2_request_queue (request)) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Driver did not accept the decode request."), (NULL));
    goto done;
  }

  prev_request = gst_h264_picture_get_user_data (picture);
  if (prev_request) {
    if (!gst_v4l2_codec_h264_dec_wait (self, prev_request))
      goto done;
    gst_v4l2_request_set_done (prev_request);
  }

  gst_h264_picture_set_user_data (picture, g_steal_pointer (&request),
      (GDestroyNotify) gst_v4l2_request_free);
  ret = TRUE;

done:
  if (request)
    gst_v4l2_request_free (request);
  gst_v4l2_codec_h264_dec_reset_picture (self);

  return ret;
}

static gboolean
gst_v4l2_codec_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  gsize sc_off = 0;
  gsize nal_size;
  guint8 *bitstream_data;

  if (is_slice_based (self)) {
    if (self->bitstream_map.size) {
      /* In slice mode, we submit the pending slice asking the accelerator to
       * hold on the picture */
      if (!gst_v4l2_codec_h264_dec_submit_bitstream (self, picture,
              V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF)
          || !gst_v4l2_codec_h264_dec_ensure_bitstream (self))
        return FALSE;
    }

    gst_v4l2_codec_h264_dec_fill_slice_params (self, slice);
    gst_v4l2_codec_h264_dec_fill_references (self, ref_pic_list0,
        ref_pic_list1);
  }

  bitstream_data = self->bitstream_map.data + self->bitstream_map.size;

  if (needs_start_codes (self))
    sc_off = 3;
  nal_size = sc_off + slice->nalu.size;

  if (self->bitstream_map.size + nal_size > self->bitstream_map.maxsize) {
    GST_ELEMENT_ERROR (decoder, RESOURCE, NO_SPACE_LEFT,
        ("Not enough space to send all slice of an H264 frame."), (NULL));
    return FALSE;
  }

  if (needs_start_codes (self)) {
    bitstream_data[0] = 0x00;
    bitstream_data[1] = 0x00;
    bitstream_data[2] = 0x01;
  }

  memcpy (bitstream_data + sc_off, slice->nalu.data + slice->nalu.offset,
      slice->nalu.size);
  self->bitstream_map.size += nal_size;

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  return gst_v4l2_codec_h264_dec_submit_bitstream (self, picture, 0);
}

static void
gst_v4l2_codec_h264_dec_set_flushing (GstV4l2CodecH264Dec * self,
    gboolean flushing)
{
  if (self->sink_allocator)
    gst_v4l2_codec_allocator_set_flushing (self->sink_allocator, flushing);
  if (self->src_allocator)
    gst_v4l2_codec_allocator_set_flushing (self->src_allocator, flushing);
}

static gboolean
gst_v4l2_codec_h264_dec_flush (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Flushing decoder state.");

  gst_v4l2_decoder_flush (self->decoder);
  gst_v4l2_codec_h264_dec_set_flushing (self, FALSE);

  return GST_VIDEO_DECODER_CLASS (parent_class)->flush (decoder);
}

static gboolean
gst_v4l2_codec_h264_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (self, "flush start");
      gst_v4l2_codec_h264_dec_set_flushing (self, TRUE);
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static GstStateChangeReturn
gst_v4l2_codec_h264_dec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (element);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    gst_v4l2_codec_h264_dec_set_flushing (self, TRUE);

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_v4l2_codec_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);
  GObject *dec = G_OBJECT (self->decoder);

  switch (prop_id) {
    default:
      gst_v4l2_decoder_set_property (dec, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static void
gst_v4l2_codec_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);
  GObject *dec = G_OBJECT (self->decoder);

  switch (prop_id) {
    default:
      gst_v4l2_decoder_get_property (dec, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static void
gst_v4l2_codec_h264_dec_init (GstV4l2CodecH264Dec * self)
{
}

static void
gst_v4l2_codec_h264_dec_subinit (GstV4l2CodecH264Dec * self,
    GstV4l2CodecH264DecClass * klass)
{
  self->decoder = gst_v4l2_decoder_new (klass->device);
  gst_video_info_init (&self->vinfo);
  self->slice_params = g_array_sized_new (FALSE, TRUE,
      sizeof (struct v4l2_ctrl_h264_slice_params), 4);
}

static void
gst_v4l2_codec_h264_dec_dispose (GObject * object)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);

  g_clear_object (&self->decoder);
  g_clear_pointer (&self->slice_params, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_codec_h264_dec_class_init (GstV4l2CodecH264DecClass * klass)
{
}

static void
gst_v4l2_codec_h264_dec_subclass_init (GstV4l2CodecH264DecClass * klass,
    GstV4l2CodecDevice * device)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);

  gobject_class->set_property = gst_v4l2_codec_h264_dec_set_property;
  gobject_class->get_property = gst_v4l2_codec_h264_dec_get_property;
  gobject_class->dispose = gst_v4l2_codec_h264_dec_dispose;

  gst_element_class_set_static_metadata (element_class,
      "V4L2 Stateless H.264 Video Decoder",
      "Codec/Decoder/Video/Hardware",
      "A V4L2 based H.264 video decoder",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_change_state);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_stop);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_decide_allocation);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_flush);
  decoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_sink_event);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_new_sequence);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_output_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_start_picture);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_decode_slice);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_end_picture);

  klass->device = device;
  gst_v4l2_decoder_install_properties (gobject_class, PROP_LAST, device);
}

void
gst_v4l2_codec_h264_dec_register (GstPlugin * plugin,
    GstV4l2CodecDevice * device, guint rank)
{
  gst_v4l2_decoder_register (plugin, GST_TYPE_V4L2_CODEC_H264_DEC,
      (GClassInitFunc) gst_v4l2_codec_h264_dec_subclass_init,
      (GInstanceInitFunc) gst_v4l2_codec_h264_dec_subinit,
      "v4l2sl%sh264dec", device, rank);
}
