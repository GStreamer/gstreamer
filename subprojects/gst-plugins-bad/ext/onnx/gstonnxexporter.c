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

#include "gstonnxexporter.h"

G_DEFINE_ABSTRACT_TYPE (GstOnnxExporter, gst_onnx_exporter, GST_TYPE_OBJECT);

static void
gst_onnx_exporter_class_init (GstOnnxExporterClass * klass)
{
}

static void
gst_onnx_exporter_init (GstOnnxExporter * self)
{
}

gboolean
gst_onnx_exporter_setup (GstOnnxExporter * exporter,
    const GstOnnxExporterConfig * configs, guint num_outputs)
{
  GstOnnxExporterClass *klass = GST_ONNX_EXPORTER_GET_CLASS (exporter);

  g_assert (klass->setup);

  return klass->setup (exporter, configs, num_outputs);
}

GstFlowReturn
gst_onnx_exporter_prepare (GstOnnxExporter * exporter, guint index,
    OrtValue ** value, const OrtMemoryInfo ** memory_info)
{
  GstOnnxExporterClass *klass = GST_ONNX_EXPORTER_GET_CLASS (exporter);

  *value = NULL;
  *memory_info = NULL;

  g_assert (klass->prepare);

  return klass->prepare (exporter, index, value, memory_info);
}

GstFlowReturn
gst_onnx_exporter_export (GstOnnxExporter * exporter, guint index,
    OrtValue * value, gsize size, GstBuffer ** buffer)
{
  GstOnnxExporterClass *klass = GST_ONNX_EXPORTER_GET_CLASS (exporter);

  *buffer = NULL;

  g_assert (klass->export_tensor);

  return klass->export_tensor (exporter, index, value, size, buffer);
}

void
gst_onnx_exporter_unprepare (GstOnnxExporter * exporter)
{
  GstOnnxExporterClass *klass = GST_ONNX_EXPORTER_GET_CLASS (exporter);

  g_assert (klass->unprepare);

  klass->unprepare (exporter);
}
