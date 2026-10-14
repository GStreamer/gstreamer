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

#include "gstonnxexporter-cpu.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
#define GST_CAT_DEFAULT onnx_inference_debug

typedef struct
{
  ONNXTensorElementDataType data_type;
  int64_t *dims;
  size_t dims_count;
  gboolean dynamic;
  gsize size;
  GstBufferPool *pool;
  GstBuffer *buffer;
  GstMapInfo map;
  gboolean prepared;
} GstOnnxCpuOutput;

struct _GstOnnxExporterCpu
{
  GstOnnxExporter parent;

  const OrtApi *api;
  OrtMemoryInfo *memory_info;
  GArray *outputs;
};

static void gst_onnx_exporter_cpu_finalize (GObject * object);
static gboolean gst_onnx_exporter_cpu_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs);
static GstFlowReturn gst_onnx_exporter_cpu_prepare (GstOnnxExporter * exporter,
    guint index, OrtValue ** value, const OrtMemoryInfo ** memory_info);
static GstFlowReturn gst_onnx_exporter_cpu_export (GstOnnxExporter * exporter,
    guint index, OrtValue * value, gsize size, GstBuffer ** buffer);
static void gst_onnx_exporter_cpu_unprepare (GstOnnxExporter * exporter);

#define gst_onnx_exporter_cpu_parent_class parent_class
G_DEFINE_TYPE (GstOnnxExporterCpu, gst_onnx_exporter_cpu,
    GST_TYPE_ONNX_EXPORTER);

static void
gst_onnx_exporter_cpu_class_init (GstOnnxExporterCpuClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstOnnxExporterClass *exporter_class = GST_ONNX_EXPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_exporter_cpu_finalize;

  exporter_class->setup = gst_onnx_exporter_cpu_setup;
  exporter_class->prepare = gst_onnx_exporter_cpu_prepare;
  exporter_class->export_tensor = gst_onnx_exporter_cpu_export;
  exporter_class->unprepare = gst_onnx_exporter_cpu_unprepare;
}

static void
clear_cpu_output (GstOnnxCpuOutput * output)
{
  if (output->map.memory)
    gst_buffer_unmap (output->buffer, &output->map);
  gst_clear_buffer (&output->buffer);

  if (output->pool) {
    gst_buffer_pool_set_active (output->pool, FALSE);
    gst_clear_object (&output->pool);
  }

  g_clear_pointer (&output->dims, g_free);
}

static void
gst_onnx_exporter_cpu_init (GstOnnxExporterCpu * self)
{
  self->outputs = g_array_new (FALSE, TRUE, sizeof (GstOnnxCpuOutput));
  g_array_set_clear_func (self->outputs, (GDestroyNotify) clear_cpu_output);
}

static void
gst_onnx_exporter_cpu_finalize (GObject * object)
{
  GstOnnxExporterCpu *self = GST_ONNX_EXPORTER_CPU (object);

  g_array_unref (self->outputs);
  if (self->memory_info)
    self->api->ReleaseMemoryInfo (self->memory_info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_onnx_exporter_cpu_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs)
{
  GstOnnxExporterCpu *self = GST_ONNX_EXPORTER_CPU (exporter);

  g_array_set_size (self->outputs, 0);
  g_array_set_size (self->outputs, num_outputs);

  for (guint i = 0; i < num_outputs; i++) {
    const GstOnnxExporterConfig *config = &configs[i];
    GstOnnxCpuOutput *output = &g_array_index (self->outputs,
        GstOnnxCpuOutput, i);
    GstStructure *pool_config;

    if ((config->dims_count && !config->dims) ||
        config->dims_count > G_MAXSIZE / sizeof (int64_t)) {
      GST_ERROR_OBJECT (self, "Invalid dimensions for output %u", i);
      goto error;
    }

    output->data_type = config->data_type;
    output->dynamic = config->use_ort_allocation;
    output->dims_count = config->dims_count;
    output->dims = g_memdup2 (config->dims,
        config->dims_count * sizeof (int64_t));

    for (size_t j = 0; j < config->dims_count; j++) {
      if (config->dims[j] < 0)
        output->dynamic = TRUE;
    }

    if (output->dynamic)
      continue;

    output->size = config->size;
    if (!output->size)
      continue;

    output->pool = gst_buffer_pool_new ();
    pool_config = gst_buffer_pool_get_config (output->pool);
    gst_buffer_pool_config_set_params (pool_config, NULL,
        (guint) output->size, 0, 0);
    if (!gst_buffer_pool_set_config (output->pool, pool_config)) {
      GST_ERROR_OBJECT (self, "Couldn't configure pool for output %u", i);
      goto error;
    }
    if (!gst_buffer_pool_set_active (output->pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Couldn't activate pool for output %u", i);
      goto error;
    }
  }

  return TRUE;

error:
  GST_ERROR_OBJECT (self, "Couldn't configure CPU outputs");
  g_array_set_size (self->outputs, 0);
  return FALSE;
}

static GstFlowReturn
gst_onnx_exporter_cpu_prepare (GstOnnxExporter * exporter, guint index,
    OrtValue ** value, const OrtMemoryInfo ** memory_info)
{
  GstOnnxExporterCpu *self = GST_ONNX_EXPORTER_CPU (exporter);
  GstOnnxCpuOutput *output;
  OrtStatus *status;
  GstFlowReturn ret;

  if (index >= self->outputs->len) {
    GST_ERROR_OBJECT (self, "Invalid output index %u", index);
    return GST_FLOW_ERROR;
  }

  output = &g_array_index (self->outputs, GstOnnxCpuOutput, index);
  if (output->prepared) {
    GST_ERROR_OBJECT (self, "Output %u is already prepared", index);
    return GST_FLOW_ERROR;
  }

  output->prepared = TRUE;
  if (output->dynamic) {
    *memory_info = self->memory_info;
    return GST_FLOW_OK;
  }

  if (!output->size) {
    OrtAllocator *allocator;

    status = self->api->GetAllocatorWithDefaultOptions (&allocator);
    if (status) {
      GST_ERROR_OBJECT (self, "Couldn't get default allocator: %s",
          self->api->GetErrorMessage (status));
      self->api->ReleaseStatus (status);
      return GST_FLOW_ERROR;
    }

    status = self->api->CreateTensorAsOrtValue (allocator, output->dims,
        output->dims_count, output->data_type, value);
    if (status) {
      GST_ERROR_OBJECT (self, "Couldn't create empty output tensor: %s",
          self->api->GetErrorMessage (status));
      self->api->ReleaseStatus (status);
      return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
  }

  ret = gst_buffer_pool_acquire_buffer (output->pool, &output->buffer, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!gst_buffer_map (output->buffer, &output->map, GST_MAP_WRITE))
    return GST_FLOW_ERROR;

  status = self->api->CreateTensorWithDataAsOrtValue (self->memory_info,
      output->map.data, output->size, output->dims, output->dims_count,
      output->data_type, value);

  if (status) {
    GST_ERROR_OBJECT (self, "Couldn't create output tensor: %s",
        self->api->GetErrorMessage (status));
    self->api->ReleaseStatus (status);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_onnx_exporter_cpu_export (GstOnnxExporter * exporter, guint index,
    OrtValue * value, gsize size, GstBuffer ** buffer)
{
  GstOnnxExporterCpu *self = GST_ONNX_EXPORTER_CPU (exporter);
  GstOnnxCpuOutput *output;
  const OrtMemoryInfo *memory_info;
  OrtMemoryInfoDeviceType device_type;
  OrtStatus *status = NULL;
  void *data;

  if (index >= self->outputs->len) {
    GST_ERROR_OBJECT (self, "Invalid output index %u", index);
    return GST_FLOW_ERROR;
  }
  output = &g_array_index (self->outputs, GstOnnxCpuOutput, index);
  if (!output->prepared) {
    GST_ERROR_OBJECT (self, "Output %u is not prepared", index);
    return GST_FLOW_ERROR;
  }

  status = self->api->GetTensorMemoryInfo (value, &memory_info);
  if (status) {
    GST_ERROR_OBJECT (self, "Couldn't get output tensor memory info: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  self->api->MemoryInfoGetDeviceType (memory_info, &device_type);
  if (device_type != OrtMemoryInfoDeviceType_CPU) {
    GST_ERROR_OBJECT (self, "Output is not CPU memory");
    return GST_FLOW_ERROR;
  }

  if (!size) {
    if (!output->dynamic && output->size) {
      GST_ERROR_OBJECT (self, "Output does not match the prepared CPU tensor");
      return GST_FLOW_ERROR;
    }

    *buffer = gst_buffer_new ();
    return GST_FLOW_OK;
  }

  status = self->api->GetTensorMutableData (value, &data);
  if (status) {
    GST_ERROR_OBJECT (self, "Couldn't get output tensor data: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  if (!output->dynamic) {
    if (!output->buffer || !output->map.memory || size != output->size ||
        data != output->map.data) {
      GST_ERROR_OBJECT (self, "Output does not match the prepared CPU tensor");
      return GST_FLOW_ERROR;
    }

    gst_buffer_unmap (output->buffer, &output->map);
    output->map = (GstMapInfo) GST_MAP_INFO_INIT;
    *buffer = g_steal_pointer (&output->buffer);
  } else {
    GstBuffer *result;
    GstFlowReturn ret;

    if (!output->pool || output->size < size) {
      GstStructure *pool_config;
      gsize capacity;
      gsize margin;
      gsize old_capacity = output->size;

      if (output->pool) {
        gst_buffer_pool_set_active (output->pool, FALSE);
        gst_clear_object (&output->pool);
      }

      output->size = 0;

      margin = MIN (size / 2, GST_ONNX_EXPORTER_MAX_ALLOC_MARGIN);
      capacity = size + MIN (margin, G_MAXUINT - size);

      output->pool = gst_buffer_pool_new ();
      pool_config = gst_buffer_pool_get_config (output->pool);
      gst_buffer_pool_config_set_params (pool_config, NULL,
          (guint) capacity, 0, 0);
      if (!gst_buffer_pool_set_config (output->pool, pool_config)) {
        GST_ERROR_OBJECT (self, "Couldn't configure pool for output %u", index);
        gst_clear_object (&output->pool);
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_pool_set_active (output->pool, TRUE)) {
        GST_ERROR_OBJECT (self, "Couldn't activate pool for output %u", index);
        gst_clear_object (&output->pool);
        return GST_FLOW_ERROR;
      }

      output->size = capacity;
      GST_DEBUG_OBJECT (self, "Created pool for output %u, old capacity: %"
          G_GSIZE_FORMAT ", tensor size: %" G_GSIZE_FORMAT ", pool capacity: %"
          G_GSIZE_FORMAT, index, old_capacity, size, capacity);
    }

    ret = gst_buffer_pool_acquire_buffer (output->pool, &result, NULL);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Couldn't acquire buffer for output %u", index);
      return ret;
    }

    if (size && gst_buffer_fill (result, 0, data, size) != size) {
      gst_buffer_unref (result);
      return GST_FLOW_ERROR;
    }

    gst_buffer_resize (result, 0, size);
    *buffer = result;
  }

  return GST_FLOW_OK;

error:
  if (status)
    self->api->ReleaseStatus (status);

  return GST_FLOW_ERROR;
}

static void
gst_onnx_exporter_cpu_unprepare (GstOnnxExporter * exporter)
{
  GstOnnxExporterCpu *self = GST_ONNX_EXPORTER_CPU (exporter);

  for (guint i = 0; i < self->outputs->len; i++) {
    GstOnnxCpuOutput *output = &g_array_index (self->outputs,
        GstOnnxCpuOutput, i);

    if (output->map.memory)
      gst_buffer_unmap (output->buffer, &output->map);

    memset (&output->map, 0, sizeof (GstMapInfo));
    gst_clear_buffer (&output->buffer);

    output->prepared = FALSE;
  }
}

GstOnnxExporter *
gst_onnx_exporter_cpu_new (const OrtApi * api)
{
  GstOnnxExporterCpu *self;
  OrtMemoryInfo *memory_info;
  OrtStatus *status;

  status = api->CreateCpuMemoryInfo (OrtDeviceAllocator, OrtMemTypeDefault,
      &memory_info);

  if (status) {
    GST_ERROR ("Couldn't create CPU memory info: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return NULL;
  }

  self = g_object_new (GST_TYPE_ONNX_EXPORTER_CPU, NULL);
  gst_object_ref_sink (self);
  self->api = api;
  self->memory_info = memory_info;

  return GST_ONNX_EXPORTER (self);
}
