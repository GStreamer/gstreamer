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
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_base_inference_debug);
#define GST_CAT_DEFAULT onnx_base_inference_debug

#define NORMALIZATION_EPSILON_UINT8  0.001
#define NORMALIZATION_EPSILON_FLOAT  1e-6

struct _GstOnnxImporterPrivate
{
  OrtMemoryInfo *system_memory_info;
  GstVideoFrame frame;
  guint8 *dest;
  gsize dest_size;
  OrtValue *value;
};

#define gst_onnx_importer_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstOnnxImporter,
    gst_onnx_importer, GST_TYPE_OBJECT);

static void gst_onnx_importer_finalize (GObject * object);
static OrtValue *gst_onnx_importer_prepare_default (GstOnnxImporter * importer,
    GstBuffer * buffer);
static void gst_onnx_importer_unprepare_default (GstOnnxImporter * importer);

static void
gst_onnx_importer_class_init (GstOnnxImporterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstOnnxImporterClass *importer_class = GST_ONNX_IMPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_importer_finalize;

  importer_class->prepare = gst_onnx_importer_prepare_default;
  importer_class->unprepare = gst_onnx_importer_unprepare_default;
}

static void
gst_onnx_importer_init (GstOnnxImporter * self)
{
  self->priv = gst_onnx_importer_get_instance_private (self);
}

static void
gst_onnx_importer_finalize (GObject * object)
{
  GstOnnxImporter *self = GST_ONNX_IMPORTER (object);
  GstOnnxImporterPrivate *priv = self->priv;

  gst_onnx_importer_unprepare_default (self);
  g_clear_pointer (&priv->dest, g_free);

  if (priv->system_memory_info)
    self->api->ReleaseMemoryInfo (priv->system_memory_info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
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

gboolean
gst_onnx_importer_setup (GstOnnxImporter * importer, const GstVideoInfo * info,
    const GstOnnxImporterConfig * config)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);
  GstOnnxImporterPrivate *priv = importer->priv;

  switch (config->data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
      importer->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
      importer->elem_size = sizeof (guint8);
      break;
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      importer->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
      importer->elem_size = sizeof (gfloat);
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  gst_onnx_importer_unprepare_default (importer);

  importer->info = *info;
  importer->config = *config;
  importer->needs_normalization =
      gst_onnx_importer_config_needs_normalization (config, info);

  if (priv->dest_size != config->tensor_size) {
    priv->dest = g_realloc (priv->dest, config->tensor_size);
    priv->dest_size = config->tensor_size;
  }

  if (klass->setup)
    klass->setup (importer, info, config);

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
gst_onnx_importer_prepare_default (GstOnnxImporter * self, GstBuffer * buffer)
{
  GstOnnxImporterPrivate *priv = self->priv;
  OrtStatus *status = NULL;
  const GstVideoInfo *info = &self->info;
  GstVideoFrame *vframe = &priv->frame;
  const OrtApi *api = self->api;
  OrtValue *ret = NULL;
  const GstOnnxImporterConfig *config = &self->config;

  gst_onnx_importer_unprepare_default (self);

  if (!gst_video_frame_map (vframe, &self->info, buffer,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (self, "Couldn't map buffer");
    return NULL;
  }

  switch (self->data_type_onnx) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
    {
      uint8_t *src_data;

      gboolean needs_conversion = self->needs_normalization;

      /* Check if conversion is needed based on strides / plane offsets.
       * ONNX needs tightly packed data */
      void (*convert) (guint8 * dst, const GstVideoFrame * vframe,
          const gdouble * scales, const gdouble * offsets) = NULL;
      switch (GST_VIDEO_FRAME_FORMAT (&priv->frame)) {
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
        convert (priv->dest, vframe, config->scales, config->offsets);
        src_data = priv->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (priv->system_memory_info,
          src_data, config->tensor_size, config->dims,
          config->dims_count, self->data_type_onnx, &ret);
      break;
    }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
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
        convert ((float *) priv->dest, vframe, config->scales, config->offsets);
        src_data = (float *) priv->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (priv->system_memory_info,
          src_data, config->tensor_size, config->dims,
          config->dims_count, self->data_type_onnx, &ret);
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported input data type");
      goto error;
  }

  if (status)
    goto error;

  priv->value = ret;

  return ret;

error:
  if (status) {
    GST_ERROR_OBJECT (self,
        "Couldn't create tensor: %s", api->GetErrorMessage (status));
    api->ReleaseStatus (status);
  }

  gst_onnx_importer_unprepare_default (self);

  return NULL;
}

OrtValue *
gst_onnx_importer_prepare (GstOnnxImporter * importer, GstBuffer * buffer)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);

  g_assert (klass->prepare);

  return klass->prepare (importer, buffer);
}

static void
gst_onnx_importer_unprepare_default (GstOnnxImporter * self)
{
  GstOnnxImporterPrivate *priv = self->priv;

  if (priv->value) {
    self->api->ReleaseValue (priv->value);
    priv->value = NULL;
  }

  if (priv->frame.buffer)
    gst_video_frame_unmap (&priv->frame);
}

void
gst_onnx_importer_unprepare (GstOnnxImporter * importer)
{
  GstOnnxImporterClass *klass = GST_ONNX_IMPORTER_GET_CLASS (importer);

  g_assert (klass->unprepare);

  klass->unprepare (importer);
}

GstOnnxImporter *
gst_onnx_importer_new (const OrtApi * api)
{
  GstOnnxImporter *self = g_object_new (GST_TYPE_ONNX_IMPORTER, NULL);
  gst_object_ref_sink (self);

  if (!gst_onnx_importer_create_system_memory_info (self, api)) {
    gst_object_unref (self);
    return NULL;
  }

  return self;
}

gboolean
gst_onnx_importer_create_system_memory_info (GstOnnxImporter * importer,
    const OrtApi * api)
{
  GstOnnxImporterPrivate *priv = importer->priv;
  OrtStatus *status;
  OrtMemoryInfo *memory_info;

  g_assert (!importer->api);
  g_assert (!priv->system_memory_info);

  status = api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
      &memory_info);
  if (status) {
    GST_ERROR ("Couldn't create memory info: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return FALSE;
  }

  importer->api = api;
  priv->system_memory_info = memory_info;

  return TRUE;
}
