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

#include "gstd3d11vp9dec.h"
#include "gstd3d11memory.h"
#include "gstd3d11bufferpool.h"

#include <gst/codecs/gstvp9decoder.h>
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_vp9_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_vp9_dec_debug

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0,
    0x463707f8, 0xa1d0, 0x4585, 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2,
    0xa4c749ef, 0x6ecf, 0x48aa, 0x84, 0x48, 0x50, 0xa7, 0xa1, 0x16, 0x5f, 0xf7);

/* reference list 8 + 4 margin */
#define NUM_OUTPUT_VIEW 12

typedef struct _GstD3D11Vp9Dec
{
  GstVp9Decoder parent;

  GstVideoCodecState *output_state;

  GstD3D11Device *device;

  GstD3D11Decoder *d3d11_decoder;

  guint width, height;
  GstVP9Profile profile;

  GstVideoFormat out_format;

  gboolean use_d3d11_output;
} GstD3D11Vp9Dec;

typedef struct _GstD3D11Vp9DecClass
{
  GstVp9DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11Vp9DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_VP9_DEC(object) ((GstD3D11Vp9Dec *) (object))
#define GST_D3D11_VP9_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Vp9DecClass))

static void gst_d3d11_vp9_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp9_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_vp9_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstVp9Decoder */
static gboolean gst_d3d11_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHdr * frame_hdr);
static gboolean gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture);
static GstVp9Picture *gst_d3d11_vp9_dec_duplicate_picture (GstVp9Decoder *
    decoder, GstVp9Picture * picture);
static GstFlowReturn gst_d3d11_vp9_dec_output_picture (GstVp9Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp9Picture * picture);
static gboolean gst_d3d11_vp9_dec_start_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);
static gboolean gst_d3d11_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb);
static gboolean gst_d3d11_vp9_dec_end_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);

static void
gst_d3d11_vp9_dec_class_init (GstD3D11Vp9DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp9DecoderClass *vp9decoder_class = GST_VP9_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_vp9_dec_get_property;

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
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_set_context);

  long_name = g_strdup_printf ("Direct3D11 VP9 %s Decoder", cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based VP9 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_src_query);

  vp9decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_new_sequence);
  vp9decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_new_picture);
  vp9decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_duplicate_picture);
  vp9decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_output_picture);
  vp9decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_start_picture);
  vp9decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_decode_picture);
  vp9decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_end_picture);
}

static void
gst_d3d11_vp9_dec_init (GstD3D11Vp9Dec * self)
{
}

static void
gst_d3d11_vp9_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (object);

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
gst_d3d11_vp9_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (element);
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11Vp9DecClass *klass = GST_D3D11_VP9_DEC_GET_CLASS (self);

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
gst_d3d11_vp9_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstVp9Decoder *vp9dec = GST_VP9_DECODER (decoder);

  if (!gst_d3d11_decoder_negotiate (decoder, vp9dec->input_state,
          self->out_format, self->width, self->height, &self->output_state,
          &self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (decoder, query, self->device,
          GST_D3D11_CODEC_VP9, self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_vp9_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

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
gst_d3d11_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHdr * frame_hdr)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  gboolean modified = FALSE;
  static const GUID *profile0_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0;
  static const GUID *profile2_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2;

  GST_LOG_OBJECT (self, "new sequence");

  if (self->width < frame_hdr->width || self->height < frame_hdr->height) {
    self->width = frame_hdr->width;
    self->height = frame_hdr->height;
    GST_INFO_OBJECT (self, "resolution changed %dx%d",
        self->width, self->height);
    modified = TRUE;
  }

  if (self->profile != frame_hdr->profile) {
    self->profile = frame_hdr->profile;
    GST_INFO_OBJECT (self, "profile changed %d", self->profile);
    modified = TRUE;
  }

  if (modified || !self->d3d11_decoder->opened) {
    const GUID *profile_guid = NULL;
    GstVideoInfo info;

    self->out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->profile == GST_VP9_PROFILE_0) {
      self->out_format = GST_VIDEO_FORMAT_NV12;
      profile_guid = profile0_guid;
    } else if (self->profile == GST_VP9_PROFILE_2) {
      self->out_format = GST_VIDEO_FORMAT_P010_10LE;
      profile_guid = profile2_guid;
    }

    if (self->out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support profile %d", self->profile);
      return FALSE;
    }

    gst_video_info_set_format (&info,
        self->out_format, self->width, self->height);

    gst_d3d11_decoder_reset (self->d3d11_decoder);
    if (!gst_d3d11_decoder_open (self->d3d11_decoder, GST_D3D11_CODEC_VP9,
            &info, self->width, self->height,
            NUM_OUTPUT_VIEW, &profile_guid, 1)) {
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
gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
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

  gst_vp9_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New VP9 picture %p", picture);

  return TRUE;
}

static GstVp9Picture *
gst_d3d11_vp9_dec_duplicate_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstBuffer *view_buffer;
  GstD3D11Memory *mem;
  GstVp9Picture *new_picture;

  view_buffer = gst_vp9_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Parent picture does not have output view buffer");
    return NULL;
  }

  new_picture = gst_vp9_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (view_buffer, 0);

  GST_LOG_OBJECT (self, "Duplicate output with buffer %" GST_PTR_FORMAT
      " (index %d)", view_buffer, mem->subresource_index);

  gst_vp9_picture_set_user_data (new_picture,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return new_picture;
}

static GstFlowReturn
gst_d3d11_vp9_dec_output_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *output_buffer = NULL;
  GstFlowReturn ret;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!picture->frame_hdr.show_frame) {
    GST_LOG_OBJECT (self, "Decode only picture %p", picture);
    if (frame) {
      GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);
      gst_vp9_picture_unref (picture);

      return gst_video_decoder_finish_frame (vdec, frame);
    } else {
      /* expected case if we are decoding super frame */
      gst_vp9_picture_unref (picture);

      return GST_FLOW_OK;
    }
  }

  /* if downstream is d3d11 element and forward playback case,
   * expose our decoder view without copy. In case of reverse playback, however,
   * we cannot do that since baseclass will store the decoded buffer
   * up to gop size but our dpb pool cannot be increased */
  if (self->use_d3d11_output &&
      gst_d3d11_decoder_supports_direct_rendering (self->d3d11_decoder) &&
      GST_VIDEO_DECODER (self)->input_segment.rate > 0 &&
      GST_VIDEO_INFO_WIDTH (&self->output_state->info) ==
      picture->frame_hdr.width
      && GST_VIDEO_INFO_HEIGHT (&self->output_state->info) ==
      picture->frame_hdr.height) {
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

  if (!frame) {
    /* this is the case where super frame has multiple displayable
     * (non-decode-only) subframes. Should be rare case but it's possible
     * in theory */
    GST_WARNING_OBJECT (self, "No codec frame for picture %p", picture);

    GST_BUFFER_PTS (output_buffer) = picture->pts;
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) = GST_CLOCK_TIME_NONE;
  } else {
    frame->output_buffer = output_buffer;
    GST_BUFFER_PTS (output_buffer) = GST_BUFFER_PTS (frame->input_buffer);
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) =
        GST_BUFFER_DURATION (frame->input_buffer);
  }

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder,
          &self->output_state->info,
          picture->frame_hdr.width, picture->frame_hdr.height,
          view_buffer, output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (output_buffer)));

  gst_vp9_picture_unref (picture);

  if (frame) {
    ret = gst_video_decoder_finish_frame (vdec, frame);
  } else {
    ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), output_buffer);
  }

  return ret;

error:
  if (frame) {
    /* normal case */
    gst_video_decoder_drop_frame (vdec, frame);
  } else if (output_buffer) {
    /* in case of super frame with multiple displayable subframes */
    gst_buffer_unref (output_buffer);
  }
  gst_vp9_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static GstD3D11DecoderOutputView *
gst_d3d11_vp9_dec_get_output_view_from_picture (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture)
{
  GstBuffer *view_buffer;
  GstD3D11DecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static gboolean
gst_d3d11_vp9_dec_start_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstD3D11DecoderOutputView *view;

  view = gst_d3d11_vp9_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");

  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  return TRUE;
}

static void
gst_d3d11_vp9_dec_copy_frame_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHdr *frame_hdr = &picture->frame_hdr;

  params->profile = frame_hdr->profile;

  params->frame_type = frame_hdr->frame_type;
  params->show_frame = frame_hdr->show_frame;
  params->error_resilient_mode = frame_hdr->error_resilient_mode;
  params->subsampling_x = picture->subsampling_x;
  params->subsampling_y = picture->subsampling_y;
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
  params->BitDepthMinus8Luma = picture->bit_depth - 8;
  params->BitDepthMinus8Chroma = picture->bit_depth - 8;

  params->interp_filter = frame_hdr->mcomp_filter_type;
  params->log2_tile_cols = frame_hdr->log2_tile_columns;
  params->log2_tile_rows = frame_hdr->log2_tile_rows;

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump frame params");
  /* set before calling this function */
  GST_TRACE_OBJECT (self, "\tCurrPic.Index7Bits: %d",
      params->CurrPic.Index7Bits);
  GST_TRACE_OBJECT (self, "\tuncompressed_header_size_byte_aligned: %d",
      params->uncompressed_header_size_byte_aligned);
  GST_TRACE_OBJECT (self, "\tfirst_partition_size: %d",
      params->first_partition_size);

  GST_TRACE_OBJECT (self, "\tprofile: %d", params->profile);
  GST_TRACE_OBJECT (self, "\tframe_type: %d", params->frame_type);
  GST_TRACE_OBJECT (self, "\tshow_frame: %d", params->show_frame);
  GST_TRACE_OBJECT (self, "\terror_resilient_mode: %d",
      params->error_resilient_mode);
  GST_TRACE_OBJECT (self, "\tsubsampling_x: %d", params->subsampling_x);
  GST_TRACE_OBJECT (self, "\tsubsampling_t: %d", params->subsampling_y);
  GST_TRACE_OBJECT (self, "\trefresh_frame_context: %d",
      params->refresh_frame_context);
  GST_TRACE_OBJECT (self, "\tframe_parallel_decoding_mode: %d",
      params->frame_parallel_decoding_mode);
  GST_TRACE_OBJECT (self, "\tintra_only: %d", params->intra_only);
  GST_TRACE_OBJECT (self, "\tframe_context_idx: %d", params->frame_context_idx);
  GST_TRACE_OBJECT (self, "\treset_frame_context: %d",
      params->reset_frame_context);
  GST_TRACE_OBJECT (self, "\tallow_high_precision_mv: %d",
      params->allow_high_precision_mv);
  GST_TRACE_OBJECT (self, "\twidth: %d", params->width);
  GST_TRACE_OBJECT (self, "\theight: %d", params->height);
  GST_TRACE_OBJECT (self, "\tBitDepthMinus8Luma: %d",
      params->BitDepthMinus8Luma);
  GST_TRACE_OBJECT (self, "\tBitDepthMinus8Chroma: %d",
      params->BitDepthMinus8Chroma);
  GST_TRACE_OBJECT (self, "\tinterp_filter: %d", params->interp_filter);
  GST_TRACE_OBJECT (self, "\tlog2_tile_cols: %d", params->log2_tile_cols);
  GST_TRACE_OBJECT (self, "\tlog2_tile_rows: %d", params->log2_tile_rows);
#endif
}

static void
gst_d3d11_vp9_dec_copy_reference_frames (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, GstVp9Dpb * dpb, DXVA_PicParams_VP9 * params)
{
  gint i;

  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    if (dpb->pic_list[i]) {
      GstVp9Picture *other_pic = dpb->pic_list[i];
      GstD3D11DecoderOutputView *view;

      view = gst_d3d11_vp9_dec_get_output_view_from_picture (self, other_pic);
      if (!view) {
        GST_ERROR_OBJECT (self, "picture does not have output view handle");
        return;
      }

      params->ref_frame_map[i].Index7Bits = view->view_id;
      params->ref_frame_coded_width[i] = picture->frame_hdr.width;
      params->ref_frame_coded_height[i] = picture->frame_hdr.height;
    } else {
      params->ref_frame_map[i].bPicEntry = 0xff;
      params->ref_frame_coded_width[i] = 0;
      params->ref_frame_coded_height[i] = 0;
    }
  }

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump reference frames");
  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    GST_TRACE_OBJECT (self, "\t[%d] ref_frame_map.Index7Bits: %d", i,
        params->ref_frame_map[i].Index7Bits);
    GST_TRACE_OBJECT (self, "\t[%d] ref_frame_coded_width: %d", i,
        params->ref_frame_coded_width[i]);
    GST_TRACE_OBJECT (self, "\t[%d] ref_frame_coded_height: %d", i,
        params->ref_frame_coded_height[i]);
  }
#endif
}

static void
gst_d3d11_vp9_dec_copy_frame_refs (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHdr *frame_hdr = &picture->frame_hdr;
  gint i;

  for (i = 0; i < GST_VP9_REFS_PER_FRAME; i++) {
    params->frame_refs[i] =
        params->ref_frame_map[frame_hdr->ref_frame_indices[i]];
  }

  for (i = 0; i < GST_VP9_REFS_PER_FRAME; i++) {
    params->ref_frame_sign_bias[i + 1] = frame_hdr->ref_frame_sign_bias[i];
  }

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump frame refs");
  for (i = 0; i < GST_VP9_REFS_PER_FRAME; i++) {
    GST_TRACE_OBJECT (self, "\t[%d] ref_frame_indices: %d", i,
        frame_hdr->ref_frame_indices[i]);
    GST_TRACE_OBJECT (self, "\t[%d] frame_refs.Index7Bits: %d", i,
        params->frame_refs[i].Index7Bits);
    GST_TRACE_OBJECT (self, "\t[%d] ref_frame_sign_bias: %d", i,
        params->ref_frame_sign_bias[i + 1]);
  }
#endif
}

static void
gst_d3d11_vp9_dec_copy_loop_filter_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHdr *frame_hdr = &picture->frame_hdr;
  const GstVp9LoopFilter *loopfilter = &frame_hdr->loopfilter;
  gint i;

  params->filter_level = loopfilter->filter_level;
  params->sharpness_level = loopfilter->sharpness_level;
  params->mode_ref_delta_enabled = loopfilter->mode_ref_delta_enabled;
  params->mode_ref_delta_update = loopfilter->mode_ref_delta_update;

  for (i = 0; i < GST_VP9_MAX_REF_LF_DELTAS; i++) {
    params->ref_deltas[i] = loopfilter->ref_deltas[i];
  }

  for (i = 0; i < GST_VP9_MAX_MODE_LF_DELTAS; i++) {
    params->mode_deltas[i] = loopfilter->mode_deltas[i];
  }

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump loop filter params");
  GST_TRACE_OBJECT (self, "\tfilter_level: %d", params->filter_level);
  GST_TRACE_OBJECT (self, "\tsharpness_level: %d", params->sharpness_level);
  GST_TRACE_OBJECT (self, "\tmode_ref_delta_enabled: %d",
      params->mode_ref_delta_enabled);
  GST_TRACE_OBJECT (self, "\tmode_ref_delta_update: %d",
      params->mode_ref_delta_update);
  for (i = 0; i < GST_VP9_MAX_REF_LF_DELTAS; i++)
    GST_TRACE_OBJECT (self, "\tref_deltas[%d]: %d", i, params->ref_deltas[i]);
  for (i = 0; i < GST_VP9_MAX_MODE_LF_DELTAS; i++)
    GST_TRACE_OBJECT (self, "\tmode_deltas[%d]: %d", i, params->mode_deltas[i]);
#endif
}

static void
gst_d3d11_vp9_dec_copy_quant_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHdr *frame_hdr = &picture->frame_hdr;
  const GstVp9QuantIndices *quant_indices = &frame_hdr->quant_indices;

  params->base_qindex = quant_indices->y_ac_qi;
  params->y_dc_delta_q = quant_indices->y_dc_delta;
  params->uv_dc_delta_q = quant_indices->uv_dc_delta;
  params->uv_ac_delta_q = quant_indices->uv_ac_delta;

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump quantization params");
  GST_TRACE_OBJECT (self, "\tbase_qindex: %d", params->base_qindex);
  GST_TRACE_OBJECT (self, "\ty_dc_delta_q: %d", params->y_dc_delta_q);
  GST_TRACE_OBJECT (self, "\tuv_dc_delta_q: %d", params->uv_dc_delta_q);
  GST_TRACE_OBJECT (self, "\tuv_ac_delta_q: %d", params->uv_ac_delta_q);
#endif
}

static void
gst_d3d11_vp9_dec_copy_segmentation_params (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  const GstVp9FrameHdr *frame_hdr = &picture->frame_hdr;
  const GstVp9SegmentationInfo *seg = &frame_hdr->segmentation;
  gint i;

  params->stVP9Segments.enabled = seg->enabled;
  params->stVP9Segments.update_map = seg->update_map;
  params->stVP9Segments.temporal_update = seg->temporal_update;
  params->stVP9Segments.abs_delta = seg->abs_delta;

  for (i = 0; i < GST_VP9_SEG_TREE_PROBS; i++)
    params->stVP9Segments.tree_probs[i] = seg->tree_probs[i];

  for (i = 0; i < GST_VP9_PREDICTION_PROBS; i++)
    params->stVP9Segments.pred_probs[i] = seg->pred_probs[i];

  for (i = 0; i < GST_VP9_MAX_SEGMENTS; i++) {
    params->stVP9Segments.feature_mask[i] = 0;

    if (seg->data[i].alternate_quantizer_enabled)
      params->stVP9Segments.feature_mask[i] |= (1 << 0);

    if (seg->data[i].alternate_loop_filter_enabled)
      params->stVP9Segments.feature_mask[i] |= (1 << 1);

    if (seg->data[i].reference_frame_enabled)
      params->stVP9Segments.feature_mask[i] |= (1 << 2);

    if (seg->data[i].reference_skip)
      params->stVP9Segments.feature_mask[i] |= (1 << 3);

    params->stVP9Segments.feature_data[i][0] = seg->data[i].alternate_quantizer;
    params->stVP9Segments.feature_data[i][1] =
        seg->data[i].alternate_loop_filter;
    params->stVP9Segments.feature_data[i][2] = seg->data[i].reference_frame;
    params->stVP9Segments.feature_data[i][3] = 0;
  }

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Dump segmentation params");
  GST_TRACE_OBJECT (self, "\tenabled: %d", params->stVP9Segments.enabled);
  GST_TRACE_OBJECT (self, "\tupdate_map: %d", params->stVP9Segments.update_map);
  GST_TRACE_OBJECT (self, "\ttemporal_update: %d",
      params->stVP9Segments.temporal_update);
  GST_TRACE_OBJECT (self, "\tabs_delta: %d", params->stVP9Segments.abs_delta);
  for (i = 0; i < GST_VP9_SEG_TREE_PROBS; i++)
    GST_TRACE_OBJECT (self, "\ttree_probs[%d]: %d", i,
        params->stVP9Segments.tree_probs[i]);
  for (i = 0; i < GST_VP9_PREDICTION_PROBS; i++)
    GST_TRACE_OBJECT (self, "\tpred_probs[%d]: %d", i,
        params->stVP9Segments.pred_probs[i]);
  for (i = 0; i < GST_VP9_MAX_SEGMENTS; i++) {
    gint j;
    GST_TRACE_OBJECT (self, "\tfeature_mask[%d]: 0x%x", i,
        params->stVP9Segments.feature_mask[i]);
    for (j = 0; j < 4; j++)
      GST_TRACE_OBJECT (self, "\tfeature_data[%d][%d]: %d", i, j,
          params->stVP9Segments.feature_data[i][j]);
  }
#endif
}

static gboolean
gst_d3d11_vp9_dec_submit_picture_data (GstD3D11Vp9Dec * self,
    GstVp9Picture * picture, DXVA_PicParams_VP9 * params)
{
  guint d3d11_buffer_size;
  gpointer d3d11_buffer;
  gsize buffer_offset = 0;
  gboolean is_first = TRUE;

  GST_TRACE_OBJECT (self, "Getting picture params buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for picture parameters");
    return FALSE;
  }

  memcpy (d3d11_buffer, params, sizeof (DXVA_PicParams_VP9));

  GST_TRACE_OBJECT (self, "Release picture param decoder buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  if (!picture->data || !picture->size) {
    GST_ERROR_OBJECT (self, "No data to submit");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Submit total %" G_GSIZE_FORMAT " bytes",
      picture->size);

  while (buffer_offset < picture->size) {
    gsize bytes_to_copy = picture->size - buffer_offset;
    gsize written_buffer_size;
    gboolean is_last = TRUE;
    DXVA_Slice_VPx_Short slice_short = { 0, };
    D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[3] = { 0, };
    gboolean bad_aligned_bitstream_buffer = FALSE;

    GST_TRACE_OBJECT (self, "Getting bitstream buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &d3d11_buffer_size,
            &d3d11_buffer)) {
      GST_ERROR_OBJECT (self, "Couldn't get bitstream buffer");
      goto error;
    }

    if ((d3d11_buffer_size & 127) != 0) {
      GST_WARNING_OBJECT (self,
          "The size of bitstream buffer is not 128 bytes aligned");
      bad_aligned_bitstream_buffer = TRUE;
    }

    if (bytes_to_copy > d3d11_buffer_size) {
      /* if the size of this slice is larger than the size of remaining d3d11
       * decoder bitstream memory, write the data up to the remaining d3d11
       * decoder bitstream memory size and the rest would be written to the
       * next d3d11 bitstream memory */
      bytes_to_copy = d3d11_buffer_size;
      is_last = FALSE;
    }

    memcpy (d3d11_buffer, picture->data + buffer_offset, bytes_to_copy);
    written_buffer_size = bytes_to_copy;

    /* DXVA2 spec is saying that written bitstream data must be 128 bytes
     * aligned if the bitstream buffer contains end of frame
     * (i.e., wBadSliceChopping == 0 or 2) */
    if (is_last) {
      guint padding = MIN (GST_ROUND_UP_128 (bytes_to_copy) - bytes_to_copy,
          d3d11_buffer_size - bytes_to_copy);

      if (padding) {
        GST_TRACE_OBJECT (self,
            "Written bitstream buffer size %" G_GSIZE_FORMAT
            " is not 128 bytes aligned, add padding %d bytes",
            bytes_to_copy, padding);
        memset ((guint8 *) d3d11_buffer + bytes_to_copy, 0, padding);
        written_buffer_size += padding;
      }
    }

    GST_TRACE_OBJECT (self, "Release bitstream buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
      GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");

      goto error;
    }

    slice_short.BSNALunitDataLocation = 0;
    slice_short.SliceBytesInBuffer = (UINT) written_buffer_size;

    /* wBadSliceChopping: (dxva spec.)
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

    GST_TRACE_OBJECT (self, "Getting slice control buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &d3d11_buffer_size,
            &d3d11_buffer)) {
      GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");

      goto error;
    }

    memcpy (d3d11_buffer, &slice_short, sizeof (DXVA_Slice_VPx_Short));

    GST_TRACE_OBJECT (self, "Release slice control buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL)) {
      GST_ERROR_OBJECT (self, "Failed to release slice control buffer");

      goto error;
    }

    buffer_desc[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
    buffer_desc[0].DataOffset = 0;
    buffer_desc[0].DataSize = sizeof (DXVA_PicParams_VP9);

    buffer_desc[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
    buffer_desc[1].DataOffset = 0;
    buffer_desc[1].DataSize = sizeof (DXVA_Slice_VPx_Short);

    if (!bad_aligned_bitstream_buffer && (written_buffer_size & 127) != 0) {
      GST_WARNING_OBJECT (self,
          "Written bitstream buffer size %" G_GSIZE_FORMAT
          " is not 128 bytes aligned", written_buffer_size);
    }

    buffer_desc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
    buffer_desc[2].DataOffset = 0;
    buffer_desc[2].DataSize = written_buffer_size;

    if (!gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
            3, buffer_desc)) {
      GST_ERROR_OBJECT (self, "Couldn't submit decoder buffers");
      goto error;
    }

    buffer_offset += bytes_to_copy;
    is_first = FALSE;
  }

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_d3d11_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  DXVA_PicParams_VP9 pic_params = { 0, };
  GstD3D11DecoderOutputView *view;

  view = gst_d3d11_vp9_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  pic_params.CurrPic.Index7Bits = view->view_id;
  pic_params.uncompressed_header_size_byte_aligned =
      picture->frame_hdr.frame_header_length_in_bytes;
  pic_params.first_partition_size = picture->frame_hdr.first_partition_size;
  pic_params.StatusReportFeedbackNumber = 1;

  gst_d3d11_vp9_dec_copy_frame_params (self, picture, &pic_params);
  gst_d3d11_vp9_dec_copy_reference_frames (self, picture, dpb, &pic_params);
  gst_d3d11_vp9_dec_copy_frame_refs (self, picture, &pic_params);
  gst_d3d11_vp9_dec_copy_loop_filter_params (self, picture, &pic_params);
  gst_d3d11_vp9_dec_copy_quant_params (self, picture, &pic_params);
  gst_d3d11_vp9_dec_copy_segmentation_params (self, picture, &pic_params);

  return gst_d3d11_vp9_dec_submit_picture_data (self, picture, &pic_params);
}

static gboolean
gst_d3d11_vp9_dec_end_picture (GstVp9Decoder * decoder, GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11Vp9DecResolution;

void
gst_d3d11_vp9_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  GUID profile;
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
  static const GUID *profile2_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2;
  static const GUID *profile0_guid =
      &GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0;
  /* values were taken from chromium. See supported_profile_helper.cc */
  GstD3D11Vp9DecResolution resolutions_to_check[] = {
    {4096, 2160}, {4096, 2304}, {7680, 4320}, {8192, 4320}, {8192, 8192}
  };
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  gboolean have_profile2 = FALSE;
  gboolean have_profile0 = FALSE;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

  have_profile2 = gst_d3d11_decoder_get_supported_decoder_profile (decoder,
      &profile2_guid, 1, &profile);
  if (!have_profile2) {
    GST_DEBUG_OBJECT (device,
        "decoder does not support VP9_VLD_10BIT_PROFILE2");
  } else {
    have_profile2 &=
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_P010);
    have_profile2 &=
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_NV12);
    if (!have_profile2) {
      GST_FIXME_OBJECT (device,
          "device does not support P010 and/or NV12 format");
    }
  }

  have_profile0 = gst_d3d11_decoder_get_supported_decoder_profile (decoder,
      &profile0_guid, 1, &profile);
  if (!have_profile0) {
    GST_DEBUG_OBJECT (device, "decoder does not support VP9_VLD_PROFILE0");
  } else {
    have_profile0 =
        gst_d3d11_decoder_supports_format (decoder, &profile, DXGI_FORMAT_NV12);
    if (!have_profile0) {
      GST_FIXME_OBJECT (device, "device does not support NV12 format");
    }
  }

  if (!have_profile2 && !have_profile0) {
    GST_INFO_OBJECT (device, "device does not support VP9 decoding");
    return;
  }

  if (have_profile0) {
    profile = *profile0_guid;
    format = DXGI_FORMAT_NV12;
  } else {
    profile = *profile2_guid;
    format = DXGI_FORMAT_P010;
  }

  for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    if (gst_d3d11_decoder_supports_resolution (decoder, &profile,
            format, resolutions_to_check[i].width,
            resolutions_to_check[i].height)) {
      max_width = resolutions_to_check[i].width;
      max_height = resolutions_to_check[i].height;

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

  sink_caps = gst_caps_from_string ("video/x-vp9, "
      "framerate = " GST_VIDEO_FPS_RANGE);
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), "
      "framerate = " GST_VIDEO_FPS_RANGE ";"
      "video/x-raw, " "framerate = " GST_VIDEO_FPS_RANGE);

  if (have_profile2) {
    GValue format_list = G_VALUE_INIT;
    GValue format_value = G_VALUE_INIT;

    g_value_init (&format_list, GST_TYPE_LIST);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "NV12");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    g_value_init (&format_value, G_TYPE_STRING);
    g_value_set_string (&format_value, "P010_10LE");
    gst_value_list_append_and_take_value (&format_list, &format_value);

    gst_caps_set_value (src_caps, "format", &format_list);
    g_value_unset (&format_list);
  } else {
    gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);
  }

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
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
