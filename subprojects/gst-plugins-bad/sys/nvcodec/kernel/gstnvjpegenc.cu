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

#ifdef __NVCC__
__device__ inline unsigned char
scale_to_uchar (float val)
{
  return (unsigned char) __float2int_rz (val * 255.0);
}

extern "C" {
__global__ void
GstNvJpegEncConvertMain (cudaTextureObject_t uv_tex, unsigned char * out_u,
    unsigned char * out_v, int width, int height, int stride)
{
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;
  if (x_pos >= width || y_pos >= height)
    return;
  float x = 0;
  float y = 0;
  if (width > 1)
    x = (float) x_pos / (width - 1);
  if (height > 1)
    y = (float) y_pos / (height - 1);
  float2 uv = tex2D<float2> (uv_tex, x, y);
  unsigned int pos = x_pos + (y_pos * stride);
  out_u[pos] = scale_to_uchar (uv.x);
  out_v[pos] = scale_to_uchar (uv.y);
}
}
#else
static const gchar *GstNvJpegEncConvertMain_str = R"(
__device__ inline unsigned char
scale_to_uchar (float val)
{
  return (unsigned char) __float2int_rz (val * 255.0);
}

extern "C" {
__global__ void
GstNvJpegEncConvertMain (cudaTextureObject_t uv_tex, unsigned char * out_u,
    unsigned char * out_v, int width, int height, int stride)
{
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;
  if (x_pos >= width || y_pos >= height)
    return;
  float x = 0;
  float y = 0;
  if (width > 1)
    x = (float) x_pos / (width - 1);
  if (height > 1)
    y = (float) y_pos / (height - 1);
  float2 uv = tex2D<float2> (uv_tex, x, y);
  unsigned int pos = x_pos + (y_pos * stride);
  out_u[pos] = scale_to_uchar (uv.x);
  out_v[pos] = scale_to_uchar (uv.y);
}
}
)";
#endif
