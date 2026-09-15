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
cbuffer TextureToBufferData : register(b0)
{
  uint width;
  uint height;
  uint dst_stride;
  uint dst_offset;
  uint src_x;
  uint src_y;
  float scale;
  float offset;
  float scale1;
  float offset1;
  float scale2;
  float offset2;
  uint channels;
  uint src_stride;
  uint src_offset;
  uint padding;
};

ByteAddressBuffer src : register(t0);
RWByteAddressBuffer dst : register(u0);

float
Normalize (float value, uint channel)
{
  float s = channel == 0 ? scale : (channel == 1 ? scale1 : scale2);
  float o = channel == 0 ? offset : (channel == 1 ? offset1 : offset2);
  return value * s + o;
}

void
Execute (uint2 pos)
{
  if (pos.x >= width * channels || pos.y >= height)
    return;

  float value = asfloat (src.Load (src_offset + pos.y * src_stride +
          pos.x * 4u));
  value = Normalize (value, pos.x % channels);
  dst.Store (dst_offset + pos.y * dst_stride + pos.x * 4u,
      asuint (value));
}

[numthreads(64, 1, 1)]
void
ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid.xy);
}
#else
static const char str_CSMain_buffer_scale_2d_f32[] =
"cbuffer TextureToBufferData : register(b0)\n"
"{\n"
"  uint width;\n"
"  uint height;\n"
"  uint dst_stride;\n"
"  uint dst_offset;\n"
"  uint src_x;\n"
"  uint src_y;\n"
"  float scale;\n"
"  float offset;\n"
"  float scale1;\n"
"  float offset1;\n"
"  float scale2;\n"
"  float offset2;\n"
"  uint channels;\n"
"  uint src_stride;\n"
"  uint src_offset;\n"
"  uint padding;\n"
"};\n"
"\n"
"ByteAddressBuffer src : register(t0);\n"
"RWByteAddressBuffer dst : register(u0);\n"
"\n"
"float\n"
"Normalize (float value, uint channel)\n"
"{\n"
"  float s = channel == 0 ? scale : (channel == 1 ? scale1 : scale2);\n"
"  float o = channel == 0 ? offset : (channel == 1 ? offset1 : offset2);\n"
"  return value * s + o;\n"
"}\n"
"\n"
"void\n"
"Execute (uint2 pos)\n"
"{\n"
"  if (pos.x >= width * channels || pos.y >= height)\n"
"    return;\n"
"\n"
"  float value = asfloat (src.Load (src_offset + pos.y * src_stride +\n"
"          pos.x * 4u));\n"
"  value = Normalize (value, pos.x % channels);\n"
"  dst.Store (dst_offset + pos.y * dst_stride + pos.x * 4u,\n"
"      asuint (value));\n"
"}\n"
"\n"
"[numthreads(64, 1, 1)]\n"
"void\n"
"ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid.xy);\n"
"}\n";
#endif
