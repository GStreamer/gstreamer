/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) <2019> Jeongki Kim <jeongki.kim@jeongki.kim>
 * Copyright (C) <2022> Seungha Yang <seungha@centricular.com>
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

#include "gstd3d11-private.h"
#include "gstd3d11converter.h"
#include "gstd3d11device.h"
#include "gstd3d11utils.h"
#include "gstd3d11memory.h"
#include "gstd3d11compile.h"
#include "gstd3d11bufferpool.h"
#include <wrl.h>
#include <string.h>
#include <math.h>

/**
 * SECTION:gstd3d11converter
 * @title: GstD3D11Converter
 * @short_description: Direct3D11 video converter object
 *
 * This object performs various video conversion operation
 * via Direct3D11 API
 *
 * Since: 1.22
 */

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_converter_debug);
#define GST_CAT_DEFAULT gst_d3d11_converter_debug

DEFINE_ENUM_FLAG_OPERATORS (GstD3D11ConverterBackend);

GType
gst_d3d11_converter_backend_get_type (void)
{
  static GType type = 0;
  static const GFlagsValue values[] = {
    {GST_D3D11_CONVERTER_BACKEND_SHADER, "GST_D3D11_CONVERTER_BACKEND_SHADER",
        "shader"},
    {GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR,
        "GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR", "video-processor"},
    {0, nullptr, nullptr}
  };

  GST_D3D11_CALL_ONCE_BEGIN {
    type = g_flags_register_static ("GstD3D11ConverterBackend", values);
  } GST_D3D11_CALL_ONCE_END;

  return type;
}

GType
gst_d3d11_converter_sampler_filter_get_type (void)
{
  static GType filter_type = 0;
  static const GEnumValue filter_types[] = {
    {D3D11_FILTER_MIN_MAG_MIP_POINT,
        "D3D11_FILTER_MIN_MAG_MIP_POINT", "min-mag-mip-point"},
    {D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        "D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT", "min-linear-mag-mip-point"},
    {D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        "D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT", "min-mag-linear-mip-point"},
    {0, nullptr, nullptr},
  };

  GST_D3D11_CALL_ONCE_BEGIN {
    filter_type = g_enum_register_static ("GstD3D11ConverterSamplerFilter",
        filter_types);
  } GST_D3D11_CALL_ONCE_END;

  return filter_type;
}

GType
gst_d3d11_converter_alpha_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue alpha_mode[] = {
    {GST_D3D11_CONVERTER_ALPHA_MODE_UNSPECIFIED,
        "GST_D3D11_CONVERTER_ALPHA_MODE_UNSPECIFIED", "unspecified"},
    {GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED,
        "GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED", "premultiplied"},
    {GST_D3D11_CONVERTER_ALPHA_MODE_STRAIGHT,
        "GST_D3D11_CONVERTER_ALPHA_MODE_STRAIGHT", "straight"},
    {0, nullptr, nullptr},
  };

  GST_D3D11_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D11ConverterAlphaMode", alpha_mode);
  } GST_D3D11_CALL_ONCE_END;

  return type;
}

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

#define CONVERTER_MAX_QUADS 2
#define GAMMA_LUT_SIZE 4096

/* undefined symbols in ancient MinGW headers */
/* D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_METADATA_HDR10 */
#define FEATURE_CAPS_METADATA_HDR10 (0x800)
/* D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ROTATION */
#define FEATURE_CAPS_ROTATION (0x40)
/* D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_MIRROR */
#define PROCESSOR_FEATURE_CAPS_MIRROR (0x200)

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
  PSColorSpace to_rgb_buf;
  PSColorSpace to_yuv_buf;
  PSColorSpace XYZ_convert_buf;
  FLOAT alpha;
  DWORD in_premul_alpha;
  DWORD out_premul_alpha;
  FLOAT padding;
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

static const gchar templ_OUTPUT_FOUR_PLANES[] =
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane_0: SV_TARGET0;\n"
    "  float4 Plane_1: SV_TARGET1;\n"
    "  float4 Plane_2: SV_TARGET2;\n"
    "  float4 Plane_3: SV_TARGET3;\n"
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
  OUTPUT_FOUR_PLANES,
};

static const PSOutputType output_types[] = {
  {templ_OUTPUT_SINGLE_PLANE, 1},
  {templ_OUTPUT_TWO_PLANES, 2},
  {templ_OUTPUT_THREE_PLANES, 3},
  {templ_OUTPUT_FOUR_PLANES, 4},
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
    "  sample.%c = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[1].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[2].Sample(samplerState, uv).x;\n"
    "  return float4 (saturate(sample * %d), 1.0);\n"
    "}";

static const gchar templ_SAMPLE_PLANAR_4[] =
    "float4 sample_texture (float2 uv)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample.%c = shaderTexture[0].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[1].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[2].Sample(samplerState, uv).x;\n"
    "  sample.%c = shaderTexture[3].Sample(samplerState, uv).x;\n"
    "  return saturate(sample * %d);\n"
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

static const gchar templ_OUTPUT_PLANAR[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_PLANAR_SCALED[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  float3 scaled = sample.xyz / %d;\n"
    "  output.Plane_0 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_PLANAR_4[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  output.Plane_0 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_3 = float4 (sample.%c, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

static const gchar templ_OUTPUT_PLANAR_4_SCALED[] =
    "PS_OUTPUT build_output (float4 sample)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  float4 scaled = sample / %d;\n"
    "  output.Plane_0 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_1 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_2 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  output.Plane_3 = float4 (scaled.%c, 0.0, 0.0, 1.0);\n"
    "  return output;\n"
    "}";

/* gamma and XYZ convert */
static const gchar templ_GAMMA_DECODE_IDENTITY[] =
    "float3 gamma_decode (float3 sample)\n"
    "{\n"
    "  return sample;\n"
    "}";

static const gchar templ_GAMMA_DECODE[] =
    "float3 gamma_decode (float3 sample)\n"
    "{\n"
    "  float3 dec;\n"
    "  dec.x = gammaDecLUT.Sample (samplerState, sample.x);\n"
    "  dec.y = gammaDecLUT.Sample (samplerState, sample.y);\n"
    "  dec.z = gammaDecLUT.Sample (samplerState, sample.z);\n"
    "  return dec;\n"
    "}";

static const gchar templ_GAMMA_ENCODE_IDENTITY[] =
    "float3 gamma_encode (float3 sample)\n"
    "{\n"
    "  return sample;\n"
    "}";

static const gchar templ_GAMMA_ENCODE[] =
    "float3 gamma_encode (float3 sample)\n"
    "{\n"
    "  float3 enc;\n"
    "  enc.x = gammaEncLUT.Sample (samplerState, sample.x);\n"
    "  enc.y = gammaEncLUT.Sample (samplerState, sample.y);\n"
    "  enc.z = gammaEncLUT.Sample (samplerState, sample.z);\n"
    "  return enc;\n"
    "}";

static const gchar templ_XYZ_CONVERT_IDENTITY[] =
    "float3 XYZ_convert (float3 sample)\n"
    "{\n"
    "  return sample;\n"
    "}";

static const gchar templ_XYZ_CONVERT[] =
    "float3 XYZ_convert (float3 sample)\n"
    "{\n"
    "  float3 out_space;\n"
    "  out_space.x = dot (primariesCoeff.CoeffX, sample);\n"
    "  out_space.y = dot (primariesCoeff.CoeffY, sample);\n"
    "  out_space.z = dot (primariesCoeff.CoeffZ, sample);\n"
    "  return saturate (out_space);\n"
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
    "  PSColorSpace toRGBCoeff;\n"
    "  PSColorSpace toYUVCoeff;\n"
    "  PSColorSpace primariesCoeff;\n"
    "  float AlphaMul;\n"
    "  dword InPremulAlpha;\n"
    "  dword OutPremulAlpha;\n"
    "};\n"
    "Texture2D shaderTexture[4] : register(t0);\n"
    "Texture1D<float> gammaDecLUT: register(t4);\n"
    "Texture1D<float> gammaEncLUT: register(t5);\n"
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
    /* gamma_decode() function */
    "%s\n"
    /* gamma_encode() function */
    "%s\n"
    /* XYZ_convert() function */
    "%s\n"
    "float4 alpha_premul (float4 sample)\n"
    "{\n"
    "  float4 premul_tex;\n"
    "  premul_tex.r = sample.r * sample.a;\n"
    "  premul_tex.g = sample.g * sample.a;\n"
    "  premul_tex.b = sample.b * sample.a;\n"
    "  premul_tex.a = sample.a;\n"
    "  return premul_tex;\n"
    "}\n"
    "float4 alpha_unpremul (float4 sample)\n"
    "{\n"
    "  float4 unpremul_tex;\n"
    "  if (sample.a == 0 || sample.a == 1)\n"
    "    return sample;\n"
    "  unpremul_tex.r = saturate (sample.r / sample.a);\n"
    "  unpremul_tex.g = saturate (sample.g / sample.a);\n"
    "  unpremul_tex.b = saturate (sample.b / sample.a);\n"
    "  unpremul_tex.a = sample.a;\n"
    "  return unpremul_tex;\n"
    "}\n"
    "PS_OUTPUT main(PS_INPUT input)\n"
    "{\n"
    "  float4 sample;\n"
    "  sample = sample_texture (input.Texture);\n"
    "  if (InPremulAlpha)\n"
    "    sample = alpha_unpremul (sample);\n"
    "  sample.a = saturate (sample.a * AlphaMul);\n"
    "  sample.xyz = to_rgb (sample.xyz, toRGBCoeff);\n"
    "  sample.xyz = gamma_decode (sample.xyz);\n"
    "  sample.xyz = XYZ_convert (sample.xyz);\n"
    "  sample.xyz = gamma_encode (sample.xyz);\n"
    "  sample.xyz = to_yuv (sample.xyz, toYUVCoeff);\n"
    "  if (OutPremulAlpha)\n"
    "    sample = alpha_premul (sample);\n"
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
  const gchar *gamma_decode_func;
  const gchar *gamma_encode_func;
  const gchar *XYZ_convert_func;
} ConvertInfo;

enum
{
  PROP_0,
  PROP_SRC_X,
  PROP_SRC_Y,
  PROP_SRC_WIDTH,
  PROP_SRC_HEIGHT,
  PROP_DEST_X,
  PROP_DEST_Y,
  PROP_DEST_WIDTH,
  PROP_DEST_HEIGHT,
  PROP_ALPHA,
  PROP_BLEND_STATE,
  PROP_BLEND_FACTOR_RED,
  PROP_BLEND_FACTOR_GREEN,
  PROP_BLEND_FACTOR_BLUE,
  PROP_BLEND_FACTOR_ALPHA,
  PROP_BLEND_SAMPLE_MASK,
  PROP_FILL_BORDER,
  PROP_BORDER_COLOR,
  PROP_SRC_MASTERING_DISPLAY_INFO,
  PROP_SRC_CONTENT_LIGHT_LEVEL,
  PROP_DEST_MASTERING_DISPLAY_INFO,
  PROP_DEST_CONTENT_LIGHT_LEVEL,
  PROP_VIDEO_DIRECTION,
  PROP_SRC_ALPHA_MODE,
  PROP_DEST_ALPHA_MODE,
};

struct _GstD3D11ConverterPrivate
{
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GstD3D11Format in_d3d11_format;
  GstD3D11Format out_d3d11_format;

  guint num_input_view;
  guint num_output_view;

  GstD3D11ConverterBackend supported_backend;

  ID3D11Buffer *vertex_buffer;
  ID3D11Buffer *index_buffer;
  ID3D11Buffer *const_buffer;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *linear_sampler;
  ID3D11PixelShader *ps[CONVERTER_MAX_QUADS];
  D3D11_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];

  ID3D11Texture1D *gamma_dec_lut;
  ID3D11Texture1D *gamma_enc_lut;
  ID3D11ShaderResourceView *gamma_dec_srv;
  ID3D11ShaderResourceView *gamma_enc_srv;

  D3D11_BLEND_DESC blend_desc;
  ID3D11BlendState *blend;

  gboolean fast_path;
  gboolean do_primaries;

  gint input_texture_width;
  gint input_texture_height;
  gboolean update_src_rect;
  gboolean update_dest_rect;
  gboolean update_alpha;

  ConvertInfo convert_info;
  PSConstBuffer const_data;

  gboolean clear_background;
  FLOAT clear_color[4][4];
  GstD3D11ColorMatrix clear_color_matrix;

  GstVideoConverter *unpack_convert;

  /* video processor */
  D3D11_VIDEO_COLOR background_color;
  ID3D11VideoDevice *video_device;
  ID3D11VideoContext2 *video_context2;
  ID3D11VideoContext1 *video_context;
  ID3D11VideoProcessorEnumerator1 *enumerator;
  ID3D11VideoProcessor *processor;
  D3D11_VIDEO_PROCESSOR_CAPS processor_caps;
  RECT src_rect;
  RECT dest_rect;
  RECT dest_full_rect;
  gboolean processor_in_use;
  gboolean processor_direction_not_supported;
  gboolean enable_mirror;
  gboolean flip_h;
  gboolean flip_v;
  gboolean enable_rotation;
  D3D11_VIDEO_PROCESSOR_ROTATION rotation;

  /* HDR10 */
  gboolean have_in_hdr10;
  gboolean have_out_hdr10;
  gboolean in_hdr10_updated;
  gboolean out_hdr10_updated;
  DXGI_HDR_METADATA_HDR10 in_hdr10_meta;
  DXGI_HDR_METADATA_HDR10 out_hdr10_meta;
  gchar *in_mdcv_str;
  gchar *out_mdcv_str;
  gchar *in_cll_str;
  gchar *out_cll_str;

  /* Fallback buffer and info, for shader */
  GstVideoInfo fallback_info;
  GstBuffer *fallback_inbuf;

  /* Fallback buffer used for processor */
  GstVideoInfo piv_info;
  GstBuffer *piv_inbuf;

  GstVideoOrientationMethod video_direction;

  SRWLOCK prop_lock;

  /* properties */
  gint src_x;
  gint src_y;
  gint src_width;
  gint src_height;
  gint dest_x;
  gint dest_y;
  gint dest_width;
  gint dest_height;
  gdouble alpha;
  gfloat blend_factor[4];
  guint blend_sample_mask;
  gboolean fill_border;
  guint64 border_color;
  GstD3D11ConverterAlphaMode src_alpha_mode;
  GstD3D11ConverterAlphaMode dst_alpha_mode;
};

static void gst_d3d11_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_converter_dispose (GObject * object);
static void gst_d3d11_converter_finalize (GObject * object);
static void
gst_d3d11_converter_calculate_border_color (GstD3D11Converter * self);

#define gst_d3d11_converter_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11Converter, gst_d3d11_converter,
    GST_TYPE_OBJECT);

static void
gst_d3d11_converter_class_init (GstD3D11ConverterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamFlags param_flags =
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = gst_d3d11_converter_set_property;
  object_class->get_property = gst_d3d11_converter_get_property;
  object_class->dispose = gst_d3d11_converter_dispose;
  object_class->finalize = gst_d3d11_converter_finalize;

  g_object_class_install_property (object_class, PROP_SRC_X,
      g_param_spec_int ("src-x", "Src-X",
          "Source x poisition to start conversion", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_SRC_Y,
      g_param_spec_int ("src-y", "Src-Y",
          "Source y poisition to start conversion", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_SRC_WIDTH,
      g_param_spec_int ("src-width", "Src-Width",
          "Source width to convert", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_SRC_HEIGHT,
      g_param_spec_int ("src-height", "Src-Height",
          "Source height to convert", 0, G_MAXINT, 0, param_flags));
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
  g_object_class_install_property (object_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha",
          "The alpha color value to use", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_STATE,
      g_param_spec_pointer ("blend-state", "Blend State",
          "ID3D11BlendState object to use", param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_FACTOR_RED,
      g_param_spec_float ("blend-factor-red", "Blend Factor Red",
          "Blend factor for red component", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_FACTOR_GREEN,
      g_param_spec_float ("blend-factor-green", "Blend Factor Green",
          "Blend factor for green component", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_FACTOR_BLUE,
      g_param_spec_float ("blend-factor-blue", "Blend Factor Blue",
          "Blend factor for blue component", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_FACTOR_ALPHA,
      g_param_spec_float ("blend-factor-alpha", "Blend Factor Alpha",
          "Blend factor for alpha component", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_BLEND_SAMPLE_MASK,
      g_param_spec_uint ("blend-sample-mask", "Blend Sample Mask",
          "Blend sample mask", 0, 0xffffffff, 0xffffffff, param_flags));
  g_object_class_install_property (object_class, PROP_FILL_BORDER,
      g_param_spec_boolean ("fill-border", "Fill border",
          "Fill border with \"border-color\" if destination rectangle does not "
          "fill the complete destination image", FALSE, param_flags));
  g_object_class_install_property (object_class, PROP_BORDER_COLOR,
      g_param_spec_uint64 ("border-color", "Border Color",
          "ARGB representation of the border color to use",
          0, G_MAXUINT64, 0xffff000000000000, param_flags));
  g_object_class_install_property (object_class,
      PROP_SRC_MASTERING_DISPLAY_INFO,
      g_param_spec_string ("src-mastering-display-info",
          "Src Mastering Display Info",
          "String representation of GstVideoMasteringDisplayInfo for source",
          nullptr, param_flags));
  g_object_class_install_property (object_class, PROP_SRC_CONTENT_LIGHT_LEVEL,
      g_param_spec_string ("src-content-light-level",
          "Src Content Light Level",
          "String representation of GstVideoContentLightLevel for src",
          nullptr, param_flags));
  g_object_class_install_property (object_class,
      PROP_DEST_MASTERING_DISPLAY_INFO,
      g_param_spec_string ("dest-mastering-display-info",
          "Dest Mastering Display Info",
          "String representation of GstVideoMasteringDisplayInfo for dest",
          nullptr, param_flags));
  g_object_class_install_property (object_class, PROP_DEST_CONTENT_LIGHT_LEVEL,
      g_param_spec_string ("dest-content-light-level",
          "Src Content Light Level",
          "String representation of GstVideoContentLightLevel for dest",
          nullptr, param_flags));
  g_object_class_install_property (object_class, PROP_VIDEO_DIRECTION,
      g_param_spec_enum ("video-direction", "Video Direction",
          "Video direction", GST_TYPE_VIDEO_ORIENTATION_METHOD,
          GST_VIDEO_ORIENTATION_IDENTITY, param_flags));
  g_object_class_install_property (object_class, PROP_SRC_ALPHA_MODE,
      g_param_spec_enum ("src-alpha-mode", "Src Alpha Mode",
          "Src alpha mode to use", GST_TYPE_D3D11_CONVERTER_ALPHA_MODE,
          GST_D3D11_CONVERTER_ALPHA_MODE_UNSPECIFIED, param_flags));
  g_object_class_install_property (object_class, PROP_DEST_ALPHA_MODE,
      g_param_spec_enum ("dest-alpha-mode", "Dest Alpha Mode",
          "Dest alpha mode to use", GST_TYPE_D3D11_CONVERTER_ALPHA_MODE,
          GST_D3D11_CONVERTER_ALPHA_MODE_UNSPECIFIED, param_flags));

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_converter_debug,
      "d3d11converter", 0, "d3d11converter");
}

static void
gst_d3d11_converter_init (GstD3D11Converter * self)
{
  self->priv = (GstD3D11ConverterPrivate *)
      gst_d3d11_converter_get_instance_private (self);
  self->priv->src_alpha_mode = self->priv->dst_alpha_mode =
      GST_D3D11_CONVERTER_ALPHA_MODE_UNSPECIFIED;
}

static void
gst_d3d11_converter_dispose (GObject * object)
{
  GstD3D11Converter *self = GST_D3D11_CONVERTER (object);
  GstD3D11ConverterPrivate *priv = self->priv;

  GST_D3D11_CLEAR_COM (priv->vertex_buffer);
  GST_D3D11_CLEAR_COM (priv->index_buffer);
  GST_D3D11_CLEAR_COM (priv->const_buffer);
  GST_D3D11_CLEAR_COM (priv->vs);
  GST_D3D11_CLEAR_COM (priv->layout);
  GST_D3D11_CLEAR_COM (priv->linear_sampler);
  GST_D3D11_CLEAR_COM (priv->gamma_dec_lut);
  GST_D3D11_CLEAR_COM (priv->gamma_dec_srv);
  GST_D3D11_CLEAR_COM (priv->gamma_enc_lut);
  GST_D3D11_CLEAR_COM (priv->gamma_enc_srv);
  GST_D3D11_CLEAR_COM (priv->video_device);
  GST_D3D11_CLEAR_COM (priv->video_context2);
  GST_D3D11_CLEAR_COM (priv->video_context);
  GST_D3D11_CLEAR_COM (priv->enumerator);
  GST_D3D11_CLEAR_COM (priv->processor);
  GST_D3D11_CLEAR_COM (priv->blend);

  for (guint i = 0; i < CONVERTER_MAX_QUADS; i++)
    GST_D3D11_CLEAR_COM (priv->ps[i]);

  gst_clear_buffer (&priv->fallback_inbuf);
  gst_clear_buffer (&priv->piv_inbuf);
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_converter_finalize (GObject * object)
{
  GstD3D11Converter *self = GST_D3D11_CONVERTER (object);
  GstD3D11ConverterPrivate *priv = self->priv;

  for (guint i = 0; i < CONVERTER_MAX_QUADS; i++) {
    g_free (priv->convert_info.sample_texture_func[i]);
    g_free (priv->convert_info.build_output_func[i]);
  }

  g_free (priv->in_mdcv_str);
  g_free (priv->out_mdcv_str);
  g_free (priv->in_cll_str);
  g_free (priv->out_cll_str);

  if (priv->unpack_convert)
    gst_video_converter_free (priv->unpack_convert);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_src_rect (GstD3D11Converter * self, gint * old_val,
    const GValue * new_val)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  gint tmp;

  tmp = g_value_get_int (new_val);
  if (tmp != *old_val) {
    priv->update_src_rect = TRUE;
    *old_val = tmp;
  }
}

static void
update_dest_rect (GstD3D11Converter * self, gint * old_val,
    const GValue * new_val)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  gint tmp;

  tmp = g_value_get_int (new_val);
  if (tmp != *old_val) {
    priv->update_dest_rect = TRUE;
    *old_val = tmp;
  }
}

static void
update_alpha (GstD3D11Converter * self, gdouble * old_val,
    const GValue * new_val)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  gdouble tmp;

  tmp = g_value_get_double (new_val);
  if (tmp != *old_val) {
    priv->update_alpha = TRUE;
    *old_val = tmp;
  }
}

static void
gst_d3d11_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Converter *self = GST_D3D11_CONVERTER (object);
  GstD3D11ConverterPrivate *priv = self->priv;

  GstD3D11SRWLockGuard (&priv->prop_lock);
  switch (prop_id) {
    case PROP_SRC_X:
      update_src_rect (self, &priv->src_x, value);
      break;
    case PROP_SRC_Y:
      update_src_rect (self, &priv->src_y, value);
      break;
    case PROP_SRC_WIDTH:
      update_src_rect (self, &priv->src_width, value);
      break;
    case PROP_SRC_HEIGHT:
      update_src_rect (self, &priv->src_height, value);
      break;
    case PROP_DEST_X:
      update_dest_rect (self, &priv->dest_x, value);
      break;
    case PROP_DEST_Y:
      update_dest_rect (self, &priv->dest_y, value);
      break;
    case PROP_DEST_WIDTH:
      update_dest_rect (self, &priv->dest_width, value);
      break;
    case PROP_DEST_HEIGHT:
      update_dest_rect (self, &priv->dest_height, value);
      break;
    case PROP_ALPHA:
      update_alpha (self, &priv->alpha, value);
      priv->const_data.alpha = priv->alpha;
      break;
    case PROP_BLEND_STATE:{
      ID3D11BlendState *blend =
          (ID3D11BlendState *) g_value_get_pointer (value);
      GST_D3D11_CLEAR_COM (priv->blend);
      priv->blend = blend;
      if (priv->blend) {
        priv->blend->AddRef ();
        priv->blend->GetDesc (&priv->blend_desc);
      }
      break;
    }
    case PROP_BLEND_FACTOR_RED:
      priv->blend_factor[0] = g_value_get_float (value);
      break;
    case PROP_BLEND_FACTOR_GREEN:
      priv->blend_factor[1] = g_value_get_float (value);
      break;
    case PROP_BLEND_FACTOR_BLUE:
      priv->blend_factor[2] = g_value_get_float (value);
      break;
    case PROP_BLEND_FACTOR_ALPHA:
      priv->blend_factor[3] = g_value_get_float (value);
      break;
    case PROP_BLEND_SAMPLE_MASK:
      priv->blend_sample_mask = g_value_get_uint (value);
      break;
    case PROP_FILL_BORDER:{
      gboolean fill_border = g_value_get_boolean (value);

      if (fill_border != priv->fill_border) {
        priv->update_dest_rect = TRUE;
        priv->fill_border = fill_border;
      }
      break;
    }
    case PROP_BORDER_COLOR:{
      guint64 border_color = g_value_get_uint64 (value);

      if (border_color != priv->border_color) {
        priv->border_color = border_color;
        gst_d3d11_converter_calculate_border_color (self);
      }
      break;
    }
    case PROP_SRC_MASTERING_DISPLAY_INFO:
      g_clear_pointer (&priv->in_mdcv_str, g_free);
      priv->in_mdcv_str = g_value_dup_string (value);
      priv->in_hdr10_updated = TRUE;
      break;
    case PROP_SRC_CONTENT_LIGHT_LEVEL:
      g_clear_pointer (&priv->in_cll_str, g_free);
      priv->in_cll_str = g_value_dup_string (value);
      priv->in_hdr10_updated = TRUE;
      break;
    case PROP_DEST_MASTERING_DISPLAY_INFO:
      g_clear_pointer (&priv->out_mdcv_str, g_free);
      priv->out_mdcv_str = g_value_dup_string (value);
      priv->out_hdr10_updated = TRUE;
      break;
    case PROP_DEST_CONTENT_LIGHT_LEVEL:
      g_clear_pointer (&priv->out_cll_str, g_free);
      priv->out_cll_str = g_value_dup_string (value);
      priv->out_hdr10_updated = TRUE;
      break;
    case PROP_VIDEO_DIRECTION:{
      GstVideoOrientationMethod video_direction =
          (GstVideoOrientationMethod) g_value_get_enum (value);
      if (video_direction != priv->video_direction) {
        priv->video_direction = video_direction;
        priv->update_src_rect = TRUE;
      }
      break;
    }
    case PROP_SRC_ALPHA_MODE:
    {
      DWORD prev_premul = priv->const_data.in_premul_alpha;
      priv->src_alpha_mode = (GstD3D11ConverterAlphaMode)
          g_value_get_enum (value);
      if (priv->src_alpha_mode == GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED) {
        priv->const_data.in_premul_alpha = TRUE;
      } else {
        priv->const_data.in_premul_alpha = FALSE;
      }

      if (prev_premul != priv->const_data.in_premul_alpha) {
        priv->update_alpha = TRUE;
      }
      break;
    }
    case PROP_DEST_ALPHA_MODE:
    {
      DWORD prev_premul = priv->const_data.out_premul_alpha;
      priv->dst_alpha_mode = (GstD3D11ConverterAlphaMode)
          g_value_get_enum (value);
      if (priv->dst_alpha_mode == GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED) {
        priv->const_data.out_premul_alpha = TRUE;
      } else {
        priv->const_data.out_premul_alpha = FALSE;
      }

      if (prev_premul != priv->const_data.out_premul_alpha) {
        priv->update_alpha = TRUE;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Converter *self = GST_D3D11_CONVERTER (object);
  GstD3D11ConverterPrivate *priv = self->priv;

  GstD3D11SRWLockGuard (&priv->prop_lock);
  switch (prop_id) {
    case PROP_SRC_X:
      g_value_set_int (value, priv->src_x);
      break;
    case PROP_SRC_Y:
      g_value_set_int (value, priv->src_y);
      break;
    case PROP_SRC_WIDTH:
      g_value_set_int (value, priv->src_width);
      break;
    case PROP_SRC_HEIGHT:
      g_value_set_int (value, priv->src_height);
      break;
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
    case PROP_ALPHA:
      g_value_set_double (value, priv->alpha);
      break;
    case PROP_BLEND_STATE:
      g_value_set_pointer (value, priv->blend);
      break;
    case PROP_BLEND_FACTOR_RED:
      g_value_set_float (value, priv->blend_factor[0]);
      break;
    case PROP_BLEND_FACTOR_GREEN:
      g_value_set_float (value, priv->blend_factor[1]);
      break;
    case PROP_BLEND_FACTOR_BLUE:
      g_value_set_float (value, priv->blend_factor[2]);
      break;
    case PROP_BLEND_FACTOR_ALPHA:
      g_value_set_float (value, priv->blend_factor[3]);
      break;
    case PROP_BLEND_SAMPLE_MASK:
      g_value_set_uint (value, priv->blend_sample_mask);
      break;
    case PROP_FILL_BORDER:
      g_value_set_boolean (value, priv->fill_border);
      break;
    case PROP_BORDER_COLOR:
      g_value_set_uint64 (value, priv->border_color);
      break;
    case PROP_SRC_MASTERING_DISPLAY_INFO:
      g_value_set_string (value, priv->in_mdcv_str);
      break;
    case PROP_SRC_CONTENT_LIGHT_LEVEL:
      g_value_set_string (value, priv->in_cll_str);
      break;
    case PROP_DEST_MASTERING_DISPLAY_INFO:
      g_value_set_string (value, priv->out_mdcv_str);
      break;
    case PROP_DEST_CONTENT_LIGHT_LEVEL:
      g_value_set_string (value, priv->out_cll_str);
      break;
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, priv->video_direction);
      break;
    case PROP_SRC_ALPHA_MODE:
      g_value_set_enum (value, priv->src_alpha_mode);
      break;
    case PROP_DEST_ALPHA_MODE:
      g_value_set_enum (value, priv->dst_alpha_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

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
get_planar_component (GstVideoFormat format, gchar * x, gchar * y, gchar * z,
    gchar * w, guint * scale)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBRA_10LE:
      *scale = (1 << 6);
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
      *scale = (1 << 4);
      break;
    default:
      *scale = 1;
      break;
  }

  switch (format) {
    case GST_VIDEO_FORMAT_RGBP:
      *x = 'x';
      *y = 'y';
      *z = 'z';
      break;
    case GST_VIDEO_FORMAT_BGRP:
      *x = 'z';
      *y = 'y';
      *z = 'x';
      break;
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
      *x = 'y';
      *y = 'z';
      *z = 'x';
      break;
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
      *x = 'y';
      *y = 'z';
      *z = 'x';
      *w = 'w';
      break;
    case GST_VIDEO_FORMAT_YV12:
      *x = 'x';
      *y = 'z';
      *z = 'y';
      break;
    default:
      *x = 'x';
      *y = 'y';
      *z = 'z';
      break;
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
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    D3D11_FILTER sampler_filter)
{
  GstD3D11ConverterPrivate *priv = self->priv;
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
  ConvertInfo *cinfo = &priv->convert_info;
  ComPtr < ID3D11PixelShader > ps[CONVERTER_MAX_QUADS];
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11SamplerState > linear_sampler;
  ComPtr < ID3D11Buffer > const_buffer;
  ComPtr < ID3D11Buffer > vertex_buffer;
  ComPtr < ID3D11Buffer > index_buffer;
  gint i;

  memset (&sampler_desc, 0, sizeof (sampler_desc));
  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  /* bilinear filtering */
  sampler_desc.Filter = sampler_filter;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = device_handle->CreateSamplerState (&sampler_desc, &linear_sampler);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create samplerState state, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  for (i = 0; i < CONVERTER_MAX_QUADS; i++) {
    gchar *shader_code = nullptr;

    if (cinfo->sample_texture_func[i]) {
      g_assert (cinfo->ps_output[i] != nullptr);

      shader_code = g_strdup_printf (templ_pixel_shader,
          cinfo->ps_output[i]->output_template, cinfo->sample_texture_func[i],
          cinfo->to_rgb_func[i], cinfo->to_yuv_func[i],
          cinfo->build_output_func[i], cinfo->gamma_decode_func,
          cinfo->gamma_encode_func, cinfo->XYZ_convert_func);

      hr = gst_d3d11_create_pixel_shader_simple (device,
          shader_code, "main", &ps[i]);
      g_free (shader_code);
      if (!gst_d3d11_result (hr, device)) {
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

  hr = gst_d3d11_create_vertex_shader_simple (device, templ_vertex_shader,
      "main", input_desc, G_N_ELEMENTS (input_desc), &vs, &layout);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't vertex pixel shader");
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
    GST_ERROR_OBJECT (self,
        "Couldn't create constant buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  /* setup vertext buffer and index buffer */
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
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

  GstD3D11DeviceLockGuard lk (device);
  hr = context_handle->Map (const_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't map constant buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  memcpy (map.pData, &priv->const_data, sizeof (PSConstBuffer));
  context_handle->Unmap (const_buffer.Get (), 0);

  hr = context_handle->Map (vertex_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  vertex_data = (VertexData *) map.pData;

  hr = context_handle->Map (index_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't map index buffer, hr: 0x%x", (guint) hr);
    context_handle->Unmap (vertex_buffer.Get (), 0);
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

  /* holds vertex buffer for crop rect update */
  priv->vertex_buffer = vertex_buffer.Detach ();
  priv->index_buffer = index_buffer.Detach ();
  priv->const_buffer = const_buffer.Detach ();
  priv->vs = vs.Detach ();
  priv->layout = layout.Detach ();
  priv->linear_sampler = linear_sampler.Detach ();
  priv->ps[0] = ps[0].Detach ();
  if (ps[1])
    priv->ps[1] = ps[1].Detach ();

  priv->input_texture_width = GST_VIDEO_INFO_WIDTH (in_info);
  priv->input_texture_height = GST_VIDEO_INFO_HEIGHT (in_info);

  priv->num_input_view = GST_VIDEO_INFO_N_PLANES (in_info);
  priv->num_output_view = GST_VIDEO_INFO_N_PLANES (out_info);

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++) {
    priv->viewport[i].TopLeftX = 0;
    priv->viewport[i].TopLeftY = 0;
    priv->viewport[i].Width = GST_VIDEO_INFO_COMP_WIDTH (out_info, i);
    priv->viewport[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, i);
    priv->viewport[i].MinDepth = 0.0f;
    priv->viewport[i].MaxDepth = 1.0f;
  }

  return TRUE;
}

static void
gst_d3d11_converter_apply_orientation (GstD3D11Converter * self,
    VertexData * vertex_data, gfloat l, gfloat r, gfloat t, gfloat b)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  gfloat u[4], v[4];

  /*
   * 1 (l, t) -- 2 (r, t)
   *     |            |
   * 0 (l, b) -- 3 (r, b)
   */
  u[0] = l;
  u[1] = l;
  u[2] = r;
  u[3] = r;

  v[0] = b;
  v[1] = t;
  v[2] = t;
  v[3] = b;

  switch (priv->video_direction) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
    case GST_VIDEO_ORIENTATION_AUTO:
    case GST_VIDEO_ORIENTATION_CUSTOM:
    default:
      break;
    case GST_VIDEO_ORIENTATION_90R:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (l, b) -- 2 (l, t)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (r, b) -- 3 (r, t)
       */
      u[0] = r;
      u[1] = l;
      u[2] = l;
      u[3] = r;

      v[0] = b;
      v[1] = b;
      v[2] = t;
      v[3] = t;
      break;
    case GST_VIDEO_ORIENTATION_180:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (r, b) -- 2 (l, b)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (r, t) -- 3 (l, t)
       */
      u[0] = r;
      u[1] = r;
      u[2] = l;
      u[3] = l;

      v[0] = t;
      v[1] = b;
      v[2] = b;
      v[3] = t;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (r, t) -- 2 (r, b)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (l, t) -- 3 (l, b)
       */
      u[0] = l;
      u[1] = r;
      u[2] = r;
      u[3] = l;

      v[0] = t;
      v[1] = t;
      v[2] = b;
      v[3] = b;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (r, t) -- 2 (l, t)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (r, b) -- 3 (l, b)
       */
      u[0] = r;
      u[1] = r;
      u[2] = l;
      u[3] = l;

      v[0] = b;
      v[1] = t;
      v[2] = t;
      v[3] = b;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (l, b) -- 2 (r, b)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (l, t) -- 3 (r, t)
       */
      u[0] = l;
      u[1] = l;
      u[2] = r;
      u[3] = r;

      v[0] = t;
      v[1] = b;
      v[2] = b;
      v[3] = t;
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (l, t) -- 2 (l, b)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (r, t) -- 3 (r, b)
       */
      u[0] = r;
      u[1] = l;
      u[2] = l;
      u[3] = r;

      v[0] = t;
      v[1] = t;
      v[2] = b;
      v[3] = b;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      /*
       * 1 (l, t) -- 2 (r, t)    1 (r, b) -- 2 (r, t)
       *     |           |    ->      |          |
       * 0 (l, b) -- 3 (r, b)    0 (l, b) -- 3 (l, t)
       */
      u[0] = l;
      u[1] = r;
      u[2] = r;
      u[3] = l;

      v[0] = b;
      v[1] = b;
      v[2] = t;
      v[3] = t;
      break;
  }

  for (guint i = 0; i < 4; i++) {
    vertex_data[i].texture.u = u[i];
    vertex_data[i].texture.v = v[i];
  }
}

static gboolean
gst_d3d11_converter_update_src_rect (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  ID3D11DeviceContext *context_handle;
  HRESULT hr;
  FLOAT u0, u1, v0, v1, off_u, off_v;
  gint texture_width = priv->input_texture_width;
  gint texture_height = priv->input_texture_height;

  if (!priv->update_src_rect)
    return TRUE;

  priv->update_src_rect = FALSE;

  priv->src_rect.left = priv->src_x;
  priv->src_rect.top = priv->src_y;
  priv->src_rect.right = priv->src_x + priv->src_width;
  priv->src_rect.bottom = priv->src_y + priv->src_height;

  if ((priv->supported_backend & GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR)) {
    priv->processor_direction_not_supported = FALSE;
    priv->enable_mirror = FALSE;
    priv->flip_h = FALSE;
    priv->flip_v = FALSE;
    priv->enable_rotation = FALSE;
    priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY;

    /* filtering order is rotation -> mirror */
    switch (priv->video_direction) {
      case GST_VIDEO_ORIENTATION_IDENTITY:
      case GST_VIDEO_ORIENTATION_AUTO:
      case GST_VIDEO_ORIENTATION_CUSTOM:
      default:
        break;
      case GST_VIDEO_ORIENTATION_90R:
        priv->enable_rotation = TRUE;
        priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_90;
        break;
      case GST_VIDEO_ORIENTATION_180:
        priv->enable_rotation = TRUE;
        priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_180;
        break;
      case GST_VIDEO_ORIENTATION_90L:
        priv->enable_rotation = TRUE;
        priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_270;
        break;
      case GST_VIDEO_ORIENTATION_HORIZ:
        priv->enable_mirror = TRUE;
        priv->flip_h = TRUE;
        break;
      case GST_VIDEO_ORIENTATION_VERT:
        priv->enable_mirror = TRUE;
        priv->flip_v = TRUE;
        break;
      case GST_VIDEO_ORIENTATION_UL_LR:
        priv->enable_rotation = TRUE;
        priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_270;
        priv->enable_mirror = TRUE;
        priv->flip_v = TRUE;
        break;
      case GST_VIDEO_ORIENTATION_UR_LL:
        priv->enable_rotation = TRUE;
        priv->rotation = D3D11_VIDEO_PROCESSOR_ROTATION_90;
        priv->enable_mirror = TRUE;
        priv->flip_v = TRUE;
        break;
    }

    if (priv->enable_rotation &&
        (priv->processor_caps.FeatureCaps & FEATURE_CAPS_ROTATION) == 0) {
      GST_WARNING_OBJECT (self, "Device does not support rotation");
      priv->processor_direction_not_supported = TRUE;
    }

    if (priv->enable_mirror &&
        (priv->processor_caps.FeatureCaps & PROCESSOR_FEATURE_CAPS_MIRROR) ==
        0) {
      GST_WARNING_OBJECT (self, "Device does not support mirror");
      priv->processor_direction_not_supported = TRUE;
    }
  }

  if ((priv->supported_backend & GST_D3D11_CONVERTER_BACKEND_SHADER) == 0)
    return TRUE;

  context_handle = gst_d3d11_device_get_device_context_handle (self->device);

  hr = context_handle->Map (priv->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD,
      0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Updating vertex buffer");

  vertex_data = (VertexData *) map.pData;
  /*
   *  (u0, v0) -- (u1, v0)
   *     |            |
   *  (u0, v1) -- (u1, v1)
   */
  off_u = 0.5f / texture_width;
  off_v = 0.5f / texture_height;

  if (priv->src_x > 0)
    u0 = (priv->src_x / (gfloat) texture_width) + off_u;
  else
    u0 = 0.0f;

  if ((priv->src_x + priv->src_width) != texture_width)
    u1 = ((priv->src_x + priv->src_width) / (gfloat) texture_width) - off_u;
  else
    u1 = 1.0f;

  if (priv->src_y > 0)
    v0 = (priv->src_y / (gfloat) texture_height) + off_v;
  else
    v0 = 0.0;

  if ((priv->src_y + priv->src_height) != texture_height)
    v1 = ((priv->src_y + priv->src_height) / (gfloat) texture_height) - off_v;
  else
    v1 = 1.0f;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;

  gst_d3d11_converter_apply_orientation (self, vertex_data, u0, u1, v0, v1);

  context_handle->Unmap (priv->vertex_buffer, 0);

  return TRUE;
}

static gboolean
gst_d3d11_converter_update_dest_rect (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  const GstVideoInfo *out_info = &priv->out_info;

  if (!priv->update_dest_rect)
    return TRUE;

  priv->viewport[0].TopLeftX = priv->dest_x;
  priv->viewport[0].TopLeftY = priv->dest_y;
  priv->viewport[0].Width = priv->dest_width;
  priv->viewport[0].Height = priv->dest_height;

  priv->dest_rect.left = priv->dest_x;
  priv->dest_rect.top = priv->dest_y;
  priv->dest_rect.right = priv->dest_x + priv->dest_width;
  priv->dest_rect.bottom = priv->dest_y + priv->dest_height;

  GST_DEBUG_OBJECT (self,
      "Update viewport, TopLeftX: %f, TopLeftY: %f, Width: %f, Height %f",
      priv->viewport[0].TopLeftX, priv->viewport[0].TopLeftY,
      priv->viewport[0].Width, priv->viewport[0].Height);

  if (priv->fill_border && (priv->dest_x != 0 || priv->dest_y != 0 ||
          priv->dest_width != out_info->width ||
          priv->dest_height != out_info->height)) {
    GST_DEBUG_OBJECT (self, "Enable background color");
    priv->clear_background = TRUE;
  } else {
    GST_DEBUG_OBJECT (self, "Disable background color");
    priv->clear_background = FALSE;
  }

  switch (GST_VIDEO_INFO_FORMAT (&priv->out_info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
      priv->viewport[1].TopLeftX = priv->viewport[0].TopLeftX / 2;
      priv->viewport[1].TopLeftY = priv->viewport[0].TopLeftY / 2;
      priv->viewport[1].Width = priv->viewport[0].Width / 2;
      priv->viewport[1].Height = priv->viewport[0].Height / 2;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++)
        priv->viewport[i] = priv->viewport[1];

      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
      priv->viewport[1].TopLeftX = priv->viewport[0].TopLeftX / 2;
      priv->viewport[1].TopLeftY = priv->viewport[0].TopLeftY;
      priv->viewport[1].Width = priv->viewport[0].Width / 2;
      priv->viewport[1].Height = priv->viewport[0].Height;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++)
        priv->viewport[i] = priv->viewport[1];
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
      for (guint i = 1; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++)
        priv->viewport[i] = priv->viewport[0];
      break;
    default:
      if (priv->num_output_view > 1) {
        g_assert_not_reached ();
        return FALSE;
      }
      break;
  }

  priv->update_dest_rect = FALSE;

  return TRUE;
}

static gboolean
gst_d3d11_converter_prepare_output (GstD3D11Converter * self,
    const GstVideoInfo * info)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (info);
  ConvertInfo *cinfo = &priv->convert_info;

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
      gchar y, u, v, w;
      guint scale;

      get_planar_component (format, &y, &u, &v, &w, &scale);

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
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    {
      gchar x, y, z, w;
      guint scale;

      get_planar_component (format, &x, &y, &z, &w, &scale);

      cinfo->ps_output[0] = &output_types[OUTPUT_THREE_PLANES];
      if (info->finfo->depth[0] == 8) {
        cinfo->build_output_func[0] = g_strdup_printf (templ_OUTPUT_PLANAR,
            x, y, z);
      } else {
        cinfo->build_output_func[0] =
            g_strdup_printf (templ_OUTPUT_PLANAR_SCALED, scale, x, y, z);
      }
      break;
    }
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    {
      gchar x, y, z, w;
      guint scale;

      get_planar_component (format, &x, &y, &z, &w, &scale);

      cinfo->ps_output[0] = &output_types[OUTPUT_FOUR_PLANES];
      if (info->finfo->depth[0] == 8) {
        cinfo->build_output_func[0] = g_strdup_printf (templ_OUTPUT_PLANAR_4,
            x, y, z, w);
      } else {
        cinfo->build_output_func[0] =
            g_strdup_printf (templ_OUTPUT_PLANAR_4_SCALED, scale, x, y, z, w);
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
  GstD3D11ConverterPrivate *priv = self->priv;
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (in_info);
  gboolean out_rgb = GST_VIDEO_INFO_IS_RGB (out_info);
  gboolean out_yuv = GST_VIDEO_INFO_IS_YUV (out_info);
  gboolean out_gray = GST_VIDEO_INFO_IS_GRAY (out_info);
  ConvertInfo *cinfo = &priv->convert_info;

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
          if (priv->fast_path) {
            cinfo->sample_texture_func[0] = g_strdup (templ_SAMPLE_YUV_LUMA);
            cinfo->sample_texture_func[1] =
                g_strdup_printf (templ_SAMPLE_SEMI_PLANAR_CHROMA, u, v);
          } else {
            cinfo->sample_texture_func[0] =
                g_strdup_printf (templ_SAMPLE_SEMI_PLANAR, u, v);
            cinfo->sample_texture_func[1] =
                g_strdup (cinfo->sample_texture_func[0]);
          }
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
    case GST_VIDEO_FORMAT_Y444_16LE:
    {
      gchar x, y, z, w;
      guint scale;

      get_planar_component (format, &x, &y, &z, &w, &scale);
      if (out_rgb) {
        cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR,
            x, y, z, scale);
      } else if (out_gray) {
        cinfo->sample_texture_func[0] =
            g_strdup_printf (templ_SAMPLE_YUV_LUMA_SCALED, scale);
      } else if (out_yuv) {
        if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
            cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
          /* YUV packed or Y444 */
          cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR,
              x, y, z, scale);
        } else {
          if (priv->fast_path) {
            cinfo->sample_texture_func[0] =
                g_strdup_printf (templ_SAMPLE_YUV_LUMA_SCALED, scale);
            cinfo->sample_texture_func[1] =
                g_strdup_printf (templ_SAMPLE_PLANAR_CHROMA, y, z, scale);
          } else {
            cinfo->sample_texture_func[0] =
                g_strdup_printf (templ_SAMPLE_PLANAR, x, y, z, scale);
            cinfo->sample_texture_func[1] =
                g_strdup (cinfo->sample_texture_func[0]);
          }
        }
      } else {
        g_assert_not_reached ();
        return FALSE;
      }
      break;
    }
      /* RGB planar */
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    {
      gchar x, y, z, w;
      guint scale;

      get_planar_component (format, &x, &y, &z, &w, &scale);

      if (GST_VIDEO_INFO_N_PLANES (in_info) == 4) {
        cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR_4,
            x, y, z, w, scale);
      } else {
        cinfo->sample_texture_func[0] = g_strdup_printf (templ_SAMPLE_PLANAR,
            x, y, z, scale);
      }

      if (cinfo->ps_output[1]) {
        cinfo->sample_texture_func[1] =
            g_strdup (cinfo->sample_texture_func[0]);
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
gst_d3d11_converter_prepare_colorspace_fast (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  const GstVideoColorimetry *in_color = &in_info->colorimetry;
  const GstVideoColorimetry *out_color = &out_info->colorimetry;
  ConvertInfo *cinfo = &priv->convert_info;
  PSColorSpace *to_rgb_buf = &priv->const_data.to_rgb_buf;
  PSColorSpace *to_yuv_buf = &priv->const_data.to_yuv_buf;
  GstD3D11ColorMatrix to_rgb_matrix;
  GstD3D11ColorMatrix to_yuv_matrix;
  gchar *matrix_dump;

  memset (&to_rgb_matrix, 0, sizeof (GstD3D11ColorMatrix));
  memset (&to_yuv_matrix, 0, sizeof (GstD3D11ColorMatrix));

  for (guint i = 0; i < 2; i++) {
    cinfo->to_rgb_func[i] = templ_COLOR_SPACE_IDENTITY;
    cinfo->to_yuv_func[i] = templ_COLOR_SPACE_IDENTITY;
  }

  cinfo->gamma_decode_func = templ_GAMMA_DECODE_IDENTITY;
  cinfo->gamma_encode_func = templ_GAMMA_ENCODE_IDENTITY;
  cinfo->XYZ_convert_func = templ_XYZ_CONVERT_IDENTITY;

  if (GST_VIDEO_INFO_IS_RGB (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      if (in_color->range == out_color->range) {
        GST_DEBUG_OBJECT (self, "RGB -> RGB without colorspace conversion");
      } else {
        if (!gst_d3d11_color_range_adjust_matrix_unorm (in_info, out_info,
                &to_rgb_matrix)) {
          GST_ERROR_OBJECT (self, "Failed to get RGB range adjust matrix");
          return FALSE;
        }

        matrix_dump = gst_d3d11_dump_color_matrix (&to_rgb_matrix);
        GST_DEBUG_OBJECT (self, "RGB range adjust %s -> %s\n%s",
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
        GST_WARNING_OBJECT (self, "Invalid matrix is detected");
        yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      }

      if (!gst_d3d11_rgb_to_yuv_matrix_unorm (in_info,
              &yuv_info, &to_yuv_matrix)) {
        GST_ERROR_OBJECT (self, "Failed to get RGB -> YUV transform matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&to_yuv_matrix);
      GST_DEBUG_OBJECT (self, "RGB -> YUV matrix:\n%s", matrix_dump);
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
    GstD3D11ColorMatrix matrix;

    memset (&matrix, 0, sizeof (GstD3D11ColorMatrix));

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
        GST_ERROR_OBJECT (self, "Failed to get GRAY range adjust matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&matrix);
      GST_DEBUG_OBJECT (self, "GRAY range adjust matrix:\n%s", matrix_dump);
      g_free (matrix_dump);
    }

    if (GST_VIDEO_INFO_IS_GRAY (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (self, "GRAY to GRAY without range adjust");
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
      }

      to_yuv_matrix = matrix;
    } else if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (self, "GRAY to RGB without range adjust");
        cinfo->to_rgb_func[0] = templ_COLOR_SPACE_GRAY_TO_RGB;
      } else {
        cinfo->to_rgb_func[0] = templ_COLOR_SPACE_GRAY_TO_RGB_RANGE_ADJUST;
      }

      to_rgb_matrix = matrix;
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      if (identity) {
        GST_DEBUG_OBJECT (self, "GRAY to YUV without range adjust");
      } else {
        cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
        cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT_LUMA;
      }

      to_yuv_matrix = matrix;
    } else {
      g_assert_not_reached ();
      return FALSE;
    }
  } else if (GST_VIDEO_INFO_IS_YUV (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      GstVideoInfo yuv_info = *in_info;

      if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
          yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
        GST_WARNING_OBJECT (self, "Invalid matrix is detected");
        yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      }

      if (!gst_d3d11_yuv_to_rgb_matrix_unorm (&yuv_info,
              out_info, &to_rgb_matrix)) {
        GST_ERROR_OBJECT (self, "Failed to get YUV -> RGB transform matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&to_rgb_matrix);
      GST_DEBUG_OBJECT (self, "YUV -> RGB matrix:\n%s", matrix_dump);
      g_free (matrix_dump);

      cinfo->to_rgb_func[0] = templ_COLOR_SPACE_CONVERT;
    } else if (in_color->range != out_color->range) {
      if (!gst_d3d11_color_range_adjust_matrix_unorm (in_info, out_info,
              &to_yuv_matrix)) {
        GST_ERROR_OBJECT (self, "Failed to get GRAY range adjust matrix");
        return FALSE;
      }

      matrix_dump = gst_d3d11_dump_color_matrix (&to_yuv_matrix);
      GST_DEBUG_OBJECT (self, "YUV range adjust matrix:\n%s", matrix_dump);
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
    to_rgb_buf->coeffX[i] = to_rgb_matrix.matrix[0][i];
    to_rgb_buf->coeffY[i] = to_rgb_matrix.matrix[1][i];
    to_rgb_buf->coeffZ[i] = to_rgb_matrix.matrix[2][i];
    to_rgb_buf->offset[i] = to_rgb_matrix.offset[i];
    to_rgb_buf->min[i] = to_rgb_matrix.min[i];
    to_rgb_buf->max[i] = to_rgb_matrix.max[i];

    to_yuv_buf->coeffX[i] = to_yuv_matrix.matrix[0][i];
    to_yuv_buf->coeffY[i] = to_yuv_matrix.matrix[1][i];
    to_yuv_buf->coeffZ[i] = to_yuv_matrix.matrix[2][i];
    to_yuv_buf->offset[i] = to_yuv_matrix.offset[i];
    to_yuv_buf->min[i] = to_yuv_matrix.min[i];
    to_yuv_buf->max[i] = to_yuv_matrix.max[i];
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_prepare_colorspace (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  const GstVideoColorimetry *in_color = &in_info->colorimetry;
  const GstVideoColorimetry *out_color = &out_info->colorimetry;
  ConvertInfo *cinfo = &priv->convert_info;
  PSColorSpace *to_rgb_buf = &priv->const_data.to_rgb_buf;
  PSColorSpace *to_yuv_buf = &priv->const_data.to_yuv_buf;
  PSColorSpace *XYZ_convert_buf = &priv->const_data.XYZ_convert_buf;
  GstD3D11ColorMatrix to_rgb_matrix;
  GstD3D11ColorMatrix to_yuv_matrix;
  GstD3D11ColorMatrix XYZ_convert_matrix;
  gchar *matrix_dump;
  GstVideoInfo in_rgb_info = *in_info;
  GstVideoInfo out_rgb_info = *out_info;

  g_assert (GST_VIDEO_INFO_IS_RGB (in_info) || GST_VIDEO_INFO_IS_YUV (in_info));
  g_assert (GST_VIDEO_INFO_IS_RGB (out_info)
      || GST_VIDEO_INFO_IS_YUV (out_info));

  memset (&to_rgb_matrix, 0, sizeof (GstD3D11ColorMatrix));
  memset (&to_yuv_matrix, 0, sizeof (GstD3D11ColorMatrix));
  memset (&XYZ_convert_matrix, 0, sizeof (GstD3D11ColorMatrix));

  for (guint i = 0; i < 2; i++) {
    cinfo->to_rgb_func[i] = templ_COLOR_SPACE_IDENTITY;
    cinfo->to_yuv_func[i] = templ_COLOR_SPACE_IDENTITY;
  }

  cinfo->XYZ_convert_func = templ_XYZ_CONVERT_IDENTITY;
  cinfo->gamma_decode_func = templ_GAMMA_DECODE;
  cinfo->gamma_encode_func = templ_GAMMA_ENCODE;

  /* 1) convert input to 0..255 range RGB */
  if (GST_VIDEO_INFO_IS_RGB (in_info) &&
      in_color->range == GST_VIDEO_COLOR_RANGE_16_235) {
    in_rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

    if (!gst_d3d11_color_range_adjust_matrix_unorm (in_info, &in_rgb_info,
            &to_rgb_matrix)) {
      GST_ERROR_OBJECT (self, "Failed to get RGB range adjust matrix");
      return FALSE;
    }

    matrix_dump = gst_d3d11_dump_color_matrix (&to_rgb_matrix);
    GST_DEBUG_OBJECT (self, "Input RGB range adjust matrix\n%s", matrix_dump);
    g_free (matrix_dump);

    cinfo->to_rgb_func[0] = cinfo->to_rgb_func[1] = templ_COLOR_SPACE_CONVERT;
  } else if (GST_VIDEO_INFO_IS_YUV (in_info)) {
    GstVideoInfo yuv_info;
    GstVideoFormat rgb_format;

    yuv_info = *in_info;
    if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      GST_WARNING_OBJECT (self, "Invalid matrix is detected");
      yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }

    if (in_info->finfo->depth[0] == 8) {
      rgb_format = GST_VIDEO_FORMAT_RGBA;
    } else {
      rgb_format = GST_VIDEO_FORMAT_RGBA64_LE;
    }

    gst_video_info_set_format (&in_rgb_info, rgb_format, in_info->width,
        in_info->height);
    in_rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    in_rgb_info.colorimetry.transfer = in_color->transfer;
    in_rgb_info.colorimetry.primaries = in_color->primaries;

    if (!gst_d3d11_yuv_to_rgb_matrix_unorm (&yuv_info, &in_rgb_info,
            &to_rgb_matrix)) {
      GST_ERROR_OBJECT (self, "Failed to get YUV -> RGB transform matrix");
      return FALSE;
    }

    matrix_dump = gst_d3d11_dump_color_matrix (&to_rgb_matrix);
    GST_DEBUG_OBJECT (self, "YUV -> RGB matrix:\n%s", matrix_dump);
    g_free (matrix_dump);

    cinfo->to_rgb_func[0] = cinfo->to_rgb_func[1] = templ_COLOR_SPACE_CONVERT;
  }

  /* 2) convert gamma/XYZ converted 0..255 RGB to output format */
  if (GST_VIDEO_INFO_IS_RGB (out_info) &&
      out_color->range == GST_VIDEO_COLOR_RANGE_16_235) {
    out_rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

    if (!gst_d3d11_color_range_adjust_matrix_unorm (&out_rgb_info, out_info,
            &to_yuv_matrix)) {
      GST_ERROR_OBJECT (self, "Failed to get RGB range adjust matrix");
      return FALSE;
    }

    matrix_dump = gst_d3d11_dump_color_matrix (&to_yuv_matrix);
    GST_DEBUG_OBJECT (self, "Output RGB range adjust matrix\n%s", matrix_dump);
    g_free (matrix_dump);

    cinfo->to_yuv_func[0] = cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT;
  } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
    GstVideoInfo yuv_info;

    yuv_info = *out_info;
    if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      GST_WARNING_OBJECT (self, "Invalid matrix is detected");
      yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }

    gst_video_info_set_format (&out_rgb_info,
        GST_VIDEO_INFO_FORMAT (&in_rgb_info), out_info->width,
        out_info->height);
    out_rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    out_rgb_info.colorimetry.transfer = out_color->transfer;
    out_rgb_info.colorimetry.primaries = out_color->primaries;

    if (!gst_d3d11_rgb_to_yuv_matrix_unorm (&out_rgb_info,
            &yuv_info, &to_yuv_matrix)) {
      GST_ERROR_OBJECT (self, "Failed to get RGB -> YUV transform matrix");
      return FALSE;
    }

    matrix_dump = gst_d3d11_dump_color_matrix (&to_yuv_matrix);
    GST_DEBUG_OBJECT (self, "RGB -> YUV matrix:\n%s", matrix_dump);
    g_free (matrix_dump);

    if (GST_VIDEO_INFO_N_PLANES (out_info) == 1 ||
        cinfo->ps_output[0] == &output_types[OUTPUT_THREE_PLANES]) {
      /* YUV packed or Y444 */
      cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT;
    } else {
      cinfo->to_yuv_func[0] = templ_COLOR_SPACE_CONVERT_LUMA;
      cinfo->to_yuv_func[1] = templ_COLOR_SPACE_CONVERT_CHROMA;
    }
  }

  /* TODO: handle HDR mastring display info */
  if (priv->do_primaries) {
    const GstVideoColorPrimariesInfo *in_pinfo;
    const GstVideoColorPrimariesInfo *out_pinfo;

    in_pinfo = gst_video_color_primaries_get_info (in_color->primaries);
    out_pinfo = gst_video_color_primaries_get_info (out_color->primaries);

    if (!gst_d3d11_color_primaries_matrix_unorm (in_pinfo, out_pinfo,
            &XYZ_convert_matrix)) {
      GST_ERROR_OBJECT (self, "Failed to get primaries conversion matrix");
      return FALSE;
    }

    matrix_dump = gst_d3d11_dump_color_matrix (&XYZ_convert_matrix);
    GST_DEBUG_OBJECT (self, "Primaries conversion matrix:\n%s", matrix_dump);
    g_free (matrix_dump);

    cinfo->XYZ_convert_func = templ_XYZ_CONVERT;
  }

  for (guint i = 0; i < 3; i++) {
    to_rgb_buf->coeffX[i] = to_rgb_matrix.matrix[0][i];
    to_rgb_buf->coeffY[i] = to_rgb_matrix.matrix[1][i];
    to_rgb_buf->coeffZ[i] = to_rgb_matrix.matrix[2][i];
    to_rgb_buf->offset[i] = to_rgb_matrix.offset[i];
    to_rgb_buf->min[i] = to_rgb_matrix.min[i];
    to_rgb_buf->max[i] = to_rgb_matrix.max[i];

    to_yuv_buf->coeffX[i] = to_yuv_matrix.matrix[0][i];
    to_yuv_buf->coeffY[i] = to_yuv_matrix.matrix[1][i];
    to_yuv_buf->coeffZ[i] = to_yuv_matrix.matrix[2][i];
    to_yuv_buf->offset[i] = to_yuv_matrix.offset[i];
    to_yuv_buf->min[i] = to_yuv_matrix.min[i];
    to_yuv_buf->max[i] = to_yuv_matrix.max[i];

    XYZ_convert_buf->coeffX[i] = XYZ_convert_matrix.matrix[0][i];
    XYZ_convert_buf->coeffY[i] = XYZ_convert_matrix.matrix[1][i];
    XYZ_convert_buf->coeffZ[i] = XYZ_convert_matrix.matrix[2][i];
    XYZ_convert_buf->offset[i] = XYZ_convert_matrix.offset[i];
    XYZ_convert_buf->min[i] = XYZ_convert_matrix.min[i];
    XYZ_convert_buf->max[i] = XYZ_convert_matrix.max[i];
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_setup_lut (GstD3D11Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstD3D11Device *device = self->device;
  ID3D11Device *device_handle = gst_d3d11_device_get_device_handle (device);
  D3D11_TEXTURE1D_DESC desc;
  D3D11_SUBRESOURCE_DATA subresource;
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
  HRESULT hr;
  ComPtr < ID3D11Texture1D > gamma_dec_lut;
  ComPtr < ID3D11Texture1D > gamma_enc_lut;
  ComPtr < ID3D11ShaderResourceView > gamma_dec_srv;
  ComPtr < ID3D11ShaderResourceView > gamma_enc_srv;
  guint16 gamma_dec_table[GAMMA_LUT_SIZE];
  guint16 gamma_enc_table[GAMMA_LUT_SIZE];
  GstVideoTransferFunction in_trc = in_info->colorimetry.transfer;
  GstVideoTransferFunction out_trc = out_info->colorimetry.transfer;
  gdouble scale = (gdouble) 1 / (GAMMA_LUT_SIZE - 1);

  memset (&desc, 0, sizeof (D3D11_TEXTURE1D_DESC));
  memset (&subresource, 0, sizeof (D3D11_SUBRESOURCE_DATA));
  memset (&srv_desc, 0, sizeof (D3D11_SHADER_RESOURCE_VIEW_DESC));

  for (guint i = 0; i < GAMMA_LUT_SIZE; i++) {
    gdouble val = gst_video_transfer_function_decode (in_trc, i * scale);
    val = rint (val * 65535);
    val = CLAMP (val, 0, 65535);
    gamma_dec_table[i] = (guint16) val;

    val = gst_video_transfer_function_encode (out_trc, i * scale);
    val = rint (val * 65535);
    val = CLAMP (val, 0, 65535);
    gamma_enc_table[i] = (guint16) val;
  }

  desc.Width = GAMMA_LUT_SIZE;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R16_UNORM;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  subresource.pSysMem = gamma_dec_table;
  subresource.SysMemPitch = GAMMA_LUT_SIZE * sizeof (guint16);

  srv_desc.Format = DXGI_FORMAT_R16_UNORM;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
  srv_desc.Texture1D.MipLevels = 1;

  hr = device_handle->CreateTexture1D (&desc, &subresource, &gamma_dec_lut);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Failed to create gamma decode LUT");
    return FALSE;
  }

  hr = device_handle->CreateShaderResourceView (gamma_dec_lut.Get (), &srv_desc,
      &gamma_dec_srv);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Failed to create gamma decode LUT SRV");
    return FALSE;
  }

  subresource.pSysMem = gamma_enc_table;
  hr = device_handle->CreateTexture1D (&desc, &subresource, &gamma_enc_lut);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Failed to create gamma encode LUT");
    return FALSE;
  }

  hr = device_handle->CreateShaderResourceView (gamma_enc_lut.Get (), &srv_desc,
      &gamma_enc_srv);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Failed to create gamma decode LUT SRV");
    return FALSE;
  }

  priv->gamma_dec_lut = gamma_dec_lut.Detach ();
  priv->gamma_enc_lut = gamma_enc_lut.Detach ();
  priv->gamma_dec_srv = gamma_dec_srv.Detach ();
  priv->gamma_enc_srv = gamma_enc_srv.Detach ();

  return TRUE;
}

static void
gst_d3d11_converter_calculate_border_color (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstD3D11ColorMatrix *m = &priv->clear_color_matrix;
  const GstVideoInfo *out_info = &priv->out_info;
  gdouble a;
  gdouble rgb[3];
  gdouble converted[3];
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (out_info);

  a = ((priv->border_color & 0xffff000000000000) >> 48) / (gdouble) G_MAXUINT16;
  rgb[0] =
      ((priv->border_color & 0x0000ffff00000000) >> 32) / (gdouble) G_MAXUINT16;
  rgb[1] =
      ((priv->border_color & 0x00000000ffff0000) >> 16) / (gdouble) G_MAXUINT16;
  rgb[2] = (priv->border_color & 0x000000000000ffff) / (gdouble) G_MAXUINT16;

  for (guint i = 0; i < 3; i++) {
    converted[i] = 0;
    for (guint j = 0; j < 3; j++) {
      converted[i] += m->matrix[i][j] * rgb[j];
    }
    converted[i] += m->offset[i];
    converted[i] = CLAMP (converted[i], m->min[i], m->max[i]);
  }

  GST_DEBUG_OBJECT (self, "Calculated background color ARGB: %f, %f, %f, %f",
      a, converted[0], converted[1], converted[2]);

  /* background color for video processor */
  priv->background_color.RGBA.R = converted[0];
  priv->background_color.RGBA.G = converted[1];
  priv->background_color.RGBA.B = converted[2];
  priv->background_color.RGBA.A = a;

  /* scale down if output is planar high bitdepth format */
  switch (format) {
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBRA_10LE:
      for (guint i = 0; i < 3; i++) {
        converted[i] /= 64.0;
      }
      a /= 64.0;
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
      for (guint i = 0; i < 3; i++) {
        converted[i] /= 16.0;
      }
      a /= 16.0;
      break;
    default:
      break;
  }

  if ((GST_VIDEO_INFO_IS_RGB (out_info) &&
          GST_VIDEO_INFO_N_PLANES (out_info) == 1) ||
      GST_VIDEO_INFO_IS_GRAY (out_info)) {
    for (guint i = 0; i < 3; i++)
      priv->clear_color[0][i] = converted[i];
    priv->clear_color[0][3] = a;
  } else {
    switch (format) {
      case GST_VIDEO_FORMAT_VUYA:
        priv->clear_color[0][0] = converted[2];
        priv->clear_color[0][1] = converted[1];
        priv->clear_color[0][2] = converted[0];
        priv->clear_color[0][3] = a;
        break;
      case GST_VIDEO_FORMAT_AYUV:
      case GST_VIDEO_FORMAT_AYUV64:
        priv->clear_color[0][0] = a;
        priv->clear_color[0][1] = converted[0];
        priv->clear_color[0][2] = converted[1];
        priv->clear_color[0][3] = converted[2];
        break;
      case GST_VIDEO_FORMAT_NV12:
      case GST_VIDEO_FORMAT_NV21:
      case GST_VIDEO_FORMAT_P010_10LE:
      case GST_VIDEO_FORMAT_P012_LE:
      case GST_VIDEO_FORMAT_P016_LE:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[0][1] = 0;
        priv->clear_color[0][2] = 0;
        priv->clear_color[0][3] = 1.0;
        if (format == GST_VIDEO_FORMAT_NV21) {
          priv->clear_color[1][0] = converted[2];
          priv->clear_color[1][1] = converted[1];
        } else {
          priv->clear_color[1][0] = converted[1];
          priv->clear_color[1][1] = converted[2];
        }
        priv->clear_color[1][2] = 0;
        priv->clear_color[1][3] = 1.0;
        break;
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
      case GST_VIDEO_FORMAT_Y444_16LE:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[0][1] = 0;
        priv->clear_color[0][2] = 0;
        priv->clear_color[0][3] = 1.0;
        if (format == GST_VIDEO_FORMAT_YV12) {
          priv->clear_color[1][0] = converted[2];
          priv->clear_color[2][0] = converted[1];
        } else {
          priv->clear_color[1][0] = converted[1];
          priv->clear_color[2][0] = converted[2];
        }
        priv->clear_color[1][1] = 0;
        priv->clear_color[1][2] = 0;
        priv->clear_color[1][3] = 1.0;
        priv->clear_color[2][1] = 0;
        priv->clear_color[2][2] = 0;
        priv->clear_color[2][3] = 1.0;
        break;
      case GST_VIDEO_FORMAT_RGBP:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[2][0] = converted[2];
        break;
      case GST_VIDEO_FORMAT_BGRP:
        priv->clear_color[0][0] = converted[2];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[2][0] = converted[0];
        break;
      case GST_VIDEO_FORMAT_GBR:
      case GST_VIDEO_FORMAT_GBR_10LE:
      case GST_VIDEO_FORMAT_GBR_12LE:
        priv->clear_color[0][0] = converted[1];
        priv->clear_color[1][0] = converted[2];
        priv->clear_color[2][0] = converted[0];
        break;
      case GST_VIDEO_FORMAT_GBRA:
      case GST_VIDEO_FORMAT_GBRA_10LE:
      case GST_VIDEO_FORMAT_GBRA_12LE:
        priv->clear_color[0][0] = converted[1];
        priv->clear_color[1][0] = converted[2];
        priv->clear_color[2][0] = converted[0];
        priv->clear_color[3][0] = a;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
}

static gboolean
gst_d3d11_converter_setup_processor (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstD3D11Device *device = self->device;
  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;
  HRESULT hr;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;
  ComPtr < ID3D11VideoContext1 > video_context1;
  ComPtr < ID3D11VideoContext2 > video_context2;
  ComPtr < ID3D11VideoProcessorEnumerator > enumerator;
  ComPtr < ID3D11VideoProcessorEnumerator1 > enumerator1;
  ComPtr < ID3D11VideoProcessor > processor;
  UINT support_flags;
  BOOL conversion_supported = TRUE;
  DXGI_COLOR_SPACE_TYPE in_space, out_space;
  DXGI_FORMAT in_dxgi_format = priv->in_d3d11_format.dxgi_format;
  DXGI_FORMAT out_dxgi_format = priv->out_d3d11_format.dxgi_format;
  UINT in_format_flags = priv->in_d3d11_format.format_support[0];
  UINT out_format_flags = priv->out_d3d11_format.format_support[0];

  if (GST_VIDEO_INFO_IS_GRAY (&priv->in_info) ||
      GST_VIDEO_INFO_IS_GRAY (&priv->out_info)) {
    return FALSE;
  }

  /* Not a native DXGI format */
  if (in_dxgi_format == DXGI_FORMAT_UNKNOWN ||
      out_dxgi_format == DXGI_FORMAT_UNKNOWN) {
    return FALSE;
  }

  /* cannot bind to processor in/out view */
  if ((in_format_flags & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) == 0 ||
      (out_format_flags & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT) == 0) {
    return FALSE;
  }

  if (!gst_video_info_to_dxgi_color_space (&priv->in_info, &in_space)) {
    GST_WARNING_OBJECT (self, "Unknown input DXGI colorspace");
    return FALSE;
  }

  if (!gst_video_info_to_dxgi_color_space (&priv->out_info, &out_space)) {
    GST_WARNING_OBJECT (self, "Unknown output DXGI colorspace");
    return FALSE;
  }

  video_device = gst_d3d11_device_get_video_device_handle (self->device);
  if (!video_device) {
    GST_DEBUG_OBJECT (self, "video device interface is not available");
    return FALSE;
  }

  video_context = gst_d3d11_device_get_video_context_handle (self->device);
  if (!video_context) {
    GST_DEBUG_OBJECT (self, "video context interface is not available");
    return FALSE;
  }

  hr = video_context->QueryInterface (IID_PPV_ARGS (&video_context1));
  if (!gst_d3d11_result (hr, device)) {
    GST_DEBUG_OBJECT (self, "ID3D11VideoContext1 interface is not available");
    return FALSE;
  }

  memset (&desc, 0, sizeof (D3D11_VIDEO_PROCESSOR_CONTENT_DESC));

  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = priv->in_info.width;
  desc.InputHeight = priv->in_info.height;
  desc.OutputWidth = priv->out_info.width;
  desc.OutputHeight = priv->out_info.height;
  /* TODO: make option for this */
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = video_device->CreateVideoProcessorEnumerator (&desc, &enumerator);
  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (self, "Failed to create enumerator");
    return FALSE;
  }

  hr = enumerator.As (&enumerator1);
  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (self,
        "ID3D11VideoProcessorEnumerator1 interface is not available");
    return FALSE;
  }

  support_flags = 0;
  hr = enumerator1->CheckVideoProcessorFormat (in_dxgi_format, &support_flags);
  if (!gst_d3d11_result (hr, device) || (support_flags & 0x1) == 0) {
    GST_DEBUG_OBJECT (self, "Input format is not supported");
    return FALSE;
  }

  support_flags = 0;
  hr = enumerator1->CheckVideoProcessorFormat (out_dxgi_format, &support_flags);
  if (!gst_d3d11_result (hr, device) || (support_flags & 0x2) == 0) {
    GST_DEBUG_OBJECT (self, "Output format is not supported");
    return FALSE;
  }

  hr = enumerator1->CheckVideoProcessorFormatConversion (in_dxgi_format,
      in_space, out_dxgi_format, out_space, &conversion_supported);
  if (!gst_d3d11_result (hr, device) || !conversion_supported) {
    GST_DEBUG_OBJECT (self, "Conversion is not supported");
    return FALSE;
  }

  hr = enumerator1->GetVideoProcessorCaps (&priv->processor_caps);
  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (self, "Failed to query processor caps");
    return FALSE;
  }

  hr = video_device->CreateVideoProcessor (enumerator1.Get (), 0, &processor);
  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (self, "Failed to create processor");
    return FALSE;
  }

  GstD3D11DeviceLockGuard lk (device);
  /* We don't want auto processing by driver */
  video_context1->VideoProcessorSetStreamAutoProcessingMode
      (processor.Get (), 0, FALSE);
  video_context1->VideoProcessorSetStreamColorSpace1 (processor.Get (),
      0, in_space);
  video_context1->VideoProcessorSetOutputColorSpace1 (processor.Get (),
      out_space);

  priv->video_device = video_device;
  video_device->AddRef ();
  priv->processor = processor.Detach ();
  hr = video_context1.As (&video_context2);
  if (SUCCEEDED (hr))
    priv->video_context2 = video_context2.Detach ();
  priv->video_context = video_context1.Detach ();
  priv->enumerator = enumerator1.Detach ();

  priv->src_rect.left = 0;
  priv->src_rect.top = 0;
  priv->src_rect.right = priv->in_info.width;
  priv->src_rect.bottom = priv->in_info.height;

  priv->dest_rect.left = 0;
  priv->dest_rect.top = 0;
  priv->dest_rect.right = priv->out_info.width;
  priv->dest_rect.bottom = priv->out_info.height;

  priv->dest_full_rect = priv->dest_rect;

  return TRUE;
}

/**
 * gst_d3d11_converter_new:
 * @device: a #GstD3D11Device
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @method: (inout) (optional) (nullable): a #GstD3D11ConverterMethod

 * Create a new converter object to convert between @in_info and @out_info
 * with @method. When @method is not specified, converter will configure
 * conversion path for all available method. Otherwise, converter will configure
 * conversion path only for specified method(s) and set @method will be updated
 * with supported method.
 *
 * Returns: (transfer full) (nullable): a #GstD3D11Converter or %NULL if conversion is not possible
 *
 * Since: 1.22
 */
GstD3D11Converter *
gst_d3d11_converter_new (GstD3D11Device * device, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * config)
{
  GstD3D11Converter *self;
  GstD3D11ConverterPrivate *priv;
  GstD3D11Format in_d3d11_format;
  GstD3D11Format out_d3d11_format;
  guint wanted_backend = 0;
  gboolean allow_gamma = FALSE;
  gboolean allow_primaries = FALSE;
  D3D11_FILTER sampler_filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  gchar *backend_str;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), nullptr);
  g_return_val_if_fail (in_info != nullptr, nullptr);
  g_return_val_if_fail (out_info != nullptr, nullptr);

  self = (GstD3D11Converter *) g_object_new (GST_TYPE_D3D11_CONVERTER, nullptr);
  gst_object_ref_sink (self);
  priv = self->priv;

  if (config) {
    gint value;
    gst_structure_get_flags (config, GST_D3D11_CONVERTER_OPT_BACKEND,
        GST_TYPE_D3D11_CONVERTER_BACKEND, &wanted_backend);

    if (gst_structure_get_enum (config, GST_D3D11_CONVERTER_OPT_GAMMA_MODE,
            GST_TYPE_VIDEO_GAMMA_MODE, &value) &&
        (GstVideoGammaMode) value != GST_VIDEO_GAMMA_MODE_NONE) {
      allow_gamma = TRUE;
    }

    if (gst_structure_get_enum (config, GST_D3D11_CONVERTER_OPT_PRIMARIES_MODE,
            GST_TYPE_VIDEO_PRIMARIES_MODE, &value) &&
        (GstVideoPrimariesMode) value != GST_VIDEO_PRIMARIES_MODE_NONE) {
      allow_primaries = TRUE;
    }

    gst_structure_get_enum (config, GST_D3D11_CONVERTER_OPT_SAMPLER_FILTER,
        GST_TYPE_D3D11_CONVERTER_SAMPLER_FILTER, (int *) &sampler_filter);
    gst_structure_free (config);
  }

  if (!wanted_backend) {
    wanted_backend =
        GST_D3D11_CONVERTER_BACKEND_SHADER |
        GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR;
  }

  backend_str = g_flags_to_string (GST_TYPE_D3D11_CONVERTER_BACKEND,
      wanted_backend);

  GST_DEBUG_OBJECT (self,
      "Setup converter with format %s -> %s, wanted backend: %s, "
      "allow gamma conversion: %d, allow primaries conversion: %d ",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)),
      backend_str, allow_gamma, allow_primaries);
  g_free (backend_str);

  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (in_info),
          &in_d3d11_format)) {
    GST_ERROR_OBJECT (self, "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
    gst_object_unref (self);

    return nullptr;
  }

  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (out_info),
          &out_d3d11_format)) {
    GST_ERROR_OBJECT (self, "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
    gst_object_unref (self);

    return nullptr;
  }

  self->device = (GstD3D11Device *) gst_object_ref (device);
  priv->fast_path = TRUE;
  priv->const_data.alpha = 1.0;
  priv->in_info = *in_info;
  priv->fallback_info = *in_info;
  priv->piv_info = *in_info;
  priv->out_info = *out_info;
  priv->in_d3d11_format = in_d3d11_format;
  priv->out_d3d11_format = out_d3d11_format;

  /* Init properties */
  priv->src_width = GST_VIDEO_INFO_WIDTH (in_info);
  priv->src_height = GST_VIDEO_INFO_HEIGHT (in_info);
  priv->dest_width = GST_VIDEO_INFO_WIDTH (out_info);
  priv->dest_height = GST_VIDEO_INFO_HEIGHT (out_info);
  priv->alpha = 1.0;
  for (guint i = 0; i < G_N_ELEMENTS (priv->blend_factor); i++)
    priv->blend_factor[i] = 1.0;
  priv->blend_sample_mask = 0xffffffff;
  priv->border_color = 0xffff000000000000;

  if (GST_VIDEO_INFO_IS_RGB (out_info)) {
    GstVideoInfo rgb_info = *out_info;
    rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    gst_d3d11_color_range_adjust_matrix_unorm (&rgb_info, out_info,
        &priv->clear_color_matrix);
  } else {
    GstVideoInfo rgb_info;
    GstVideoInfo yuv_info;

    gst_video_info_set_format (&rgb_info, GST_VIDEO_FORMAT_RGBA64_LE,
        out_info->width, out_info->height);
    convert_info_gray_to_yuv (out_info, &yuv_info);

    if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      GST_WARNING_OBJECT (self, "Invalid matrix is detected");
      yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }

    gst_d3d11_rgb_to_yuv_matrix_unorm (&rgb_info,
        &yuv_info, &priv->clear_color_matrix);
  }

  gst_d3d11_converter_calculate_border_color (self);

  if ((wanted_backend & GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR) != 0) {
    if (gst_d3d11_converter_setup_processor (self)) {
      GST_DEBUG_OBJECT (self, "Video processor is available");
      priv->supported_backend |= GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR;
    }
  }

  if ((wanted_backend & GST_D3D11_CONVERTER_BACKEND_SHADER) == 0)
    goto out;

  if (!GST_VIDEO_INFO_IS_GRAY (in_info) && !GST_VIDEO_INFO_IS_GRAY (out_info)) {
    if (in_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
        out_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
        !gst_video_transfer_function_is_equivalent (in_info->
            colorimetry.transfer, GST_VIDEO_INFO_COMP_DEPTH (in_info, 0),
            out_info->colorimetry.transfer, GST_VIDEO_INFO_COMP_DEPTH (out_info,
                0))) {
      if (allow_gamma) {
        GST_DEBUG_OBJECT (self, "Different transfer function %d -> %d",
            in_info->colorimetry.transfer, out_info->colorimetry.transfer);
        priv->fast_path = FALSE;
      } else {
        GST_DEBUG_OBJECT (self,
            "Different transfer function %d -> %d but gamma remap is disabled",
            in_info->colorimetry.transfer, out_info->colorimetry.transfer);
      }
    }

    if (in_info->colorimetry.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN &&
        out_info->colorimetry.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN &&
        !gst_video_color_primaries_is_equivalent (in_info->
            colorimetry.primaries, out_info->colorimetry.primaries)) {
      if (allow_primaries) {
        GST_DEBUG_OBJECT (self, "Different primaries %d -> %d",
            in_info->colorimetry.primaries, out_info->colorimetry.primaries);
        priv->fast_path = FALSE;
        priv->do_primaries = TRUE;
      } else {
        GST_DEBUG_OBJECT (self,
            "Different primaries %d -> %d but chromatic adaptation is disabled",
            in_info->colorimetry.primaries, out_info->colorimetry.primaries);
      }
    }
  }

  if (!gst_d3d11_converter_prepare_output (self, out_info))
    goto out;

  /* XXX: hard to make sampling of packed 4:2:2 format, use software
   * converter to convert YUV2 to Y42B */
  if (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_YUY2) {
    GstVideoInfo tmp_info;

    gst_video_info_set_interlaced_format (&tmp_info, GST_VIDEO_FORMAT_Y42B,
        GST_VIDEO_INFO_INTERLACE_MODE (in_info),
        GST_VIDEO_INFO_WIDTH (in_info), GST_VIDEO_INFO_HEIGHT (in_info));
    tmp_info.chroma_site = in_info->chroma_site;
    tmp_info.colorimetry = in_info->colorimetry;
    tmp_info.fps_n = in_info->fps_n;
    tmp_info.fps_d = in_info->fps_d;
    tmp_info.par_n = in_info->par_n;
    tmp_info.par_d = in_info->par_d;

    priv->unpack_convert =
        gst_video_converter_new (in_info, &tmp_info, nullptr);
    if (!priv->unpack_convert) {
      GST_ERROR_OBJECT (self, "Couldn't create unpack convert");
      priv->supported_backend = (GstD3D11ConverterBackend) 0;
      goto out;
    }

    priv->fallback_info = tmp_info;
    in_info = &priv->fallback_info;
  }

  if (!gst_d3d11_converter_prepare_sample_texture (self, in_info, out_info))
    goto out;

  if (priv->fast_path) {
    if (!gst_d3d11_converter_prepare_colorspace_fast (self, in_info, out_info))
      goto out;
  } else {
    if (!gst_d3d11_converter_prepare_colorspace (self, in_info, out_info))
      goto out;

    if (!gst_d3d11_converter_setup_lut (self, in_info, out_info))
      goto out;
  }

  if (!gst_d3d11_color_convert_setup_shader (self, in_info, out_info,
          sampler_filter)) {
    goto out;
  }

  priv->supported_backend |= GST_D3D11_CONVERTER_BACKEND_SHADER;

out:
  if (priv->supported_backend == 0) {
    GST_ERROR_OBJECT (self, "Conversion %s to %s not supported",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

static gboolean
gst_d3d11_converter_convert_internal (GstD3D11Converter * self,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES],
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  GstD3D11ConverterPrivate *priv;
  ComPtr < ID3D11Resource > resource;
  ComPtr < ID3D11Texture2D > texture;
  D3D11_TEXTURE2D_DESC desc;
  ConvertInfo *cinfo;
  ID3D11DeviceContext *context;
  UINT offsets = 0;
  UINT vertex_stride = sizeof (VertexData);
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { nullptr, };

  priv = self->priv;
  cinfo = &priv->convert_info;
  context = gst_d3d11_device_get_device_context_handle (self->device);

  /* check texture resolution and update crop area */
  srv[0]->GetResource (&resource);
  resource.As (&texture);
  texture->GetDesc (&desc);

  if (desc.Width != (guint) priv->input_texture_width ||
      desc.Height != (guint) priv->input_texture_height) {
    GST_DEBUG_OBJECT (self, "Update vertext buffer, texture resolution: %dx%d",
        desc.Width, desc.Height);

    priv->input_texture_width = desc.Width;
    priv->input_texture_height = desc.Height;
    priv->update_src_rect = TRUE;

    if (!gst_d3d11_converter_update_src_rect (self)) {
      GST_ERROR_OBJECT (self, "Cannot update src rect");
      return FALSE;
    }
  }

  if (priv->update_alpha) {
    D3D11_MAPPED_SUBRESOURCE map;
    PSConstBuffer *const_buffer;
    HRESULT hr;

    hr = context->Map (priv->const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

    if (!gst_d3d11_result (hr, self->device)) {
      GST_ERROR_OBJECT (self,
          "Couldn't map constant buffer, hr: 0x%x", (guint) hr);
      return FALSE;
    }

    const_buffer = (PSConstBuffer *) map.pData;
    memcpy (const_buffer, &priv->const_data, sizeof (PSConstBuffer));

    context->Unmap (priv->const_buffer, 0);
    priv->update_alpha = FALSE;
  }

  if (priv->clear_background) {
    for (guint i = 0; i < priv->num_output_view; i++)
      context->ClearRenderTargetView (rtv[i], priv->clear_color[i]);
  }

  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout (priv->layout);
  context->IASetVertexBuffers (0, 1, &priv->vertex_buffer, &vertex_stride,
      &offsets);
  context->IASetIndexBuffer (priv->index_buffer, DXGI_FORMAT_R16_UINT, 0);
  context->PSSetSamplers (0, 1, &priv->linear_sampler);
  context->VSSetShader (priv->vs, nullptr, 0);
  context->PSSetConstantBuffers (0, 1, &priv->const_buffer);
  context->PSSetShaderResources (0, priv->num_input_view, srv);
  if (!priv->fast_path) {
    ID3D11ShaderResourceView *gamma_srv[2];
    gamma_srv[0] = priv->gamma_dec_srv;
    gamma_srv[1] = priv->gamma_enc_srv;
    context->PSSetShaderResources (4, 2, gamma_srv);
  }

  context->PSSetShader (priv->ps[0], nullptr, 0);
  context->RSSetViewports (cinfo->ps_output[0]->num_rtv, priv->viewport);
  context->OMSetRenderTargets (cinfo->ps_output[0]->num_rtv, rtv, nullptr);
  if (priv->blend) {
    context->OMSetBlendState (priv->blend,
        priv->blend_factor, priv->blend_sample_mask);
  } else {
    context->OMSetBlendState (nullptr, nullptr, 0xffffffff);
  }
  context->DrawIndexed (6, 0, 0);

  if (priv->ps[1]) {
    guint view_offset = cinfo->ps_output[0]->num_rtv;

    context->PSSetShader (priv->ps[1], nullptr, 0);
    context->RSSetViewports (cinfo->ps_output[1]->num_rtv,
        &priv->viewport[view_offset]);
    context->OMSetRenderTargets (cinfo->ps_output[1]->num_rtv,
        &rtv[view_offset], nullptr);
    context->DrawIndexed (6, 0, 0);
  }

  context->PSSetShaderResources (0, 4, clear_view);
  context->OMSetRenderTargets (0, nullptr, nullptr);

  return TRUE;
}

static gboolean
gst_d3d11_converter_check_bind_flags_for_piv (guint bind_flags)
{
  static const guint flags = (D3D11_BIND_DECODER |
      D3D11_BIND_VIDEO_ENCODER | D3D11_BIND_RENDER_TARGET |
      D3D11_BIND_UNORDERED_ACCESS);

  if (bind_flags == 0)
    return TRUE;

  if ((bind_flags & flags) != 0)
    return TRUE;

  return FALSE;
}

static gboolean
gst_d3d11_converter_is_d3d11_buffer (GstD3D11Converter * self,
    GstBuffer * buffer)
{
  if (gst_buffer_n_memory (buffer) == 0) {
    GST_WARNING_OBJECT (self, "Empty buffer");
    return FALSE;
  }

  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);
    GstD3D11Memory *dmem;

    if (!gst_is_d3d11_memory (mem)) {
      GST_LOG_OBJECT (self, "Memory at %d is not d3d11 memory", i);
      return FALSE;
    }

    dmem = GST_D3D11_MEMORY_CAST (mem);
    if (dmem->device != self->device) {
      GST_LOG_OBJECT (self, "Memory at %d belongs to different device", i);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_create_fallback_buffer (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstD3D11AllocationParams *params;
  GstBufferPool *pool;
  GstCaps *caps;
  guint bind_flags = D3D11_BIND_SHADER_RESOURCE;
  GstStructure *config;

  gst_clear_buffer (&priv->fallback_inbuf);

  params = gst_d3d11_allocation_params_new (self->device, &priv->fallback_info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);

  caps = gst_video_info_to_caps (&priv->fallback_info);
  pool = gst_d3d11_buffer_pool_new (self->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, priv->fallback_info.size,
      0, 0);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_caps_unref (caps);
  gst_d3d11_allocation_params_free (params);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to set active");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_buffer_pool_acquire_buffer (pool, &priv->fallback_inbuf, nullptr);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  if (!priv->fallback_inbuf) {
    GST_ERROR_OBJECT (self, "Failed to create fallback buffer");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_upload_for_shader (GstD3D11Converter * self,
    GstBuffer * in_buf)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstVideoFrame frame, fallback_frame;
  GstVideoInfo *fallback_info = &priv->fallback_info;
  gboolean ret = TRUE;

  if (!gst_video_frame_map (&frame, &priv->in_info, in_buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return FALSE;
  }

  /* Probably cropped buffer */
  if (fallback_info->width != GST_VIDEO_FRAME_WIDTH (&frame) ||
      fallback_info->height != GST_VIDEO_FRAME_HEIGHT (&frame)) {
    gst_clear_buffer (&priv->fallback_inbuf);

    if (GST_VIDEO_INFO_FORMAT (&priv->in_info) == GST_VIDEO_FORMAT_YUY2 &&
        priv->unpack_convert) {
      gst_video_info_set_interlaced_format (fallback_info,
          GST_VIDEO_FORMAT_Y42B, GST_VIDEO_INFO_INTERLACE_MODE (&frame.info),
          GST_VIDEO_INFO_WIDTH (&frame.info),
          GST_VIDEO_INFO_HEIGHT (&frame.info));
      fallback_info->chroma_site = frame.info.chroma_site;
      fallback_info->colorimetry = frame.info.colorimetry;
      fallback_info->fps_n = frame.info.fps_n;
      fallback_info->fps_d = frame.info.fps_d;
      fallback_info->par_n = frame.info.par_n;
      fallback_info->par_d = frame.info.par_d;

      if (priv->unpack_convert)
        gst_video_converter_free (priv->unpack_convert);

      priv->unpack_convert =
          gst_video_converter_new (&frame.info, fallback_info, nullptr);

      g_assert (priv->unpack_convert);
    } else {
      *fallback_info = frame.info;
    }
  }

  if (!priv->fallback_inbuf &&
      !gst_d3d11_converter_create_fallback_buffer (self)) {
    goto error;
  }

  if (!gst_video_frame_map (&fallback_frame,
          &priv->fallback_info, priv->fallback_inbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map fallback buffer");
    goto error;
  }

  if (priv->unpack_convert) {
    gst_video_converter_frame (priv->unpack_convert, &frame, &fallback_frame);
  } else {
    ret = gst_video_frame_copy (&fallback_frame, &frame);
  }
  gst_video_frame_unmap (&fallback_frame);
  gst_video_frame_unmap (&frame);

  return ret;

error:
  gst_video_frame_unmap (&frame);
  return FALSE;
}

static gboolean
gst_d3d11_converter_map_buffer (GstD3D11Converter * self, GstBuffer * buffer,
    GstMapInfo info[GST_VIDEO_MAX_PLANES], GstMapFlags flags)
{
  GstMapFlags map_flags;
  guint num_mapped = 0;

  map_flags = (GstMapFlags) (flags | GST_MAP_D3D11);

  for (num_mapped = 0; num_mapped < gst_buffer_n_memory (buffer); num_mapped++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, num_mapped);

    if (!gst_memory_map (mem, &info[num_mapped], map_flags)) {
      GST_WARNING_OBJECT (self, "Failed to map memory at %d", num_mapped);
      goto error;
    }
  }

  return TRUE;

error:
  for (guint i = 0; i < num_mapped; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);
    gst_memory_unmap (mem, &info[i]);
  }

  return FALSE;
}

static void
gst_d3d11_converter_unmap_buffer (GstD3D11Converter * self, GstBuffer * buffer,
    GstMapInfo info[GST_VIDEO_MAX_PLANES])
{
  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    gst_memory_unmap (mem, &info[i]);
  }
}

static guint
gst_d3d11_converter_get_srv (GstD3D11Converter * self, GstBuffer * buffer,
    ID3D11ShaderResourceView * srv[GST_VIDEO_MAX_PLANES])
{
  guint num_views = 0;

  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint num_view_in_mem;

    num_view_in_mem = gst_d3d11_memory_get_shader_resource_view_size (mem);
    if (!num_view_in_mem)
      return 0;

    for (guint j = 0; j < num_view_in_mem; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR_OBJECT (self, "Too many SRV");
        return 0;
      }

      srv[num_views] = gst_d3d11_memory_get_shader_resource_view (mem, j);
      num_views++;
    }
  }

  return num_views;
}

static guint
gst_d3d11_converter_get_rtv (GstD3D11Converter * self, GstBuffer * buffer,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  guint num_views = 0;

  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint num_view_in_mem;

    num_view_in_mem = gst_d3d11_memory_get_render_target_view_size (mem);
    if (!num_view_in_mem)
      return 0;

    for (guint j = 0; j < num_view_in_mem; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR_OBJECT (self, "Too many SRV");
        return 0;
      }

      rtv[num_views] = gst_d3d11_memory_get_render_target_view (mem, j);
      num_views++;
    }
  }

  return num_views;
}

static gboolean
gst_d3d11_converter_ensure_fallback_inbuf (GstD3D11Converter * self,
    GstBuffer * in_buf, GstMapInfo in_info[GST_VIDEO_MAX_PLANES])
{
  GstD3D11ConverterPrivate *priv = self->priv;
  D3D11_TEXTURE2D_DESC desc[GST_VIDEO_MAX_PLANES];
  gboolean same_size = TRUE;
  ID3D11DeviceContext *context;

  for (guint i = 0; i < gst_buffer_n_memory (in_buf); i++) {
    GstD3D11Memory *in_mem =
        (GstD3D11Memory *) gst_buffer_peek_memory (in_buf, i);

    gst_d3d11_memory_get_texture_desc (in_mem, &desc[i]);

    if (same_size && priv->fallback_inbuf) {
      D3D11_TEXTURE2D_DESC prev_desc;
      GstD3D11Memory *prev_mem =
          (GstD3D11Memory *) gst_buffer_peek_memory (priv->fallback_inbuf, i);

      gst_d3d11_memory_get_texture_desc (prev_mem, &prev_desc);

      if (prev_desc.Width != desc[i].Width ||
          prev_desc.Height != desc[i].Height) {
        same_size = FALSE;
      }
    }
  }

  priv->fallback_info.width = desc[0].Width;
  priv->fallback_info.height = desc[0].Height;

  if (priv->fallback_inbuf && !same_size) {
    GST_DEBUG_OBJECT (self,
        "Size of new buffer is different from previous fallback");
    gst_clear_buffer (&priv->fallback_inbuf);
  }

  if (!priv->fallback_inbuf &&
      !gst_d3d11_converter_create_fallback_buffer (self)) {
    return FALSE;
  }

  context = gst_d3d11_device_get_device_context_handle (self->device);
  for (guint i = 0; i < gst_buffer_n_memory (in_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (priv->fallback_inbuf, i);
    GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
    GstMapInfo info;
    ID3D11Resource *src_tex = (ID3D11Resource *) in_info[i].data;
    guint src_subresource = GPOINTER_TO_UINT (in_info[i].user_data[0]);
    ID3D11Resource *fallback_tex;
    D3D11_TEXTURE2D_DESC fallback_desc;
    D3D11_BOX src_box = { 0, };

    if (!gst_memory_map (mem, &info, (GstMapFlags)
            (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Couldn't map fallback memory");
    }

    fallback_tex = (ID3D11Resource *) info.data;
    gst_d3d11_memory_get_texture_desc (dmem, &fallback_desc);

    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (fallback_desc.Width, desc[i].Width);
    src_box.bottom = MIN (fallback_desc.Height, desc[i].Height);

    context->CopySubresourceRegion (fallback_tex, 0, 0, 0, 0,
        src_tex, src_subresource, &src_box);
    gst_memory_unmap (mem, &info);
  }

  return TRUE;
}

static void
gst_d3d11_converter_fill_hdr10_meta (const GstVideoMasteringDisplayInfo * mdcv,
    const GstVideoContentLightLevel * cll, DXGI_HDR_METADATA_HDR10 * meta)
{
  meta->RedPrimary[0] = mdcv->display_primaries[0].x;
  meta->RedPrimary[1] = mdcv->display_primaries[0].y;
  meta->GreenPrimary[0] = mdcv->display_primaries[1].x;
  meta->GreenPrimary[1] = mdcv->display_primaries[1].y;
  meta->BluePrimary[0] = mdcv->display_primaries[2].x;
  meta->BluePrimary[1] = mdcv->display_primaries[2].y;
  meta->WhitePoint[0] = mdcv->white_point.x;
  meta->WhitePoint[1] = mdcv->white_point.y;
  meta->MaxMasteringLuminance = mdcv->max_display_mastering_luminance;
  meta->MinMasteringLuminance = mdcv->min_display_mastering_luminance;

  meta->MaxContentLightLevel = cll->max_content_light_level;
  meta->MaxFrameAverageLightLevel = cll->max_frame_average_light_level;
}

/* called with prop lock */
static void
gst_d3d11_converter_update_hdr10_meta (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;

  if (priv->in_hdr10_updated) {
    if (!priv->in_mdcv_str || !priv->in_cll_str) {
      priv->have_in_hdr10 = FALSE;
    } else {
      GstVideoMasteringDisplayInfo mdcv;
      GstVideoContentLightLevel cll;

      if (gst_video_mastering_display_info_from_string (&mdcv,
              priv->in_mdcv_str) &&
          gst_video_content_light_level_from_string (&cll, priv->in_cll_str)) {
        gst_d3d11_converter_fill_hdr10_meta (&mdcv, &cll, &priv->in_hdr10_meta);
        priv->have_in_hdr10 = TRUE;
      } else {
        priv->have_in_hdr10 = FALSE;
      }
    }

    priv->in_hdr10_updated = FALSE;
  }

  if (priv->out_hdr10_updated) {
    if (!priv->in_mdcv_str || !priv->in_cll_str) {
      priv->have_out_hdr10 = FALSE;
    } else {
      GstVideoMasteringDisplayInfo mdcv;
      GstVideoContentLightLevel cll;

      if (gst_video_mastering_display_info_from_string (&mdcv,
              priv->in_mdcv_str) &&
          gst_video_content_light_level_from_string (&cll, priv->in_cll_str)) {
        gst_d3d11_converter_fill_hdr10_meta (&mdcv, &cll, &priv->in_hdr10_meta);
        priv->have_out_hdr10 = TRUE;
      } else {
        priv->have_out_hdr10 = FALSE;
      }
    }

    priv->out_hdr10_updated = FALSE;
  }
}

static gboolean
gst_d3d11_converter_need_blend (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;

  if (priv->blend && priv->blend_desc.RenderTarget[0].BlendEnable) {
    if (priv->alpha != 1.0) {
      return TRUE;
    } else if ((priv->blend_desc.RenderTarget[0].SrcBlend ==
            D3D11_BLEND_BLEND_FACTOR
            || priv->blend_desc.RenderTarget[0].SrcBlend ==
            D3D11_BLEND_INV_BLEND_FACTOR)
        && (priv->blend_factor[0] != 1.0 || priv->blend_factor[1] != 1.0
            || priv->blend_factor[2] != 1.0 || priv->blend_factor[3] != 1.0)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_d3d11_converter_processor_available (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;

  if ((priv->supported_backend &
          GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR) == 0)
    return FALSE;

  /* TODO: processor may be able to blend textures */
  if (gst_d3d11_converter_need_blend (self))
    return FALSE;

  /* flip/rotate is not supported by processor */
  if (priv->processor_direction_not_supported)
    return FALSE;

  return TRUE;
}

static gboolean
gst_d3d11_converter_piv_available (GstD3D11Converter * self, GstBuffer * in_buf)
{
  GstD3D11Memory *mem;
  D3D11_TEXTURE2D_DESC desc;

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (in_buf, 0);
  gst_d3d11_memory_get_texture_desc (mem, &desc);
  return gst_d3d11_converter_check_bind_flags_for_piv (desc.BindFlags);
}

static gboolean
gst_d3d11_converter_create_piv_buffer (GstD3D11Converter * self)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstD3D11AllocationParams *params;
  GstBufferPool *pool;
  GstCaps *caps;
  GstStructure *config;

  gst_clear_buffer (&priv->piv_inbuf);

  params = gst_d3d11_allocation_params_new (self->device, &priv->piv_info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT, 0, 0);

  caps = gst_video_info_to_caps (&priv->piv_info);
  pool = gst_d3d11_buffer_pool_new (self->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, priv->piv_info.size, 0, 0);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_caps_unref (caps);
  gst_d3d11_allocation_params_free (params);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to set active");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_buffer_pool_acquire_buffer (pool, &priv->piv_inbuf, nullptr);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  if (!priv->piv_inbuf) {
    GST_ERROR_OBJECT (self, "Failed to create PIV buffer");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_converter_upload_for_processor (GstD3D11Converter * self,
    GstBuffer * in_buf)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  GstVideoFrame frame, fallback_frame;
  GstVideoInfo *piv_info = &priv->piv_info;
  gboolean ret = TRUE;

  if (!gst_video_frame_map (&frame, &priv->in_info, in_buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return FALSE;
  }

  /* Probably cropped buffer */
  if (piv_info->width != GST_VIDEO_FRAME_WIDTH (&frame) ||
      piv_info->height != GST_VIDEO_FRAME_HEIGHT (&frame)) {
    gst_clear_buffer (&priv->piv_inbuf);

    *piv_info = frame.info;
  }

  if (!priv->piv_inbuf && !gst_d3d11_converter_create_piv_buffer (self)) {
    goto error;
  }

  if (!gst_video_frame_map (&fallback_frame,
          &priv->piv_info, priv->piv_inbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map fallback buffer");
    goto error;
  }

  ret = gst_video_frame_copy (&fallback_frame, &frame);
  gst_video_frame_unmap (&fallback_frame);
  gst_video_frame_unmap (&frame);

  return ret;

error:
  gst_video_frame_unmap (&frame);
  return FALSE;
}

static gboolean
gst_d3d11_converter_do_processor_blt (GstD3D11Converter * self,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  ID3D11VideoProcessorInputView *piv = nullptr;
  ID3D11VideoProcessorOutputView *pov = nullptr;
  ID3D11VideoContext1 *video_ctx = priv->video_context;
  ID3D11VideoProcessor *proc = priv->processor;
  D3D11_VIDEO_PROCESSOR_STREAM stream = { 0, };
  HRESULT hr;
  GstMemory *in_mem, *out_mem;
  GstD3D11Memory *in_dmem;
  GstD3D11Memory *out_dmem;
  GstMapInfo in_info, out_info;
  gboolean ret = FALSE;

  g_assert (gst_buffer_n_memory (in_buf) == 1);
  g_assert (gst_buffer_n_memory (out_buf) == 1);

  in_mem = gst_buffer_peek_memory (in_buf, 0);
  out_mem = gst_buffer_peek_memory (out_buf, 0);

  if (!gst_memory_map (in_mem, &in_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return FALSE;
  }

  if (!gst_memory_map (out_mem, &out_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    gst_memory_unmap (in_mem, &in_info);
    return FALSE;
  }

  in_dmem = GST_D3D11_MEMORY_CAST (in_mem);
  out_dmem = GST_D3D11_MEMORY_CAST (out_mem);

  piv = gst_d3d11_memory_get_processor_input_view (in_dmem,
      priv->video_device, priv->enumerator);
  if (!piv) {
    GST_ERROR_OBJECT (self, "PIV is unavailable");
    goto out;
  }

  pov = gst_d3d11_memory_get_processor_output_view (out_dmem,
      priv->video_device, priv->enumerator);
  if (!pov) {
    GST_ERROR_OBJECT (self, "POV is unavailable");
    goto out;
  }

  video_ctx->VideoProcessorSetStreamSourceRect (proc, 0, TRUE, &priv->src_rect);
  video_ctx->VideoProcessorSetStreamDestRect (proc, 0, TRUE, &priv->dest_rect);

  if (priv->clear_background) {
    video_ctx->VideoProcessorSetOutputTargetRect (proc,
        TRUE, &priv->dest_full_rect);
    video_ctx->VideoProcessorSetOutputBackgroundColor (proc,
        GST_VIDEO_INFO_IS_YUV (&priv->out_info), &priv->background_color);
  } else {
    video_ctx->VideoProcessorSetOutputTargetRect (proc, TRUE, &priv->dest_rect);
  }

  if (priv->video_context2 &&
      (priv->processor_caps.FeatureCaps & FEATURE_CAPS_METADATA_HDR10) != 0) {
    if (priv->have_in_hdr10) {
      priv->video_context2->VideoProcessorSetStreamHDRMetaData (proc, 0,
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof (DXGI_HDR_METADATA_HDR10),
          &priv->in_hdr10_meta);
    } else {
      priv->video_context2->VideoProcessorSetStreamHDRMetaData (proc, 0,
          DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
    }

    if (priv->have_out_hdr10) {
      priv->video_context2->VideoProcessorSetOutputHDRMetaData (proc,
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof (DXGI_HDR_METADATA_HDR10),
          &priv->in_hdr10_meta);
    }
  }

  if ((priv->processor_caps.FeatureCaps & FEATURE_CAPS_ROTATION) != 0) {
    video_ctx->VideoProcessorSetStreamRotation (proc, 0,
        priv->enable_rotation, priv->rotation);
  }

  if ((priv->processor_caps.FeatureCaps & PROCESSOR_FEATURE_CAPS_MIRROR) != 0) {
    video_ctx->VideoProcessorSetStreamMirror (proc, 0, priv->enable_mirror,
        priv->flip_h, priv->flip_v);
  }

  stream.Enable = TRUE;
  stream.pInputSurface = piv;

  GST_TRACE_OBJECT (self, "Converting using processor");

  hr = video_ctx->VideoProcessorBlt (proc, pov, 0, 1, &stream);
  ret = gst_d3d11_result (hr, self->device);

  priv->processor_in_use = ret;

out:
  gst_memory_unmap (out_mem, &out_info);
  gst_memory_unmap (in_mem, &in_info);

  return ret;
}

static gboolean
gst_d3d11_converter_convert_buffer_internal (GstD3D11Converter * self,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstD3D11ConverterPrivate *priv = self->priv;
  ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES] = { nullptr, };
  ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES] = { nullptr, };
  GstD3D11Memory *in_dmem;
  GstD3D11Memory *out_dmem;
  GstMapInfo in_info[GST_VIDEO_MAX_PLANES];
  GstMapInfo out_info[GST_VIDEO_MAX_PLANES];
  D3D11_TEXTURE2D_DESC desc;
  guint num_srv, num_rtv;
  gboolean ret = FALSE;
  gboolean in_d3d11;

  GstD3D11SRWLockGuard (&priv->prop_lock);

  /* Output buffer must be valid D3D11 buffer */
  if (!gst_d3d11_converter_is_d3d11_buffer (self, out_buf)) {
    GST_ERROR_OBJECT (self, "Output is not d3d11 buffer");
    return FALSE;
  }

  if (gst_buffer_n_memory (in_buf) == 0) {
    GST_ERROR_OBJECT (self, "Empty input buffer");
    return FALSE;
  }

  out_dmem = (GstD3D11Memory *) gst_buffer_peek_memory (out_buf, 0);
  if (!gst_d3d11_memory_get_texture_desc (out_dmem, &desc)) {
    GST_ERROR_OBJECT (self, "Failed to get output desc");
    return FALSE;
  }

  if ((desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
    GST_ERROR_OBJECT (self, "Output is not bound to render target");
    return FALSE;
  }

  gst_d3d11_converter_update_hdr10_meta (self);
  /* Update in/out rect */
  if (!gst_d3d11_converter_update_dest_rect (self)) {
    GST_ERROR_OBJECT (self, "Failed to update dest rect");
    return FALSE;
  }

  if (!gst_d3d11_converter_update_src_rect (self)) {
    GST_ERROR_OBJECT (self, "Failed to update src rect");
    return FALSE;
  }

  in_d3d11 = gst_d3d11_converter_is_d3d11_buffer (self, in_buf);
  if (gst_d3d11_converter_processor_available (self)) {
    gboolean use_processor = FALSE;
    gboolean piv_available = FALSE;

    if (in_d3d11)
      piv_available = gst_d3d11_converter_piv_available (self, in_buf);

    if ((priv->supported_backend & GST_D3D11_CONVERTER_BACKEND_SHADER) == 0) {
      /* processor only */
      use_processor = TRUE;
    } else if ((priv->src_alpha_mode ==
            GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED ||
            priv->dst_alpha_mode ==
            GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED)
        && GST_VIDEO_INFO_HAS_ALPHA (&priv->in_info)) {
      /* Needs alpha conversion */
      use_processor = FALSE;
    } else if (piv_available) {
      in_dmem = (GstD3D11Memory *) gst_buffer_peek_memory (in_buf, 0);

      if (GST_VIDEO_INFO_FORMAT (&priv->in_info) == GST_VIDEO_FORMAT_YUY2) {
        /* Always use processor for packed YUV */
        use_processor = TRUE;
      } else if (!gst_d3d11_memory_get_shader_resource_view_size (in_dmem)) {
        /* SRV is unavailable, use processor */
        use_processor = TRUE;
      } else if (priv->video_context2 &&
          (priv->have_in_hdr10 || priv->have_out_hdr10)) {
        /* HDR10 tonemap is needed */
        use_processor = TRUE;
      } else if (priv->processor_in_use) {
        use_processor = TRUE;
      }
    }

    if (use_processor) {
      if (!piv_available) {
        if (!gst_d3d11_converter_upload_for_processor (self, in_buf)) {
          GST_ERROR_OBJECT (self, "Couldn't upload buffer");
          return FALSE;
        }

        in_buf = priv->piv_inbuf;
      }

      return gst_d3d11_converter_do_processor_blt (self, in_buf, out_buf);
    }
  }

  if ((priv->supported_backend & GST_D3D11_CONVERTER_BACKEND_SHADER) == 0) {
    GST_ERROR_OBJECT (self, "Conversion is not supported");
    goto out;
  }

  if (!in_d3d11 ||
      GST_VIDEO_INFO_FORMAT (&priv->in_info) == GST_VIDEO_FORMAT_YUY2) {
    if (!gst_d3d11_converter_upload_for_shader (self, in_buf)) {
      GST_ERROR_OBJECT (self, "Couldn't copy into fallback buffer");
      return FALSE;
    }

    in_buf = priv->fallback_inbuf;
  }

  if (!gst_d3d11_converter_map_buffer (self, in_buf, in_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return FALSE;
  }

  if (!gst_d3d11_converter_map_buffer (self, out_buf, out_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    gst_d3d11_converter_unmap_buffer (self, in_buf, in_info);
    return FALSE;
  }

  num_rtv = gst_d3d11_converter_get_rtv (self, out_buf, rtv);
  if (!num_rtv) {
    GST_ERROR_OBJECT (self, "RTV is unavailable");
    goto out;
  }

  num_srv = gst_d3d11_converter_get_srv (self, in_buf, srv);
  if (!num_srv) {
    if (in_buf == priv->fallback_inbuf) {
      GST_ERROR_OBJECT (self, "Unable to get SRV from fallback buffer");
      goto out;
    } else if (!gst_d3d11_converter_ensure_fallback_inbuf (self,
            in_buf, in_info)) {
      GST_ERROR_OBJECT (self, "Couldn't copy into fallback texture");
      goto out;
    }

    gst_d3d11_converter_unmap_buffer (self, in_buf, in_info);
    in_buf = priv->fallback_inbuf;

    if (!gst_d3d11_converter_map_buffer (self,
            in_buf, in_info, (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Couldn't map fallback buffer");
      in_buf = nullptr;
      goto out;
    }

    num_srv = gst_d3d11_converter_get_srv (self, in_buf, srv);
    if (!num_srv) {
      GST_ERROR_OBJECT (self, "Couldn't get SRV from fallback input");
      goto out;
    }
  }

  GST_TRACE_OBJECT (self, "Converting using shader");

  ret = gst_d3d11_converter_convert_internal (self, srv, rtv);

out:
  if (in_buf)
    gst_d3d11_converter_unmap_buffer (self, in_buf, in_info);
  gst_d3d11_converter_unmap_buffer (self, out_buf, out_info);

  return ret;
}

/**
 * gst_d3d11_converter_convert_buffer:
 * @converter: a #GstD3D11Converter
 * @in_buf: a #GstBuffer
 * @out_buf: a #GstBuffer
 *
 * Converts @in_buf into @out_buf
 *
 * Returns: %TRUE if conversion is successful
 *
 * Since: 1.22
 */
gboolean
gst_d3d11_converter_convert_buffer (GstD3D11Converter * converter,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  g_return_val_if_fail (GST_IS_D3D11_CONVERTER (converter), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (in_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (out_buf), FALSE);

  GstD3D11DeviceLockGuard lk (converter->device);

  return gst_d3d11_converter_convert_buffer_internal (converter,
      in_buf, out_buf);
}

/**
 * gst_d3d11_converter_convert_buffer_unlocked:
 * @converter: a #GstD3D11Converter
 * @in_buf: a #GstBuffer
 * @out_buf: a #GstBuffer
 *
 * Converts @in_buf into @out_buf. Caller should take d3d11 device lock
 * in case that multiple threads can perform GPU processing using the
 * same #GstD3D11Device
 *
 * Returns: %TRUE if conversion is successful
 *
 * Since: 1.22
 */

gboolean
gst_d3d11_converter_convert_buffer_unlocked (GstD3D11Converter * converter,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  g_return_val_if_fail (GST_IS_D3D11_CONVERTER (converter), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (in_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (out_buf), FALSE);

  return gst_d3d11_converter_convert_buffer_internal (converter,
      in_buf, out_buf);
}
