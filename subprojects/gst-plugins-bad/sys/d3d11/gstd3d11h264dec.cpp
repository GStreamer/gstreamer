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
 *
 * NOTE: some of implementations are copied/modified from Chromium code
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

/**
 * SECTION:element-d3d11h264dec
 * @title: d3d11h264dec
 *
 * A Direct3D11/DXVA based H.264 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/h264/file ! parsebin ! d3d11h264dec ! d3d11videosink
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11h264dec.h"

#include <gst/codecs/gsth264decoder.h>
#include <string.h>
#include <vector>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_h264_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_h264_dec_debug

/* *INDENT-OFF* */
typedef struct _GstD3D11H264DecInner
{
  GstD3D11Device *device = nullptr;
  GstD3D11Decoder *d3d11_decoder = nullptr;

  DXVA_PicParams_H264 pic_params;
  DXVA_Qmatrix_H264 iq_matrix;

  std::vector<DXVA_Slice_H264_Short> slice_list;
  std::vector<guint8> bitstream_buffer;

  gint crop_x = 0;
  gint crop_y = 0;
  gint width = 0;
  gint height = 0;
  gint coded_width = 0;
  gint coded_height = 0;
  gint bitdepth = 0;
  guint8 chroma_format_idc = 0;
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;
  gboolean interlaced = FALSE;
  gint max_dpb_size = 0;
} GstD3D11H264DecInner;

/* *INDENT-ON* */
typedef struct _GstD3D11H264Dec
{
  GstH264Decoder parent;
  GstD3D11H264DecInner *inner;
} GstD3D11H264Dec;

typedef struct _GstD3D11H264DecClass
{
  GstH264DecoderClass parent_class;
  GstD3D11DecoderSubClassData class_data;
} GstD3D11H264DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_H264_DEC(object) ((GstD3D11H264Dec *) (object))
#define GST_D3D11_H264_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11H264DecClass))

static void gst_d3d11_h264_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_h264_dec_finalize (GObject * object);
static void gst_d3d11_h264_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_h264_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_h264_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_h264_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstH264Decoder */
static GstFlowReturn gst_d3d11_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size);
static GstFlowReturn gst_d3d11_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture);
static GstFlowReturn
gst_d3d11_h264_dec_new_field_picture (GstH264Decoder * decoder,
    GstH264Picture * first_field, GstH264Picture * second_field);
static GstFlowReturn gst_d3d11_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb);
static GstFlowReturn gst_d3d11_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1);
static GstFlowReturn gst_d3d11_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture);
static GstFlowReturn gst_d3d11_h264_dec_output_picture (GstH264Decoder *
    decoder, GstVideoCodecFrame * frame, GstH264Picture * picture);

static void
gst_d3d11_h264_dec_class_init (GstD3D11H264DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;

  gobject_class->get_property = gst_d3d11_h264_dec_get_property;
  gobject_class->finalize = gst_d3d11_h264_dec_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gst_d3d11_decoder_class_data_fill_subclass_data (cdata, &klass->class_data);

  /**
   * GstD3D11H264Dec:adapter-luid:
   *
   * DXGI Adapter LUID for this element
   *
   * Since: 1.20
   */
  gst_d3d11_decoder_proxy_class_init (element_class, cdata,
      "Seungha Yang <seungha.yang@navercorp.com>");

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_src_query);
  decoder_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_sink_event);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_new_sequence);
  h264decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_new_picture);
  h264decoder_class->new_field_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_new_field_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_start_picture);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_decode_slice);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_end_picture);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_output_picture);
}

static void
gst_d3d11_h264_dec_init (GstD3D11H264Dec * self)
{
  self->inner = new GstD3D11H264DecInner ();
}

static void
gst_d3d11_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (object);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_decoder_proxy_get_property (object, prop_id, value, pspec, cdata);
}

static void
gst_d3d11_h264_dec_finalize (GObject * object)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (object);

  delete self->inner;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_h264_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (element);
  GstD3D11H264DecInner *inner = self->inner;
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, cdata->adapter_luid, &inner->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

/* Clear all codec specific (e.g., SPS) data */
static void
gst_d3d11_h264_dec_reset (GstD3D11H264Dec * self)
{
  GstD3D11H264DecInner *inner = self->inner;

  inner->width = 0;
  inner->height = 0;
  inner->coded_width = 0;
  inner->coded_height = 0;
  inner->bitdepth = 0;
  inner->chroma_format_idc = 0;
  inner->out_format = GST_VIDEO_FORMAT_UNKNOWN;
  inner->interlaced = FALSE;
  inner->max_dpb_size = 0;
}

static gboolean
gst_d3d11_h264_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  if (!gst_d3d11_decoder_proxy_open (decoder,
          cdata, &inner->device, &inner->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  gst_d3d11_h264_dec_reset (self);

  return TRUE;
}

static gboolean
gst_d3d11_h264_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;

  gst_clear_object (&inner->d3d11_decoder);
  gst_clear_object (&inner->device);

  return TRUE;
}

static gboolean
gst_d3d11_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_negotiate (inner->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_h264_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_decide_allocation (inner->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_h264_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;

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
gst_d3d11_h264_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;

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
gst_d3d11_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  gint crop_width, crop_height;
  gboolean interlaced;
  gboolean modified = FALSE;

  GST_LOG_OBJECT (self, "new sequence");

  if (sps->frame_cropping_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  } else {
    crop_width = sps->width;
    crop_height = sps->height;
  }

  if (inner->width != crop_width || inner->height != crop_height ||
      inner->coded_width != sps->width || inner->coded_height != sps->height ||
      inner->crop_x != sps->crop_rect_x || inner->crop_y != sps->crop_rect_y) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d (%dx%d)",
        crop_width, crop_height, sps->width, sps->height);
    inner->crop_x = sps->crop_rect_x;
    inner->crop_y = sps->crop_rect_y;
    inner->width = crop_width;
    inner->height = crop_height;
    inner->coded_width = sps->width;
    inner->coded_height = sps->height;
    modified = TRUE;
  }

  if (inner->bitdepth != sps->bit_depth_luma_minus8 + 8) {
    GST_INFO_OBJECT (self, "bitdepth changed");
    inner->bitdepth = (guint) sps->bit_depth_luma_minus8 + 8;
    modified = TRUE;
  }

  if (inner->chroma_format_idc != sps->chroma_format_idc) {
    GST_INFO_OBJECT (self, "chroma format changed");
    inner->chroma_format_idc = sps->chroma_format_idc;
    modified = TRUE;
  }

  interlaced = !sps->frame_mbs_only_flag;
  if (inner->interlaced != interlaced) {
    GST_INFO_OBJECT (self, "interlaced sequence changed");
    inner->interlaced = interlaced;
    modified = TRUE;
  }

  if (inner->max_dpb_size < max_dpb_size) {
    GST_INFO_OBJECT (self, "Requires larger DPB size (%d -> %d)",
        inner->max_dpb_size, max_dpb_size);
    modified = TRUE;
  }

  if (modified || !gst_d3d11_decoder_is_configured (inner->d3d11_decoder)) {
    GstVideoInfo info;

    inner->out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (inner->bitdepth == 8) {
      if (inner->chroma_format_idc == 1)
        inner->out_format = GST_VIDEO_FORMAT_NV12;
      else {
        GST_FIXME_OBJECT (self, "Could not support 8bits non-4:2:0 format");
      }
    }

    if (inner->out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_video_info_set_format (&info,
        inner->out_format, inner->width, inner->height);
    if (inner->interlaced)
      GST_VIDEO_INFO_INTERLACE_MODE (&info) = GST_VIDEO_INTERLACE_MODE_MIXED;

    /* Store configured DPB size here. Then, it will be referenced later
     * to decide whether we need to re-open decoder object or not.
     * For instance, if every configuration is same apart from DPB size and
     * new DPB size is decreased, we can reuse existing decoder object.
     */
    inner->max_dpb_size = max_dpb_size;
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
gst_d3d11_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (inner->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return GST_FLOW_FLUSHING;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_h264_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New h264picture %p", picture);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_h264_dec_new_field_picture (GstH264Decoder * decoder,
    GstH264Picture * first_field, GstH264Picture * second_field)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstBuffer *view_buffer;

  view_buffer = (GstBuffer *) gst_h264_picture_get_user_data (first_field);

  if (!view_buffer) {
    GST_WARNING_OBJECT (self, "First picture does not have output view buffer");
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (self, "New field picture with buffer %" GST_PTR_FORMAT,
      view_buffer);

  gst_h264_picture_set_user_data (second_field,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return GST_FLOW_OK;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_h264_dec_get_output_view_from_picture (GstD3D11H264Dec * self,
    GstH264Picture * picture, guint8 * view_id)
{
  GstD3D11H264DecInner *inner = self->inner;
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_h264_picture_get_user_data (picture);
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

static void
gst_d3d11_h264_dec_picture_params_from_sps (GstD3D11H264Dec * self,
    const GstH264SPS * sps, gboolean field_pic, DXVA_PicParams_H264 * params)
{
#define COPY_FIELD(f) \
  (params)->f = (sps)->f

  params->wFrameWidthInMbsMinus1 = sps->pic_width_in_mbs_minus1;
  if (!sps->frame_mbs_only_flag) {
    params->wFrameHeightInMbsMinus1 =
        ((sps->pic_height_in_map_units_minus1 + 1) << 1) - 1;
  } else {
    params->wFrameHeightInMbsMinus1 = sps->pic_height_in_map_units_minus1;
  }
  params->residual_colour_transform_flag = sps->separate_colour_plane_flag;
  params->MbaffFrameFlag = (sps->mb_adaptive_frame_field_flag && !field_pic);
  params->field_pic_flag = field_pic;
  params->MinLumaBipredSize8x8Flag = sps->level_idc >= 31;

  COPY_FIELD (num_ref_frames);
  COPY_FIELD (chroma_format_idc);
  COPY_FIELD (frame_mbs_only_flag);
  COPY_FIELD (bit_depth_luma_minus8);
  COPY_FIELD (bit_depth_chroma_minus8);
  COPY_FIELD (log2_max_frame_num_minus4);
  COPY_FIELD (pic_order_cnt_type);
  COPY_FIELD (log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (delta_pic_order_always_zero_flag);
  COPY_FIELD (direct_8x8_inference_flag);

#undef COPY_FIELD
}

static void
gst_d3d11_h264_dec_picture_params_from_pps (GstD3D11H264Dec * self,
    const GstH264PPS * pps, DXVA_PicParams_H264 * params)
{
#define COPY_FIELD(f) \
  (params)->f = (pps)->f

  COPY_FIELD (constrained_intra_pred_flag);
  COPY_FIELD (weighted_pred_flag);
  COPY_FIELD (weighted_bipred_idc);
  COPY_FIELD (transform_8x8_mode_flag);
  COPY_FIELD (pic_init_qs_minus26);
  COPY_FIELD (chroma_qp_index_offset);
  COPY_FIELD (second_chroma_qp_index_offset);
  COPY_FIELD (pic_init_qp_minus26);
  COPY_FIELD (num_ref_idx_l0_active_minus1);
  COPY_FIELD (num_ref_idx_l1_active_minus1);
  COPY_FIELD (entropy_coding_mode_flag);
  COPY_FIELD (pic_order_present_flag);
  COPY_FIELD (deblocking_filter_control_present_flag);
  COPY_FIELD (redundant_pic_cnt_present_flag);
  COPY_FIELD (num_slice_groups_minus1);
  COPY_FIELD (slice_group_map_type);

#undef COPY_FIELD
}

static void
gst_d3d11_h264_dec_picture_params_from_slice_header (GstD3D11H264Dec *
    self, const GstH264SliceHdr * slice_header, DXVA_PicParams_H264 * params)
{
  params->sp_for_switch_flag = slice_header->sp_for_switch_flag;
  params->field_pic_flag = slice_header->field_pic_flag;
  params->CurrPic.AssociatedFlag = slice_header->bottom_field_flag;
  params->IntraPicFlag =
      GST_H264_IS_I_SLICE (slice_header) || GST_H264_IS_SI_SLICE (slice_header);
}

static gboolean
gst_d3d11_h264_dec_fill_picture_params (GstD3D11H264Dec * self,
    const GstH264SliceHdr * slice_header, DXVA_PicParams_H264 * params)
{
  const GstH264SPS *sps;
  const GstH264PPS *pps;

  g_return_val_if_fail (slice_header->pps != NULL, FALSE);
  g_return_val_if_fail (slice_header->pps->sequence != NULL, FALSE);

  pps = slice_header->pps;
  sps = pps->sequence;

  params->MbsConsecutiveFlag = 1;
  params->Reserved16Bits = 3;
  params->ContinuationFlag = 1;
  params->Reserved8BitsA = 0;
  params->Reserved8BitsB = 0;
  params->StatusReportFeedbackNumber = 1;

  gst_d3d11_h264_dec_picture_params_from_sps (self,
      sps, slice_header->field_pic_flag, params);
  gst_d3d11_h264_dec_picture_params_from_pps (self, pps, params);
  gst_d3d11_h264_dec_picture_params_from_slice_header (self,
      slice_header, params);

  return TRUE;
}

static inline void
init_pic_params (DXVA_PicParams_H264 * params)
{
  memset (params, 0, sizeof (DXVA_PicParams_H264));
  for (guint i = 0; i < G_N_ELEMENTS (params->RefFrameList); i++)
    params->RefFrameList[i].bPicEntry = 0xff;
}

static GstFlowReturn
gst_d3d11_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  DXVA_PicParams_H264 *pic_params = &inner->pic_params;
  DXVA_Qmatrix_H264 *iq_matrix = &inner->iq_matrix;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  GArray *dpb_array;
  GstH264PPS *pps;
  guint i, j;

  pps = slice->header.pps;

  view = gst_d3d11_h264_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  init_pic_params (pic_params);
  gst_d3d11_h264_dec_fill_picture_params (self, &slice->header, pic_params);

  pic_params->CurrPic.Index7Bits = view_id;
  pic_params->RefPicFlag = GST_H264_PICTURE_IS_REF (picture);
  pic_params->frame_num = picture->frame_num;

  if (picture->field == GST_H264_PICTURE_FIELD_TOP_FIELD) {
    pic_params->CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    pic_params->CurrFieldOrderCnt[1] = 0;
  } else if (picture->field == GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
    pic_params->CurrFieldOrderCnt[0] = 0;
    pic_params->CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
  } else {
    pic_params->CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    pic_params->CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
  }

  dpb_array = gst_h264_dpb_get_pictures_all (dpb);
  for (i = 0, j = 0; i < dpb_array->len && j < 16; i++) {
    GstH264Picture *other = g_array_index (dpb_array, GstH264Picture *, i);
    guint8 id = 0xff;

    if (!GST_H264_PICTURE_IS_REF (other))
      continue;

    /* The second field picture will be handled differently */
    if (other->second_field)
      continue;

    gst_d3d11_h264_dec_get_output_view_from_picture (self, other, &id);
    pic_params->RefFrameList[j].Index7Bits = id;

    if (GST_H264_PICTURE_IS_LONG_TERM_REF (other)) {
      pic_params->RefFrameList[j].AssociatedFlag = 1;
      pic_params->FrameNumList[j] = other->long_term_frame_idx;
    } else {
      pic_params->RefFrameList[j].AssociatedFlag = 0;
      pic_params->FrameNumList[j] = other->frame_num;
    }

    switch (other->field) {
      case GST_H264_PICTURE_FIELD_TOP_FIELD:
        pic_params->FieldOrderCntList[j][0] = other->top_field_order_cnt;
        pic_params->UsedForReferenceFlags |= 0x1 << (2 * j);
        break;
      case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
        pic_params->FieldOrderCntList[j][1] = other->bottom_field_order_cnt;
        pic_params->UsedForReferenceFlags |= 0x1 << (2 * j + 1);
        break;
      default:
        pic_params->FieldOrderCntList[j][0] = other->top_field_order_cnt;
        pic_params->FieldOrderCntList[j][1] = other->bottom_field_order_cnt;
        pic_params->UsedForReferenceFlags |= 0x3 << (2 * j);
        break;
    }

    if (other->other_field) {
      GstH264Picture *other_field = other->other_field;

      switch (other_field->field) {
        case GST_H264_PICTURE_FIELD_TOP_FIELD:
          pic_params->FieldOrderCntList[j][0] =
              other_field->top_field_order_cnt;
          pic_params->UsedForReferenceFlags |= 0x1 << (2 * j);
          break;
        case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
          pic_params->FieldOrderCntList[j][1] =
              other_field->bottom_field_order_cnt;
          pic_params->UsedForReferenceFlags |= 0x1 << (2 * j + 1);
          break;
        default:
          break;
      }
    }

    pic_params->NonExistingFrameFlags |= (other->nonexisting) << j;
    j++;
  }
  g_array_unref (dpb_array);

  G_STATIC_ASSERT (sizeof (iq_matrix->bScalingLists4x4) ==
      sizeof (pps->scaling_lists_4x4));
  memcpy (iq_matrix->bScalingLists4x4, pps->scaling_lists_4x4,
      sizeof (pps->scaling_lists_4x4));

  G_STATIC_ASSERT (sizeof (iq_matrix->bScalingLists8x8[0]) ==
      sizeof (pps->scaling_lists_8x8[0]));
  memcpy (iq_matrix->bScalingLists8x8[0], pps->scaling_lists_8x8[0],
      sizeof (pps->scaling_lists_8x8[0]));
  memcpy (iq_matrix->bScalingLists8x8[1], pps->scaling_lists_8x8[1],
      sizeof (pps->scaling_lists_8x8[1]));

  inner->slice_list.resize (0);
  inner->bitstream_buffer.resize (0);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  DXVA_Slice_H264_Short dxva_slice;
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
gst_d3d11_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
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

  view = gst_d3d11_h264_dec_get_output_view_from_picture (self, picture,
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

    DXVA_Slice_H264_Short & slice = inner->slice_list.back ();
    slice.SliceBytesInBuffer += padding;
  }

  input_args.picture_params = &inner->pic_params;
  input_args.picture_params_size = sizeof (DXVA_PicParams_H264);
  input_args.slice_control = &inner->slice_list[0];
  input_args.slice_control_size =
      sizeof (DXVA_Slice_H264_Short) * inner->slice_list.size ();
  input_args.bitstream = &inner->bitstream_buffer[0];
  input_args.bitstream_size = inner->bitstream_buffer.size ();
  input_args.inverse_quantization_matrix = &inner->iq_matrix;
  input_args.inverse_quantization_matrix_size = sizeof (DXVA_Qmatrix_H264);

  return gst_d3d11_decoder_decode_frame (inner->d3d11_decoder,
      view, &input_args);
}

static GstFlowReturn
gst_d3d11_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecInner *inner = self->inner;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  view_buffer = (GstBuffer *) gst_h264_picture_get_user_data (picture);

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

void
gst_d3d11_h264_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank, gboolean legacy)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  gboolean ret;
  GTypeInfo type_info = {
    sizeof (GstD3D11H264DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_h264_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11H264Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_h264_dec_init,
  };
  const GUID *supported_profile = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;

  ret = gst_d3d11_decoder_get_supported_decoder_profile (device,
      GST_DXVA_CODEC_H264, GST_VIDEO_FORMAT_NV12, &supported_profile);

  if (!ret) {
    GST_WARNING_OBJECT (device, "decoder profile unavailable");
    return;
  }

  ret =
      gst_d3d11_decoder_supports_format (device, supported_profile,
      DXGI_FORMAT_NV12);
  if (!ret) {
    GST_FIXME_OBJECT (device, "device does not support NV12 format");
    return;
  }

  /* we will not check the maximum resolution for legacy devices.
   * it might cause crash */
  if (legacy) {
    max_width = gst_dxva_resolutions[0].width;
    max_height = gst_dxva_resolutions[0].height;
  } else {
    for (i = 0; i < G_N_ELEMENTS (gst_dxva_resolutions); i++) {
      if (gst_d3d11_decoder_supports_resolution (device, supported_profile,
              DXGI_FORMAT_NV12, gst_dxva_resolutions[i].width,
              gst_dxva_resolutions[i].height)) {
        max_width = gst_dxva_resolutions[i].width;
        max_height = gst_dxva_resolutions[i].height;

        GST_DEBUG_OBJECT (device,
            "device support resolution %dx%d", max_width, max_height);
      } else {
        break;
      }
    }
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (device, "Couldn't query supported resolution");
    return;
  }

  sink_caps = gst_caps_from_string ("video/x-h264, "
      "stream-format= (string) { avc, avc3, byte-stream }, "
      "alignment= (string) au, "
      "profile = (string) { high, progressive-high, constrained-high, main, constrained-baseline, baseline }");
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), format = (string) NV12; "
      "video/x-raw, format = (string) NV12");

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, GST_DXVA_CODEC_H264,
      sink_caps, src_caps, resolution);

  type_name = g_strdup ("GstD3D11H264Dec");
  feature_name = g_strdup ("d3d11h264dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11H264Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11h264device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_H264_DECODER,
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
