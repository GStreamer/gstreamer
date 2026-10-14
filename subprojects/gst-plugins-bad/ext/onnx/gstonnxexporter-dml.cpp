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

#include <DirectML.h>
#include <directx/d3dx12.h>
#include "gstonnxexporter-dml.h"
#include <gst/d3d12/gstd3d12-private.h>
#include <dml_provider_factory.h>
#include <wrl.h>
#include <new>

/* *INDENT-OFF* */
G_BEGIN_DECLS
GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
G_END_DECLS

#define GST_CAT_DEFAULT onnx_inference_debug

using namespace Microsoft::WRL;

struct GstOnnxDmlOutput
{
  ONNXTensorElementDataType data_type;
  int64_t *dims;
  size_t dims_count;
  gboolean dynamic;
  gsize size;
  gsize capacity;
  GstD3D12PoolAllocator *pool;
  GstD3D12StagingPoolAllocator *staging_pool;
  GstMemory *memory;
  void *allocation;
};

struct GstOnnxExporterDmlPrivate
{
  const OrtApi *api = nullptr;
  const OrtDmlApi *dml_api = nullptr;
  OrtMemoryInfo *memory_info = nullptr;
  OrtAllocator *allocator = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12FenceDataPool *fence_data_pool = nullptr;
  GstD3D12CmdQueue *compute_queue = nullptr;
  GstD3D12CmdAllocPool *compute_pool = nullptr;
  ComPtr<ID3D12GraphicsCommandList> compute_list;
  GArray *outputs = nullptr;
};

struct _GstOnnxExporterDml
{
  GstOnnxExporter parent;
  GstOnnxExporterDmlPrivate *priv;
};
/* *INDENT-ON* */

static void gst_onnx_exporter_dml_finalize (GObject * object);
static gboolean gst_onnx_exporter_dml_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs);
static GstFlowReturn gst_onnx_exporter_dml_prepare (GstOnnxExporter * exporter,
    guint index, OrtValue ** value, const OrtMemoryInfo ** memory_info);
static GstFlowReturn gst_onnx_exporter_dml_export (GstOnnxExporter * exporter,
    guint index, OrtValue * value, gsize size, GstBuffer ** buffer);
static void gst_onnx_exporter_dml_unprepare (GstOnnxExporter * exporter);
static void clear_dml_output (GstOnnxDmlOutput * output);

#define gst_onnx_exporter_dml_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstOnnxExporterDml, gst_onnx_exporter_dml,
    GST_TYPE_ONNX_EXPORTER);

static void
gst_onnx_exporter_dml_class_init (GstOnnxExporterDmlClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto exporter_class = GST_ONNX_EXPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_exporter_dml_finalize;
  exporter_class->setup = gst_onnx_exporter_dml_setup;
  exporter_class->prepare = gst_onnx_exporter_dml_prepare;
  exporter_class->export_tensor = gst_onnx_exporter_dml_export;
  exporter_class->unprepare = gst_onnx_exporter_dml_unprepare;
}

static void
clear_dml_output (GstOnnxDmlOutput * output)
{
  g_clear_pointer (&output->memory, gst_memory_unref);
  if (output->pool) {
    gst_d3d12_allocator_set_active (GST_D3D12_ALLOCATOR (output->pool), FALSE);
    gst_clear_object (&output->pool);
  }

  if (output->staging_pool) {
    gst_d3d12_staging_allocator_set_active (GST_D3D12_STAGING_ALLOCATOR
        (output->staging_pool), FALSE);
    gst_clear_object (&output->staging_pool);
  }

  g_clear_pointer (&output->dims, g_free);
}

static void
gst_onnx_exporter_dml_init (GstOnnxExporterDml * self)
{
  auto storage = gst_onnx_exporter_dml_get_instance_private (self);
  auto priv = new (storage) GstOnnxExporterDmlPrivate ();
  self->priv = priv;
  priv->fence_data_pool = gst_d3d12_fence_data_pool_new ();
  priv->outputs = g_array_new (FALSE, TRUE, sizeof (GstOnnxDmlOutput));
  g_array_set_clear_func (priv->outputs, (GDestroyNotify) clear_dml_output);
}

static void
gst_onnx_exporter_dml_finalize (GObject * object)
{
  auto self = GST_ONNX_EXPORTER_DML (object);
  auto priv = self->priv;

  gst_onnx_exporter_dml_unprepare (GST_ONNX_EXPORTER (self));
  g_array_unref (priv->outputs);
  gst_clear_object (&priv->fence_data_pool);
  gst_clear_object (&priv->compute_pool);
  if (priv->allocator)
    priv->api->ReleaseAllocator (priv->allocator);
  if (priv->memory_info)
    priv->api->ReleaseMemoryInfo (priv->memory_info);
  gst_clear_object (&priv->device);
  priv->~GstOnnxExporterDmlPrivate ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static inline gboolean
CHECK_ORT (GstOnnxExporterDml * self, OrtStatus * status)
{
  if (!status)
    return TRUE;

  auto priv = self->priv;
  GST_ERROR_OBJECT (self, "%s", priv->api->GetErrorMessage (status));
  priv->api->ReleaseStatus (status);

  return FALSE;
}

static gboolean
ensure_staging_pool (GstOnnxExporterDml * self, GstOnnxDmlOutput * output,
    guint index, gsize size)
{
  auto priv = self->priv;
  if (output->staging_pool && output->capacity >= size)
    return TRUE;

  auto old_capacity = output->capacity;
  if (output->staging_pool) {
    gst_d3d12_staging_allocator_set_active (GST_D3D12_STAGING_ALLOCATOR
        (output->staging_pool), FALSE);
    gst_clear_object (&output->staging_pool);
  }

  output->capacity = 0;
  gsize margin = output->dynamic ?
      MIN (size / 2, GST_ONNX_EXPORTER_MAX_ALLOC_MARGIN) : 0;
  auto capacity = size + MIN (margin, G_MAXUINT - size);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = { };
  layout.Footprint.Format = DXGI_FORMAT_UNKNOWN;
  layout.Footprint.Width = (UINT) capacity;
  layout.Footprint.Height = 1;
  layout.Footprint.Depth = 1;
  layout.Footprint.RowPitch = (UINT) capacity;
  output->staging_pool = gst_d3d12_staging_pool_allocator_new (priv->device,
      1, &layout, capacity);
  if (!output->staging_pool) {
    GST_ERROR_OBJECT (self, "Couldn't create staging pool for output %u",
        index);
    return FALSE;
  }

  if (!gst_d3d12_staging_allocator_set_active (GST_D3D12_STAGING_ALLOCATOR
          (output->staging_pool), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate staging pool for output %u",
        index);
    gst_clear_object (&output->staging_pool);
    return FALSE;
  }

  output->capacity = capacity;
  GST_DEBUG_OBJECT (self, "Created staging pool for output %u, old capacity: %"
      G_GSIZE_FORMAT ", tensor size: %" G_GSIZE_FORMAT ", pool capacity: %"
      G_GSIZE_FORMAT, index, old_capacity, size, capacity);

  return TRUE;
}

static gboolean
gst_onnx_exporter_dml_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs)
{
  auto self = GST_ONNX_EXPORTER_DML (exporter);
  auto priv = self->priv;

  gst_onnx_exporter_dml_unprepare (exporter);
  g_array_set_size (priv->outputs, 0);
  g_array_set_size (priv->outputs, num_outputs);

  for (guint i = 0; i < num_outputs; i++) {
    auto config = &configs[i];
    auto output = &g_array_index (priv->outputs, GstOnnxDmlOutput, i);

    output->data_type = config->data_type;
    output->dims_count = config->dims_count;
    output->dims = (int64_t *) g_memdup2 (config->dims,
        config->dims_count * sizeof (int64_t));
    output->dynamic = config->use_ort_allocation;

    for (size_t j = 0; j < config->dims_count; j++) {
      if (config->dims[j] < 0)
        output->dynamic = TRUE;
    }

    if (output->dynamic)
      continue;

    output->size = config->size;

    if (output->size > G_MAXUINT - (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1)) {
      GST_ERROR_OBJECT (self, "Output %u is too large", i);
      goto error;
    }

    if (!output->size)
      continue;

    {
      CD3DX12_HEAP_PROPERTIES heap (D3D12_HEAP_TYPE_DEFAULT);
      auto desc = CD3DX12_RESOURCE_DESC::Buffer (GST_ROUND_UP_N (output->size,
              D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT),
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
      output->pool = gst_d3d12_pool_allocator_new (priv->device, &heap,
          D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
      if (!output->pool) {
        GST_ERROR_OBJECT (self, "Couldn't create pool for output %u", i);
        goto error;
      }
      if (!gst_d3d12_allocator_set_active (GST_D3D12_ALLOCATOR (output->pool),
              TRUE)) {
        GST_ERROR_OBJECT (self, "Couldn't activate pool for output %u", i);
        goto error;
      }
    }
  }

  return TRUE;

error:
  g_array_set_size (priv->outputs, 0);
  return FALSE;
}

static GstFlowReturn
gst_onnx_exporter_dml_prepare (GstOnnxExporter * exporter, guint index,
    OrtValue ** value, const OrtMemoryInfo ** memory_info)
{
  auto self = GST_ONNX_EXPORTER_DML (exporter);
  auto priv = self->priv;
  auto output = &g_array_index (priv->outputs, GstOnnxDmlOutput, index);

  *memory_info = priv->memory_info;
  if (output->dynamic || !output->size)
    return GST_FLOW_OK;

  auto ret = gst_d3d12_pool_allocator_acquire_memory (output->pool,
      &output->memory);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire memory for output %u", index);
    return ret;
  }

  auto resource =
      gst_d3d12_memory_get_resource_handle (GST_D3D12_MEMORY_CAST
      (output->memory));

  if (!CHECK_ORT (self,
          priv->dml_api->CreateGPUAllocationFromD3DResource (resource,
              &output->allocation))) {
    GST_ERROR_OBJECT (self, "Couldn't wrap resource for output %u", index);
    return GST_FLOW_ERROR;
  }

  if (!CHECK_ORT (self,
          priv->api->CreateTensorWithDataAsOrtValue (priv->memory_info,
              output->allocation, output->size, output->dims,
              output->dims_count, output->data_type, value))) {
    GST_ERROR_OBJECT (self, "Couldn't create output tensor %u", index);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_onnx_exporter_dml_export (GstOnnxExporter * exporter, guint index,
    OrtValue * value, gsize size, GstBuffer ** buffer)
{
  auto self = GST_ONNX_EXPORTER_DML (exporter);
  auto priv = self->priv;
  auto output = &g_array_index (priv->outputs, GstOnnxDmlOutput, index);
  GstMemory *staging = nullptr;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstD3D12FenceData *fence_data = nullptr;
  ComPtr < ID3D12Resource > source;
  const OrtMemoryInfo *memory_info = nullptr;
  OrtMemoryInfoDeviceType device_type;
  void *data = nullptr;
  guint64 fence_value = 0;
  auto queue = priv->compute_queue;
  auto ca_pool = priv->compute_pool;
  auto & cl = priv->compute_list;

  if (!size) {
    *buffer = gst_buffer_new ();
    return GST_FLOW_OK;
  }

  if (!CHECK_ORT (self, priv->api->GetTensorMemoryInfo (value, &memory_info)) ||
      !CHECK_ORT (self, priv->api->GetTensorMutableData (value, &data))) {
    return GST_FLOW_ERROR;
  }

  priv->api->MemoryInfoGetDeviceType (memory_info, &device_type);
  if (!ensure_staging_pool (self, output, index, size))
    return GST_FLOW_ERROR;

  auto ret =
      gst_d3d12_staging_pool_allocator_acquire_memory (output->staging_pool,
      &staging);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire staging memory for output %u",
        index);
    return ret;
  }

  gst_memory_resize (staging, 0, size);
  if (device_type == OrtMemoryInfoDeviceType_CPU) {
    GST_DEBUG_OBJECT (self, "Output %u is in CPU memory, copying to staging",
        index);
    auto result = gst_buffer_new ();
    gst_buffer_append_memory (result, staging);
    if (gst_buffer_fill (result, 0, data, size) != size) {
      GST_ERROR_OBJECT (self, "Couldn't copy CPU output %u", index);
      gst_buffer_unref (result);
      return GST_FLOW_ERROR;
    }

    *buffer = result;
    return GST_FLOW_OK;
  }

  if (output->memory) {
    if (data != output->allocation || size != output->size) {
      GST_ERROR_OBJECT (self, "Output %u does not match prepared memory",
          index);
      goto error;
    }

    source =
        gst_d3d12_memory_get_resource_handle (GST_D3D12_MEMORY_CAST
        (output->memory));
  } else {
    if (!CHECK_ORT (self,
            priv->dml_api->GetD3D12ResourceFromAllocation (priv->allocator,
                data, &source))) {
      GST_ERROR_OBJECT (self, "Couldn't get resource for output %u", index);
      goto error;
    }
  }

  if (!gst_memory_map (staging, &map, GST_MAP_WRITE_D3D12)) {
    GST_ERROR_OBJECT (self, "Couldn't map staging memory for output %u", index);
    goto error;
  }

  if (!gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fence data");
    goto error;
  }

  if (!gst_d3d12_device_prepare_graphics_cmd_list (priv->device,
          cl.GetAddressOf (), ca_pool, nullptr, fence_data)) {
    GST_ERROR_OBJECT (self, "Couldn't prepare output copy command list");
    goto error;
  }

  cl->CopyBufferRegion ((ID3D12Resource *) map.data, 0, source.Get (), 0, size);
  gst_memory_unmap (staging, &map);
  if (!gst_d3d12_result (cl->Close (), priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close output copy command list");
    goto error;
  }

  {
    ID3D12CommandList *lists[] = { cl.Get () };
    auto hr = gst_d3d12_cmd_queue_execute_command_lists (queue,
        1, lists, &fence_value);
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't submit output copy");
      goto error;
    }
  }

  if (output->memory) {
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (g_steal_pointer (&output->memory)));
  } else {
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_COM (source.Detach ()));
  }

  gst_d3d12_cmd_queue_set_notify (queue, fence_value, fence_data,
      (GDestroyNotify) gst_d3d12_fence_data_unref);

  gst_d3d12_staging_memory_set_fence (GST_D3D12_STAGING_MEMORY_CAST (staging),
      gst_d3d12_cmd_queue_get_fence_handle (queue), fence_value, FALSE);

  *buffer = gst_buffer_new ();
  gst_buffer_append_memory (*buffer, staging);

  return GST_FLOW_OK;

error:
  if (map.memory)
    gst_memory_unmap (staging, &map);
  g_clear_pointer (&staging, gst_memory_unref);

  gst_clear_d3d12_fence_data (&fence_data);

  return GST_FLOW_ERROR;
}

static void
gst_onnx_exporter_dml_unprepare (GstOnnxExporter * exporter)
{
  auto self = GST_ONNX_EXPORTER_DML (exporter);
  auto priv = self->priv;

  for (guint i = 0; i < priv->outputs->len; i++) {
    auto output = &g_array_index (priv->outputs, GstOnnxDmlOutput, i);
    if (output->allocation) {
      CHECK_ORT (self, priv->dml_api->FreeGPUAllocation (output->allocation));
      output->allocation = nullptr;
    }
    g_clear_pointer (&output->memory, gst_memory_unref);
  }
}

GstOnnxExporter *
gst_onnx_exporter_dml_new (const OrtApi * api, GstD3D12Device * device,
    OrtSession * session)
{
  auto self = (GstOnnxExporterDml *)
      g_object_new (GST_TYPE_ONNX_EXPORTER_DML, nullptr);
  gst_object_ref_sink (self);
  auto priv = self->priv;
  priv->api = api;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->compute_queue = gst_d3d12_device_get_cmd_queue (device,
      D3D12_COMMAND_LIST_TYPE_COMPUTE);

  auto handle = gst_d3d12_device_get_device_handle (device);
  priv->compute_pool = gst_d3d12_cmd_alloc_pool_new (handle,
      D3D12_COMMAND_LIST_TYPE_COMPUTE);
  if (!priv->compute_pool) {
    GST_ERROR_OBJECT (self, "Couldn't create command allocator pool");
    goto error;
  }

  if (!CHECK_ORT (self, api->GetExecutionProviderApi ("DML", ORT_API_VERSION,
              (const void **) &priv->dml_api))) {
    GST_ERROR_OBJECT (self, "Couldn't get DML API");
    goto error;
  }

  if (!CHECK_ORT (self, api->CreateMemoryInfo ("DML", OrtDeviceAllocator,
              0, OrtMemTypeDefault, &priv->memory_info))) {
    GST_ERROR_OBJECT (self, "Couldn't create DML memory info");
    goto error;
  }

  if (!CHECK_ORT (self, api->CreateAllocator (session, priv->memory_info,
              &priv->allocator))) {
    GST_ERROR_OBJECT (self, "Couldn't get DML allocator");
    goto error;
  }

  return GST_ONNX_EXPORTER (self);

error:
  gst_object_unref (self);
  return nullptr;
}
