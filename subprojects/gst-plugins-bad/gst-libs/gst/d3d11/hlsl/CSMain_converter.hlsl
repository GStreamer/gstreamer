/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#ifdef BUILDING_CSMain_AYUV64_to_Y410
Texture2D<float4> inTex : register(t0);
RWTexture2D<uint> outTex : register(u0);

void Execute (uint3 tid)
{
  float4 val = inTex.Load (tid);
  uint3 scaled = val.yzw * 1023;
  outTex[tid.xy] = (0xc0 << 24) | (scaled.z << 20) | (scaled.x << 10) | scaled.y;
}
#endif

#ifdef BUILDING_CSMain_VUYA_to_YUY2
Texture2D<uint4> inTex : register(t0);
RWTexture2D<uint> outTex : register(u0);

void Execute (uint3 tid)
{
  uint4 val = inTex.Load (uint3(tid.x * 2, tid.y, 0));
  uint Y0 = val.b;
  uint U = val.g;
  uint V = val.r;
  uint Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).b;

  outTex[tid.xy] = Y0 | (U << 8) | (Y1 << 16) | (V << 24);
}
#endif

#ifdef BUILDING_CSMain_YUY2_to_VUYA
Texture2D<uint4> inTex : register(t0);
RWTexture2D<uint> outTex : register(u0);

void Execute (uint3 tid)
{
  uint4 val = inTex.Load (tid);
  uint Y0 = val.r;
  uint U = val.g;
  uint Y1 = val.b;
  uint V = val.a;

  outTex[uint2(tid.x * 2, tid.y)] = V | (U << 8) | (Y0 << 16) | (0xff << 24);
  outTex[uint2(tid.x * 2 + 1, tid.y)] = V | (U << 8) | (Y1 << 16) | (0xff << 24);
}
#endif

#ifdef BUILDING_CSMain_AYUV64_to_Y210
Texture2D<float4> inTex : register(t0);
RWTexture2D<uint4> outTex : register(u0);

void Execute (uint3 tid)
{
  float4 val = inTex.Load (uint3(tid.x * 2, tid.y, 0));
  uint3 scaled = val.yzw * 65535;
  uint Y0 = scaled.x;
  uint U = scaled.y;
  uint V = scaled.z;
  uint Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).y * 65535;
  outTex[tid.xy] = uint4(Y0, U, Y1, V);
}
#endif

#ifdef BUILDING_CSMain_Y210_to_AYUV64
Texture2D<uint4> inTex : register(t0);
RWTexture2D<unorm float4> outTex : register(u0);

void Execute (uint3 tid)
{
  float4 val = inTex.Load (tid) / 65535.0;
  float Y0 = val.r;
  float U = val.g;
  float Y1 = val.b;
  float V = val.a;

  outTex[uint2(tid.x * 2, tid.y)] = float4 (1.0, Y0, U, V);
  outTex[uint2(tid.x * 2 + 1, tid.y)] = float4 (1.0, Y1, U, V);
}
#endif

#ifdef BUILDING_CSMain_AYUV64_to_Y412
Texture2D<float4> inTex : register(t0);
RWTexture2D<uint4> outTex : register(u0);

void Execute (uint3 tid)
{
  float4 val = inTex.Load (tid);
  outTex[tid.xy] = uint4(val.zywx * 65535);
}
#endif

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid);
}
#else
static const char g_CSMain_converter_str[] =
"#ifdef BUILDING_CSMain_AYUV64_to_Y410\n"
"Texture2D<float4> inTex : register(t0);\n"
"RWTexture2D<uint> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (tid);\n"
"  uint3 scaled = val.yzw * 1023;\n"
"  outTex[tid.xy] = (0xc0 << 24) | (scaled.z << 20) | (scaled.x << 10) | scaled.y;\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_VUYA_to_YUY2\n"
"Texture2D<uint4> inTex : register(t0);\n"
"RWTexture2D<uint> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  uint4 val = inTex.Load (uint3(tid.x * 2, tid.y, 0));\n"
"  uint Y0 = val.b;\n"
"  uint U = val.g;\n"
"  uint V = val.r;\n"
"  uint Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).b;\n"
"\n"
"  outTex[tid.xy] = Y0 | (U << 8) | (Y1 << 16) | (V << 24);\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_YUY2_to_VUYA\n"
"Texture2D<uint4> inTex : register(t0);\n"
"RWTexture2D<uint> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  uint4 val = inTex.Load (tid);\n"
"  uint Y0 = val.r;\n"
"  uint U = val.g;\n"
"  uint Y1 = val.b;\n"
"  uint V = val.a;\n"
"\n"
"  outTex[uint2(tid.x * 2, tid.y)] = V | (U << 8) | (Y0 << 16) | (0xff << 24);\n"
"  outTex[uint2(tid.x * 2 + 1, tid.y)] = V | (U << 8) | (Y1 << 16) | (0xff << 24);\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_AYUV64_to_Y210\n"
"Texture2D<float4> inTex : register(t0);\n"
"RWTexture2D<uint4> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (uint3(tid.x * 2, tid.y, 0));\n"
"  uint3 scaled = val.yzw * 65535;\n"
"  uint Y0 = scaled.x;\n"
"  uint U = scaled.y;\n"
"  uint V = scaled.z;\n"
"  uint Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).y * 65535;\n"
"  outTex[tid.xy] = uint4(Y0, U, Y1, V);\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_Y210_to_AYUV64\n"
"Texture2D<uint4> inTex : register(t0);\n"
"RWTexture2D<unorm float4> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (tid) / 65535.0;\n"
"  float Y0 = val.r;\n"
"  float U = val.g;\n"
"  float Y1 = val.b;\n"
"  float V = val.a;\n"
"\n"
"  outTex[uint2(tid.x * 2, tid.y)] = float4 (1.0, Y0, U, V);\n"
"  outTex[uint2(tid.x * 2 + 1, tid.y)] = float4 (1.0, Y1, U, V);\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_AYUV64_to_Y412\n"
"Texture2D<float4> inTex : register(t0);\n"
"RWTexture2D<uint4> outTex : register(u0);\n"
"\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (tid);\n"
"  outTex[tid.xy] = uint4(val.zywx * 65535);\n"
"}\n"
"#endif\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid);\n"
"}\n";
#endif
