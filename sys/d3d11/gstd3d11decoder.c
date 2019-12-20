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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11decoder.h"
#include "gstd3d11memory.h"
#include "gstd3d11bufferpool.h"
#include "gstd3d11device.h"
#include <string.h>

GST_DEBUG_CATEGORY (d3d11_decoder_debug);
#define GST_CAT_DEFAULT d3d11_decoder_debug

enum
{
  PROP_0,
  PROP_DEVICE,
};

struct _GstD3D11DecoderPrivate
{
  GstD3D11Device *device;

  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;

  ID3D11VideoDecoder *decoder;

  GstBufferPool *internal_pool;

  /* for staging */
  ID3D11Texture2D *staging;
  D3D11_TEXTURE2D_DESC staging_desc;
};

#define OUTPUT_VIEW_QUARK _decoder_output_view_get()
static GQuark
_decoder_output_view_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark =
        (gsize) g_quark_from_static_string ("GstD3D11DecoderOutputView");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

static void gst_d3d11_decoder_constructed (GObject * object);
static void gst_d3d11_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_decoder_dispose (GObject * obj);

#define parent_class gst_d3d11_decoder_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11Decoder,
    gst_d3d11_decoder, GST_TYPE_OBJECT);

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
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (d3d11_decoder_debug,
      "d3d11decoder", 0, "Direct3D11 Base Video Decoder object");
}

static void
gst_d3d11_decoder_init (GstD3D11Decoder * self)
{
  self->priv = gst_d3d11_decoder_get_instance_private (self);
}

static void
gst_d3d11_decoder_constructed (GObject * object)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (object);
  GstD3D11DecoderPrivate *priv = self->priv;
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *device_context_handle;

  if (!priv->device) {
    GST_ERROR_OBJECT (self, "No D3D11Device available");
    return;
  }

  device_handle = gst_d3d11_device_get_device_handle (priv->device);
  device_context_handle =
      gst_d3d11_device_get_device_context_handle (priv->device);

  hr = ID3D11Device_QueryInterface (device_handle, &IID_ID3D11VideoDevice,
      (void **) &priv->video_device);

  if (!gst_d3d11_result (hr, priv->device) || !priv->video_device) {
    GST_ERROR_OBJECT (self, "Cannot create VideoDevice Object: 0x%x",
        (guint) hr);
    priv->video_device = NULL;

    return;
  }

  hr = ID3D11DeviceContext_QueryInterface (device_context_handle,
      &IID_ID3D11VideoContext, (void **) &priv->video_context);

  if (!gst_d3d11_result (hr, priv->device) || !priv->video_context) {
    GST_ERROR_OBJECT (self, "Cannot create VideoContext Object: 0x%x",
        (guint) hr);
    priv->video_context = NULL;

    goto fail;
  }

  return;

fail:
  if (priv->video_device) {
    ID3D11VideoDevice_Release (priv->video_device);
    priv->video_device = NULL;
  }

  if (priv->video_context) {
    ID3D11VideoContext_Release (priv->video_context);
    priv->video_device = NULL;
  }

  return;
}

static void
gst_d3d11_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (object);
  GstD3D11DecoderPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_DEVICE:
      priv->device = g_value_dup_object (value);
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
  GstD3D11DecoderPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d11_decoder_close (GstD3D11Decoder * self)
{
  GstD3D11DecoderPrivate *priv = self->priv;

  gst_d3d11_decoder_reset (self);

  if (priv->video_device) {
    ID3D11VideoDevice_Release (priv->video_device);
    priv->video_device = NULL;
  }

  if (priv->video_context) {
    ID3D11VideoContext_Release (priv->video_context);
    priv->video_device = NULL;
  }

  return TRUE;
}

static void
gst_d3d11_decoder_reset_unlocked (GstD3D11Decoder * decoder)
{
  GstD3D11DecoderPrivate *priv;

  priv = decoder->priv;
  gst_clear_object (&priv->internal_pool);

  if (priv->decoder) {
    ID3D11VideoDecoder_Release (priv->decoder);
    priv->decoder = NULL;
  }

  if (priv->staging) {
    ID3D11Texture2D_Release (priv->staging);
    priv->staging = NULL;
  }

  decoder->opened = FALSE;
}

void
gst_d3d11_decoder_reset (GstD3D11Decoder * decoder)
{
  GstD3D11DecoderPrivate *priv;

  g_return_if_fail (GST_IS_D3D11_DECODER (decoder));

  priv = decoder->priv;
  gst_d3d11_device_lock (priv->device);
  gst_d3d11_decoder_reset_unlocked (decoder);
  gst_d3d11_device_unlock (priv->device);
}

static void
gst_d3d11_decoder_dispose (GObject * obj)
{
  GstD3D11Decoder *self = GST_D3D11_DECODER (obj);
  GstD3D11DecoderPrivate *priv = self->priv;

  if (priv->device) {
    gst_d3d11_decoder_close (self);
    gst_object_unref (priv->device);
    priv->device = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

GstD3D11Decoder *
gst_d3d11_decoder_new (GstD3D11Device * device)
{
  GstD3D11Decoder *decoder;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  decoder = g_object_new (GST_TYPE_D3D11_DECODER, "device", device, NULL);

  if (decoder)
    gst_object_ref_sink (decoder);

  return decoder;
}

static void
gst_d3d11_decoder_output_view_free (GstD3D11DecoderOutputView * view)
{
  GST_DEBUG_OBJECT (view->device, "Free view %p", view);

  if (view->handle) {
    gst_d3d11_device_lock (view->device);
    ID3D11VideoDecoderOutputView_Release (view->handle);
    gst_d3d11_device_unlock (view->device);
  }

  gst_clear_object (&view->device);
  g_free (view);
}

static gboolean
gst_d3d11_decoder_prepare_output_view (GstD3D11Decoder * self,
    GstBufferPool * pool, guint pool_size, const GUID * decoder_profile)
{
  GstD3D11DecoderPrivate *priv = self->priv;
  GstBuffer **list = NULL;
  D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC view_desc = { 0, };
  guint i;

  view_desc.DecodeProfile = *decoder_profile;
  view_desc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;

  list = g_newa (GstBuffer *, pool_size);
  memset (list, 0, sizeof (GstBuffer *) * pool_size);

  /* create output view here */
  for (i = 0; i < pool_size; i++) {
    GstFlowReturn ret;
    GstBuffer *buf = NULL;
    GstD3D11Memory *mem;
    GstD3D11DecoderOutputView *view;
    ID3D11VideoDecoderOutputView *view_handle;
    HRESULT hr;
    ID3D11Texture2D *texture;
    GstD3D11Allocator *allocator;

    ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);

    if (ret != GST_FLOW_OK || !buf) {
      GST_ERROR_OBJECT (self, "Could acquire buffer from pool");
      goto error;
    }

    list[i] = buf;
    mem = (GstD3D11Memory *) gst_buffer_peek_memory (buf, 0);

    allocator = GST_D3D11_ALLOCATOR (GST_MEMORY_CAST (mem)->allocator);
    texture = allocator->texture;

    if (!texture) {
      GST_ERROR_OBJECT (self, "D3D11 allocator does not have texture");
      goto error;
    }

    view_desc.Texture2D.ArraySlice = mem->subresource_index;
    GST_LOG_OBJECT (self,
        "Create decoder output view with index %d", mem->subresource_index);

    hr = ID3D11VideoDevice_CreateVideoDecoderOutputView (priv->video_device,
        (ID3D11Resource *) texture, &view_desc, &view_handle);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self,
          "Could not create decoder output view, hr: 0x%x", (guint) hr);
      goto error;
    }

    view = g_new0 (GstD3D11DecoderOutputView, 1);
    view->device = gst_object_ref (priv->device);
    view->handle = view_handle;
    view->view_id = mem->subresource_index;

    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem), OUTPUT_VIEW_QUARK,
        view, (GDestroyNotify) gst_d3d11_decoder_output_view_free);
  }

  /* return buffers to pool */
  for (i = 0; i < pool_size; i++) {
    gst_buffer_unref (list[i]);
  }

  return TRUE;

error:
  if (list) {
    for (i = 0; i < pool_size; i++) {
      if (list[i])
        gst_buffer_unref (list[i]);
    }
  }

  return FALSE;
}

/* Must be called from D3D11Device thread */
static gboolean
gst_d3d11_decoder_prepare_output_view_pool (GstD3D11Decoder * self,
    GstVideoInfo * info, guint pool_size, const GUID * decoder_profile)
{
  GstD3D11DecoderPrivate *priv = self->priv;
  GstD3D11AllocationParams *alloc_params = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstCaps *caps = NULL;

  gst_clear_object (&priv->internal_pool);

  alloc_params = gst_d3d11_allocation_params_new (info,
      GST_D3D11_ALLOCATION_FLAG_TEXTURE_ARRAY, D3D11_USAGE_DEFAULT,
      D3D11_BIND_DECODER);

  if (!alloc_params) {
    GST_ERROR_OBJECT (self, "Failed to create allocation param");
    goto error;
  }

  alloc_params->desc[0].ArraySize = pool_size;

  pool = gst_d3d11_buffer_pool_new (priv->device);
  if (!pool) {
    GST_ERROR_OBJECT (self, "Failed to create buffer pool");
    goto error;
  }

  /* Setup buffer pool */
  config = gst_buffer_pool_get_config (pool);
  caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Couldn't convert video info to caps");
    goto error;
  }

  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (info),
      0, pool_size);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, alloc_params);
  gst_caps_unref (caps);
  gst_d3d11_allocation_params_free (alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Invalid pool config");
    goto error;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate pool");
    goto error;
  }

  if (!gst_d3d11_decoder_prepare_output_view (self,
          pool, pool_size, decoder_profile)) {
    goto error;
  }

  priv->internal_pool = pool;

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
gst_d3d11_decoder_open (GstD3D11Decoder * decoder, GstD3D11Codec codec,
    GstVideoInfo * info, guint pool_size, const GUID ** decoder_profiles,
    guint profile_size)
{
  GstD3D11DecoderPrivate *priv;
  const GstD3D11Format *d3d11_format;
  HRESULT hr;
  BOOL can_support = FALSE;
  guint config_count;
  D3D11_VIDEO_DECODER_CONFIG *config_list;
  D3D11_VIDEO_DECODER_CONFIG *best_config = NULL;
  D3D11_VIDEO_DECODER_DESC decoder_desc = { 0, };
  const GUID *selected_profile = NULL;
  GUID *guid_list = NULL;
  guint available_profile_count;
  gint i, j;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (codec > GST_D3D11_CODEC_NONE, FALSE);
  g_return_val_if_fail (codec < GST_D3D11_CODEC_LAST, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (pool_size > 0, FALSE);
  g_return_val_if_fail (decoder_profiles != NULL, FALSE);
  g_return_val_if_fail (profile_size > 0, FALSE);

  priv = decoder->priv;
  decoder->opened = FALSE;

  d3d11_format = gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (info));
  if (!d3d11_format || d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (decoder, "Could not determine dxgi format from %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    return FALSE;
  }

  gst_d3d11_device_lock (priv->device);
  available_profile_count =
      ID3D11VideoDevice_GetVideoDecoderProfileCount (priv->video_device);

  if (available_profile_count == 0) {
    GST_ERROR_OBJECT (decoder, "No available decoder profile");
    goto error;
  }

  GST_DEBUG_OBJECT (decoder,
      "Have %u available decoder profiles", available_profile_count);
  guid_list = g_alloca (sizeof (GUID) * available_profile_count);

  for (i = 0; i < available_profile_count; i++) {
    hr = ID3D11VideoDevice_GetVideoDecoderProfile (priv->video_device,
        i, &guid_list[i]);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (decoder, "Failed to get %d th decoder profile", i);
      goto error;
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
      if (IsEqualGUID (decoder_profiles[i], &guid_list[j])) {
        selected_profile = decoder_profiles[i];
        break;
      }
    }
  }

  if (!selected_profile) {
    GST_ERROR_OBJECT (decoder, "No supported decoder profile");
    goto error;
  } else {
    GST_DEBUG_OBJECT (decoder,
        "Selected guid "
        "{ %8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x }",
        (guint) selected_profile->Data1, (guint) selected_profile->Data2,
        (guint) selected_profile->Data3,
        selected_profile->Data4[0], selected_profile->Data4[1],
        selected_profile->Data4[2], selected_profile->Data4[3],
        selected_profile->Data4[4], selected_profile->Data4[5],
        selected_profile->Data4[6], selected_profile->Data4[7]);
  }

  hr = ID3D11VideoDevice_CheckVideoDecoderFormat (priv->video_device,
      selected_profile, d3d11_format->dxgi_format, &can_support);
  if (!gst_d3d11_result (hr, priv->device) || !can_support) {
    GST_ERROR_OBJECT (decoder,
        "VideoDevice could not support dxgi format %d, hr: 0x%x",
        d3d11_format->dxgi_format, (guint) hr);
    goto error;
  }

  gst_d3d11_decoder_reset_unlocked (decoder);

  decoder_desc.SampleWidth = GST_VIDEO_INFO_WIDTH (info);
  decoder_desc.SampleHeight = GST_VIDEO_INFO_HEIGHT (info);
  decoder_desc.OutputFormat = d3d11_format->dxgi_format;
  decoder_desc.Guid = *selected_profile;

  hr = ID3D11VideoDevice_GetVideoDecoderConfigCount (priv->video_device,
      &decoder_desc, &config_count);
  if (!gst_d3d11_result (hr, priv->device) || config_count == 0) {
    GST_ERROR_OBJECT (decoder, "Could not get decoder config count, hr: 0x%x",
        (guint) hr);
    goto error;
  }

  GST_DEBUG_OBJECT (decoder, "Total %d config available", config_count);

  config_list = g_alloca (sizeof (D3D11_VIDEO_DECODER_CONFIG) * config_count);

  for (i = 0; i < config_count; i++) {
    hr = ID3D11VideoDevice_GetVideoDecoderConfig (priv->video_device,
        &decoder_desc, i, &config_list[i]);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (decoder, "Could not get decoder %dth config, hr: 0x%x",
          i, (guint) hr);
      goto error;
    }

    /* FIXME: need support DXVA_Slice_H264_Long ?? */
    /* this config uses DXVA_Slice_H264_Short */
    if (codec == GST_D3D11_CODEC_H264 && config_list[i].ConfigBitstreamRaw == 2) {
      best_config = &config_list[i];
      break;
    }

    if (codec == GST_D3D11_CODEC_VP9 && config_list[i].ConfigBitstreamRaw == 1) {
      best_config = &config_list[i];
      break;
    }
  }

  if (best_config == NULL) {
    GST_ERROR_OBJECT (decoder, "Could not determine decoder config");
    goto error;
  }

  if (!gst_d3d11_decoder_prepare_output_view_pool (decoder,
          info, pool_size, selected_profile)) {
    GST_ERROR_OBJECT (decoder, "Couldn't prepare output view pool");
    goto error;
  }

  hr = ID3D11VideoDevice_CreateVideoDecoder (priv->video_device,
      &decoder_desc, best_config, &priv->decoder);
  if (!gst_d3d11_result (hr, priv->device) || !priv->decoder) {
    GST_ERROR_OBJECT (decoder,
        "Could not create decoder object, hr: 0x%x", (guint) hr);
    goto error;
  }

  GST_DEBUG_OBJECT (decoder, "Decoder object %p created", priv->decoder);

  /* create stage texture to copy out */
  memset (&priv->staging_desc, 0, sizeof (D3D11_TEXTURE2D_DESC));
  priv->staging_desc.Width = GST_VIDEO_INFO_WIDTH (info);
  priv->staging_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
  priv->staging_desc.MipLevels = 1;
  priv->staging_desc.Format = d3d11_format->dxgi_format;
  priv->staging_desc.SampleDesc.Count = 1;
  priv->staging_desc.ArraySize = 1;
  priv->staging_desc.Usage = D3D11_USAGE_STAGING;
  priv->staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  priv->staging = gst_d3d11_device_create_texture (priv->device,
      &priv->staging_desc, NULL);
  if (!priv->staging) {
    GST_ERROR_OBJECT (decoder, "Couldn't create staging texture");
    goto error;
  }

  decoder->opened = TRUE;
  gst_d3d11_device_unlock (priv->device);

  return TRUE;

error:
  gst_d3d11_device_unlock (priv->device);

  return FALSE;
}

gboolean
gst_d3d11_decoder_begin_frame (GstD3D11Decoder * decoder,
    GstD3D11DecoderOutputView * output_view, guint content_key_size,
    gconstpointer content_key)
{
  GstD3D11DecoderPrivate *priv;
  guint retry_count = 0;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (output_view != NULL, FALSE);
  g_return_val_if_fail (output_view->handle != NULL, FALSE);

  priv = decoder->priv;

  do {
    GST_LOG_OBJECT (decoder, "Try begin frame, retry count %d", retry_count);
    gst_d3d11_device_lock (priv->device);
    hr = ID3D11VideoContext_DecoderBeginFrame (priv->video_context,
        priv->decoder, output_view->handle, content_key_size, content_key);
    gst_d3d11_device_unlock (priv->device);

    if (gst_d3d11_result (hr, priv->device)) {
      GST_LOG_OBJECT (decoder, "Success with retry %d", retry_count);
      break;
    } else if (hr == E_PENDING && retry_count < 50) {
      GST_LOG_OBJECT (decoder, "GPU busy, try again");

      /* HACK: no better idea other than sleep...
       * 1ms waiting like msdkdec */
      g_usleep (1000);
    } else {
      break;
    }

    retry_count++;
  } while (TRUE);

  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (decoder, "Failed to begin frame, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_decoder_end_frame (GstD3D11Decoder * decoder)
{
  GstD3D11DecoderPrivate *priv;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  priv = decoder->priv;

  gst_d3d11_device_lock (priv->device);
  hr = ID3D11VideoContext_DecoderEndFrame (priv->video_context, priv->decoder);
  gst_d3d11_device_unlock (priv->device);

  if (!gst_d3d11_result (hr, priv->device)) {
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
  GstD3D11DecoderPrivate *priv;
  UINT size;
  void *decoder_buffer;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  priv = decoder->priv;

  gst_d3d11_device_lock (priv->device);
  hr = ID3D11VideoContext_GetDecoderBuffer (priv->video_context,
      priv->decoder, type, &size, &decoder_buffer);
  gst_d3d11_device_unlock (priv->device);

  if (!gst_d3d11_result (hr, priv->device)) {
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
  GstD3D11DecoderPrivate *priv;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  priv = decoder->priv;

  gst_d3d11_device_lock (priv->device);
  hr = ID3D11VideoContext_ReleaseDecoderBuffer (priv->video_context,
      priv->decoder, type);
  gst_d3d11_device_unlock (priv->device);

  if (!gst_d3d11_result (hr, priv->device)) {
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
  GstD3D11DecoderPrivate *priv;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  priv = decoder->priv;

  gst_d3d11_device_lock (priv->device);
  hr = ID3D11VideoContext_SubmitDecoderBuffers (priv->video_context,
      priv->decoder, buffer_count, buffers);
  gst_d3d11_device_unlock (priv->device);

  if (!gst_d3d11_result (hr, priv->device)) {
    GST_WARNING_OBJECT (decoder, "SubmitDecoderBuffers failed, hr: 0x%x",
        (guint) hr);
    return FALSE;
  }

  return TRUE;
}

GstBuffer *
gst_d3d11_decoder_get_output_view_buffer (GstD3D11Decoder * decoder)
{
  GstD3D11DecoderPrivate *priv;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);

  priv = decoder->priv;

  ret = gst_buffer_pool_acquire_buffer (priv->internal_pool, &buf, NULL);

  if (ret != GST_FLOW_OK || !buf) {
    GST_ERROR_OBJECT (decoder, "Couldn't get buffer from pool, ret %s",
        gst_flow_get_name (ret));
    return FALSE;
  }

  return buf;
}

GstD3D11DecoderOutputView *
gst_d3d11_decoder_get_output_view_from_buffer (GstD3D11Decoder * decoder,
    GstBuffer * buffer)
{
  GstMemory *mem;
  GstD3D11DecoderOutputView *view;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), NULL);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_WARNING_OBJECT (decoder, "nemory is not d3d11 memory");
    return NULL;
  }

  view = (GstD3D11DecoderOutputView *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (mem), OUTPUT_VIEW_QUARK);

  if (!view) {
    GST_WARNING_OBJECT (decoder, "memory does not have output view");
  }

  return view;
}

guint
gst_d3d11_decoder_get_output_view_index (GstD3D11Decoder * decoder,
    ID3D11VideoDecoderOutputView * view_handle)
{
  D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC view_desc;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), 0xff);
  g_return_val_if_fail (view_handle != NULL, 0xff);

  ID3D11VideoDecoderOutputView_GetDesc (view_handle, &view_desc);

  return view_desc.Texture2D.ArraySlice;
}

static gboolean
copy_to_system (GstD3D11Decoder * self, GstVideoInfo * info,
    GstBuffer * decoder_buffer, GstBuffer * output)
{
  GstD3D11DecoderPrivate *priv = self->priv;
  D3D11_TEXTURE2D_DESC *desc = &priv->staging_desc;
  GstVideoFrame out_frame;
  gint i;
  GstD3D11Memory *in_mem;
  D3D11_MAPPED_SUBRESOURCE map;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  gsize dummy;
  HRESULT hr;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (priv->device);

  if (!gst_video_frame_map (&out_frame, info, output, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    return FALSE;
  }

  in_mem = (GstD3D11Memory *) gst_buffer_peek_memory (decoder_buffer, 0);

  gst_d3d11_device_lock (priv->device);
  ID3D11DeviceContext_CopySubresourceRegion (device_context,
      (ID3D11Resource *) priv->staging, 0, 0, 0, 0,
      (ID3D11Resource *) in_mem->texture, in_mem->subresource_index, NULL);

  hr = ID3D11DeviceContext_Map (device_context,
      (ID3D11Resource *) priv->staging, 0, D3D11_MAP_READ, 0, &map);

  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Failed to map, hr: 0x%x", (guint) hr);
    gst_d3d11_device_unlock (priv->device);
    return FALSE;
  }

  gst_d3d11_dxgi_format_get_size (desc->Format, desc->Width, desc->Height,
      map.RowPitch, offset, stride, &dummy);

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&out_frame); i++) {
    guint8 *src, *dst;
    gint j;
    gint width;

    src = (guint8 *) map.pData + offset[i];
    dst = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    width = GST_VIDEO_FRAME_COMP_WIDTH (&out_frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&out_frame, i);

    for (j = 0; j < GST_VIDEO_FRAME_COMP_HEIGHT (&out_frame, i); j++) {
      memcpy (dst, src, width);
      dst += GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, i);
      src += map.RowPitch;
    }
  }

  gst_video_frame_unmap (&out_frame);
  ID3D11DeviceContext_Unmap (device_context, (ID3D11Resource *) priv->staging,
      0);
  gst_d3d11_device_unlock (priv->device);

  return TRUE;
}

static gboolean
copy_to_d3d11 (GstD3D11Decoder * self, GstVideoInfo * info,
    GstBuffer * decoder_buffer, GstBuffer * output)
{
  GstD3D11DecoderPrivate *priv = self->priv;
  gint i;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (priv->device);

  gst_d3d11_device_lock (priv->device);
  for (i = 0; i < gst_buffer_n_memory (output); i++) {
    GstD3D11Memory *in_mem;
    GstD3D11Memory *out_mem;
    D3D11_BOX src_box;
    D3D11_TEXTURE2D_DESC desc;

    in_mem = (GstD3D11Memory *) gst_buffer_peek_memory (decoder_buffer, i);
    out_mem = (GstD3D11Memory *) gst_buffer_peek_memory (output, i);

    ID3D11Texture2D_GetDesc (out_mem->texture, &desc);

    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = desc.Width;
    src_box.bottom = desc.Height;

    ID3D11DeviceContext_CopySubresourceRegion (device_context,
        (ID3D11Resource *) out_mem->texture,
        out_mem->subresource_index, 0, 0, 0,
        (ID3D11Resource *) in_mem->texture, in_mem->subresource_index,
        &src_box);

    GST_MINI_OBJECT_FLAG_SET (out_mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  }
  gst_d3d11_device_unlock (priv->device);

  return TRUE;
}

gboolean
gst_d3d11_decoder_copy_decoder_buffer (GstD3D11Decoder * decoder,
    GstVideoInfo * info, GstBuffer * decoder_buffer, GstBuffer * output)
{
  GstD3D11DecoderPrivate *priv;
  gboolean can_device_copy = TRUE;

  g_return_val_if_fail (GST_IS_D3D11_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (decoder_buffer), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (output), FALSE);

  priv = decoder->priv;

  if (gst_buffer_n_memory (decoder_buffer) == gst_buffer_n_memory (output)) {
    gint i;

    for (i = 0; i < gst_buffer_n_memory (output); i++) {
      GstMemory *mem;
      GstD3D11Memory *dmem;

      mem = gst_buffer_peek_memory (output, i);

      if (!gst_is_d3d11_memory (mem)) {
        can_device_copy = FALSE;
        break;
      }

      dmem = (GstD3D11Memory *) mem;

      if (dmem->device != priv->device) {
        can_device_copy = FALSE;
        break;
      }
    }
  } else {
    can_device_copy = FALSE;
  }

  if (can_device_copy) {
    return copy_to_d3d11 (decoder, info, decoder_buffer, output);
  }

  return copy_to_system (decoder, info, decoder_buffer, output);
}
