/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) <2019> Jeongki Kim <jeongki.kim@jeongki.kim>
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
#  include <config.h>
#endif

#include "gstd3d11colorconverter.h"
#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11shader.h"
#include "gstd3d11format.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_colorconverter_debug);
#define GST_CAT_DEFAULT gst_d3d11_colorconverter_debug

#define CONVERTER_MAX_QUADS 2

/* *INDENT-OFF* */
typedef struct
{
  FLOAT trans_matrix[12];
  FLOAT padding[4];
} PixelShaderColorTransform;

typedef struct
{
  struct {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct {
    FLOAT x;
    FLOAT y;
  } texture;
} VertexData;

typedef struct
{
  const gchar *constant_buffer;
  const gchar *func;
} PixelShaderTemplate;

#define COLOR_TRANSFORM_COEFF \
    "cbuffer PixelShaderColorTransform : register(b0)\n" \
    "{\n" \
    "  float3x4 trans_matrix;\n" \
    "  float3 padding;\n" \
    "};\n"

#define HLSL_FUNC_YUV_TO_RGB \
    "float3 yuv_to_rgb (float3 yuv)\n" \
    "{\n" \
    "  yuv += float3(-0.062745f, -0.501960f, -0.501960f);\n" \
    "  yuv = mul(yuv, trans_matrix);\n" \
    "  return saturate(yuv);\n" \
    "}\n"

#define HLSL_FUNC_RGB_TO_YUV \
    "float3 rgb_to_yuv (float3 rgb)\n" \
    "{\n" \
    "  float3 yuv;\n" \
    "  yuv = mul(rgb, trans_matrix);\n" \
    "  yuv += float3(0.062745f, 0.501960f, 0.501960f);\n" \
    "  return saturate(yuv);\n" \
    "}\n"

static const PixelShaderTemplate templ_REORDER =
    { NULL, NULL };

static const PixelShaderTemplate templ_YUV_to_RGB =
    { COLOR_TRANSFORM_COEFF, HLSL_FUNC_YUV_TO_RGB };

static const PixelShaderTemplate templ_RGB_to_YUV =
    { COLOR_TRANSFORM_COEFF, HLSL_FUNC_RGB_TO_YUV };

static const gchar templ_REORDER_BODY[] =
    "  output.Plane_0 = shaderTexture[0].Sample(samplerState, input.Texture);\n";

static const gchar templ_VUYA_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).z;\n"
    "  sample.y  = shaderTexture[0].Sample(samplerState, input.Texture).y;\n"
    "  sample.z  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.a  = shaderTexture[0].Sample(samplerState, input.Texture).a;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = sample.a;\n"
    "  output.Plane_0 = rgba;\n";

static const gchar templ_RGB_to_VUYA_BODY[] =
    "  float4 sample, vuya;\n"
    "  sample = shaderTexture[0].Sample(samplerState, input.Texture);\n"
    "  vuya.zyx = rgb_to_yuv (sample.rgb);\n"
    "  vuya.a = sample.a;\n"
    "  output.Plane_0 = vuya;\n";

/* YUV to RGB conversion */
static const gchar templ_PLANAR_YUV_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.y  = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.z  = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  output.Plane_0 = rgba;\n";

static const gchar templ_SEMI_PLANAR_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, input.Texture).xy;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  output.Plane_0 = rgba;\n";

/* RGB to YUV conversion */
static const gchar templ_RGB_to_LUMA_BODY[] =
    "  float4 sample, rgba;\n"
    "  rgba.rgb = shaderTexture[0].Sample(samplerState, input.Texture).rgb;\n"
    "  sample.xyz = rgb_to_yuv (rgba.rgb);\n"
    "  sample.y = 0.0;\n"
    "  sample.z = 0.0;\n"
    "  sample.a = 0.0;\n"
    "  sample.x = sample.x / %d;\n"
    "  output.Plane_0 = sample;\n";

static const gchar templ_RGB_to_SEMI_PLANAR_CHROMA_BODY[] =
    "  float4 sample, rgba;\n"
    "  rgba.rgb = shaderTexture[0].Sample(samplerState, input.Texture).rgb;\n"
    "  sample.xyz = rgb_to_yuv (rgba.rgb);\n"
    "  sample.x = sample.y;\n"
    "  sample.y = sample.z;\n"
    "  sample.z = 0.0;\n"
    "  sample.a = 0.0;\n"
    "  output.Plane_0 = sample;\n";

static const gchar templ_RGB_to_PLANAR_CHROMA_BODY[] =
    "  float4 sample, rgba;\n"
    "  rgba.rgb = shaderTexture[0].Sample(samplerState, input.Texture).rgb;\n"
    "  sample.xyz = rgb_to_yuv (rgba.rgb);\n"
    "  output.Plane_0 = float4(sample.y / %d, 0.0, 0.0, 0.0);\n"
    "  output.Plane_1 = float4(sample.z / %d, 0.0, 0.0, 0.0);\n";

/* YUV to YUV conversion */
static const gchar templ_LUMA_to_LUMA_BODY[] =
    "  float4 sample;\n"
    "  sample.x = shaderTexture[0].Sample(samplerState, input.Texture).x * %d;\n"
    "  output.Plane_0 = float4(sample.x / %d, 0.0, 0.0, 0.0);\n";

static const gchar templ_PLANAR_TO_SEMI_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.y = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.z = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  output.Plane_0 = float4(sample.yz, 0.0, 0.0);\n";

static const gchar templ_SEMI_PLANAR_TO_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, input.Texture).xy;\n"
    "  output.Plane_0 = float4(sample.y / %d, 0.0, 0.0, 0.0);\n"
    "  output.Plane_1 = float4(sample.z / %d, 0.0, 0.0, 0.0);\n";

static const gchar templ_SEMI_PLANAR_TO_SEMI_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, input.Texture).xy;\n"
    "  output.Plane_0 = float4(sample.yz, 0.0, 0.0);\n";

static const gchar templ_PLANAR_TO_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.y = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.z = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  output.Plane_0 = float4(sample.y / %d, 0.0, 0.0, 0.0);\n"
    "  output.Plane_1 = float4(sample.z / %d, 0.0, 0.0, 0.0);\n";

/* VUYA to YUV */
static const gchar templ_VUYA_to_LUMA_BODY[] =
    "  float4 sample;\n"
    "  sample.x = shaderTexture[0].Sample(samplerState, input.Texture).z;\n"
    "  output.Plane_0 = float4(sample.x / %d, 0.0, 0.0, 0.0);\n";

static const gchar templ_VUYA_TO_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.yz = shaderTexture[0].Sample(samplerState, input.Texture).yx;\n"
    "  output.Plane_0 = float4(sample.y / %d, 0.0, 0.0, 0.0);\n"
    "  output.Plane_1 = float4(sample.z / %d, 0.0, 0.0, 0.0);\n";

static const gchar templ_VUYA_TO_SEMI_PLANAR_CHROMA_BODY[] =
    "  float4 sample;\n"
    "  sample.yz = shaderTexture[0].Sample(samplerState, input.Texture).yx;\n"
    "  output.Plane_0 = float4(sample.yz, 0.0, 0.0);\n";

/* YUV to VUYA */
static const gchar templ_PLANAR_to_VUYA_BODY[] =
    "  float4 sample;\n"
    "  sample.z = shaderTexture[0].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.y = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.x = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  output.Plane_0 = float4(sample.xyz, 1.0f);\n";

static const gchar templ_SEMI_PLANAR_to_VUYA_BODY[] =
    "  float4 sample;\n"
    "  sample.z = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.xy = shaderTexture[1].Sample(samplerState, input.Texture).yx;\n"
    "  output.Plane_0 = float4(sample.xyz, 1.0f);\n";

static const gchar templ_pixel_shader[] =
    /* constant buffer */
    "%s\n"
    "Texture2D shaderTexture[4];\n"
    "SamplerState samplerState;\n"
    "\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float3 Texture: TEXCOORD0;\n"
    "};\n"
    "\n"
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane_0: SV_TARGET0;\n"
    "  float4 Plane_1: SV_TARGET1;\n"
    "};\n"
    "\n"
    /* rgb <-> yuv function */
    "%s\n"
    "PS_OUTPUT main(PS_INPUT input)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "%s"
    "  return output;\n"
    "}\n";

static const gchar templ_vertex_shader[] =
    "struct VS_INPUT\n"
    "{\n"
    "  float4 Position : POSITION;\n"
    "  float4 Texture : TEXCOORD0;\n"
    "};\n"
    "\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float4 Texture: TEXCOORD0;\n"
    "};\n"
    "\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}\n";

/* *INDENT-ON* */

typedef struct
{
  const PixelShaderTemplate *templ;
  gchar *ps_body[CONVERTER_MAX_QUADS];
  PixelShaderColorTransform transform;
} ConvertInfo;

struct _GstD3D11ColorConverter
{
  GstD3D11Device *device;
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  const GstD3D11Format *in_d3d11_format;
  const GstD3D11Format *out_d3d11_format;

  guint num_input_view;
  guint num_output_view;

  GstD3D11Quad *quad[CONVERTER_MAX_QUADS];

  D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];

  RECT crop_rect;
  gint input_texture_width;
  gint input_texture_height;
  ID3D11Buffer *vertex_buffer;
  gboolean update_vertex;

  ConvertInfo convert_info;
};

/* from video-converter.c */
typedef struct
{
  gfloat dm[4][4];
} MatrixData;

static void
color_matrix_set_identity (MatrixData * m)
{
  gint i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->dm[i][j] = (i == j);
    }
  }
}

static void
color_matrix_copy (MatrixData * d, const MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      d->dm[i][j] = s->dm[i][j];
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (MatrixData * dst, MatrixData * a, MatrixData * b)
{
  MatrixData tmp;
  gint i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      gfloat x = 0;
      for (k = 0; k < 4; k++) {
        x += a->dm[i][k] * b->dm[k][j];
      }
      tmp.dm[i][j] = x;
    }
  }
  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_offset_components (MatrixData * m, gfloat a1, gfloat a2, gfloat a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][3] = a1;
  a.dm[1][3] = a2;
  a.dm[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (MatrixData * m, gfloat a1, gfloat a2, gfloat a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][0] = a1;
  a.dm[1][1] = a2;
  a.dm[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_debug (GstD3D11ColorConverter * self, const MatrixData * s)
{
  GST_DEBUG ("[%f %f %f %f]",
      s->dm[0][0], s->dm[0][1], s->dm[0][2], s->dm[0][3]);
  GST_DEBUG ("[%f %f %f %f]",
      s->dm[1][0], s->dm[1][1], s->dm[1][2], s->dm[1][3]);
  GST_DEBUG ("[%f %f %f %f]",
      s->dm[2][0], s->dm[2][1], s->dm[2][2], s->dm[2][3]);
  GST_DEBUG ("[%f %f %f %f]",
      s->dm[3][0], s->dm[3][1], s->dm[3][2], s->dm[3][3]);
}

static void
color_matrix_YCbCr_to_RGB (MatrixData * m, gfloat Kr, gfloat Kb)
{
  gfloat Kg = 1.0 - Kr - Kb;
  MatrixData k = {
    {
          {1., 0., 2 * (1 - Kr), 0.},
          {1., -2 * Kb * (1 - Kb) / Kg, -2 * Kr * (1 - Kr) / Kg, 0.},
          {1., 2 * (1 - Kb), 0., 0.},
          {0., 0., 0., 1.},
        }
  };

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_YCbCr (MatrixData * m, gfloat Kr, gfloat Kb)
{
  gfloat Kg = 1.0 - Kr - Kb;
  MatrixData k;
  gfloat x;

  k.dm[0][0] = Kr;
  k.dm[0][1] = Kg;
  k.dm[0][2] = Kb;
  k.dm[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.dm[1][0] = -x * Kr;
  k.dm[1][1] = -x * Kg;
  k.dm[1][2] = x * (1 - Kb);
  k.dm[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.dm[2][0] = x * (1 - Kr);
  k.dm[2][1] = -x * Kg;
  k.dm[2][2] = -x * Kb;
  k.dm[2][3] = 0;

  k.dm[3][0] = 0;
  k.dm[3][1] = 0;
  k.dm[3][2] = 0;
  k.dm[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
compute_matrix_to_RGB (GstD3D11ColorConverter * self, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  /* bring color components to [0..1.0] range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_offset_components (data, -offset[0], -offset[1], -offset[2]);
  color_matrix_scale_components (data, 1 / ((float) scale[0]),
      1 / ((float) scale[1]), 1 / ((float) scale[2]));

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to R'G'B' space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_YCbCr_to_RGB (data, Kr, Kb);
  }
  color_matrix_debug (self, data);
}

static void
compute_matrix_to_YUV (GstD3D11ColorConverter * self, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to YCbCr space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_RGB_to_YCbCr (data, Kr, Kb);
  }

  /* bring color components to nominal range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_scale_components (data, (float) scale[0], (float) scale[1],
      (float) scale[2]);
  color_matrix_offset_components (data, offset[0], offset[1], offset[2]);

  color_matrix_debug (self, data);
}

static gboolean
converter_get_matrix (GstD3D11ColorConverter * self, MatrixData * matrix,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gboolean same_matrix;
  guint in_bits, out_bits;

  in_bits = GST_VIDEO_INFO_COMP_DEPTH (in_info, 0);
  out_bits = GST_VIDEO_INFO_COMP_DEPTH (out_info, 0);

  same_matrix = in_info->colorimetry.matrix == out_info->colorimetry.matrix;

  GST_DEBUG ("matrix %d -> %d (%d)", in_info->colorimetry.matrix,
      out_info->colorimetry.matrix, same_matrix);

  color_matrix_set_identity (matrix);

  if (same_matrix) {
    GST_DEBUG ("conversion matrix is not required");
    return FALSE;
  }

  if (in_bits < out_bits) {
    gint scale = 1 << (out_bits - in_bits);
    color_matrix_scale_components (matrix,
        1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
  }

  GST_DEBUG ("to RGB matrix");
  compute_matrix_to_RGB (self, matrix, in_info);
  GST_DEBUG ("current matrix");
  color_matrix_debug (self, matrix);

  GST_DEBUG ("to YUV matrix");
  compute_matrix_to_YUV (self, matrix, out_info);
  GST_DEBUG ("current matrix");
  color_matrix_debug (self, matrix);

  if (in_bits > out_bits) {
    gint scale = 1 << (in_bits - out_bits);
    color_matrix_scale_components (matrix,
        (float) scale, (float) scale, (float) scale);
  }

  GST_DEBUG ("final matrix");
  color_matrix_debug (self, matrix);

  return TRUE;
}

static gboolean
setup_convert_info_rgb_to_rgb (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *convert_info = &self->convert_info;

  convert_info->templ = &templ_REORDER;
  convert_info->ps_body[0] = g_strdup_printf (templ_REORDER_BODY);

  return TRUE;
}

static gboolean
setup_convert_info_yuv_to_rgb (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;

  info->templ = &templ_YUV_to_RGB;

  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_VUYA:
      info->ps_body[0] = g_strdup_printf (templ_VUYA_to_RGB_BODY);
      break;
    case GST_VIDEO_FORMAT_I420:
      info->ps_body[0] =
          g_strdup_printf (templ_PLANAR_YUV_to_RGB_BODY, 1, 1, 1);
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      info->ps_body[0] =
          g_strdup_printf (templ_PLANAR_YUV_to_RGB_BODY, 64, 64, 64);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
      info->ps_body[0] = g_strdup_printf (templ_SEMI_PLANAR_to_RGB_BODY);
      break;
    default:
      GST_FIXME_OBJECT (self,
          "Unhandled input format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
      return FALSE;
  }

  return TRUE;
}

static gboolean
setup_convert_info_rgb_to_yuv (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;

  info->templ = &templ_RGB_to_YUV;

  switch (GST_VIDEO_INFO_FORMAT (out_info)) {
    case GST_VIDEO_FORMAT_VUYA:
      info->ps_body[0] = g_strdup_printf (templ_RGB_to_VUYA_BODY);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
      info->ps_body[0] = g_strdup_printf (templ_RGB_to_LUMA_BODY, 1);
      info->ps_body[1] = g_strdup_printf (templ_RGB_to_SEMI_PLANAR_CHROMA_BODY);
      break;
    case GST_VIDEO_FORMAT_I420:
      info->ps_body[0] = g_strdup_printf (templ_RGB_to_LUMA_BODY, 1);
      info->ps_body[1] =
          g_strdup_printf (templ_RGB_to_PLANAR_CHROMA_BODY, 1, 1);
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      info->ps_body[0] = g_strdup_printf (templ_RGB_to_LUMA_BODY, 64);
      info->ps_body[1] =
          g_strdup_printf (templ_RGB_to_PLANAR_CHROMA_BODY, 64, 64);
      break;
    default:
      GST_FIXME_OBJECT (self,
          "Unhandled output format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
      return FALSE;
  }

  return TRUE;
}

static gboolean
setup_convert_info_planar_to_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint mul = 1;
  gint div = 1;

  info->templ = &templ_REORDER;

  if (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_I420_10LE)
    mul = 64;

  if (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_I420_10LE)
    div = 64;

  info->ps_body[0] = g_strdup_printf (templ_LUMA_to_LUMA_BODY, mul, div);
  info->ps_body[1] =
      g_strdup_printf (templ_PLANAR_TO_PLANAR_CHROMA_BODY, mul, mul, div, div);

  return TRUE;
}

static gboolean
setup_convert_info_planar_to_semi_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint mul = 1;
  gint div = 1;

  info->templ = &templ_REORDER;

  if (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_I420_10LE)
    mul = 64;

  info->ps_body[0] = g_strdup_printf (templ_LUMA_to_LUMA_BODY, mul, div);
  info->ps_body[1] =
      g_strdup_printf (templ_PLANAR_TO_SEMI_PLANAR_CHROMA_BODY, mul, mul);

  return TRUE;
}

static gboolean
setup_convert_info_semi_planar_to_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint mul = 1;
  gint div = 1;

  info->templ = &templ_REORDER;

  if (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_I420_10LE)
    div = 64;

  info->ps_body[0] = g_strdup_printf (templ_LUMA_to_LUMA_BODY, mul, div);
  info->ps_body[1] =
      g_strdup_printf (templ_SEMI_PLANAR_TO_PLANAR_CHROMA_BODY, div, div);

  return TRUE;
}

static gboolean
setup_convert_info_semi_planar_to_semi_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint mul = 1;
  gint div = 1;

  info->templ = &templ_REORDER;

  info->ps_body[0] = g_strdup_printf (templ_LUMA_to_LUMA_BODY, mul, div);
  info->ps_body[1] =
      g_strdup_printf (templ_SEMI_PLANAR_TO_SEMI_PLANAR_CHROMA_BODY);

  return TRUE;
}

static gboolean
setup_convert_info_vuya_to_vuya (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;

  info->templ = &templ_REORDER;

  info->ps_body[0] = g_strdup_printf (templ_REORDER_BODY);

  return TRUE;
}

static gboolean
setup_convert_info_vuya_to_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint div = 1;

  info->templ = &templ_REORDER;

  if (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_I420_10LE)
    div = 64;

  info->ps_body[0] = g_strdup_printf (templ_VUYA_to_LUMA_BODY, div);
  info->ps_body[1] =
      g_strdup_printf (templ_VUYA_TO_PLANAR_CHROMA_BODY, div, div);

  return TRUE;
}

static gboolean
setup_convert_info_vuya_to_semi_planar (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint div = 1;

  info->templ = &templ_REORDER;

  info->ps_body[0] = g_strdup_printf (templ_VUYA_to_LUMA_BODY, div);
  info->ps_body[1] = g_strdup_printf (templ_VUYA_TO_SEMI_PLANAR_CHROMA_BODY);

  return TRUE;
}

static gboolean
setup_convert_info_planar_to_vuya (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;
  gint mul = 1;

  info->templ = &templ_REORDER;

  if (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_I420_10LE)
    mul = 64;

  info->ps_body[0] = g_strdup_printf (templ_PLANAR_to_VUYA_BODY, mul, mul, mul);

  return TRUE;
}

static gboolean
setup_convert_info_semi_planar_to_vuya (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->convert_info;

  info->templ = &templ_REORDER;

  info->ps_body[0] = g_strdup_printf (templ_SEMI_PLANAR_to_VUYA_BODY);

  return TRUE;
}

static gboolean
setup_convert_info_yuv_to_yuv (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  gboolean in_planar, out_planar;
  gboolean in_vuya, out_vuya;

  in_vuya = GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_VUYA;
  out_vuya = GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_VUYA;
  in_planar = (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_I420 ||
      GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_I420_10LE);
  out_planar = (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_I420 ||
      GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_I420_10LE);

  if (in_vuya && out_vuya) {
    return setup_convert_info_vuya_to_vuya (self, in_info, out_info);
  } else if (in_vuya) {
    if (out_planar)
      return setup_convert_info_vuya_to_planar (self, in_info, out_info);
    else
      return setup_convert_info_vuya_to_semi_planar (self, in_info, out_info);
  } else if (out_vuya) {
    if (in_planar)
      return setup_convert_info_planar_to_vuya (self, in_info, out_info);
    else
      return setup_convert_info_semi_planar_to_vuya (self, in_info, out_info);
  }

  if (in_planar) {
    if (out_planar)
      return setup_convert_info_planar_to_planar (self, in_info, out_info);
    else
      return setup_convert_info_planar_to_semi_planar (self, in_info, out_info);
  } else {
    if (out_planar)
      return setup_convert_info_semi_planar_to_planar (self, in_info, out_info);
    else
      return setup_convert_info_semi_planar_to_semi_planar (self, in_info,
          out_info);
  }

  return FALSE;
}

static gboolean
gst_d3d11_color_convert_setup_shader (GstD3D11ColorConverter * self,
    GstD3D11Device * device, GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  HRESULT hr;
  D3D11_SAMPLER_DESC sampler_desc = { 0, };
  D3D11_INPUT_ELEMENT_DESC input_desc[2] = { 0, };
  D3D11_BUFFER_DESC buffer_desc = { 0, };
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  ConvertInfo *convert_info = &self->convert_info;
  ID3D11PixelShader *ps[CONVERTER_MAX_QUADS] = { NULL, NULL };
  ID3D11VertexShader *vs = NULL;
  ID3D11InputLayout *layout = NULL;
  ID3D11SamplerState *sampler = NULL;
  ID3D11Buffer *const_buffer = NULL;
  ID3D11Buffer *vertex_buffer = NULL;
  ID3D11Buffer *index_buffer = NULL;
  const guint index_count = 2 * 3;
  gboolean ret = TRUE;
  gint i;

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  /* bilinear filtering */
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = ID3D11Device_CreateSamplerState (device_handle, &sampler_desc, &sampler);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create sampler state, hr: 0x%x", (guint) hr);
    ret = FALSE;
    goto clear;
  }

  for (i = 0; i < CONVERTER_MAX_QUADS; i++) {
    gchar *shader_code = NULL;

    if (convert_info->ps_body[i]) {
      shader_code = g_strdup_printf (templ_pixel_shader,
          convert_info->templ->constant_buffer ?
          convert_info->templ->constant_buffer : "",
          convert_info->templ->func ? convert_info->templ->func : "",
          convert_info->ps_body[i]);

      ret = gst_d3d11_create_pixel_shader (device, shader_code, &ps[i]);
      g_free (shader_code);
      if (!ret) {
        GST_ERROR ("Couldn't create pixel shader");
        goto clear;
      }
    }
  }

  if (convert_info->templ->constant_buffer) {
    D3D11_BUFFER_DESC const_buffer_desc = { 0, };

    const_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    const_buffer_desc.ByteWidth = sizeof (PixelShaderColorTransform);
    const_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    const_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const_buffer_desc.MiscFlags = 0;
    const_buffer_desc.StructureByteStride = 0;

    hr = ID3D11Device_CreateBuffer (device_handle, &const_buffer_desc, NULL,
        &const_buffer);

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR ("Couldn't create constant buffer, hr: 0x%x", (guint) hr);
      ret = FALSE;
      goto clear;
    }

    gst_d3d11_device_lock (device);
    hr = ID3D11DeviceContext_Map (context_handle,
        (ID3D11Resource *) const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR ("Couldn't map constant buffer, hr: 0x%x", (guint) hr);
      gst_d3d11_device_unlock (device);
      ret = FALSE;
      goto clear;
    }

    memcpy (map.pData, &convert_info->transform,
        sizeof (PixelShaderColorTransform));

    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) const_buffer, 0);
    gst_d3d11_device_unlock (device);
  }

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "TEXCOORD";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  if (!gst_d3d11_create_vertex_shader (device, templ_vertex_shader,
          input_desc, G_N_ELEMENTS (input_desc), &vs, &layout)) {
    GST_ERROR ("Couldn't vertex pixel shader");
    ret = FALSE;
    goto clear;
  }

  /* setup vertext buffer and index buffer */
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &vertex_buffer);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
    ret = FALSE;
    goto clear;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * index_count;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &index_buffer);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create index buffer, hr: 0x%x", (guint) hr);
    ret = FALSE;
    goto clear;
  }

  gst_d3d11_device_lock (device);
  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    gst_d3d11_device_unlock (device);
    ret = FALSE;
    goto clear;
  }

  vertex_data = (VertexData *) map.pData;

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't map index buffer, hr: 0x%x", (guint) hr);
    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) vertex_buffer, 0);
    gst_d3d11_device_unlock (device);
    ret = FALSE;
    goto clear;
  }

  indices = (WORD *) map.pData;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.x = 0.0f;
  vertex_data[0].texture.y = 1.0f;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.x = 0.0f;
  vertex_data[1].texture.y = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.x = 1.0f;
  vertex_data[2].texture.y = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.x = 1.0f;
  vertex_data[3].texture.y = 1.0f;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  ID3D11DeviceContext_Unmap (context_handle,
      (ID3D11Resource *) vertex_buffer, 0);
  ID3D11DeviceContext_Unmap (context_handle,
      (ID3D11Resource *) index_buffer, 0);
  gst_d3d11_device_unlock (device);

  self->quad[0] = gst_d3d11_quad_new (device,
      ps[0], vs, layout, sampler, NULL, NULL, const_buffer, vertex_buffer,
      sizeof (VertexData), index_buffer, DXGI_FORMAT_R16_UINT, index_count);

  if (ps[1]) {
    self->quad[1] = gst_d3d11_quad_new (device,
        ps[1], vs, layout, sampler, NULL, NULL, const_buffer, vertex_buffer,
        sizeof (VertexData), index_buffer, DXGI_FORMAT_R16_UINT, index_count);
  }

  self->num_input_view = GST_VIDEO_INFO_N_PLANES (in_info);
  self->num_output_view = GST_VIDEO_INFO_N_PLANES (out_info);

  /* holds vertex buffer for crop rect update */
  self->vertex_buffer = vertex_buffer;
  ID3D11Buffer_AddRef (vertex_buffer);

  self->crop_rect.left = 0;
  self->crop_rect.top = 0;
  self->crop_rect.right = GST_VIDEO_INFO_WIDTH (in_info);
  self->crop_rect.bottom = GST_VIDEO_INFO_HEIGHT (in_info);

  self->input_texture_width = GST_VIDEO_INFO_WIDTH (in_info);
  self->input_texture_height = GST_VIDEO_INFO_HEIGHT (in_info);

clear:
  for (i = 0; i < CONVERTER_MAX_QUADS; i++) {
    if (ps[i])
      ID3D11PixelShader_Release (ps[i]);
  }
  if (vs)
    ID3D11VertexShader_Release (vs);
  if (layout)
    ID3D11InputLayout_Release (layout);
  if (sampler)
    ID3D11SamplerState_Release (sampler);
  if (const_buffer)
    ID3D11Buffer_Release (const_buffer);
  if (vertex_buffer)
    ID3D11Buffer_Release (vertex_buffer);
  if (index_buffer)
    ID3D11Buffer_Release (index_buffer);

  return ret;
}

GstD3D11ColorConverter *
gst_d3d11_color_converter_new (GstD3D11Device * device,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  const GstVideoInfo *unknown_info;
  const GstD3D11Format *in_d3d11_format;
  const GstD3D11Format *out_d3d11_format;
  gboolean is_supported = FALSE;
  MatrixData matrix;
  GstD3D11ColorConverter *converter = NULL;
  gboolean ret;
  gint i;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);

  GST_DEBUG ("Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  in_d3d11_format =
      gst_d3d11_device_format_from_gst (device,
      GST_VIDEO_INFO_FORMAT (in_info));
  if (!in_d3d11_format) {
    unknown_info = in_info;
    goto format_unknown;
  }

  out_d3d11_format =
      gst_d3d11_device_format_from_gst (device,
      GST_VIDEO_INFO_FORMAT (out_info));
  if (!out_d3d11_format) {
    unknown_info = out_info;
    goto format_unknown;
  }

  converter = g_new0 (GstD3D11ColorConverter, 1);
  converter->device = gst_object_ref (device);

  if (GST_VIDEO_INFO_IS_RGB (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      is_supported =
          setup_convert_info_rgb_to_rgb (converter, in_info, out_info);
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      is_supported =
          setup_convert_info_rgb_to_yuv (converter, in_info, out_info);
    }
  } else if (GST_VIDEO_INFO_IS_YUV (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      is_supported =
          setup_convert_info_yuv_to_rgb (converter, in_info, out_info);
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      is_supported =
          setup_convert_info_yuv_to_yuv (converter, in_info, out_info);
    }
  }

  if (!is_supported) {
    goto conversion_not_supported;
  }

  if (converter_get_matrix (converter, &matrix, in_info, out_info)) {
    PixelShaderColorTransform *transform = &converter->convert_info.transform;

    /* padding the last column for 16bytes alignment */
    transform->trans_matrix[0] = matrix.dm[0][0];
    transform->trans_matrix[1] = matrix.dm[0][1];
    transform->trans_matrix[2] = matrix.dm[0][2];
    transform->trans_matrix[3] = 0;
    transform->trans_matrix[4] = matrix.dm[1][0];
    transform->trans_matrix[5] = matrix.dm[1][1];
    transform->trans_matrix[6] = matrix.dm[1][2];
    transform->trans_matrix[7] = 0;
    transform->trans_matrix[8] = matrix.dm[2][0];
    transform->trans_matrix[9] = matrix.dm[2][1];
    transform->trans_matrix[10] = matrix.dm[2][2];
    transform->trans_matrix[11] = 0;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++) {
    converter->viewport[i].TopLeftX = 0;
    converter->viewport[i].TopLeftY = 0;
    converter->viewport[i].Width = GST_VIDEO_INFO_COMP_WIDTH (out_info, i);
    converter->viewport[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, i);
    converter->viewport[i].MinDepth = 0.0f;
    converter->viewport[i].MaxDepth = 1.0f;
  }

  ret = gst_d3d11_color_convert_setup_shader (converter,
      device, in_info, out_info);

  if (!ret) {
    GST_ERROR ("Couldn't setup shader");
    gst_d3d11_color_converter_free (converter);
    converter = NULL;
  } else {
    converter->in_info = *in_info;
    converter->out_info = *out_info;
  }

  return converter;

  /* ERRORS */
format_unknown:
  {
    GST_ERROR ("%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (unknown_info)));
    return NULL;
  }
conversion_not_supported:
  {
    GST_ERROR ("Conversion %s to %s not supported",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    gst_d3d11_color_converter_free (converter);
    return NULL;
  }
}

void
gst_d3d11_color_converter_free (GstD3D11ColorConverter * converter)
{
  gint i;

  g_return_if_fail (converter != NULL);

  for (i = 0; i < CONVERTER_MAX_QUADS; i++) {
    if (converter->quad[i])
      gst_d3d11_quad_free (converter->quad[i]);

    g_free (converter->convert_info.ps_body[i]);
  }

  if (converter->vertex_buffer)
    ID3D11Buffer_Release (converter->vertex_buffer);

  gst_clear_object (&converter->device);
  g_free (converter);
}

/* must be called with gst_d3d11_device_lock since ID3D11DeviceContext is not
 * thread-safe */
static gboolean
gst_d3d11_color_converter_update_vertex_buffer (GstD3D11ColorConverter * self)
{
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  ID3D11DeviceContext *context_handle;
  HRESULT hr;
  FLOAT u, v;
  const RECT *crop_rect = &self->crop_rect;
  gint texture_width = self->input_texture_width;
  gint texture_height = self->input_texture_height;

  context_handle = gst_d3d11_device_get_device_context_handle (self->device);

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) self->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD,
      0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR ("Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  vertex_data = (VertexData *) map.pData;

  /* bottom left */
  u = (crop_rect->left / (gfloat) texture_width) - 0.5f / texture_width;
  v = (crop_rect->bottom / (gfloat) texture_height) - 0.5f / texture_height;

  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.x = u;
  vertex_data[0].texture.y = v;

  /* top left */
  u = (crop_rect->left / (gfloat) texture_width) - 0.5f / texture_width;
  v = (crop_rect->top / (gfloat) texture_height) - 0.5f / texture_height;

  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.x = u;
  vertex_data[1].texture.y = v;

  /* top right */
  u = (crop_rect->right / (gfloat) texture_width) - 0.5f / texture_width;
  v = (crop_rect->top / (gfloat) texture_height) - 0.5f / texture_height;

  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.x = u;
  vertex_data[2].texture.y = v;

  /* bottom right */
  u = (crop_rect->right / (gfloat) texture_width) - 0.5f / texture_width;
  v = (crop_rect->bottom / (gfloat) texture_height) - 0.5f / texture_height;

  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.x = u;
  vertex_data[3].texture.y = v;

  ID3D11DeviceContext_Unmap (context_handle,
      (ID3D11Resource *) self->vertex_buffer, 0);

  self->update_vertex = FALSE;

  return TRUE;
}

gboolean
gst_d3d11_color_converter_convert (GstD3D11ColorConverter * converter,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (converter != NULL, FALSE);
  g_return_val_if_fail (srv != NULL, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);

  gst_d3d11_device_lock (converter->device);
  ret = gst_d3d11_color_converter_convert_unlocked (converter, srv, rtv);
  gst_d3d11_device_unlock (converter->device);

  return ret;
}

gboolean
gst_d3d11_color_converter_convert_unlocked (GstD3D11ColorConverter * converter,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;
  ID3D11Resource *resource;
  D3D11_TEXTURE2D_DESC desc;

  g_return_val_if_fail (converter != NULL, FALSE);
  g_return_val_if_fail (srv != NULL, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);

  /* check texture resolution and update crop area */
  ID3D11ShaderResourceView_GetResource (srv[0], &resource);
  ID3D11Texture2D_GetDesc ((ID3D11Texture2D *) resource, &desc);
  ID3D11Resource_Release (resource);

  if (converter->update_vertex ||
      desc.Width != converter->input_texture_width ||
      desc.Height != converter->input_texture_height) {
    GST_DEBUG ("Update vertext buffer, texture resolution: %dx%d",
        desc.Width, desc.Height);

    converter->input_texture_width = desc.Width;
    converter->input_texture_height = desc.Height;

    if (!gst_d3d11_color_converter_update_vertex_buffer (converter)) {
      GST_ERROR ("Cannot update vertex buffer");
      return FALSE;
    }
  }

  ret = gst_d3d11_draw_quad_unlocked (converter->quad[0], converter->viewport,
      1, srv, converter->num_input_view, rtv, 1, NULL);

  if (!ret)
    return FALSE;

  if (converter->quad[1]) {
    ret = gst_d3d11_draw_quad_unlocked (converter->quad[1],
        &converter->viewport[1], converter->num_output_view - 1,
        srv, converter->num_input_view, &rtv[1], converter->num_output_view - 1,
        NULL);

    if (!ret)
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_color_converter_update_rect (GstD3D11ColorConverter * converter,
    RECT * rect)
{
  g_return_val_if_fail (converter != NULL, FALSE);
  g_return_val_if_fail (rect != NULL, FALSE);

  converter->viewport[0].TopLeftX = rect->left;
  converter->viewport[0].TopLeftY = rect->top;
  converter->viewport[0].Width = rect->right - rect->left;
  converter->viewport[0].Height = rect->bottom - rect->top;

  switch (GST_VIDEO_INFO_FORMAT (&converter->out_info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_I420_10LE:{
      gint i;
      converter->viewport[1].TopLeftX = converter->viewport[0].TopLeftX / 2;
      converter->viewport[1].TopLeftY = converter->viewport[0].TopLeftY / 2;
      converter->viewport[1].Width = converter->viewport[0].Width / 2;
      converter->viewport[1].Height = converter->viewport[0].Height / 2;

      for (i = 2; i < GST_VIDEO_INFO_N_PLANES (&converter->out_info); i++)
        converter->viewport[i] = converter->viewport[1];

      break;
    }
    default:
      if (converter->num_output_view > 1)
        g_assert_not_reached ();
      break;
  }

  return TRUE;
}

gboolean
gst_d3d11_color_converter_update_crop_rect (GstD3D11ColorConverter * converter,
    RECT * crop_rect)
{
  g_return_val_if_fail (converter != NULL, FALSE);
  g_return_val_if_fail (crop_rect != NULL, FALSE);

  if (converter->crop_rect.left != crop_rect->left ||
      converter->crop_rect.top != crop_rect->top ||
      converter->crop_rect.right != crop_rect->right ||
      converter->crop_rect.bottom != crop_rect->bottom) {
    converter->crop_rect = *crop_rect;

    /* vertex buffer will be updated on next convert() call */
    converter->update_vertex = TRUE;
  }

  return TRUE;
}
