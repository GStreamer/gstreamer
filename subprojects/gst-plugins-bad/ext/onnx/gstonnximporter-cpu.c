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

#include "gstonnximporter-cpu.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
#define GST_CAT_DEFAULT onnx_inference_debug

struct _GstOnnxImporterCpu
{
  GstOnnxImporter parent;

  const OrtApi *api;
  OrtMemoryInfo *memory_info;

  GstVideoInfo info;
  GstOnnxImporterConfig config;
  gboolean needs_normalization;

  GstVideoFrame frame;

  guint8 *dest;
};

static void gst_onnx_importer_cpu_finalize (GObject * object);
static gboolean gst_onnx_importer_cpu_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config);
static OrtValue *gst_onnx_importer_cpu_prepare (GstOnnxImporter * importer,
    GstBuffer * buffer);
static void gst_onnx_importer_cpu_unprepare (GstOnnxImporter * importer);

#define gst_onnx_importer_cpu_parent_class parent_class
G_DEFINE_TYPE (GstOnnxImporterCpu, gst_onnx_importer_cpu,
    GST_TYPE_ONNX_IMPORTER);

static void
gst_onnx_importer_cpu_class_init (GstOnnxImporterCpuClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstOnnxImporterClass *importer_class = GST_ONNX_IMPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_importer_cpu_finalize;

  importer_class->setup = gst_onnx_importer_cpu_setup;
  importer_class->prepare = gst_onnx_importer_cpu_prepare;
  importer_class->unprepare = gst_onnx_importer_cpu_unprepare;
}

static void
gst_onnx_importer_cpu_init (GstOnnxImporterCpu * self)
{
}

static void
gst_onnx_importer_cpu_reset (GstOnnxImporterCpu * self, gboolean hard)
{
  gst_onnx_importer_cpu_unprepare (GST_ONNX_IMPORTER (self));

  if (hard) {
    self->config.tensor_size = 0;
    g_clear_pointer (&self->dest, g_free);
  }
}

static void
gst_onnx_importer_cpu_finalize (GObject * object)
{
  GstOnnxImporterCpu *self = GST_ONNX_IMPORTER_CPU (object);

  gst_onnx_importer_cpu_reset (self, TRUE);
  if (self->memory_info)
    self->api->ReleaseMemoryInfo (self->memory_info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_onnx_importer_cpu_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config)
{
  GstOnnxImporterCpu *self = GST_ONNX_IMPORTER_CPU (importer);

  gst_onnx_importer_cpu_reset (self, FALSE);

  if (self->config.tensor_size != config->tensor_size)
    self->dest = g_realloc (self->dest, config->tensor_size);

  self->info = *info;
  self->config = *config;
  self->needs_normalization =
      gst_onnx_importer_config_needs_normalization (config, info);

  return TRUE;
}

#define CONVERT_INTERLEAVED_FUNC(name, type, ncomps, clamp)                 \
static void                                                                 \
convert_image_ ##name##_##type (type * dst, const GstVideoFrame * vframe,   \
    const gdouble * scales, const gdouble * offsets)                        \
{                                                                           \
  const type *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);                 \
  gsize height = GST_VIDEO_FRAME_HEIGHT (vframe);                           \
  gsize width = GST_VIDEO_FRAME_WIDTH (vframe);                             \
  gsize stride_elems =                                                      \
      GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) / sizeof (type);             \
  gsize row_elems = width * ncomps;                                         \
                                                                            \
  for (size_t y = 0; y < height; y++) {                                     \
    for (size_t x = 0; x < width; x++) {                                    \
      for (size_t c = 0; c < ncomps; c++)                                   \
        dst[c] = clamp (src[c] * scales[c] + offsets[c]);                   \
      src += ncomps;                                                        \
      dst += ncomps;                                                        \
    }                                                                       \
    src += stride_elems - row_elems;                                        \
  }                                                                         \
}

#define CONVERT_PLANAR_FUNC(name, type, ncomps, clamp)                      \
static void                                                                 \
convert_image_ ##name##_##type (type * dst, const GstVideoFrame * vframe,   \
    const gdouble * scales, const gdouble * offsets)                        \
{                                                                           \
  gsize height = GST_VIDEO_FRAME_HEIGHT (vframe);                           \
  gsize width = GST_VIDEO_FRAME_WIDTH (vframe);                             \
                                                                            \
  for (size_t c = 0; c < ncomps; c++) {                                     \
    gsize stride_elems =                                                    \
        GST_VIDEO_FRAME_PLANE_STRIDE (vframe, c) / sizeof (type);           \
    const type *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, c);               \
    gsize row_elems = width;                                                \
    for (size_t y = 0; y < height; y++) {                                   \
      for (size_t x = 0; x < width; x++) {                                  \
        *dst = clamp (*src * scales[c] + offsets[c]);                       \
        dst++;                                                              \
        src++;                                                              \
      }                                                                     \
      src += stride_elems - row_elems;                                      \
    }                                                                       \
  }                                                                         \
}

#define CLAMP_U8(x) ((uint8_t) CLAMP((x), 0.0, 255.0))
#define CLAMP_F32(x) (x)

CONVERT_INTERLEAVED_FUNC (gray, uint8_t, 1, CLAMP_U8);
CONVERT_INTERLEAVED_FUNC (gray, float, 1, CLAMP_F32);
CONVERT_INTERLEAVED_FUNC (rgb, uint8_t, 3, CLAMP_U8);
CONVERT_INTERLEAVED_FUNC (rgb, float, 3, CLAMP_F32);

CONVERT_PLANAR_FUNC (rgbp, uint8_t, 3, CLAMP_U8);
CONVERT_PLANAR_FUNC (rgbp, float, 3, CLAMP_F32);

static OrtValue *
gst_onnx_importer_cpu_prepare (GstOnnxImporter * importer, GstBuffer * buffer)
{
  GstOnnxImporterCpu *self = GST_ONNX_IMPORTER_CPU (importer);
  OrtStatus *status = NULL;
  const GstVideoInfo *info = &self->info;
  GstVideoFrame *vframe = &self->frame;
  const OrtApi *api = self->api;
  OrtValue *ret = NULL;
  const GstOnnxImporterConfig *config = &self->config;

  gst_onnx_importer_cpu_unprepare (importer);

  if (!gst_video_frame_map (vframe, &self->info, buffer,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (self, "Couldn't map buffer");
    return NULL;
  }

  switch (config->data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
    {
      uint8_t *src_data;

      gboolean needs_conversion = self->needs_normalization;

      /* Check if conversion is needed based on strides / plane offsets.
       * ONNX needs tightly packed data */
      void (*convert) (guint8 * dst, const GstVideoFrame * vframe,
          const gdouble * scales, const gdouble * offsets) = NULL;
      switch (GST_VIDEO_FRAME_FORMAT (&self->frame)) {
        case GST_VIDEO_FORMAT_RGB:
          needs_conversion = needs_conversion
              || GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) != info->width * 3;
          convert = convert_image_rgb_uint8_t;
          break;
        case GST_VIDEO_FORMAT_RGBP:
          needs_conversion = needs_conversion ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) != info->width ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1) != info->width ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 2) != info->width ||
              GST_VIDEO_FRAME_PLANE_DATA (vframe,
              1) != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe,
              0) + info->width * info->height
              || GST_VIDEO_FRAME_PLANE_DATA (vframe,
              2) != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe,
              1) + info->width * info->height;
          convert = convert_image_rgbp_uint8_t;
          break;
        case GST_VIDEO_FORMAT_GRAY8:
          needs_conversion = needs_conversion
              || GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) != info->width;
          convert = convert_image_gray_uint8_t;
          break;
        default:
          g_assert_not_reached ();
          goto error;
      }

      if (!needs_conversion) {
        GST_TRACE_OBJECT (self, "Zero-copy UINT8");
        src_data = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
      } else {
        GST_TRACE_OBJECT (self, "Need UINT8 conversion");
        convert (self->dest, vframe, config->scales, config->offsets);
        src_data = self->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info, src_data,
          config->tensor_size, config->dims,
          config->dims_count, ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, &ret);
      break;
    }
    case GST_TENSOR_DATA_TYPE_FLOAT32:
    {
      float *src_data;
      gboolean needs_conversion = self->needs_normalization;
      void (*convert) (float *dst, const GstVideoFrame * vframe,
          const gdouble * scales, const gdouble * offsets) = NULL;

      switch (GST_VIDEO_FRAME_FORMAT (vframe)) {
        case GST_ONNX_FORMAT_RGB_F32:
          needs_conversion = needs_conversion ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) !=
              info->width * 3 * sizeof (float);
          convert = convert_image_rgb_float;
          break;
        case GST_ONNX_FORMAT_RGBP_F32:
        {
          gsize plane_size = info->width * info->height * sizeof (float);
          needs_conversion = needs_conversion ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) !=
              info->width * sizeof (float) ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1) !=
              info->width * sizeof (float) ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 2) !=
              info->width * sizeof (float) ||
              GST_VIDEO_FRAME_PLANE_DATA (vframe, 1) !=
              (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 0) + plane_size ||
              GST_VIDEO_FRAME_PLANE_DATA (vframe, 2) !=
              (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 1) + plane_size;
          convert = convert_image_rgbp_float;
          break;
        }
        case GST_ONNX_FORMAT_GRAY_F32:
          needs_conversion = needs_conversion ||
              GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) !=
              info->width * sizeof (float);
          convert = convert_image_gray_float;
          break;
        default:
          g_assert_not_reached ();
          goto error;
      }

      if (!needs_conversion) {
        GST_TRACE_OBJECT (self, "Zero-copy F32");
        src_data = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
      } else {
        GST_TRACE_OBJECT (self, "Need F32 conversion");
        convert ((float *) self->dest, vframe, config->scales, config->offsets);
        src_data = (float *) self->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info,
          src_data, config->tensor_size, config->dims,
          config->dims_count, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &ret);
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported input data type");
      goto error;
  }

  if (status)
    goto error;

  return ret;

error:
  if (status) {
    GST_ERROR_OBJECT (self,
        "Couldn't create tensor: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
  }

  gst_onnx_importer_cpu_unprepare (importer);
  return NULL;
}

static void
gst_onnx_importer_cpu_unprepare (GstOnnxImporter * importer)
{
  GstOnnxImporterCpu *self = GST_ONNX_IMPORTER_CPU (importer);

  if (self->frame.buffer)
    gst_video_frame_unmap (&self->frame);

  memset (&self->frame, 0, sizeof (GstVideoFrame));
}

GstOnnxImporter *
gst_onnx_importer_cpu_new (const OrtApi * api)
{
  GstOnnxImporterCpu *self;
  OrtStatus *status;
  OrtMemoryInfo *memory_info;

  status = api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
      &memory_info);
  if (status) {
    GST_ERROR ("Couldn't create memory info: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return NULL;
  }

  self = g_object_new (GST_TYPE_ONNX_IMPORTER_CPU, NULL);

  gst_object_ref_sink (self);
  self->api = api;
  self->memory_info = memory_info;

  return (GstOnnxImporter *) self;
}
