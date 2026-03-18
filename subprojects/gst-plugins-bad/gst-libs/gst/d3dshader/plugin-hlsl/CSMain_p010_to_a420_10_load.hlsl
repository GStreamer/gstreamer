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

void Execute(uint2 pos)
{
  if (pos.x >= DstSize.x || pos.y >= DstSize.y)
      return;

  const float alpha_max = 1023.0f / 65535.0f;

  DstY[pos] = MainY.Load (int3 (pos, 0)).r / 64.0f;

  if (FillAlpha) {
    DstA[pos] = alpha_max;
  } else {
    DstA[pos] = AlphaY.Load (int3(pos, 0)).r / 64.0f;
  }

  uint2 chromaSize = DstSize >> 1;
  if (pos.x < chromaSize.x && pos.y < chromaSize.y) {
    float2 uv = MainUV.Load (int3 (pos, 0)).rg / 64.0f;
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
static const char str_CSMain_p010_to_a420_10_load[] =
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
"void Execute(uint2 pos)\n"
"{\n"
"  if (pos.x >= DstSize.x || pos.y >= DstSize.y)\n"
"      return;\n"
"\n"
"  const float alpha_max = 1023.0f / 65535.0f;\n"
"\n"
"  DstY[pos] = MainY.Load (int3 (pos, 0)).r / 64.0f;\n"
"\n"
"  if (FillAlpha) {\n"
"    DstA[pos] = alpha_max;\n"
"  } else {\n"
"    DstA[pos] = AlphaY.Load (int3(pos, 0)).r / 64.0f;\n"
"  }\n"
"\n"
"  uint2 chromaSize = DstSize >> 1;\n"
"  if (pos.x < chromaSize.x && pos.y < chromaSize.y) {\n"
"    float2 uv = MainUV.Load (int3 (pos, 0)).rg / 64.0f;\n"
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
