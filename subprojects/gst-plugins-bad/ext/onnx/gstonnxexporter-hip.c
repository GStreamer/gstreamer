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

#include "gstonnxexporter-hip.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
#define GST_CAT_DEFAULT onnx_inference_debug

typedef struct
{
  gboolean dynamic;
  gsize size;
  gsize capacity;
  GstHipHostPoolAllocator *pool;
} GstOnnxHipOutput;

struct _GstOnnxExporterHip
{
  GstOnnxExporter parent;
  const OrtApi *api;
  OrtMemoryInfo *memory_info;
  GstHipDevice *device;
  GArray *outputs;
};

static void gst_onnx_exporter_hip_finalize (GObject * object);
static gboolean gst_onnx_exporter_hip_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs);
static GstFlowReturn gst_onnx_exporter_hip_prepare (GstOnnxExporter * exporter,
    guint index, OrtValue ** value, const OrtMemoryInfo ** memory_info);
static GstFlowReturn gst_onnx_exporter_hip_export (GstOnnxExporter * exporter,
    guint index, OrtValue * value, gsize size, GstBuffer ** buffer);
static void gst_onnx_exporter_hip_unprepare (GstOnnxExporter * exporter);

#define gst_onnx_exporter_hip_parent_class parent_class
G_DEFINE_TYPE (GstOnnxExporterHip, gst_onnx_exporter_hip,
    GST_TYPE_ONNX_EXPORTER);

static void
gst_onnx_exporter_hip_class_init (GstOnnxExporterHipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstOnnxExporterClass *exporter_class = GST_ONNX_EXPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_exporter_hip_finalize;
  exporter_class->setup = gst_onnx_exporter_hip_setup;
  exporter_class->prepare = gst_onnx_exporter_hip_prepare;
  exporter_class->export_tensor = gst_onnx_exporter_hip_export;
  exporter_class->unprepare = gst_onnx_exporter_hip_unprepare;
}

static void
clear_hip_output (GstOnnxHipOutput * output)
{
  if (output->pool) {
    gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (output->pool),
        FALSE);
    gst_clear_object (&output->pool);
  }
}

static void
gst_onnx_exporter_hip_init (GstOnnxExporterHip * self)
{
  self->outputs = g_array_new (FALSE, TRUE, sizeof (GstOnnxHipOutput));
  g_array_set_clear_func (self->outputs, (GDestroyNotify) clear_hip_output);
}

static void
gst_onnx_exporter_hip_finalize (GObject * object)
{
  GstOnnxExporterHip *self = GST_ONNX_EXPORTER_HIP (object);

  g_array_unref (self->outputs);
  if (self->memory_info)
    self->api->ReleaseMemoryInfo (self->memory_info);
  gst_clear_object (&self->device);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static inline gboolean
CHECK_ORT (GstOnnxExporterHip * self, OrtStatus * status)
{
  if (!status)
    return TRUE;

  GST_ERROR_OBJECT (self, "%s", self->api->GetErrorMessage (status));
  self->api->ReleaseStatus (status);

  return FALSE;
}

static gboolean
ensure_host_pool (GstOnnxExporterHip * self, GstOnnxHipOutput * output,
    guint index, gsize size)
{
  gsize old_capacity = output->capacity;
  gsize margin = 0;
  gsize capacity;

  if (output->pool && output->capacity >= size)
    return TRUE;

  clear_hip_output (output);
  output->capacity = 0;
  if (output->dynamic)
    margin = MIN (size / 2, GST_ONNX_EXPORTER_MAX_ALLOC_MARGIN);

  capacity = size + MIN (margin, G_MAXUINT - size);
  output->pool = gst_hip_host_pool_allocator_new (self->device, capacity, 0);
  if (!output->pool) {
    GST_ERROR_OBJECT (self, "Couldn't create host pool for output %u", index);
    return FALSE;
  }

  if (!gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (output->pool),
          TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate host pool for output %u", index);
    gst_clear_object (&output->pool);
    return FALSE;
  }

  output->capacity = capacity;
  GST_DEBUG_OBJECT (self, "Created host pool for output %u, old capacity: %"
      G_GSIZE_FORMAT ", tensor size: %" G_GSIZE_FORMAT ", pool capacity: %"
      G_GSIZE_FORMAT, index, old_capacity, size, capacity);

  return TRUE;
}

static gboolean
gst_onnx_exporter_hip_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs)
{
  GstOnnxExporterHip *self = GST_ONNX_EXPORTER_HIP (exporter);

  g_array_set_size (self->outputs, 0);
  g_array_set_size (self->outputs, num_outputs);
  for (guint i = 0; i < num_outputs; i++) {
    const GstOnnxExporterConfig *config = &configs[i];
    GstOnnxHipOutput *output = &g_array_index (self->outputs,
        GstOnnxHipOutput, i);

    output->dynamic = config->use_ort_allocation;
    for (size_t j = 0; j < config->dims_count; j++) {
      if (config->dims[j] < 0)
        output->dynamic = TRUE;
    }

    if (output->dynamic)
      continue;

    output->size = config->size;

    if (output->size && !ensure_host_pool (self, output, i, output->size))
      goto error;
  }

  return TRUE;

error:
  g_array_set_size (self->outputs, 0);

  return FALSE;
}

static GstFlowReturn
gst_onnx_exporter_hip_prepare (GstOnnxExporter * exporter, guint index,
    OrtValue ** value, const OrtMemoryInfo ** memory_info)
{
  GstOnnxExporterHip *self = GST_ONNX_EXPORTER_HIP (exporter);

  /* Let ORT allocate device outputs, including dynamic shapes */
  *memory_info = self->memory_info;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_onnx_exporter_hip_export (GstOnnxExporter * exporter, guint index,
    OrtValue * value, gsize size, GstBuffer ** buffer)
{
  GstOnnxExporterHip *self = GST_ONNX_EXPORTER_HIP (exporter);
  GstOnnxHipOutput *output = &g_array_index (self->outputs,
      GstOnnxHipOutput, index);
  const OrtMemoryInfo *memory_info;
  OrtMemoryInfoDeviceType device_type;
  GstMemory *memory = NULL;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstFlowReturn ret;
  void *data;

  if (!output->dynamic && size != output->size) {
    GST_ERROR_OBJECT (self, "Unexpected size for output %u", index);
    return GST_FLOW_ERROR;
  }

  if (!size) {
    *buffer = gst_buffer_new ();
    return GST_FLOW_OK;
  }

  if (!CHECK_ORT (self, self->api->GetTensorMemoryInfo (value, &memory_info)) ||
      !CHECK_ORT (self, self->api->GetTensorMutableData (value, &data))) {
    return GST_FLOW_ERROR;
  }

  self->api->MemoryInfoGetDeviceType (memory_info, &device_type);
  if (!ensure_host_pool (self, output, index, size))
    return GST_FLOW_ERROR;

  ret = gst_hip_host_pool_allocator_acquire_memory (output->pool, &memory);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire host memory for output %u",
        index);
    return ret;
  }

  gst_memory_resize (memory, 0, size);
  if (!gst_memory_map (memory, &map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map host memory for output %u", index);
    goto error;
  }

  if (device_type == OrtMemoryInfoDeviceType_CPU) {
    GST_DEBUG_OBJECT (self,
        "Output %u is in CPU memory, copying to host memory", index);
    memcpy (map.data, data, size);
  } else if (device_type == OrtMemoryInfoDeviceType_GPU) {
    GstHipVendor vendor = gst_hip_device_get_vendor (self->device);
    GstHipStream *device_stream = gst_hip_device_get_stream (self->device);
    hipStream_t stream = gst_hip_stream_get_handle (device_stream);
    hipError_t hip_ret;

    if (!gst_hip_device_set_current (self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't make HIP device current");
      goto error;
    }

    hip_ret = HipMemcpyDtoHAsync (vendor, map.data, (hipDeviceptr_t) data,
        size, stream);
    if (!gst_hip_result (hip_ret, vendor)) {
      GST_ERROR_OBJECT (self, "Couldn't copy output %u to host memory", index);
      goto error;
    }

    hip_ret = HipStreamSynchronize (vendor, stream);
    if (!gst_hip_result (hip_ret, vendor)) {
      GST_ERROR_OBJECT (self, "Couldn't synchronize output copy stream");
      goto error;
    }
  } else {
    GST_ERROR_OBJECT (self, "Unsupported output device type %d", device_type);
    goto error;
  }

  gst_memory_unmap (memory, &map);
  *buffer = gst_buffer_new ();
  gst_buffer_append_memory (*buffer, memory);

  GST_LOG_OBJECT (self, "Copied output %u to host memory", index);

  return GST_FLOW_OK;

error:
  if (map.memory)
    gst_memory_unmap (memory, &map);
  gst_memory_unref (memory);

  return GST_FLOW_ERROR;
}

static void
gst_onnx_exporter_hip_unprepare (GstOnnxExporter * exporter)
{
}

GstOnnxExporter *
gst_onnx_exporter_hip_new (const OrtApi * api, GstHipDevice * device)
{
  GstOnnxExporterHip *self = g_object_new (GST_TYPE_ONNX_EXPORTER_HIP, NULL);

  gst_object_ref_sink (self);
  self->api = api;
  self->device = (GstHipDevice *) gst_object_ref (device);
  if (!CHECK_ORT (self, api->CreateMemoryInfo ("Hip", OrtDeviceAllocator,
              gst_hip_device_get_device_id (device), OrtMemTypeDefault,
              &self->memory_info))) {
    GST_ERROR_OBJECT (self, "Couldn't create HIP memory info");
    gst_object_unref (self);
    return NULL;
  }

  return GST_ONNX_EXPORTER (self);
}
