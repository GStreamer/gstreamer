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

/*
 * Copyright(c) 2018 Jeremiah van Oosten
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Reference: https://github.com/jpvanoosten/LearningDirectX12 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/d3d12/gstd3d12-private.h>
#include <gst/d3dshader/gstd3dshader.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <algorithm>
#include <vector>

#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_mip_gen_debug);
#define GST_CAT_DEFAULT gst_d3d12_mip_gen_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace DirectX;
/* *INDENT-ON* */

struct GenerateMipsCB
{
  UINT SrcMipLevel;
  UINT NumMipLevels;
  UINT SrcDimension;
  UINT padding;
  XMFLOAT2 TexelSize;
};

struct GstD3D12MipGenPrivate
{
  ~GstD3D12MipGenPrivate ()
  {
    pso = nullptr;
    rs = nullptr;
    gst_clear_object (&desc_pool);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  GstD3D12DescHeapPool *desc_pool = nullptr;
  ComPtr < ID3D12PipelineState > pso;
  ComPtr < ID3D12RootSignature > rs;
  guint desc_inc_size;
  std::vector < D3D12_RESOURCE_STATES > resource_states;
  std::vector < D3D12_RESOURCE_BARRIER > barriers;
};

struct _GstD3D12MipGen
{
  GstObject parent;

  GstD3D12MipGenPrivate *priv;
};
/* *INDENT-ON* */

static void gst_d3d12_mip_gen_finalize (GObject * object);

#define gst_d3d12_mip_gen_parent_class parent_class
G_DEFINE_TYPE (GstD3D12MipGen, gst_d3d12_mip_gen, GST_TYPE_OBJECT);

static void
gst_d3d12_mip_gen_class_init (GstD3D12MipGenClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_mip_gen_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_mip_gen_debug,
      "d3d12mipgen", 0, "d3d12mipgen");
}

static void
gst_d3d12_mip_gen_init (GstD3D12MipGen * self)
{
  self->priv = new GstD3D12MipGenPrivate ();
}

static void
gst_d3d12_mip_gen_finalize (GObject * object)
{
  auto self = GST_D3D12_MIP_GEN (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
get_mipgen_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob_ = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_ROOT_PARAMETER root_params[3];
    CD3DX12_DESCRIPTOR_RANGE range_srv;
    CD3DX12_DESCRIPTOR_RANGE range_uav;
    D3D12_STATIC_SAMPLER_DESC sampler_desc = { };

    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MipLODBias = 0;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_desc.ShaderRegister = 0;
    sampler_desc.RegisterSpace = 0;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[0].InitAsConstants (6, 0, 0);

    range_srv.Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    root_params[1].InitAsDescriptorTable (1, &range_srv);

    range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);
    root_params[2].InitAsDescriptorTable (1, &range_uav);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 3, root_params,
        1, &sampler_desc,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
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

GstD3D12MipGen *
gst_d3d12_mip_gen_new (GstD3D12Device * device, GstD3DPluginCS cs_type)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto self = (GstD3D12MipGen *) g_object_new (GST_TYPE_D3D12_MIP_GEN, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = (GstD3D12Device *) gst_object_ref (device);

  ComPtr < ID3DBlob > rs_blob;
  if (!get_mipgen_rs_blob (device, &rs_blob)) {
    gst_object_unref (self);
    return nullptr;
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  auto hr = device_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&priv->rs));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create root signature");
    gst_object_unref (self);
    return nullptr;
  }

  GstD3DShaderByteCode byte_code;
  if (!gst_d3d_plugin_shader_get_cs_blob (cs_type, GST_D3D_SM_5_0, &byte_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get shader byte code");
    gst_object_unref (self);
    return nullptr;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  pso_desc.pRootSignature = priv->rs.Get ();
  pso_desc.CS.pShaderBytecode = byte_code.byte_code;
  pso_desc.CS.BytecodeLength = byte_code.byte_code_len;
  hr = device_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&priv->pso));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create PSO");
    gst_object_unref (self);
    return nullptr;
  }

  D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = { };
  desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc_heap_desc.NumDescriptors = 5;
  desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  priv->desc_pool = gst_d3d12_desc_heap_pool_new (device_handle,
      &desc_heap_desc);
  if (!priv->desc_pool) {
    GST_ERROR_OBJECT (self, "Couldn't create descriptor pool");
    gst_object_unref (self);
    return nullptr;
  }

  priv->desc_inc_size = device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  return self;
}

static gboolean
gst_d3d12_mip_gen_execute_internal (GstD3D12MipGen * gen,
    ID3D12Resource * resource, GstD3D12FenceData * fence_data,
    ID3D12GraphicsCommandList * cl, guint mip_levels)
{
  auto desc = GetDesc (resource);

  if (desc.MipLevels == 1) {
    GST_LOG_OBJECT (gen, "Single mip level texture");
    return TRUE;
  }

  if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) !=
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ||
      (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ==
      D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) {
    GST_WARNING_OBJECT (gen, "Resource flag is incompatible");
    return FALSE;
  }

  if (mip_levels != 0 && mip_levels < desc.MipLevels)
    desc.MipLevels = (UINT16) mip_levels;

  auto priv = gen->priv;
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  priv->resource_states.resize (desc.MipLevels);
  priv->resource_states[0] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  cl->SetComputeRootSignature (priv->rs.Get ());
  cl->SetPipelineState (priv->pso.Get ());

  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
  DXGI_FORMAT view_format = desc.Format;
  if (desc.Format == DXGI_FORMAT_AYUV)
    view_format = DXGI_FORMAT_R8G8B8A8_UNORM;

  srv_desc.Format = view_format;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = desc.MipLevels;

  for (UINT16 srcMip = 0; srcMip < desc.MipLevels - 1;) {
    guint64 srcWidth = desc.Width >> srcMip;
    guint srcHeight = desc.Height >> srcMip;
    guint dstWidth = static_cast < guint > (srcWidth >> 1);
    guint dstHeight = srcHeight >> 1;
    GenerateMipsCB cbuf;

    // 0b00(0): Both width and height are even.
    // 0b01(1): Width is odd, height is even.
    // 0b10(2): Width is even, height is odd.
    // 0b11(3): Both width and height are odd.
    cbuf.SrcDimension = (srcHeight & 1) << 1 | (srcWidth & 1);

    // How many mipmap levels to compute this pass (max 4 mips per pass)
    DWORD mipCount;

    // The number of times we can half the size of the texture and get
    // exactly a 50% reduction in size.
    // A 1 bit in the width or height indicates an odd dimension.
    // The case where either the width or the height is exactly 1 is handled
    // as a special case (as the dimension does not require reduction).
    _BitScanForward (&mipCount, (dstWidth == 1 ? dstHeight : dstWidth) |
        (dstHeight == 1 ? dstWidth : dstHeight));
    // Maximum number of mips to generate is 4.
    mipCount = std::min < DWORD > (4, mipCount + 1);
    // Clamp to total number of mips left over.
    mipCount = (srcMip + mipCount) >= desc.MipLevels ?
        desc.MipLevels - srcMip - 1 : mipCount;

    // Dimensions should not reduce to 0.
    // This can happen if the width and height are not the same.
    dstWidth = std::max < DWORD > (1, dstWidth);
    dstHeight = std::max < DWORD > (1, dstHeight);

    cbuf.SrcMipLevel = srcMip;
    cbuf.NumMipLevels = mipCount;
    cbuf.TexelSize.x = 1.0f / (float) dstWidth;
    cbuf.TexelSize.y = 1.0f / (float) dstHeight;

    if (srcMip != 0) {
      D3D12_RESOURCE_BARRIER barriers[2];
      barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip);
      barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV (resource);
      cl->ResourceBarrier (2, barriers);

      priv->resource_states[srcMip] =
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    GstD3D12DescHeap *desc_heap;
    if (!gst_d3d12_desc_heap_pool_acquire (priv->desc_pool, &desc_heap)) {
      GST_ERROR_OBJECT (gen, "Couldn't acquire descriptor heap");
      return FALSE;
    }

    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (desc_heap));
    auto desc_handle = gst_d3d12_desc_heap_get_handle (desc_heap);
    auto cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE
        (GetCPUDescriptorHandleForHeapStart (desc_handle));

    device->CreateShaderResourceView (resource, &srv_desc, cpu_handle);

    for (guint mip = 0; mip < mipCount; mip++) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = { };
      uavDesc.Format = view_format;
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      uavDesc.Texture2D.MipSlice = srcMip + mip + 1;

      priv->resource_states[uavDesc.Texture2D.MipSlice] =
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

      cpu_handle.Offset (priv->desc_inc_size);
      device->CreateUnorderedAccessView (resource,
          nullptr, &uavDesc, cpu_handle);
    }

    auto gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
        (GetGPUDescriptorHandleForHeapStart (desc_handle));

    ID3D12DescriptorHeap *heaps[] = { desc_handle };
    cl->SetDescriptorHeaps (1, heaps);
    cl->SetComputeRoot32BitConstants (0, 6, &cbuf, 0);
    cl->SetComputeRootDescriptorTable (1, gpu_handle);
    gpu_handle.Offset (priv->desc_inc_size);
    cl->SetComputeRootDescriptorTable (2, gpu_handle);

    cl->Dispatch ((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);

    srcMip += mipCount;
  }

  return TRUE;
}

gboolean
gst_d3d12_mip_gen_execute (GstD3D12MipGen * gen, ID3D12Resource * resource,
    GstD3D12FenceData * fence_data, ID3D12GraphicsCommandList * cl)
{
  g_return_val_if_fail (GST_IS_D3D12_MIP_GEN (gen), FALSE);
  g_return_val_if_fail (resource, FALSE);
  g_return_val_if_fail (fence_data, FALSE);
  g_return_val_if_fail (cl, FALSE);

  return gst_d3d12_mip_gen_execute_internal (gen, resource, fence_data, cl, 0);
}

gboolean
gst_d3d12_mip_gen_execute_full (GstD3D12MipGen * gen, ID3D12Resource * resource,
    GstD3D12FenceData * fence_data, ID3D12GraphicsCommandList * cl,
    guint mip_levels, D3D12_RESOURCE_STATES state_after)
{
  g_return_val_if_fail (GST_IS_D3D12_MIP_GEN (gen), FALSE);
  g_return_val_if_fail (resource, FALSE);
  g_return_val_if_fail (fence_data, FALSE);
  g_return_val_if_fail (cl, FALSE);

  auto priv = gen->priv;

  if (!gst_d3d12_mip_gen_execute_internal (gen, resource, fence_data, cl, 0))
    return FALSE;

  priv->barriers.resize (0);
  for (size_t i = 1; i < priv->resource_states.size (); i++) {
    auto state_before = priv->resource_states[i];
    if ((state_before & state_after) != state_after) {
      priv->barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              state_before, state_after, i));
    }
  }

  auto size = priv->barriers.size ();
  if (size != 0)
    cl->ResourceBarrier (size, priv->barriers.data ());

  return TRUE;
}
