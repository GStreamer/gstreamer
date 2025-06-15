/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthip-config.h"

#include "gsthip.h"
#include "gsthipconverter.h"
#include "gsthiprtc.h"
#include <hip/hip_runtime.h>
#include <string.h>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include "kernel/converter.cu"
#include "kernel/converter-unpack.cu"

/* *INDENT-OFF* */
#ifdef HIP_AMD_PRECOMPILED
#include "kernel/converter_hsaco.h"
#else
static std::unordered_map<std::string, const unsigned char *> g_precompiled_hsaco_table;
#endif

#ifdef HIP_NVIDIA_PRECOMPILED
#include "kernel/converter_ptx.h"
#else
static std::unordered_map<std::string, const char *> g_precompiled_ptx_table;
#endif

static std::unordered_map<std::string, const char *> g_ptx_table;
static std::mutex g_kernel_table_lock;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_hip_converter_debug);
#define GST_CAT_DEFAULT gst_hip_converter_debug

#define HIP_BLOCK_X 16
#define HIP_BLOCK_Y 16
#define DIV_UP(size,block) (((size) + ((block) - 1)) / (block))

/* from GstD3D11 */
struct GstHipColorMatrix
{
  gdouble matrix[3][3];
  gdouble offset[3];
  gdouble min[3];
  gdouble max[3];
};

static gchar *
gst_hip_dump_color_matrix (GstHipColorMatrix * matrix)
{
  /* *INDENT-OFF* */
  static const gchar format[] =
      "[MATRIX]\n"
      "|% .6f, % .6f, % .6f|\n"
      "|% .6f, % .6f, % .6f|\n"
      "|% .6f, % .6f, % .6f|\n"
      "[OFFSET]\n"
      "|% .6f, % .6f, % .6f|\n"
      "[MIN]\n"
      "|% .6f, % .6f, % .6f|\n"
      "[MAX]\n"
      "|% .6f, % .6f, % .6f|";
  /* *INDENT-ON* */

  return g_strdup_printf (format,
      matrix->matrix[0][0], matrix->matrix[0][1], matrix->matrix[0][2],
      matrix->matrix[1][0], matrix->matrix[1][1], matrix->matrix[1][2],
      matrix->matrix[2][0], matrix->matrix[2][1], matrix->matrix[2][2],
      matrix->offset[0], matrix->offset[1], matrix->offset[2],
      matrix->min[0], matrix->min[1], matrix->min[2],
      matrix->max[0], matrix->max[1], matrix->max[2]);
}

static void
color_matrix_copy (GstHipColorMatrix * dst, const GstHipColorMatrix * src)
{
  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      dst->matrix[i][j] = src->matrix[i][j];
    }
  }
}

static void
color_matrix_multiply (GstHipColorMatrix * dst, GstHipColorMatrix * a,
    GstHipColorMatrix * b)
{
  GstHipColorMatrix tmp;

  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      gdouble val = 0;
      for (guint k = 0; k < 3; k++) {
        val += a->matrix[i][k] * b->matrix[k][j];
      }

      tmp.matrix[i][j] = val;
    }
  }

  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_identity (GstHipColorMatrix * m)
{
  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      if (i == j)
        m->matrix[i][j] = 1.0;
      else
        m->matrix[i][j] = 0;
    }
  }
}

static gboolean
gst_hip_color_range_adjust_matrix_unorm (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstHipColorMatrix * matrix)
{
  gboolean in_rgb, out_rgb;
  gint in_offset[GST_VIDEO_MAX_COMPONENTS];
  gint in_scale[GST_VIDEO_MAX_COMPONENTS];
  gint out_offset[GST_VIDEO_MAX_COMPONENTS];
  gint out_scale[GST_VIDEO_MAX_COMPONENTS];
  GstVideoColorRange in_range;
  GstVideoColorRange out_range;
  gdouble src_fullscale, dst_fullscale;

  memset (matrix, 0, sizeof (GstHipColorMatrix));
  for (guint i = 0; i < 3; i++) {
    matrix->matrix[i][i] = 1.0;
    matrix->matrix[i][i] = 1.0;
    matrix->matrix[i][i] = 1.0;
    matrix->max[i] = 1.0;
  }

  in_rgb = GST_VIDEO_INFO_IS_RGB (in_info);
  out_rgb = GST_VIDEO_INFO_IS_RGB (out_info);

  if (in_rgb != out_rgb) {
    GST_WARNING ("Invalid format conversion");
    return FALSE;
  }

  in_range = in_info->colorimetry.range;
  out_range = out_info->colorimetry.range;

  if (in_range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    GST_WARNING ("Unknown input color range");
    if (in_rgb || GST_VIDEO_INFO_IS_GRAY (in_info))
      in_range = GST_VIDEO_COLOR_RANGE_0_255;
    else
      in_range = GST_VIDEO_COLOR_RANGE_16_235;
  }

  if (out_range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    GST_WARNING ("Unknown output color range");
    if (out_rgb || GST_VIDEO_INFO_IS_GRAY (out_info))
      out_range = GST_VIDEO_COLOR_RANGE_0_255;
    else
      out_range = GST_VIDEO_COLOR_RANGE_16_235;
  }

  src_fullscale = (gdouble) ((1 << in_info->finfo->depth[0]) - 1);
  dst_fullscale = (gdouble) ((1 << out_info->finfo->depth[0]) - 1);

  gst_video_color_range_offsets (in_range, in_info->finfo, in_offset, in_scale);
  gst_video_color_range_offsets (out_range,
      out_info->finfo, out_offset, out_scale);

  matrix->min[0] = matrix->min[1] = matrix->min[2] =
      (gdouble) out_offset[0] / dst_fullscale;

  matrix->max[0] = (out_scale[0] + out_offset[0]) / dst_fullscale;
  matrix->max[1] = matrix->max[2] =
      (out_scale[1] + out_offset[0]) / dst_fullscale;

  if (in_info->colorimetry.range == out_info->colorimetry.range) {
    GST_DEBUG ("Same color range");
    return TRUE;
  }

  /* Formula
   *
   * 1) Scales and offset compensates input to [0..1] range
   * SRC_NORM[i] = (src[i] * src_fullscale - in_offset[i]) / in_scale[i]
   *             = (src[i] * src_fullscale / in_scale[i]) - in_offset[i] / in_scale[i]
   *
   * 2) Reverse to output UNIT scale
   * DST_UINT[i] = SRC_NORM[i] * out_scale[i] + out_offset[i]
   *             = src[i] * src_fullscale * out_scale[i] / in_scale[i]
   *               - in_offset[i] * out_scale[i] / in_scale[i]
   *               + out_offset[i]
   *
   * 3) Back to [0..1] scale
   * dst[i] = DST_UINT[i] / dst_fullscale
   *        = COEFF[i] * src[i] + OFF[i]
   * where
   *             src_fullscale * out_scale[i]
   * COEFF[i] = ------------------------------
   *             dst_fullscale * in_scale[i]
   *
   *            out_offset[i]     in_offset[i] * out_scale[i]
   * OFF[i] =  -------------- -  ------------------------------
   *            dst_fullscale     dst_fullscale * in_scale[i]
   */
  for (guint i = 0; i < 3; i++) {
    matrix->matrix[i][i] = (src_fullscale * out_scale[i]) /
        (dst_fullscale * in_scale[i]);
    matrix->offset[i] = (out_offset[i] / dst_fullscale) -
        ((gdouble) in_offset[i] * out_scale[i] / (dst_fullscale * in_scale[i]));
  }

  return TRUE;
}

static gboolean
gst_hip_yuv_to_rgb_matrix_unorm (const GstVideoInfo * in_yuv_info,
    const GstVideoInfo * out_rgb_info, GstHipColorMatrix * matrix)
{
  gint offset[4], scale[4];
  gdouble Kr, Kb, Kg;

  /*
   * <Formula>
   *
   * Input: Unsigned normalized Y'CbCr(unorm), [0.0..1.0] range
   * Output: Unsigned normalized non-linear R'G'B'(unorm), [0.0..1.0] range
   *
   * 1) Y'CbCr(unorm) to scaled Y'CbCr
   * | Y' |     | Y'(unorm) |
   * | Cb | = S | Cb(unorm) |
   * | Cb |     | Cr(unorm) |
   * where S = (2 ^ bitdepth) - 1
   *
   * 2) Y'CbCr to YPbPr
   * Y  = (Y' - offsetY )    / scaleY
   * Pb = [(Cb - offsetCbCr) / scaleCbCr]
   * Pr = [(Cr - offsetCrCr) / scaleCrCr]
   * =>
   * Y  = Y'(unorm) * Sy  + Oy
   * Pb = Cb(unorm) * Suv + Ouv
   * Pb = Cr(unorm) * Suv + Ouv
   * where
   * Sy  = S / scaleY
   * Suv = S / scaleCbCr
   * Oy  = -(offsetY / scaleY)
   * Ouv = -(offsetCbCr / scaleCbCr)
   *
   * 3) YPbPr to R'G'B'
   * | R' |      | Y  |
   * | G' | = M *| Pb |
   * | B' |      | Pr |
   * where
   *     | vecR |
   * M = | vecG |
   *     | vecB |
   * vecR = | 1,         0           ,       2(1 - Kr)      |
   * vecG = | 1, -(Kb/Kg) * 2(1 - Kb), -(Kr/Kg) * 2(1 - Kr) |
   * vecB = | 1,       2(1 - Kb)     ,          0           |
   * =>
   * R' = dot(vecR, (Syuv * Y'CbCr(unorm))) + dot(vecR, Offset)
   * G' = dot(vecG, (Svuy * Y'CbCr(unorm))) + dot(vecG, Offset)
   * B' = dot(vecB, (Syuv * Y'CbCr(unorm)) + dot(vecB, Offset)
   * where
   *        | Sy,   0,   0 |
   * Syuv = |  0, Suv,   0 |
   *        |  0    0, Suv |
   *
   *          | Oy  |
   * Offset = | Ouv |
   *          | Ouv |
   *
   * 4) YUV -> RGB matrix
   * | R' |            | Y'(unorm) |   | offsetA |
   * | G' | = Matrix * | Cb(unorm) | + | offsetB |
   * | B' |            | Cr(unorm) |   | offsetC |
   *
   * where
   *          | vecR |
   * Matrix = | vecG | * Syuv
   *          | vecB |
   *
   * offsetA = dot(vecR, Offset)
   * offsetB = dot(vecG, Offset)
   * offsetC = dot(vecB, Offset)
   *
   * 4) Consider 16-235 scale RGB
   * RGBfull(0..255) -> RGBfull(16..235) matrix is represented by
   * | Rs |      | Rf |   | Or |
   * | Gs | = Ms | Gf | + | Og |
   * | Bs |      | Bf |   | Ob |
   *
   * Combining all matrix into
   * | Rs |                   | Y'(unorm) |   | offsetA |     | Or |
   * | Gs | = Ms * ( Matrix * | Cb(unorm) | + | offsetB | ) + | Og |
   * | Bs |                   | Cr(unorm) |   | offsetC |     | Ob |
   *
   *                        | Y'(unorm) |      | offsetA |   | Or |
   *        = Ms * Matrix * | Cb(unorm) | + Ms | offsetB | + | Og |
   *                        | Cr(unorm) |      | offsetC |   | Ob |
   */

  memset (matrix, 0, sizeof (GstHipColorMatrix));
  for (guint i = 0; i < 3; i++)
    matrix->max[i] = 1.0;

  gst_video_color_range_offsets (in_yuv_info->colorimetry.range,
      in_yuv_info->finfo, offset, scale);

  if (gst_video_color_matrix_get_Kr_Kb (in_yuv_info->colorimetry.matrix,
          &Kr, &Kb)) {
    guint S;
    gdouble Sy, Suv;
    gdouble Oy, Ouv;
    gdouble vecR[3], vecG[3], vecB[3];

    Kg = 1.0 - Kr - Kb;

    vecR[0] = 1.0;
    vecR[1] = 0;
    vecR[2] = 2 * (1 - Kr);

    vecG[0] = 1.0;
    vecG[1] = -(Kb / Kg) * 2 * (1 - Kb);
    vecG[2] = -(Kr / Kg) * 2 * (1 - Kr);

    vecB[0] = 1.0;
    vecB[1] = 2 * (1 - Kb);
    vecB[2] = 0;

    /* Assume all components has the same bitdepth */
    S = (1 << in_yuv_info->finfo->depth[0]) - 1;
    Sy = (gdouble) S / scale[0];
    Suv = (gdouble) S / scale[1];
    Oy = -((gdouble) offset[0] / scale[0]);
    Ouv = -((gdouble) offset[1] / scale[1]);

    matrix->matrix[0][0] = Sy * vecR[0];
    matrix->matrix[1][0] = Sy * vecG[0];
    matrix->matrix[2][0] = Sy * vecB[0];

    matrix->matrix[0][1] = Suv * vecR[1];
    matrix->matrix[1][1] = Suv * vecG[1];
    matrix->matrix[2][1] = Suv * vecB[1];

    matrix->matrix[0][2] = Suv * vecR[2];
    matrix->matrix[1][2] = Suv * vecG[2];
    matrix->matrix[2][2] = Suv * vecB[2];

    matrix->offset[0] = vecR[0] * Oy + vecR[1] * Ouv + vecR[2] * Ouv;
    matrix->offset[1] = vecG[0] * Oy + vecG[1] * Ouv + vecG[2] * Ouv;
    matrix->offset[2] = vecB[0] * Oy + vecB[1] * Ouv + vecB[2] * Ouv;

    /* Apply RGB range scale matrix */
    if (out_rgb_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
      GstHipColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *out_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_hip_color_range_adjust_matrix_unorm (&full_rgb,
              out_rgb_info, &scale_matrix)) {
        /* Ms * Matrix */
        color_matrix_multiply (&rst, &scale_matrix, matrix);

        /* Ms * transform offsets */
        for (guint i = 0; i < 3; i++) {
          gdouble val = 0;
          for (guint j = 0; j < 3; j++) {
            val += scale_matrix.matrix[i][j] * matrix->offset[j];
          }
          rst.offset[i] = val + scale_matrix.offset[i];
        }

        /* copy back to output matrix */
        for (guint i = 0; i < 3; i++) {
          for (guint j = 0; j < 3; j++) {
            matrix->matrix[i][j] = rst.matrix[i][j];
          }
          matrix->offset[i] = rst.offset[i];
          matrix->min[i] = scale_matrix.min[i];
          matrix->max[i] = scale_matrix.max[i];
        }
      }
    }
  } else {
    /* Unknown matrix */
    matrix->matrix[0][0] = 1.0;
    matrix->matrix[1][1] = 1.0;
    matrix->matrix[2][2] = 1.0;
  }

  return TRUE;
}

static gboolean
gst_hip_rgb_to_yuv_matrix_unorm (const GstVideoInfo * in_rgb_info,
    const GstVideoInfo * out_yuv_info, GstHipColorMatrix * matrix)
{
  gint offset[4], scale[4];
  gdouble Kr, Kb, Kg;

  /*
   * <Formula>
   *
   * Input: Unsigned normalized non-linear R'G'B'(unorm), [0.0..1.0] range
   * Output: Unsigned normalized Y'CbCr(unorm), [0.0..1.0] range
   *
   * 1) R'G'B' to YPbPr
   * | Y  |      | R' |
   * | Pb | = M *| G' |
   * | Pr |      | B' |
   * where
   *     | vecY |
   * M = | vecU |
   *     | vecV |
   * vecY = |       Kr      ,       Kg      ,      Kb       |
   * vecU = | -0.5*Kr/(1-Kb), -0.5*Kg/(1-Kb),     0.5       |
   * vecV = |      0.5      , -0.5*Kg/(1-Kr), -0.5*Kb(1-Kr) |
   *
   * 2) YPbPr to Y'CbCr(unorm)
   * Y'(unorm) = (Y  * scaleY + offsetY)       / S
   * Cb(unorm) = (Pb * scaleCbCr + offsetCbCr) / S
   * Cr(unorm) = (Pr * scaleCbCr + offsetCbCr) / S
   * =>
   * Y'(unorm) = (Y  * scaleY    / S) + (offsetY    / S)
   * Cb(unorm) = (Pb * scaleCbCr / S) + (offsetCbCr / S)
   * Cr(unorm) = (Pb * scaleCbCr / S) + (offsetCbCr / S)
   * where S = (2 ^ bitdepth) - 1
   *
   * 3) RGB -> YUV matrix
   * | Y'(unorm) |            | R' |   | offsetA |
   * | Cb(unorm) | = Matrix * | G' | + | offsetB |
   * | Cr(unorm) |            | B' |   | offsetC |
   *
   * where
   *          | (scaleY/S)    * vecY |
   * Matrix = | (scaleCbCr/S) * vecU |
   *          | (scaleCbCr/S) * vecV |
   *
   * offsetA = offsetY    / S
   * offsetB = offsetCbCr / S
   * offsetC = offsetCbCr / S
   *
   * 4) Consider 16-235 scale RGB
   * RGBstudio(16..235) -> RGBfull(0..255) matrix is represented by
   * | Rf |      | Rs |   | Or |
   * | Gf | = Ms | Gs | + | Og |
   * | Bf |      | Bs |   | Ob |
   *
   * Combining all matrix into
   * | Y'(unorm) |                 | Rs |   | Or |     | offsetA |
   * | Cb(unorm) | = Matrix * ( Ms | Gs | + | Og | ) + | offsetB |
   * | Cr(unorm) |                 | Bs |   | Ob |     | offsetC |
   *
   *                             | Rs |          | Or |   | offsetA |
   *               = Matrix * Ms | Gs | + Matrix | Og | + | offsetB |
   *                             | Bs |          | Ob |   | offsetB |
   */

  memset (matrix, 0, sizeof (GstHipColorMatrix));
  for (guint i = 0; i < 3; i++)
    matrix->max[i] = 1.0;

  gst_video_color_range_offsets (out_yuv_info->colorimetry.range,
      out_yuv_info->finfo, offset, scale);

  if (gst_video_color_matrix_get_Kr_Kb (out_yuv_info->colorimetry.matrix,
          &Kr, &Kb)) {
    guint S;
    gdouble Sy, Suv;
    gdouble Oy, Ouv;
    gdouble vecY[3], vecU[3], vecV[3];

    Kg = 1.0 - Kr - Kb;

    vecY[0] = Kr;
    vecY[1] = Kg;
    vecY[2] = Kb;

    vecU[0] = -0.5 * Kr / (1 - Kb);
    vecU[1] = -0.5 * Kg / (1 - Kb);
    vecU[2] = 0.5;

    vecV[0] = 0.5;
    vecV[1] = -0.5 * Kg / (1 - Kr);
    vecV[2] = -0.5 * Kb / (1 - Kr);

    /* Assume all components has the same bitdepth */
    S = (1 << out_yuv_info->finfo->depth[0]) - 1;
    Sy = (gdouble) scale[0] / S;
    Suv = (gdouble) scale[1] / S;
    Oy = (gdouble) offset[0] / S;
    Ouv = (gdouble) offset[1] / S;

    for (guint i = 0; i < 3; i++) {
      matrix->matrix[0][i] = Sy * vecY[i];
      matrix->matrix[1][i] = Suv * vecU[i];
      matrix->matrix[2][i] = Suv * vecV[i];
    }

    matrix->offset[0] = Oy;
    matrix->offset[1] = Ouv;
    matrix->offset[2] = Ouv;

    matrix->min[0] = Oy;
    matrix->min[1] = Oy;
    matrix->min[2] = Oy;

    matrix->max[0] = ((gdouble) scale[0] + offset[0]) / S;
    matrix->max[1] = ((gdouble) scale[1] + offset[0]) / S;
    matrix->max[2] = ((gdouble) scale[1] + offset[0]) / S;

    /* Apply RGB range scale matrix */
    if (in_rgb_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
      GstHipColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *in_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_hip_color_range_adjust_matrix_unorm (in_rgb_info,
              &full_rgb, &scale_matrix)) {
        /* Matrix * Ms */
        color_matrix_multiply (&rst, matrix, &scale_matrix);

        /* Matrix * scale offsets */
        for (guint i = 0; i < 3; i++) {
          gdouble val = 0;
          for (guint j = 0; j < 3; j++) {
            val += matrix->matrix[i][j] * scale_matrix.offset[j];
          }
          rst.offset[i] = val + matrix->offset[i];
        }

        /* copy back to output matrix */
        for (guint i = 0; i < 3; i++) {
          for (guint j = 0; j < 3; j++) {
            matrix->matrix[i][j] = rst.matrix[i][j];
          }
          matrix->offset[i] = rst.offset[i];
        }
      }
    }
  } else {
    /* Unknown matrix */
    matrix->matrix[0][0] = 1.0;
    matrix->matrix[1][1] = 1.0;
    matrix->matrix[2][2] = 1.0;
  }

  return TRUE;
}

struct ColorMatrix
{
  float coeffX[3];
  float coeffY[3];
  float coeffZ[3];
  float offset[3];
  float min[3];
  float max[3];
};

struct ConstBuffer
{
  ColorMatrix convert_matrix;
  int width;
  int height;
  int left;
  int top;
  int right;
  int bottom;
  int view_width;
  int view_height;
  float border_x;
  float border_y;
  float border_z;
  float border_w;
  int fill_border;
  int video_direction;
  float alpha;
  int do_blend;
  int do_convert;
};

#define COLOR_SPACE_IDENTITY "color_space_identity"
#define COLOR_SPACE_CONVERT "color_space_convert"

#define SAMPLE_YUV_PLANAR "I420"
#define SAMPLE_YV12 "YV12"
#define SAMPLE_YUV_PLANAR_10BIS "I420_10"
#define SAMPLE_YUV_PLANAR_12BIS "I420_12"
#define SAMPLE_SEMI_PLANAR "NV12"
#define SAMPLE_SEMI_PLANAR_SWAP "NV21"
#define SAMPLE_RGBA "RGBA"
#define SAMPLE_BGRA "BGRA"
#define SAMPLE_RGBx "RGBx"
#define SAMPLE_BGRx "BGRx"
#define SAMPLE_ARGB "ARGB"
/* same as ARGB */
#define SAMPLE_ABGR "ABGR"
#define SAMPLE_RGBP "RGBP"
#define SAMPLE_BGRP "BGRP"
#define SAMPLE_GBR "GBR"
#define SAMPLE_GBR_10 "GBR_10"
#define SAMPLE_GBR_12 "GBR_12"
#define SAMPLE_GBRA "GBRA"
#define SAMPLE_VUYA "VUYA"

typedef struct _TextureFormat
{
  GstVideoFormat format;
  hipArray_Format array_format[GST_VIDEO_MAX_COMPONENTS];
  guint channels[GST_VIDEO_MAX_COMPONENTS];
  const gchar *sample_func;
} TextureFormat;

#define HIP_AD_FORMAT_NONE ((hipArray_Format)0)
#define MAKE_FORMAT_YUV_PLANAR(f,cf,sample_func) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE },  {1, 1, 1, 0}, sample_func }
#define MAKE_FORMAT_YUV_SEMI_PLANAR(f,cf,sample_func) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {1, 2, 0, 0}, sample_func }
#define MAKE_FORMAT_RGB(f,cf,sample_func) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {4, 0, 0, 0}, sample_func }
#define MAKE_FORMAT_RGBP(f,cf,sample_func) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE }, {1, 1, 1, 0}, sample_func }
#define MAKE_FORMAT_RGBAP(f,cf,sample_func) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf }, {1, 1, 1, 1}, sample_func }

static const TextureFormat format_map[] = {
  MAKE_FORMAT_YUV_PLANAR (I420, UNSIGNED_INT8, SAMPLE_YUV_PLANAR),
  MAKE_FORMAT_YUV_PLANAR (YV12, UNSIGNED_INT8, SAMPLE_YV12),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV12, UNSIGNED_INT8, SAMPLE_SEMI_PLANAR),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV21, UNSIGNED_INT8, SAMPLE_SEMI_PLANAR_SWAP),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P010_10LE, UNSIGNED_INT16, SAMPLE_SEMI_PLANAR),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P012_LE, UNSIGNED_INT16, SAMPLE_SEMI_PLANAR),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P016_LE, UNSIGNED_INT16, SAMPLE_SEMI_PLANAR),
  MAKE_FORMAT_YUV_PLANAR (I420_10LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_10BIS),
  MAKE_FORMAT_YUV_PLANAR (I420_12LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_12BIS),
  MAKE_FORMAT_YUV_PLANAR (Y444, UNSIGNED_INT8, SAMPLE_YUV_PLANAR),
  MAKE_FORMAT_YUV_PLANAR (Y444_10LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_10BIS),
  MAKE_FORMAT_YUV_PLANAR (Y444_12LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_12BIS),
  MAKE_FORMAT_YUV_PLANAR (Y444_16LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR),
  MAKE_FORMAT_RGB (RGBA, UNSIGNED_INT8, SAMPLE_RGBA),
  MAKE_FORMAT_RGB (BGRA, UNSIGNED_INT8, SAMPLE_BGRA),
  MAKE_FORMAT_RGB (RGBx, UNSIGNED_INT8, SAMPLE_RGBx),
  MAKE_FORMAT_RGB (BGRx, UNSIGNED_INT8, SAMPLE_BGRx),
  MAKE_FORMAT_RGB (ARGB, UNSIGNED_INT8, SAMPLE_ARGB),
  MAKE_FORMAT_RGB (ARGB64, UNSIGNED_INT16, SAMPLE_ARGB),
  MAKE_FORMAT_RGB (ABGR, UNSIGNED_INT8, SAMPLE_ABGR),
  MAKE_FORMAT_YUV_PLANAR (Y42B, UNSIGNED_INT8, SAMPLE_YUV_PLANAR),
  MAKE_FORMAT_YUV_PLANAR (I422_10LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_10BIS),
  MAKE_FORMAT_YUV_PLANAR (I422_12LE, UNSIGNED_INT16, SAMPLE_YUV_PLANAR_12BIS),
  MAKE_FORMAT_RGBP (RGBP, UNSIGNED_INT8, SAMPLE_RGBP),
  MAKE_FORMAT_RGBP (BGRP, UNSIGNED_INT8, SAMPLE_BGRP),
  MAKE_FORMAT_RGBP (GBR, UNSIGNED_INT8, SAMPLE_GBR),
  MAKE_FORMAT_RGBP (GBR_10LE, UNSIGNED_INT16, SAMPLE_GBR_10),
  MAKE_FORMAT_RGBP (GBR_12LE, UNSIGNED_INT16, SAMPLE_GBR_12),
  MAKE_FORMAT_RGBP (GBR_16LE, UNSIGNED_INT16, SAMPLE_GBR),
  MAKE_FORMAT_RGBAP (GBRA, UNSIGNED_INT8, SAMPLE_GBRA),
  MAKE_FORMAT_RGB (VUYA, UNSIGNED_INT8, SAMPLE_VUYA),
};

struct TextureBuffer
{
  gpointer ptr = nullptr;
  gsize stride = 0;
  hipTextureObject_t texture = nullptr;
};

enum
{
  PROP_0,
  PROP_DEST_X,
  PROP_DEST_Y,
  PROP_DEST_WIDTH,
  PROP_DEST_HEIGHT,
  PROP_FILL_BORDER,
  PROP_VIDEO_DIRECTION,
  PROP_ALPHA,
  PROP_BLEND,
};

struct _GstHipConverterPrivate
{
  _GstHipConverterPrivate ()
  {
    config = gst_structure_new_empty ("converter-config");
    const_buf = g_new0 (ConstBuffer, 1);
  }

   ~_GstHipConverterPrivate ()
  {
    if (config)
      gst_structure_free (config);
    g_free (const_buf);
  }

  std::mutex lock;

  GstHipVendor vendor;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  hipStream_t stream = nullptr;

  GstStructure *config = nullptr;

  GstVideoInfo texture_info;
  const TextureFormat *texture_fmt;
  gint tex_align;

  TextureBuffer fallback_buffer[GST_VIDEO_MAX_COMPONENTS];
  TextureBuffer unpack_buffer;
  ConstBuffer *const_buf = nullptr;

  hipModule_t main_module = nullptr;
  hipFunction_t main_func = nullptr;

  hipModule_t unpack_module = nullptr;
  hipFunction_t unpack_func = nullptr;

  gboolean update_const_buf = TRUE;

  /* properties */
  gint dest_x = 0;
  gint dest_y = 0;
  gint dest_width = 0;
  gint dest_height = 0;
  GstVideoOrientationMethod video_direction = GST_VIDEO_ORIENTATION_IDENTITY;
  gboolean fill_border = FALSE;
  HIPfilter_mode filter_mode = HIP_TR_FILTER_MODE_LINEAR;
  gdouble alpha = 1.0;
  gboolean blend = FALSE;
};

static void gst_hip_converter_dispose (GObject * object);
static void gst_hip_converter_finalize (GObject * object);
static void gst_hip_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hip_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_hip_converter_parent_class parent_class
G_DEFINE_TYPE (GstHipConverter, gst_hip_converter, GST_TYPE_OBJECT);

static void
gst_hip_converter_class_init (GstHipConverterClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto param_flags = (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->dispose = gst_hip_converter_dispose;
  object_class->finalize = gst_hip_converter_finalize;
  object_class->set_property = gst_hip_converter_set_property;
  object_class->get_property = gst_hip_converter_get_property;

  g_object_class_install_property (object_class, PROP_DEST_X,
      g_param_spec_int ("dest-x", "Dest-X",
          "x poisition in the destination frame", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_DEST_Y,
      g_param_spec_int ("dest-y", "Dest-Y",
          "y poisition in the destination frame", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_DEST_WIDTH,
      g_param_spec_int ("dest-width", "Dest-Width",
          "Width in the destination frame", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_DEST_HEIGHT,
      g_param_spec_int ("dest-height", "Dest-Height",
          "Height in the destination frame", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_FILL_BORDER,
      g_param_spec_boolean ("fill-border", "Fill border",
          "Fill border", FALSE, param_flags));
  g_object_class_install_property (object_class, PROP_VIDEO_DIRECTION,
      g_param_spec_enum ("video-direction", "Video Direction",
          "Video direction", GST_TYPE_VIDEO_ORIENTATION_METHOD,
          GST_VIDEO_ORIENTATION_IDENTITY, param_flags));
  g_object_class_install_property (object_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha",
          "The alpha color value to use", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND,
      g_param_spec_boolean ("blend", "Blend",
          "Enable alpha blending", FALSE, param_flags));

  GST_DEBUG_CATEGORY_INIT (gst_hip_converter_debug,
      "hipconverter", 0, "hipconverter");
}

static void
gst_hip_converter_init (GstHipConverter * self)
{
  self->priv = new GstHipConverterPrivate ();
}

static void
gst_hip_converter_dispose (GObject * object)
{
  auto self = GST_HIP_CONVERTER (object);
  auto priv = self->priv;

  if (self->device && gst_hip_device_set_current (self->device)) {
    if (priv->unpack_module) {
      HipModuleUnload (priv->vendor, priv->unpack_module);
      priv->unpack_module = nullptr;
    }

    if (priv->main_module) {
      HipModuleUnload (priv->vendor, priv->main_module);
      priv->main_module = nullptr;
    }

    for (guint i = 0; i < G_N_ELEMENTS (priv->fallback_buffer); i++) {
      if (priv->fallback_buffer[i].ptr) {
        if (priv->fallback_buffer[i].texture) {
          HipTexObjectDestroy (priv->vendor, priv->fallback_buffer[i].texture);
          priv->fallback_buffer[i].texture = nullptr;
        }

        HipFree (priv->vendor, priv->fallback_buffer[i].ptr);
        priv->fallback_buffer[i].ptr = 0;
      }
    }

    if (priv->unpack_buffer.ptr) {
      if (priv->unpack_buffer.texture) {
        HipTexObjectDestroy (priv->vendor, priv->unpack_buffer.texture);
        priv->unpack_buffer.texture = 0;
      }

      HipFree (priv->vendor, priv->unpack_buffer.ptr);
      priv->unpack_buffer.ptr = 0;
    }
  }

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_hip_converter_finalize (GObject * object)
{
  auto self = GST_HIP_CONVERTER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hip_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_CONVERTER (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEST_X:
    {
      auto dest_x = g_value_get_int (value);
      if (priv->dest_x != dest_x) {
        priv->update_const_buf = TRUE;
        priv->dest_x = dest_x;
        priv->const_buf->left = dest_x;
        priv->const_buf->right = priv->dest_x + priv->dest_width;
      }
      break;
    }
    case PROP_DEST_Y:
    {
      auto dest_y = g_value_get_int (value);
      if (priv->dest_y != dest_y) {
        priv->update_const_buf = TRUE;
        priv->dest_y = dest_y;
        priv->const_buf->top = dest_y;
        priv->const_buf->bottom = priv->dest_y + priv->dest_height;
      }
      break;
    }
    case PROP_DEST_WIDTH:
    {
      auto dest_width = g_value_get_int (value);
      if (priv->dest_width != dest_width) {
        priv->update_const_buf = TRUE;
        priv->dest_width = dest_width;
        priv->const_buf->right = priv->dest_x + dest_width;
        priv->const_buf->view_width = dest_width;
      }
      break;
    }
    case PROP_DEST_HEIGHT:
    {
      auto dest_height = g_value_get_int (value);
      if (priv->dest_height != dest_height) {
        priv->update_const_buf = TRUE;
        priv->dest_height = dest_height;
        priv->const_buf->bottom = priv->dest_y + dest_height;
        priv->const_buf->view_height = dest_height;
      }
      break;
    }
    case PROP_FILL_BORDER:
    {
      auto fill_border = g_value_get_boolean (value);
      if (priv->fill_border != fill_border) {
        priv->update_const_buf = TRUE;
        priv->fill_border = fill_border;
        priv->const_buf->fill_border = fill_border;
      }
      break;
    }
    case PROP_VIDEO_DIRECTION:
    {
      auto video_direction =
          (GstVideoOrientationMethod) g_value_get_enum (value);
      if (priv->video_direction != video_direction) {
        priv->update_const_buf = TRUE;
        priv->video_direction = video_direction;
        priv->const_buf->video_direction = video_direction;
      }
      break;
    }
    case PROP_ALPHA:
    {
      auto alpha = g_value_get_double (value);
      if (priv->alpha != alpha) {
        priv->update_const_buf = TRUE;
        priv->const_buf->alpha = (float) alpha;
      }
      break;
    }
    case PROP_BLEND:
    {
      auto blend = g_value_get_boolean (value);
      if (priv->blend != blend) {
        priv->update_const_buf = TRUE;
        priv->const_buf->do_blend = blend;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_CONVERTER (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEST_X:
      g_value_set_int (value, priv->dest_x);
      break;
    case PROP_DEST_Y:
      g_value_set_int (value, priv->dest_y);
      break;
    case PROP_DEST_WIDTH:
      g_value_set_int (value, priv->dest_width);
      break;
    case PROP_DEST_HEIGHT:
      g_value_set_int (value, priv->dest_height);
      break;
    case PROP_FILL_BORDER:
      g_value_set_boolean (value, priv->fill_border);
      break;
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, priv->video_direction);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, priv->alpha);
      break;
    case PROP_BLEND:
      g_value_set_boolean (value, priv->blend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const gchar *
get_color_range_name (GstVideoColorRange range)
{
  switch (range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      return "FULL";
    case GST_VIDEO_COLOR_RANGE_16_235:
      return "STUDIO";
    default:
      break;
  }

  return "UNKNOWN";
}

static size_t
do_align (size_t value, size_t align)
{
  if (align == 0)
    return value;

  return ((value + align - 1) / align) * align;
}

static gboolean
gst_hip_converter_setup (GstHipConverter * self)
{
  auto priv = self->priv;
  const GstVideoInfo *in_info;
  const GstVideoInfo *out_info;
  const GstVideoInfo *texture_info;
  GstHipColorMatrix convert_matrix;
  GstHipColorMatrix border_color_matrix;
  gdouble border_color[4];
  guint i, j;
  const GstVideoColorimetry *in_color;
  const GstVideoColorimetry *out_color;
  gchar *str = nullptr;
  hipError_t ret;
  std::string output_name;
  std::string unpack_name;

  in_info = &priv->in_info;
  out_info = &priv->out_info;
  texture_info = &priv->texture_info;
  in_color = &in_info->colorimetry;
  out_color = &out_info->colorimetry;

  memset (&convert_matrix, 0, sizeof (GstHipColorMatrix));
  color_matrix_identity (&convert_matrix);

  switch (GST_VIDEO_INFO_FORMAT (out_info)) {
    case GST_VIDEO_FORMAT_I420:
      output_name = "I420";
      break;
    case GST_VIDEO_FORMAT_YV12:
      output_name = "YV12";
      break;
    case GST_VIDEO_FORMAT_NV12:
      output_name = "NV12";
      break;
    case GST_VIDEO_FORMAT_NV21:
      output_name = "NV21";
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
      output_name = "P010";
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      output_name = "I420_10";
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
      output_name = "I420_12";
      break;
    case GST_VIDEO_FORMAT_Y444:
      output_name = "Y444";
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
      output_name = "Y444_10";
      break;
    case GST_VIDEO_FORMAT_Y444_12LE:
      output_name = "Y444_12";
      break;
    case GST_VIDEO_FORMAT_Y444_16LE:
      output_name = "Y444_16";
      break;
    case GST_VIDEO_FORMAT_RGBA:
      output_name = "RGBA";
      break;
    case GST_VIDEO_FORMAT_RGBx:
      output_name = "RGBx";
      break;
    case GST_VIDEO_FORMAT_BGRA:
      output_name = "BGRA";
      break;
    case GST_VIDEO_FORMAT_BGRx:
      output_name = "BGRx";
      break;
    case GST_VIDEO_FORMAT_ARGB:
      output_name = "ARGB";
      break;
    case GST_VIDEO_FORMAT_ABGR:
      output_name = "ABGR";
      break;
    case GST_VIDEO_FORMAT_RGB:
      output_name = "RGB";
      break;
    case GST_VIDEO_FORMAT_BGR:
      output_name = "BGR";
      break;
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      output_name = "RGB10A2";
      break;
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      output_name = "BGR10A2";
      break;
    case GST_VIDEO_FORMAT_Y42B:
      output_name = "Y42B";
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
      output_name = "I422_10";
      break;
    case GST_VIDEO_FORMAT_I422_12LE:
      output_name = "I422_12";
      break;
    case GST_VIDEO_FORMAT_RGBP:
      output_name = "RGBP";
      break;
    case GST_VIDEO_FORMAT_BGRP:
      output_name = "BGRP";
      break;
    case GST_VIDEO_FORMAT_GBR:
      output_name = "GBR";
      break;
    case GST_VIDEO_FORMAT_GBR_10LE:
      output_name = "GBR_10";
      break;
    case GST_VIDEO_FORMAT_GBR_12LE:
      output_name = "GBR_12";
      break;
    case GST_VIDEO_FORMAT_GBR_16LE:
      output_name = "GBR_16";
      break;
    case GST_VIDEO_FORMAT_GBRA:
      output_name = "GBRA";
      break;
    case GST_VIDEO_FORMAT_VUYA:
      output_name = "VUYA";
      break;
    default:
      break;
  }

  if (output_name.empty ()) {
    GST_ERROR_OBJECT (self, "Unknown write function for format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    return FALSE;
  }

  /* Decide texture info to use, 3 channel RGB or 10bits packed RGB
   * need be converted to other format */
  priv->texture_info = priv->in_info;
  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_RGB:
      gst_video_info_set_format (&priv->texture_info,
          GST_VIDEO_FORMAT_RGBx, GST_VIDEO_INFO_WIDTH (in_info),
          GST_VIDEO_INFO_HEIGHT (in_info));
      unpack_name = "GstHipConverterUnpack_RGB_RGBx";
      break;
    case GST_VIDEO_FORMAT_BGR:
      gst_video_info_set_format (&priv->texture_info,
          GST_VIDEO_FORMAT_BGRx, GST_VIDEO_INFO_WIDTH (in_info),
          GST_VIDEO_INFO_HEIGHT (in_info));
      unpack_name = "GstHipConverterUnpack_RGB_RGBx";
      break;
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      gst_video_info_set_format (&priv->texture_info,
          GST_VIDEO_FORMAT_ARGB64, GST_VIDEO_INFO_WIDTH (in_info),
          GST_VIDEO_INFO_HEIGHT (in_info));
      unpack_name = "GstHipConverterUnpack_RGB10A2_ARGB64";
      break;
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      gst_video_info_set_format (&priv->texture_info,
          GST_VIDEO_FORMAT_ARGB64, GST_VIDEO_INFO_WIDTH (in_info),
          GST_VIDEO_INFO_HEIGHT (in_info));
      unpack_name = "GstHipConverterUnpack_BGR10A2_ARGB64";
      break;
    default:
      break;
  }

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == GST_VIDEO_INFO_FORMAT (texture_info)) {
      priv->texture_fmt = &format_map[i];
      break;
    }
  }

  if (!priv->texture_fmt) {
    GST_ERROR_OBJECT (self, "Couldn't find texture format for %s (%s)",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (texture_info)));
    return FALSE;
  }

  /* calculate black color
   * TODO: add support border color */
  if (GST_VIDEO_INFO_IS_RGB (out_info)) {
    GstVideoInfo rgb_info = *out_info;
    rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    gst_hip_color_range_adjust_matrix_unorm (&rgb_info, out_info,
        &border_color_matrix);
  } else {
    GstVideoInfo rgb_info;

    gst_video_info_set_format (&rgb_info, GST_VIDEO_FORMAT_RGBA64_LE,
        out_info->width, out_info->height);

    gst_hip_rgb_to_yuv_matrix_unorm (&rgb_info, out_info, &border_color_matrix);
  }

  for (i = 0; i < 3; i++) {
    /* TODO: property */
    gdouble border_rgba[4] = { 0, 0, 0 };
    border_color[i] = 0;
    for (j = 0; j < 3; j++)
      border_color[i] += border_color_matrix.matrix[i][j] * border_rgba[i];
    border_color[i] = border_color_matrix.offset[i];
    border_color[i] = CLAMP (border_color[i],
        border_color_matrix.min[i], border_color_matrix.max[i]);
  }

  /* FIXME: handle primaries and transfer functions */
  priv->const_buf->do_convert = 0;
  if (GST_VIDEO_INFO_IS_RGB (texture_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      /* RGB -> RGB */
      if (in_color->range == out_color->range) {
        GST_DEBUG_OBJECT (self, "RGB -> RGB conversion without matrix");
      } else {
        if (!gst_hip_color_range_adjust_matrix_unorm (in_info, out_info,
                &convert_matrix)) {
          GST_ERROR_OBJECT (self, "Failed to get RGB range adjust matrix");
          return FALSE;
        }

        str = gst_hip_dump_color_matrix (&convert_matrix);
        GST_DEBUG_OBJECT (self, "RGB range adjust %s -> %s\n%s",
            get_color_range_name (in_color->range),
            get_color_range_name (out_color->range), str);
        g_free (str);

        priv->const_buf->do_convert = 1;
      }
    } else {
      /* RGB -> YUV */
      if (!gst_hip_rgb_to_yuv_matrix_unorm (in_info, out_info, &convert_matrix)) {
        GST_ERROR_OBJECT (self, "Failed to get RGB -> YUV transform matrix");
        return FALSE;
      }

      str = gst_hip_dump_color_matrix (&convert_matrix);
      GST_DEBUG_OBJECT (self, "RGB -> YUV matrix:\n%s", str);
      g_free (str);

      priv->const_buf->do_convert = 1;
    }
  } else {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      /* YUV -> RGB */
      if (!gst_hip_yuv_to_rgb_matrix_unorm (in_info, out_info, &convert_matrix)) {
        GST_ERROR_OBJECT (self, "Failed to get YUV -> RGB transform matrix");
        return FALSE;
      }

      str = gst_hip_dump_color_matrix (&convert_matrix);
      GST_DEBUG_OBJECT (self, "YUV -> RGB matrix:\n%s", str);
      g_free (str);

      priv->const_buf->do_convert = 1;
    } else {
      /* YUV -> YUV */
      if (in_color->range == out_color->range) {
        GST_DEBUG_OBJECT (self, "YUV -> YU conversion without matrix");
      } else {
        if (!gst_hip_color_range_adjust_matrix_unorm (in_info, out_info,
                &convert_matrix)) {
          GST_ERROR_OBJECT (self, "Failed to get GRAY range adjust matrix");
          return FALSE;
        }

        str = gst_hip_dump_color_matrix (&convert_matrix);
        GST_DEBUG_OBJECT (self, "YUV range adjust matrix:\n%s", str);
        g_free (str);

        priv->const_buf->do_convert = 1;
      }
    }
  }

  for (i = 0; i < 3; i++) {
    priv->const_buf->convert_matrix.coeffX[i] = convert_matrix.matrix[0][i];
    priv->const_buf->convert_matrix.coeffY[i] = convert_matrix.matrix[1][i];
    priv->const_buf->convert_matrix.coeffZ[i] = convert_matrix.matrix[2][i];
    priv->const_buf->convert_matrix.offset[i] = convert_matrix.offset[i];
    priv->const_buf->convert_matrix.min[i] = convert_matrix.min[i];
    priv->const_buf->convert_matrix.max[i] = convert_matrix.max[i];
  }

  priv->const_buf->width = out_info->width;
  priv->const_buf->height = out_info->height;
  priv->const_buf->left = 0;
  priv->const_buf->top = 0;
  priv->const_buf->right = out_info->width;
  priv->const_buf->bottom = out_info->height;
  priv->const_buf->view_width = out_info->width;
  priv->const_buf->view_height = out_info->height;
  priv->const_buf->border_x = border_color[0];
  priv->const_buf->border_y = border_color[1];
  priv->const_buf->border_z = border_color[2];
  priv->const_buf->border_w = border_color[3];
  priv->const_buf->fill_border = 0;
  priv->const_buf->video_direction = 0;
  priv->const_buf->alpha = 1;
  priv->const_buf->do_blend = 0;

  if (!gst_hip_device_set_current (self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't set device");
    return FALSE;
  }

  auto device_id = gst_hip_device_get_device_id (self->device);
  const gchar *program = nullptr;

  std::string kernel_name_base = "GstHipConverterMain_" +
      std::string (priv->texture_fmt->sample_func) + "_" + output_name;

  if (priv->vendor == GST_HIP_VENDOR_AMD) {
    auto kernel_name = kernel_name_base + "_amd";
    auto precompiled = g_precompiled_hsaco_table.find (kernel_name);
    if (precompiled != g_precompiled_hsaco_table.end ())
      program = (const gchar *) precompiled->second;
  } else {
    auto kernel_name = kernel_name_base + "_nvidia";
    auto precompiled = g_precompiled_ptx_table.find (kernel_name);
    if (precompiled != g_precompiled_ptx_table.end ())
      program = precompiled->second;
  }

  if (program) {
    ret = HipModuleLoadData (priv->vendor, &priv->main_module, program);
    if (ret != hipSuccess) {
      GST_WARNING_OBJECT (self,
          "Could not load module from precompiled, ret %d", ret);
      program = nullptr;
      priv->main_module = nullptr;
    } else {
      GST_DEBUG_OBJECT (self, "Loaded precompiled kernel");
    }
  }

  if (!program) {
    std::string kernel_name = kernel_name_base + "_" +
        std::to_string (device_id);
    if (priv->vendor == GST_HIP_VENDOR_AMD)
      kernel_name += "_amd";
    else
      kernel_name += "_nvidia";

    std::string sampler_define = std::string ("-DSAMPLER=Sample") +
        std::string (priv->texture_fmt->sample_func);
    std::string output_define = std::string ("-DOUTPUT=Output") + output_name;
    std::string texture_define;
    std::string arch_opt;
    if (priv->vendor == GST_HIP_VENDOR_AMD) {
      texture_define = std::string ("-DTextureObject_t=hipTextureObject_t");
      hipDeviceProp_t prop;
      ret = HipGetDeviceProperties (GST_HIP_VENDOR_AMD, &prop, device_id);
      if (ret == hipSuccess)
        arch_opt = std::string ("--gpu-architecture=") + prop.gcnArchName;
    } else {
      texture_define = std::string ("-DTextureObject_t=cudaTextureObject_t");
    }

    std::vector < const char *>opts;
    opts.push_back (sampler_define.c_str ());
    opts.push_back (output_define.c_str ());
    opts.push_back (texture_define.c_str ());
    if (priv->vendor == GST_HIP_VENDOR_AMD)
      opts.push_back (arch_opt.c_str ());

    std::lock_guard < std::mutex > lk (g_kernel_table_lock);

    auto ptx = g_ptx_table.find (kernel_name);
    if (ptx == g_ptx_table.end ()) {
      GST_DEBUG_OBJECT (self, "Building PTX");
      program = gst_hip_rtc_compile (self->device, ConverterMain_str,
          opts.data (), opts.size ());
      if (program)
        g_ptx_table[kernel_name] = program;
    } else {
      GST_DEBUG_OBJECT (self, "Found cached PTX");
      program = ptx->second;
    }

    if (program && !priv->main_module) {
      GST_DEBUG_OBJECT (self, "Loading PTX module");
      gst_hip_device_set_current (self->device);
      ret = HipModuleLoadData (priv->vendor, &priv->main_module, program);
      if (ret != hipSuccess) {
        GST_ERROR_OBJECT (self, "Could not load module from PTX, ret %d", ret);
        program = nullptr;
        priv->main_module = nullptr;
      }
    }
  }

  if (!priv->main_module) {
    GST_ERROR_OBJECT (self, "Couldn't load module");
    return FALSE;
  }

  ret = HipModuleGetFunction (priv->vendor, &priv->main_func,
      priv->main_module, "GstHipConverterMain");
  if (!gst_hip_result (ret, priv->vendor)) {
    GST_ERROR_OBJECT (self, "Could not get main function");
    return FALSE;
  }

  /* Allocates intermediate memory for texture */
  if (!unpack_name.empty ()) {
    HIP_TEXTURE_DESC texture_desc = { };
    HIP_RESOURCE_DESC resource_desc = { };
    hipTextureObject_t texture = nullptr;
    guint stride = GST_VIDEO_INFO_COMP_WIDTH (texture_info, 0) *
        GST_VIDEO_INFO_COMP_PSTRIDE (texture_info, 0);
    stride = do_align (stride, priv->tex_align);
    priv->unpack_buffer.stride = stride;

    ret = HipMalloc (priv->vendor, &priv->unpack_buffer.ptr, stride *
        GST_VIDEO_INFO_HEIGHT (texture_info));

    if (!gst_hip_result (ret, priv->vendor)) {
      GST_ERROR_OBJECT (self, "Couldn't allocate unpack buffer");
      return FALSE;
    }

    resource_desc.resType = HIP_RESOURCE_TYPE_PITCH2D;
    resource_desc.res.pitch2D.format = priv->texture_fmt->array_format[0];
    resource_desc.res.pitch2D.numChannels = 4;
    resource_desc.res.pitch2D.width = in_info->width;
    resource_desc.res.pitch2D.height = in_info->height;
    resource_desc.res.pitch2D.pitchInBytes = priv->unpack_buffer.stride;
    resource_desc.res.pitch2D.devPtr = priv->unpack_buffer.ptr;

    texture_desc.filterMode = priv->filter_mode;
    texture_desc.flags = 0x2;
    texture_desc.addressMode[0] = (HIPaddress_mode) 1;
    texture_desc.addressMode[1] = (HIPaddress_mode) 1;
    texture_desc.addressMode[2] = (HIPaddress_mode) 1;

    ret =
        HipTexObjectCreate (priv->vendor, &texture, &resource_desc,
        &texture_desc, nullptr);
    if (!gst_hip_result (ret, priv->vendor)) {
      GST_ERROR_OBJECT (self, "Couldn't create unpack texture");
      return FALSE;
    }

    priv->unpack_buffer.texture = texture;
    program = nullptr;

    std::string unpack_module_name_base = "GstHipConverterUnpack";

    if (priv->vendor == GST_HIP_VENDOR_AMD) {
      auto kernel_name = unpack_module_name_base + "_amd";
      auto precompiled = g_precompiled_hsaco_table.find (kernel_name);
      if (precompiled != g_precompiled_hsaco_table.end ())
        program = (const gchar *) precompiled->second;
    } else {
      auto kernel_name = unpack_module_name_base + "_nvidia";
      auto precompiled = g_precompiled_ptx_table.find (kernel_name);
      if (precompiled != g_precompiled_ptx_table.end ())
        program = precompiled->second;
    }

    if (program) {
      ret = HipModuleLoadData (priv->vendor, &priv->unpack_module, program);
      if (ret != hipSuccess) {
        GST_WARNING_OBJECT (self,
            "Could not load module from precompiled, ret %d", ret);
        program = nullptr;
        priv->unpack_module = nullptr;
      } else {
        GST_DEBUG_OBJECT (self, "Loaded precompiled kernel");
      }
    }

    if (!program) {
      std::string unpack_module_name = unpack_module_name_base + "_" +
          std::to_string (device_id);
      if (priv->vendor == GST_HIP_VENDOR_AMD)
        unpack_module_name += "_amd";
      else
        unpack_module_name += "_nvidia";

      std::string arch_opt;
      if (priv->vendor == GST_HIP_VENDOR_AMD) {
        hipDeviceProp_t prop;
        ret = HipGetDeviceProperties (GST_HIP_VENDOR_AMD, &prop, device_id);
        if (ret == hipSuccess)
          arch_opt = std::string ("--gpu-architecture=") + prop.gcnArchName;
      }

      std::vector < const char *>opts;
      if (!arch_opt.empty ())
        opts.push_back (arch_opt.c_str ());

      std::lock_guard < std::mutex > lk (g_kernel_table_lock);
      auto ptx = g_ptx_table.find (unpack_module_name);
      if (ptx == g_ptx_table.end ()) {
        GST_DEBUG_OBJECT (self, "Building PTX");
        program = gst_hip_rtc_compile (self->device, ConverterUnpack_str,
            opts.empty ()? nullptr : opts.data (), opts.size ());
        if (program)
          g_ptx_table[unpack_module_name] = program;
      } else {
        GST_DEBUG_OBJECT (self, "Found cached PTX");
        program = ptx->second;
      }

      if (program && !priv->unpack_module) {
        GST_DEBUG_OBJECT (self, "PTX CUBIN module");
        ret = HipModuleLoadData (priv->vendor, &priv->unpack_module, program);
        if (!gst_hip_result (ret, priv->vendor)) {
          GST_ERROR_OBJECT (self, "Could not load module from PTX");
          program = nullptr;
          priv->unpack_module = nullptr;
        }
      }
    }

    if (!priv->unpack_module) {
      GST_ERROR_OBJECT (self, "Couldn't load unpack module");
      return FALSE;
    }

    ret = HipModuleGetFunction (priv->vendor, &priv->unpack_func,
        priv->unpack_module, unpack_name.c_str ());
    if (!gst_hip_result (ret, priv->vendor)) {
      GST_ERROR_OBJECT (self, "Could not get unpack function");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
copy_config (const GstIdStr * fieldname, const GValue * value,
    gpointer user_data)
{
  GstHipConverter *self = (GstHipConverter *) user_data;

  gst_structure_id_str_set_value (self->priv->config, fieldname, value);

  return TRUE;
}

static void
gst_hip_converter_set_config (GstHipConverter * self, GstStructure * config)
{
  gst_structure_foreach_id_str (config, copy_config, self);
  gst_structure_free (config);
}

GstHipConverter *
gst_hip_converter_new (GstHipDevice * device, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * config)
{
  g_return_val_if_fail (in_info != nullptr, nullptr);
  g_return_val_if_fail (out_info != nullptr, nullptr);
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);

  gboolean tex_support = FALSE;
  g_object_get (device, "texture2d-support", &tex_support, nullptr);
  if (!tex_support) {
    GST_WARNING_OBJECT (device, "Texture not supported");
    return nullptr;
  }

  gint tex_align = 0;
  auto hip_ret = gst_hip_device_get_attribute (device,
      hipDeviceAttributeTextureAlignment, &tex_align);
  if (hip_ret != hipSuccess || tex_align <= 0) {
    GST_WARNING_OBJECT (device, "Unknown texture alignment");
    return nullptr;
  }

  auto self =
      (GstHipConverter *) g_object_new (GST_TYPE_HIP_CONVERTER, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstHipDevice *) gst_object_ref (device);

  auto priv = self->priv;
  priv->in_info = *in_info;
  priv->out_info = *out_info;
  priv->dest_width = out_info->width;
  priv->dest_height = out_info->height;
  priv->tex_align = tex_align;
  priv->vendor = gst_hip_device_get_vendor (device);
  priv->stream = gst_hip_stream_get_handle (gst_hip_device_get_stream (device));

  if (config)
    gst_hip_converter_set_config (self, config);

  if (!gst_hip_converter_setup (self)) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

static hipTextureObject_t
gst_hip_converter_create_texture_unchecked (GstHipConverter * self,
    gpointer src, gint width, gint height, hipArray_Format format,
    guint channels, gint stride, gint plane, HIPfilter_mode mode)
{
  auto priv = self->priv;
  HIP_TEXTURE_DESC texture_desc = { };
  HIP_RESOURCE_DESC resource_desc = { };
  hipTextureObject_t texture = nullptr;

  resource_desc.resType = HIP_RESOURCE_TYPE_PITCH2D;
  resource_desc.res.pitch2D.format = format;
  resource_desc.res.pitch2D.numChannels = channels;
  resource_desc.res.pitch2D.width = width;
  resource_desc.res.pitch2D.height = height;
  resource_desc.res.pitch2D.pitchInBytes = stride;
  resource_desc.res.pitch2D.devPtr = src;

  texture_desc.filterMode = mode;
  /* Will read texture value as a normalized [0, 1] float value
   * with [0, 1) coordinates */
  /* CU_TRSF_NORMALIZED_COORDINATES */
  texture_desc.flags = 0x2;
  /* CU_TR_ADDRESS_MODE_CLAMP */
  texture_desc.addressMode[0] = (HIPaddress_mode) 1;
  texture_desc.addressMode[1] = (HIPaddress_mode) 1;
  texture_desc.addressMode[2] = (HIPaddress_mode) 1;

  auto hip_ret = HipTexObjectCreate (priv->vendor, &texture,
      &resource_desc, &texture_desc, nullptr);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (self, "Could not create texture");
    return nullptr;
  }

  return texture;
}

static gboolean
ensure_fallback_buffer (GstHipConverter * self, gint width_in_bytes,
    gint height, guint plane)
{
  auto priv = self->priv;

  if (priv->fallback_buffer[plane].ptr)
    return TRUE;

  size_t pitch = do_align (width_in_bytes, priv->tex_align);
  priv->fallback_buffer[plane].stride = pitch;
  auto hip_ret = HipMalloc (priv->vendor, &priv->fallback_buffer[plane].ptr,
      pitch * height);

  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (self, "Couldn't allocate fallback buffer");
    return FALSE;
  }

  return TRUE;
}

static hipTextureObject_t
gst_hip_converter_create_texture (GstHipConverter * self,
    gpointer src, gint width, gint height, gint stride, HIPfilter_mode mode,
    hipArray_Format format, guint channels, gint plane)
{
  auto priv = self->priv;
  hip_Memcpy2D params = { };

  if (!ensure_fallback_buffer (self, stride, height, plane))
    return nullptr;

  params.srcMemoryType = hipMemoryTypeDevice;
  params.srcPitch = stride;
  params.srcDevice = src;

  params.dstMemoryType = hipMemoryTypeDevice;
  params.dstPitch = priv->fallback_buffer[plane].stride;
  params.dstDevice = priv->fallback_buffer[plane].ptr;
  params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (&priv->in_info, plane)
      * GST_VIDEO_INFO_COMP_PSTRIDE (&priv->in_info, plane),
      params.Height = GST_VIDEO_INFO_COMP_HEIGHT (&priv->in_info, plane);

  auto hip_ret = HipMemcpyParam2DAsync (priv->vendor, &params, priv->stream);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (self, "Couldn't copy to fallback buffer");
    return nullptr;
  }

  if (!priv->fallback_buffer[plane].texture) {
    auto src_ptr = priv->fallback_buffer[plane].ptr;
    stride = priv->fallback_buffer[plane].stride;

    priv->fallback_buffer[plane].texture =
        gst_hip_converter_create_texture_unchecked (self, src_ptr, width,
        height, format, channels, stride, plane, mode);
  }

  return priv->fallback_buffer[plane].texture;
}

static gboolean
gst_hip_converter_unpack_rgb (GstHipConverter * self, GstVideoFrame * src_frame)
{
  auto priv = self->priv;
  gpointer src;
  gint width, height, src_stride, dst_stride;
  gpointer args[] = { &src, &priv->unpack_buffer.ptr,
    &width, &height, &src_stride, &dst_stride
  };

  g_assert (priv->unpack_buffer.ptr);
  g_assert (priv->unpack_buffer.stride > 0);

  src = GST_VIDEO_FRAME_PLANE_DATA (src_frame, 0);
  width = GST_VIDEO_FRAME_WIDTH (src_frame);
  height = GST_VIDEO_FRAME_HEIGHT (src_frame);
  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src_frame, 0);
  dst_stride = (gint) priv->unpack_buffer.stride;

  auto hip_ret = HipModuleLaunchKernel (priv->vendor, priv->unpack_func,
      DIV_UP (width, HIP_BLOCK_X), DIV_UP (height, HIP_BLOCK_Y), 1,
      HIP_BLOCK_X, HIP_BLOCK_Y, 1, 0, priv->stream, args, nullptr);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (self, "Couldn't unpack source RGB");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_hip_converter_convert_frame (GstHipConverter * converter,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  const TextureFormat *format;
  hipTextureObject_t texture[GST_VIDEO_MAX_COMPONENTS] = { };
  guint8 *dst[GST_VIDEO_MAX_COMPONENTS] = { };
  gint stride[2] = { 0, };
  gint width, height;
  gint off_x = 0;
  gint off_y = 0;
  GstVideoFrame in_frame, out_frame;

  g_return_val_if_fail (GST_IS_HIP_CONVERTER (converter), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (in_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (out_buf), FALSE);

  auto priv = converter->priv;

  if (!gst_hip_device_set_current (converter->device)) {
    GST_ERROR_OBJECT (converter, "Couldn't set device");
    return FALSE;
  }

  if (!gst_video_frame_map (&in_frame, &priv->in_info, in_buf,
          GST_MAP_READ_HIP)) {
    GST_ERROR_OBJECT (converter, "Couldn't map input buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, &priv->out_info, out_buf,
          GST_MAP_WRITE_HIP)) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (converter, "Couldn't map output buffer");
    return FALSE;
  }

  auto in_hmem = (GstHipMemory *) gst_buffer_peek_memory (in_buf, 0);
  auto out_hmem = (GstHipMemory *) gst_buffer_peek_memory (out_buf, 0);

  auto in_stream = gst_hip_memory_get_stream (in_hmem);
  auto out_stream = gst_hip_memory_get_stream (out_hmem);

  gboolean set_event = FALSE;
  /* Avoid sync if in/out mem use the same hip stream */
  priv->stream = gst_hip_stream_get_handle (out_stream);
  if (in_stream != out_stream) {
    gst_hip_memory_sync (in_hmem);
  } else {
    set_event = TRUE;
  }

  priv = converter->priv;
  format = priv->texture_fmt;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->fill_border && (priv->dest_width <= 0 || priv->dest_height <= 0))
    return TRUE;

  gpointer args[] = { &texture[0], &texture[1], &texture[2], &texture[3],
    &dst[0], &dst[1], &dst[2], &dst[3], &stride[0], &stride[1],
    priv->const_buf, &off_x, &off_y
  };

  auto cmem = (GstHipMemory *) gst_buffer_peek_memory (in_buf, 0);

  if (priv->unpack_func) {
    if (!gst_hip_converter_unpack_rgb (converter, &in_frame)) {
      gst_video_frame_unmap (&out_frame);
      gst_video_frame_unmap (&in_frame);
      return FALSE;
    }

    texture[0] = priv->unpack_buffer.texture;
    if (!texture[0]) {
      GST_ERROR_OBJECT (converter, "Unpack texture is unavailable");
      gst_video_frame_unmap (&out_frame);
      gst_video_frame_unmap (&in_frame);
      return FALSE;
    }
  } else {
    for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&in_frame); i++) {
      if (!gst_hip_memory_get_texture (cmem,
              i, priv->filter_mode, HIP_TR_ADDRESS_MODE_CLAMP, &texture[i])) {
        auto src = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);
        texture[i] = gst_hip_converter_create_texture (converter,
            src, GST_VIDEO_FRAME_COMP_WIDTH (&in_frame, i),
            GST_VIDEO_FRAME_COMP_HEIGHT (&in_frame, i),
            GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, i),
            priv->filter_mode, format->array_format[i], format->channels[i], i);
      }

      if (!texture[i]) {
        GST_ERROR_OBJECT (converter, "Couldn't create texture %d", i);
        gst_video_frame_unmap (&out_frame);
        gst_video_frame_unmap (&in_frame);
        return FALSE;
      }
    }
  }

  width = GST_VIDEO_FRAME_WIDTH (&out_frame);
  height = GST_VIDEO_FRAME_HEIGHT (&out_frame);

  if (!priv->fill_border) {
    if (priv->dest_width < width) {
      off_x = priv->dest_x;
      width = priv->dest_width;
    }

    if (priv->dest_height < height) {
      off_y = priv->dest_y;
      height = priv->dest_height;
    }
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&out_frame); i++)
    dst[i] = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);

  stride[0] = stride[1] = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 0);
  if (GST_VIDEO_FRAME_N_PLANES (&out_frame) > 1)
    stride[1] = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 1);

  auto hip_ret = HipModuleLaunchKernel (priv->vendor, priv->main_func,
      DIV_UP (width, HIP_BLOCK_X), DIV_UP (height, HIP_BLOCK_Y), 1,
      HIP_BLOCK_X, HIP_BLOCK_Y, 1,
      0, priv->stream, args, nullptr);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (converter, "Couldn't convert frame");
    return FALSE;
  }

  auto stream = gst_hip_device_get_stream (converter->device);
  GstHipEvent *event;
  if (set_event && gst_hip_stream_record_event (stream, &event)) {
    auto hmem = (GstHipMemory *) gst_buffer_peek_memory (out_buf, 0);
    gst_hip_memory_set_event (hmem, event);
    gst_hip_event_unref (event);
  } else {
    HipStreamSynchronize (priv->vendor, priv->stream);
  }

  return TRUE;
}
