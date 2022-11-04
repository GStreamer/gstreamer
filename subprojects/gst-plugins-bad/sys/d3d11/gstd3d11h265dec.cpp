/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
 * SECTION:element-d3d11h265dec
 * @title: d3d11h265dec
 *
 * A Direct3D11/DXVA based H.265 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/hevc/file ! parsebin ! d3d11h265dec ! d3d11videosink
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11h265dec.h"

#include <gst/codecs/gsth265decoder.h>
#include <string.h>
#include <vector>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_h265_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_h265_dec_debug

/* *INDENT-OFF* */
typedef struct _GstD3D11H265DecInner
{
  GstD3D11Device *device = nullptr;
  GstD3D11Decoder *d3d11_decoder = nullptr;

  DXVA_PicParams_HEVC pic_params;
  DXVA_Qmatrix_HEVC iq_matrix;

  std::vector<DXVA_Slice_HEVC_Short> slice_list;
  std::vector<guint8> bitstream_buffer;

  gboolean submit_iq_data;

  gint crop_x = 0;
  gint crop_y = 0;
  gint width = 0;
  gint height = 0;
  gint coded_width = 0;
  gint coded_height = 0;
  guint bitdepth = 0;
  guint8 chroma_format_idc = 0;
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
} GstD3D11H265DecInner;
/* *INDENT-ON* */

typedef struct _GstD3D11H265Dec
{
  GstH265Decoder parent;
  GstD3D11H265DecInner *inner;
} GstD3D11H265Dec;

typedef struct _GstD3D11H265DecClass
{
  GstH265DecoderClass parent_class;
  GstD3D11DecoderSubClassData class_data;
} GstD3D11H265DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_H265_DEC(object) ((GstD3D11H265Dec *) (object))
#define GST_D3D11_H265_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11H265DecClass))

static void gst_d3d11_h265_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_h265_dec_finalize (GObject * object);
static void gst_d3d11_h265_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_h265_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_h265_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_h265_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstH265Decoder */
static GstFlowReturn gst_d3d11_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size);
static GstFlowReturn gst_d3d11_h265_dec_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * cframe, GstH265Picture * picture);
static GstFlowReturn gst_d3d11_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb);
static GstFlowReturn gst_d3d11_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice,
    GArray * ref_pic_list0, GArray * ref_pic_list1);
static GstFlowReturn gst_d3d11_h265_dec_end_picture (GstH265Decoder * decoder,
    GstH265Picture * picture);
static GstFlowReturn gst_d3d11_h265_dec_output_picture (GstH265Decoder *
    decoder, GstVideoCodecFrame * frame, GstH265Picture * picture);

static void
gst_d3d11_h265_dec_class_init (GstD3D11H265DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH265DecoderClass *h265decoder_class = GST_H265_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;

  gobject_class->get_property = gst_d3d11_h265_dec_get_property;
  gobject_class->finalize = gst_d3d11_h265_dec_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gst_d3d11_decoder_class_data_fill_subclass_data (cdata, &klass->class_data);

  /**
   * GstD3D11H265Dec:adapter-luid:
   *
   * DXGI Adapter LUID for this element
   *
   * Since: 1.20
   */
  gst_d3d11_decoder_proxy_class_init (element_class, cdata,
      "Seungha Yang <seungha.yang@navercorp.com>");

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_src_query);
  decoder_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_sink_event);

  h265decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_new_sequence);
  h265decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_new_picture);
  h265decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_start_picture);
  h265decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_decode_slice);
  h265decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_end_picture);
  h265decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h265_dec_output_picture);
}

static void
gst_d3d11_h265_dec_init (GstD3D11H265Dec * self)
{
  self->inner = new GstD3D11H265DecInner ();
}

static void
gst_d3d11_h265_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (object);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_decoder_proxy_get_property (object, prop_id, value, pspec, cdata);
}

static void
gst_d3d11_h265_dec_finalize (GObject * object)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (object);

  delete self->inner;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_h265_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (element);
  GstD3D11H265DecInner *inner = self->inner;
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, cdata->adapter_luid, &inner->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_h265_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  GstD3D11H265DecClass *klass = GST_D3D11_H265_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  if (!gst_d3d11_decoder_proxy_open (decoder,
          cdata, &inner->device, &inner->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;

  gst_clear_object (&inner->d3d11_decoder);
  gst_clear_object (&inner->device);

  return TRUE;
}

static gboolean
gst_d3d11_h265_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_negotiate (inner->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_h265_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_decide_allocation (inner->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_h265_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, inner->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_h265_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (inner->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (inner->d3d11_decoder, decoder, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (inner->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (inner->d3d11_decoder, decoder, FALSE);
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static GstFlowReturn
gst_d3d11_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  gint crop_width, crop_height;
  gboolean modified = FALSE;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  GST_LOG_OBJECT (self, "new sequence");

  if (sps->conformance_window_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  } else {
    crop_width = sps->width;
    crop_height = sps->height;
  }

  if (inner->width != crop_width || inner->height != crop_height ||
      inner->coded_width != sps->width || inner->coded_height != sps->height ||
      inner->crop_x != sps->crop_rect_x || inner->crop_y != sps->crop_rect_y) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d -> %dx%d",
        crop_width, crop_height, sps->width, sps->height);
    inner->crop_x = sps->crop_rect_x;
    inner->crop_y = sps->crop_rect_y;
    inner->width = crop_width;
    inner->height = crop_height;
    inner->coded_width = sps->width;
    inner->coded_height = sps->height;
    modified = TRUE;
  }

  if (inner->bitdepth != (guint) sps->bit_depth_luma_minus8 + 8) {
    GST_INFO_OBJECT (self, "bitdepth changed");
    inner->bitdepth = sps->bit_depth_luma_minus8 + 8;
    modified = TRUE;
  }

  if (sps->vui_parameters_present_flag && sps->vui_params.field_seq_flag) {
    interlace_mode = GST_VIDEO_INTERLACE_MODE_ALTERNATE;
  } else {
    /* 7.4.4 Profile, tier and level sementics */
    if (sps->profile_tier_level.progressive_source_flag &&
        !sps->profile_tier_level.interlaced_source_flag) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    } else {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
    }
  }

  if (inner->interlace_mode != interlace_mode) {
    GST_INFO_OBJECT (self, "Interlace mode change %d -> %d",
        inner->interlace_mode, interlace_mode);
    inner->interlace_mode = interlace_mode;
    modified = TRUE;
  }

  if (inner->chroma_format_idc != sps->chroma_format_idc) {
    GST_INFO_OBJECT (self, "chroma format changed");
    inner->chroma_format_idc = sps->chroma_format_idc;
    modified = TRUE;
  }

  if (modified || !gst_d3d11_decoder_is_configured (inner->d3d11_decoder)) {
    GstVideoInfo info;

    inner->out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (inner->bitdepth == 8) {
      if (inner->chroma_format_idc == 1) {
        inner->out_format = GST_VIDEO_FORMAT_NV12;
      } else {
        GST_FIXME_OBJECT (self, "Could not support 8bits non-4:2:0 format");
      }
    } else if (inner->bitdepth == 10) {
      if (inner->chroma_format_idc == 1) {
        inner->out_format = GST_VIDEO_FORMAT_P010_10LE;
      } else {
        GST_FIXME_OBJECT (self, "Could not support 10bits non-4:2:0 format");
      }
    }

    if (inner->out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_video_info_set_format (&info,
        inner->out_format, inner->width, inner->height);
    GST_VIDEO_INFO_INTERLACE_MODE (&info) = inner->interlace_mode;

    if (!gst_d3d11_decoder_configure (inner->d3d11_decoder,
            decoder->input_state, &info, inner->crop_x, inner->crop_y,
            inner->coded_width, inner->coded_height, max_dpb_size)) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_WARNING_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_h265_dec_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * cframe, GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (inner->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return GST_FLOW_FLUSHING;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_h265_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New h265picture %p", picture);

  return GST_FLOW_OK;
}

static void
gst_d3d11_h265_dec_picture_params_from_sps (GstD3D11H265Dec * self,
    const GstH265SPS * sps, DXVA_PicParams_HEVC * params)
{
#define COPY_FIELD(f) \
  (params)->f = (sps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(sps_,f) = (sps)->f

  params->PicWidthInMinCbsY =
      sps->width >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
  params->PicHeightInMinCbsY =
      sps->height >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
  params->sps_max_dec_pic_buffering_minus1 =
      sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1];

  COPY_FIELD (chroma_format_idc);
  COPY_FIELD (separate_colour_plane_flag);
  COPY_FIELD (bit_depth_luma_minus8);
  COPY_FIELD (bit_depth_chroma_minus8);
  COPY_FIELD (log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (log2_min_luma_coding_block_size_minus3);
  COPY_FIELD (log2_diff_max_min_luma_coding_block_size);
  COPY_FIELD (log2_min_transform_block_size_minus2);
  COPY_FIELD (log2_diff_max_min_transform_block_size);
  COPY_FIELD (max_transform_hierarchy_depth_inter);
  COPY_FIELD (max_transform_hierarchy_depth_intra);
  COPY_FIELD (num_short_term_ref_pic_sets);
  COPY_FIELD (num_long_term_ref_pics_sps);
  COPY_FIELD (scaling_list_enabled_flag);
  COPY_FIELD (amp_enabled_flag);
  COPY_FIELD (sample_adaptive_offset_enabled_flag);
  COPY_FIELD (pcm_enabled_flag);

  if (sps->pcm_enabled_flag) {
    COPY_FIELD (pcm_sample_bit_depth_luma_minus1);
    COPY_FIELD (pcm_sample_bit_depth_chroma_minus1);
    COPY_FIELD (log2_min_pcm_luma_coding_block_size_minus3);
    COPY_FIELD (log2_diff_max_min_pcm_luma_coding_block_size);
  }

  COPY_FIELD (pcm_loop_filter_disabled_flag);
  COPY_FIELD (long_term_ref_pics_present_flag);
  COPY_FIELD_WITH_PREFIX (temporal_mvp_enabled_flag);
  COPY_FIELD (strong_intra_smoothing_enabled_flag);

#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
}

static void
gst_d3d11_h265_dec_picture_params_from_pps (GstD3D11H265Dec * self,
    const GstH265PPS * pps, DXVA_PicParams_HEVC * params)
{
  guint i;

#define COPY_FIELD(f) \
  (params)->f = (pps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(pps_,f) = (pps)->f

  COPY_FIELD (num_ref_idx_l0_default_active_minus1);
  COPY_FIELD (num_ref_idx_l1_default_active_minus1);
  COPY_FIELD (init_qp_minus26);
  COPY_FIELD (dependent_slice_segments_enabled_flag);
  COPY_FIELD (output_flag_present_flag);
  COPY_FIELD (num_extra_slice_header_bits);
  COPY_FIELD (sign_data_hiding_enabled_flag);
  COPY_FIELD (cabac_init_present_flag);
  COPY_FIELD (constrained_intra_pred_flag);
  COPY_FIELD (transform_skip_enabled_flag);
  COPY_FIELD (cu_qp_delta_enabled_flag);
  COPY_FIELD_WITH_PREFIX (slice_chroma_qp_offsets_present_flag);
  COPY_FIELD (weighted_pred_flag);
  COPY_FIELD (weighted_bipred_flag);
  COPY_FIELD (transquant_bypass_enabled_flag);
  COPY_FIELD (tiles_enabled_flag);
  COPY_FIELD (entropy_coding_sync_enabled_flag);
  COPY_FIELD (uniform_spacing_flag);

  if (pps->tiles_enabled_flag)
    COPY_FIELD (loop_filter_across_tiles_enabled_flag);

  COPY_FIELD_WITH_PREFIX (loop_filter_across_slices_enabled_flag);
  COPY_FIELD (deblocking_filter_override_enabled_flag);
  COPY_FIELD_WITH_PREFIX (deblocking_filter_disabled_flag);
  COPY_FIELD (lists_modification_present_flag);
  COPY_FIELD (slice_segment_header_extension_present_flag);
  COPY_FIELD_WITH_PREFIX (cb_qp_offset);
  COPY_FIELD_WITH_PREFIX (cr_qp_offset);

  if (pps->tiles_enabled_flag) {
    COPY_FIELD (num_tile_columns_minus1);
    COPY_FIELD (num_tile_rows_minus1);
    if (!pps->uniform_spacing_flag) {
      for (i = 0; i < pps->num_tile_columns_minus1 &&
          i < G_N_ELEMENTS (params->column_width_minus1); i++)
        COPY_FIELD (column_width_minus1[i]);

      for (i = 0; i < pps->num_tile_rows_minus1 &&
          i < G_N_ELEMENTS (params->row_height_minus1); i++)
        COPY_FIELD (row_height_minus1[i]);
    }
  }

  COPY_FIELD (diff_cu_qp_delta_depth);
  COPY_FIELD_WITH_PREFIX (beta_offset_div2);
  COPY_FIELD_WITH_PREFIX (tc_offset_div2);
  COPY_FIELD (log2_parallel_merge_level_minus2);

#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
}

static void
gst_d3d11_h265_dec_picture_params_from_slice_header (GstD3D11H265Dec *
    self, const GstH265SliceHdr * slice_header, DXVA_PicParams_HEVC * params)
{
  if (slice_header->short_term_ref_pic_set_sps_flag == 0) {
    params->ucNumDeltaPocsOfRefRpsIdx =
        slice_header->short_term_ref_pic_sets.NumDeltaPocsOfRefRpsIdx;
    params->wNumBitsForShortTermRPSInSlice =
        slice_header->short_term_ref_pic_set_size;
  }
}

static gboolean
gst_d3d11_h265_dec_fill_picture_params (GstD3D11H265Dec * self,
    const GstH265SliceHdr * slice_header, DXVA_PicParams_HEVC * params)
{
  const GstH265SPS *sps;
  const GstH265PPS *pps;

  g_return_val_if_fail (slice_header->pps != NULL, FALSE);
  g_return_val_if_fail (slice_header->pps->sps != NULL, FALSE);

  pps = slice_header->pps;
  sps = pps->sps;

  /* not related to hevc syntax */
  params->NoPicReorderingFlag = 0;
  params->NoBiPredFlag = 0;
  params->ReservedBits1 = 0;
  params->StatusReportFeedbackNumber = 1;

  gst_d3d11_h265_dec_picture_params_from_sps (self, sps, params);
  gst_d3d11_h265_dec_picture_params_from_pps (self, pps, params);
  gst_d3d11_h265_dec_picture_params_from_slice_header (self,
      slice_header, params);

  return TRUE;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_h265_dec_get_output_view_from_picture (GstD3D11H265Dec * self,
    GstH265Picture * picture, guint8 * view_id)
{
  GstD3D11H265DecInner *inner = self->inner;
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_h265_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view = gst_d3d11_decoder_get_output_view_from_buffer (inner->d3d11_decoder,
      view_buffer, view_id);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static UCHAR
gst_d3d11_h265_dec_get_ref_index (const DXVA_PicParams_HEVC * pic_params,
    guint8 view_id)
{
  for (UCHAR i = 0; i < G_N_ELEMENTS (pic_params->RefPicList); i++) {
    if (pic_params->RefPicList[i].Index7Bits == view_id)
      return i;
  }

  return 0xff;
}

static inline void
init_pic_params (DXVA_PicParams_HEVC * params)
{
  memset (params, 0, sizeof (DXVA_PicParams_HEVC));
  for (guint i = 0; i < G_N_ELEMENTS (params->RefPicList); i++)
    params->RefPicList[i].bPicEntry = 0xff;

  for (guint i = 0; i < G_N_ELEMENTS (params->RefPicSetStCurrBefore); i++) {
    params->RefPicSetStCurrBefore[i] = 0xff;
    params->RefPicSetStCurrAfter[i] = 0xff;
    params->RefPicSetLtCurr[i] = 0xff;
  }
}

static GstFlowReturn
gst_d3d11_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  DXVA_PicParams_HEVC *pic_params = &inner->pic_params;
  DXVA_Qmatrix_HEVC *iq_matrix = &inner->iq_matrix;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  guint i, j;
  GArray *dpb_array;
  GstH265SPS *sps;
  GstH265PPS *pps;
  GstH265ScalingList *scaling_list = nullptr;

  pps = slice->header.pps;
  sps = pps->sps;

  view = gst_d3d11_h265_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  init_pic_params (pic_params);
  gst_d3d11_h265_dec_fill_picture_params (self, &slice->header, pic_params);

  pic_params->CurrPic.Index7Bits = view_id;
  pic_params->IrapPicFlag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);
  pic_params->IdrPicFlag = GST_H265_IS_NAL_TYPE_IDR (slice->nalu.type);
  pic_params->IntraPicFlag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);
  pic_params->CurrPicOrderCntVal = picture->pic_order_cnt;

  dpb_array = gst_h265_dpb_get_pictures_all (dpb);
  for (i = 0, j = 0;
      i < dpb_array->len && j < G_N_ELEMENTS (pic_params->RefPicList); i++) {
    GstH265Picture *other = g_array_index (dpb_array, GstH265Picture *, i);
    guint8 id = 0xff;

    if (!other->ref) {
      GST_LOG_OBJECT (self, "%dth picture in dpb is not reference, skip", i);
      continue;
    }

    gst_d3d11_h265_dec_get_output_view_from_picture (self, other, &id);
    pic_params->RefPicList[j].Index7Bits = id;
    pic_params->RefPicList[j].AssociatedFlag = other->long_term;
    pic_params->PicOrderCntValList[j] = other->pic_order_cnt;
    j++;
  }
  g_array_unref (dpb_array);

  for (i = 0, j = 0; i < G_N_ELEMENTS (pic_params->RefPicSetStCurrBefore); i++) {
    GstH265Picture *other = nullptr;
    guint8 other_view_id = 0xff;
    guint8 id = 0xff;

    while (!other && j < decoder->NumPocStCurrBefore)
      other = decoder->RefPicSetStCurrBefore[j++];

    if (other) {
      ID3D11VideoDecoderOutputView *other_view;

      other_view = gst_d3d11_h265_dec_get_output_view_from_picture (self,
          other, &other_view_id);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (pic_params, other_view_id);
    }

    pic_params->RefPicSetStCurrBefore[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (pic_params->RefPicSetStCurrAfter); i++) {
    GstH265Picture *other = nullptr;
    guint8 other_view_id = 0xff;
    guint8 id = 0xff;

    while (!other && j < decoder->NumPocStCurrAfter)
      other = decoder->RefPicSetStCurrAfter[j++];

    if (other) {
      ID3D11VideoDecoderOutputView *other_view;

      other_view = gst_d3d11_h265_dec_get_output_view_from_picture (self,
          other, &other_view_id);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (pic_params, other_view_id);
    }

    pic_params->RefPicSetStCurrAfter[i] = id;
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (pic_params->RefPicSetLtCurr); i++) {
    GstH265Picture *other = nullptr;
    guint8 other_view_id = 0xff;
    guint8 id = 0xff;

    while (!other && j < decoder->NumPocLtCurr)
      other = decoder->RefPicSetLtCurr[j++];

    if (other) {
      ID3D11VideoDecoderOutputView *other_view;

      other_view = gst_d3d11_h265_dec_get_output_view_from_picture (self,
          other, &other_view_id);

      if (other_view)
        id = gst_d3d11_h265_dec_get_ref_index (pic_params, other_view_id);
    }

    pic_params->RefPicSetLtCurr[i] = id;
  }

  if (pps->scaling_list_data_present_flag ||
      (sps->scaling_list_enabled_flag
          && !sps->scaling_list_data_present_flag)) {
    scaling_list = &pps->scaling_list;
  } else if (sps->scaling_list_enabled_flag &&
      sps->scaling_list_data_present_flag) {
    scaling_list = &sps->scaling_list;
  }

  if (scaling_list) {
    G_STATIC_ASSERT (sizeof (iq_matrix->ucScalingLists0) ==
        sizeof (scaling_list->scaling_lists_4x4));
    G_STATIC_ASSERT (sizeof (iq_matrix->ucScalingLists1) ==
        sizeof (scaling_list->scaling_lists_8x8));
    G_STATIC_ASSERT (sizeof (iq_matrix->ucScalingLists2) ==
        sizeof (scaling_list->scaling_lists_16x16));
    G_STATIC_ASSERT (sizeof (iq_matrix->ucScalingLists3) ==
        sizeof (scaling_list->scaling_lists_32x32));

    memcpy (iq_matrix->ucScalingLists0, scaling_list->scaling_lists_4x4,
        sizeof (iq_matrix->ucScalingLists0));
    memcpy (iq_matrix->ucScalingLists1, scaling_list->scaling_lists_8x8,
        sizeof (iq_matrix->ucScalingLists1));
    memcpy (iq_matrix->ucScalingLists2, scaling_list->scaling_lists_16x16,
        sizeof (iq_matrix->ucScalingLists2));
    memcpy (iq_matrix->ucScalingLists3, scaling_list->scaling_lists_32x32,
        sizeof (iq_matrix->ucScalingLists3));

    for (i = 0; i < G_N_ELEMENTS (iq_matrix->ucScalingListDCCoefSizeID2); i++) {
      iq_matrix->ucScalingListDCCoefSizeID2[i] =
          scaling_list->scaling_list_dc_coef_minus8_16x16[i] + 8;
    }

    for (i = 0; i < G_N_ELEMENTS (iq_matrix->ucScalingListDCCoefSizeID3); i++) {
      iq_matrix->ucScalingListDCCoefSizeID3[i] =
          scaling_list->scaling_list_dc_coef_minus8_32x32[i] + 8;
    }

    inner->submit_iq_data = TRUE;
  } else {
    inner->submit_iq_data = FALSE;
  }

  inner->slice_list.resize (0);
  inner->bitstream_buffer.resize (0);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice,
    GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  DXVA_Slice_HEVC_Short dxva_slice;
  static const guint8 start_code[] = { 0, 0, 1 };
  const size_t start_code_size = sizeof (start_code);

  dxva_slice.BSNALunitDataLocation = inner->bitstream_buffer.size ();
  /* Includes 3 bytes start code prefix */
  dxva_slice.SliceBytesInBuffer = slice->nalu.size + start_code_size;
  dxva_slice.wBadSliceChopping = 0;

  inner->slice_list.push_back (dxva_slice);

  size_t pos = inner->bitstream_buffer.size ();
  inner->bitstream_buffer.resize (pos + start_code_size + slice->nalu.size);

  /* Fill start code prefix */
  memcpy (&inner->bitstream_buffer[0] + pos, start_code, start_code_size);

  /* Copy bitstream */
  memcpy (&inner->bitstream_buffer[0] + pos + start_code_size,
      slice->nalu.data + slice->nalu.offset, slice->nalu.size);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_h265_dec_end_picture (GstH265Decoder * decoder,
    GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  size_t bitstream_buffer_size;
  size_t bitstream_pos;
  GstD3D11DecodeInputStreamArgs input_args;

  GST_LOG_OBJECT (self, "end picture %p, (poc %d)",
      picture, picture->pic_order_cnt);

  if (inner->bitstream_buffer.empty () || inner->slice_list.empty ()) {
    GST_ERROR_OBJECT (self, "No bitstream buffer to submit");
    return GST_FLOW_ERROR;
  }

  view = gst_d3d11_h265_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (&input_args, 0, sizeof (GstD3D11DecodeInputStreamArgs));

  bitstream_pos = inner->bitstream_buffer.size ();
  bitstream_buffer_size = GST_ROUND_UP_128 (bitstream_pos);

  if (bitstream_buffer_size > bitstream_pos) {
    size_t padding = bitstream_buffer_size - bitstream_pos;

    /* As per DXVA spec, total amount of bitstream buffer size should be
     * 128 bytes aligned. If actual data is not multiple of 128 bytes,
     * the last slice data needs to be zero-padded */
    inner->bitstream_buffer.resize (bitstream_buffer_size, 0);

    DXVA_Slice_HEVC_Short & slice = inner->slice_list.back ();
    slice.SliceBytesInBuffer += padding;
  }

  input_args.picture_params = &inner->pic_params;
  input_args.picture_params_size = sizeof (DXVA_PicParams_HEVC);
  input_args.slice_control = &inner->slice_list[0];
  input_args.slice_control_size =
      sizeof (DXVA_Slice_HEVC_Short) * inner->slice_list.size ();
  input_args.bitstream = &inner->bitstream_buffer[0];
  input_args.bitstream_size = inner->bitstream_buffer.size ();

  if (inner->submit_iq_data) {
    input_args.inverse_quantization_matrix = &inner->iq_matrix;
    input_args.inverse_quantization_matrix_size = sizeof (DXVA_Qmatrix_HEVC);
  }

  return gst_d3d11_decoder_decode_frame (inner->d3d11_decoder,
      view, &input_args);
}

static GstFlowReturn
gst_d3d11_h265_dec_output_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture)
{
  GstD3D11H265Dec *self = GST_D3D11_H265_DEC (decoder);
  GstD3D11H265DecInner *inner = self->inner;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p, poc %d, picture_struct %d, "
      "buffer flags 0x%x", picture, picture->pic_order_cnt, picture->pic_struct,
      picture->buffer_flags);

  view_buffer = (GstBuffer *) gst_h265_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!gst_d3d11_decoder_process_output (inner->d3d11_decoder, vdec,
          picture->discont_state, inner->width, inner->height, view_buffer,
          &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  GST_BUFFER_FLAG_SET (frame->output_buffer, picture->buffer_flags);
  gst_h265_picture_unref (picture);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);

error:
  gst_h265_picture_unref (picture);
  gst_video_decoder_release_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

void
gst_d3d11_h265_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  const GUID *profile = NULL;
  GTypeInfo type_info = {
    sizeof (GstD3D11H265DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_h265_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11H265Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_h265_dec_init,
  };
  const GUID *main_10_guid = NULL;
  const GUID *main_guid = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  GstCaps *src_caps_copy;
  GstCaps *tmp;
  GstCapsFeatures *caps_features;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  gboolean have_main10 = FALSE;
  gboolean have_main = FALSE;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

  have_main10 = gst_d3d11_decoder_get_supported_decoder_profile (device,
      GST_DXVA_CODEC_H265, GST_VIDEO_FORMAT_P010_10LE, &main_10_guid);
  if (!have_main10) {
    GST_DEBUG_OBJECT (device, "decoder does not support HEVC_VLD_MAIN10");
  } else {
    have_main10 &=
        gst_d3d11_decoder_supports_format (device, main_10_guid,
        DXGI_FORMAT_P010);
    if (!have_main10) {
      GST_FIXME_OBJECT (device, "device does not support P010 format");
    }
  }

  have_main = gst_d3d11_decoder_get_supported_decoder_profile (device,
      GST_DXVA_CODEC_H265, GST_VIDEO_FORMAT_NV12, &main_guid);
  if (!have_main) {
    GST_DEBUG_OBJECT (device, "decoder does not support HEVC_VLD_MAIN");
  } else {
    have_main =
        gst_d3d11_decoder_supports_format (device, main_guid, DXGI_FORMAT_NV12);
    if (!have_main) {
      GST_FIXME_OBJECT (device, "device does not support NV12 format");
    }
  }

  if (!have_main10 && !have_main) {
    GST_INFO_OBJECT (device, "device does not support h.265 decoding");
    return;
  }

  if (have_main) {
    profile = main_guid;
    format = DXGI_FORMAT_NV12;
  } else {
    profile = main_10_guid;
    format = DXGI_FORMAT_P010;
  }

  for (i = 0; i < G_N_ELEMENTS (gst_dxva_resolutions); i++) {
    if (gst_d3d11_decoder_supports_resolution (device, profile,
            format, gst_dxva_resolutions[i].width,
            gst_dxva_resolutions[i].height)) {
      max_width = gst_dxva_resolutions[i].width;
      max_height = gst_dxva_resolutions[i].height;

      GST_DEBUG_OBJECT (device,
          "device support resolution %dx%d", max_width, max_height);
    } else {
      break;
    }
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (device, "Couldn't query supported resolution");
    return;
  }

  sink_caps = gst_caps_from_string ("video/x-h265, "
      "stream-format=(string) { hev1, hvc1, byte-stream }, "
      "alignment= (string) au");
  src_caps = gst_caps_new_empty_simple ("video/x-raw");

  if (have_main10) {
    /* main10 profile covers main and main10 */
    GValue profile_list = G_VALUE_INIT;
    GValue profile_value = G_VALUE_INIT;
    GValue format_list = G_VALUE_INIT;
    GValue format_value = G_VALUE_INIT;

    g_value_init (&profile_list, GST_TYPE_LIST);

    g_value_init (&profile_value, G_TYPE_STRING);
    g_value_set_string (&profile_value, "main");
    gst_value_list_append_and_take_value (&profile_list, &profile_value);

    g_value_init (&profile_value, G_TYPE_STRING);
    g_value_set_string (&profile_value, "main-10");
    gst_value_list_append_and_take_value (&profile_list, &profile_value);


    g_value_init (&format_list, GST_TYPE_LIST);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "NV12");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "P010_10LE");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    gst_caps_set_value (sink_caps, "profile", &profile_list);
    gst_caps_set_value (src_caps, "format", &format_list);
    g_value_unset (&profile_list);
    g_value_unset (&format_list);
  } else {
    gst_caps_set_simple (sink_caps, "profile", G_TYPE_STRING, "main", NULL);
    gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);
  }

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);

  /* Copy src caps to append other capsfeatures */
  src_caps_copy = gst_caps_copy (src_caps);

  /* System memory with alternate interlace-mode */
  tmp = gst_caps_copy (src_caps_copy);
  caps_features = gst_caps_features_new (GST_CAPS_FEATURE_FORMAT_INTERLACED,
      NULL);
  gst_caps_set_features_simple (tmp, caps_features);
  gst_caps_set_simple (tmp, "interlace-mode", G_TYPE_STRING, "alternate", NULL);
  gst_caps_append (src_caps, tmp);

  /* D3D11 memory feature */
  tmp = gst_caps_copy (src_caps_copy);
  caps_features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
      NULL);
  gst_caps_set_features_simple (tmp, caps_features);
  gst_caps_append (src_caps, tmp);

  /* FIXME: D3D11 deinterlace element is not prepared, so this D3D11 with
   * interlaced caps feature is pointless at the moment */
#if 0
  /* D3D11 memory with alternate interlace-mode */
  tmp = gst_caps_copy (src_caps_copy);
  caps_features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
      GST_CAPS_FEATURE_FORMAT_INTERLACED, NULL);
  gst_caps_set_features_simple (tmp, caps_features);
  gst_caps_set_simple (tmp, "interlace-mode", G_TYPE_STRING, "alternate", NULL);
  gst_caps_append (src_caps, tmp);
#endif

  gst_caps_unref (src_caps_copy);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, GST_DXVA_CODEC_H265,
      sink_caps, src_caps, resolution);

  type_name = g_strdup ("GstD3D11H265Dec");
  feature_name = g_strdup ("d3d11h265dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11H265Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11h265device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_H265_DECODER,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
