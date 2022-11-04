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
 * SECTION:element-d3d11vp9dec
 * @title: d3d11vp9dec
 *
 * A Direct3D11/DXVA based VP9 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/vp9/file ! parsebin ! d3d11vp9dec ! d3d11videosink
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11vp9dec.h"
#include "gstd3d11pluginutils.h"

#include <gst/codecs/gstvp9decoder.h>
#include <string.h>
#include <vector>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_vp9_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_vp9_dec_debug

/* *INDENT-OFF* */
typedef struct _GstD3D11Vp9DecInner
{
  GstD3D11Device *device = nullptr;
  GstD3D11Decoder *d3d11_decoder = nullptr;

  DXVA_PicParams_VP9 pic_params;
  DXVA_Slice_VPx_Short slice;

  /* In case of VP9, there's only one slice per picture so we don't
   * need this bitstream buffer, but this will be used for 128 bytes alignment */
  std::vector<guint8> bitstream_buffer;

  /* To calculate use_prev_in_find_mv_refs */
  guint last_frame_width = 0;
  guint last_frame_height = 0;
  gboolean last_show_frame = FALSE;
} GstD3D11Vp9DecInner;
/* *INDENT-ON* */

typedef struct _GstD3D11Vp9Dec
{
  GstVp9Decoder parent;
  GstD3D11Vp9DecInner *inner;
} GstD3D11Vp9Dec;

typedef struct _GstD3D11Vp9DecClass
{
  GstVp9DecoderClass parent_class;
  GstD3D11DecoderSubClassData class_data;
} GstD3D11Vp9DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_VP9_DEC(object) ((GstD3D11Vp9Dec *) (object))
#define GST_D3D11_VP9_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Vp9DecClass))

static void gst_d3d11_vp9_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp9_dec_finalize (GObject * object);
static void gst_d3d11_vp9_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_vp9_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_vp9_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstVp9Decoder */
static GstFlowReturn gst_d3d11_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHeader * frame_hdr, gint max_dpb_size);
static GstFlowReturn gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture);
static GstVp9Picture *gst_d3d11_vp9_dec_duplicate_picture (GstVp9Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp9Picture * picture);
static GstFlowReturn gst_d3d11_vp9_dec_start_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);
static GstFlowReturn gst_d3d11_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb);
static GstFlowReturn gst_d3d11_vp9_dec_end_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);
static GstFlowReturn gst_d3d11_vp9_dec_output_picture (GstVp9Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp9Picture * picture);

static void
gst_d3d11_vp9_dec_class_init (GstD3D11Vp9DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp9DecoderClass *vp9decoder_class = GST_VP9_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;

  gobject_class->get_property = gst_d3d11_vp9_dec_get_property;
  gobject_class->finalize = gst_d3d11_vp9_dec_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gst_d3d11_decoder_class_data_fill_subclass_data (cdata, &klass->class_data);

  /**
   * GstD3D11Vp9Dec:adapter-luid:
   *
   * DXGI Adapter LUID for this element
   *
   * Since: 1.20
   */

  gst_d3d11_decoder_proxy_class_init (element_class, cdata,
      "Seungha Yang <seungha.yang@navercorp.com>");

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_src_query);
  decoder_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_sink_event);

  vp9decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_new_sequence);
  vp9decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_new_picture);
  vp9decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_duplicate_picture);
  vp9decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_start_picture);
  vp9decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_decode_picture);
  vp9decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_end_picture);
  vp9decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_output_picture);
}

static void
gst_d3d11_vp9_dec_init (GstD3D11Vp9Dec * self)
{
  self->inner = new GstD3D11Vp9DecInner ();
}

static void
gst_d3d11_vp9_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (object);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_decoder_proxy_get_property (object, prop_id, value, pspec, cdata);
}

static void
gst_d3d11_vp9_dec_finalize (GObject * object)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (object);

  delete self->inner;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_vp9_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (element);
  GstD3D11Vp9DecInner *inner = self->inner;
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, cdata->adapter_luid, &inner->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder)
{
  GstVp9Decoder *vp9dec = GST_VP9_DECODER (decoder);
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  if (!gst_d3d11_decoder_proxy_open (decoder,
          cdata, &inner->device, &inner->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  /* XXX: ConfigDecoderSpecific bit 12 indicates whether accelerator can
   * support non-keyframe format change or not, but it doesn't seem to be
   * reliable, since 1b means that it's supported and 0b indicates it may not be
   * supported. Because some GPUs can support it even if the bit 12 is not
   * set, do filtering by vendor for now (AMD and Intel looks fine) */
  if (gst_d3d11_get_device_vendor (inner->device) ==
      GST_D3D11_DEVICE_VENDOR_NVIDIA) {
    gst_vp9_decoder_set_non_keyframe_format_change_support (vp9dec, FALSE);
  }

  return TRUE;
}

static gboolean
gst_d3d11_vp9_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

  gst_clear_object (&inner->d3d11_decoder);
  gst_clear_object (&inner->device);

  return TRUE;
}

static gboolean
gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_negotiate (inner->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_decide_allocation (inner->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_vp9_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

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
gst_d3d11_vp9_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

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
gst_d3d11_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHeader * frame_hdr, gint max_dpb_size)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  GstVideoInfo info;
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;

  GST_LOG_OBJECT (self, "new sequence");

  if (frame_hdr->profile == GST_VP9_PROFILE_0)
    out_format = GST_VIDEO_FORMAT_NV12;
  else if (frame_hdr->profile == GST_VP9_PROFILE_2)
    out_format = GST_VIDEO_FORMAT_P010_10LE;

  if (out_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Could not support profile %d", frame_hdr->profile);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  gst_video_info_set_format (&info,
      out_format, frame_hdr->width, frame_hdr->height);

  if (!gst_d3d11_decoder_configure (inner->d3d11_decoder,
          decoder->input_state, &info, 0, 0, (gint) frame_hdr->width,
          (gint) frame_hdr->height, max_dpb_size)) {
    GST_ERROR_OBJECT (self, "Failed to create decoder");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
    GST_WARNING_OBJECT (self, "Failed to negotiate with downstream");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  /* Will be updated per decode_picture */
  inner->last_frame_width = inner->last_frame_height = 0;
  inner->last_show_frame = FALSE;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (inner->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return GST_FLOW_FLUSHING;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_vp9_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New VP9 picture %p", picture);

  return GST_FLOW_OK;
}

static GstVp9Picture *
gst_d3d11_vp9_dec_duplicate_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstBuffer *view_buffer;
  GstVp9Picture *new_picture;

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Parent picture does not have output view buffer");
    return NULL;
  }

  new_picture = gst_vp9_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;

  GST_LOG_OBJECT (self, "Duplicate output with buffer %" GST_PTR_FORMAT,
      view_buffer);

  gst_vp9_picture_set_user_data (new_picture,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return new_picture;
}

static GstFlowReturn
gst_d3d11_vp9_dec_start_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;

  inner->bitstream_buffer.resize (0);

  return GST_FLOW_OK;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_vp9_dec_get_output_view_from_picture (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, guint8 * view_id)
{
  GstD3D11Vp9DecInner *inner = self->inner;
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (inner->d3d11_decoder,
      view_buffer, view_id);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static void
gst_d3d11_vp9_dec_copy_frame_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;

  params->profile = frame_hdr->profile;
  params->frame_type = frame_hdr->frame_type;
  params->show_frame = frame_hdr->show_frame;
  params->error_resilient_mode = frame_hdr->error_resilient_mode;
  params->subsampling_x = frame_hdr->subsampling_x;
  params->subsampling_y = frame_hdr->subsampling_y;
  params->refresh_frame_context = frame_hdr->refresh_frame_context;
  params->frame_parallel_decoding_mode =
      frame_hdr->frame_parallel_decoding_mode;
  params->intra_only = frame_hdr->intra_only;
  params->frame_context_idx = frame_hdr->frame_context_idx;
  params->reset_frame_context = frame_hdr->reset_frame_context;
  if (frame_hdr->frame_type == GST_VP9_KEY_FRAME)
    params->allow_high_precision_mv = 0;
  else
    params->allow_high_precision_mv = frame_hdr->allow_high_precision_mv;

  params->width = frame_hdr->width;
  params->height = frame_hdr->height;
  params->BitDepthMinus8Luma = frame_hdr->bit_depth - 8;
  params->BitDepthMinus8Chroma = frame_hdr->bit_depth - 8;

  params->interp_filter = frame_hdr->interpolation_filter;
  params->log2_tile_cols = frame_hdr->tile_cols_log2;
  params->log2_tile_rows = frame_hdr->tile_rows_log2;
}

static void
gst_d3d11_vp9_dec_copy_reference_frames (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, GstVp9Dpb * dpb, DXVA_PicParams_VP9 * params)
{
  gint i;

  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    if (dpb->pic_list[i]) {
      GstVp9Picture *other_pic = dpb->pic_list[i];
      ID3D11VideoDecoderOutputView *view;
      guint8 view_id = 0xff;

      view = gst_d3d11_vp9_dec_get_output_view_from_picture (self, other_pic,
          &view_id);
      if (!view) {
        GST_ERROR_OBJECT (self, "picture does not have output view handle");
        return;
      }

      params->ref_frame_map[i].Index7Bits = view_id;
      params->ref_frame_coded_width[i] = picture->frame_hdr.width;
      params->ref_frame_coded_height[i] = picture->frame_hdr.height;
    } else {
      params->ref_frame_map[i].bPicEntry = 0xff;
      params->ref_frame_coded_width[i] = 0;
      params->ref_frame_coded_height[i] = 0;
    }
  }
}

static void
gst_d3d11_vp9_dec_copy_frame_refs (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  gint i;

  for (i = 0; i < GST_VP9_REFS_PER_FRAME; i++) {
    params->frame_refs[i] = params->ref_frame_map[frame_hdr->ref_frame_idx[i]];
  }

  G_STATIC_ASSERT (G_N_ELEMENTS (params->ref_frame_sign_bias) ==
      G_N_ELEMENTS (frame_hdr->ref_frame_sign_bias));
  G_STATIC_ASSERT (sizeof (params->ref_frame_sign_bias) ==
      sizeof (frame_hdr->ref_frame_sign_bias));
  memcpy (params->ref_frame_sign_bias,
      frame_hdr->ref_frame_sign_bias, sizeof (frame_hdr->ref_frame_sign_bias));
}

static void
gst_d3d11_vp9_dec_copy_loop_filter_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  GstD3D11Vp9DecInner *inner = self->inner;
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  const GstVp9LoopFilterParams *lfp = &frame_hdr->loop_filter_params;

  params->filter_level = lfp->loop_filter_level;
  params->sharpness_level = lfp->loop_filter_sharpness;
  params->mode_ref_delta_enabled = lfp->loop_filter_delta_enabled;
  params->mode_ref_delta_update = lfp->loop_filter_delta_update;
  params->use_prev_in_find_mv_refs =
      inner->last_show_frame && !frame_hdr->error_resilient_mode;

  if (frame_hdr->frame_type != GST_VP9_KEY_FRAME && !frame_hdr->intra_only) {
    params->use_prev_in_find_mv_refs &=
        (frame_hdr->width == inner->last_frame_width &&
        frame_hdr->height == inner->last_frame_height);
  }

  G_STATIC_ASSERT (G_N_ELEMENTS (params->ref_deltas) ==
      G_N_ELEMENTS (lfp->loop_filter_ref_deltas));
  G_STATIC_ASSERT (sizeof (params->ref_deltas) ==
      sizeof (lfp->loop_filter_ref_deltas));
  memcpy (params->ref_deltas, lfp->loop_filter_ref_deltas,
      sizeof (lfp->loop_filter_ref_deltas));

  G_STATIC_ASSERT (G_N_ELEMENTS (params->mode_deltas) ==
      G_N_ELEMENTS (lfp->loop_filter_mode_deltas));
  G_STATIC_ASSERT (sizeof (params->mode_deltas) ==
      sizeof (lfp->loop_filter_mode_deltas));
  memcpy (params->mode_deltas, lfp->loop_filter_mode_deltas,
      sizeof (lfp->loop_filter_mode_deltas));
}

static void
gst_d3d11_vp9_dec_copy_quant_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  const GstVp9QuantizationParams *qp = &frame_hdr->quantization_params;

  params->base_qindex = qp->base_q_idx;
  params->y_dc_delta_q = qp->delta_q_y_dc;
  params->uv_dc_delta_q = qp->delta_q_uv_dc;
  params->uv_ac_delta_q = qp->delta_q_uv_ac;
}

static void
gst_d3d11_vp9_dec_copy_segmentation_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  const GstVp9SegmentationParams *sp = &frame_hdr->segmentation_params;
  gint i, j;

  params->stVP9Segments.enabled = sp->segmentation_enabled;
  params->stVP9Segments.update_map = sp->segmentation_update_map;
  params->stVP9Segments.temporal_update = sp->segmentation_temporal_update;
  params->stVP9Segments.abs_delta = sp->segmentation_abs_or_delta_update;

  G_STATIC_ASSERT (G_N_ELEMENTS (params->stVP9Segments.tree_probs) ==
      G_N_ELEMENTS (sp->segmentation_tree_probs));
  G_STATIC_ASSERT (sizeof (params->stVP9Segments.tree_probs) ==
      sizeof (sp->segmentation_tree_probs));
  memcpy (params->stVP9Segments.tree_probs, sp->segmentation_tree_probs,
      sizeof (sp->segmentation_tree_probs));

  G_STATIC_ASSERT (G_N_ELEMENTS (params->stVP9Segments.pred_probs) ==
      G_N_ELEMENTS (sp->segmentation_pred_prob));
  G_STATIC_ASSERT (sizeof (params->stVP9Segments.pred_probs) ==
      sizeof (sp->segmentation_pred_prob));

  if (sp->segmentation_temporal_update) {
    memcpy (params->stVP9Segments.pred_probs, sp->segmentation_pred_prob,
        sizeof (params->stVP9Segments.pred_probs));
  } else {
    memset (params->stVP9Segments.pred_probs, 255,
        sizeof (params->stVP9Segments.pred_probs));
  }

  for (i = 0; i < GST_VP9_MAX_SEGMENTS; i++) {
    params->stVP9Segments.feature_mask[i] =
        (sp->feature_enabled[i][GST_VP9_SEG_LVL_ALT_Q] << 0) |
        (sp->feature_enabled[i][GST_VP9_SEG_LVL_ALT_L] << 1) |
        (sp->feature_enabled[i][GST_VP9_SEG_LVL_REF_FRAME] << 2) |
        (sp->feature_enabled[i][GST_VP9_SEG_SEG_LVL_SKIP] << 3);

    for (j = 0; j < 3; j++)
      params->stVP9Segments.feature_data[i][j] = sp->feature_data[i][j];
    params->stVP9Segments.feature_data[i][3] = 0;
  }
}

static GstFlowReturn
gst_d3d11_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  DXVA_PicParams_VP9 *pic_params = &inner->pic_params;
  DXVA_Slice_VPx_Short *slice = &inner->slice;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;

  view = gst_d3d11_vp9_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (pic_params, 0, sizeof (DXVA_PicParams_VP9));

  pic_params->CurrPic.Index7Bits = view_id;
  pic_params->uncompressed_header_size_byte_aligned =
      picture->frame_hdr.frame_header_length_in_bytes;
  pic_params->first_partition_size = picture->frame_hdr.header_size_in_bytes;
  pic_params->StatusReportFeedbackNumber = 1;

  gst_d3d11_vp9_dec_copy_frame_params (self, picture, pic_params);
  gst_d3d11_vp9_dec_copy_reference_frames (self, picture, dpb, pic_params);
  gst_d3d11_vp9_dec_copy_frame_refs (self, picture, pic_params);
  gst_d3d11_vp9_dec_copy_loop_filter_params (self, picture, pic_params);
  gst_d3d11_vp9_dec_copy_quant_params (self, picture, pic_params);
  gst_d3d11_vp9_dec_copy_segmentation_params (self, picture, pic_params);

  inner->bitstream_buffer.resize (picture->size);
  memcpy (&inner->bitstream_buffer[0], picture->data, picture->size);

  slice->BSNALunitDataLocation = 0;
  slice->SliceBytesInBuffer = inner->bitstream_buffer.size ();
  slice->wBadSliceChopping = 0;

  inner->last_frame_width = pic_params->width;
  inner->last_frame_height = pic_params->height;
  inner->last_show_frame = pic_params->show_frame;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_vp9_dec_end_picture (GstVp9Decoder * decoder, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  size_t bitstream_buffer_size;
  size_t bitstream_pos;
  GstD3D11DecodeInputStreamArgs input_args;

  if (inner->bitstream_buffer.empty ()) {
    GST_ERROR_OBJECT (self, "No bitstream buffer to submit");
    return GST_FLOW_ERROR;
  }

  view = gst_d3d11_vp9_dec_get_output_view_from_picture (self,
      picture, &view_id);
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

    inner->slice.SliceBytesInBuffer += padding;
  }

  input_args.picture_params = &inner->pic_params;
  input_args.picture_params_size = sizeof (DXVA_PicParams_VP9);
  input_args.slice_control = &inner->slice;
  input_args.slice_control_size = sizeof (DXVA_Slice_VPx_Short);
  input_args.bitstream = &inner->bitstream_buffer[0];
  input_args.bitstream_size = inner->bitstream_buffer.size ();

  return gst_d3d11_decoder_decode_frame (inner->d3d11_decoder,
      view, &input_args);
}

static GstFlowReturn
gst_d3d11_vp9_dec_output_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecInner *inner = self->inner;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!gst_d3d11_decoder_process_output (inner->d3d11_decoder, vdec,
          picture->discont_state, picture->frame_hdr.width,
          picture->frame_hdr.height, view_buffer, &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  gst_vp9_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_vp9_picture_unref (picture);
  gst_video_decoder_release_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

void
gst_d3d11_vp9_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  const GUID *profile;
  GTypeInfo type_info = {
    sizeof (GstD3D11Vp9DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_vp9_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11Vp9Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_vp9_dec_init,
  };
  const GUID *profile2_guid = NULL;
  const GUID *profile0_guid = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  GstCaps *d3d11_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  gboolean have_profile2 = FALSE;
  gboolean have_profile0 = FALSE;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

  have_profile2 = gst_d3d11_decoder_get_supported_decoder_profile (device,
      GST_DXVA_CODEC_VP9, GST_VIDEO_FORMAT_P010_10LE, &profile2_guid);
  if (!have_profile2) {
    GST_DEBUG_OBJECT (device,
        "decoder does not support VP9_VLD_10BIT_PROFILE2");
  } else {
    have_profile2 &=
        gst_d3d11_decoder_supports_format (device,
        profile2_guid, DXGI_FORMAT_P010);
    if (!have_profile2) {
      GST_FIXME_OBJECT (device, "device does not support P010 format");
    }
  }

  have_profile0 = gst_d3d11_decoder_get_supported_decoder_profile (device,
      GST_DXVA_CODEC_VP9, GST_VIDEO_FORMAT_NV12, &profile0_guid);
  if (!have_profile0) {
    GST_DEBUG_OBJECT (device, "decoder does not support VP9_VLD_PROFILE0");
  } else {
    have_profile0 =
        gst_d3d11_decoder_supports_format (device, profile0_guid,
        DXGI_FORMAT_NV12);
    if (!have_profile0) {
      GST_FIXME_OBJECT (device, "device does not support NV12 format");
    }
  }

  if (!have_profile2 && !have_profile0) {
    GST_INFO_OBJECT (device, "device does not support VP9 decoding");
    return;
  }

  if (have_profile0) {
    profile = profile0_guid;
    format = DXGI_FORMAT_NV12;
  } else {
    profile = profile2_guid;
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

  if (have_profile0 && have_profile2) {
    sink_caps = gst_caps_from_string ("video/x-vp9, "
        "alignment = (string) frame, profile = (string) 0; "
        "video/x-vp9, alignment = (string) frame, profile = (string) 2, "
        "bit-depth-luma = (uint) 10, bit-depth-chroma = (uint) 10");
    src_caps = gst_caps_from_string ("video/x-raw, "
        "format = (string) { NV12, P010_10LE }");
  } else if (have_profile0) {
    sink_caps = gst_caps_from_string ("video/x-vp9, "
        "alignment = (string) frame, profile = (string) 0");
    src_caps = gst_caps_from_string ("video/x-raw, " "format = (string) NV12");
  } else if (have_profile2) {
    sink_caps = gst_caps_from_string ("video/x-vp9, "
        "alignment = (string) frame, profile = (string) 2, "
        "bit-depth-luma = (uint) 10, bit-depth-chroma = (uint) 10");
    src_caps = gst_caps_from_string ("video/x-raw, "
        "format = (string) P010_10LE");
  } else {
    g_assert_not_reached ();
    return;
  }

  d3d11_caps = gst_caps_copy (src_caps);
  gst_caps_set_features_simple (d3d11_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
  src_caps = gst_caps_merge (d3d11_caps, src_caps);

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, GST_DXVA_CODEC_VP9,
      sink_caps, src_caps, resolution);

  type_name = g_strdup ("GstD3D11Vp9Dec");
  feature_name = g_strdup ("d3d11vp9dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11Vp9Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11vp9device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_VP9_DECODER,
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
