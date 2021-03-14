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

#include "gstd3d11decoder.h"
#include "gstd3d11converter.h"
#include "gstd3d11pluginutils.h"
#include <string.h>

GST_DEBUG_CATEGORY (d3d11_decoder_debug);
#define GST_CAT_DEFAULT d3d11_decoder_debug

enum
{
  PROP_0,
  PROP_DEVICE,
};

struct _GstD3D11Decoder
{
  GstObject parent;

  gboolean configured;

  GstD3D11Device *device;

  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;

  ID3D11VideoDecoder *decoder_handle;

  GstVideoInfo info;

  GstBufferPool *internal_pool;
  /* Internal pool params */
  gint aligned_width;
  gint aligned_height;
  gboolean use_array_of_texture;
  guint dpb_size;
  guint downstream_min_buffers;

  /* for staging */
  ID3D11Texture2D *staging;
  gsize staging_texture_offset[GST_VIDEO_MAX_PLANES];
  gint stating_texture_stride[GST_VIDEO_MAX_PLANES];

  GUID decoder_profile;

  /* For device specific workaround */
  gboolean can_direct_rendering;
};

static void gst_d3d11_decoder_constructed (GObject * object);
static void gst_d3d11_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_decoder_dispose (GObject * obj);

#define parent_class gst_d3d11_decoder_parent_class
G_DEFINE_TYPE (GstD3D11Decoder, gst_d3d11_decoder, GST_TYPE_OBJECT);

static void
gst_d3d11_decoder_class_init (GstD3D11DecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_d3d11_decoder_constructed;
  gobject_class->set_property = gst_d3d11_decoder_set_property;
  gobject_class->get_property = gst_d3d11_decoder_get_property;
  gobject_class->dispose = gst_d3d11_decoder_dispose;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_object ("device", "Device",
          "D3D11 Devicd to use", GST_TYPE_D3D11_DEVICE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
              G_PARAM_STATIC_STRINGS)));

  GST_DEBUG_CATEGORY_INIT (d3d11_decoder_debug,
      "d3d11decoder", 0, "Direct3D11 Base Video Decoder object");
}

static void
gst_d3d11_decoder_init (GstD3D11Decoder * self)
{
}

static void
gst_d3d11_decoder_constructed (GObject * object)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (object);
  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;

  if (!self->device) {
    GST_ERROR_OBJECT (self, "No D3D11Device available");
    return;
  }

  video_device = gst_d3d11_device_get_video_device_handle (self->device);
  if (!video_device) {
    GST_ERROR_OBJECT (self, "ID3D11VideoDevice is not available");
    return;
  }

  video_context = gst_d3d11_device_get_video_context_handle (self->device);
  if (!video_context) {
    GST_ERROR_OBJECT (self, "ID3D11VideoContext is not available");
    return;
  }

  self->video_device = video_device;
  video_device->AddRef ();

  self->video_context = video_context;
  video_context->AddRef ();

  return;
}

static void
gst_d3d11_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (object);

  switch (prop_id) {
    case PROP_DEVICE:
      self->device = (GstD3D11Device *) g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, self->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d11_decoder_close (GstD3D11Decoder * self)
{
  gst_d3d11_decoder_reset (self);

  GST_D3D11_CLEAR_COM (self->video_device);
  GST_D3D11_CLEAR_COM (self->video_context);

  return TRUE;
}

static void
gst_d3d11_decoder_reset_unlocked (GstD3D11Decoder * decoder)
{
  gst_clear_object (&decoder->internal_pool);

  GST_D3D11_CLEAR_COM (decoder->decoder_handle);
  GST_D3D11_CLEAR_COM (decoder->staging);

  decoder->dpb_size = 0;
  decoder->downstream_min_buffers = 0;

  decoder->configured = FALSE;
}

void
gst_d3d11_decoder_reset (GstD3D11Decoder * decoder)
{
  g_return_if_fail (GST_IS_D3D11_DECODER (decoder));

  gst_d3d11_device_lock (decoder->device);
  gst_d3d11_decoder_reset_unlocked (decoder);
  gst_d3d11_device_unlock (decoder->device);
}

static void
gst_d3d11_decoder_dispose (GObject * obj)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (obj);

  if (self->device) {
    gst_d3d11_decoder_close (self);
    gst_object_unref (self->device);
    self->device = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

GstD3D11Decoder *
gst_d3d11_decoder_new (GstD3D11Device * device)
{
  GstD3D11Decoder *self;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  self = (GstD3D11Decoder *)
      g_object_new (GST_TYPE_D3D11_DECODER, "device", device, NULL);

  if (!self->video_device || !self->video_context) {
    gst_object_unref (self);
    return NULL;
  }

  gst_object_ref_sink (self);

  return self;
}

gboolean
gst_d3d11_decoder_is_configured (GstD3D11Decoder * decoder)
{
  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  return decoder->configured;
}

static gboolean
gst_d3d11_decoder_ensure_output_view (GstD3D11Decoder * self,
    GstBuffer * buffer)
{
  GstD3D11Memory *mem;

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);
  if (!gst_d3d11_memory_get_decoder_output_view (mem, self->video_device,
          &self->decoder_profile)) {
    GST_ERROR_OBJECT (self, "Decoder output view is unavailable");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_decoder_prepare_output_view_pool (GstD3D11Decoder * self)
{
  GstD3D11AllocationParams *alloc_params = NULL;
  GstBufferPool *pool = NULL;
  GstCaps *caps = NULL;
  GstVideoAlignment align;
  GstD3D11AllocationFlags alloc_flags = (GstD3D11AllocationFlags) 0;
  gint bind_flags = D3D11_BIND_DECODER;
  GstVideoInfo *info = &self->info;
  guint pool_size;

  gst_clear_object (&self->internal_pool);

  if (!self->use_array_of_texture) {
    alloc_flags = GST_D3D11_ALLOCATION_FLAG_TEXTURE_ARRAY;
  } else {
    /* array of texture can have shader resource view */
    bind_flags |= D3D11_BIND_SHADER_RESOURCE;
  }

  alloc_params = gst_d3d11_allocation_params_new (self->device, info,
      alloc_flags, bind_flags);

  if (!alloc_params) {
    GST_ERROR_OBJECT (self, "Failed to create allocation param");
    goto error;
  }

  pool_size = self->dpb_size + self->downstream_min_buffers;
  GST_DEBUG_OBJECT (self,
      "Configuring internal pool with size %d "
      "(dpb size: %d, downstream min buffers: %d)", pool_size, self->dpb_size,
      self->downstream_min_buffers);

  if (!self->use_array_of_texture)
    alloc_params->desc[0].ArraySize = pool_size;
  gst_video_alignment_reset (&align);

  align.padding_right = self->aligned_width - GST_VIDEO_INFO_WIDTH (info);
  align.padding_bottom = self->aligned_height - GST_VIDEO_INFO_HEIGHT (info);
  if (!gst_d3d11_allocation_params_alignment (alloc_params, &align)) {
    GST_ERROR_OBJECT (self, "Cannot set alignment");
    goto error;
  }

  caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Couldn't convert video info to caps");
    goto error;
  }

  pool = gst_d3d11_buffer_pool_new_with_options (self->device,
      caps, alloc_params, 0, pool_size);
  gst_clear_caps (&caps);
  g_clear_pointer (&alloc_params, gst_d3d11_allocation_params_free);

  if (!pool) {
    GST_ERROR_OBJECT (self, "Failed to create buffer pool");
    goto error;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate pool");
    goto error;
  }

  self->internal_pool = pool;

  return TRUE;

error:
  if (alloc_params)
    gst_d3d11_allocation_params_free (alloc_params);
  if (pool)
    gst_object_unref (pool);
  if (caps)
    gst_caps_unref (caps);

  return FALSE;
}

gboolean
gst_d3d11_decoder_get_supported_decoder_profile (GstD3D11Decoder * decoder,
    const GUID ** decoder_profiles, guint profile_size, GUID * selected_profile)
{
  GUID *guid_list = NULL;
  const GUID *profile = NULL;
  guint available_profile_count;
  guint i, j;
  HRESULT hr;
  ID3D11VideoDevice *video_device;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (decoder_profiles != NULL, FALSE);
  g_return_val_if_fail (profile_size > 0, FALSE);
  g_return_val_if_fail (selected_profile != NULL, FALSE);

  video_device = decoder->video_device;

  available_profile_count = video_device->GetVideoDecoderProfileCount ();

  if (available_profile_count == 0) {
    GST_WARNING_OBJECT (decoder, "No available decoder profile");
    return FALSE;
  }

  GST_DEBUG_OBJECT (decoder,
      "Have %u available decoder profiles", available_profile_count);
  guid_list = (GUID *) g_alloca (sizeof (GUID) * available_profile_count);

  for (i = 0; i < available_profile_count; i++) {
    hr = video_device->GetVideoDecoderProfile (i, &guid_list[i]);
    if (!gst_d3d11_result (hr, decoder->device)) {
      GST_WARNING_OBJECT (decoder, "Failed to get %d th decoder profile", i);
      return FALSE;
    }
  }

#ifndef GST_DISABLE_GST_DEBUG
  GST_LOG_OBJECT (decoder, "Supported decoder GUID");
  for (i = 0; i < available_profile_count; i++) {
    const GUID *guid = &guid_list[i];

    GST_LOG_OBJECT (decoder,
        "\t { %8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x }",
        (guint) guid->Data1, (guint) guid->Data2, (guint) guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
        guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
  }

  GST_LOG_OBJECT (decoder, "Requested decoder GUID");
  for (i = 0; i < profile_size; i++) {
    const GUID *guid = decoder_profiles[i];

    GST_LOG_OBJECT (decoder,
        "\t { %8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x }",
        (guint) guid->Data1, (guint) guid->Data2, (guint) guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
        guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
  }
#endif

  for (i = 0; i < profile_size; i++) {
    for (j = 0; j < available_profile_count; j++) {
      if (IsEqualGUID (*decoder_profiles[i], guid_list[j])) {
        profile = decoder_profiles[i];
        break;
      }
    }
  }

  if (!profile) {
    GST_WARNING_OBJECT (decoder, "No supported decoder profile");
    return FALSE;
  }

  *selected_profile = *profile;

  GST_DEBUG_OBJECT (decoder,
      "Selected guid "
      "{ %8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x }",
      (guint) selected_profile->Data1, (guint) selected_profile->Data2,
      (guint) selected_profile->Data3,
      selected_profile->Data4[0], selected_profile->Data4[1],
      selected_profile->Data4[2], selected_profile->Data4[3],
      selected_profile->Data4[4], selected_profile->Data4[5],
      selected_profile->Data4[6], selected_profile->Data4[7]);

  return TRUE;
}

gboolean
gst_d3d11_decoder_configure (GstD3D11Decoder * decoder, GstD3D11Codec codec,
    GstVideoInfo * info, gint coded_width, gint coded_height,
    guint dpb_size, const GUID ** decoder_profiles, guint profile_size)
{
  const GstD3D11Format *d3d11_format;
  HRESULT hr;
  BOOL can_support = FALSE;
  guint config_count;
  D3D11_VIDEO_DECODER_CONFIG *config_list;
  D3D11_VIDEO_DECODER_CONFIG *best_config = NULL;
  D3D11_VIDEO_DECODER_DESC decoder_desc = { 0, };
  D3D11_TEXTURE2D_DESC staging_desc = { 0, };
  GUID selected_profile;
  guint i;
  gint aligned_width, aligned_height;
  guint alignment;
  GstD3D11DeviceVendor vendor;
  ID3D11Device *device_handle;
  ID3D11VideoDevice *video_device;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (codec > GST_D3D11_CODEC_NONE, FALSE);
  g_return_val_if_fail (codec < GST_D3D11_CODEC_LAST, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (coded_width >= GST_VIDEO_INFO_WIDTH (info), FALSE);
  g_return_val_if_fail (coded_height >= GST_VIDEO_INFO_HEIGHT (info), FALSE);
  g_return_val_if_fail (dpb_size > 0, FALSE);
  g_return_val_if_fail (decoder_profiles != NULL, FALSE);
  g_return_val_if_fail (profile_size > 0, FALSE);

  decoder->configured = FALSE;
  decoder->use_array_of_texture = FALSE;

  device_handle = gst_d3d11_device_get_device_handle (decoder->device);
  video_device = decoder->video_device;

  d3d11_format = gst_d3d11_device_format_from_gst (decoder->device,
      GST_VIDEO_INFO_FORMAT (info));
  if (!d3d11_format || d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (decoder, "Could not determine dxgi format from %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    return FALSE;
  }

  gst_d3d11_device_lock (decoder->device);
  if (!gst_d3d11_decoder_get_supported_decoder_profile (decoder,
          decoder_profiles, profile_size, &selected_profile)) {
    goto error;
  }

  hr = video_device->CheckVideoDecoderFormat (&selected_profile,
      d3d11_format->dxgi_format, &can_support);
  if (!gst_d3d11_result (hr, decoder->device) || !can_support) {
    GST_ERROR_OBJECT (decoder,
        "VideoDevice could not support dxgi format %d, hr: 0x%x",
        d3d11_format->dxgi_format, (guint) hr);
    goto error;
  }

  gst_d3d11_decoder_reset_unlocked (decoder);

  decoder->can_direct_rendering = TRUE;

  vendor = gst_d3d11_get_device_vendor (decoder->device);
  switch (vendor) {
    case GST_D3D11_DEVICE_VENDOR_XBOX:
    case GST_D3D11_DEVICE_VENDOR_QUALCOMM:
      /* FIXME: Need to figure out Xbox device's behavior
       * https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/1312
       *
       * Qualcomm driver seems to be buggy in zero-copy scenario
       */
      decoder->can_direct_rendering = FALSE;
      break;
    default:
      break;
  }

  /* NOTE: other dxva implementations (ffmpeg and vlc) do this
   * and they say the required alignment were mentioned by dxva spec.
   * See ff_dxva2_common_frame_params() in dxva.c of ffmpeg and
   * directx_va_Setup() in directx_va.c of vlc.
   * But... where it is? */
  switch (codec) {
    case GST_D3D11_CODEC_H265:
      /* See directx_va_Setup() impl. in vlc */
      if (vendor != GST_D3D11_DEVICE_VENDOR_XBOX)
        alignment = 128;
      else
        alignment = 16;
      break;
    case GST_D3D11_CODEC_MPEG2:
      /* XXX: ffmpeg does this */
      alignment = 32;
      break;
    default:
      alignment = 16;
      break;
  }

  aligned_width = GST_ROUND_UP_N (coded_width, alignment);
  aligned_height = GST_ROUND_UP_N (coded_height, alignment);
  if (aligned_width != coded_width || aligned_height != coded_height) {
    GST_DEBUG_OBJECT (decoder,
        "coded resolution %dx%d is not aligned to %d, adjust to %dx%d",
        coded_width, coded_height, alignment, aligned_width, aligned_height);
  }

  decoder_desc.SampleWidth = aligned_width;
  decoder_desc.SampleHeight = aligned_height;
  decoder_desc.OutputFormat = d3d11_format->dxgi_format;
  decoder_desc.Guid = selected_profile;

  hr = video_device->GetVideoDecoderConfigCount (&decoder_desc, &config_count);
  if (!gst_d3d11_result (hr, decoder->device) || config_count == 0) {
    GST_ERROR_OBJECT (decoder, "Could not get decoder config count, hr: 0x%x",
        (guint) hr);
    goto error;
  }

  GST_DEBUG_OBJECT (decoder, "Total %d config available", config_count);

  config_list = (D3D11_VIDEO_DECODER_CONFIG *)
      g_alloca (sizeof (D3D11_VIDEO_DECODER_CONFIG) * config_count);

  for (i = 0; i < config_count; i++) {
    hr = video_device->GetVideoDecoderConfig (&decoder_desc, i,
        &config_list[i]);
    if (!gst_d3d11_result (hr, decoder->device)) {
      GST_ERROR_OBJECT (decoder, "Could not get decoder %dth config, hr: 0x%x",
          i, (guint) hr);
      goto error;
    }

    /* FIXME: need support DXVA_Slice_H264_Long ?? */
    /* this config uses DXVA_Slice_H264_Short */
    switch (codec) {
      case GST_D3D11_CODEC_H264:
        if (config_list[i].ConfigBitstreamRaw == 2)
          best_config = &config_list[i];
        break;
      case GST_D3D11_CODEC_H265:
      case GST_D3D11_CODEC_VP9:
      case GST_D3D11_CODEC_VP8:
      case GST_D3D11_CODEC_MPEG2:
        if (config_list[i].ConfigBitstreamRaw == 1)
          best_config = &config_list[i];
        break;
      default:
        g_assert_not_reached ();
        goto error;
    }

    if (best_config)
      break;
  }

  if (best_config == NULL) {
    GST_ERROR_OBJECT (decoder, "Could not determine decoder config");
    goto error;
  }

  GST_DEBUG_OBJECT (decoder, "ConfigDecoderSpecific 0x%x",
      best_config->ConfigDecoderSpecific);

  /* FIXME: Revisit this at some point.
   * Some 4K VP9 + super frame enabled streams would be broken with
   * this configuration (driver crash) on Intel and Nvidia
   */
#if 0
  /* bit 14 is equal to 1b means this config support array of texture and
   * it's recommended type as per DXVA spec */
  if ((best_config->ConfigDecoderSpecific & 0x4000) == 0x4000) {
    GST_DEBUG_OBJECT (decoder, "Config support array of texture");
    decoder->use_array_of_texture = TRUE;
  }
#endif

  hr = video_device->CreateVideoDecoder (&decoder_desc,
      best_config, &decoder->decoder_handle);
  if (!gst_d3d11_result (hr, decoder->device) || !decoder->decoder_handle) {
    GST_ERROR_OBJECT (decoder,
        "Could not create decoder object, hr: 0x%x", (guint) hr);
    goto error;
  }

  GST_DEBUG_OBJECT (decoder,
      "Decoder object %p created", decoder->decoder_handle);

  /* create stage texture to copy out */
  staging_desc.Width = aligned_width;
  staging_desc.Height = aligned_height;
  staging_desc.MipLevels = 1;
  staging_desc.Format = d3d11_format->dxgi_format;
  staging_desc.SampleDesc.Count = 1;
  staging_desc.ArraySize = 1;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  hr = device_handle->CreateTexture2D (&staging_desc, NULL, &decoder->staging);
  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't create staging texture");
    goto error;
  }

  memset (decoder->staging_texture_offset,
      0, sizeof (decoder->staging_texture_offset));
  memset (decoder->stating_texture_stride,
      0, sizeof (decoder->stating_texture_stride));

  decoder->decoder_profile = selected_profile;

  /* Store pool related information here, then we will setup internal pool
   * later once the number of min buffer size required by downstream is known.
   * Actual buffer pool size will be "dpb_size + downstream_min_buffers"
   */
  decoder->info = *info;
  decoder->dpb_size = dpb_size;
  decoder->aligned_width = aligned_width;
  decoder->aligned_height = aligned_height;
  decoder->downstream_min_buffers = 0;

  decoder->configured = TRUE;

  gst_d3d11_device_unlock (decoder->device);

  return TRUE;

error:
  gst_d3d11_decoder_reset_unlocked (decoder);
  gst_d3d11_device_unlock (decoder->device);

  return FALSE;
}

gboolean
gst_d3d11_decoder_begin_frame (GstD3D11Decoder * decoder,
    ID3D11VideoDecoderOutputView * output_view, guint content_key_size,
    gconstpointer content_key)
{
  ID3D11VideoContext *video_context;
  guint retry_count = 0;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (output_view != NULL, FALSE);

  video_context = decoder->video_context;

  do {
    GST_LOG_OBJECT (decoder, "Try begin frame, retry count %d", retry_count);
    gst_d3d11_device_lock (decoder->device);
    hr = video_context->DecoderBeginFrame (decoder->decoder_handle,
        output_view, content_key_size, content_key);
    gst_d3d11_device_unlock (decoder->device);

    /* HACK: Do 100 times retry with 1ms sleep per failure, since DXVA/D3D11
     * doesn't provide API for "GPU-IS-READY-TO-DECODE" like signal.
     * In the worst case, we will error out after 100ms.
     * Note that Windows' clock precision is known to be incorrect,
     * so it would be longer than 100ms in reality.
     */
    if (hr == E_PENDING && retry_count < 100) {
      GST_LOG_OBJECT (decoder, "GPU is busy, try again. Retry count %d",
          retry_count);
      g_usleep (1000);
    } else {
      if (gst_d3d11_result (hr, decoder->device))
        GST_LOG_OBJECT (decoder, "Succeeded with retry count %d", retry_count);
      break;
    }

    retry_count++;
  } while (TRUE);

  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Failed to begin frame, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_decoder_end_frame (GstD3D11Decoder * decoder)
{
  HRESULT hr;
  ID3D11VideoContext *video_context;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  video_context = decoder->video_context;

  gst_d3d11_device_lock (decoder->device);
  hr = video_context->DecoderEndFrame (decoder->decoder_handle);
  gst_d3d11_device_unlock (decoder->device);

  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_WARNING_OBJECT (decoder, "EndFrame failed, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_decoder_get_decoder_buffer (GstD3D11Decoder * decoder,
    D3D11_VIDEO_DECODER_BUFFER_TYPE type, guint * buffer_size,
    gpointer * buffer)
{
  UINT size;
  void *decoder_buffer;
  HRESULT hr;
  ID3D11VideoContext *video_context;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  video_context = decoder->video_context;

  gst_d3d11_device_lock (decoder->device);
  hr = video_context->GetDecoderBuffer (decoder->decoder_handle,
      type, &size, &decoder_buffer);
  gst_d3d11_device_unlock (decoder->device);

  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_WARNING_OBJECT (decoder, "Getting buffer type %d error, hr: 0x%x",
        type, (guint) hr);
    return FALSE;
  }

  *buffer_size = size;
  *buffer = decoder_buffer;

  return TRUE;
}

gboolean
gst_d3d11_decoder_release_decoder_buffer (GstD3D11Decoder * decoder,
    D3D11_VIDEO_DECODER_BUFFER_TYPE type)
{
  HRESULT hr;
  ID3D11VideoContext *video_context;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  video_context = decoder->video_context;

  gst_d3d11_device_lock (decoder->device);
  hr = video_context->ReleaseDecoderBuffer (decoder->decoder_handle, type);
  gst_d3d11_device_unlock (decoder->device);

  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_WARNING_OBJECT (decoder, "ReleaseDecoderBuffer failed, hr: 0x%x",
        (guint) hr);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_decoder_submit_decoder_buffers (GstD3D11Decoder * decoder,
    guint buffer_count, const D3D11_VIDEO_DECODER_BUFFER_DESC * buffers)
{
  HRESULT hr;
  ID3D11VideoContext *video_context;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  video_context = decoder->video_context;

  gst_d3d11_device_lock (decoder->device);
  hr = video_context->SubmitDecoderBuffers (decoder->decoder_handle,
      buffer_count, buffers);
  gst_d3d11_device_unlock (decoder->device);

  if (!gst_d3d11_result (hr, decoder->device)) {
    GST_WARNING_OBJECT (decoder, "SubmitDecoderBuffers failed, hr: 0x%x",
        (guint) hr);
    return FALSE;
  }

  return TRUE;
}

GstBuffer *
gst_d3d11_decoder_get_output_view_buffer (GstD3D11Decoder * decoder,
    GstVideoDecoder * videodec)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  if (!decoder->internal_pool) {
    /* Replicate gst_video_decoder_allocate_output_buffer().
     * In case of zero-copy playback, this is the last chance for querying
     * required min-buffer size by downstream and take account of
     * the min-buffer size into our internel pool size */
    GST_VIDEO_DECODER_STREAM_LOCK (videodec);
    if (gst_pad_check_reconfigure (GST_VIDEO_DECODER_SRC_PAD (videodec))) {
      GST_DEBUG_OBJECT (videodec,
          "Downstream was reconfigured, negotiating again");
      gst_video_decoder_negotiate (videodec);
    }
    GST_VIDEO_DECODER_STREAM_UNLOCK (videodec);

    if (!gst_d3d11_decoder_prepare_output_view_pool (decoder)) {
      GST_ERROR_OBJECT (videodec, "Failed to setup internal pool");
      return NULL;
    }
  }

  ret = gst_buffer_pool_acquire_buffer (decoder->internal_pool, &buf, NULL);

  if (ret != GST_FLOW_OK || !buf) {
    GST_ERROR_OBJECT (videodec, "Couldn't get buffer from pool, ret %s",
        gst_flow_get_name (ret));
    return NULL;
  }

  if (!gst_d3d11_decoder_ensure_output_view (decoder, buf)) {
    GST_ERROR_OBJECT (videodec, "Output view unavailable");
    gst_buffer_unref (buf);

    return NULL;
  }

  return buf;
}

ID3D11VideoDecoderOutputView *
gst_d3d11_decoder_get_output_view_from_buffer (GstD3D11Decoder * decoder,
    GstBuffer * buffer)
{
  GstMemory *mem;
  GstD3D11Memory *dmem;
  ID3D11VideoDecoderOutputView *view;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), NULL);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_WARNING_OBJECT (decoder, "Not a d3d11 memory");
    return NULL;
  }

  dmem = (GstD3D11Memory *) mem;
  view = gst_d3d11_memory_get_decoder_output_view (dmem, decoder->video_device,
      &decoder->decoder_profile);

  if (!view) {
    GST_ERROR_OBJECT (decoder, "Decoder output view is unavailable");
    return NULL;
  }

  return view;
}

guint8
gst_d3d11_decoder_get_output_view_index (ID3D11VideoDecoderOutputView *
    view_handle)
{
  D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC view_desc;

  g_return_val_if_fail (view_handle != NULL, 0xff);

  view_handle->GetDesc (&view_desc);

  return view_desc.Texture2D.ArraySlice;
}

static gboolean
copy_to_system (GstD3D11Decoder * self, GstVideoInfo * info, gint display_width,
    gint display_height, GstBuffer * decoder_buffer, GstBuffer * output)
{
  GstVideoFrame out_frame;
  guint i;
  GstD3D11Memory *in_mem;
  D3D11_MAPPED_SUBRESOURCE map;
  HRESULT hr;
  ID3D11Texture2D *in_texture;
  guint in_subresource_index;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (self->device);

  if (!gst_video_frame_map (&out_frame, info, output, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    return FALSE;
  }

  in_mem = (GstD3D11Memory *) gst_buffer_peek_memory (decoder_buffer, 0);

  in_texture = gst_d3d11_memory_get_texture_handle (in_mem);
  in_subresource_index = gst_d3d11_memory_get_subresource_index (in_mem);

  gst_d3d11_device_lock (self->device);
  device_context->CopySubresourceRegion (self->staging, 0, 0, 0, 0,
      in_texture, in_subresource_index, NULL);

  hr = device_context->Map (self->staging, 0, D3D11_MAP_READ, 0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map, hr: 0x%x", (guint) hr);

    gst_d3d11_device_unlock (self->device);
    gst_video_frame_unmap (&out_frame);

    return FALSE;
  }

  /* calculate stride and offset only once */
  if (self->stating_texture_stride[0] == 0) {
    D3D11_TEXTURE2D_DESC desc;
    gsize dummy;

    self->staging->GetDesc (&desc);

    gst_d3d11_dxgi_format_get_size (desc.Format, desc.Width, desc.Height,
        map.RowPitch, self->staging_texture_offset,
        self->stating_texture_stride, &dummy);
  }

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&out_frame); i++) {
    guint8 *src, *dst;
    gint j;
    gint width;

    src = (guint8 *) map.pData + self->staging_texture_offset[i];
    dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    width = GST_VIDEO_FRAME_COMP_WIDTH (&out_frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&out_frame, i);

    for (j = 0; j < GST_VIDEO_FRAME_COMP_HEIGHT (&out_frame, i); j++) {
      memcpy (dst, src, width);
      dst += GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, i);
      src += self->stating_texture_stride[i];
    }
  }

  gst_video_frame_unmap (&out_frame);
  device_context->Unmap (self->staging, 0);
  gst_d3d11_device_unlock (self->device);

  return TRUE;
}

static gboolean
copy_to_d3d11 (GstD3D11Decoder * self, GstVideoInfo * info, gint display_width,
    gint display_height, GstBuffer * decoder_buffer, GstBuffer * output)
{
  GstD3D11Memory *in_mem;
  GstD3D11Memory *out_mem;
  GstMapInfo out_map;
  D3D11_BOX src_box;
  ID3D11Texture2D *in_texture;
  guint in_subresource_index, out_subresource_index;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (self->device);

  in_mem = (GstD3D11Memory *) gst_buffer_peek_memory (decoder_buffer, 0);
  out_mem = (GstD3D11Memory *) gst_buffer_peek_memory (output, 0);

  if (!gst_memory_map (GST_MEMORY_CAST (out_mem),
          &out_map, (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Couldn't map output d3d11 memory");
    return FALSE;
  }

  gst_d3d11_device_lock (self->device);
  in_texture = gst_d3d11_memory_get_texture_handle (in_mem);
  in_subresource_index = gst_d3d11_memory_get_subresource_index (in_mem);

  src_box.left = 0;
  src_box.top = 0;
  src_box.front = 0;
  src_box.back = 1;

  src_box.right = GST_ROUND_UP_2 (GST_VIDEO_INFO_WIDTH (&self->info));
  src_box.bottom = GST_ROUND_UP_2 (GST_VIDEO_INFO_HEIGHT (&self->info));

  out_subresource_index = gst_d3d11_memory_get_subresource_index (out_mem);
  device_context->CopySubresourceRegion ((ID3D11Resource *) out_map.data,
      out_subresource_index, 0, 0, 0, in_texture, in_subresource_index,
      &src_box);

  gst_d3d11_device_unlock (self->device);
  gst_memory_unmap (GST_MEMORY_CAST (out_mem), &out_map);

  return TRUE;
}

gboolean
gst_d3d11_decoder_process_output (GstD3D11Decoder * decoder,
    GstVideoInfo * info, gint display_width, gint display_height,
    GstBuffer * decoder_buffer, GstBuffer * output)
{
  gboolean can_device_copy = TRUE;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (decoder_buffer), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (output), FALSE);

  /* if decoder buffer is intended to be outputted and we don't need to
   * do post processing, do nothing here */
  if (decoder_buffer == output)
    return TRUE;

  /* decoder buffer must have single memory */
  if (gst_buffer_n_memory (decoder_buffer) == gst_buffer_n_memory (output)) {
    GstMemory *mem;
    GstD3D11Memory *dmem;

    mem = gst_buffer_peek_memory (output, 0);
    if (!gst_is_d3d11_memory (mem)) {
      can_device_copy = FALSE;
      goto do_process;
    }

    dmem = (GstD3D11Memory *) mem;
    if (dmem->device != decoder->device)
      can_device_copy = FALSE;
  } else {
    can_device_copy = FALSE;
  }

do_process:
  if (can_device_copy) {
    return copy_to_d3d11 (decoder, info, display_width, display_height,
        decoder_buffer, output);
  }

  return copy_to_system (decoder, info, display_width, display_height,
      decoder_buffer, output);
}

gboolean
gst_d3d11_decoder_negotiate (GstVideoDecoder * decoder,
    GstVideoCodecState * input_state, GstVideoFormat format,
    guint width, guint height, GstVideoInterlaceMode interlace_mode,
    GstVideoCodecState ** output_state, gboolean * downstream_supports_d3d11)
{
  GstCaps *peer_caps;
  GstVideoCodecState *state = NULL;
  gboolean alternate_interlaced;
  gboolean alternate_supported = FALSE;
  gboolean d3d11_supported = FALSE;

  g_return_val_if_fail (GST_IS_VIDEO_DECODER (decoder), FALSE);
  g_return_val_if_fail (input_state != NULL, FALSE);
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (width > 0, FALSE);
  g_return_val_if_fail (height > 0, FALSE);
  g_return_val_if_fail (output_state != NULL, FALSE);
  g_return_val_if_fail (downstream_supports_d3d11 != NULL, FALSE);

  alternate_interlaced = (interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE);

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  GST_DEBUG_OBJECT (decoder, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  if (!peer_caps || gst_caps_is_any (peer_caps)) {
    GST_DEBUG_OBJECT (decoder,
        "cannot determine output format, use system memory");
  } else {
    GstCapsFeatures *features;
    guint size = gst_caps_get_size (peer_caps);
    guint i;

    for (i = 0; i < size; i++) {
      features = gst_caps_get_features (peer_caps, i);

      if (!features)
        continue;

      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
        d3d11_supported = TRUE;
      }

      /* FIXME: software deinterlace element will not return interlaced caps
       * feature... We should fix it */
      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_FORMAT_INTERLACED)) {
        alternate_supported = TRUE;
      }
    }
  }
  gst_clear_caps (&peer_caps);

  GST_DEBUG_OBJECT (decoder,
      "Downstream feature support, D3D11 memory: %d, interlaced format %d",
      d3d11_supported, alternate_supported);

  if (alternate_interlaced) {
    /* FIXME: D3D11 cannot support alternating interlaced stream yet */
    GST_FIXME_OBJECT (decoder,
        "Implement alternating interlaced stream for D3D11");

    if (alternate_supported) {
      /* Set caps resolution with display size, that's how we designed
       * for alternating interlaced stream */
      height = 2 * height;
      state = gst_video_decoder_set_interlaced_output_state (decoder,
          format, interlace_mode, width, height, input_state);
    } else {
      GST_WARNING_OBJECT (decoder,
          "Downstream doesn't support alternating interlaced stream");

      state = gst_video_decoder_set_output_state (decoder,
          format, width, height, input_state);

      /* XXX: adjust PAR, this would produce output similar to that of
       * "line doubling" (so called bob deinterlacing) processing.
       * apart from missing anchor line (top-field or bottom-field) information.
       * Potentially flickering could happen. So this might not be correct.
       * But it would be better than negotiation error of half-height squeezed
       * image */
      state->info.par_d *= 2;
      state->info.fps_n *= 2;
    }
  } else {
    state = gst_video_decoder_set_interlaced_output_state (decoder,
        format, interlace_mode, width, height, input_state);
  }

  if (!state) {
    GST_ERROR_OBJECT (decoder, "Couldn't set output state");
    return FALSE;
  }

  state->caps = gst_video_info_to_caps (&state->info);

  if (*output_state)
    gst_video_codec_state_unref (*output_state);
  *output_state = state;

  if (d3d11_supported) {
    gst_caps_set_features (state->caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
  }

  *downstream_supports_d3d11 = d3d11_supported;

  return TRUE;
}

gboolean
gst_d3d11_decoder_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query, GstD3D11Device * device, GstD3D11Codec codec,
    gboolean use_d3d11_pool, GstD3D11Decoder * d3d11_decoder)
{
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min = 0, max = 0;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;

  g_return_val_if_fail (GST_IS_VIDEO_DECODER (decoder), FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);
  g_return_val_if_fail (codec > GST_D3D11_CODEC_NONE &&
      codec < GST_D3D11_CODEC_LAST, FALSE);

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps) {
    GST_DEBUG_OBJECT (decoder, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool && (use_d3d11_pool && !GST_D3D11_BUFFER_POOL (pool))) {
    gst_object_unref (pool);
    pool = NULL;
  }

  if (!pool) {
    if (use_d3d11_pool)
      pool = gst_d3d11_buffer_pool_new (device);
    else
      pool = gst_video_buffer_pool_new ();

    min = max = 0;
    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (use_d3d11_pool) {
    GstVideoAlignment align;
    gint width, height;

    gst_video_alignment_reset (&align);

    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params)
      d3d11_params = gst_d3d11_allocation_params_new (device, &vinfo,
          (GstD3D11AllocationFlags) 0, 0);

    width = GST_VIDEO_INFO_WIDTH (&vinfo);
    height = GST_VIDEO_INFO_HEIGHT (&vinfo);

    /* need alignment to copy decoder output texture to downstream texture */
    align.padding_right = GST_ROUND_UP_16 (width) - width;
    align.padding_bottom = GST_ROUND_UP_16 (height) - height;
    if (!gst_d3d11_allocation_params_alignment (d3d11_params, &align)) {
      GST_ERROR_OBJECT (decoder, "Cannot set alignment");
      return FALSE;
    }

    /* Needs render target bind flag so that it can be used for
     * output of shader pipeline if internal resizing is required.
     * Also, downstream can keep using video processor even if we copy
     * some decoded textures into downstream buffer */
    d3d11_params->desc[0].BindFlags |= D3D11_BIND_RENDER_TARGET;

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);

    /* Store min buffer size. We need to take account of the amount of buffers
     * which might be held by downstream in case of zero-copy playback */
    /* XXX: hardcoded bound 16, to avoid too large pool size */
    d3d11_decoder->downstream_min_buffers = MIN (min, 16);

    GST_DEBUG_OBJECT (decoder, "Downstream min buffres: %d", min);
  }

  gst_buffer_pool_set_config (pool, config);
  if (use_d3d11_pool)
    size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

gboolean
gst_d3d11_decoder_can_direct_render (GstD3D11Decoder * decoder,
    GstBuffer * view_buffer, GstMiniObject * picture)
{
  GstMemory *mem;
  GstD3D11Allocator *alloc;
  guint array_size, num_texture_in_use;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (view_buffer), FALSE);
  g_return_val_if_fail (picture != NULL, FALSE);

  if (!decoder->can_direct_rendering)
    return FALSE;

  /* XXX: Not a thread-safe way, but should not be a problem.
   * This object must be protected by videodecoder stream lock
   * and codec base classes are working on upstream streaming thread
   * (i.g., single threaded) */

  /* Baseclass is not holding this picture. So we can wait for this memory
   * to be consumed by downstream as it will be relased once it's processed
   * by downstream */
  if (GST_MINI_OBJECT_REFCOUNT (picture) == 1)
    return TRUE;

  mem = gst_buffer_peek_memory (view_buffer, 0);
  alloc = GST_D3D11_ALLOCATOR_CAST (mem->allocator);

  /* something went wrong */
  if (!gst_d3d11_allocator_get_texture_array_size (alloc, &array_size,
          &num_texture_in_use)) {
    GST_ERROR_OBJECT (decoder, "Couldn't query size of texture array");
    return FALSE;
  }

  GST_TRACE_OBJECT (decoder, "textures-in-use/array-size: %d/%d",
      num_texture_in_use, array_size);

  /* DPB pool is full now */
  if (num_texture_in_use >= array_size)
    return FALSE;

  return TRUE;
}

/* Keep sync with chromium and keep in sorted order.
 * See supported_profile_helpers.cc in chromium */
static const guint legacy_amd_list[] = {
  0x130f, 0x6700, 0x6701, 0x6702, 0x6703, 0x6704, 0x6705, 0x6706, 0x6707,
  0x6708, 0x6709, 0x6718, 0x6719, 0x671c, 0x671d, 0x671f, 0x6720, 0x6721,
  0x6722, 0x6723, 0x6724, 0x6725, 0x6726, 0x6727, 0x6728, 0x6729, 0x6738,
  0x6739, 0x673e, 0x6740, 0x6741, 0x6742, 0x6743, 0x6744, 0x6745, 0x6746,
  0x6747, 0x6748, 0x6749, 0x674a, 0x6750, 0x6751, 0x6758, 0x6759, 0x675b,
  0x675d, 0x675f, 0x6760, 0x6761, 0x6762, 0x6763, 0x6764, 0x6765, 0x6766,
  0x6767, 0x6768, 0x6770, 0x6771, 0x6772, 0x6778, 0x6779, 0x677b, 0x6798,
  0x67b1, 0x6821, 0x683d, 0x6840, 0x6841, 0x6842, 0x6843, 0x6849, 0x6850,
  0x6858, 0x6859, 0x6880, 0x6888, 0x6889, 0x688a, 0x688c, 0x688d, 0x6898,
  0x6899, 0x689b, 0x689c, 0x689d, 0x689e, 0x68a0, 0x68a1, 0x68a8, 0x68a9,
  0x68b0, 0x68b8, 0x68b9, 0x68ba, 0x68be, 0x68bf, 0x68c0, 0x68c1, 0x68c7,
  0x68c8, 0x68c9, 0x68d8, 0x68d9, 0x68da, 0x68de, 0x68e0, 0x68e1, 0x68e4,
  0x68e5, 0x68e8, 0x68e9, 0x68f1, 0x68f2, 0x68f8, 0x68f9, 0x68fa, 0x68fe,
  0x9400, 0x9401, 0x9402, 0x9403, 0x9405, 0x940a, 0x940b, 0x940f, 0x9440,
  0x9441, 0x9442, 0x9443, 0x9444, 0x9446, 0x944a, 0x944b, 0x944c, 0x944e,
  0x9450, 0x9452, 0x9456, 0x945a, 0x945b, 0x945e, 0x9460, 0x9462, 0x946a,
  0x946b, 0x947a, 0x947b, 0x9480, 0x9487, 0x9488, 0x9489, 0x948a, 0x948f,
  0x9490, 0x9491, 0x9495, 0x9498, 0x949c, 0x949e, 0x949f, 0x94a0, 0x94a1,
  0x94a3, 0x94b1, 0x94b3, 0x94b4, 0x94b5, 0x94b9, 0x94c0, 0x94c1, 0x94c3,
  0x94c4, 0x94c5, 0x94c6, 0x94c7, 0x94c8, 0x94c9, 0x94cb, 0x94cc, 0x94cd,
  0x9500, 0x9501, 0x9504, 0x9505, 0x9506, 0x9507, 0x9508, 0x9509, 0x950f,
  0x9511, 0x9515, 0x9517, 0x9519, 0x9540, 0x9541, 0x9542, 0x954e, 0x954f,
  0x9552, 0x9553, 0x9555, 0x9557, 0x955f, 0x9580, 0x9581, 0x9583, 0x9586,
  0x9587, 0x9588, 0x9589, 0x958a, 0x958b, 0x958c, 0x958d, 0x958e, 0x958f,
  0x9590, 0x9591, 0x9593, 0x9595, 0x9596, 0x9597, 0x9598, 0x9599, 0x959b,
  0x95c0, 0x95c2, 0x95c4, 0x95c5, 0x95c6, 0x95c7, 0x95c9, 0x95cc, 0x95cd,
  0x95ce, 0x95cf, 0x9610, 0x9611, 0x9612, 0x9613, 0x9614, 0x9615, 0x9616,
  0x9640, 0x9641, 0x9642, 0x9643, 0x9644, 0x9645, 0x9647, 0x9648, 0x9649,
  0x964a, 0x964b, 0x964c, 0x964e, 0x964f, 0x9710, 0x9711, 0x9712, 0x9713,
  0x9714, 0x9715, 0x9802, 0x9803, 0x9804, 0x9805, 0x9806, 0x9807, 0x9808,
  0x9809, 0x980a, 0x9830, 0x983d, 0x9850, 0x9851, 0x9874, 0x9900, 0x9901,
  0x9903, 0x9904, 0x9905, 0x9906, 0x9907, 0x9908, 0x9909, 0x990a, 0x990b,
  0x990c, 0x990d, 0x990e, 0x990f, 0x9910, 0x9913, 0x9917, 0x9918, 0x9919,
  0x9990, 0x9991, 0x9992, 0x9993, 0x9994, 0x9995, 0x9996, 0x9997, 0x9998,
  0x9999, 0x999a, 0x999b, 0x999c, 0x999d, 0x99a0, 0x99a2, 0x99a4
};

static const guint legacy_intel_list[] = {
  0x102, 0x106, 0x116, 0x126, 0x152, 0x156, 0x166,
  0x402, 0x406, 0x416, 0x41e, 0xa06, 0xa16, 0xf31,
};

static gint
binary_search_compare (const guint * a, const guint * b)
{
  return *a - *b;
}

/* Certain AMD GPU drivers like R600, R700, Evergreen and Cayman and some second
 * generation Intel GPU drivers crash if we create a video device with a
 * resolution higher then 1920 x 1088. This function checks if the GPU is in
 * this list and if yes returns true. */
gboolean
gst_d3d11_decoder_util_is_legacy_device (GstD3D11Device * device)
{
  const guint amd_id[] = { 0x1002, 0x1022 };
  const guint intel_id = 0x8086;
  guint device_id = 0;
  guint vendor_id = 0;
  guint *match = NULL;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), FALSE);

  g_object_get (device, "device-id", &device_id, "vendor-id", &vendor_id, NULL);

  if (vendor_id == amd_id[0] || vendor_id == amd_id[1]) {
    match =
        (guint *) gst_util_array_binary_search ((gpointer) legacy_amd_list,
        G_N_ELEMENTS (legacy_amd_list), sizeof (guint),
        (GCompareDataFunc) binary_search_compare,
        GST_SEARCH_MODE_EXACT, &device_id, NULL);
  } else if (vendor_id == intel_id) {
    match =
        (guint *) gst_util_array_binary_search ((gpointer) legacy_intel_list,
        G_N_ELEMENTS (legacy_intel_list), sizeof (guint),
        (GCompareDataFunc) binary_search_compare,
        GST_SEARCH_MODE_EXACT, &device_id, NULL);
  }

  if (match) {
    GST_DEBUG_OBJECT (device, "it's legacy device");
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_d3d11_decoder_supports_format (GstD3D11Decoder * decoder,
    const GUID * decoder_profile, DXGI_FORMAT format)
{
  HRESULT hr;
  BOOL can_support = FALSE;
  ID3D11VideoDevice *video_device;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (decoder_profile != NULL, FALSE);
  g_return_val_if_fail (format != DXGI_FORMAT_UNKNOWN, FALSE);

  video_device = decoder->video_device;

  hr = video_device->CheckVideoDecoderFormat (decoder_profile, format,
      &can_support);
  if (!gst_d3d11_result (hr, decoder->device) || !can_support) {
    GST_DEBUG_OBJECT (decoder,
        "VideoDevice could not support dxgi format %d, hr: 0x%x",
        format, (guint) hr);

    return FALSE;
  }

  return TRUE;
}

/* Don't call this method with legacy device */
gboolean
gst_d3d11_decoder_supports_resolution (GstD3D11Decoder * decoder,
    const GUID * decoder_profile, DXGI_FORMAT format, guint width, guint height)
{
  D3D11_VIDEO_DECODER_DESC desc;
  HRESULT hr;
  UINT config_count;
  ID3D11VideoDevice *video_device;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (decoder_profile != NULL, FALSE);
  g_return_val_if_fail (format != DXGI_FORMAT_UNKNOWN, FALSE);

  video_device = decoder->video_device;

  desc.SampleWidth = width;
  desc.SampleHeight = height;
  desc.OutputFormat = format;
  desc.Guid = *decoder_profile;

  hr = video_device->GetVideoDecoderConfigCount (&desc, &config_count);
  if (!gst_d3d11_result (hr, decoder->device) || config_count == 0) {
    GST_DEBUG_OBJECT (decoder, "Could not get decoder config count, hr: 0x%x",
        (guint) hr);
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_d3d11_decoder_class_data_new:
 * @device: (transfer none): a #GstD3D11Device
 * @sink_caps: (transfer full): a #GstCaps
 * @src_caps: (transfer full): a #GstCaps
 *
 * Create new #GstD3D11DecoderClassData
 *
 * Returns: (transfer full): the new #GstD3D11DecoderClassData
 */
GstD3D11DecoderClassData *
gst_d3d11_decoder_class_data_new (GstD3D11Device * device,
    GstCaps * sink_caps, GstCaps * src_caps)
{
  GstD3D11DecoderClassData *ret;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (sink_caps != NULL, NULL);
  g_return_val_if_fail (src_caps != NULL, NULL);

  ret = g_new0 (GstD3D11DecoderClassData, 1);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  g_object_get (device, "adapter", &ret->adapter,
      "device-id", &ret->device_id, "vendor-id", &ret->vendor_id,
      "description", &ret->description, NULL);
  ret->sink_caps = sink_caps;
  ret->src_caps = src_caps;

  return ret;
}

void
gst_d3d11_decoder_class_data_free (GstD3D11DecoderClassData * data)
{
  if (!data)
    return;

  gst_clear_caps (&data->sink_caps);
  gst_clear_caps (&data->src_caps);
  g_free (data->description);
  g_free (data);
}
