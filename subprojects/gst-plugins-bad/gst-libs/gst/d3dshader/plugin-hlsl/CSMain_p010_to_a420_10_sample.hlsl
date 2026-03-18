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

Texture2D<float> MainY : register(t0);
Texture2D<float2> MainUV : register(t1);
Texture2D<float> AlphaY : register(t2);

RWTexture2D<float> DstY : register(u0);
RWTexture2D<float> DstU : register(u1);
RWTexture2D<float> DstV : register(u2);
RWTexture2D<float> DstA : register(u3);

SamplerState samplerState : register(s0);

void Execute(uint2 pos)
{
  if (pos.x >= DstSize.x || pos.y >= DstSize.y)
      return;

  const float alpha_max = 1023.0f / 65535.0f;
  float2 main_uv = ((float2 (pos) + 0.5f) / float2 (DstSize)) *
                   (float2 (MainViewSize) / float2 (MainTexSize));

  DstY[pos] = MainY.SampleLevel (samplerState, main_uv, 0.0f).r / 64.0f;

  if (FillAlpha) {
    DstA[pos] = alpha_max;
  } else {
    float2 alpha_uv = ((float2 (pos) + 0.5f) / float2 (DstSize)) *
                      (float2 (AlphaViewSize) / float2 (AlphaTexSize));
    DstA[pos] = AlphaY.SampleLevel (samplerState, alpha_uv, 0.0f).r / 64.0f;
  }

  uint2 chromaSize = DstSize >> 1;
  if (pos.x < chromaSize.x && pos.y < chromaSize.y) {
    float2 chroma_uv = ((float2(pos) + 0.5f) / float2(chromaSize)) *
                       (float2(MainViewSize) / float2(MainTexSize));

    float2 uv = MainUV.SampleLevel(samplerState, chroma_uv, 0.0f).rg / 64.0f;
    DstU[pos] = uv.x;
    DstV[pos] = uv.y;
  }
}

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (uint2(tid.x, tid.y));
}
#else
static const char str_CSMain_p010_to_a420_10_sample[] =
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
"Texture2D<float> MainY : register(t0);\n"
"Texture2D<float2> MainUV : register(t1);\n"
"Texture2D<float> AlphaY : register(t2);\n"
"\n"
"RWTexture2D<float> DstY : register(u0);\n"
"RWTexture2D<float> DstU : register(u1);\n"
"RWTexture2D<float> DstV : register(u2);\n"
"RWTexture2D<float> DstA : register(u3);\n"
"\n"
"SamplerState samplerState : register(s0);\n"
"\n"
"void Execute(uint2 pos)\n"
"{\n"
"  if (pos.x >= DstSize.x || pos.y >= DstSize.y)\n"
"      return;\n"
"\n"
"  const float alpha_max = 1023.0f / 65535.0f;\n"
"  float2 main_uv = ((float2 (pos) + 0.5f) / float2 (DstSize)) *\n"
"                   (float2 (MainViewSize) / float2 (MainTexSize));\n"
"\n"
"  DstY[pos] = MainY.SampleLevel (samplerState, main_uv, 0.0f).r / 64.0f;\n"
"\n"
"  if (FillAlpha) {\n"
"    DstA[pos] = alpha_max;\n"
"  } else {\n"
"    float2 alpha_uv = ((float2 (pos) + 0.5f) / float2 (DstSize)) *\n"
"                      (float2 (AlphaViewSize) / float2 (AlphaTexSize));\n"
"    DstA[pos] = AlphaY.SampleLevel (samplerState, alpha_uv, 0.0f).r / 64.0f;\n"
"  }\n"
"\n"
"  uint2 chromaSize = DstSize >> 1;\n"
"  if (pos.x < chromaSize.x && pos.y < chromaSize.y) {\n"
"    float2 chroma_uv = ((float2(pos) + 0.5f) / float2(chromaSize)) *\n"
"                       (float2(MainViewSize) / float2(MainTexSize));\n"
"\n"
"    float2 uv = MainUV.SampleLevel(samplerState, chroma_uv, 0.0f).rg / 64.0f;\n"
"    DstU[pos] = uv.x;\n"
"    DstV[pos] = uv.y;\n"
"  }\n"
"}\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (uint2(tid.x, tid.y));\n"
"}\n";
#endif
