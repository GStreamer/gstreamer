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
cbuffer CopyBuffer2DData : register(b0)
{
  uint src_stride;
  uint dst_stride;
  uint width_in_bytes;
  uint height;
};

ByteAddressBuffer src : register(t0);
RWByteAddressBuffer dst : register(u0);

void Execute (uint2 pos)
{
  if (pos.y >= height)
    return;

  uint byte_offset = pos.x * 4;
  if (byte_offset >= width_in_bytes)
    return;

  uint src_offset = pos.y * src_stride + byte_offset;
  uint dst_offset = pos.y * dst_stride + byte_offset;

  uint value = src.Load (src_offset);

  uint remaining = width_in_bytes - byte_offset;

  if (remaining >= 4) {
    dst.Store (dst_offset, value);
  } else {
    uint old_value = dst.Load (dst_offset);
    uint mask = (1u << (remaining * 8)) - 1;
    dst.Store (dst_offset, (old_value & ~mask) | (value & mask));
  }
}

[numthreads(64, 1, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid.xy);
}
#else
static const char str_CSMain_buffer_copy_2d[] =
"cbuffer CopyBuffer2DData : register(b0)\n"
"{\n"
"  uint src_stride;\n"
"  uint dst_stride;\n"
"  uint width_in_bytes;\n"
"  uint height;\n"
"};\n"
"\n"
"ByteAddressBuffer src : register(t0);\n"
"RWByteAddressBuffer dst : register(u0);\n"
"\n"
"void Execute (uint2 pos)\n"
"{\n"
"  if (pos.y >= height)\n"
"    return;\n"
"\n"
"  uint byte_offset = pos.x * 4;\n"
"  if (byte_offset >= width_in_bytes)\n"
"    return;\n"
"\n"
"  uint src_offset = pos.y * src_stride + byte_offset;\n"
"  uint dst_offset = pos.y * dst_stride + byte_offset;\n"
"\n"
"  uint value = src.Load (src_offset);\n"
"\n"
"  uint remaining = width_in_bytes - byte_offset;\n"
"\n"
"  if (remaining >= 4) {\n"
"    dst.Store (dst_offset, value);\n"
"  } else {\n"
"    uint old_value = dst.Load (dst_offset);\n"
"    uint mask = (1u << (remaining * 8)) - 1;\n"
"    dst.Store (dst_offset, (old_value & ~mask) | (value & mask));\n"
"  }\n"
"}\n"
"\n"
"[numthreads(64, 1, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid.xy);\n"
"}\n"
"\n";
#endif
