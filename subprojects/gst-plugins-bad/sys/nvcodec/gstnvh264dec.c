/* GStreamer
 * Copyright (C) 2017 Ericsson AB. All rights reserved.
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
 *
 * Copyright 2015 The Chromium Authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvh264dec.h"
#include "gstcudautils.h"
#include "gstnvdecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_h264_dec_debug);
#define GST_CAT_DEFAULT gst_nv_h264_dec_debug

struct _GstNvH264Dec
{
  GstH264Decoder parent;

  GstVideoCodecState *output_state;

  GstCudaContext *context;
  GstNvDecoder *decoder;
  CUVIDPICPARAMS params;

  /* slice buffer which will be passed to CUVIDPICPARAMS::pBitstreamData */
  guint8 *bitstream_buffer;
  /* allocated memory size of bitstream_buffer */
  gsize bitstream_buffer_alloc_size;
  /* current offset of bitstream_buffer (per frame) */
  gsize bitstream_buffer_offset;

  guint *slice_offsets;
  guint slice_offsets_alloc_len;
  guint num_slices;

  guint width, height;
  guint coded_width, coded_height;
  guint bitdepth;
  guint chroma_format_idc;
  gint max_dpb_size;

  gboolean interlaced;

  GArray *ref_list;
};

struct _GstNvH264DecClass
{
  GstH264DecoderClass parent_class;
  guint cuda_device_id;
};

#define gst_nv_h264_dec_parent_class parent_class
G_DEFINE_TYPE (GstNvH264Dec, gst_nv_h264_dec, GST_TYPE_H264_DECODER);

static void gst_nv_h264_decoder_dispose (GObject * object);
static void gst_nv_h264_dec_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_nv_h264_dec_open (GstVideoDecoder * decoder);
static gboolean gst_nv_h264_dec_close (GstVideoDecoder * decoder);
static gboolean gst_nv_h264_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_nv_h264_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_nv_h264_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstH264Decoder */
static GstFlowReturn gst_nv_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size);
static GstFlowReturn gst_nv_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture);
static GstFlowReturn gst_nv_h264_dec_new_field_picture (GstH264Decoder *
    decoder, const GstH264Picture * first_field, GstH264Picture * second_field);
static GstFlowReturn gst_nv_h264_dec_output_picture (GstH264Decoder *
    decoder, GstVideoCodecFrame * frame, GstH264Picture * picture);
static GstFlowReturn gst_nv_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb);
static GstFlowReturn gst_nv_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1);
static GstFlowReturn gst_nv_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture);
static guint
gst_nv_h264_dec_get_preferred_output_delay (GstH264Decoder * decoder,
    gboolean live);

static void
gst_nv_h264_dec_class_init (GstNvH264DecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);

  /**
   * GstNvH264Dec
   *
   * Since: 1.18
   */

  object_class->dispose = gst_nv_h264_decoder_dispose;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_h264_dec_set_context);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_nv_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_nv_h264_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_nv_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_nv_h264_dec_src_query);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_new_sequence);
  h264decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_new_picture);
  h264decoder_class->new_field_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_new_field_picture);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_output_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_start_picture);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_decode_slice);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_end_picture);
  h264decoder_class->get_preferred_output_delay =
      GST_DEBUG_FUNCPTR (gst_nv_h264_dec_get_preferred_output_delay);

  GST_DEBUG_CATEGORY_INIT (gst_nv_h264_dec_debug,
      "nvh264dec", 0, "Nvidia H.264 Decoder");

  gst_type_mark_as_plugin_api (GST_TYPE_NV_H264_DEC, 0);
}

static void
gst_nv_h264_dec_init (GstNvH264Dec * self)
{
  self->ref_list = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 16);
  g_array_set_clear_func (self->ref_list,
      (GDestroyNotify) gst_h264_picture_clear);
}

static void
gst_nv_h264_decoder_dispose (GObject * object)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (object);

  g_clear_pointer (&self->ref_list, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_nv_h264_dec_set_context (GstElement * element, GstContext * context)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (element);
  GstNvH264DecClass *klass = GST_NV_H264_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "set context %s",
      gst_context_get_context_type (context));

  if (gst_cuda_handle_set_context (element, context, klass->cuda_device_id,
          &self->context)) {
    goto done;
  }

  if (self->decoder)
    gst_nv_decoder_handle_set_context (self->decoder, element, context);

done:
  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

/* Clear all codec specific (e.g., SPS) data */
static void
gst_d3d11_h264_dec_reset (GstNvH264Dec * self)
{
  self->width = 0;
  self->height = 0;
  self->coded_width = 0;
  self->coded_height = 0;
  self->bitdepth = 0;
  self->chroma_format_idc = 0;
  self->max_dpb_size = 0;
  self->interlaced = FALSE;
}

static gboolean
gst_nv_h264_dec_open (GstVideoDecoder * decoder)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  GstNvH264DecClass *klass = GST_NV_H264_DEC_GET_CLASS (self);

  if (!gst_cuda_ensure_element_context (GST_ELEMENT (self),
          klass->cuda_device_id, &self->context)) {
    GST_ERROR_OBJECT (self, "Required element data is unavailable");
    return FALSE;
  }

  self->decoder = gst_nv_decoder_new (self->context);
  if (!self->decoder) {
    GST_ERROR_OBJECT (self, "Failed to create decoder object");
    gst_clear_object (&self->context);

    return FALSE;
  }

  gst_d3d11_h264_dec_reset (self);

  return TRUE;
}

static gboolean
gst_nv_h264_dec_close (GstVideoDecoder * decoder)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);

  g_clear_pointer (&self->output_state, gst_video_codec_state_unref);
  gst_clear_object (&self->decoder);
  gst_clear_object (&self->context);

  g_clear_pointer (&self->bitstream_buffer, g_free);
  g_clear_pointer (&self->slice_offsets, g_free);

  self->bitstream_buffer_alloc_size = 0;
  self->slice_offsets_alloc_len = 0;

  return TRUE;
}

static gboolean
gst_nv_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "negotiate");

  gst_nv_decoder_negotiate (self->decoder, decoder, h264dec->input_state,
      &self->output_state);

  /* TODO: add support D3D11 memory */

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_nv_h264_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);

  if (!gst_nv_decoder_decide_allocation (self->decoder, decoder, query)) {
    GST_WARNING_OBJECT (self, "Failed to handle decide allocation");
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_nv_h264_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_cuda_handle_context_query (GST_ELEMENT (decoder), query,
              self->context)) {
        return TRUE;
      } else if (self->decoder &&
          gst_nv_decoder_handle_context_query (self->decoder, decoder, query)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static GstFlowReturn
gst_nv_h264_dec_new_sequence (GstH264Decoder * decoder, const GstH264SPS * sps,
    gint max_dpb_size)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  gint crop_width, crop_height;
  gboolean modified = FALSE;
  gboolean interlaced;

  GST_LOG_OBJECT (self, "new sequence");

  if (sps->frame_cropping_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  } else {
    crop_width = sps->width;
    crop_height = sps->height;
  }

  if (self->width != crop_width || self->height != crop_height ||
      self->coded_width != sps->width || self->coded_height != sps->height) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d (%dx%d)",
        crop_width, crop_height, sps->width, sps->height);
    self->width = crop_width;
    self->height = crop_height;
    self->coded_width = sps->width;
    self->coded_height = sps->height;
    modified = TRUE;
  }

  if (self->bitdepth != sps->bit_depth_luma_minus8 + 8) {
    GST_INFO_OBJECT (self, "bitdepth changed");
    self->bitdepth = sps->bit_depth_luma_minus8 + 8;
    modified = TRUE;
  }

  if (self->chroma_format_idc != sps->chroma_format_idc) {
    GST_INFO_OBJECT (self, "chroma format changed");
    self->chroma_format_idc = sps->chroma_format_idc;
    modified = TRUE;
  }

  interlaced = !sps->frame_mbs_only_flag;
  if (self->interlaced != interlaced) {
    GST_INFO_OBJECT (self, "interlaced sequence changed");
    self->interlaced = interlaced;
    modified = TRUE;
  }

  if (self->max_dpb_size < max_dpb_size) {
    GST_INFO_OBJECT (self, "Requires larger DPB size (%d -> %d)",
        self->max_dpb_size, max_dpb_size);
    modified = TRUE;
  }

  if (modified || !gst_nv_decoder_is_configured (self->decoder)) {
    GstVideoInfo info;
    GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->bitdepth == 8) {
      if (self->chroma_format_idc == 1)
        out_format = GST_VIDEO_FORMAT_NV12;
      else {
        GST_FIXME_OBJECT (self, "Could not support 8bits non-4:2:0 format");
      }
    } else if (self->bitdepth == 10) {
      if (self->chroma_format_idc == 1)
        out_format = GST_VIDEO_FORMAT_P010_10LE;
      else {
        GST_FIXME_OBJECT (self, "Could not support 10bits non-4:2:0 format");
      }
    }

    if (out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_video_info_set_format (&info, out_format, self->width, self->height);
    if (self->interlaced)
      GST_VIDEO_INFO_INTERLACE_MODE (&info) = GST_VIDEO_INTERLACE_MODE_MIXED;

    self->max_dpb_size = max_dpb_size;
    /* FIXME: add support cudaVideoCodec_H264_SVC and cudaVideoCodec_H264_MVC */
    if (!gst_nv_decoder_configure (self->decoder,
            cudaVideoCodec_H264, &info, self->coded_width, self->coded_height,
            self->bitdepth,
            /* Additional 4 buffers for render delay */
            max_dpb_size + 4)) {
      GST_ERROR_OBJECT (self, "Failed to configure decoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    memset (&self->params, 0, sizeof (CUVIDPICPARAMS));
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  GstNvDecoderFrame *nv_frame;

  nv_frame = gst_nv_decoder_new_frame (self->decoder);
  if (!nv_frame) {
    GST_ERROR_OBJECT (self, "No available decoder frame");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self,
      "New decoder frame %p (index %d)", nv_frame, nv_frame->index);

  gst_h264_picture_set_user_data (picture,
      nv_frame, (GDestroyNotify) gst_nv_decoder_frame_unref);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h264_dec_new_field_picture (GstH264Decoder * decoder,
    const GstH264Picture * first_field, GstH264Picture * second_field)
{
  GstNvDecoderFrame *nv_frame;

  nv_frame = (GstNvDecoderFrame *)
      gst_h264_picture_get_user_data ((GstH264Picture *) first_field);
  if (!nv_frame) {
    GST_ERROR_OBJECT (decoder,
        "No decoder frame in the first picture %p", first_field);
    return GST_FLOW_ERROR;
  }

  gst_h264_picture_set_user_data (second_field,
      gst_nv_decoder_frame_ref (nv_frame),
      (GDestroyNotify) gst_nv_decoder_frame_unref);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstNvDecoderFrame *decoder_frame;

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  decoder_frame =
      (GstNvDecoderFrame *) gst_h264_picture_get_user_data (picture);
  if (!decoder_frame) {
    GST_ERROR_OBJECT (self, "No decoder frame in picture %p", picture);
    goto error;
  }

  if (!gst_nv_decoder_finish_frame (self->decoder, vdec, decoder_frame,
          &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to handle output picture");
    goto error;
  }

  if (picture->buffer_flags != 0) {
    gboolean interlaced =
        (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) != 0;
    gboolean tff = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0;

    GST_TRACE_OBJECT (self,
        "apply buffer flags 0x%x (interlaced %d, top-field-first %d)",
        picture->buffer_flags, interlaced, tff);
    GST_BUFFER_FLAG_SET (frame->output_buffer, picture->buffer_flags);
  }

  gst_h264_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_h264_picture_unref (picture);
  gst_video_decoder_release_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

static GstNvDecoderFrame *
gst_nv_h264_dec_get_decoder_frame_from_picture (GstNvH264Dec * self,
    GstH264Picture * picture)
{
  GstNvDecoderFrame *frame;

  frame = (GstNvDecoderFrame *) gst_h264_picture_get_user_data (picture);

  if (!frame)
    GST_DEBUG_OBJECT (self, "current picture does not have decoder frame");

  return frame;
}

static void
gst_nv_h264_dec_fill_scaling_list_4x4 (const GstH264PPS * pps,
    CUVIDH264PICPARAMS * params)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (params->WeightScale4x4); i++)
    gst_h264_quant_matrix_4x4_get_raster_from_zigzag (params->WeightScale4x4[i],
        pps->scaling_lists_4x4[i]);
}

static void
gst_nv_h264_dec_fill_scaling_list_8x8 (const GstH264PPS * pps,
    CUVIDH264PICPARAMS * params)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (params->WeightScale8x8); i++) {
    gst_h264_quant_matrix_8x8_get_raster_from_zigzag (params->WeightScale8x8[i],
        pps->scaling_lists_8x8[i]);
  }
}

static void
gst_nv_h264_dec_picture_params_from_sps (GstNvH264Dec * self,
    const GstH264SPS * sps, gboolean field_pic, CUVIDH264PICPARAMS * params)
{
  params->residual_colour_transform_flag = sps->separate_colour_plane_flag;
  params->MbaffFrameFlag = sps->mb_adaptive_frame_field_flag && !field_pic;

#define COPY_FIELD(f) \
  (params)->f = (sps)->f

  COPY_FIELD (log2_max_frame_num_minus4);
  COPY_FIELD (pic_order_cnt_type);
  COPY_FIELD (log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (delta_pic_order_always_zero_flag);
  COPY_FIELD (frame_mbs_only_flag);
  COPY_FIELD (direct_8x8_inference_flag);
  COPY_FIELD (num_ref_frames);
  COPY_FIELD (bit_depth_luma_minus8);
  COPY_FIELD (bit_depth_chroma_minus8);
  COPY_FIELD (qpprime_y_zero_transform_bypass_flag);

#undef COPY_FIELD
}

static void
gst_nv_h264_dec_picture_params_from_pps (GstNvH264Dec * self,
    const GstH264PPS * pps, CUVIDH264PICPARAMS * params)
{
  params->second_chroma_qp_index_offset =
      (gint8) pps->second_chroma_qp_index_offset;

#define COPY_FIELD(f) \
  (params)->f = (pps)->f

  COPY_FIELD (entropy_coding_mode_flag);
  COPY_FIELD (pic_order_present_flag);
  COPY_FIELD (num_ref_idx_l0_active_minus1);
  COPY_FIELD (num_ref_idx_l1_active_minus1);
  COPY_FIELD (pic_init_qp_minus26);
  COPY_FIELD (weighted_pred_flag);
  COPY_FIELD (weighted_bipred_idc);
  COPY_FIELD (pic_init_qp_minus26);
  COPY_FIELD (deblocking_filter_control_present_flag);
  COPY_FIELD (redundant_pic_cnt_present_flag);
  COPY_FIELD (transform_8x8_mode_flag);
  COPY_FIELD (constrained_intra_pred_flag);
  COPY_FIELD (chroma_qp_index_offset);
#undef COPY_FIELD

  gst_nv_h264_dec_fill_scaling_list_4x4 (pps, params);
  gst_nv_h264_dec_fill_scaling_list_8x8 (pps, params);
}

static void
gst_nv_h264_dec_reset_bitstream_params (GstNvH264Dec * self)
{
  self->bitstream_buffer_offset = 0;
  self->num_slices = 0;

  self->params.nBitstreamDataLen = 0;
  self->params.pBitstreamData = NULL;
  self->params.nNumSlices = 0;
  self->params.pSliceDataOffsets = NULL;
}

static void
gst_nv_h264_dec_fill_dpb (GstNvH264Dec * self, GstH264Picture * ref,
    CUVIDH264DPBENTRY * dpb)
{
  GstNvDecoderFrame *frame;

  dpb->not_existing = ref->nonexisting;
  dpb->PicIdx = -1;

  frame = gst_nv_h264_dec_get_decoder_frame_from_picture (self, ref);
  if (!frame) {
    dpb->not_existing = 1;
  } else if (!dpb->not_existing) {
    dpb->PicIdx = frame->index;
  }

  if (dpb->not_existing)
    return;

  if (GST_H264_PICTURE_IS_LONG_TERM_REF (ref)) {
    dpb->FrameIdx = ref->long_term_frame_idx;
    dpb->is_long_term = 1;
  } else {
    dpb->FrameIdx = ref->frame_num;
    dpb->is_long_term = 0;
  }

  switch (ref->field) {
    case GST_H264_PICTURE_FIELD_FRAME:
      dpb->FieldOrderCnt[0] = ref->top_field_order_cnt;
      dpb->FieldOrderCnt[1] = ref->bottom_field_order_cnt;
      dpb->used_for_reference = 0x3;
      break;
    case GST_H264_PICTURE_FIELD_TOP_FIELD:
      dpb->FieldOrderCnt[0] = ref->top_field_order_cnt;
      dpb->used_for_reference = 0x1;
      if (ref->other_field) {
        dpb->FieldOrderCnt[1] = ref->other_field->bottom_field_order_cnt;
        dpb->used_for_reference |= 0x2;
      } else {
        dpb->FieldOrderCnt[1] = 0;
      }
      break;
    case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
      dpb->FieldOrderCnt[1] = ref->bottom_field_order_cnt;
      dpb->used_for_reference = 0x2;
      if (ref->other_field) {
        dpb->FieldOrderCnt[0] = ref->other_field->bottom_field_order_cnt;
        dpb->used_for_reference |= 0x1;
      } else {
        dpb->FieldOrderCnt[0] = 0;
      }
      break;
    default:
      dpb->FieldOrderCnt[0] = 0;
      dpb->FieldOrderCnt[1] = 0;
      dpb->used_for_reference = 0;
      break;
  }
}

static GstFlowReturn
gst_nv_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  CUVIDPICPARAMS *params = &self->params;
  CUVIDH264PICPARAMS *h264_params = &params->CodecSpecific.h264;
  const GstH264SliceHdr *slice_header = &slice->header;
  const GstH264SPS *sps;
  const GstH264PPS *pps;
  GstNvDecoderFrame *frame;
  GArray *ref_list = self->ref_list;
  guint i, ref_frame_idx;

  g_return_val_if_fail (slice_header->pps != NULL, FALSE);
  g_return_val_if_fail (slice_header->pps->sequence != NULL, FALSE);

  frame = gst_nv_h264_dec_get_decoder_frame_from_picture (self, picture);

  if (!frame) {
    GST_ERROR_OBJECT (self,
        "Couldn't get decoder frame frame picture %p", picture);
    return GST_FLOW_ERROR;
  }

  gst_nv_h264_dec_reset_bitstream_params (self);

  sps = slice_header->pps->sequence;
  pps = slice_header->pps;

  /* FIXME: update sps/pps related params only when it's required */
  params->PicWidthInMbs = sps->pic_width_in_mbs_minus1 + 1;
  if (!sps->frame_mbs_only_flag) {
    params->FrameHeightInMbs = (sps->pic_height_in_map_units_minus1 + 1) << 1;
  } else {
    params->FrameHeightInMbs = sps->pic_height_in_map_units_minus1 + 1;
  }
  params->CurrPicIdx = frame->index;
  params->field_pic_flag = slice_header->field_pic_flag;
  params->bottom_field_flag =
      picture->field == GST_H264_PICTURE_FIELD_BOTTOM_FIELD;
  params->second_field = picture->second_field;

  if (picture->field == GST_H264_PICTURE_FIELD_TOP_FIELD) {
    h264_params->CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    h264_params->CurrFieldOrderCnt[1] = 0;
  } else if (picture->field == GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
    h264_params->CurrFieldOrderCnt[0] = 0;
    h264_params->CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
  } else {
    h264_params->CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    h264_params->CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
  }

  /* nBitstreamDataLen, pBitstreamData, nNumSlices and pSliceDataOffsets
   * will be set later */

  params->ref_pic_flag = GST_H264_PICTURE_IS_REF (picture);
  /* will be updated later, if any slices belong to this frame is not
   * intra slice */
  params->intra_pic_flag = 1;

  h264_params->frame_num = picture->frame_num;
  h264_params->ref_pic_flag = GST_H264_PICTURE_IS_REF (picture);

  gst_nv_h264_dec_picture_params_from_sps (self,
      sps, slice_header->field_pic_flag, h264_params);
  gst_nv_h264_dec_picture_params_from_pps (self, pps, h264_params);

  ref_frame_idx = 0;
  g_array_set_size (ref_list, 0);

  memset (&h264_params->dpb, 0, sizeof (h264_params->dpb));
  gst_h264_dpb_get_pictures_short_term_ref (dpb, FALSE, FALSE, ref_list);
  for (i = 0; ref_frame_idx < 16 && i < ref_list->len; i++) {
    GstH264Picture *other = g_array_index (ref_list, GstH264Picture *, i);
    gst_nv_h264_dec_fill_dpb (self, other, &h264_params->dpb[ref_frame_idx]);
    ref_frame_idx++;
  }
  g_array_set_size (ref_list, 0);

  gst_h264_dpb_get_pictures_long_term_ref (dpb, FALSE, ref_list);
  for (i = 0; ref_frame_idx < 16 && i < ref_list->len; i++) {
    GstH264Picture *other = g_array_index (ref_list, GstH264Picture *, i);
    gst_nv_h264_dec_fill_dpb (self, other, &h264_params->dpb[ref_frame_idx]);
    ref_frame_idx++;
  }
  g_array_set_size (ref_list, 0);

  for (i = ref_frame_idx; i < 16; i++)
    h264_params->dpb[i].PicIdx = -1;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  gsize new_size;

  GST_LOG_OBJECT (self, "Decode slice, nalu size %u", slice->nalu.size);

  if (self->slice_offsets_alloc_len < self->num_slices + 1) {
    self->slice_offsets_alloc_len = 2 * (self->num_slices + 1);

    self->slice_offsets = (guint *) g_realloc_n (self->slice_offsets,
        self->slice_offsets_alloc_len, sizeof (guint));
  }
  self->slice_offsets[self->num_slices] = self->bitstream_buffer_offset;
  GST_LOG_OBJECT (self, "Slice offset %u for slice %d",
      self->slice_offsets[self->num_slices], self->num_slices);

  self->num_slices++;

  new_size = self->bitstream_buffer_offset + slice->nalu.size + 3;
  if (self->bitstream_buffer_alloc_size < new_size) {
    self->bitstream_buffer_alloc_size = 2 * new_size;

    self->bitstream_buffer = (guint8 *) g_realloc (self->bitstream_buffer,
        self->bitstream_buffer_alloc_size);
  }

  self->bitstream_buffer[self->bitstream_buffer_offset] = 0;
  self->bitstream_buffer[self->bitstream_buffer_offset + 1] = 0;
  self->bitstream_buffer[self->bitstream_buffer_offset + 2] = 1;

  memcpy (self->bitstream_buffer + self->bitstream_buffer_offset + 3,
      slice->nalu.data + slice->nalu.offset, slice->nalu.size);
  self->bitstream_buffer_offset = new_size;

  if (!GST_H264_IS_I_SLICE (&slice->header) &&
      !GST_H264_IS_SI_SLICE (&slice->header))
    self->params.intra_pic_flag = 0;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h264_dec_end_picture (GstH264Decoder * decoder, GstH264Picture * picture)
{
  GstNvH264Dec *self = GST_NV_H264_DEC (decoder);
  gboolean ret;
  CUVIDPICPARAMS *params = &self->params;

  params->nBitstreamDataLen = self->bitstream_buffer_offset;
  params->pBitstreamData = self->bitstream_buffer;
  params->nNumSlices = self->num_slices;
  params->pSliceDataOffsets = self->slice_offsets;

  GST_LOG_OBJECT (self, "End picture, bitstream len: %" G_GSIZE_FORMAT
      ", num slices %d", self->bitstream_buffer_offset, self->num_slices);

  ret = gst_nv_decoder_decode_picture (self->decoder, &self->params);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to decode picture");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static guint
gst_nv_h264_dec_get_preferred_output_delay (GstH264Decoder * decoder,
    gboolean live)
{
  /* Prefer to zero latency for live pipeline */
  if (live)
    return 0;

  /* NVCODEC SDK uses 4 frame delay for better throughput performance */
  return 4;
}

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  guint cuda_device_id;
  gboolean is_default;
} GstNvH264DecClassData;

static void
gst_nv_h264_dec_subclass_init (gpointer klass, GstNvH264DecClassData * cdata)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstNvH264DecClass *nvdec_class = (GstNvH264DecClass *) (klass);
  gchar *long_name;

  if (cdata->is_default) {
    long_name = g_strdup_printf ("NVDEC H.264 Stateless Decoder");
  } else {
    long_name = g_strdup_printf ("NVDEC H.264 Stateless Decoder with device %d",
        cdata->cuda_device_id);
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "Nvidia H.264 video decoder", "Seungha Yang <seungha@centricular.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  nvdec_class->cuda_device_id = cdata->cuda_device_id;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

void
gst_nv_h264_dec_register (GstPlugin * plugin, guint device_id, guint rank,
    GstCaps * sink_caps, GstCaps * src_caps, gboolean is_primary)
{
  GTypeQuery type_query;
  GTypeInfo type_info = { 0, };
  GType subtype;
  gchar *type_name;
  gchar *feature_name;
  GstNvH264DecClassData *cdata;
  gboolean is_default = TRUE;
  const GValue *value;
  GstStructure *s;

  /**
   * element-nvh264sldec
   *
   * Since: 1.18
   */

  cdata = g_new0 (GstNvH264DecClassData, 1);
  cdata->sink_caps = gst_caps_from_string ("video/x-h264, "
      "stream-format= (string) { avc, avc3, byte-stream }, "
      "alignment= (string) au, "
      "profile = (string) { high, main, constrained-high, constrained-baseline, baseline }, "
      "framerate = " GST_VIDEO_FPS_RANGE);

  s = gst_caps_get_structure (sink_caps, 0);
  value = gst_structure_get_value (s, "width");
  gst_caps_set_value (cdata->sink_caps, "width", value);

  value = gst_structure_get_value (s, "height");
  gst_caps_set_value (cdata->sink_caps, "height", value);

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  cdata->src_caps = gst_caps_ref (src_caps);
  cdata->cuda_device_id = device_id;

  g_type_query (GST_TYPE_NV_H264_DEC, &type_query);
  memset (&type_info, 0, sizeof (type_info));
  type_info.class_size = type_query.class_size;
  type_info.instance_size = type_query.instance_size;
  type_info.class_init = (GClassInitFunc) gst_nv_h264_dec_subclass_init;
  type_info.class_data = cdata;

  if (is_primary) {
    type_name = g_strdup ("GstNvH264StatelessPrimaryDec");
    feature_name = g_strdup ("nvh264dec");
  } else {
    type_name = g_strdup ("GstNvH264StatelessDec");
    feature_name = g_strdup ("nvh264sldec");
  }

  if (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    g_free (feature_name);
    if (is_primary) {
      type_name =
          g_strdup_printf ("GstNvH264StatelessPrimaryDevice%dDec", device_id);
      feature_name = g_strdup_printf ("nvh264device%ddec", device_id);
    } else {
      type_name = g_strdup_printf ("GstNvH264StatelessDevice%dDec", device_id);
      feature_name = g_strdup_printf ("nvh264sldevice%ddec", device_id);
    }

    is_default = FALSE;
  }

  cdata->is_default = is_default;
  subtype = g_type_register_static (GST_TYPE_NV_H264_DEC,
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && !is_default)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, subtype))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
