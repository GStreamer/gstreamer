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

#ifdef BUILDING_HLSL
cbuffer CopyParams : register(b0)
{
  uint2 AlphaTexSize;
  uint2 AlphaViewSize;
  uint2 DstSize;

  uint2 MainTexSize;
  uint2 MainViewSize;
  uint FillAlpha;
  uint Padding;
};

Texture2D<float> AlphaTex : register(t0);
RWTexture2D<float> DstTex : register(u0);
SamplerState samplerState : register(s0);

void Execute(uint2 dstPos)
{
  if (dstPos.x >= DstSize.x || dstPos.y >= DstSize.y)
      return;

  float2 uv = ((float2 (dstPos) + 0.5f) / float2 (DstSize)) *
              (float2 (AlphaViewSize) / float2 (AlphaTexSize));

  DstTex[dstPos] = AlphaTex.SampleLevel(samplerState, uv, 0.0f).r;
}

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (uint2(tid.x, tid.y));
}
#else
static const char str_CSMain_nv12_to_r8_sample[] =
"cbuffer CopyParams : register(b0)\n"
"{\n"
"  uint2 AlphaTexSize;\n"
"  uint2 AlphaViewSize;\n"
"  uint2 DstSize;\n"
"\n"
"  uint2 MainTexSize;\n"
"  uint2 MainViewSize;\n"
"  uint FillAlpha;\n"
"  uint Padding;\n"
"};\n"
"\n"
"Texture2D<float> AlphaTex : register(t0);\n"
"RWTexture2D<float> DstTex : register(u0);\n"
"SamplerState samplerState : register(s0);\n"
"\n"
"void Execute(uint2 dstPos)\n"
"{\n"
"  if (dstPos.x >= DstSize.x || dstPos.y >= DstSize.y)\n"
"      return;\n"
"\n"
"  float2 uv = ((float2 (dstPos) + 0.5f) / float2 (DstSize)) *\n"
"              (float2 (AlphaViewSize) / float2 (AlphaTexSize));\n"
"\n"
"  DstTex[dstPos] = AlphaTex.SampleLevel(samplerState, uv, 0.0f).r;\n"
"}\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (uint2(tid.x, tid.y));\n"
"}\n";
#endif
