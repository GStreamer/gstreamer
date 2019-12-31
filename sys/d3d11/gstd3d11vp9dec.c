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
  PROP_ADAPTER
};

#define DEFAULT_ADAPTER -1

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0,
    0x463707f8, 0xa1d0, 0x4585, 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e);
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2,
    0xa4c749ef, 0x6ecf, 0x48aa, 0x84, 0x48, 0x50, 0xa7, 0xa1, 0x16, 0x5f, 0xf7);

/* reference list 8 + 4 margin */
#define NUM_OUTPUT_VIEW 12

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9")
    );

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, "{ NV12, P010_10LE }") "; "
        GST_VIDEO_CAPS_MAKE ("{ NV12, P010_10LE }")));

#define parent_class gst_d3d11_vp9_dec_parent_class
G_DEFINE_TYPE (GstD3D11Vp9Dec, gst_d3d11_vp9_dec, GST_TYPE_VP9_DECODER);

static void gst_d3d11_vp9_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp9_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp9_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_start (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_stop (GstVideoDecoder * decoder);
static GstFlowReturn gst_d3d11_vp9_dec_handle_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame);
static gboolean gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_vp9_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstVp9Decoder */
static gboolean gst_d3d11_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHdr * frame_hdr);
static gboolean gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);
static GstVp9Picture *gst_d3d11_vp9_dec_duplicate_picture (GstVp9Decoder *
    decoder, GstVp9Picture * picture);
static GstFlowReturn gst_d3d11_vp9_dec_output_picture (GstVp9Decoder *
    decoder, GstVp9Picture * picture);
static gboolean gst_d3d11_vp9_dec_start_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);
static gboolean gst_d3d11_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb);
static gboolean gst_d3d11_vp9_dec_end_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture);

static void
gst_d3d11_vp9_dec_class_init (GstD3D11Vp9DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp9DecoderClass *vp9decoder_class = GST_VP9_DECODER_CLASS (klass);

  gobject_class->set_property = gst_d3d11_vp9_dec_set_property;
  gobject_class->get_property = gst_d3d11_vp9_dec_get_property;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 VP9 Video Decoder",
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based VP9 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_close);
  decoder_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_stop);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp9_dec_handle_frame);
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
  self->adapter = DEFAULT_ADAPTER;
}

static void
gst_d3d11_vp9_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_vp9_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter);
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

  gst_d3d11_handle_set_context (element, context, self->adapter, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_vp9_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), self->adapter,
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
gst_d3d11_vp9_dec_start (GstVideoDecoder * decoder)
{
  return GST_VIDEO_DECODER_CLASS (parent_class)->start (decoder);
}

static gboolean
gst_d3d11_vp9_dec_stop (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);

  gst_vp9_picture_replace (&self->current_picture, NULL);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static GstFlowReturn
gst_d3d11_vp9_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstBuffer *in_buf = frame->input_buffer;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  if (!self->current_picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    gst_video_decoder_drop_frame (decoder, frame);

    return GST_FLOW_ERROR;
  }

  gst_video_codec_frame_set_user_data (frame,
      self->current_picture, (GDestroyNotify) gst_vp9_picture_unref);
  self->current_picture = NULL;

  gst_video_codec_frame_unref (frame);

  return GST_FLOW_OK;
}

static gboolean
gst_d3d11_vp9_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstVp9Decoder *vp9dec = GST_VP9_DECODER (decoder);
  GstCaps *peer_caps;

  GST_DEBUG_OBJECT (self, "negotiate");

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  self->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      self->out_format, self->width, self->height, vp9dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  self->use_d3d11_output = FALSE;

  if (!peer_caps || gst_caps_is_any (peer_caps)) {
    GST_DEBUG_OBJECT (self,
        "cannot determine output format, use system memory");
  } else {
    GstCapsFeatures *features;
    guint size = gst_caps_get_size (peer_caps);
    guint i;

    for (i = 0; i < size; i++) {
      features = gst_caps_get_features (peer_caps, i);
      if (features && gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
        GST_DEBUG_OBJECT (self, "found D3D11 memory feature");
        gst_caps_set_features (self->output_state->caps, 0,
            gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));

        self->use_d3d11_output = TRUE;
        break;
      }
    }
  }
  gst_clear_caps (&peer_caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_vp9_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;

  GST_DEBUG_OBJECT (self, "decide allocation");

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps) {
    GST_DEBUG_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool && (self->use_d3d11_output && !GST_D3D11_BUFFER_POOL (pool))) {
    gst_object_unref (pool);
    pool = NULL;
  }

  if (!pool) {
    if (self->use_d3d11_output)
      pool = gst_d3d11_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();

    min = max = 0;
    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (self->use_d3d11_output) {
    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params)
      d3d11_params = gst_d3d11_allocation_params_new (&vinfo, 0,
          D3D11_USAGE_DEFAULT, 0);

    /* dxva2 decoder uses non-resource format
     * (e.g., use NV12 instead of R8 + R8G8 */
    d3d11_params->desc[0].Width = GST_VIDEO_INFO_WIDTH (&vinfo);
    d3d11_params->desc[0].Height = GST_VIDEO_INFO_HEIGHT (&vinfo);
    d3d11_params->desc[0].Format = d3d11_params->d3d11_format->dxgi_format;

    d3d11_params->flags &= ~GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT;

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  gst_buffer_pool_set_config (pool, config);
  if (self->use_d3d11_output)
    size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

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
            &info, NUM_OUTPUT_VIEW, &profile_guid, 1)) {
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
gst_d3d11_vp9_dec_new_picture (GstVp9Decoder * decoder, GstVp9Picture * picture)
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

  gst_vp9_picture_replace (&self->current_picture, picture);

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

  gst_vp9_picture_replace (&self->current_picture, new_picture);

  return new_picture;
}

static GstFlowReturn
gst_d3d11_vp9_dec_output_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstD3D11Vp9Dec *self = GST_D3D11_VP9_DEC (decoder);
  GList *pending_frames, *iter;
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *output_buffer = NULL;
  GstFlowReturn ret;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_vp9_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    return FALSE;
  }

  pending_frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));
  for (iter = pending_frames; iter; iter = g_list_next (iter)) {
    GstVideoCodecFrame *tmp;
    GstVp9Picture *other_pic;

    tmp = (GstVideoCodecFrame *) iter->data;
    other_pic = gst_video_codec_frame_get_user_data (tmp);
    if (!other_pic) {
      /* FIXME: what should we do here? */
      GST_WARNING_OBJECT (self,
          "Codec frame %p does not have corresponding picture object", tmp);
      continue;
    }

    if (other_pic == picture) {
      frame = gst_video_codec_frame_ref (tmp);
      break;
    }
  }

  g_list_free_full (pending_frames,
      (GDestroyNotify) gst_video_codec_frame_unref);

  if (!picture->frame_hdr.show_frame) {
    GST_LOG_OBJECT (self, "Decode only picture %p", picture);
    if (frame) {
      GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);

      return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
    } else {
      GST_WARNING_OBJECT (self,
          "Failed to find codec frame for picture %p", picture);
      return GST_FLOW_OK;
    }
  }

  if (!frame) {
    GST_WARNING_OBJECT (self,
        "Failed to find codec frame for picture %p", picture);

    output_buffer =
        gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));

    if (!output_buffer) {
      GST_ERROR_OBJECT (self, "Couldn't allocate output buffer");
      return GST_FLOW_ERROR;
    }

    GST_BUFFER_PTS (output_buffer) = picture->pts;
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) = GST_CLOCK_TIME_NONE;
  } else {
    ret =
        gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (self),
        frame);

    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate output frame");
      return ret;
    }

    output_buffer = frame->output_buffer;
    GST_BUFFER_PTS (output_buffer) = picture->pts;
    GST_BUFFER_DTS (output_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (output_buffer) =
        GST_BUFFER_DURATION (frame->input_buffer);
  }

  if (!gst_d3d11_decoder_copy_decoder_buffer (self->d3d11_decoder,
          &self->output_state->info, view_buffer, output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    if (frame)
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    else
      gst_buffer_unref (output_buffer);

    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (output_buffer)));

  if (frame) {
    ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
  } else {
    ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), output_buffer);
  }

  return ret;
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
    gsize copy_size = picture->size - buffer_offset;
    gboolean is_last = TRUE;
    DXVA_Slice_VPx_Short slice_short = { 0, };
    D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[3] = { 0, };

    GST_TRACE_OBJECT (self, "Getting bitstream buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &d3d11_buffer_size,
            &d3d11_buffer)) {
      GST_ERROR_OBJECT (self, "Couldn't get bitstream buffer");
      goto error;
    }

    if (copy_size > d3d11_buffer_size) {
      copy_size = d3d11_buffer_size;
      is_last = FALSE;
    }

    memcpy (d3d11_buffer, picture->data + buffer_offset, copy_size);

    GST_TRACE_OBJECT (self, "Release bitstream buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
      GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");

      goto error;
    }

    slice_short.BSNALunitDataLocation = 0;
    slice_short.SliceBytesInBuffer = (UINT) copy_size;
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

    buffer_desc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
    buffer_desc[2].DataOffset = 0;
    buffer_desc[2].DataSize = copy_size;

    if (!gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
            3, buffer_desc)) {
      GST_ERROR_OBJECT (self, "Couldn't submit decoder buffers");
      goto error;
    }

    buffer_offset += copy_size;
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
