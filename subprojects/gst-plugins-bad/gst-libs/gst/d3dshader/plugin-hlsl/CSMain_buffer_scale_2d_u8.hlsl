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
StoreMasked (uint address, uint value, uint mask)
{
  if (mask == 0xffffffffu) {
    dst.Store (address, value);
    return;
  }

  uint old_value = dst.Load (address);
  while (1) {
    uint new_value = (old_value & ~mask) | (value & mask);
    uint original;
    dst.InterlockedCompareExchange (
        address, old_value, new_value, original);
    if (original == old_value)
      break;
    old_value = original;
  }
}

void
Execute (uint2 pos)
{
  if (pos.y >= height)
    return;

  uint row = dst_offset + pos.y * dst_stride;
  uint word = (row & ~3u) + pos.x * 4u;
  uint begin = max (word, row);
  uint end = min (word + 4u, row + width * channels);
  if (begin >= end)
    return;

  uint value = 0;
  uint mask = 0;
  for (uint address = begin; address < end; address++) {
    uint x = address - row;
    uint src_address = src_offset + pos.y * src_stride + x;
    uint sample_value = (src.Load (src_address & ~3u) >>
        ((src_address & 3u) * 8u)) & 0xffu;
    float v = clamp (Normalize ((float) sample_value, x % channels),
        0.0, 255.0);
    uint shift = (address - word) * 8u;
    value |= (uint) v << shift;
    mask |= 0xffu << shift;
  }
  StoreMasked (word, value, mask);
}

[numthreads(64, 1, 1)]
void
ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid.xy);
}
#else
static const char str_CSMain_buffer_scale_2d_u8[] =
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
"StoreMasked (uint address, uint value, uint mask)\n"
"{\n"
"  if (mask == 0xffffffffu) {\n"
"    dst.Store (address, value);\n"
"    return;\n"
"  }\n"
"\n"
"  uint old_value = dst.Load (address);\n"
"  while (1) {\n"
"    uint new_value = (old_value & ~mask) | (value & mask);\n"
"    uint original;\n"
"    dst.InterlockedCompareExchange (\n"
"        address, old_value, new_value, original);\n"
"    if (original == old_value)\n"
"      break;\n"
"    old_value = original;\n"
"  }\n"
"}\n"
"\n"
"void\n"
"Execute (uint2 pos)\n"
"{\n"
"  if (pos.y >= height)\n"
"    return;\n"
"\n"
"  uint row = dst_offset + pos.y * dst_stride;\n"
"  uint word = (row & ~3u) + pos.x * 4u;\n"
"  uint begin = max (word, row);\n"
"  uint end = min (word + 4u, row + width * channels);\n"
"  if (begin >= end)\n"
"    return;\n"
"\n"
"  uint value = 0;\n"
"  uint mask = 0;\n"
"  for (uint address = begin; address < end; address++) {\n"
"    uint x = address - row;\n"
"    uint src_address = src_offset + pos.y * src_stride + x;\n"
"    uint sample_value = (src.Load (src_address & ~3u) >>\n"
"        ((src_address & 3u) * 8u)) & 0xffu;\n"
"    float v = clamp (Normalize ((float) sample_value, x % channels),\n"
"        0.0, 255.0);\n"
"    uint shift = (address - word) * 8u;\n"
"    value |= (uint) v << shift;\n"
"    mask |= 0xffu << shift;\n"
"  }\n"
"  StoreMasked (word, value, mask);\n"
"}\n"
"\n"
"[numthreads(64, 1, 1)]\n"
"void\n"
"ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid.xy);\n"
"}\n";
#endif
