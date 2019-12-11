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

#if 0
static const PixelShaderTemplate templ_RGB_to_YUV =
    { COLOR_TRANSFORM_COEFF, HLSL_FUNC_RGB_TO_YUV };
#endif

static const gchar templ_REORDER_BODY[] =
    "  float4 sample;\n"
    "  sample  = shaderTexture[0].Sample(samplerState, input.Texture);\n"
    /* alpha channel */
    "  %s\n"
    "  return sample;\n";

static const gchar templ_VUYA_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).z;\n"
    "  sample.y  = shaderTexture[0].Sample(samplerState, input.Texture).y;\n"
    "  sample.z  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.a  = shaderTexture[0].Sample(samplerState, input.Texture).a;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = sample.a;\n"
    "  return rgba;\n";

#if 0
static const gchar templ_RGB_to_VUYA_BODY[] =
    "  float4 sample, vuya;\n"
    "  sample = shaderTexture[0].Sample(samplerState, input.Texture);\n"
    "  vuya.zyx = rgb_to_yuv (sample.rgb);\n"
    "  vuya.a = %s;\n"
    "  return vuya;\n";
#endif

/* YUV to RGB conversion */
static const gchar templ_PLANAR_YUV_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.y  = shaderTexture[1].Sample(samplerState, input.Texture).x;\n"
    "  sample.z  = shaderTexture[2].Sample(samplerState, input.Texture).x;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

static const gchar templ_PLANAR_YUV_HIGH_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.y  = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.z  = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

/* FIXME: add RGB to planar */

static const gchar templ_SEMI_PLANAR_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, input.Texture).xy;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

/* FIXME: add RGB to semi-planar */

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
    /* rgb <-> yuv function */
    "%s\n"
    "float4 main(PS_INPUT input): SV_TARGET\n"
    "{\n"
    "%s"
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
  gchar *ps_body;
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

  GstD3D11Quad *quad;

  D3D11_VIEWPORT viewport;

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
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (in_info);

#define IS_RGBX_FORMAT(f) \
  ((f) == GST_VIDEO_FORMAT_RGBx || \
   (f) == GST_VIDEO_FORMAT_xRGB || \
   (f) == GST_VIDEO_FORMAT_BGRx || \
   (f) == GST_VIDEO_FORMAT_xBGR)

  convert_info->templ = &templ_REORDER;
  convert_info->ps_body = g_strdup_printf (templ_REORDER_BODY,
      IS_RGBX_FORMAT (in_format) ? "sample.a = 1.0f;" : "");

#undef IS_RGBX_FORMAT

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
      info->ps_body = g_strdup_printf (templ_VUYA_to_RGB_BODY);
      break;
    case GST_VIDEO_FORMAT_I420:
      info->ps_body = g_strdup_printf (templ_PLANAR_YUV_to_RGB_BODY);
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      info->ps_body =
          g_strdup_printf (templ_PLANAR_YUV_HIGH_to_RGB_BODY, 64, 64, 64);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
      info->ps_body = g_strdup_printf (templ_SEMI_PLANAR_to_RGB_BODY);
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
  GST_FIXME ("Implement RGB to YUV format conversion");
  return FALSE;
}

static gboolean
setup_convert_info_yuv_to_yuv (GstD3D11ColorConverter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GST_FIXME ("Implement YUV to YUV format conversion");
  return FALSE;
}

typedef struct
{
  GstD3D11ColorConverter *self;
  GstVideoInfo *in_info;
  GstVideoInfo *out_info;
  gboolean ret;
} SetupShaderData;

static void
gst_d3d11_color_convert_setup_shader (GstD3D11Device * device,
    SetupShaderData * data)
{
  GstD3D11ColorConverter *self = data->self;
  HRESULT hr;
  D3D11_SAMPLER_DESC sampler_desc = { 0, };
  D3D11_INPUT_ELEMENT_DESC input_desc[2] = { 0, };
  D3D11_BUFFER_DESC buffer_desc = { 0, };
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  gchar *shader_code = NULL;
  ConvertInfo *convert_info = &self->convert_info;
  ID3D11PixelShader *ps = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11InputLayout *layout = NULL;
  ID3D11SamplerState *sampler = NULL;
  ID3D11Buffer *const_buffer = NULL;
  ID3D11Buffer *vertex_buffer = NULL;
  ID3D11Buffer *index_buffer = NULL;
  const guint index_count = 2 * 3;

  data->ret = TRUE;

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
  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("Couldn't create sampler state, hr: 0x%x", (guint) hr);
    data->ret = FALSE;
    goto clear;
  }

  shader_code = g_strdup_printf (templ_pixel_shader,
      convert_info->templ->constant_buffer ?
      convert_info->templ->constant_buffer : "",
      convert_info->templ->func ? convert_info->templ->func : "",
      convert_info->ps_body);

  GST_LOG ("Create Pixel Shader \n%s", shader_code);

  if (!gst_d3d11_create_pixel_shader (device, shader_code, &ps)) {
    GST_ERROR ("Couldn't create pixel shader");

    g_free (shader_code);
    data->ret = FALSE;
    goto clear;
  }

  g_free (shader_code);

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

    if (!gst_d3d11_result (hr)) {
      GST_ERROR ("Couldn't create constant buffer, hr: 0x%x", (guint) hr);
      data->ret = FALSE;
      goto clear;
    }

    hr = ID3D11DeviceContext_Map (context_handle,
        (ID3D11Resource *) const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

    if (!gst_d3d11_result (hr)) {
      GST_ERROR ("Couldn't map constant buffer, hr: 0x%x", (guint) hr);
      data->ret = FALSE;
      goto clear;
    }

    memcpy (map.pData, &convert_info->transform,
        sizeof (PixelShaderColorTransform));

    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) const_buffer, 0);
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
    data->ret = FALSE;
    goto clear;
  }

  /* setup vertext buffer and index buffer */
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &vertex_buffer);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
    data->ret = FALSE;
    goto clear;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * index_count;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &index_buffer);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("Couldn't create index buffer, hr: 0x%x", (guint) hr);
    data->ret = FALSE;
    goto clear;
  }

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    data->ret = FALSE;
    goto clear;
  }

  vertex_data = (VertexData *) map.pData;

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR ("Couldn't map index buffer, hr: 0x%x", (guint) hr);
    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) vertex_buffer, 0);
    data->ret = FALSE;
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

  self->quad = gst_d3d11_quad_new (device,
      ps, vs, layout, sampler, const_buffer, vertex_buffer, sizeof (VertexData),
      index_buffer, DXGI_FORMAT_R16_UINT, index_count);

  self->num_input_view = GST_VIDEO_INFO_N_PLANES (data->in_info);
  self->num_output_view = GST_VIDEO_INFO_N_PLANES (data->out_info);

clear:
  if (ps)
    ID3D11PixelShader_Release (ps);
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

  return;
}

GstD3D11ColorConverter *
gst_d3d11_color_converter_new (GstD3D11Device * device,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  SetupShaderData data;
  const GstVideoInfo *unknown_info;
  const GstD3D11Format *in_d3d11_format;
  const GstD3D11Format *out_d3d11_format;
  gboolean is_supported = FALSE;
  MatrixData matrix;
  GstD3D11ColorConverter *converter = NULL;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);

  GST_DEBUG ("Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  in_d3d11_format = gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (in_info));
  if (!in_d3d11_format) {
    unknown_info = in_info;
    goto format_unknown;
  }

  out_d3d11_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (out_info));
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

  converter->viewport.TopLeftX = 0;
  converter->viewport.TopLeftY = 0;
  converter->viewport.Width = GST_VIDEO_INFO_WIDTH (out_info);
  converter->viewport.Height = GST_VIDEO_INFO_HEIGHT (out_info);
  converter->viewport.MinDepth = 0.0f;
  converter->viewport.MaxDepth = 1.0f;

  data.self = converter;
  data.in_info = in_info;
  data.out_info = out_info;
  gst_d3d11_device_thread_add (device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_color_convert_setup_shader, &data);

  if (!data.ret || !converter->quad) {
    GST_ERROR ("Couldn't setup shader");
    gst_d3d11_color_converter_free (converter);
    converter = NULL;
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
  g_return_if_fail (converter != NULL);

  if (converter->quad)
    gst_d3d11_quad_free (converter->quad);

  gst_clear_object (&converter->device);
  g_free (converter->convert_info.ps_body);
  g_free (converter);
}

typedef struct
{
  GstD3D11ColorConverter *self;
  ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES];
  ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES];

  gboolean ret;
} DoConvertData;

static void
do_convert (GstD3D11Device * device, DoConvertData * data)
{
  GstD3D11ColorConverter *self = data->self;

  data->ret =
      gst_d3d11_draw_quad (self->quad, &self->viewport, 1,
      data->srv, self->num_input_view, data->rtv, self->num_output_view);
}

gboolean
gst_d3d11_color_converter_convert (GstD3D11ColorConverter * converter,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  DoConvertData data = { 0, };
  gint i;

  g_return_val_if_fail (converter != NULL, FALSE);
  g_return_val_if_fail (srv != NULL, FALSE);
  g_return_val_if_fail (rtv != NULL, FALSE);

  data.self = converter;

  for (i = 0; i < converter->num_input_view; i++)
    data.srv[i] = srv[i];

  for (i = 0; i < converter->num_output_view; i++)
    data.rtv[i] = rtv[i];

  data.ret = TRUE;

  gst_d3d11_device_thread_add (converter->device,
      (GstD3D11DeviceThreadFunc) do_convert, &data);

  return data.ret;
}

gboolean
gst_d3d11_color_converter_update_rect (GstD3D11ColorConverter * converter,
    RECT * rect)
{
  g_return_val_if_fail (converter != NULL, FALSE);

  converter->viewport.TopLeftX = rect->left;
  converter->viewport.TopLeftY = rect->top;
  converter->viewport.Width = rect->right - rect->left;
  converter->viewport.Height = rect->bottom - rect->top;

  return TRUE;
}
