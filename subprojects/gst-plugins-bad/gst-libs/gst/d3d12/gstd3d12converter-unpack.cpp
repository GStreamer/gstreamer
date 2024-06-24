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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include <gst/d3dshader/gstd3dshader.h>
#include "gstd3d12converter-unpack.h"
#include <directx/d3dx12.h>
#include <wrl.h>
#include <math.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_converter_debug);
#define GST_CAT_DEFAULT gst_d3d12_converter_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

struct GstD3D12UnpackPrivate
{
  ~GstD3D12UnpackPrivate ()
  {
    if (upload_pool)
      gst_buffer_pool_set_active (upload_pool, FALSE);
    if (output_pool)
      gst_buffer_pool_set_active (output_pool, FALSE);
    gst_clear_object (&upload_pool);
    gst_clear_object (&output_pool);
    gst_clear_object (&desc_pool);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  GstVideoInfo upload_info;
  GstVideoInfo pool_info;

  ComPtr < ID3D12RootSignature > rs_typed;
  ComPtr < ID3D12PipelineState > pso_typed;
  guint tg_x = 0;
  guint tg_y = 0;
  guint x_unit = 8;
  guint y_unit = 8;

  GstD3D12DescriptorPool *desc_pool = nullptr;

  GstBufferPool *upload_pool = nullptr;
  GstBufferPool *output_pool = nullptr;
  bool need_process = false;
  guint heap_inc_size;
};

struct _GstD3D12Unpack
{
  GstObject parent;

  GstD3D12Device *device;
  GstD3D12UnpackPrivate *priv;
};

static void gst_d3d12_unpack_finalize (GObject * object);

#define gst_d3d12_unpack_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Unpack, gst_d3d12_unpack, GST_TYPE_OBJECT);

static void
gst_d3d12_unpack_class_init (GstD3D12UnpackClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_unpack_finalize;
}

static void
gst_d3d12_unpack_init (GstD3D12Unpack * self)
{
  self->priv = new GstD3D12UnpackPrivate ();
}

static void
gst_d3d12_unpack_finalize (GObject * object)
{
  auto self = GST_D3D12_UNPACK (object);

  delete self->priv;
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBufferPool *
gst_d3d12_unpacker_create_pool (GstD3D12Unpack * self,
    const GstVideoInfo * info, D3D12_RESOURCE_FLAGS resource_flags)
{
  auto priv = self->priv;
  auto pool = gst_d3d12_buffer_pool_new (priv->device);
  auto caps = gst_video_info_to_caps (info);
  auto config = gst_buffer_pool_get_config (pool);
  auto params = gst_d3d12_allocation_params_new (priv->device, info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags, D3D12_HEAP_FLAG_NONE);
  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps, priv->out_info.size, 0, 0);
  gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    gst_object_unref (pool);
    return nullptr;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't set active");
    gst_object_unref (pool);
    return nullptr;
  }

  return pool;
}

GstD3D12Unpack *
gst_d3d12_unpack_new (GstD3D12Device * device,
    const GstVideoInfo * converter_input_info)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (converter_input_info, nullptr);

  auto self = (GstD3D12Unpack *) g_object_new (GST_TYPE_D3D12_UNPACK, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;

  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->in_info = *converter_input_info;
  priv->out_info = *converter_input_info;
  priv->upload_info = *converter_input_info;
  priv->pool_info = *converter_input_info;

  auto conv_format = GST_VIDEO_FORMAT_UNKNOWN;
  auto format = GST_VIDEO_INFO_FORMAT (converter_input_info);
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
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
    {
      GstD3D12Format device_format;
      gst_d3d12_device_get_format (device, format, &device_format);
      if (device_format.dxgi_format == DXGI_FORMAT_R16_UINT) {
        conv_format = GST_VIDEO_FORMAT_RGBA;
      } else {
        /* Device supports this format */
        return self;
      }
      break;
    }
    default:
      return self;
  }

  g_assert (conv_format != GST_VIDEO_FORMAT_UNKNOWN);

  priv->need_process = true;
  gst_video_info_set_format (&priv->out_info, conv_format,
      converter_input_info->width, converter_input_info->height);
  priv->out_info.colorimetry = converter_input_info->colorimetry;
  priv->out_info.chroma_site = converter_input_info->chroma_site;
  priv->pool_info = priv->out_info;

  auto dev_handle = gst_d3d12_device_get_device_handle (device);
  priv->heap_inc_size = dev_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = { };
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heap_desc.NumDescriptors = 2;

  priv->desc_pool = gst_d3d12_descriptor_pool_new (dev_handle, &heap_desc);

  D3D12_ROOT_SIGNATURE_FLAGS rs_flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

  D3D_ROOT_SIGNATURE_VERSION rs_version = D3D_ROOT_SIGNATURE_VERSION_1_0;

  CD3DX12_ROOT_PARAMETER param;
  CD3DX12_DESCRIPTOR_RANGE range[2];
  range[0].Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
  range[1].Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

  param.InitAsDescriptorTable (2, range);

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc = { };

  ComPtr < ID3DBlob > rs_blob;
  ComPtr < ID3DBlob > error_blob;
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (rs_desc, 1, &param,
      0, nullptr, rs_flags);
  auto hr = D3DX12SerializeVersionedRootSignature (&rs_desc,
      rs_version, &rs_blob, &error_blob);
  if (!gst_d3d12_result (hr, device)) {
    const gchar *error_msg = nullptr;
    if (error_blob)
      error_msg = (const gchar *) error_blob->GetBufferPointer ();
    GST_ERROR_OBJECT (self,
        "Couldn't serialize root signature, hr: 0x%x, error detail: %s",
        (guint) hr, GST_STR_NULL (error_msg));
    gst_object_unref (self);
    return nullptr;
  }

  hr = dev_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&priv->rs_typed));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Couldn't create root signature");
    gst_object_unref (self);
    return nullptr;
  }

  GstD3DConverterCSByteCode bytecode;
  if (!gst_d3d_converter_shader_get_cs_blob (GST_VIDEO_INFO_FORMAT
          (&priv->in_info), GST_VIDEO_INFO_FORMAT (&priv->out_info),
          GST_D3D_SM_5_0, &bytecode)) {
    GST_ERROR_OBJECT (device, "Couldn't get shader blob");
    gst_object_unref (self);
    return nullptr;
  }

  priv->tg_x = (guint) ceil (priv->in_info.width / (float) bytecode.x_unit);
  priv->tg_y = (guint) ceil (priv->in_info.height / (float) bytecode.y_unit);

  priv->x_unit = bytecode.x_unit;
  priv->y_unit = bytecode.x_unit;

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  pso_desc.pRootSignature = priv->rs_typed.Get ();
  pso_desc.CS.pShaderBytecode = bytecode.byte_code.byte_code;
  pso_desc.CS.BytecodeLength = bytecode.byte_code.byte_code_len;
  hr = dev_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&priv->pso_typed));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create pso");
    gst_object_unref (self);
    return nullptr;
  }

  priv->output_pool = gst_d3d12_unpacker_create_pool (self,
      &priv->pool_info, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
  if (!priv->output_pool) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

gboolean
gst_d3d12_unpack_get_video_info (GstD3D12Unpack * unpack,
    GstVideoInfo * unpack_output_info)
{
  g_return_val_if_fail (GST_IS_D3D12_UNPACK (unpack), FALSE);
  g_return_val_if_fail (unpack_output_info, FALSE);

  auto priv = unpack->priv;
  *unpack_output_info = priv->out_info;

  return TRUE;
}

static gboolean
gst_d3d12_unpack_needs_upload (GstD3D12Unpack * self, GstBuffer * buf)
{
  auto priv = self->priv;
  auto mem = gst_buffer_peek_memory (buf, 0);
  if (!gst_is_d3d12_memory (mem))
    return TRUE;

  auto dmem = GST_D3D12_MEMORY_CAST (mem);
  if (!gst_d3d12_device_is_equal (dmem->device, priv->device))
    return TRUE;

  auto resource = gst_d3d12_memory_get_resource_handle (dmem);
  auto desc = GetDesc (resource);
  if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ==
      D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) {
    return TRUE;
  }

  return FALSE;
}

static GstBuffer *
gst_d3d12_unpack_upload (GstD3D12Unpack * self, GstBuffer * in_buf,
    gint & width, gint & height)
{
  auto priv = self->priv;
  GstBuffer *upload_buf = nullptr;

  auto meta = gst_buffer_get_video_meta (in_buf);
  if (meta) {
    width = meta->width;
    height = meta->height;
  } else {
    width = priv->in_info.width;
    height = priv->in_info.height;
  }

  if (!gst_d3d12_unpack_needs_upload (self, in_buf))
    return gst_buffer_ref (in_buf);

  if (priv->upload_info.width != width || priv->upload_info.height != height) {
    gst_video_info_set_format (&priv->upload_info,
        GST_VIDEO_INFO_FORMAT (&priv->in_info), width, height);
    if (priv->upload_pool) {
      gst_buffer_pool_set_active (priv->upload_pool, FALSE);
      gst_clear_object (&priv->upload_pool);
    }
  }

  if (!priv->upload_pool) {
    priv->upload_pool = gst_d3d12_unpacker_create_pool (self,
        &priv->upload_info, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    if (!priv->upload_pool)
      return nullptr;
  }

  gst_buffer_pool_acquire_buffer (priv->upload_pool, &upload_buf, nullptr);
  if (!upload_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fallback buf");
    return nullptr;
  }

  if (gst_d3d12_buffer_copy_into (upload_buf, in_buf, &priv->upload_info)) {
    GST_ERROR_OBJECT (self, "Couldn't copy to upload buffer");
    gst_buffer_unref (upload_buf);
    return nullptr;
  }

  return upload_buf;
}

static GstBuffer *
gst_d3d12_unpack_create_output (GstD3D12Unpack * self, gint width, gint height)
{
  auto priv = self->priv;
  if (priv->pool_info.width != width || priv->pool_info.height != height) {
    if (priv->output_pool)
      gst_buffer_pool_set_active (priv->output_pool, FALSE);
    gst_clear_object (&priv->output_pool);

    gst_video_info_set_format (&priv->pool_info,
        GST_VIDEO_INFO_FORMAT (&priv->out_info), width, height);
  }

  if (!priv->output_pool) {
    priv->output_pool = gst_d3d12_unpacker_create_pool (self,
        &priv->pool_info, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!priv->output_pool)
      return nullptr;

    priv->tg_x = (guint) ceil (width / (float) priv->x_unit);
    priv->tg_y = (guint) ceil (height / (float) priv->y_unit);
  }

  GstBuffer *outbuf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->output_pool, &outbuf, nullptr);

  return outbuf;
}

GstBuffer *
gst_d3d12_unpack_execute (GstD3D12Unpack * unpack, GstBuffer * buffer,
    GstD3D12FenceData * fence_data, ID3D12GraphicsCommandList * cl)
{
  g_return_val_if_fail (GST_IS_D3D12_UNPACK (unpack), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (fence_data, FALSE);
  g_return_val_if_fail (cl, FALSE);

  auto priv = unpack->priv;
  gint width, height;
  auto upload_buf = gst_d3d12_unpack_upload (unpack, buffer, width, height);
  if (!upload_buf)
    return nullptr;

  if (!priv->need_process)
    return upload_buf;

  auto outbuf = gst_d3d12_unpack_create_output (unpack, width, height);
  if (!outbuf) {
    GST_ERROR_OBJECT (unpack, "Couldn't create output buffer");
    gst_buffer_unref (upload_buf);
    return nullptr;
  }

  GstD3D12Frame in_frame;
  GstD3D12Frame out_frame;
  if (!gst_d3d12_frame_map (&in_frame, &priv->in_info, upload_buf,
          GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (unpack, "Couldn't map input frame");
    gst_buffer_unref (upload_buf);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  if (!gst_d3d12_frame_map (&out_frame, &priv->out_info, outbuf,
          GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_UAV)) {
    GST_ERROR_OBJECT (unpack, "Couldn't map output frame");
    gst_d3d12_frame_unmap (&in_frame);
    gst_buffer_unref (upload_buf);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  GstD3D12Descriptor *descriptor;
  if (!gst_d3d12_descriptor_pool_acquire (priv->desc_pool, &descriptor)) {
    GST_ERROR_OBJECT (unpack, "Couldn't acquire descriptor heap");
    gst_d3d12_frame_unmap (&in_frame);
    gst_buffer_unref (upload_buf);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (descriptor));

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto desc_handle = gst_d3d12_descriptor_get_handle (descriptor);
  auto desc_cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE
      (GetCPUDescriptorHandleForHeapStart (desc_handle));
  device->CopyDescriptorsSimple (1, desc_cpu_handle,
      in_frame.srv_desc_handle[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  desc_cpu_handle.Offset (priv->heap_inc_size);
  device->CopyDescriptorsSimple (1, desc_cpu_handle,
      out_frame.uav_desc_handle[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  cl->SetComputeRootSignature (priv->rs_typed.Get ());
  cl->SetPipelineState (priv->pso_typed.Get ());

  ID3D12DescriptorHeap *heaps[] = { desc_handle };
  cl->SetDescriptorHeaps (1, heaps);
  cl->SetComputeRootDescriptorTable (0,
      GetGPUDescriptorHandleForHeapStart (desc_handle));
  cl->Dispatch (priv->tg_x, priv->tg_y, 1);
  D3D12_RESOURCE_BARRIER barrier =
      CD3DX12_RESOURCE_BARRIER::Transition (out_frame.data[0],
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  cl->ResourceBarrier (1, &barrier);

  gst_d3d12_frame_unmap (&out_frame);
  gst_d3d12_frame_unmap (&in_frame);

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (upload_buf));

  return outbuf;
}
