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
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>
#include <onnxruntime_c_api.h>

G_BEGIN_DECLS

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_ONNX_FORMAT_RGB_F32   GST_VIDEO_FORMAT_RGB_F32LE
#define GST_ONNX_FORMAT_RGBP_F32  GST_VIDEO_FORMAT_RGBP_F32LE
#define GST_ONNX_FORMAT_GRAY_F32  GST_VIDEO_FORMAT_GRAY_F32LE
#else
#define GST_ONNX_FORMAT_RGB_F32   GST_VIDEO_FORMAT_RGB_F32BE
#define GST_ONNX_FORMAT_RGBP_F32  GST_VIDEO_FORMAT_RGBP_F32BE
#define GST_ONNX_FORMAT_GRAY_F32  GST_VIDEO_FORMAT_GRAY_F32BE
#endif

#define GST_TYPE_ONNX_IMPORTER (gst_onnx_importer_get_type ())
G_DECLARE_DERIVABLE_TYPE (GstOnnxImporter,
    gst_onnx_importer, GST, ONNX_IMPORTER, GstObject);

#define GST_ONNX_IMPORTER_MAX_DIMS 4
typedef struct _GstOnnxImporterConfig
{
  GstTensorDataType data_type;
  gdouble scales[GST_ONNX_IMPORTER_MAX_DIMS];
  gdouble offsets[GST_ONNX_IMPORTER_MAX_DIMS];
  gint64 dims[GST_ONNX_IMPORTER_MAX_DIMS];
  size_t dims_count;
  gsize tensor_size;
} GstOnnxImporterConfig;

struct _GstOnnxImporterClass
{
  GstObjectClass parent_class;

  gboolean (*setup) (GstOnnxImporter * importer,
                     const GstVideoInfo * info,
                     const GstOnnxImporterConfig * config);

  OrtValue * (*prepare) (GstOnnxImporter * importer,
                         GstBuffer * buffer);

  void       (*unprepare) (GstOnnxImporter * importer);
};

gboolean gst_onnx_importer_setup (GstOnnxImporter * importer,
                                  const GstVideoInfo * info,
                                  const GstOnnxImporterConfig * config);

OrtValue * gst_onnx_importer_prepare (GstOnnxImporter * importer,
                                      GstBuffer * buffer);

void gst_onnx_importer_unprepare (GstOnnxImporter * importer);

gboolean gst_onnx_importer_config_needs_normalization (const GstOnnxImporterConfig * config,
                                                       const GstVideoInfo * info);

G_END_DECLS
