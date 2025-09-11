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
cbuffer WeaveCBData : register(b0)
{
  uint Width;
  uint Height;
  uint Mode;
  uint FieldOrder; // 0 = tff, 1 = bff
};

Texture2D<float4> srcTexA : register(t0);
Texture2D<float4> srcTexB : register(t1);
RWTexture2D<unorm float4> outTex : register(u0);

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  uint x = tid.x;
  uint y = tid.y;

  if (x >= Width || y >= Height)
    return;

  bool is_top = ((y & 1u) == 0u);
  if (FieldOrder == 1)
    is_top = !is_top;

  float4 val;
  if (is_top)
    val = srcTexA.Load (uint3 (x, y, 0));
  else
    val = srcTexB.Load (uint3 (x, y, 0));

  outTex[uint2(x, y)] = val;
}
#else
static const char str_CSMain_weave_interlace_4[] =
"cbuffer WeaveCBData : register(b0)\n"
"{\n"
"  uint Width;\n"
"  uint Height;\n"
"  uint Mode;\n"
"  uint FieldOrder; // 0 = tff, 1 = bff\n"
"};\n"
"\n"
"Texture2D<float4> srcTexA : register(t0);\n"
"Texture2D<float4> srcTexB : register(t1);\n"
"RWTexture2D<unorm float4> outTex : register(u0);\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  uint x = tid.x;\n"
"  uint y = tid.y;\n"
"\n"
"  if (x >= Width || y >= Height)\n"
"    return;\n"
"\n"
"  bool is_top = ((y & 1u) == 0u);\n"
"  if (FieldOrder == 1)\n"
"    is_top = !is_top;\n"
"\n"
"  float4 val;\n"
"  if (is_top)\n"
"    val = srcTexA.Load (uint3 (x, y, 0));\n"
"  else\n"
"    val = srcTexB.Load (uint3 (x, y, 0));\n"
"\n"
"  outTex[uint2(x, y)] = val;\n"
"}\n";
#endif
