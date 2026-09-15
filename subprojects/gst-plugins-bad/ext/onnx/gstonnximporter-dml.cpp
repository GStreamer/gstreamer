/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gstonnximporter-dml.h"
#include <gst/d3d12/gstd3d12-private.h>
#include <gst/d3dshader/gstd3dshadercache.h>
#include <directx/d3dx12.h>
#include <dml_provider_factory.h>
#include <directml.h>
#include <wrl.h>
#include <new>
#include <vector>
#include <string.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

G_BEGIN_DECLS
GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
G_END_DECLS
#define GST_CAT_DEFAULT onnx_inference_debug

struct GstOnnxImporterDmlPrivate
{
  ~GstOnnxImporterDmlPrivate ()
  {
    gst_clear_object (&ca_pool);
    gst_clear_object (&desc_pool);
    if (api && memory_info)
      api->ReleaseMemoryInfo (memory_info);

    gst_clear_object (&device);
  }

  const OrtApi *api = nullptr;
  const OrtDmlApi *dml_api = nullptr;
  OrtMemoryInfo *memory_info = nullptr;
  void *allocation = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12CmdQueue *queue = nullptr;
  ComPtr<IDMLDevice> device_ml;
  GstD3D12Memory *tensor = nullptr;
  ComPtr<ID3D12RootSignature> rs;
  ComPtr<ID3D12PipelineState> texture_pso;
  ComPtr<ID3D12PipelineState> buffer_pso;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12DescHeapPool *desc_pool = nullptr;
  ComPtr<ID3D12GraphicsCommandList> cl;
  GstVideoInfo info = {};
  GstOnnxImporterConfig config = {};
  GstD3D12Frame frame = {};
  ONNXTensorElementDataType data_type_onnx =
      ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
  guint elem_size = 0;
  guint channels = 0;
  guint num_planes = 0;
  gboolean is_packed = FALSE;
  gboolean needs_normalization = FALSE;
};
/* *INDENT-ON* */

struct _GstOnnxImporterDml
{
  GstOnnxImporter parent;
  GstOnnxImporterDmlPrivate *priv;
};

struct GstOnnxDmlConstants
{
  guint width;
  guint height;
  guint dst_stride;
  guint dst_offset;
  guint src_x;
  guint src_y;
  gfloat scale;
  gfloat offset;
  gfloat scale1;
  gfloat offset1;
  gfloat scale2;
  gfloat offset2;
  guint channels;
  guint src_stride;
  guint src_offset;
  guint padding;
};

G_STATIC_ASSERT (sizeof (GstOnnxDmlConstants) % 16 == 0);

static void gst_onnx_importer_dml_finalize (GObject * object);
static gboolean gst_onnx_importer_dml_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config);
static OrtValue *gst_onnx_importer_dml_prepare (GstOnnxImporter * importer,
    GstBuffer * buffer);
static void gst_onnx_importer_dml_unprepare (GstOnnxImporter * importer);

#define gst_onnx_importer_dml_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstOnnxImporterDml, gst_onnx_importer_dml,
    GST_TYPE_ONNX_IMPORTER);

static inline gboolean
CHECK_ORT (GstOnnxImporterDml * self, OrtStatus * status)
{
  if (!status)
    return TRUE;

  GST_ERROR_OBJECT (self, "%s", self->priv->api->GetErrorMessage (status));
  self->priv->api->ReleaseStatus (status);
  return FALSE;
}

static void
gst_onnx_importer_dml_class_init (GstOnnxImporterDmlClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto importer_class = GST_ONNX_IMPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_importer_dml_finalize;
  importer_class->setup = gst_onnx_importer_dml_setup;
  importer_class->prepare = gst_onnx_importer_dml_prepare;
  importer_class->unprepare = gst_onnx_importer_dml_unprepare;
}

static void
gst_onnx_importer_dml_init (GstOnnxImporterDml * self)
{
  auto storage = gst_onnx_importer_dml_get_instance_private (self);
  self->priv = new (storage) GstOnnxImporterDmlPrivate ();
}

static void
gst_onnx_importer_dml_unprepare (GstOnnxImporter * importer)
{
  auto self = GST_ONNX_IMPORTER_DML (importer);
  auto priv = self->priv;

  if (priv->frame.buffer)
    gst_d3d12_frame_unmap (&priv->frame);

  memset (&priv->frame, 0, sizeof (priv->frame));
}

struct GstOnnxDmlAllocation
{
  const OrtApi *api;
  const OrtDmlApi *dml_api;
  void *allocation;
};

static void
gst_onnx_importer_dml_free_allocation (gpointer user_data)
{
  auto data = (GstOnnxDmlAllocation *) user_data;
  auto status = data->dml_api->FreeGPUAllocation (data->allocation);

  if (status) {
    GST_ERROR ("Couldn't free DML allocation: %s",
        data->api->GetErrorMessage (status));
    data->api->ReleaseStatus (status);
  }
  g_free (data);
}

static void
gst_onnx_importer_dml_clear_tensor (GstOnnxImporterDml * self)
{
  auto priv = self->priv;

  if (priv->tensor)
    gst_memory_unref (GST_MEMORY_CAST (priv->tensor));
  priv->allocation = nullptr;
  priv->tensor = nullptr;
}

static void
gst_onnx_importer_dml_finalize (GObject * object)
{
  auto self = GST_ONNX_IMPORTER_DML (object);
  auto priv = self->priv;

  gst_onnx_importer_dml_unprepare (GST_ONNX_IMPORTER (self));
  gst_onnx_importer_dml_clear_tensor (self);

  priv->~GstOnnxImporterDmlPrivate ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
get_dml_rs_blob (GstD3D12Device * device, ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob_ = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    CD3DX12_ROOT_PARAMETER params[2];
    CD3DX12_DESCRIPTOR_RANGE ranges[2];
    ranges[0].Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    ranges[1].Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[0].InitAsConstants (sizeof (GstOnnxDmlConstants) / 4, 0);
    params[1].InitAsDescriptorTable (G_N_ELEMENTS (ranges), ranges);
    CD3DX12_ROOT_SIGNATURE_DESC desc (G_N_ELEMENTS (params), params);

    ComPtr < ID3DBlob > rs_blob;
    ComPtr < ID3DBlob > error_blob;
    auto hr = D3D12SerializeRootSignature (&desc,
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
gst_onnx_importer_dml_create_pipeline (GstOnnxImporterDml * self,
    GstD3DPluginCS shader, ID3D12PipelineState ** pso)
{
  auto priv = self->priv;
  GstD3DShaderByteCode bytecode = { };
  if (!gst_d3d_plugin_shader_get_cs_blob (shader, GST_D3D_SM_5_0, &bytecode)) {
    GST_ERROR_OBJECT (self, "Couldn't get shader blob for shader %d", shader);
    return FALSE;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = { };
  desc.pRootSignature = priv->rs.Get ();
  desc.CS = { bytecode.byte_code, bytecode.byte_code_len };
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto hr = device->CreateComputePipelineState (&desc, IID_PPV_ARGS (pso));

  return gst_d3d12_result (hr, priv->device);
}

static gboolean
gst_onnx_importer_dml_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config)
{
  auto self = GST_ONNX_IMPORTER_DML (importer);
  auto priv = self->priv;
  gst_onnx_importer_dml_unprepare (importer);

  switch (config->data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
      priv->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
      priv->elem_size = sizeof (guint8);
      break;
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      priv->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
      priv->elem_size = sizeof (gfloat);
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  priv->channels = GST_VIDEO_INFO_N_COMPONENTS (info);
  priv->num_planes = GST_VIDEO_INFO_N_PLANES (info);
  priv->is_packed = priv->channels == 3 && priv->num_planes == 1;
  gsize tensor_size = config->tensor_size;
  guint64 row_elements = (guint64) info->width *
      (priv->is_packed ? priv->channels : 1);
  /* add 3 to 255, accounting for up to 3 bytes of alignment mismatch before
   * aligned UAV word */
  guint64 groups = priv->elem_size == 1 ?
      (row_elements + 258) / 256 : (row_elements + 63) / 64;
  if (info->height > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
      groups > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
      tensor_size > G_MAXUINT - (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1)) {
    GST_ERROR_OBJECT (self, "Tensor layout exceeds shader limits");
    return FALSE;
  }

  priv->needs_normalization =
      gst_onnx_importer_config_needs_normalization (config, info);
  priv->texture_pso = nullptr;
  priv->buffer_pso = nullptr;
  auto device = gst_d3d12_device_get_device_handle (priv->device);

  GstD3DPluginCS texture_shader;
  GstD3DPluginCS buffer_shader;
  if (priv->elem_size == 1) {
    texture_shader = priv->needs_normalization ?
        GST_D3D_PLUGIN_CS_TEXTURE_SCALE_2D_U8 :
        GST_D3D_PLUGIN_CS_TEXTURE_COPY_2D_U8;
    buffer_shader = GST_D3D_PLUGIN_CS_BUFFER_SCALE_2D_U8;
  } else {
    texture_shader = priv->needs_normalization ?
        GST_D3D_PLUGIN_CS_TEXTURE_SCALE_2D_F32 :
        GST_D3D_PLUGIN_CS_TEXTURE_COPY_2D_F32;
    buffer_shader = GST_D3D_PLUGIN_CS_BUFFER_SCALE_2D_F32;
  }

  if (!gst_onnx_importer_dml_create_pipeline (self, texture_shader,
          &priv->texture_pso)) {
    GST_ERROR_OBJECT (self, "Couldn't create texture pipeline");
    return FALSE;
  }

  if (!gst_onnx_importer_dml_create_pipeline (self, buffer_shader,
          &priv->buffer_pso)) {
    GST_ERROR_OBJECT (self, "Couldn't create buffer pipeline");
    return FALSE;
  }

  if (!priv->tensor || priv->config.tensor_size != config->tensor_size) {
    gst_onnx_importer_dml_clear_tensor (self);
    CD3DX12_HEAP_PROPERTIES heap (D3D12_HEAP_TYPE_DEFAULT);
    auto size_aligned = GST_ROUND_UP_N (tensor_size,
        D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer (size_aligned,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr < ID3D12Resource > tensor;
    auto hr = device->CreateCommittedResource (&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS (&tensor));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create tensor buffer of size %"
          G_GSIZE_FORMAT, size_aligned);
      return FALSE;
    }

    if (!CHECK_ORT (self,
            priv->dml_api->CreateGPUAllocationFromD3DResource (tensor.Get (),
                &priv->allocation))) {
      GST_ERROR_OBJECT (self,
          "Couldn't create DML allocation for tensor buffer");
      return FALSE;
    }

    auto data = g_new0 (GstOnnxDmlAllocation, 1);
    data->api = priv->api;
    data->dml_api = priv->dml_api;
    data->allocation = priv->allocation;
    auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, priv->device,
        tensor.Get (), 0, data, gst_onnx_importer_dml_free_allocation);
    if (!mem) {
      GST_ERROR_OBJECT (self, "Couldn't wrap tensor buffer");
      gst_onnx_importer_dml_free_allocation (data);
      priv->allocation = nullptr;
      return FALSE;
    }
    priv->tensor = GST_D3D12_MEMORY_CAST (mem);
  }

  priv->info = *info;
  priv->config = *config;

  return TRUE;
}

struct GstOnnxDmlLayout
{
  guint width;
  guint height;
  guint components;
  guint row_size;
  guint plane_size;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[GST_VIDEO_MAX_PLANES];
  gboolean copy_texture[GST_VIDEO_MAX_PLANES];
  gboolean is_buffer[GST_VIDEO_MAX_PLANES];
  D3D12_RESOURCE_DESC descs[GST_VIDEO_MAX_PLANES];
};

static gboolean
gst_onnx_importer_dml_get_layout (GstOnnxImporterDml * self,
    GstOnnxDmlLayout & layout)
{
  auto priv = self->priv;
  auto frame = &priv->frame;
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto tensor = gst_d3d12_memory_get_resource_handle (priv->tensor);

  layout = { };
  layout.width = (guint) priv->info.width;
  layout.height = (guint) priv->info.height;
  layout.components = priv->is_packed ? priv->channels : 1;
  layout.row_size = layout.width * layout.components * priv->elem_size;
  layout.plane_size = layout.row_size * layout.height;

  for (guint p = 0; p < priv->num_planes; p++) {
    if (!frame->data[p]) {
      GST_ERROR_OBJECT (self, "Missing resource for plane %u", p);
      return FALSE;
    }

    auto & desc = layout.descs[p];
    desc = GetDesc (frame->data[p]);
    layout.is_buffer[p] = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    if (layout.is_buffer[p]) {
      guint64 end = (guint64) frame->info.offset[p] +
          (guint64) (layout.height - 1) * frame->info.stride[p] +
          layout.row_size;
      if (frame->info.stride[p] < (gint) layout.row_size
          || frame->info.offset[p] > G_MAXUINT || end > G_MAXUINT - 3
          || (priv->needs_normalization ? GST_ROUND_UP_4 (end) : end) >
          desc.Width || (priv->elem_size == 4
              && ((frame->info.offset[p] | frame->info.stride[p]) & 3))) {
        GST_ERROR_OBJECT (self,
            "Invalid buffer layout for plane %u: " "offset %" G_GSIZE_FORMAT
            ", stride %d, row size %u, buffer size %" G_GUINT64_FORMAT, p,
            frame->info.offset[p], frame->info.stride[p], layout.row_size,
            (guint64) desc.Width);
        return FALSE;
      }
    } else {
      if (priv->is_packed ||
          desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
          desc.SampleDesc.Count != 1 ||
          desc.Format != (priv->elem_size == 1 ?
              DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R32_FLOAT) ||
          frame->plane_rect[p].right < (LONG) layout.width ||
          frame->plane_rect[p].bottom < (LONG) layout.height) {
        GST_ERROR_OBJECT (self, "Unsupported texture layout for plane %u: "
            "dimension %d, format %d, samples %u, rectangle %ld x %ld", p,
            desc.Dimension, desc.Format, desc.SampleDesc.Count,
            frame->plane_rect[p].right, frame->plane_rect[p].bottom);
        return FALSE;
      }

      UINT64 size = 0;
      device->GetCopyableFootprints (&desc, frame->subresource_index[p], 1,
          (UINT64) layout.plane_size * p, &layout.layouts[p], nullptr, nullptr,
          &size);
      layout.copy_texture[p] = !priv->needs_normalization &&
          layout.layouts[p].Offset == (UINT64) layout.plane_size * p &&
          layout.layouts[p].Footprint.RowPitch == layout.row_size &&
          layout.layouts[p].Footprint.Width == layout.width &&
          layout.layouts[p].Footprint.Height == layout.height &&
          size <= GetDesc (tensor).Width;

      if (!layout.copy_texture[p] && desc.DepthOrArraySize != 1) {
        GST_ERROR_OBJECT (self,
            "Shader copy does not support texture arrays for plane %u", p);
        return FALSE;
      }
    }

    if ((priv->needs_normalization ||
            (!layout.is_buffer[p] && !layout.copy_texture[p])) &&
        (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
      GST_WARNING_OBJECT (self,
          "Resource for plane %u denies shader access", p);
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_onnx_importer_dml_dispatch_plane (GstOnnxImporterDml * self,
    const GstOnnxDmlLayout & layout, guint p,
    CD3DX12_CPU_DESCRIPTOR_HANDLE & cpu, CD3DX12_GPU_DESCRIPTOR_HANDLE & gpu,
    guint increment)
{
  auto priv = self->priv;
  auto frame = &priv->frame;
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto cl = priv->cl.Get ();
  auto tensor = gst_d3d12_memory_get_resource_handle (priv->tensor);
  auto source = frame->data[p];
  auto subresource = frame->subresource_index[p];
  auto dst_offset = p * layout.plane_size;

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = { };
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (layout.is_buffer[p]) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.Buffer.NumElements = (UINT) (layout.descs[p].Width / 4);
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  } else {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Format = layout.descs[p].Format;
    srv.Texture2D.MostDetailedMip = subresource;
    srv.Texture2D.MipLevels = 1;
  }
  device->CreateShaderResourceView (source, &srv, cpu);
  cpu.Offset (1, increment);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { };
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.Buffer.NumElements = (UINT) (GetDesc (tensor).Width / 4);
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  device->CreateUnorderedAccessView (tensor, nullptr, &uav, cpu);
  cpu.Offset (1, increment);

  GstOnnxDmlConstants constants = {
    layout.width, layout.height, layout.row_size, dst_offset, 0, 0,
    (gfloat) priv->config.scales[p], (gfloat) priv->config.offsets[p],
    (gfloat) priv->config.scales[1], (gfloat) priv->config.offsets[1],
    (gfloat) priv->config.scales[2], (gfloat) priv->config.offsets[2],
    layout.components, (guint) frame->info.stride[p],
    (guint) frame->info.offset[p], 0,
  };
  cl->SetPipelineState (layout.is_buffer[p] ?
      priv->buffer_pso.Get () : priv->texture_pso.Get ());
  cl->SetComputeRoot32BitConstants (0, sizeof (constants) / 4, &constants, 0);
  cl->SetComputeRootDescriptorTable (1, gpu);
  /* add 3 to 255, accounting for up to 3 bytes of alignment mismatch before
   * aligned UAV word */
  guint groups = priv->elem_size == 1 ?
      (layout.row_size + 258) / 256 : (layout.width * layout.components +
      63) / 64;
  cl->Dispatch (groups, layout.height, 1);
  gpu.Offset (2, increment);
}

static void
gst_onnx_importer_dml_record_commands (GstOnnxImporterDml * self,
    const GstOnnxDmlLayout & layout, GstD3D12DescHeap * desc_heap)
{
  auto priv = self->priv;
  auto frame = &priv->frame;
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto cl = priv->cl.Get ();
  auto tensor = gst_d3d12_memory_get_resource_handle (priv->tensor);
  auto heap_handle = gst_d3d12_desc_heap_get_handle (desc_heap);

  ID3D12DescriptorHeap *heaps[] = { heap_handle };
  cl->SetDescriptorHeaps (1, heaps);
  cl->SetComputeRootSignature (priv->rs.Get ());
  CD3DX12_CPU_DESCRIPTOR_HANDLE
      cpu_handle (GetCPUDescriptorHandleForHeapStart (heap_handle));
  CD3DX12_GPU_DESCRIPTOR_HANDLE
      gpu_handle (GetGPUDescriptorHandleForHeapStart (heap_handle));
  auto increment =
      device->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto tensor_state = D3D12_RESOURCE_STATE_COMMON;

  for (guint p = 0; p < priv->num_planes; p++) {
    auto source = frame->data[p];
    auto subresource = frame->subresource_index[p];
    auto dst_offset = p * layout.plane_size;
    auto copy = layout.copy_texture[p] ||
        (layout.is_buffer[p] && !priv->needs_normalization);
    auto next_state = copy ? D3D12_RESOURCE_STATE_COPY_DEST :
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (tensor_state != D3D12_RESOURCE_STATE_COMMON &&
        tensor_state != next_state) {
      auto barrier = CD3DX12_RESOURCE_BARRIER::Transition (tensor,
          tensor_state, next_state, 0);
      cl->ResourceBarrier (1, &barrier);
    }
    tensor_state = next_state;

    if (copy) {
      if (layout.copy_texture[p]) {
        CD3DX12_TEXTURE_COPY_LOCATION src_loc (source, subresource);
        CD3DX12_TEXTURE_COPY_LOCATION dst_loc (tensor, layout.layouts[p]);
        cl->CopyTextureRegion (&dst_loc, 0, 0, 0, &src_loc, nullptr);
      } else {
        for (guint y = 0; y < layout.height; y++) {
          cl->CopyBufferRegion (tensor,
              (UINT64) dst_offset + y * layout.row_size, source,
              frame->info.offset[p] + (UINT64) y * frame->info.stride[p],
              layout.row_size);
        }
      }
    } else {
      gst_onnx_importer_dml_dispatch_plane (self, layout, p, cpu_handle,
          gpu_handle, increment);
    }
  }
}

static OrtValue *
gst_onnx_importer_dml_prepare (GstOnnxImporter * importer, GstBuffer * buffer)
{
  auto self = GST_ONNX_IMPORTER_DML (importer);
  auto priv = self->priv;

  gst_onnx_importer_dml_unprepare (importer);

  auto n_mem = gst_buffer_n_memory (buffer);
  if (!n_mem) {
    GST_ERROR_OBJECT (self, "Input buffer has no memory");
    return nullptr;
  }

  for (guint i = 0; i < n_mem; i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_d3d12_memory (mem)) {
      GST_DEBUG_OBJECT (self, "Input memory %u is not D3D12 memory", i);
      return nullptr;
    }
    if (!gst_d3d12_device_is_equal (GST_D3D12_MEMORY_CAST (mem)->device,
            priv->device)) {
      GST_DEBUG_OBJECT (self,
          "Input memory %u belongs to a different device", i);
      return nullptr;
    }
  }

  auto frame = &priv->frame;
  if (!gst_d3d12_frame_map (frame, &priv->info, buffer,
          GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR_OBJECT (self, "Couldn't map input D3D12 frame");
    return nullptr;
  }

  auto cl = priv->cl.Get ();
  GstOnnxDmlLayout layout = { };
  GstD3D12FenceData *fence_data = nullptr;
  GstD3D12DescHeap *desc_heap = nullptr;

  if (!gst_onnx_importer_dml_get_layout (self, layout))
    goto error;

  if (!gst_d3d12_device_acquire_fence_data (priv->device, &fence_data)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fence data");
    goto error;
  }

  if (!gst_d3d12_desc_heap_pool_acquire (priv->desc_pool, &desc_heap)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
    goto error;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (desc_heap));
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (buffer)));
  priv->rs->AddRef ();
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_COM (priv->rs.Get ()));
  priv->texture_pso->AddRef ();
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_COM (priv->texture_pso.Get ()));
  priv->buffer_pso->AddRef ();
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_COM (priv->buffer_pso.Get ()));

  if (!gst_d3d12_device_prepare_graphics_cmd_list (priv->device,
          priv->cl.GetAddressOf (), priv->ca_pool, nullptr, fence_data)) {
    GST_ERROR_OBJECT (self, "Couldn't prepare command list");
    goto error;
  }

  cl = priv->cl.Get ();

  gst_onnx_importer_dml_record_commands (self, layout, desc_heap);

  {
    auto hr = cl->Close ();
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't close command list");
      goto error;
    }
  }

  {
    std::vector < ID3D12Fence * >fences;
    std::vector < guint64 > values;
    for (guint p = 0; p < priv->num_planes; p++) {
      if (frame->fence[p].fence) {
        fences.push_back (frame->fence[p].fence);
        values.push_back (frame->fence[p].fence_value);
      }
    }
    ID3D12CommandList *lists[] = { cl };
    guint64 fence_value = 0;
    auto hr = gst_d3d12_cmd_queue_execute_command_lists_full (priv->queue,
        (guint) fences.size (), fences.data (), values.data (),
        1, lists, &fence_value);
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't execute command list");
      goto error;
    }

    gst_d3d12_memory_set_fence (priv->tensor,
        gst_d3d12_cmd_queue_get_fence_handle (priv->queue), fence_value, FALSE);
    gst_d3d12_cmd_queue_set_notify (priv->queue, fence_value, fence_data,
        (GDestroyNotify) gst_d3d12_fence_data_unref);
    fence_data = nullptr;

    OrtValue *ret = nullptr;
    if (CHECK_ORT (self,
            priv->api->CreateTensorWithDataAsOrtValue (priv->memory_info,
                priv->allocation, priv->config.tensor_size, priv->config.dims,
                priv->config.dims_count, priv->data_type_onnx, &ret))) {
      GST_LOG_OBJECT (self, "Prepared DML tensor");
      return ret;
    }

    GST_ERROR_OBJECT (self, "Couldn't create ONNX input tensor");
  }

error:
  if (fence_data)
    gst_d3d12_fence_data_unref (fence_data);

  gst_onnx_importer_dml_unprepare (importer);
  return nullptr;
}

GstOnnxImporter *
gst_onnx_importer_dml_new (const OrtApi * api, GstD3D12Device * device,
    OrtSessionOptions * options)
{
  auto self = (GstOnnxImporterDml *)
      g_object_new (GST_TYPE_ONNX_IMPORTER_DML, nullptr);
  gst_object_ref_sink (self);
  auto priv = self->priv;
  priv->api = api;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->queue = gst_d3d12_device_get_cmd_queue (device,
      D3D12_COMMAND_LIST_TYPE_COMPUTE);

  {
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    ComPtr < ID3DBlob > blob;
    if (!get_dml_rs_blob (device, &blob)) {
      GST_ERROR_OBJECT (self, "Couldn't get root signature blob");
      goto error;
    }

    auto hr = device_handle->CreateRootSignature (0, blob->GetBufferPointer (),
        blob->GetBufferSize (), IID_PPV_ARGS (&priv->rs));
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't create root signature");
      goto error;
    }

    priv->ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_COMPUTE);
    if (!priv->ca_pool) {
      GST_ERROR_OBJECT (self, "Couldn't create command allocator pool");
      goto error;
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = GST_VIDEO_MAX_PLANES * 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    priv->desc_pool = gst_d3d12_desc_heap_pool_new (device_handle, &desc);
    if (!priv->desc_pool) {
      GST_ERROR_OBJECT (self, "Couldn't create descriptor heap pool");
      goto error;
    }
  }

  if (!CHECK_ORT (self, api->GetExecutionProviderApi ("DML", ORT_API_VERSION,
              (const void **) &priv->dml_api))) {
    GST_ERROR_OBJECT (self, "Couldn't get DML execution provider API");
    goto error;
  }

  {
    auto hr = DMLCreateDevice (gst_d3d12_device_get_device_handle (device),
        DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS (&priv->device_ml));
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't create DML device");
      goto error;
    }
  }

  if (!CHECK_ORT (self, api->CreateMemoryInfo ("DML", OrtDeviceAllocator,
              0, OrtMemTypeDefault, &priv->memory_info))) {
    GST_ERROR_OBJECT (self, "Couldn't create DML memory info");
    goto error;
  }

  if (!CHECK_ORT (self, api->SetSessionExecutionMode (options, ORT_SEQUENTIAL))) {
    GST_ERROR_OBJECT (self, "Couldn't set sequential execution mode");
    goto error;
  }

  if (!CHECK_ORT (self, api->DisableMemPattern (options))) {
    GST_ERROR_OBJECT (self, "Couldn't disable memory pattern optimization");
    goto error;
  }

  if (!CHECK_ORT (self,
          priv->dml_api->SessionOptionsAppendExecutionProvider_DML1 (options,
              priv->device_ml.Get (),
              gst_d3d12_cmd_queue_get_handle (priv->queue)))) {
    GST_ERROR_OBJECT (self, "Couldn't append DML execution provider");
    goto error;
  }

  return GST_ONNX_IMPORTER (self);

error:
  gst_object_unref (self);
  return nullptr;
}
