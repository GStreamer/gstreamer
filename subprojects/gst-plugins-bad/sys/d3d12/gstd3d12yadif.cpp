/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
#include "config.h"
#endif

#include "gstd3d12yadif.h"
#include "gstd3d12pluginutils.h"
#include <gst/d3dshader/gstd3dshader.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <vector>
#include <math.h>
#include <memory>
#include <mutex>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12yadif", 0, "d3d12yadif");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

/* *INDENT-OFF* */
struct YadifCBData
{
  UINT width;
  UINT height;
  UINT primary_line = 0;
  UINT is_second = 0;
};

struct YadifContext
{
  ComPtr<ID3D12PipelineState> pso;
  YadifCBData cb_data = { };
  guint dispatch_x;
  guint dispatch_y;
};

struct YadifConvertContext
{
  ComPtr<ID3D12PipelineState> pso;
  guint dispatch_x;
  guint dispatch_y;
};

struct GstD3D12YadifPrivate
{
  GstD3D12YadifPrivate ()
  {
    fence_pool = gst_d3d12_fence_data_pool_new ();
    output_queue = gst_vec_deque_new (2);
    current_queue = gst_vec_deque_new (2);
    gst_vec_deque_set_clear_func (output_queue,
        (GDestroyNotify) gst_buffer_unref);
    gst_vec_deque_set_clear_func (current_queue,
        (GDestroyNotify) gst_buffer_unref);
  }

  ~GstD3D12YadifPrivate ()
  {
    if (device) {
      gst_d3d12_device_fence_wait (device, queue_type,
          fence_val);
    }

    contexts.clear ();
    pre_context = nullptr;
    post_context = nullptr;
    rs = nullptr;
    cl = nullptr;
    fence = nullptr;
    Flush ();
    gst_vec_deque_free (output_queue);
    if (output_pool)
      gst_buffer_pool_set_active (output_pool, FALSE);
    gst_clear_object (&output_pool);
    if (convert_pool)
      gst_buffer_pool_set_active (convert_pool, FALSE);
    gst_clear_object (&convert_pool);
    gst_clear_object (&desc_pool);
    gst_clear_object (&ca_pool);
    gst_clear_object (&fence_pool);
    gst_clear_object (&cq);
    gst_clear_object (&device);
  }

  void Flush ()
  {
    gst_clear_buffer (&prev_buf);
    gst_clear_buffer (&cur_buf);
    gst_clear_buffer (&next_buf);
  }

  std::vector<std::shared_ptr<YadifContext>> contexts;
  std::shared_ptr<YadifConvertContext> pre_context;
  std::shared_ptr<YadifConvertContext> post_context;
  GstVecDeque *output_queue = nullptr;
  GstVecDeque *current_queue = nullptr;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12RootSignature> convert_rs;
  GstD3D12Device *device = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  ComPtr<ID3D12Fence> fence;
  GstD3D12FenceDataPool *fence_pool = nullptr;
  GstD3D12DescHeapPool *desc_pool = nullptr;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstBuffer *prev_buf = nullptr;
  GstBuffer *cur_buf = nullptr;
  GstBuffer *next_buf = nullptr;
  GstBufferPool *output_pool = nullptr;
  GstBufferPool *convert_pool = nullptr;
  GstVideoInfo info;
  GstVideoInfo origin_info;
  guint64 fence_val = 0;
  guint desc_inc_size;
  gboolean is_forward = TRUE;
  GstD3D12YadifFields fields = GST_D3D12_YADIF_FIELDS_ALL;
  D3D12_COMMAND_LIST_TYPE queue_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12Yadif
{
  GstObject parent;

  GstD3D12YadifPrivate *priv;
};

static void gst_d3d12_yadif_finalize (GObject * object);

#define gst_d3d12_yadif_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Yadif, gst_d3d12_yadif, GST_TYPE_OBJECT);

static void
gst_d3d12_yadif_class_init (GstD3D12YadifClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_yadif_finalize;
}

static void
gst_d3d12_yadif_init (GstD3D12Yadif * self)
{
  self->priv = new GstD3D12YadifPrivate ();
}

static void
gst_d3d12_yadif_finalize (GObject * object)
{
  auto self = GST_D3D12_YADIF (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_d3d12_yadif_get_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob_ = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    std::vector < D3D12_DESCRIPTOR_RANGE > ranges;
    std::vector < D3D12_ROOT_PARAMETER > params;

    for (guint i = 0; i < 3; i++) {
      ranges.push_back (CD3DX12_DESCRIPTOR_RANGE
          (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i));
    }

    ranges.push_back (CD3DX12_DESCRIPTOR_RANGE (D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
            1, 0));

    CD3DX12_ROOT_PARAMETER param;
    param.InitAsDescriptorTable (ranges.size (), ranges.data ());
    params.push_back (param);

    param.InitAsConstants (4, 0);
    params.push_back (param);

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, params.size (),
        params.data (), 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS);

    ComPtr < ID3DBlob > rs_blob;
    ComPtr < ID3DBlob > error_blob;
    auto hr = D3DX12SerializeVersionedRootSignature (&desc,
        D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &error_blob);
    if (!gst_d3d12_result (hr, device)) {
      const gchar *error_msg = nullptr;
      if (error_blob)
        error_msg = (const gchar *) error_blob->GetBufferPointer ();

      GST_ERROR_OBJECT (device,
          "Couldn't serialize rs, hr: 0x%x, error detail: %s",
          (guint) hr, GST_STR_NULL (error_msg));
    } else {
      rs_blob_ = rs_blob.Detach ();
    }
  }
  GST_D3D12_CALL_ONCE_END;

  if (rs_blob_) {
    *blob = rs_blob_;
    rs_blob_->AddRef ();
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_d3d12_yadif_get_convert_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob_ = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    CD3DX12_ROOT_PARAMETER param;
    CD3DX12_DESCRIPTOR_RANGE range[2];
    range[0].Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
    range[1].Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

    param.InitAsDescriptorTable (2, range);

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 1, &param, 0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS);

    ComPtr < ID3DBlob > rs_blob;
    ComPtr < ID3DBlob > error_blob;
    auto hr = D3DX12SerializeVersionedRootSignature (&desc,
        D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &error_blob);
    if (!gst_d3d12_result (hr, device)) {
      const gchar *error_msg = nullptr;
      if (error_blob)
        error_msg = (const gchar *) error_blob->GetBufferPointer ();

      GST_ERROR_OBJECT (device,
          "Couldn't serialize rs, hr: 0x%x, error detail: %s",
          (guint) hr, GST_STR_NULL (error_msg));
    } else {
      rs_blob_ = rs_blob.Detach ();
    }
  }
  GST_D3D12_CALL_ONCE_END;

  if (rs_blob_) {
    *blob = rs_blob_;
    rs_blob_->AddRef ();
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_d3d12_yadif_prepare_convert (GstD3D12Yadif * self)
{
  auto priv = self->priv;

  GstVideoFormat conv_format = GST_VIDEO_FORMAT_UNKNOWN;
  auto format = GST_VIDEO_INFO_FORMAT (&priv->origin_info);
  switch (format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_VYUY:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_IYU2:
      conv_format = GST_VIDEO_FORMAT_AYUV;
      break;
    case GST_VIDEO_FORMAT_Y210:
    case GST_VIDEO_FORMAT_Y212_LE:
    case GST_VIDEO_FORMAT_Y216_LE:
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_v216:
      conv_format = GST_VIDEO_FORMAT_AYUV64;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      conv_format = GST_VIDEO_FORMAT_RGBA;
      break;
    case GST_VIDEO_FORMAT_r210:
      conv_format = GST_VIDEO_FORMAT_RGB10A2_LE;
      break;
    default:
      return TRUE;
  }

  GstD3DConverterCSByteCode pre_byte_code = { };
  GstD3DConverterCSByteCode post_byte_code = { };
  if (!gst_d3d_converter_shader_get_cs_blob (format, conv_format,
          GST_D3D_SM_5_0, &pre_byte_code) ||
      !gst_d3d_converter_shader_get_cs_blob (conv_format, format,
          GST_D3D_SM_5_0, &post_byte_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get convert shader blob");
    return FALSE;
  }

  gst_video_info_set_interlaced_format (&priv->info, conv_format,
      priv->origin_info.interlace_mode,
      priv->origin_info.width, priv->origin_info.height);
  GST_VIDEO_INFO_FIELD_ORDER (&priv->info) =
      GST_VIDEO_INFO_FIELD_ORDER (&priv->origin_info);

  ComPtr < ID3DBlob > rs_blob;
  if (!gst_d3d12_yadif_get_convert_rs_blob (priv->device, &rs_blob)) {
    GST_ERROR_OBJECT (self, "Couldn't get rs blob");
    return FALSE;
  }

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto hr = device->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&priv->convert_rs));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create rs");
    return FALSE;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  pso_desc.pRootSignature = priv->convert_rs.Get ();
  auto pre_context = std::make_shared < YadifConvertContext > ();
  pso_desc.CS.pShaderBytecode = pre_byte_code.byte_code.byte_code;
  pso_desc.CS.BytecodeLength = pre_byte_code.byte_code.byte_code_len;
  hr = device->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&pre_context->pso));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create pso");
    return FALSE;
  }

  pre_context->dispatch_x = (guint) ceil (priv->info.width /
      (float) pre_byte_code.x_unit);
  pre_context->dispatch_y = (guint) ceil (priv->info.height /
      (float) pre_byte_code.y_unit);

  auto post_context = std::make_shared < YadifConvertContext > ();
  pso_desc.CS.pShaderBytecode = post_byte_code.byte_code.byte_code;
  pso_desc.CS.BytecodeLength = post_byte_code.byte_code.byte_code_len;
  hr = device->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&post_context->pso));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create pso");
    return FALSE;
  }

  post_context->dispatch_x = (guint) ceil (priv->info.width /
      (float) post_byte_code.x_unit);
  post_context->dispatch_y = (guint) ceil (priv->info.height /
      (float) post_byte_code.y_unit);

  priv->pre_context = pre_context;
  priv->post_context = post_context;

  priv->convert_pool = gst_d3d12_buffer_pool_new (priv->device);
  auto config = gst_buffer_pool_get_config (priv->convert_pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  auto caps = gst_video_info_to_caps (&priv->origin_info);
  gst_buffer_pool_config_set_params (config,
      caps, priv->origin_info.size, 0, 0);
  gst_caps_unref (caps);

  GstD3D12Format d3d12_format;
  gst_d3d12_device_get_format (priv->device, format, &d3d12_format);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if ((d3d12_format.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }

  auto params = gst_d3d12_allocation_params_new (priv->device,
      &priv->origin_info, GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
      D3D12_HEAP_FLAG_SHARED);
  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);

  if (!gst_buffer_pool_set_config (priv->convert_pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (priv->convert_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Pool active failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_yadif_prepare_context (GstD3D12Yadif * self,
    const GstVideoInfo * info)
{
  auto priv = self->priv;

  ComPtr < ID3DBlob > rs_blob;
  if (!gst_d3d12_yadif_get_rs_blob (priv->device, &rs_blob)) {
    GST_ERROR_OBJECT (self, "Couldn't get rs blob");
    return FALSE;
  }

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto hr = device->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&priv->rs));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create rs");
    return FALSE;
  }

  auto format = GST_VIDEO_INFO_FORMAT (info);
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_AV12:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
    case GST_VIDEO_FORMAT_NV24:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode_luma = { };
      GstD3DShaderByteCode bytecode_chroma = { };
      if (!gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_YADIF_1,
              GST_D3D_SM_5_0, &bytecode_luma) ||
          !gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_YADIF_2,
              GST_D3D_SM_5_0, &bytecode_chroma)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode_luma.byte_code;
      pso_desc.CS.BytecodeLength = bytecode_luma.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      context = std::make_shared < YadifContext > ();

      pso_desc.CS.pShaderBytecode = bytecode_chroma.byte_code;
      pso_desc.CS.BytecodeLength = bytecode_chroma.byte_code_len;
      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      switch (format) {
        case GST_VIDEO_FORMAT_NV16:
        case GST_VIDEO_FORMAT_NV61:
          context->cb_data.width = width / 2;
          context->cb_data.height = height;
          context->cb_data.primary_line = 0;
          context->cb_data.is_second = 0;
          context->dispatch_x = (guint) ceil (width / 16.0);
          context->dispatch_y = (guint) ceil (height / 8.0);
          break;
        case GST_VIDEO_FORMAT_NV24:
          context->cb_data.width = width;
          context->cb_data.height = height;
          context->cb_data.primary_line = 0;
          context->cb_data.is_second = 0;
          context->dispatch_x = (guint) ceil (width / 8.0);
          context->dispatch_y = (guint) ceil (height / 8.0);
          break;
        default:
          context->cb_data.width = width / 2;
          context->cb_data.height = height / 2;
          context->cb_data.primary_line = 0;
          context->cb_data.is_second = 0;
          context->dispatch_x = (guint) ceil (width / 16.0);
          context->dispatch_y = (guint) ceil (height / 16.0);
          break;
      }

      priv->contexts.push_back (context);

      if (format == GST_VIDEO_FORMAT_AV12) {
        context = std::make_shared < YadifContext > ();
        context->pso = priv->contexts[0]->pso;
        context->cb_data.width = width;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 8.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_I420_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_I420_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
          break;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 2;
        context->cb_data.height = height / 2;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 16.0);
        context->dispatch_y = (guint) ceil (height / 16.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_Y41B:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_4 (info->width);
      guint height = GST_ROUND_UP_4 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 4;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 32.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_I422_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_I422_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
          break;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 2;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 16.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_4 (info->width);
      guint height = GST_ROUND_UP_4 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 4;
        context->cb_data.height = height / 4;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 32.0);
        context->dispatch_y = (guint) ceil (height / 32.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_RGBP:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_Y444_10LE:
        case GST_VIDEO_FORMAT_GBR_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_Y444_12LE:
        case GST_VIDEO_FORMAT_GBR_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
          break;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();
      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 8.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_BGRA64_LE:
    case GST_VIDEO_FORMAT_Y412_LE:
    case GST_VIDEO_FORMAT_Y416_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_BGR10A2_LE:
    case GST_VIDEO_FORMAT_VUYA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ARGB64_LE:
    case GST_VIDEO_FORMAT_AYUV64:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY8:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_4;
      switch (format) {
        case GST_VIDEO_FORMAT_GRAY16_LE:
        case GST_VIDEO_FORMAT_GRAY8:
          cs = GST_D3D_PLUGIN_CS_YADIF_1;
          break;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();
      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);
      break;
    }
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A420_16LE:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_A420_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_A420_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 2;
        context->cb_data.height = height / 2;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 16.0);
        context->dispatch_y = (guint) ceil (height / 16.0);

        priv->contexts.push_back (context);
      }

      context = std::make_shared < YadifContext > ();
      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);
      break;
    }
    case GST_VIDEO_FORMAT_A422:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_A422_12LE:
    case GST_VIDEO_FORMAT_A422_16LE:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_A422_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_A422_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 2; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width / 2;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 16.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }

      context = std::make_shared < YadifContext > ();
      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);
      break;
    }
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    case GST_VIDEO_FORMAT_A444:
    case GST_VIDEO_FORMAT_A444_10LE:
    case GST_VIDEO_FORMAT_A444_12LE:
    case GST_VIDEO_FORMAT_A444_16LE:
    {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
      pso_desc.pRootSignature = priv->rs.Get ();

      GstD3DShaderByteCode bytecode = { };
      GstD3DPluginCS cs = GST_D3D_PLUGIN_CS_YADIF_1;
      switch (format) {
        case GST_VIDEO_FORMAT_GBRA_10LE:
        case GST_VIDEO_FORMAT_A444_10LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_10;
          break;
        case GST_VIDEO_FORMAT_GBRA_12LE:
        case GST_VIDEO_FORMAT_A444_12LE:
          cs = GST_D3D_PLUGIN_CS_YADIF_1_12;
          break;
        default:
          break;
      }

      if (!gst_d3d_plugin_shader_get_cs_blob (cs, GST_D3D_SM_5_0, &bytecode)) {
        GST_ERROR_OBJECT (self, "Couldn't get cs blob");
        return FALSE;
      }

      pso_desc.CS.pShaderBytecode = bytecode.byte_code;
      pso_desc.CS.BytecodeLength = bytecode.byte_code_len;

      auto context = std::make_shared < YadifContext > ();

      hr = device->CreateComputePipelineState (&pso_desc,
          IID_PPV_ARGS (&context->pso));
      if (!gst_d3d12_result (hr, priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      guint width = GST_ROUND_UP_2 (info->width);
      guint height = GST_ROUND_UP_2 (info->height);

      context->cb_data.width = width;
      context->cb_data.height = height;
      context->cb_data.primary_line = 0;
      context->cb_data.is_second = 0;
      context->dispatch_x = (guint) ceil (width / 8.0);
      context->dispatch_y = (guint) ceil (height / 8.0);

      priv->contexts.push_back (context);

      for (guint i = 0; i < 3; i++) {
        context = std::make_shared < YadifContext > ();
        context->cb_data.width = width;
        context->cb_data.height = height;
        context->cb_data.primary_line = 0;
        context->cb_data.is_second = 0;
        context->dispatch_x = (guint) ceil (width / 8.0);
        context->dispatch_y = (guint) ceil (height / 8.0);

        priv->contexts.push_back (context);
      }
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Not supported format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
      return FALSE;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = { };
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  /* 4 descriptors per Dispatch (3 SRV and 1 UAV)
   * 2x for top and bottom fields */
  heap_desc.NumDescriptors = 4 * 2 * GST_VIDEO_INFO_N_PLANES (info);
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  priv->desc_pool = gst_d3d12_desc_heap_pool_new (device, &heap_desc);

  priv->desc_inc_size = device->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  priv->output_pool = gst_d3d12_buffer_pool_new (priv->device);
  auto config = gst_buffer_pool_get_config (priv->output_pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  auto caps = gst_video_info_to_caps (info);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);
  gst_caps_unref (caps);

  GstD3D12Format d3d12_format;
  gst_d3d12_device_get_format (priv->device, GST_VIDEO_INFO_FORMAT (info),
      &d3d12_format);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if ((d3d12_format.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }

  auto params = gst_d3d12_allocation_params_new (priv->device, info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
      D3D12_HEAP_FLAG_SHARED);
  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);

  if (!gst_buffer_pool_set_config (priv->output_pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (priv->output_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Pool active failed");
    return FALSE;
  }

  return TRUE;
}

GstD3D12Yadif *
gst_d3d12_yadif_new (GstD3D12Device * device, const GstVideoInfo * info,
    gboolean use_compute)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (info, nullptr);

  auto self = (GstD3D12Yadif *) g_object_new (GST_TYPE_D3D12_YADIF, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->info = *info;
  priv->origin_info = *info;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->queue_type = use_compute ?
      D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;

  switch (info->interlace_mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
    case GST_VIDEO_INTERLACE_MODE_MIXED:
      break;
    default:
      GST_ERROR_OBJECT (self, "Interlaced mode not supported");
      gst_object_unref (self);
      return nullptr;
  }

  if (!gst_d3d12_yadif_prepare_convert (self)) {
    gst_object_unref (self);
    return nullptr;
  }

  if (!gst_d3d12_yadif_prepare_context (self, &priv->info)) {
    gst_object_unref (self);
    return nullptr;
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  priv->ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
      priv->queue_type);
  priv->cq = gst_d3d12_device_get_cmd_queue (priv->device, priv->queue_type);
  gst_object_ref (priv->cq);
  priv->fence = gst_d3d12_cmd_queue_get_fence_handle (priv->cq);

  return self;
}

void
gst_d3d12_yadif_set_fields (GstD3D12Yadif * yadif, GstD3D12YadifFields fields)
{
  g_return_if_fail (GST_IS_D3D12_YADIF (yadif));

  auto priv = yadif->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->fields = fields;
}

void
gst_d3d12_yadif_set_direction (GstD3D12Yadif * yadif, gboolean is_forward)
{
  g_return_if_fail (GST_IS_D3D12_YADIF (yadif));

  auto priv = yadif->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->is_forward = is_forward;
}

struct GstD3D12YadifFrameCtx
{
  GstD3D12Frame prev;
  GstD3D12Frame cur;
  GstD3D12Frame next;
  GstD3D12Frame out_frames[2];
  GstD3D12Frame conv_frames[2];
  UINT is_second[2];
};

static void
gst_d3d12_yadif_unmap_frame_ctx (GstD3D12YadifFrameCtx * ctx)
{
  gst_d3d12_frame_unmap (&ctx->prev);
  gst_d3d12_frame_unmap (&ctx->cur);
  gst_d3d12_frame_unmap (&ctx->next);
  for (guint i = 0; i < 2; i++) {
    gst_d3d12_frame_unmap (&ctx->out_frames[i]);
    gst_d3d12_frame_unmap (&ctx->conv_frames[i]);
  }
}

struct CopyMetaData
{
  GstBuffer *outbuf;
  gboolean copy_cc;
};

static gboolean
foreach_metadata (GstBuffer * inbuf, GstMeta ** meta, gpointer user_data)
{
  auto data = (CopyMetaData *) user_data;
  auto info = (*meta)->info;
  bool do_copy = false;

  if (gst_meta_api_type_has_tag (info->api, _gst_meta_tag_memory)
      || gst_meta_api_type_has_tag (info->api, _gst_meta_tag_memory_reference)) {
    /* never call the transform_meta with memory specific metadata */
    do_copy = false;
  } else if (info->api == GST_VIDEO_CAPTION_META_API_TYPE && !data->copy_cc) {
    do_copy = false;
  } else if (!gst_meta_api_type_get_tags (info->api)) {
    do_copy = true;
  }

  if (do_copy && info->transform_func) {
    GstMetaTransformCopy copy_data;
    copy_data.region = FALSE;
    copy_data.offset = 0;
    copy_data.size = (gsize) - 1;
    info->transform_func (data->outbuf, *meta, inbuf, _gst_meta_transform_copy,
        &copy_data);
  }

  return TRUE;
}

static gboolean
gst_d3d12_yadif_map_frames (GstD3D12Yadif * self, guint tff,
    GstD3D12YadifFrameCtx * ctx, GstD3D12FenceData * fence_data,
    std::vector < ID3D12Fence * >&fences_to_wait,
    std::vector < guint64 > &fence_values_to_wait)
{
  auto priv = self->priv;
  GstClockTime first_pts = GST_CLOCK_TIME_NONE;
  GstClockTime second_pts = GST_CLOCK_TIME_NONE;
  GstClockTime dur = GST_CLOCK_TIME_NONE;
  GstBuffer *first_buf = nullptr;
  GstBuffer *second_buf = nullptr;
  GstBuffer *first_conv_buf = nullptr;
  GstBuffer *second_conv_buf = nullptr;
  GstBuffer *first_target = nullptr;
  GstBuffer *second_target = nullptr;
  GstD3D12FrameMapFlags out_map_flags = GST_D3D12_FRAME_MAP_FLAG_UAV;

  if (priv->post_context)
    out_map_flags |= GST_D3D12_FRAME_MAP_FLAG_SRV;

  gst_vec_deque_clear (priv->current_queue);
  memset (ctx, 0, sizeof (GstD3D12YadifFrameCtx));

  if (!gst_d3d12_frame_map (&ctx->prev, &priv->info, priv->prev_buf,
          GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (self, "Couldn't map prev frame");
    goto error;
  }

  if (!gst_d3d12_frame_map (&ctx->cur, &priv->info, priv->cur_buf,
          GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (self, "Couldn't map cur frame");
    goto error;
  }

  if (!gst_d3d12_frame_map (&ctx->next, &priv->info, priv->next_buf,
          GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (self, "Couldn't map next frame");
    goto error;
  }

  gst_buffer_pool_acquire_buffer (priv->output_pool, &first_buf, nullptr);
  if (!first_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire first field buffer");
    goto error;
  }

  if (priv->post_context) {
    gst_buffer_pool_acquire_buffer (priv->convert_pool,
        &first_conv_buf, nullptr);
    if (!first_conv_buf) {
      GST_ERROR_OBJECT (self, "Couldn't acquire first field output buffer");
      gst_clear_buffer (&first_buf);
      goto error;
    }

    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (first_buf));
    gst_vec_deque_push_tail (priv->current_queue, first_conv_buf);
    first_target = first_conv_buf;
  } else {
    gst_vec_deque_push_tail (priv->current_queue, first_buf);
    first_target = first_buf;
  }

  /* Copy buffer flags except for interlace related ones */
  gst_buffer_copy_into (first_target, priv->cur_buf, GST_BUFFER_COPY_FLAGS, 0,
      -1);
  GST_BUFFER_FLAG_UNSET (first_target, GST_VIDEO_BUFFER_FLAG_INTERLACED);
  GST_BUFFER_FLAG_UNSET (first_target, GST_VIDEO_BUFFER_FLAG_TFF);

  if (priv->fields == GST_D3D12_YADIF_FIELDS_ALL) {
    gst_buffer_pool_acquire_buffer (priv->output_pool, &second_buf, nullptr);
    if (!second_buf) {
      GST_ERROR_OBJECT (self, "Couldn't acquire second field buffer");
      goto error;
    }

    if (priv->post_context) {
      gst_buffer_pool_acquire_buffer (priv->convert_pool,
          &second_conv_buf, nullptr);
      if (!second_conv_buf) {
        GST_ERROR_OBJECT (self, "Couldn't acquire second field output buffer");
        gst_clear_buffer (&second_buf);
        goto error;
      }
      gst_d3d12_fence_data_push (fence_data,
          FENCE_NOTIFY_MINI_OBJECT (second_buf));
      gst_vec_deque_push_tail (priv->current_queue, second_conv_buf);
      second_target = second_conv_buf;
    } else {
      gst_vec_deque_push_tail (priv->current_queue, second_buf);
      second_target = second_buf;
    }

    gst_buffer_copy_into (second_target, priv->cur_buf, GST_BUFFER_COPY_FLAGS,
        0, -1);
    GST_BUFFER_FLAG_UNSET (second_target, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    GST_BUFFER_FLAG_UNSET (second_target, GST_VIDEO_BUFFER_FLAG_TFF);

    if (GST_BUFFER_PTS_IS_VALID (priv->cur_buf)) {
      first_pts = GST_BUFFER_PTS (priv->cur_buf);
      if (GST_BUFFER_DURATION_IS_VALID (priv->cur_buf)) {
        dur = GST_BUFFER_DURATION (priv->cur_buf) / 2;
      } else if (GST_BUFFER_PTS_IS_VALID (priv->next_buf)) {
        auto next_pts = GST_BUFFER_PTS (priv->next_buf);
        if (priv->is_forward && first_pts <= next_pts)
          dur = (next_pts - first_pts) / 2;
        else if (!priv->is_forward && first_pts >= next_pts)
          dur = (first_pts - next_pts) / 2;
      }

      if (GST_CLOCK_TIME_IS_VALID (dur))
        second_pts = first_pts + dur;
    }

    if (priv->is_forward) {
      GST_BUFFER_PTS (first_target) = first_pts;
      GST_BUFFER_PTS (second_target) = second_pts;
    } else {
      GST_BUFFER_PTS (first_target) = second_pts;
      GST_BUFFER_PTS (second_target) = first_pts;
    }

    GST_BUFFER_DURATION (first_target) = dur;
    GST_BUFFER_DURATION (second_target) = dur;
  } else {
    GST_BUFFER_PTS (first_target) = GST_BUFFER_PTS (priv->cur_buf);
    GST_BUFFER_DURATION (first_target) = GST_BUFFER_DURATION (priv->cur_buf);
  }

  CopyMetaData copy_meta;
  copy_meta.copy_cc = TRUE;
  copy_meta.outbuf = first_target;
  gst_buffer_foreach_meta (priv->cur_buf, foreach_metadata, &copy_meta);

  if (second_target) {
    copy_meta.copy_cc = FALSE;
    copy_meta.outbuf = second_target;
    gst_buffer_foreach_meta (priv->cur_buf, foreach_metadata, &copy_meta);
  }

  switch (priv->fields) {
    case GST_D3D12_YADIF_FIELDS_TOP:
      ctx->is_second[0] = tff ? 0 : 1;
      break;
    case GST_D3D12_YADIF_FIELDS_BOTTOM:
      ctx->is_second[0] = tff ? 1 : 0;
      break;
    case GST_D3D12_YADIF_FIELDS_ALL:
      if (priv->is_forward) {
        ctx->is_second[0] = 0;
        ctx->is_second[1] = 1;
      } else {
        ctx->is_second[0] = 1;
        ctx->is_second[1] = 0;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!gst_d3d12_frame_map (&ctx->out_frames[0], &priv->info, first_buf,
          GST_MAP_D3D12, out_map_flags)) {
    GST_ERROR_OBJECT (self, "Couldn't map first field output");
    goto error;
  }

  if (first_conv_buf && !gst_d3d12_frame_map (&ctx->conv_frames[0],
          &priv->origin_info, first_conv_buf,
          GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_UAV)) {
    GST_ERROR_OBJECT (self, "Couldn't map first field convert output");
    goto error;
  }

  if (second_buf &&
      !gst_d3d12_frame_map (&ctx->out_frames[1], &priv->info, second_buf,
          GST_MAP_D3D12, out_map_flags)) {
    GST_ERROR_OBJECT (self, "Couldn't map second field output");
    goto error;
  }

  if (second_conv_buf && !gst_d3d12_frame_map (&ctx->conv_frames[1],
          &priv->origin_info, second_conv_buf,
          GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_UAV)) {
    GST_ERROR_OBJECT (self, "Couldn't map second field convert output");
    goto error;
  }

  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (priv->prev_buf)));
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (priv->cur_buf)));
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (priv->next_buf)));

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->info); i++) {
    if (ctx->prev.fence[i].fence &&
        ctx->prev.fence[i].fence != priv->fence.Get ()) {
      fences_to_wait.push_back (ctx->prev.fence[i].fence);
      fence_values_to_wait.push_back (ctx->prev.fence[i].fence_value);
    }

    if (ctx->cur.fence[i].fence &&
        ctx->cur.fence[i].fence != priv->fence.Get ()) {
      fences_to_wait.push_back (ctx->cur.fence[i].fence);
      fence_values_to_wait.push_back (ctx->cur.fence[i].fence_value);
    }

    if (ctx->next.fence[i].fence &&
        ctx->next.fence[i].fence != priv->fence.Get ()) {
      fences_to_wait.push_back (ctx->next.fence[i].fence);
      fence_values_to_wait.push_back (ctx->next.fence[i].fence_value);
    }
  }

  return TRUE;

error:
  gst_d3d12_yadif_unmap_frame_ctx (ctx);
  gst_vec_deque_clear (priv->current_queue);
  return FALSE;
}

static GstFlowReturn
gst_d3d12_yadif_process_frame (GstD3D12Yadif * self)
{
  auto priv = self->priv;
  UINT tff = 0;

  g_return_val_if_fail (priv->prev_buf, GST_FLOW_ERROR);
  g_return_val_if_fail (priv->cur_buf, GST_FLOW_ERROR);
  g_return_val_if_fail (priv->next_buf, GST_FLOW_ERROR);

  switch (GST_VIDEO_INFO_INTERLACE_MODE (&priv->info)) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
      gst_vec_deque_push_tail (priv->output_queue,
          gst_buffer_ref (priv->cur_buf));
      return GST_FLOW_OK;
    case GST_VIDEO_INTERLACE_MODE_MIXED:
      if (!GST_BUFFER_FLAG_IS_SET (priv->cur_buf,
              GST_VIDEO_BUFFER_FLAG_INTERLACED)) {
        gst_vec_deque_push_tail (priv->output_queue,
            gst_buffer_ref (priv->cur_buf));
        return GST_FLOW_OK;
      }

      if (GST_BUFFER_FLAG_IS_SET (priv->cur_buf, GST_VIDEO_BUFFER_FLAG_TFF))
        tff = 1;
      break;
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      if (GST_VIDEO_INFO_FIELD_ORDER (&priv->info) ==
          GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST) {
        tff = 1;
      } else if (GST_VIDEO_INFO_FIELD_ORDER (&priv->info) ==
          GST_VIDEO_FIELD_ORDER_UNKNOWN &&
          GST_BUFFER_FLAG_IS_SET (priv->cur_buf, GST_VIDEO_BUFFER_FLAG_TFF)) {
        tff = 1;
      }
      break;
    default:
      GST_ERROR_OBJECT (self, "Not supported interlace mode");
      return GST_FLOW_ERROR;
  }

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_pool, &fence_data);

  GstD3D12YadifFrameCtx frame_ctx;
  std::vector < ID3D12Fence * >fences_to_wait;
  std::vector < guint64 > fence_values_to_wait;

  if (!gst_d3d12_yadif_map_frames (self, tff, &frame_ctx, fence_data,
          fences_to_wait, fence_values_to_wait)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame context");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  GstD3D12DescHeap *desc_heap;
  if (!gst_d3d12_desc_heap_pool_acquire (priv->desc_pool, &desc_heap)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
    gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (desc_heap));

  GstD3D12DescHeap *conv_desc_heap = nullptr;
  ID3D12DescriptorHeap *conv_desc_handle = nullptr;
  if (priv->post_context) {
    if (!gst_d3d12_desc_heap_pool_acquire (priv->desc_pool, &conv_desc_heap)) {
      GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
      gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
      gst_d3d12_fence_data_unref (fence_data);
      return GST_FLOW_ERROR;
    }

    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (conv_desc_heap));
  }

  auto desc_handle = gst_d3d12_desc_heap_get_handle (desc_heap);
  auto cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE
      (GetCPUDescriptorHandleForHeapStart (desc_handle));

  guint num_fields = priv->fields == GST_D3D12_YADIF_FIELDS_ALL ? 2 : 1;
  for (guint field = 0; field < num_fields; field++) {
    for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->info); i++) {
      device->CopyDescriptorsSimple (1, cpu_handle,
          frame_ctx.prev.srv_desc_handle[i],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      cpu_handle.Offset (priv->desc_inc_size);

      device->CopyDescriptorsSimple (1, cpu_handle,
          frame_ctx.cur.srv_desc_handle[i],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      cpu_handle.Offset (priv->desc_inc_size);

      device->CopyDescriptorsSimple (1, cpu_handle,
          frame_ctx.next.srv_desc_handle[i],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      cpu_handle.Offset (priv->desc_inc_size);

      device->CopyDescriptorsSimple (1, cpu_handle,
          frame_ctx.out_frames[field].uav_desc_handle[i],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      cpu_handle.Offset (priv->desc_inc_size);
    }
  }

  if (conv_desc_heap) {
    conv_desc_handle = gst_d3d12_desc_heap_get_handle (conv_desc_heap);
    auto conv_cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE
        (GetCPUDescriptorHandleForHeapStart (conv_desc_handle));

    for (guint field = 0; field < num_fields; field++) {
      device->CopyDescriptorsSimple (1, conv_cpu_handle,
          frame_ctx.out_frames[field].srv_desc_handle[0],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      conv_cpu_handle.Offset (priv->desc_inc_size);

      device->CopyDescriptorsSimple (1, conv_cpu_handle,
          frame_ctx.conv_frames[field].uav_desc_handle[0],
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      conv_cpu_handle.Offset (priv->desc_inc_size);
    }
  }

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (priv->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  HRESULT hr = ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (!priv->cl) {
    hr = device->CreateCommandList (0, priv->queue_type,
        ca, nullptr, IID_PPV_ARGS (&priv->cl));
  } else {
    hr = priv->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
      (GetGPUDescriptorHandleForHeapStart (desc_handle));

  priv->cl->SetComputeRootSignature (priv->rs.Get ());
  ID3D12DescriptorHeap *heaps[] = { desc_handle };
  priv->cl->SetDescriptorHeaps (1, heaps);

  for (guint field = 0; field < num_fields; field++) {
    for (size_t i = 0; i < priv->contexts.size (); i++) {
      auto ctx = priv->contexts[i];
      ctx->cb_data.primary_line = tff ? 0 : 1;
      ctx->cb_data.is_second = frame_ctx.is_second[field];

      if (ctx->pso)
        priv->cl->SetPipelineState (ctx->pso.Get ());

      priv->cl->SetComputeRootDescriptorTable (0, gpu_handle);
      gpu_handle.Offset (priv->desc_inc_size * 4);

      priv->cl->SetComputeRoot32BitConstants (1, 4, &ctx->cb_data, 0);
      priv->cl->Dispatch (ctx->dispatch_x, ctx->dispatch_y, 1);

      if (priv->post_context) {
        auto barrier =
            CD3DX12_RESOURCE_BARRIER::Transition
            (frame_ctx.out_frames[field].data[0],
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
        priv->cl->ResourceBarrier (1, &barrier);
      }
    }
  }

  if (priv->post_context) {
    auto conv_gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
        (GetGPUDescriptorHandleForHeapStart (conv_desc_handle));
    auto ctx = priv->post_context;

    priv->cl->SetComputeRootSignature (priv->convert_rs.Get ());
    ID3D12DescriptorHeap *conv_heaps[] = { conv_desc_handle };
    priv->cl->SetDescriptorHeaps (1, conv_heaps);
    priv->cl->SetPipelineState (ctx->pso.Get ());

    for (guint field = 0; field < num_fields; field++) {
      auto barrier = CD3DX12_RESOURCE_BARRIER::Transition
          (frame_ctx.out_frames[field].data[0],
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
      priv->cl->ResourceBarrier (1, &barrier);

      priv->cl->SetComputeRootDescriptorTable (0, conv_gpu_handle);
      conv_gpu_handle.Offset (priv->desc_inc_size * 2);
      priv->cl->Dispatch (ctx->dispatch_x, ctx->dispatch_y, 1);
    }
  }

  hr = priv->cl->Close ();

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);
    gst_d3d12_fence_data_unref (fence_data);
    gst_vec_deque_clear (priv->current_queue);
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cmd_list[] = { priv->cl.Get () };
  if (fences_to_wait.empty ()) {
    hr = gst_d3d12_cmd_queue_execute_command_lists (priv->cq,
        1, cmd_list, &priv->fence_val);
  } else {
    hr = gst_d3d12_cmd_queue_execute_command_lists_full (priv->cq,
        fences_to_wait.size (), fences_to_wait.data (),
        fence_values_to_wait.data (), 1, cmd_list, &priv->fence_val);
  }

  gst_d3d12_yadif_unmap_frame_ctx (&frame_ctx);

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_vec_deque_clear (priv->current_queue);
    return GST_FLOW_ERROR;
  }

  gst_d3d12_cmd_queue_set_notify (priv->cq, priv->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  while (!gst_vec_deque_is_empty (priv->current_queue)) {
    auto buf = (GstBuffer *) gst_vec_deque_pop_head (priv->current_queue);
    gst_d3d12_buffer_set_fence (buf, priv->fence.Get (),
        priv->fence_val, FALSE);
    gst_vec_deque_push_tail (priv->output_queue, buf);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_yadif_push_unlocked (GstD3D12Yadif * self, GstBuffer * buffer)
{
  auto priv = self->priv;

  gst_clear_buffer (&priv->prev_buf);

  priv->prev_buf = priv->cur_buf;
  priv->cur_buf = priv->next_buf;
  priv->next_buf = buffer;

  if (!priv->cur_buf)
    priv->cur_buf = gst_buffer_ref (priv->next_buf);

  if (!priv->prev_buf)
    return GST_D3D12_YADIF_FLOW_NEED_DATA;

  return gst_d3d12_yadif_process_frame (self);
}

static GstBuffer *
gst_d3d12_yadif_preproc (GstD3D12Yadif * self, GstBuffer * buffer)
{
  auto priv = self->priv;

  if (!priv->pre_context)
    return buffer;

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (buffer));

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (priv->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  if (!priv->cl) {
    hr = device->CreateCommandList (0, priv->queue_type,
        ca, nullptr, IID_PPV_ARGS (&priv->cl));
  } else {
    hr = priv->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  GstD3D12DescHeap *desc_heap;
  if (!gst_d3d12_desc_heap_pool_acquire (priv->desc_pool, &desc_heap)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (desc_heap));

  GstBuffer *outbuf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->output_pool, &outbuf, nullptr);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire output buffer");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  gst_buffer_copy_into (outbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
  GstD3D12Frame in_frame, out_frame;
  if (!gst_d3d12_frame_map (&in_frame, &priv->origin_info, buffer,
          GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  if (!gst_d3d12_frame_map (&out_frame, &priv->info, outbuf,
          GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_UAV)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_d3d12_frame_unmap (&in_frame);
    gst_d3d12_fence_data_unref (fence_data);
    return nullptr;
  }

  auto desc_handle = gst_d3d12_desc_heap_get_handle (desc_heap);
  auto cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE
      (GetCPUDescriptorHandleForHeapStart (desc_handle));

  device->CopyDescriptorsSimple (1, cpu_handle, in_frame.srv_desc_handle[0],
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cpu_handle.Offset (priv->desc_inc_size);
  device->CopyDescriptorsSimple (1, cpu_handle, out_frame.uav_desc_handle[0],
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  auto gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
      (GetGPUDescriptorHandleForHeapStart (desc_handle));
  priv->cl->SetComputeRootSignature (priv->rs.Get ());

  ID3D12DescriptorHeap *heaps[] = { desc_handle };
  priv->cl->SetDescriptorHeaps (1, heaps);

  auto ctx = priv->pre_context;
  priv->cl->SetPipelineState (ctx->pso.Get ());
  priv->cl->SetComputeRootDescriptorTable (0, gpu_handle);
  priv->cl->Dispatch (ctx->dispatch_x, ctx->dispatch_y, 1);
  hr = priv->cl->Close ();

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_frame_unmap (&in_frame);
    gst_d3d12_frame_unmap (&out_frame);
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  ID3D12CommandList *cmd_list[] = { priv->cl.Get () };
  if (in_frame.fence->fence) {
    hr = gst_d3d12_cmd_queue_execute_command_lists_full (priv->cq,
        1, &in_frame.fence->fence, &in_frame.fence->fence_value,
        1, cmd_list, &priv->fence_val);
  } else {
    hr = gst_d3d12_cmd_queue_execute_command_lists (priv->cq,
        1, cmd_list, &priv->fence_val);
  }

  gst_d3d12_frame_unmap (&in_frame);
  gst_d3d12_frame_unmap (&out_frame);

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  gst_d3d12_cmd_queue_set_notify (priv->cq, priv->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));
  gst_d3d12_buffer_set_fence (outbuf, priv->fence.Get (),
      priv->fence_val, FALSE);

  return outbuf;
}

GstFlowReturn
gst_d3d12_yadif_push (GstD3D12Yadif * yadif, GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_D3D12_YADIF (yadif), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  auto priv = yadif->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  buffer = gst_d3d12_yadif_preproc (yadif, buffer);

  if (!buffer)
    return GST_FLOW_ERROR;

  return gst_d3d12_yadif_push_unlocked (yadif, buffer);
}

GstFlowReturn
gst_d3d12_yadif_pop (GstD3D12Yadif * yadif, GstBuffer ** buffer)
{
  g_return_val_if_fail (GST_IS_D3D12_YADIF (yadif), GST_FLOW_ERROR);
  g_return_val_if_fail (buffer, GST_FLOW_ERROR);

  auto priv = yadif->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  *buffer = nullptr;
  if (gst_vec_deque_is_empty (priv->output_queue))
    return GST_D3D12_YADIF_FLOW_NEED_DATA;

  *buffer = (GstBuffer *) gst_vec_deque_pop_head (priv->output_queue);

  return GST_FLOW_OK;
}

GstFlowReturn
gst_d3d12_yadif_drain (GstD3D12Yadif * yadif)
{
  g_return_val_if_fail (GST_IS_D3D12_YADIF (yadif), GST_FLOW_ERROR);

  auto priv = yadif->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->next_buf) {
    priv->Flush ();

    return GST_D3D12_YADIF_FLOW_NEED_DATA;
  }

  auto next = gst_buffer_copy (priv->next_buf);
  GstClockTime pts = GST_CLOCK_TIME_NONE;
  GstClockTime dur = GST_CLOCK_TIME_NONE;
  if (GST_BUFFER_PTS_IS_VALID (priv->next_buf)) {
    pts = GST_BUFFER_PTS (priv->next_buf);
    if (GST_BUFFER_DURATION_IS_VALID (priv->next_buf)) {
      dur = GST_BUFFER_DURATION (priv->next_buf);
    } else {
      gint fps_n = 30;
      gint fps_d = 1;
      if (priv->info.fps_n > 0 && priv->info.fps_d > 0) {
        fps_n = priv->info.fps_n;
        fps_d = priv->info.fps_d;
      }

      dur = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
    }

    if (!priv->is_forward) {
      if (pts >= dur) {
        pts -= dur;
      } else {
        dur -= pts;
        pts = 0;
      }
    } else {
      pts += dur;
    }
  }

  GST_BUFFER_PTS (next) = pts;
  GST_BUFFER_DURATION (next) = dur;

  auto ret = gst_d3d12_yadif_push_unlocked (yadif, next);
  priv->Flush ();

  return ret;
}

void
gst_d3d12_yadif_flush (GstD3D12Yadif * yadif)
{
  g_return_if_fail (GST_IS_D3D12_YADIF (yadif));

  auto priv = yadif->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->Flush ();
  gst_vec_deque_clear (priv->output_queue);
}
