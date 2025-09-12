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

#ifdef BUILDING_HLSL
Texture2D shaderTexture;
SamplerState samplerState;

struct PS_INPUT
{
  float4 Position : SV_POSITION;
  float2 Texture : TEXCOORD;
};

static const float3x3 RGB2YCbCr = {
    0.1826,   0.6142,   0.0620,   // Y
   -0.1006,  -0.3386,   0.4392,   // Cb
    0.4392,  -0.3989,  -0.0403    // Cr
};

static const float3 Offset = float3 (0.0625, 0.5, 0.5);

float4 ENTRY_POINT (PS_INPUT input): SV_TARGET
{
  float4 bgra = shaderTexture.Sample (samplerState, input.Texture);
  float3 rgb = float3(bgra.r, bgra.g, bgra.b) * bgra.a;
  float3 yuv = mul (RGB2YCbCr, rgb) + Offset;

  return float4 (yuv.z, yuv.y, yuv.x, bgra.a);
}
#else
static const char str_PSMain_sample_bgra_to_vuya_limited[] =
"Texture2D shaderTexture;\n"
"SamplerState samplerState;\n"
"\n"
"struct PS_INPUT\n"
"{\n"
"  float4 Position : SV_POSITION;\n"
"  float2 Texture : TEXCOORD;\n"
"};\n"
"\n"
"static const float3x3 RGB2YCbCr = {\n"
"    0.1826,   0.6142,   0.0620,   // Y\n"
"   -0.1006,  -0.3386,   0.4392,   // Cb\n"
"    0.4392,  -0.3989,  -0.0403    // Cr\n"
"};\n"
"\n"
"static const float3 Offset = float3 (0.0625, 0.5, 0.5);\n"
"\n"
"float4 ENTRY_POINT (PS_INPUT input): SV_TARGET\n"
"{\n"
"  float4 bgra = shaderTexture.Sample (samplerState, input.Texture);\n"
"  float3 rgb = float3(bgra.r, bgra.g, bgra.b) * bgra.a;\n"
"  float3 yuv = mul (RGB2YCbCr, rgb) + Offset;\n"
"\n"
"  return float4 (yuv.z, yuv.y, yuv.x, bgra.a);\n"
"}\n";
#endif
