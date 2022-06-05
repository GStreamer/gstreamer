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

#include "gstd3d11converter.h"
#include "gstd3d11shader.h"
#include "gstd3d11pluginutils.h"
#include <wrl.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_converter_debug);
#define GST_CAT_DEFAULT gst_d3d11_converter_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

#define CONVERTER_MAX_QUADS 2

/* *INDENT-OFF* */
typedef struct
{
  /* + 1 for 16bytes alignment  */
  FLOAT coeffX[4];
  FLOAT coeffY[4];
  FLOAT coeffZ[4];
  FLOAT offset[4];
  FLOAT min[4];
  FLOAT max[4];
} PSColorSpace;

typedef struct
{
  PSColorSpace color_space_buf;
  FLOAT AlphaMul;
  FLOAT padding[3];
} PSConstBuffer;

typedef struct
{
  struct {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct {
    FLOAT u;
    FLOAT v;
  } texture;
} VertexData;

/* output struct */
static const gchar templ_OUTPUT_SINGLE_PLANE[] =
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane_0: SV_TARGET0;\n"
    "};";

static const gchar templ_OUTPUT_TWO_PLANES[] =
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane_0: SV_TARGET0;\n"
    "  float4 Plane_1: SV_TARGET1;\n"
    "};";

static const gchar templ_OUTPUT_THREE_PLANES[] =
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane_0: SV_TARGET0;\n"
    "  float4 Plane_1: SV_TARGET1;\n"
    "  float4 Plane_2: SV_TARGET2;\n"
    "};";

typedef struct
{
  const gchar *output_template;
  guint num_rtv;
} PSOutputType;

enum
{
  OUTPUT_SINGLE_PLANE = 0,
  OUTPUT_TWO_PLANES,
  OUTPUT_THREE_PLANES,
};

static const PSOutputType output_types[] = {
  {templ_OUTPUT_SINGLE_PLANE, 1},
  {templ_OUTPUT_TWO_PLANES, 2},
  {templ_OUTPUT_THREE_PLANES, 3},
};

/* colorspace conversion */
static const gchar templ_COLOR_SPACE_IDENTITY[] =
    "{\n"
    "  return sample;\n"
    "}";

static const gchar templ_COLOR_SPACE_CONVERT[] =
    "{\n"
    "  float3 out_space;\n"
    "  out_space.x = dot (coeff.CoeffX, sample);\n"
    "  out_space.y = dot (coeff.CoeffY, sample);\n"
    "  out_space.z = dot (coeff.CoeffZ, sample);\n"
    "  out_space += coeff.Offset;\n"
    "  return clamp (out_space, coeff.Min, coeff.Max);\n"
    "}";

static const gchar templ_COLOR_SPACE_CONVERT_LUMA[] =
    "{\n"
    "  float3 out_space;\n"
    "  out_space.x = dot (coeff.CoeffX, sample) + coeff.Offset.x;\n"
    "  out_space.x = clamp (out_space.x, coeff.Min.x, coeff.Max.x);\n"
    "  out_space.y = 0.5;\n"
    "  out_space.z = 0.5;\n"
    "  return out_space;\n"
    "}";

static const gchar templ_COLOR_SPACE_CONVERT_CHROMA[] =
    "{\n"
    "  float3 out_space;\n"
    "  out_space.x = 0.0;\n"
    "  out_space.y = dot (coeff.CoeffY, sample) + coeff.Offset.y;\n"
    "  out_space.z = dot (coeff.CoeffZ, sample) + coeff.Offset.z;\n"
    "  return clamp (out_space, coeff.Min, coeff.Max);\n"
    "}";

static const gchar templ_COLOR_SPACE_GRAY_TO_RGB[] =
    "{\n"
    "  return float3 (sample.x, sample.x, sample.x);\n"
    "}";

static const gchar templ_COLOR_SPACE_GRAY_TO_RGB_RANGE_ADJUST[] =
    "{\n"
    "  float gray;\n"
    "  gray = coeff.CoeffX.x * sample.x + coeff.Offset.x;\n"
    "  gray = clamp (gray, coeff.Min.x, coeff.Max.x);\n"
    "  return float3 (gray, gray, gray);\n"
    "}";

/* sampling */
static const gchar templ_SAMPLE_DEFAULT[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  return shaderTexture[0].Sample(samplerState, uv);\n"
    "}";

static const gchar templ_SAMPLE_VUYA[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  return shaderTexture[0].Sample(samplerState, uv).%c%c%c%c;\n"
    "}";

static const gchar templ_SAMPLE_YUV_LUMA[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.x = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.y = 0.5;\n"
    "  sample.z = 0.5;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_YUV_LUMA_SCALED[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.x = saturate (shaderTexture[0].Sample(samplerState, uv).x * %d);\n"
    "  sample.y = 0.5;\n"
    "  sample.z = 0.5;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_SEMI_PLANAR[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, uv).%c%c;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_SEMI_PLANAR_CHROMA[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.x = 0.0;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, uv).%c%c;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_PLANAR[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float3 sample;\n"
    "  sample.x = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[1].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[2].Sample(samplerState, uv).x;\n"
    "  return float4 (saturate(sample * %d), 1.0);\n"
    "}";

static const gchar templ_SAMPLE_PLANAR_CHROMA[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float3 sample;\n"
    "  sample.x = 0.0;\n"
    "  sample.%c = shaderTexture[1].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[2].Sample(samplerState, uv).x;\n"
    "  return float4 (saturate(sample * %d), 1.0);\n"
    "}";

static const gchar templ_SAMPLE_YUV_PACKED[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.xyz = shaderTexture[0].Sample(samplerState, uv).%c%c%c;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_GRAY[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.x = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.y = 0.5;\n"
    "  sample.z = 0.5;\n"
    "  sample.a = 1.0;\n"
    "  return sample;\n"
    "}";

static const gchar templ_SAMPLE_GRAY_CHROMA[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  return float4 (0.0, 0.5, 0.5, 1.0);\n"
    "}";

/* building output */
static const gchar templ_OUTPUT_DEFAULT[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = sample;\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_VUYA[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  float4 vuya;\n"
    "  vuya.%c%c%c = sample.xyz;\n"
    "  vuya.%c = sample.a;\n"
    "  output.Plane_0 = vuya;\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_LUMA[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.x, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_CHROMA_SEMI_PLANAR[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.%c%c, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_LUMA_SCALED[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.x / %d, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_CHROMA_PLANAR[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_CHROMA_PLANAR_SCALED[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.%c / %d, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (sample.%c / %d, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_Y444[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.x, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (sample.y, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (sample.z, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_Y444_SCALED[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  float3 scaled = sample.xyz / %d;\n"
    "  output.Plane_0 = float4 (scaled.x, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (scaled.y, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (scaled.z, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_pixel_shader[] =
    "struct PSColorSpace\n"
    "{\n"
    "  float3 CoeffX;\n"
    "  float3 CoeffY;\n"
    "  float3 CoeffZ;\n"
    "  float3 Offset;\n"
    "  float3 Min;\n"
    "  float3 Max;\n"
    "  float padding;\n"
    "};\n"
    "cbuffer PsConstBuffer : register(b0)\n"
    "{\n"
    /* RGB <-> YUV conversion */
    "  PSColorSpace colorSpace;\n"
    "  float AlphaMul;\n"
    "};\n"
    "Texture2D shaderTexture[4];\n"
    "SamplerState samplerState : register(s0);\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    /* struct PS_OUTPUT */
    "%s\n"
    /* sample_texture() function */
    "%s\n"
    "float3 to_rgb (float3 sample, PSColorSpace coeff)\n"
    "%s\n"
    "float3 to_yuv (float3 sample, PSColorSpace coeff)\n"
    "%s\n"
    /* build_output() function */
    "%s\n"
    "PS_OUTPUT main(PS_INPUT input)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample = sample_texture (input.Texture);\n"
    "  sample.a = saturate (sample.a * AlphaMul);\n"
    "  sample.xyz = to_rgb (sample.xyz, colorSpace);\n"
    "  sample.xyz = to_yuv (sample.xyz, colorSpace);\n"
    "  return build_output (sample);\n"
    "}\n";

static const gchar templ_vertex_shader[] =
    "struct VS_INPUT\n"
    "{\n"
    "  float4 Position : POSITION;\n"
    "  float2 Texture : TEXCOORD;\n"
    "};\n"
    "\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}\n";
/* *INDENT-ON* */

typedef struct
{
  const PSOutputType *ps_output[CONVERTER_MAX_QUADS];
  gchar *sample_texture_func[CONVERTER_MAX_QUADS];
  const gchar *to_rgb_func[CONVERTER_MAX_QUADS];
  const gchar *to_yuv_func[CONVERTER_MAX_QUADS];
  gchar *build_output_func[CONVERTER_MAX_QUADS];
} ConvertInfo;

struct _GstD3D11Converter
{
  GstD3D11Device *device;
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  guint num_input_view;
  guint num_output_view;

  ID3D11Buffer *vertex_buffer;
  ID3D11Buffer *index_buffer;
  ID3D11Buffer *const_buffer;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *linear_sampler;
  ID3D11PixelShader *ps[CONVERTER_MAX_QUADS];
  D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];

  RECT src_rect;
  RECT dest_rect;
  gint input_texture_width;
  gint input_texture_height;
  gboolean update_vertex;
  gboolean update_alpha;

  ConvertInfo convert_info;
  PSConstBuffer const_data;

  GstStructure *config;
};

static gdouble
get_opt_double (GstD3D11Converter * self, const gchar * opt, gdouble def)
{
  gdouble res;
  if (!gst_structure_get_double (self->config, opt, &res))
    res = def;

  return res;
}

#define DEFAULT_OPT_ALPHA_VALUE 1.0

#define GET_OPT_ALPHA_VALUE(c) get_opt_double(c, \
    GST_D3D11_CONVERTER_OPT_ALPHA_VALUE, DEFAULT_OPT_ALPHA_VALUE);

static gboolean
get_packed_yuv_components (GstVideoFormat format, gchar * y, gchar * u,
    gchar * v)
{
  switch (format) {
    case GST_VIDEO_FORMAT_Y410:
      *y = 'g';
      *u = 'r';
      *v = 'b';
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static void
get_planar_component (GstVideoFormat format, gchar * u, gchar * v,
    guint * scale)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
      *scale = (1 << 6);
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
      *scale = (1 << 4);
      break;
    default:
      *scale = 1;
      break;
  }

  if (format == GST_VIDEO_FORMAT_YV12) {
    *u = 'z';
    *v = 'y';
  } else {
    *u = 'y';
    *v = 'z';
  }
}

static void
get_semi_planar_component (GstVideoFormat format, gchar * u, gchar * v,
    gboolean is_sampling)
{
  if (format == GST_VIDEO_FORMAT_NV21) {
    if (is_sampling) {
      *u = 'y';
      *v = 'x';
    } else {
      *u = 'z';
      *v = 'y';
    }
  } else {
    if (is_sampling) {
      *u = 'x';
      *v = 'y';
    } else {
      *u = 'y';
      *v = 'z';
    }
  }
}

static void
get_vuya_component (GstVideoFormat format, gchar * y, gchar * u,
    gchar * v, gchar * a)
{
  switch (format) {
    case GST_VIDEO_FORMAT_VUYA:
      if (y)
        *y = 'z';
      if (u)
        *u = 'y';
      if (v)
        *v = 'x';
      if (a)
        *a = 'w';
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:
      if (y)
        *y = 'g';
      if (u)
        *u = 'b';
      if (v)
        *v = 'a';
      if (a)
        *a = 'r';
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static gboolean
gst_d3d11_color_convert_setup_shader (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstD3D11Device *device = self->device;
  HRESULT hr;
  D3D11_SAMPLER_DESC sampler_desc;
  D3D11_INPUT_ELEMENT_DESC input_desc[2];
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  ConvertInfo *cinfo = &self->convert_info;
  ComPtr < ID3D11PixelShader > ps[CONVERTER_MAX_QUADS];
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11SamplerState > linear_sampler;
  ComPtr < ID3D11Buffer > const_buffer;
  ComPtr < ID3D11Buffer > vertex_buffer;
  ComPtr < ID3D11Buffer > index_buffer;
  gint i;
  gboolean ret;

  memset (&sampler_desc, 0, sizeof (sampler_desc));
  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

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

  hr = device_handle->CreateSamplerState (&sampler_desc, &linear_sampler);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create samplerState state, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  for (i = 0; i < CONVERTER_MAX_QUADS; i++) {
    gchar *shader_code = nullptr;

    if (cinfo->sample_texture_func[i]) {
      g_assert (cinfo->ps_output[i] != nullptr);

      shader_code = g_strdup_printf (templ_pixel_shader,
          cinfo->ps_output[i]->output_template, cinfo->sample_texture_func[i],
          cinfo->to_rgb_func[i], cinfo->to_yuv_func[i],
          cinfo->build_output_func[i]);

      ret = gst_d3d11_create_pixel_shader (device, shader_code, &ps[i]);
      g_free (shader_code);
      if (!ret) {
        return FALSE;
      }
    }
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
    return FALSE;
  }

  /* const buffer */
  G_STATIC_ASSERT (sizeof (PSConstBuffer) % 16 == 0);
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (PSConstBuffer);
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &const_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create constant buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  /* setup vertext buffer and index buffer */
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &index_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't create index buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  gst_d3d11_device_lock (device);
  hr = context_handle->Map (const_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't map constant buffer, hr: 0x%x", (guint) hr);
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  memcpy (map.pData, &self->const_data, sizeof (PSConstBuffer));
  context_handle->Unmap (const_buffer.Get (), 0);

  hr = context_handle->Map (vertex_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  vertex_data = (VertexData *) map.pData;

  hr = context_handle->Map (index_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR ("Couldn't map index buffer, hr: 0x%x", (guint) hr);
    context_handle->Unmap (vertex_buffer.Get (), 0);
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  indices = (WORD *) map.pData;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.u = 0.0f;
  vertex_data[0].texture.v = 1.0f;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.u = 0.0f;
  vertex_data[1].texture.v = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.u = 1.0f;
  vertex_data[2].texture.v = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.u = 1.0f;
  vertex_data[3].texture.v = 1.0f;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  context_handle->Unmap (vertex_buffer.Get (), 0);
  context_handle->Unmap (index_buffer.Get (), 0);
  gst_d3d11_device_unlock (device);

  /* holds vertex buffer for crop rect update */
  self->vertex_buffer = vertex_buffer.Detach ();
  self->index_buffer = index_buffer.Detach ();
  self->const_buffer = const_buffer.Detach ();
  self->vs = vs.Detach ();
  self->layout = layout.Detach ();
  self->linear_sampler = linear_sampler.Detach ();
  self->ps[0] = ps[0].Detach ();
  if (ps[1])
    self->ps[1] = ps[1].Detach ();

  self->src_rect.left = 0;
  self->src_rect.top = 0;
  self->src_rect.right = GST_VIDEO_INFO_WIDTH (in_info);
  self->src_rect.bottom = GST_VIDEO_INFO_HEIGHT (in_info);

  self->dest_rect.left = 0;
  self->dest_rect.top = 0;
  self->dest_rect.right = GST_VIDEO_INFO_WIDTH (out_info);
  self->dest_rect.bottom = GST_VIDEO_INFO_HEIGHT (out_info);

  self->input_texture_width = GST_VIDEO_INFO_WIDTH (in_info);
  self->input_texture_height = GST_VIDEO_INFO_HEIGHT (in_info);

  self->num_input_view = GST_VIDEO_INFO_N_PLANES (in_info);
  self->num_output_view = GST_VIDEO_INFO_N_PLANES (out_info);

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++) {
    self->viewport[i].TopLeftX = 0;
    self->viewport[i].TopLeftY = 0;
    self->viewport[i].Width = GST_VIDEO_INFO_COMP_WIDTH (out_info, i);
    self->viewport[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, i);
    self->viewport[i].MinDepth = 0.0f;
    self->viewport[i].MaxDepth = 1.0f;
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_update_vertex_buffer (GstD3D11Converter * self)
{
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  ID3D11DeviceContext *context_handle;
  HRESULT hr;
  FLOAT x0, y0, x1, y1;
  FLOAT u0, u1, v0, v1, off_u, off_v;
  const RECT *src_rect = &self->src_rect;
  const RECT *dest_rect = &self->dest_rect;
  gint texture_width = self->input_texture_width;
  gint texture_height = self->input_texture_height;
  gdouble val;

  context_handle = gst_d3d11_device_get_device_context_handle (self->device);

  hr = context_handle->Map (self->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD,
      0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR ("Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  vertex_data = (VertexData *) map.pData;
  /* bottom left */
  if (dest_rect->left > 0) {
    gst_util_fraction_to_double (dest_rect->left,
        GST_VIDEO_INFO_WIDTH (&self->out_info), &val);
    x0 = (val * 2.0f) - 1.0f;
  } else {
    x0 = -1.0f;
  }

  if (dest_rect->bottom != GST_VIDEO_INFO_HEIGHT (&self->out_info)) {
    gst_util_fraction_to_double (dest_rect->bottom,
        GST_VIDEO_INFO_HEIGHT (&self->out_info), &val);
    y0 = (val * -2.0f) + 1.0f;
  } else {
    y0 = -1.0f;
  }

  /* top right */
  if (dest_rect->right != GST_VIDEO_INFO_WIDTH (&self->out_info)) {
    gst_util_fraction_to_double (dest_rect->right,
        GST_VIDEO_INFO_WIDTH (&self->out_info), &val);
    x1 = (val * 2.0f) - 1.0f;
  } else {
    x1 = 1.0f;
  }

  if (dest_rect->top > 0) {
    gst_util_fraction_to_double (dest_rect->top,
        GST_VIDEO_INFO_HEIGHT (&self->out_info), &val);
    y1 = (val * -2.0f) + 1.0f;
  } else {
    y1 = 1.0f;
  }

  /*
   *  (u0, v0) -- (u1, v0)
   *     |            |
   *  (u0, v1) -- (u1, v1)
   */
  off_u = 0.5f / texture_width;
  off_v = 0.5f / texture_height;

  if (src_rect->left > 0)
    u0 = (src_rect->left / (gfloat) texture_width) + off_u;
  else
    u0 = 0.0f;

  if (src_rect->right != texture_width)
    u1 = (src_rect->right / (gfloat) texture_width) - off_u;
  else
    u1 = 1.0f;

  if (src_rect->top > 0)
    v0 = (src_rect->top / (gfloat) texture_height) + off_v;
  else
    v0 = 0.0;

  if (src_rect->bottom != texture_height)
    v1 = (src_rect->bottom / (gfloat) texture_height) - off_v;
  else
    v1 = 1.0f;

  /* bottom left */
  vertex_data[0].position.x = x0;
  vertex_data[0].position.y = y0;
  vertex_data[0].texture.u = u0;
  vertex_data[0].texture.v = v1;

  /* top left */
  vertex_data[1].position.x = x0;
  vertex_data[1].position.y = y1;
  vertex_data[1].texture.u = u0;
  vertex_data[1].texture.v = v0;

  /* top right */
  vertex_data[2].position.x = x1;
  vertex_data[2].position.y = y1;
  vertex_data[2].texture.u = u1;
  vertex_data[2].texture.v = v0;

  /* bottom right */
  vertex_data[3].position.x = x1;
  vertex_data[3].position.y = y0;
  vertex_data[3].texture.u = u1;
  vertex_data[3].texture.v = v1;

  context_handle->Unmap (self->vertex_buffer, 0);

  self->update_vertex = FALSE;

  return TRUE;
}

static gboolean
copy_config (GQuark field_id, const GValue * value, GstD3D11Converter * self)
{
  gst_structure_id_set_value (self->config, field_id, value);

  return TRUE;
}

static gboolean
gst_d3d11_converter_set_config (GstD3D11Converter * converter,
    GstStructure * config)
{
  gst_structure_foreach (config, (GstStructureForeachFunc) copy_config,
      converter);
  gst_structure_free (config);

  return TRUE;
}

static gboolean
gst_d3d11_converter_prepare_output (GstD3D11Converter * self,
    const GstVideoInfo * info)
{
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (info);
  ConvertInfo *cinfo = &self->convert_info;

  switch (format) {
      /* RGB */
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
      cinfo->ps_output[0] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->build_output_func[0] = g_strdup (templ_OUTPUT_DEFAULT);
      break;
      /* VUYA */
    case GST_VIDEO_FORMAT_VUYA:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:{
      gchar y, u, v, a;

      get_vuya_component (format, &y, &u, &v, &a);
      cinfo->ps_output[0] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->build_output_func[0] =
          g_strdup_printf (templ_OUTPUT_VUYA, y, u, v, a);
      break;
    }
      /* semi-planar */
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:{
      gchar u, v;

      get_semi_planar_component (format, &u, &v, FALSE);
      cinfo->ps_output[0] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->build_output_func[0] = g_strdup (templ_OUTPUT_LUMA);

      cinfo->ps_output[1] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->build_output_func[1] =
          g_strdup_printf (templ_OUTPUT_CHROMA_SEMI_PLANAR, u, v);
      break;
    }
      /* planar */
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:{
      gchar u, v;
      guint scale;

      get_planar_component (format, &u, &v, &scale);

      cinfo->ps_output[0] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->ps_output[1] = &output_types[OUTPUT_TWO_PLANES];

      if (info->finfo->depth[0] == 8) {
        cinfo->build_output_func[0] = g_strdup (templ_OUTPUT_LUMA);
        cinfo->build_output_func[1] =
            g_strdup_printf (templ_OUTPUT_CHROMA_PLANAR, u, v);
      } else {
        cinfo->build_output_func[0] = g_strdup_printf (templ_OUTPUT_LUMA_SCALED,
            scale);
        cinfo->build_output_func[1] =
            g_strdup_printf (templ_OUTPUT_CHROMA_PLANAR_SCALED,
            u, scale, v, scale);
      }
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:{
      gchar u, v;
      guint scale;

      get_planar_component (format, &u, &v, &scale);

      cinfo->ps_output[0] = &output_types[OUTPUT_THREE_PLANES];
      if (info->finfo->depth[0] == 8) {
        cinfo->build_output_func[0] = g_strdup (templ_OUTPUT_Y444);
      } else {
        cinfo->build_output_func[0] = g_strdup_printf (templ_OUTPUT_Y444_SCALED,
            scale);
      }
      break;
    }
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
      cinfo->ps_output[0] = &output_types[OUTPUT_SINGLE_PLANE];
      cinfo->build_output_func[0] = g_strdup (templ_OUTPUT_LUMA);
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_prepare_sample_texture (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (in_info);
  gboolean out_rgb = GST_VIDEO_INFO_IS_RGB (out_info);
  gboolean out_yuv = GST_VIDEO_INFO_IS_YUV (out_info);
  gboolean out_gray = GST_VIDEO_INFO_IS_GRAY (out_info);
  ConvertInfo *cinfo = &self->convert_info;

  switch (format) {
      /* RGB */
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
      cinfo->sample_texture_func[0] = g_strdup (templ_SAMPLE_DEFAULT);
      if (cinfo->ps_output[1])
        cinfo->sample_texture_func[1] =
            g_strdup (cinfo->sample_texture_func[0]);
      break;
      /* VUYA */
    case GST_VIDEO_FORMAT_VUYA:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:{
      gchar y, u, v, a;

      get_vuya_component (format, &y, &u, &v, &a);
      cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_VUYA,
          y, u, v, a);
      if (cinfo->ps_output[1]) {
        cinfo->sample_texture_func[1] =
            g_strdup (cinfo->sample_texture_func[0]);
      }
      break;
    }
      /* semi-planar */
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:{
      gchar u, v;

      get_semi_planar_component (format, &u, &v, TRUE);
      if (out_rgb) {
        cinfo->sample_texture_func[0] =
            g_strdup_printf (templ_SAMPLE_SEMI_PLANAR, u, v);
      } else if (out_gray) {
        cinfo->sample_texture_func[0] = g_strdup (templ_SAMPLE_YUV_LUMA);
      } else if (out_yuv) {
        if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
            cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
          /* YUV packed or Y444 */
          cinfo->sample_texture_func[0] =
              g_strdup_printf (templ_SAMPLE_SEMI_PLANAR, u, v);
        } else {
          cinfo->sample_texture_func[0] = g_strdup (templ_SAMPLE_YUV_LUMA);
          cinfo->sample_texture_func[1] =
              g_strdup_printf (templ_SAMPLE_SEMI_PLANAR_CHROMA, u, v);
        }
      } else {
        g_assert_not_reached ();
        return FALSE;
      }
      break;
    }
      /* planar */
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:{
      gchar u, v;
      guint scale;

      get_planar_component (format, &u, &v, &scale);
      if (out_rgb) {
        cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR,
            u, v, scale);
      } else if (out_gray) {
        cinfo->sample_texture_func[0] =
            g_strdup_printf (templ_SAMPLE_YUV_LUMA_SCALED, scale);
      } else if (out_yuv) {
        if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
            cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
          /* YUV packed or Y444 */
          cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR,
              u, v, scale);
        } else {
          cinfo->sample_texture_func[0] =
              g_strdup_printf (templ_SAMPLE_YUV_LUMA_SCALED, scale);
          cinfo->sample_texture_func[1] =
              g_strdup_printf (templ_SAMPLE_PLANAR_CHROMA, u, v, scale);
        }
      } else {
        g_assert_not_reached ();
        return FALSE;
      }
      break;
    }
      /* yuv packed */
    case GST_VIDEO_FORMAT_Y410:{
      gchar y, u, v;

      get_packed_yuv_components (format, &y, &u, &v);
      cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_YUV_PACKED,
          y, u, v);
      if (cinfo->ps_output[1]) {
        cinfo->sample_texture_func[1] =
            g_strdup (cinfo->sample_texture_func[0]);
      }
      break;
    }
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
      cinfo->sample_texture_func[0] = g_strdup (templ_SAMPLE_GRAY);
      if (cinfo->ps_output[1])
        cinfo->sample_texture_func[1] = g_strdup (templ_SAMPLE_GRAY_CHROMA);
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
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

static void
convert_info_gray_to_yuv (const GstVideoInfo * gray, GstVideoInfo * yuv)
{
  GstVideoInfo tmp;

  if (GST_VIDEO_INFO_IS_YUV (gray)) {
    *yuv = *gray;
    return;
  }

  if (gray->finfo->depth[0] == 8) {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_Y444, gray->width, gray->height);
  } else {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_Y444_16LE, gray->width, gray->height);
  }

  tmp.colorimetry.range = gray->colorimetry.range;
  if (tmp.colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN)
    tmp.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

  tmp.colorimetry.primaries = gray->colorimetry.primaries;
  if (tmp.colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
    tmp.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;

  tmp.colorimetry.transfer = gray->colorimetry.transfer;
  if (tmp.colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN)
    tmp.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;

  tmp.colorimetry.matrix = gray->colorimetry.matrix;
  if (tmp.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN)
    tmp.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;

  *yuv = tmp;
}

static void
convert_info_gray_to_rgb (const GstVideoInfo * gray, GstVideoInfo * rgb)
{
  GstVideoInfo tmp;

  if (GST_VIDEO_INFO_IS_RGB (gray)) {
    *rgb = *gray;
    return;
  }

  if (gray->finfo->depth[0] == 8) {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_RGBA, gray->width, gray->height);
  } else {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_RGBA64_LE, gray->width, gray->height);
  }

  tmp.colorimetry.range = gray->colorimetry.range;
  if (tmp.colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN)
    tmp.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

  tmp.colorimetry.primaries = gray->colorimetry.primaries;
  if (tmp.colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
    tmp.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;

  tmp.colorimetry.transfer = gray->colorimetry.transfer;
  if (tmp.colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN)
    tmp.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;

  *rgb = tmp;
}

static gboolean
gst_d3d11_converter_prepare_colorspace (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstD3D11Device *device = self->device;
  const GstVideoColorimetry *in_color = &in_info->colorimetry;
  const GstVideoColorimetry *out_color = &out_info->colorimetry;
  ConvertInfo *cinfo = &self->convert_info;
  PSColorSpace *color_space_buf = &self->const_data.color_space_buf;
  GstD3D11ColorMatrix matrix;
  gchar *matrix_dump;

  memset (&matrix, 0, sizeof (GstD3D11ColorMatrix));

  for (guint i = 0; i < 2; i++) {
    cinfo->to_rgb_func[i] = templ_COLOR_SPACE_IDENTITY;
    cinfo->to_yuv_func[i] = templ_COLOR_SPACE_IDENTITY;
  }

  if (GST_VIDEO_INFO_IS_RGB (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      if (in_color->range == out_color->range) {
        GST_DEBUG_OBJECT (device, "RGB -> RGB without colorspace conversion");
      } else {
        if (!gst_d3d11_color_range_adjust_matrix_unorm (in_info, out_info,
                &matrix)) {
          GST_ERROR_OBJECT (device, "Failed to get RGB range adjust matrix");
          return FALSE;
        }

        matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
        GST_DEBUG_OBJECT (device, "RGB range adjust %s -> %s\n%s",
            get_color_range_name (in_color->range),
            get_color_range_name (out_color->range), matrix_dump);
        g_free (matrix_dump);

        cinfo->to_rgb_func[0] = templ_COLOR_SPACE_CONVERT;
      }
    } else {
      GstVideoInfo yuv_info;

      convert_info_gray_to_yuv (out_info, &yuv_info);

      if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
          yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
        GST_WARNING_OBJECT (device, "Invalid matrix is detected");
        yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      }

      if (!gst_d3d11_rgb_to_yuv_matrix_unorm (in_info, &yuv_info, &matrix)) {
        GST_ERROR_OBJECT (device, "Failed to get RGB -> YUV transform matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
      GST_DEBUG_OBJECT (device, "RGB -> YUV matrix:\n%s", matrix_dump);
      g_free (matrix_dump);

      if (GST_VIDEO_INFO_IS_GRAY (out_info)) {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
      } else if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
          cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
        /* YUV packed or Y444 */
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT;
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
        cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT_CHROMA;
      }
    }
  } else if (GST_VIDEO_INFO_IS_GRAY (in_info)) {
    gboolean identity = TRUE;

    if (in_color->range != out_color->range) {
      GstVideoInfo in_tmp, out_tmp;

      if (GST_VIDEO_INFO_IS_RGB (out_info)) {
        convert_info_gray_to_rgb (in_info, &in_tmp);
        out_tmp = *out_info;
      } else {
        convert_info_gray_to_yuv (in_info, &in_tmp);
        convert_info_gray_to_yuv (out_info, &out_tmp);
      }

      identity = FALSE;
      if (!gst_d3d11_color_range_adjust_matrix_unorm (&in_tmp, &out_tmp,
              &matrix)) {
        GST_ERROR_OBJECT (device, "Failed to get GRAY range adjust matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
      GST_DEBUG_OBJECT (device, "GRAY range adjust matrix:\n%s", matrix_dump);
      g_free (matrix_dump);
    }

    if (GST_VIDEO_INFO_IS_GRAY (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (device, "GRAY to GRAY without range adjust");
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
      }
    } else if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (device, "GRAY to RGB without range adjust");
        cinfo->to_rgb_func[0] = templ_COLOR_SPACE_GRAY_TO_RGB;
      } else {
        cinfo->to_rgb_func[0] = templ_COLOR_SPACE_GRAY_TO_RGB_RANGE_ADJUST;
      }
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (device, "GRAY to YUV without range adjust");
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
        cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT_LUMA;
      }
    } else {
      g_assert_not_reached ();
      return FALSE;
    }
  } else if (GST_VIDEO_INFO_IS_YUV (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      GstVideoInfo yuv_info = *in_info;

      if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
          yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
        GST_WARNING_OBJECT (device, "Invalid matrix is detected");
        yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      }

      if (!gst_d3d11_yuv_to_rgb_matrix_unorm (&yuv_info, out_info, &matrix)) {
        GST_ERROR_OBJECT (device, "Failed to get YUV -> RGB transform matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
      GST_DEBUG_OBJECT (device, "YUV -> RGB matrix:\n%s", matrix_dump);
      g_free (matrix_dump);

      cinfo->to_rgb_func[0] = templ_COLOR_SPACE_CONVERT;
    } else if (in_color->range != out_color->range) {
      if (!gst_d3d11_color_range_adjust_matrix_unorm (in_info, out_info,
              &matrix)) {
        GST_ERROR_OBJECT (device, "Failed to get GRAY range adjust matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
      GST_DEBUG_OBJECT (device, "YUV range adjust matrix:\n%s", matrix_dump);
      g_free (matrix_dump);

      if (GST_VIDEO_INFO_IS_GRAY (out_info)) {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
      } else if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
          cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
        /* YUV packed or Y444 */
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT;
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
        cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT_CHROMA;
      }
    }
  } else {
    g_assert_not_reached ();
    return FALSE;
  }

  for (guint i = 0; i < 3; i++) {
    color_space_buf->coeffX[i] = matrix.matrix[0][i];
    color_space_buf->coeffY[i] = matrix.matrix[1][i];
    color_space_buf->coeffZ[i] = matrix.matrix[2][i];
    color_space_buf->offset[i] = matrix.offset[i];
    color_space_buf->min[i] = matrix.min[i];
    color_space_buf->max[i] = matrix.max[i];
  }

  return TRUE;
}

GstD3D11Converter *
gst_d3d11_converter_new (GstD3D11Device * device, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * config)
{
  const GstVideoInfo *unknown_info;
  GstD3D11Format in_d3d11_format;
  GstD3D11Format out_d3d11_format;
  GstD3D11Converter *self = nullptr;
  gboolean ret;

  GST_DEBUG_OBJECT (device, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (in_info),
          &in_d3d11_format)) {
    unknown_info = in_info;
    goto format_unknown;
  }

  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (out_info),
          &out_d3d11_format)) {
    unknown_info = out_info;
    goto format_unknown;
  }

  self = g_new0 (GstD3D11Converter, 1);
  self->device = (GstD3D11Device *) gst_object_ref (device);
  self->config = gst_structure_new_empty ("GstD3D11Converter-Config");
  if (config)
    gst_d3d11_converter_set_config (self, config);

  self->const_data.AlphaMul = GET_OPT_ALPHA_VALUE (self);

  if (!gst_d3d11_converter_prepare_output (self, out_info))
    goto conversion_not_supported;

  if (!gst_d3d11_converter_prepare_sample_texture (self, in_info, out_info))
    goto conversion_not_supported;

  if (!gst_d3d11_converter_prepare_colorspace (self, in_info, out_info))
    goto conversion_not_supported;

  ret = gst_d3d11_color_convert_setup_shader (self, in_info, out_info);
  if (!ret) {
    GST_ERROR_OBJECT (device, "Couldn't setup shader");
    gst_d3d11_converter_free (self);
    return nullptr;
  }

  self->in_info = *in_info;
  self->out_info = *out_info;

  return self;

  /* ERRORS */
format_unknown:
  {
    GST_ERROR_OBJECT (device, "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (unknown_info)));
    if (config)
      gst_structure_free (config);

    return nullptr;
  }
conversion_not_supported:
  {
    GST_ERROR_OBJECT (device, "Conversion %s to %s not supported",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    gst_d3d11_converter_free (self);
    return nullptr;
  }
}

void
gst_d3d11_converter_free (GstD3D11Converter * converter)
{
  g_return_if_fail (converter != nullptr);

  GST_D3D11_CLEAR_COM (converter->vertex_buffer);
  GST_D3D11_CLEAR_COM (converter->index_buffer);
  GST_D3D11_CLEAR_COM (converter->const_buffer);
  GST_D3D11_CLEAR_COM (converter->vs);
  GST_D3D11_CLEAR_COM (converter->layout);
  GST_D3D11_CLEAR_COM (converter->linear_sampler);

  for (guint i = 0; i < CONVERTER_MAX_QUADS; i++) {
    GST_D3D11_CLEAR_COM (converter->ps[i]);

    g_free (converter->convert_info.sample_texture_func[i]);
    g_free (converter->convert_info.build_output_func[i]);
  }

  gst_clear_object (&converter->device);

  if (converter->config)
    gst_structure_free (converter->config);

  g_free (converter);
}

gboolean
gst_d3d11_converter_convert (GstD3D11Converter * converter,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES],
    ID3D11BlendState * blend, gfloat blend_factor[4])
{
  gboolean ret;

  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (srv != nullptr, FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  gst_d3d11_device_lock (converter->device);
  ret = gst_d3d11_converter_convert_unlocked (converter,
      srv, rtv, blend, blend_factor);
  gst_d3d11_device_unlock (converter->device);

  return ret;
}

gboolean
gst_d3d11_converter_convert_unlocked (GstD3D11Converter * converter,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES],
    ID3D11BlendState * blend, gfloat blend_factor[4])
{
  ComPtr < ID3D11Resource > resource;
  ComPtr < ID3D11Texture2D > texture;
  D3D11_TEXTURE2D_DESC desc;
  ConvertInfo *cinfo;
  ID3D11DeviceContext *context;
  UINT offsets = 0;
  UINT vertex_stride = sizeof (VertexData);
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { nullptr, };

  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (srv != nullptr, FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  cinfo = &converter->convert_info;
  context = gst_d3d11_device_get_device_context_handle (converter->device);

  /* check texture resolution and update crop area */
  srv[0]->GetResource (&resource);
  resource.As (&texture);
  texture->GetDesc (&desc);

  if (converter->update_vertex ||
      desc.Width != (guint) converter->input_texture_width ||
      desc.Height != (guint) converter->input_texture_height) {
    GST_DEBUG ("Update vertext buffer, texture resolution: %dx%d",
        desc.Width, desc.Height);

    converter->input_texture_width = desc.Width;
    converter->input_texture_height = desc.Height;

    if (!gst_d3d11_converter_update_vertex_buffer (converter)) {
      GST_ERROR ("Cannot update vertex buffer");
      return FALSE;
    }
  }

  if (converter->update_alpha) {
    D3D11_MAPPED_SUBRESOURCE map;
    PSConstBuffer *const_buffer;
    HRESULT hr;

    hr = context->Map (converter->const_buffer,
        0, D3D11_MAP_WRITE_DISCARD, 0, &map);

    if (!gst_d3d11_result (hr, converter->device)) {
      GST_ERROR ("Couldn't map constant buffer, hr: 0x%x", (guint) hr);
      return FALSE;
    }

    const_buffer = (PSConstBuffer *) map.pData;
    const_buffer->AlphaMul = converter->const_data.AlphaMul;

    context->Unmap (converter->const_buffer, 0);
    converter->update_alpha = FALSE;
  }

  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout (converter->layout);
  context->IASetVertexBuffers (0, 1, &converter->vertex_buffer, &vertex_stride,
      &offsets);
  context->IASetIndexBuffer (converter->index_buffer, DXGI_FORMAT_R16_UINT, 0);
  context->PSSetSamplers (0, 1, &converter->linear_sampler);
  context->VSSetShader (converter->vs, nullptr, 0);
  context->PSSetConstantBuffers (0, 1, &converter->const_buffer);
  context->PSSetShaderResources (0, converter->num_input_view, srv);
  context->PSSetShader (converter->ps[0], nullptr, 0);
  context->RSSetViewports (cinfo->ps_output[0]->num_rtv, converter->viewport);
  context->OMSetRenderTargets (cinfo->ps_output[0]->num_rtv, rtv, nullptr);
  context->OMSetBlendState (blend, blend_factor, 0xffffffff);
  context->DrawIndexed (6, 0, 0);

  if (converter->ps[1]) {
    guint view_offset = cinfo->ps_output[0]->num_rtv;

    context->PSSetShader (converter->ps[1], nullptr, 0);
    context->RSSetViewports (cinfo->ps_output[1]->num_rtv,
        &converter->viewport[view_offset]);
    context->OMSetRenderTargets (cinfo->ps_output[1]->num_rtv,
        &rtv[view_offset], nullptr);
    context->DrawIndexed (6, 0, 0);
  }

  context->PSSetShaderResources (0, 4, clear_view);
  context->OMSetRenderTargets (0, nullptr, nullptr);

  return TRUE;
}

gboolean
gst_d3d11_converter_update_viewport (GstD3D11Converter * converter,
    const D3D11_VIEWPORT * viewport)
{
  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (viewport != nullptr, FALSE);

  converter->viewport[0] = *viewport;

  switch (GST_VIDEO_INFO_FORMAT (&converter->out_info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
      converter->viewport[1].TopLeftX = converter->viewport[0].TopLeftX / 2;
      converter->viewport[1].TopLeftY = converter->viewport[0].TopLeftY / 2;
      converter->viewport[1].Width = converter->viewport[0].Width / 2;
      converter->viewport[1].Height = converter->viewport[0].Height / 2;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&converter->out_info); i++)
        converter->viewport[i] = converter->viewport[1];

      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
      converter->viewport[1].TopLeftX = converter->viewport[0].TopLeftX / 2;
      converter->viewport[1].TopLeftY = converter->viewport[0].TopLeftY;
      converter->viewport[1].Width = converter->viewport[0].Width / 2;
      converter->viewport[1].Height = converter->viewport[0].Height;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&converter->out_info); i++)
        converter->viewport[i] = converter->viewport[1];
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
      for (guint i = 1; i < GST_VIDEO_INFO_N_PLANES (&converter->out_info); i++)
        converter->viewport[i] = converter->viewport[0];
      break;
    default:
      if (converter->num_output_view > 1)
        g_assert_not_reached ();
      break;
  }

  return TRUE;
}

gboolean
gst_d3d11_converter_update_src_rect (GstD3D11Converter * converter,
    const RECT * src_rect)
{
  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (src_rect != nullptr, FALSE);

  gst_d3d11_device_lock (converter->device);
  if (converter->src_rect.left != src_rect->left ||
      converter->src_rect.top != src_rect->top ||
      converter->src_rect.right != src_rect->right ||
      converter->src_rect.bottom != src_rect->bottom) {
    converter->src_rect = *src_rect;

    /* vertex buffer will be updated on next convert() call */
    converter->update_vertex = TRUE;
  }
  gst_d3d11_device_unlock (converter->device);

  return TRUE;
}

gboolean
gst_d3d11_converter_update_dest_rect (GstD3D11Converter * converter,
    const RECT * dest_rect)
{
  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (dest_rect != nullptr, FALSE);

  gst_d3d11_device_lock (converter->device);
  if (converter->dest_rect.left != dest_rect->left ||
      converter->dest_rect.top != dest_rect->top ||
      converter->dest_rect.right != dest_rect->right ||
      converter->dest_rect.bottom != dest_rect->bottom) {
    converter->dest_rect = *dest_rect;

    /* vertex buffer will be updated on next convert() call */
    converter->update_vertex = TRUE;
  }
  gst_d3d11_device_unlock (converter->device);

  return TRUE;
}

gboolean
gst_d3d11_converter_update_config (GstD3D11Converter * converter,
    GstStructure * config)
{
  gdouble alpha;

  g_return_val_if_fail (converter != nullptr, FALSE);
  g_return_val_if_fail (config != nullptr, FALSE);

  gst_d3d11_device_lock (converter->device);
  gst_d3d11_converter_set_config (converter, config);

  /* Check whether options are updated or not */
  alpha = GET_OPT_ALPHA_VALUE (converter);

  if (alpha != converter->const_data.AlphaMul) {
    GST_DEBUG ("Updating alpha %lf -> %lf",
        converter->const_data.AlphaMul, alpha);
    converter->const_data.AlphaMul = alpha;
    converter->update_alpha = TRUE;
  }
  gst_d3d11_device_unlock (converter->device);

  return TRUE;
}
