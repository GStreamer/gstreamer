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
Texture2D<float4> inTex : register(t0);
RWTexture2D<unorm float4> outTex : register(u0);

#ifdef BUILDING_CSMain_YUY2_to_AYUV
void Execute (uint3 tid)
{
  float4 val = inTex.Load (tid);
  float Y0 = val.r;
  float U = val.g;
  float Y1 = val.b;
  float V = val.a;

  outTex[uint2(tid.x * 2, tid.y)] = float4 (1.0, Y0, U, V);
  outTex[uint2(tid.x * 2 + 1, tid.y)] = float4 (1.0, Y1, U, V);
}
#endif

#ifdef BUILDING_CSMain_AYUV_to_YUY2
void Execute (uint3 tid)
{
  float3 val = inTex.Load (uint3(tid.x * 2, tid.y, 0)).yzw;
  float Y0 = val.x;
  float U = val.y;
  float V = val.z;
  float Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).y;

  outTex[tid.xy] = float4 (Y0, U, Y1, V);
}
#endif

#ifdef BUILDING_CSMain_AYUV_to_Y410
void Execute (uint3 tid)
{
  float4 val = inTex.Load (tid);
  float Y = val.y;
  float U = val.z;
  float V = val.w;
  float A = val.x;

  outTex[tid.xy] = float4 (U, Y, V, A);
}
#endif

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid);
}
#else
static const char g_CSMain_converter_str[] =
"Texture2D<float4> inTex : register(t0);\n"
"RWTexture2D<unorm float4> outTex : register(u0);\n"
"\n"
"#ifdef BUILDING_CSMain_YUY2_to_AYUV\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (tid);\n"
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
"#ifdef BUILDING_CSMain_AYUV_to_YUY2\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float3 val = inTex.Load (uint3(tid.x * 2, tid.y, 0)).yzw;\n"
"  float Y0 = val.x;\n"
"  float U = val.y;\n"
"  float V = val.z;\n"
"  float Y1 = inTex.Load (uint3(tid.x * 2 + 1, tid.y, 0)).y;\n"
"\n"
"  outTex[tid.xy] = float4 (Y0, U, Y1, V);\n"
"}\n"
"#endif\n"
"\n"
"#ifdef BUILDING_CSMain_AYUV_to_Y410\n"
"void Execute (uint3 tid)\n"
"{\n"
"  float4 val = inTex.Load (tid);\n"
"  float Y = val.y;\n"
"  float U = val.z;\n"
"  float V = val.w;\n"
"  float A = val.x;\n"
"\n"
"  outTex[tid.xy] = float4 (U, Y, V, A);\n"
"}\n"
"#endif\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid);\n"
"}\n";
#endif
