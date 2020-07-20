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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11h264dec.h"
#include "gstd3d11memory.h"
#include "gstd3d11bufferpool.h"

#include <gst/codecs/gsth264decoder.h>
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_h264_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_h264_dec_debug

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_H264_IDCT_FGT, 0x1b81be67, 0xa0c7,
    0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_NOFGT, 0x1b81be68, 0xa0c7,
    0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_FGT, 0x1b81be69, 0xa0c7,
    0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

typedef struct _GstD3D11H264Dec
{
  GstH264Decoder parent;

  GstVideoCodecState *output_state;

  GstD3D11Device *device;

  guint width, height;
  guint coded_width, coded_height;
  guint bitdepth;
  guint chroma_format_idc;
  GstVideoFormat out_format;

  /* Array of DXVA_Slice_H264_Short */
  GArray *slice_list;

  GstD3D11Decoder *d3d11_decoder;

  /* Pointing current bitstream buffer */
  gboolean bad_aligned_bitstream_buffer;
  guint written_buffer_size;
  guint remaining_buffer_size;
  guint8 *bitstream_buffer_data;

  gboolean use_d3d11_output;

  DXVA_PicEntry_H264 ref_frame_list[16];
  INT field_order_cnt_list[16][2];
  USHORT frame_num_list[16];
  UINT used_for_reference_flags;
  USHORT non_existing_frame_flags;
} GstD3D11H264Dec;

typedef struct _GstD3D11H264DecClass
{
  GstH264DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11H264DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_H264_DEC(object) ((GstD3D11H264Dec *) (object))
#define GST_D3D11_H264_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11H264DecClass))

static void gst_d3d11_h264_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_h264_dec_dispose (GObject * object);
static void gst_d3d11_h264_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_h264_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_h264_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_h264_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstH264Decoder */
static gboolean gst_d3d11_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size);
static gboolean gst_d3d11_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture);
static GstFlowReturn gst_d3d11_h264_dec_output_picture (GstH264Decoder *
    decoder, GstVideoCodecFrame * frame, GstH264Picture * picture);
static gboolean gst_d3d11_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb);
static gboolean gst_d3d11_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1);
static gboolean gst_d3d11_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture);

static void
gst_d3d11_h264_dec_class_init (GstD3D11H264DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_h264_dec_get_property;
  gobject_class->dispose = gst_d3d11_h264_dec_dispose;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, cdata->adapter,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  parent_class = g_type_class_peek_parent (klass);

  klass->adapter = cdata->adapter;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_set_context);

  long_name = g_strdup_printf ("Direct3D11 H.264 %s Decoder",
      cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based H.264 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_src_query);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_new_sequence);
  h264decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_new_picture);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_output_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_start_picture);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_decode_slice);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_h264_dec_end_picture);
}

static void
gst_d3d11_h264_dec_init (GstD3D11H264Dec * self)
{
  self->slice_list = g_array_new (FALSE, TRUE, sizeof (DXVA_Slice_H264_Short));
}

static void
gst_d3d11_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_uint (value, klass->adapter);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, klass->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, klass->vendor_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_h264_dec_dispose (GObject * object)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (object);

  if (self->slice_list) {
    g_array_unref (self->slice_list);
    self->slice_list = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_h264_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (element);
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_h264_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11H264DecClass *klass = GST_D3D11_H264_DEC_GET_CLASS (self);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), klass->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11device");
    return FALSE;
  }

  self->d3d11_decoder = gst_d3d11_decoder_new (self->device);

  if (!self->d3d11_decoder) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11 decoder");
    gst_clear_object (&self->device);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_h264_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);

  if (!gst_d3d11_decoder_negotiate (decoder, h264dec->input_state,
          self->out_format, self->width, self->height, &self->output_state,
          &self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_h264_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (decoder, query, self->device,
          GST_D3D11_CODEC_H264, self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_h264_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  gint crop_width, crop_height;
  gboolean modified = FALSE;
  static const GUID *supported_profiles[] = {
    &GST_GUID_D3D11_DECODER_PROFILE_H264_IDCT_FGT,
    &GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_NOFGT,
    &GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_FGT
  };

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

  if (modified || !self->d3d11_decoder->opened) {
    GstVideoInfo info;

    self->out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->bitdepth == 8) {
      if (self->chroma_format_idc == 1)
        self->out_format = GST_VIDEO_FORMAT_NV12;
      else {
        GST_FIXME_OBJECT (self, "Could not support 8bits non-4:2:0 format");
      }
    } else if (self->bitdepth == 10) {
      if (self->chroma_format_idc == 1)
        self->out_format = GST_VIDEO_FORMAT_P010_10LE;
      else {
        GST_FIXME_OBJECT (self, "Could not support 10bits non-4:2:0 format");
      }
    }

    if (self->out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return FALSE;
    }

    gst_video_info_set_format (&info,
        self->out_format, self->width, self->height);

    gst_d3d11_decoder_reset (self->d3d11_decoder);
    if (!gst_d3d11_decoder_open (self->d3d11_decoder, GST_D3D11_CODEC_H264,
            &info, self->coded_width, self->coded_height,
            /* Additional 4 views margin for zero-copy rendering */
            max_dpb_size + 4,
            supported_profiles, G_N_ELEMENTS (supported_profiles))) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return FALSE;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_d3d11_h264_dec_get_bitstream_buffer (GstD3D11H264Dec * self)
{
  GST_TRACE_OBJECT (self, "Getting bitstream buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &self->remaining_buffer_size,
          (gpointer *) & self->bitstream_buffer_data)) {
    GST_ERROR_OBJECT (self, "Faild to get bitstream buffer");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Got bitstream buffer %p with size %d",
      self->bitstream_buffer_data, self->remaining_buffer_size);
  self->written_buffer_size = 0;
  if ((self->remaining_buffer_size & 127) != 0) {
    GST_WARNING_OBJECT (self,
        "The size of bitstream buffer is not 128 bytes aligned");
    self->bad_aligned_bitstream_buffer = TRUE;
  } else {
    self->bad_aligned_bitstream_buffer = FALSE;
  }

  return TRUE;
}

static GstD3D11DecoderOutputView *
gst_d3d11_h264_dec_get_output_view_from_picture (GstD3D11H264Dec * self,
    GstH264Picture * picture)
{
  GstBuffer *view_buffer;
  GstD3D11DecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_h264_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view = gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static gboolean
gst_d3d11_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstD3D11DecoderOutputView *view;
  gint i;
  GArray *dpb_array;

  view = gst_d3d11_h264_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");

  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  for (i = 0; i < 16; i++) {
    self->ref_frame_list[i].bPicEntry = 0xFF;
    self->field_order_cnt_list[i][0] = 0;
    self->field_order_cnt_list[i][1] = 0;
    self->frame_num_list[i] = 0;
  }
  self->used_for_reference_flags = 0;
  self->non_existing_frame_flags = 0;

  dpb_array = gst_h264_dpb_get_pictures_all (dpb);

  for (i = 0; i < dpb_array->len; i++) {
    guint ref = 3;
    GstH264Picture *other = g_array_index (dpb_array, GstH264Picture *, i);
    GstD3D11DecoderOutputView *other_view;
    gint id = 0xff;

    if (!other->ref)
      continue;

    other_view = gst_d3d11_h264_dec_get_output_view_from_picture (self, other);

    if (other_view)
      id = other_view->view_id;

    self->ref_frame_list[i].Index7Bits = id;
    self->ref_frame_list[i].AssociatedFlag = other->long_term;
    self->field_order_cnt_list[i][0] = other->top_field_order_cnt;
    self->field_order_cnt_list[i][1] = other->bottom_field_order_cnt;
    self->frame_num_list[i] = self->ref_frame_list[i].AssociatedFlag
        ? other->long_term_pic_num : other->frame_num;
    self->used_for_reference_flags |= ref << (2 * i);
    self->non_existing_frame_flags |= (other->nonexisting) << i;
  }

  g_array_unref (dpb_array);
  g_array_set_size (self->slice_list, 0);

  return gst_d3d11_h264_dec_get_bitstream_buffer (self);
}

static gboolean
gst_d3d11_h264_dec_new_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstBuffer *view_buffer;
  GstD3D11Memory *mem;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (self->d3d11_decoder);
  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "No available output view buffer");
    return FALSE;
  }

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (view_buffer, 0);

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT " (index %d)",
      view_buffer, mem->subresource_index);

  gst_h264_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New h264picture %p", picture);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *output_buffer = NULL;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  view_buffer = (GstBuffer *) gst_h264_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  /* if downstream is d3d11 element and forward playback case,
   * expose our decoder view without copy. In case of reverse playback, however,
   * we cannot do that since baseclass will store the decoded buffer
   * up to gop size but our dpb pool cannot be increased */
  if (self->use_d3d11_output &&
      gst_d3d11_decoder_supports_direct_rendering (self->d3d11_decoder) &&
      GST_VIDEO_DECODER (self)->input_segment.rate > 0) {
    GstMemory *mem;

    output_buffer = gst_buffer_ref (view_buffer);
    mem = gst_buffer_peek_memory (output_buffer, 0);
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  } else {
    output_buffer = gst_video_decoder_allocate_output_buffer (vdec);
  }

  if (!output_buffer) {
    GST_ERROR_OBJECT (self, "Couldn't allocate output buffer");
    goto error;
  }

  frame->output_buffer = output_buffer;
  GST_BUFFER_PTS (output_buffer) = GST_BUFFER_PTS (frame->input_buffer);
  GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (output_buffer) =
      GST_BUFFER_DURATION (frame->input_buffer);

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder,
          &self->output_state->info,
          GST_VIDEO_INFO_WIDTH (&self->output_state->info),
          GST_VIDEO_INFO_HEIGHT (&self->output_state->info),
          view_buffer, output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (output_buffer)));

  gst_h264_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_h264_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static gboolean
gst_d3d11_h264_dec_submit_slice_data (GstD3D11H264Dec * self)
{
  guint buffer_size;
  gpointer buffer;
  guint8 *data;
  gsize offset = 0;
  gint i;
  D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[4] = { 0, };
  gboolean ret;
  DXVA_Slice_H264_Short *slice_data;

  if (self->slice_list->len < 1) {
    GST_WARNING_OBJECT (self, "Nothing to submit");
    return FALSE;
  }

  slice_data = &g_array_index (self->slice_list, DXVA_Slice_H264_Short,
      self->slice_list->len - 1);

  /* DXVA2 spec is saying that written bitstream data must be 128 bytes
   * aligned if the bitstream buffer contains end of slice
   * (i.e., wBadSliceChopping == 0 or 2) */
  if (slice_data->wBadSliceChopping == 0 || slice_data->wBadSliceChopping == 2) {
    guint padding =
        MIN (GST_ROUND_UP_128 (self->written_buffer_size) -
        self->written_buffer_size, self->remaining_buffer_size);

    if (padding) {
      GST_TRACE_OBJECT (self,
          "Written bitstream buffer size %u is not 128 bytes aligned, "
          "add padding %u bytes", self->written_buffer_size, padding);
      memset (self->bitstream_buffer_data, 0, padding);
      self->written_buffer_size += padding;
      slice_data->SliceBytesInBuffer += padding;
    }
  }

  GST_TRACE_OBJECT (self, "Getting slice control buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &buffer_size, &buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");
    return FALSE;
  }

  data = buffer;
  for (i = 0; i < self->slice_list->len; i++) {
    DXVA_Slice_H264_Short *slice_data =
        &g_array_index (self->slice_list, DXVA_Slice_H264_Short, i);

    memcpy (data + offset, slice_data, sizeof (DXVA_Slice_H264_Short));
    offset += sizeof (DXVA_Slice_H264_Short);
  }

  GST_TRACE_OBJECT (self, "Release slice control buffer");
  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL)) {
    GST_ERROR_OBJECT (self, "Failed to release slice control buffer");
    return FALSE;
  }

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
    GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");
    return FALSE;
  }

  buffer_desc[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
  buffer_desc[0].DataOffset = 0;
  buffer_desc[0].DataSize = sizeof (DXVA_PicParams_H264);

  buffer_desc[1].BufferType =
      D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
  buffer_desc[1].DataOffset = 0;
  buffer_desc[1].DataSize = sizeof (DXVA_Qmatrix_H264);

  buffer_desc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
  buffer_desc[2].DataOffset = 0;
  buffer_desc[2].DataSize =
      sizeof (DXVA_Slice_H264_Short) * self->slice_list->len;

  if (!self->bad_aligned_bitstream_buffer
      && (self->written_buffer_size & 127) != 0) {
    GST_WARNING_OBJECT (self,
        "Written bitstream buffer size %u is not 128 bytes aligned",
        self->written_buffer_size);
  }

  buffer_desc[3].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
  buffer_desc[3].DataOffset = 0;
  buffer_desc[3].DataSize = self->written_buffer_size;

  ret = gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
      4, buffer_desc);

  self->written_buffer_size = 0;
  self->bitstream_buffer_data = NULL;
  self->remaining_buffer_size = 0;
  g_array_set_size (self->slice_list, 0);

  return ret;
}

static gboolean
gst_d3d11_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);

  GST_LOG_OBJECT (self, "end picture %p, (poc %d)",
      picture, picture->pic_order_cnt);

  if (!gst_d3d11_h264_dec_submit_slice_data (self)) {
    GST_ERROR_OBJECT (self, "Failed to submit slice data");
    return FALSE;
  }

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

static void
gst_d3d11_h264_dec_picture_params_from_sps (GstD3D11H264Dec * self,
    const GstH264SPS * sps, gboolean field_pic, DXVA_PicParams_H264 * params)
{
#define COPY_FIELD(f) \
  (params)->f = (sps)->f

  params->wFrameWidthInMbsMinus1 = sps->pic_width_in_mbs_minus1;
  params->wFrameHeightInMbsMinus1 = sps->pic_height_in_map_units_minus1;
  params->residual_colour_transform_flag = sps->separate_colour_plane_flag;
  params->MbaffFrameFlag = sps->mb_adaptive_frame_field_flag && field_pic;
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

  memset (params, 0, sizeof (DXVA_PicParams_H264));

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

static gboolean
gst_d3d11_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0,
    GArray * ref_pic_list1)
{
  GstD3D11H264Dec *self = GST_D3D11_H264_DEC (decoder);
  GstH264SPS *sps;
  GstH264PPS *pps;
  DXVA_PicParams_H264 pic_params = { 0, };
  DXVA_Qmatrix_H264 iq_matrix = { 0, };
  guint d3d11_buffer_size = 0;
  gpointer d3d11_buffer = NULL;
  gint i, j;
  GstD3D11DecoderOutputView *view;

  pps = slice->header.pps;
  sps = pps->sequence;

  view = gst_d3d11_h264_dec_get_output_view_from_picture (self, picture);

  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view");
    return FALSE;
  }

  gst_d3d11_h264_dec_fill_picture_params (self, &slice->header, &pic_params);

  pic_params.CurrPic.Index7Bits = view->view_id;
  pic_params.RefPicFlag = picture->ref;
  pic_params.frame_num = picture->frame_num;

  if (pic_params.field_pic_flag && pic_params.CurrPic.AssociatedFlag) {
    pic_params.CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
    pic_params.CurrFieldOrderCnt[0] = 0;
  } else if (pic_params.field_pic_flag && !pic_params.CurrPic.AssociatedFlag) {
    pic_params.CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    pic_params.CurrFieldOrderCnt[1] = 0;
  } else {
    pic_params.CurrFieldOrderCnt[0] = picture->top_field_order_cnt;
    pic_params.CurrFieldOrderCnt[1] = picture->bottom_field_order_cnt;
  }

  memcpy (pic_params.RefFrameList, self->ref_frame_list,
      sizeof (pic_params.RefFrameList));
  memcpy (pic_params.FieldOrderCntList, self->field_order_cnt_list,
      sizeof (pic_params.FieldOrderCntList));
  memcpy (pic_params.FrameNumList, self->frame_num_list,
      sizeof (pic_params.FrameNumList));

  pic_params.UsedForReferenceFlags = self->used_for_reference_flags;
  pic_params.NonExistingFrameFlags = self->non_existing_frame_flags;

  GST_TRACE_OBJECT (self, "Getting picture param decoder buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for picture parameters");
    return FALSE;
  }

  memcpy (d3d11_buffer, &pic_params, sizeof (DXVA_PicParams_H264));

  GST_TRACE_OBJECT (self, "Release picture param decoder buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  if (pps->pic_scaling_matrix_present_flag) {
    for (i = 0; i < 6; i++) {
      for (j = 0; j < 16; j++) {
        iq_matrix.bScalingLists4x4[i][j] = pps->scaling_lists_4x4[i][j];
      }
    }

    for (i = 0; i < 2; i++) {
      for (j = 0; j < 64; j++) {
        iq_matrix.bScalingLists8x8[i][j] = pps->scaling_lists_8x8[i][j];
      }
    }
  } else {
    for (i = 0; i < 6; i++) {
      for (j = 0; j < 16; j++) {
        iq_matrix.bScalingLists4x4[i][j] = sps->scaling_lists_4x4[i][j];
      }
    }

    for (i = 0; i < 2; i++) {
      for (j = 0; j < 64; j++) {
        iq_matrix.bScalingLists8x8[i][j] = sps->scaling_lists_8x8[i][j];
      }
    }
  }

  GST_TRACE_OBJECT (self, "Getting inverse quantization maxtirx buffer");

  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX,
          &d3d11_buffer_size, &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for inv. quantization matrix");
    return FALSE;
  }

  memcpy (d3d11_buffer, &iq_matrix, sizeof (DXVA_Qmatrix_H264));

  GST_TRACE_OBJECT (self, "Release inverse quantization maxtirx buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  {
    guint to_write = slice->nalu.size + 3;
    gboolean is_first = TRUE;

    while (to_write > 0) {
      guint bytes_to_copy;
      gboolean is_last = TRUE;
      DXVA_Slice_H264_Short slice_short = { 0, };

      if (self->remaining_buffer_size < to_write && self->slice_list->len > 0) {
        if (!gst_d3d11_h264_dec_submit_slice_data (self)) {
          GST_ERROR_OBJECT (self, "Failed to submit bitstream buffers");
          return FALSE;
        }

        if (!gst_d3d11_h264_dec_get_bitstream_buffer (self)) {
          GST_ERROR_OBJECT (self, "Failed to get bitstream buffer");
          return FALSE;
        }
      }

      /* remaining_buffer_size: the size of remaining d3d11 decoder
       *                        bitstream memory allowed to write more
       * written_buffer_size: the size of written bytes to this d3d11 decoder
       *                      bitstream memory
       * bytes_to_copy: the size of which we would write to d3d11 decoder
       *                bitstream memory in this loop
       */

      bytes_to_copy = to_write;

      if (bytes_to_copy > self->remaining_buffer_size) {
        /* if the size of this slice is larger than the size of remaining d3d11
         * decoder bitstream memory, write the data up to the remaining d3d11
         * decoder bitstream memory size and the rest would be written to the
         * next d3d11 bitstream memory */
        bytes_to_copy = self->remaining_buffer_size;
        is_last = FALSE;
      }

      if (bytes_to_copy >= 3 && is_first) {
        /* normal case */
        self->bitstream_buffer_data[0] = 0;
        self->bitstream_buffer_data[1] = 0;
        self->bitstream_buffer_data[2] = 1;
        memcpy (self->bitstream_buffer_data + 3,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy - 3);
      } else {
        /* when this nal unit date is splitted into two buffer */
        memcpy (self->bitstream_buffer_data,
            slice->nalu.data + slice->nalu.offset, bytes_to_copy);
      }

      /* For wBadSliceChopping value 0 or 1, BSNALunitDataLocation means
       * the offset of the first start code of this slice in this d3d11
       * memory buffer.
       * 1) If this is the first slice of picture, it should be zero
       *    since we write start code at offset 0 (written size before this
       *    slice also must be zero).
       * 2) If this is not the first slice of picture but this is the first
       *    d3d11 bitstream buffer (meaning that one bitstream buffer contains
       *    multiple slices), then this is the written size of buffer
       *    excluding this loop.
       * And for wBadSliceChopping value 2 or 3, this should be zero by spec */
      if (is_first)
        slice_short.BSNALunitDataLocation = self->written_buffer_size;
      else
        slice_short.BSNALunitDataLocation = 0;
      slice_short.SliceBytesInBuffer = bytes_to_copy;

      /* wBadSliceChopping: (dxva h264 spec.)
       * 0: All bits for the slice are located within the corresponding
       *    bitstream data buffer
       * 1: The bitstream data buffer contains the start of the slice,
       *    but not the entire slice, because the buffer is full
       * 2: The bitstream data buffer contains the end of the slice.
       *    It does not contain the start of the slice, because the start of
       *    the slice was located in the previous bitstream data buffer.
       * 3: The bitstream data buffer does not contain the start of the slice
       *    (because the start of the slice was located in the previous
       *     bitstream data buffer), and it does not contain the end of the slice
       *    (because the current bitstream data buffer is also full).
       */
      if (is_last && is_first) {
        slice_short.wBadSliceChopping = 0;
      } else if (!is_last && is_first) {
        slice_short.wBadSliceChopping = 1;
      } else if (is_last && !is_first) {
        slice_short.wBadSliceChopping = 2;
      } else {
        slice_short.wBadSliceChopping = 3;
      }

      g_array_append_val (self->slice_list, slice_short);
      self->remaining_buffer_size -= bytes_to_copy;
      self->written_buffer_size += bytes_to_copy;
      self->bitstream_buffer_data += bytes_to_copy;
      is_first = FALSE;
      to_write -= bytes_to_copy;
    }
  }

  return TRUE;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11H264DecResolution;

void
gst_d3d11_h264_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank, gboolean legacy)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  gboolean ret;
  GUID profile;
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
  static const GUID *supported_profiles[] = {
    &GST_GUID_D3D11_DECODER_PROFILE_H264_IDCT_FGT,
    &GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_NOFGT,
    &GST_GUID_D3D11_DECODER_PROFILE_H264_VLD_FGT
  };
  /* values were taken from chromium. See supported_profile_helper.cc */
  GstD3D11H264DecResolution resolutions_to_check[] = {
    {1920, 1088}, {2560, 1440}, {3840, 2160}, {4096, 2160},
    {4096, 2304}
  };
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;

  ret = gst_d3d11_decoder_get_supported_decoder_profile (decoder,
      supported_profiles, G_N_ELEMENTS (supported_profiles), &profile);

  if (!ret) {
    GST_WARNING_OBJECT (device, "decoder profile unavailable");
    return;
  }

  ret = gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_NV12);
  if (!ret) {
    GST_FIXME_OBJECT (device, "device does not support NV12 format");
    return;
  }

  /* we will not check the maximum resolution for legacy devices.
   * it might cause crash */
  if (legacy) {
    max_width = resolutions_to_check[0].width;
    max_height = resolutions_to_check[0].height;
  } else {
    for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
      if (gst_d3d11_decoder_supports_resolution (decoder, &profile,
              DXGI_FORMAT_NV12, resolutions_to_check[i].width,
              resolutions_to_check[i].height)) {
        max_width = resolutions_to_check[i].width;
        max_height = resolutions_to_check[i].height;

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
      "profile = (string) { high, main, constrained-baseline, baseline }, "
      "framerate = " GST_VIDEO_FPS_RANGE);
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), format = (string) NV12, "
      "framerate = " GST_VIDEO_FPS_RANGE ";"
      "video/x-raw, format = (string) NV12, "
      "framerate = " GST_VIDEO_FPS_RANGE);

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, sink_caps, src_caps);

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
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
