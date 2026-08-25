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

#include "gstonnximporter.h"
#include <math.h>

#define NORMALIZATION_EPSILON_UINT8  0.001
#define NORMALIZATION_EPSILON_FLOAT  1e-6

#define gst_onnx_importer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstOnnxImporter, gst_onnx_importer, GST_TYPE_OBJECT);

static void
gst_onnx_importer_class_init (GstOnnxImporterClass * klass)
{
}

static void
gst_onnx_importer_init (GstOnnxImporter * self)
{
}

gboolean
gst_onnx_importer_setup (GstOnnxImporter * importer, const GstVideoInfo * info,
    const GstOnnxImporterConfig * config)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);

  g_assert (klass->setup);

  return klass->setup (importer, info, config);
}

OrtValue *
gst_onnx_importer_prepare (GstOnnxImporter * importer, GstBuffer * buffer)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);

  g_assert (klass->prepare);

  return klass->prepare (importer, buffer);
}

void
gst_onnx_importer_unprepare (GstOnnxImporter * importer)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);

  g_assert (klass->unprepare);

  klass->unprepare (importer);
}

gboolean
gst_onnx_importer_config_needs_normalization (const GstOnnxImporterConfig *
    config, const GstVideoInfo * info)
{
  guint channels = GST_VIDEO_INFO_N_COMPONENTS (info);
  gdouble epsilon = config->data_type == GST_TENSOR_DATA_TYPE_UINT8 ?
      NORMALIZATION_EPSILON_UINT8 : NORMALIZATION_EPSILON_FLOAT;
  guint i;

  for (i = 0; i < channels; i++) {
    if (fabs (config->scales[i] - 1.0) > epsilon
        || fabs (config->offsets[i]) > epsilon) {
      return TRUE;
    }
  }

  return FALSE;
}
