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

#define GST_ONNX_IMPORTER_MAX_DIMS 4

#define GST_TYPE_ONNX_IMPORTER            (gst_onnx_importer_get_type ())
#define GST_ONNX_IMPORTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ONNX_IMPORTER, GstOnnxImporter))
#define GST_ONNX_IMPORTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ONNX_IMPORTER, GstOnnxImporterClass))
#define GST_IS_ONNX_IMPORTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ONNX_IMPORTER))
#define GST_IS_ONNX_IMPORTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ONNX_IMPORTER))
#define GST_ONNX_IMPORTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ONNX_IMPORTER, GstOnnxImporterClass))

typedef struct _GstOnnxImporter GstOnnxImporter;
typedef struct _GstOnnxImporterClass GstOnnxImporterClass;
typedef struct _GstOnnxImporterPrivate GstOnnxImporterPrivate;
typedef struct _GstOnnxImporterConfig GstOnnxImporterConfig;

struct _GstOnnxImporterConfig
{
  GstTensorDataType data_type;
  gdouble scales[GST_ONNX_IMPORTER_MAX_DIMS];
  gdouble offsets[GST_ONNX_IMPORTER_MAX_DIMS];
  gint64 dims[GST_ONNX_IMPORTER_MAX_DIMS];
  size_t dims_count;
  gsize tensor_size;
};

struct _GstOnnxImporter
{
  GstObject parent;

  const OrtApi *api;

  GstVideoInfo info;
  GstOnnxImporterConfig config;
  ONNXTensorElementDataType data_type_onnx;
  gsize elem_size;
  gboolean needs_normalization;

  GstOnnxImporterPrivate *priv;
};

struct _GstOnnxImporterClass
{
  GstObjectClass parent_class;

  void       (*setup) (GstOnnxImporter * importer,
                       const GstVideoInfo * info,
                       const GstOnnxImporterConfig * config);

  OrtValue * (*prepare) (GstOnnxImporter * importer,
                         GstBuffer * buffer);

  void       (*unprepare) (GstOnnxImporter * importer);
};

GType       gst_onnx_importer_get_type (void);

GstOnnxImporter * gst_onnx_importer_new (const OrtApi * api);

gboolean    gst_onnx_importer_create_system_memory_info (GstOnnxImporter * importer,
                                                         const OrtApi * api);

gboolean    gst_onnx_importer_setup (GstOnnxImporter * importer,
                                     const GstVideoInfo * info,
                                     const GstOnnxImporterConfig * config);

OrtValue *  gst_onnx_importer_prepare (GstOnnxImporter * importer,
                                       GstBuffer * buffer);

void        gst_onnx_importer_unprepare (GstOnnxImporter * importer);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstOnnxImporter, gst_object_unref)

G_END_DECLS
