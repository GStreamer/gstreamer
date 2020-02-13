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
        "stream-format=(string) byte-stream, alignment=(string) au")
    );

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, YUY2 }")));

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

  struct v4l2_ctrl_h264_sps sps;
  struct v4l2_ctrl_h264_pps pps;
  struct v4l2_ctrl_h264_scaling_matrix scaling_matrix;
  struct v4l2_ctrl_h264_decode_params decode_params;
  GArray *slice_params;

  GstMemory *bitstream;
  GstMapInfo bitstream_map;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstV4l2CodecH264Dec,
    gst_v4l2_codec_h264_dec, GST_TYPE_H264_DECODER,
    GST_DEBUG_CATEGORY_INIT (v4l2_h264dec_debug, "v4l2codecs-h264dec", 0,
        "V4L2 stateless h264 decoder"));
#define parent_class gst_v4l2_codec_h264_dec_parent_class

static gboolean
gst_v4l2_codec_h264_dec_open (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  if (!gst_v4l2_decoder_open (self->decoder)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Failed to open H264 decoder"),
        ("gst_v4l2_decoder_open() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_close (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  gst_v4l2_decoder_close (self->decoder);
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_stop (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  if (self->sink_allocator) {
    gst_v4l2_codec_allocator_detach (self->sink_allocator);
    g_clear_object (&self->sink_allocator);
  }

  if (self->src_allocator) {
    gst_v4l2_codec_allocator_detach (self->src_allocator);
    g_clear_object (&self->src_allocator);
    g_clear_object (&self->src_pool);
  }

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static gboolean
gst_v4l2_codec_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "Negotiate");

  if (self->sink_allocator)
    gst_v4l2_codec_allocator_detach (self->sink_allocator);

  if (self->src_allocator)
    gst_v4l2_codec_allocator_detach (self->src_allocator);

  if (!gst_v4l2_decoder_set_sink_fmt (self->decoder, V4L2_PIX_FMT_H264_SLICE,
          self->coded_width, self->coded_height)) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Failed to configure H264 decoder"),
        ("gst_v4l2_decoder_set_sink_fmt() failed: %s", g_strerror (errno)));
    gst_v4l2_decoder_close (self->decoder);
    return FALSE;
  }

  /* TODO set sequence parameter control, this is needed to negotiate a
   * format with the help of the driver */

  if (!gst_v4l2_decoder_select_src_format (self->decoder, &self->vinfo)) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Unsupported bitdepth/chroma format"),
        ("No support for %ux%u %ubit chroma IDC %i", self->coded_width,
            self->coded_height, self->bitdepth, self->chroma_format_idc));
    return FALSE;
  }

  /* TODO some decoders supports color convertion and scaling */

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  self->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      self->vinfo.finfo->format, self->display_width,
      self->display_height, h264dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
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
    GstH264Picture * picture, GstH264Dpb * dpb)
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
    self->decode_params.dpb[i] = (struct v4l2_h264_dpb_entry) {
      /*
       * The reference is multiplied by 1000 because it's wassed as micro
       * seconds and this TS is nanosecond.
       */
      .reference_ts = ref_pic->system_frame_number * 1000,
      .frame_num = ref_pic->frame_num,
      .pic_num = ref_pic->pic_num,
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

/* FIXME This is from VA-API, need to check if this is what hantro wants */
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

  /* Ensure array is large enough */
  if (self->slice_params->len < self->decode_params.num_slices)
    g_array_set_size (self->slice_params, self->slice_params->len * 2);

  /* FIXME This is the subset Hantro uses */
  /* *INDENT-OFF* */
  g_array_index (self->slice_params, struct v4l2_ctrl_h264_slice_params, n) =
      (struct v4l2_ctrl_h264_slice_params) {
    .size = slice->nalu.size + 3,       /* FIXME HW may not want a start code */
    .header_bit_size = get_slice_header_bit_size (slice),
    .first_mb_in_slice = slice->header.first_mb_in_slice,
    .slice_type = slice->header.type,
    .pic_parameter_set_id = slice->header.pps->id,
    .frame_num = slice->header.frame_num,
    .dec_ref_pic_marking_bit_size = slice->header.dec_ref_pic_marking.bit_size,
    .idr_pic_id = slice->header.idr_pic_id,
    .pic_order_cnt_bit_size = slice->header.pic_order_cnt_bit_size,
  };
  /* *INDENT-ON* */
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
    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  /* Ensure we have a bitstream to write into */
  if (!self->bitstream) {
    self->bitstream = gst_v4l2_codec_allocator_alloc (self->sink_allocator);

    if (!self->bitstream) {
      GST_ELEMENT_ERROR (decoder, RESOURCE, NO_SPACE_LEFT,
          ("Not enough memory to decode H264 stream."), (NULL));
      return FALSE;
    }

    if (!gst_memory_map (self->bitstream, &self->bitstream_map, GST_MAP_WRITE)) {
      GST_ELEMENT_ERROR (decoder, RESOURCE, WRITE,
          ("Could not access bitstream memory for writing"), (NULL));
      g_clear_pointer (&self->bitstream, gst_memory_unref);
      return FALSE;
    }
  }

  /* We use this field to track how much we have written */
  self->bitstream_map.size = 0;

  gst_v4l2_codec_h264_dec_fill_pps (self, slice->header.pps);
  gst_v4l2_codec_h264_dec_fill_scaling_matrix (self, slice->header.pps);
  gst_v4l2_codec_h264_dec_fill_decoder_params (self, picture, dpb);
  /* FIXME Move to decode slice */
  gst_v4l2_codec_h264_dec_fill_slice_params (self, slice);

  return TRUE;
}

static GstFlowReturn
gst_v4l2_codec_h264_dec_output_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  return GST_FLOW_OK;
}

static gboolean
gst_v4l2_codec_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  gsize nal_size = 3 + slice->nalu.size;
  guint8 *bitstream_data = self->bitstream_map.data + self->bitstream_map.size;

  if (self->bitstream_map.size + nal_size > self->bitstream_map.maxsize) {
    GST_ELEMENT_ERROR (decoder, RESOURCE, NO_SPACE_LEFT,
        ("Not enough space to send all slice of an H264 frame."), (NULL));
    return FALSE;
  }

  /* FIXME check if the HW needs a start code */
  bitstream_data[0] = 0x00;
  bitstream_data[1] = 0x00;
  bitstream_data[2] = 0x01;
  memcpy (bitstream_data + 3, slice->nalu.data + slice->nalu.offset,
      slice->nalu.size);

  self->bitstream_map.size += nal_size;
  return TRUE;
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

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_stop);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_decide_allocation);

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
  GTypeQuery type_query;
  GTypeInfo type_info = { 0, };
  GType subtype;
  gchar *type_name;

  g_type_query (GST_TYPE_V4L2_CODEC_H264_DEC, &type_query);
  memset (&type_info, 0, sizeof (type_info));
  type_info.class_size = type_query.class_size;
  type_info.instance_size = type_query.instance_size;
  type_info.class_init = (GClassInitFunc) gst_v4l2_codec_h264_dec_subclass_init;
  type_info.class_data = gst_mini_object_ref (GST_MINI_OBJECT (device));
  type_info.instance_init = (GInstanceInitFunc) gst_v4l2_codec_h264_dec_subinit;
  GST_MINI_OBJECT_FLAG_SET (device, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  /* The first decoder to be registered should use a constant name, like
   * v4l2slh264dec, for any additional decoders, we create unique names. Decoder
   * names may change between boots, so this should help gain stable names for
   * the most common use cases. SL stands for state-less, we differentiate
   * with v4l2h264dec as this element may not have the same properties */
  type_name = g_strdup ("v4l2slh264dec");

  if (g_type_from_name (type_name) != 0) {
    gchar *basename = g_path_get_basename (device->video_device_path);
    g_free (type_name);
    type_name = g_strdup_printf ("v4l2sl%sh264dec", basename);
    g_free (basename);
  }

  subtype = g_type_register_static (GST_TYPE_V4L2_CODEC_H264_DEC, type_name,
      &type_info, 0);

  if (!gst_element_register (plugin, type_name, rank, subtype))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
}
