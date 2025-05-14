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

#if defined(__NVCC__) || defined(__HIPCC__)
#ifdef __HIPCC__
#include <hip/hip_runtime.h>
#endif

extern "C" {
__global__ void
GstHipConverterUnpack_RGB_RGBx
(unsigned char *src, unsigned char *dst, int width, int height,
    int src_stride, int dst_stride)
{
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;
  if (x_pos < width && y_pos < height) {
    int dst_pos = x_pos * 4 + y_pos * dst_stride;
    int src_pos = x_pos * 3 + y_pos * src_stride;
    dst[dst_pos] = src[src_pos];
    dst[dst_pos + 1] = src[src_pos + 1];
    dst[dst_pos + 2] = src[src_pos + 2];
    dst[dst_pos + 3] = 0xff;
  }
}

__global__ void
GstHipConverterUnpack_RGB10A2_ARGB64
(unsigned char *src, unsigned char *dst, int width, int height,
    int src_stride, int dst_stride)
{
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;
  if (x_pos < width && y_pos < height) {
    unsigned short a, r, g, b;
    unsigned int val;
    int dst_pos = x_pos * 8 + y_pos * dst_stride;
    val = *(unsigned int *)&src[x_pos * 4 + y_pos * src_stride];
    a = (val >> 30) & 0x03;
    a = (a << 14) | (a << 12) | (a << 10) | (a << 8) | (a << 6) | (a << 4) | (a << 2) | (a << 0);
    r = (val & 0x3ff);
    r = (r << 6) | (r >> 4);
    g = ((val >> 10) & 0x3ff);
    g = (g << 6) | (g >> 4);
    b = ((val >> 20) & 0x3ff);
    b = (b << 6) | (b >> 4);
    *(unsigned short *) &dst[dst_pos] = a;
    *(unsigned short *) &dst[dst_pos + 2] = r;
    *(unsigned short *) &dst[dst_pos + 4] = g;
    *(unsigned short *) &dst[dst_pos + 6] = b;
  }
}

__global__ void
GstHipConverterUnpack_BGR10A2_ARGB64
(unsigned char *src, unsigned char *dst, int width, int height,
    int src_stride, int dst_stride)
{
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;
  if (x_pos < width && y_pos < height) {
    unsigned short a, r, g, b;
    unsigned int val;
    int dst_pos = x_pos * 8 + y_pos * dst_stride;
    val = *(unsigned int *)&src[x_pos * 4 + y_pos * src_stride];
    a = (val >> 30) & 0x03;
    a = (a << 14) | (a << 12) | (a << 10) | (a << 8) | (a << 6) | (a << 4) | (a << 2) | (a << 0);
    b = (val & 0x3ff);
    b = (b << 6) | (b >> 4);
    g = ((val >> 10) & 0x3ff);
    g = (g << 6) | (g >> 4);
    r = ((val >> 20) & 0x3ff);
    r = (r << 6) | (r >> 4);
    *(unsigned short *) &dst[dst_pos] = a;
    *(unsigned short *) &dst[dst_pos + 2] = r;
    *(unsigned short *) &dst[dst_pos + 4] = g;
    *(unsigned short *) &dst[dst_pos + 6] = b;
  }
}
}
#else
static const char ConverterUnpack_str[] =
"extern \"C\" {\n"
"__global__ void\n"
"GstHipConverterUnpack_RGB_RGBx\n"
"(unsigned char *src, unsigned char *dst, int width, int height,\n"
"    int src_stride, int dst_stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    int dst_pos = x_pos * 4 + y_pos * dst_stride;\n"
"    int src_pos = x_pos * 3 + y_pos * src_stride;\n"
"    dst[dst_pos] = src[src_pos];\n"
"    dst[dst_pos + 1] = src[src_pos + 1];\n"
"    dst[dst_pos + 2] = src[src_pos + 2];\n"
"    dst[dst_pos + 3] = 0xff;\n"
"  }\n"
"}\n"
"\n"
"__global__ void\n"
"GstHipConverterUnpack_RGB10A2_ARGB64\n"
"(unsigned char *src, unsigned char *dst, int width, int height,\n"
"    int src_stride, int dst_stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    unsigned short a, r, g, b;\n"
"    unsigned int val;\n"
"    int dst_pos = x_pos * 8 + y_pos * dst_stride;\n"
"    val = *(unsigned int *)&src[x_pos * 4 + y_pos * src_stride];\n"
"    a = (val >> 30) & 0x03;\n"
"    a = (a << 14) | (a << 12) | (a << 10) | (a << 8) | (a << 6) | (a << 4) | (a << 2) | (a << 0);\n"
"    r = (val & 0x3ff);\n"
"    r = (r << 6) | (r >> 4);\n"
"    g = ((val >> 10) & 0x3ff);\n"
"    g = (g << 6) | (g >> 4);\n"
"    b = ((val >> 20) & 0x3ff);\n"
"    b = (b << 6) | (b >> 4);\n"
"    *(unsigned short *) &dst[dst_pos] = a;\n"
"    *(unsigned short *) &dst[dst_pos + 2] = r;\n"
"    *(unsigned short *) &dst[dst_pos + 4] = g;\n"
"    *(unsigned short *) &dst[dst_pos + 6] = b;\n"
"  }\n"
"}\n"
"\n"
"__global__ void\n"
"GstHipConverterUnpack_BGR10A2_ARGB64\n"
"(unsigned char *src, unsigned char *dst, int width, int height,\n"
"    int src_stride, int dst_stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    unsigned short a, r, g, b;\n"
"    unsigned int val;\n"
"    int dst_pos = x_pos * 8 + y_pos * dst_stride;\n"
"    val = *(unsigned int *)&src[x_pos * 4 + y_pos * src_stride];\n"
"    a = (val >> 30) & 0x03;\n"
"    a = (a << 14) | (a << 12) | (a << 10) | (a << 8) | (a << 6) | (a << 4) | (a << 2) | (a << 0);\n"
"    b = (val & 0x3ff);\n"
"    b = (b << 6) | (b >> 4);\n"
"    g = ((val >> 10) & 0x3ff);\n"
"    g = (g << 6) | (g >> 4);\n"
"    r = ((val >> 20) & 0x3ff);\n"
"    r = (r << 6) | (r >> 4);\n"
"    *(unsigned short *) &dst[dst_pos] = a;\n"
"    *(unsigned short *) &dst[dst_pos + 2] = r;\n"
"    *(unsigned short *) &dst[dst_pos + 4] = g;\n"
"    *(unsigned short *) &dst[dst_pos + 6] = b;\n"
"  }\n"
"}\n"
"}\n"
"\n";
#endif