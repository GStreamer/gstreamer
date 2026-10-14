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

#pragma once

#include <gst/gst.h>
#include <onnxruntime_c_api.h>

G_BEGIN_DECLS

/* Leave room for dynamic outputs to grow without recreating the pool,
 * but cap the extra allocation to avoid excessive overhead for large tensors */
#define GST_ONNX_EXPORTER_MAX_ALLOC_MARGIN (4 * 1024 * 1024)

#define GST_TYPE_ONNX_EXPORTER (gst_onnx_exporter_get_type ())
G_DECLARE_DERIVABLE_TYPE (GstOnnxExporter,
    gst_onnx_exporter, GST, ONNX_EXPORTER, GstObject);

typedef struct _GstOnnxExporterConfig
{
  ONNXTensorElementDataType data_type;
  int64_t *dims;
  size_t dims_count;
  /* Validated byte size for static shapes */
  gsize size;
  /* Let ORT allocate outputs that share graph inputs */
  gboolean use_ort_allocation;
} GstOnnxExporterConfig;

struct _GstOnnxExporterClass
{
  GstObjectClass parent_class;

  gboolean (*setup) (GstOnnxExporter * exporter,
                     const GstOnnxExporterConfig * configs,
                     guint num_outputs);

  GstFlowReturn (*prepare) (GstOnnxExporter * exporter,
                            guint index,
                            OrtValue ** value,
                            const OrtMemoryInfo ** memory_info);

  GstFlowReturn (*export_tensor) (GstOnnxExporter * exporter,
                                  guint index,
                                  OrtValue * value,
                                  gsize size,
                                  GstBuffer ** buffer);

  void (*unprepare) (GstOnnxExporter * exporter);
};

gboolean gst_onnx_exporter_setup (GstOnnxExporter * exporter,
                                  const GstOnnxExporterConfig * configs,
                                  guint num_outputs);

GstFlowReturn gst_onnx_exporter_prepare (GstOnnxExporter * exporter,
                                         guint index,
                                         OrtValue ** value,
                                         const OrtMemoryInfo ** memory_info);

GstFlowReturn gst_onnx_exporter_export (GstOnnxExporter * exporter,
                                        guint index,
                                        OrtValue * value,
                                        gsize size,
                                        GstBuffer ** buffer);

void gst_onnx_exporter_unprepare (GstOnnxExporter * exporter);

G_END_DECLS
