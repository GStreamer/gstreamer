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
  uint src_offset;
  uint dst_offset;
  uint padding0;
  uint padding1;
};

ByteAddressBuffer src : register(t0);
RWByteAddressBuffer dst : register(u0);

uint
LoadUnaligned4 (uint address)
{
  uint aligned_address = address & ~3u;
  uint shift = (address & 3u) * 8u;

  uint lo = src.Load (aligned_address);

  if (shift == 0)
    return lo;

  uint hi = src.Load (aligned_address + 4);

  return (lo >> shift) | (hi << (32u - shift));
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

  uint src_row = src_offset + pos.y * src_stride;
  uint dst_row = dst_offset + pos.y * dst_stride;

  uint dst_begin = dst_row;
  uint dst_end = dst_row + width_in_bytes;

  uint dst_word = (dst_row & ~3u) + pos.x * 4u;

  uint copy_begin = max (dst_word, dst_begin);
  uint copy_end = min (dst_word + 4u, dst_end);

  if (copy_begin >= copy_end)
    return;

  uint copy_size = copy_end - copy_begin;
  uint src_address = src_row + (copy_begin - dst_row);

  uint value = LoadUnaligned4 (src_address);

  uint dst_byte_offset = copy_begin - dst_word;
  uint dst_shift = dst_byte_offset * 8u;

  uint mask;
  if (copy_size == 4) {
    mask = 0xffffffffu;
  } else {
    mask = ((1u << (copy_size * 8u)) - 1u) << dst_shift;
    value <<= dst_shift;
  }

  StoreMasked (dst_word, value, mask);
}

[numthreads(64, 1, 1)]
void
ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (tid.xy);
}
#else
static const char str_CSMain_buffer_copy_2d_unaligned[] =
"cbuffer CopyBuffer2DData : register(b0)\n"
"{\n"
"  uint src_stride;\n"
"  uint dst_stride;\n"
"  uint width_in_bytes;\n"
"  uint height;\n"
"  uint src_offset;\n"
"  uint dst_offset;\n"
"  uint padding0;\n"
"  uint padding1;\n"
"};\n"
"\n"
"ByteAddressBuffer src : register(t0);\n"
"RWByteAddressBuffer dst : register(u0);\n"
"\n"
"uint\n"
"LoadUnaligned4 (uint address)\n"
"{\n"
"  uint aligned_address = address & ~3u;\n"
"  uint shift = (address & 3u) * 8u;\n"
"\n"
"  uint lo = src.Load (aligned_address);\n"
"\n"
"  if (shift == 0)\n"
"    return lo;\n"
"\n"
"  uint hi = src.Load (aligned_address + 4);\n"
"\n"
"  return (lo >> shift) | (hi << (32u - shift));\n"
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
"\n"
"  while (1) {\n"
"    uint new_value = (old_value & ~mask) | (value & mask);\n"
"    uint original;\n"
"\n"
"    dst.InterlockedCompareExchange (\n"
"        address, old_value, new_value, original);\n"
"\n"
"    if (original == old_value)\n"
"      break;\n"
"\n"
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
"  uint src_row = src_offset + pos.y * src_stride;\n"
"  uint dst_row = dst_offset + pos.y * dst_stride;\n"
"\n"
"  uint dst_begin = dst_row;\n"
"  uint dst_end = dst_row + width_in_bytes;\n"
"\n"
"  uint dst_word = (dst_row & ~3u) + pos.x * 4u;\n"
"\n"
"  uint copy_begin = max (dst_word, dst_begin);\n"
"  uint copy_end = min (dst_word + 4u, dst_end);\n"
"\n"
"  if (copy_begin >= copy_end)\n"
"    return;\n"
"\n"
"  uint copy_size = copy_end - copy_begin;\n"
"  uint src_address = src_row + (copy_begin - dst_row);\n"
"\n"
"  uint value = LoadUnaligned4 (src_address);\n"
"\n"
"  uint dst_byte_offset = copy_begin - dst_word;\n"
"  uint dst_shift = dst_byte_offset * 8u;\n"
"\n"
"  uint mask;\n"
"  if (copy_size == 4) {\n"
"    mask = 0xffffffffu;\n"
"  } else {\n"
"    mask = ((1u << (copy_size * 8u)) - 1u) << dst_shift;\n"
"    value <<= dst_shift;\n"
"  }\n"
"\n"
"  StoreMasked (dst_word, value, mask);\n"
"}\n"
"\n"
"[numthreads(64, 1, 1)]\n"
"void\n"
"ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (tid.xy);\n"
"}\n"
"\n";
#endif
