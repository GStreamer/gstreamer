/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

/**
 * SECTION:cudaconverter
 * @title: GstCudaConverter
 * @short_description: Generic video conversion using CUDA
 *
 * This object is used to convert video frames from one format to another.
 * The object can perform conversion of:
 *
 *  * video format
 *  * video colorspace
 *  * video size
 */

/**
 * TODO:
 *  * Add more interpolation method and make it selectable,
 *    currently default bi-linear interpolation only
 *  * Add fast-path for conversion like videoconvert
 *  * Full colorimetry and chroma-siting support
 *  * cropping, and x, y position support
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cuda-converter.h"
#include "gstcudautils.h"
#include "gstcudaloader.h"
#include "gstcudanvrtc.h"
#include <string.h>

#define CUDA_BLOCK_X 16
#define CUDA_BLOCK_Y 16
#define DIV_UP(size,block) (((size) + ((block) - 1)) / (block))

static gboolean cuda_converter_lookup_path (GstCudaConverter * convert);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("cuda-converter", 0,
        "cuda-converter object");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category()
#endif

#define GST_CUDA_KERNEL_FUNC "gst_cuda_kernel_func"

#define GST_CUDA_KERNEL_FUNC_TO_Y444 "gst_cuda_kernel_func_to_y444"

#define GST_CUDA_KERNEL_FUNC_Y444_TO_YUV "gst_cuda_kernel_func_y444_to_yuv"

#define GST_CUDA_KERNEL_FUNC_TO_ARGB "gst_cuda_kernel_func_to_argb"

#define GST_CUDA_KERNEL_FUNC_SCALE_RGB "gst_cuda_kernel_func_scale_rgb"

/* *INDENT-OFF* */
/**
 * read_chroma:
 * @tex1: a CUDA texture object representing a semi-planar chroma plane
 * @tex2: dummy object
 * @x: the x coordinate to read data from @tex1
 * @y: the y coordinate to read data from @tex1
 *
 * Returns: a #ushort2 vector representing both chroma pixel values
 */
static const gchar READ_CHROMA_FROM_SEMI_PLANAR[] =
"__device__ ushort2\n"
"read_chroma (cudaTextureObject_t tex1, cudaTextureObject_t tex2, \n"
"    float x, float y)\n"
"{\n"
"  return tex2D<ushort2>(tex1, x, y);\n"
"}";

/**
 * read_chroma:
 * @tex1: a CUDA texture object representing a chroma planar plane
 * @tex2: a CUDA texture object representing the other planar plane
 * @x: the x coordinate to read data from @tex1 and @tex2
 * @y: the y coordinate to read data from @tex1 and @tex2
 *
 * Returns: a #ushort2 vector representing both chroma pixel values
 */
static const gchar READ_CHROMA_FROM_PLANAR[] =
"__device__ ushort2\n"
"read_chroma (cudaTextureObject_t tex1, cudaTextureObject_t tex2, \n"
"    float x, float y)\n"
"{\n"
"  unsigned short u, v;\n"
"  u = tex2D<unsigned short>(tex1, x, y);\n"
"  v = tex2D<unsigned short>(tex2, x, y);\n"
"  return make_ushort2(u, v);\n"
"}";

/**
 * write_chroma:
 * @dst1: a CUDA global memory pointing to a semi-planar chroma plane
 * @dst2: dummy
 * @u: a pixel value to write @dst1
 * @v: a pixel value to write @dst1
 * @x: the x coordinate to write data into @tex1
 * @x: the y coordinate to write data into @tex1
 * @pstride: the pixel stride of @dst1
 * @mask: bitmask to be applied to high bitdepth plane
 *
 * Write @u and @v pixel value to @dst1 semi-planar plane
 */
static const gchar WRITE_CHROMA_TO_SEMI_PLANAR[] =
"__device__ void\n"
"write_chroma (unsigned char *dst1, unsigned char *dst2, unsigned short u,\n"
"    unsigned short v, int x, int y, int pstride, int stride, int mask)\n"
"{\n"
"  if (OUT_DEPTH > 8) {\n"
"    *(unsigned short *)&dst1[x * pstride + y * stride] = (u & mask);\n"
"    *(unsigned short *)&dst1[x * pstride + 2 + y * stride] = (v & mask);\n"
"  } else {\n"
"    dst1[x * pstride + y * stride] = u;\n"
"    dst1[x * pstride + 1 + y * stride] = v;\n"
"  }\n"
"}";

/**
 * write_chroma:
 * @dst1: a CUDA global memory pointing to a planar chroma plane
 * @dst2: a CUDA global memory pointing to a the other planar chroma plane
 * @u: a pixel value to write @dst1
 * @v: a pixel value to write @dst1
 * @x: the x coordinate to write data into @tex1
 * @x: the y coordinate to write data into @tex1
 * @pstride: the pixel stride of @dst1
 * @mask: bitmask to be applied to high bitdepth plane
 *
 * Write @u and @v pixel value into @dst1 and @dst2 planar planes
 */
static const gchar WRITE_CHROMA_TO_PLANAR[] =
"__device__ void\n"
"write_chroma (unsigned char *dst1, unsigned char *dst2, unsigned short u,\n"
"    unsigned short v, int x, int y, int pstride, int stride, int mask)\n"
"{\n"
"  if (OUT_DEPTH > 8) {\n"
"    *(unsigned short *)&dst1[x * pstride + y * stride] = (u & mask);\n"
"    *(unsigned short *)&dst2[x * pstride + y * stride] = (v & mask);\n"
"  } else {\n"
"    dst1[x * pstride + y * stride] = u;\n"
"    dst2[x * pstride + y * stride] = v;\n"
"  }\n"
"}";

/* CUDA kernel source for from YUV to YUV conversion and scale */
static const gchar templ_YUV_TO_YUV[] =
"extern \"C\"{\n"
"__constant__ float SCALE_H = %s;\n"
"__constant__ float SCALE_V = %s;\n"
"__constant__ float CHROMA_SCALE_H = %s;\n"
"__constant__ float CHROMA_SCALE_V = %s;\n"
"__constant__ int WIDTH = %d;\n"
"__constant__ int HEIGHT = %d;\n"
"__constant__ int CHROMA_WIDTH = %d;\n"
"__constant__ int CHROMA_HEIGHT = %d;\n"
"__constant__ int IN_DEPTH = %d;\n"
"__constant__ int OUT_DEPTH = %d;\n"
"__constant__ int PSTRIDE = %d;\n"
"__constant__ int CHROMA_PSTRIDE = %d;\n"
"__constant__ int IN_SHIFT = %d;\n"
"__constant__ int OUT_SHIFT = %d;\n"
"__constant__ int MASK = %d;\n"
"__constant__ int SWAP_UV = %d;\n"
"\n"
"__device__ unsigned short\n"
"do_scale_pixel (unsigned short val) \n"
"{\n"
"  unsigned int diff;\n"
"  if (OUT_DEPTH > IN_DEPTH) {\n"
"    diff = OUT_DEPTH - IN_DEPTH;\n"
"    return (val << diff) | (val >> (IN_DEPTH - diff));\n"
"  } else if (IN_DEPTH > OUT_DEPTH) {\n"
"    return val >> (IN_DEPTH - OUT_DEPTH);\n"
"  }\n"
"  return val;\n"
"}\n"
"\n"
/* __device__ ushort2
 * read_chroma (cudaTextureObject_t tex1, cudaTextureObject_t tex2, float x, float y);
 */
"%s\n"
"\n"
/* __device__ void
 * write_chroma (unsigned char *dst1, unsigned char *dst2, unsigned short u,
 *     unsigned short v, int x, int y, int pstride, int stride, int mask);
 */
"%s\n"
"\n"
"__global__ void\n"
GST_CUDA_KERNEL_FUNC
"(cudaTextureObject_t tex0, cudaTextureObject_t tex1, cudaTextureObject_t tex2,\n"
"    unsigned char *dst0, unsigned char *dst1, unsigned char *dst2,\n"
"    int stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < WIDTH && y_pos < HEIGHT) {\n"
"    float src_xpos = SCALE_H * x_pos;\n"
"    float src_ypos = SCALE_V * y_pos;\n"
"    unsigned short y = tex2D<unsigned short>(tex0, src_xpos, src_ypos);\n"
"    y = y >> IN_SHIFT;\n"
"    y = do_scale_pixel (y);\n"
"    y = y << OUT_SHIFT;\n"
"    if (OUT_DEPTH > 8) {\n"
"      *(unsigned short *)&dst0[x_pos * PSTRIDE + y_pos * stride] = (y & MASK);\n"
"    } else {\n"
"      dst0[x_pos * PSTRIDE + y_pos * stride] = y;\n"
"    }\n"
"  }\n"
"  if (x_pos < CHROMA_WIDTH && y_pos < CHROMA_HEIGHT) {\n"
"    float src_xpos = CHROMA_SCALE_H * x_pos;\n"
"    float src_ypos = CHROMA_SCALE_V * y_pos;\n"
"    unsigned short u, v;\n"
"    ushort2 uv = read_chroma (tex1, tex2, src_xpos, src_ypos);\n"
"    u = uv.x;\n"
"    v = uv.y;\n"
"    u = u >> IN_SHIFT;\n"
"    v = v >> IN_SHIFT;\n"
"    u = do_scale_pixel (u);\n"
"    v = do_scale_pixel (v);\n"
"    u = u << OUT_SHIFT;\n"
"    v = v << OUT_SHIFT;\n"
"    if (SWAP_UV) {\n"
"      unsigned short tmp = u;\n"
"      u = v;\n"
"      v = tmp;\n"
"    }\n"
"    write_chroma (dst1,\n"
"      dst2, u, v, x_pos, y_pos, CHROMA_PSTRIDE, stride, MASK);\n"
"  }\n"
"}\n"
"\n"
"}";

/* CUDA kernel source for from YUV to RGB conversion and scale */
static const gchar templ_YUV_TO_RGB[] =
"extern \"C\"{\n"
"__constant__ float offset[3] = {%s, %s, %s};\n"
"__constant__ float rcoeff[3] = {%s, %s, %s};\n"
"__constant__ float gcoeff[3] = {%s, %s, %s};\n"
"__constant__ float bcoeff[3] = {%s, %s, %s};\n"
"\n"
"__constant__ float SCALE_H = %s;\n"
"__constant__ float SCALE_V = %s;\n"
"__constant__ float CHROMA_SCALE_H = %s;\n"
"__constant__ float CHROMA_SCALE_V = %s;\n"
"__constant__ int WIDTH = %d;\n"
"__constant__ int HEIGHT = %d;\n"
"__constant__ int CHROMA_WIDTH = %d;\n"
"__constant__ int CHROMA_HEIGHT = %d;\n"
"__constant__ int IN_DEPTH = %d;\n"
"__constant__ int OUT_DEPTH = %d;\n"
"__constant__ int PSTRIDE = %d;\n"
"__constant__ int CHROMA_PSTRIDE = %d;\n"
"__constant__ int IN_SHIFT = %d;\n"
"__constant__ int OUT_SHIFT = %d;\n"
"__constant__ int MASK = %d;\n"
"__constant__ int SWAP_UV = %d;\n"
"__constant__ int MAX_IN_VAL = %d;\n"
"__constant__ int R_IDX = %d;\n"
"__constant__ int G_IDX = %d;\n"
"__constant__ int B_IDX = %d;\n"
"__constant__ int A_IDX = %d;\n"
"__constant__ int X_IDX = %d;\n"
"\n"
"__device__ unsigned short\n"
"do_scale_pixel (unsigned short val) \n"
"{\n"
"  unsigned int diff;\n"
"  if (OUT_DEPTH > IN_DEPTH) {\n"
"    diff = OUT_DEPTH - IN_DEPTH;\n"
"    return (val << diff) | (val >> (IN_DEPTH - diff));\n"
"  } else if (IN_DEPTH > OUT_DEPTH) {\n"
"    return val >> (IN_DEPTH - OUT_DEPTH);\n"
"  }\n"
"  return val;\n"
"}\n"
"\n"
"__device__ float\n"
"dot(float3 val, float *coeff)\n"
"{\n"
"  return val.x * coeff[0] + val.y * coeff[1] + val.z * coeff[2];\n"
"}\n"
"\n"
"__device__ uint3\n"
"yuv_to_rgb (unsigned short y, unsigned short u, unsigned short v, unsigned int max_val)\n"
"{\n"
"  float3 yuv = make_float3 (y, u, v);\n"
"  uint3 rgb;\n"
"  rgb.x = max ((unsigned int)(dot (yuv, rcoeff) + offset[0]), 0);\n"
"  rgb.y = max ((unsigned int)(dot (yuv, gcoeff) + offset[1]), 0);\n"
"  rgb.z = max ((unsigned int)(dot (yuv, bcoeff) + offset[2]), 0);\n"
"  rgb.x = min (rgb.x, max_val);\n"
"  rgb.y = min (rgb.y, max_val);\n"
"  rgb.z = min (rgb.z, max_val);\n"
"  return rgb;\n"
"}\n"
"\n"
/* __device__ ushort2
 * read_chroma (cudaTextureObject_t tex1, cudaTextureObject_t tex2, float x, float y);
 */
"%s\n"
"\n"
"__global__ void\n"
GST_CUDA_KERNEL_FUNC
"(cudaTextureObject_t tex0, cudaTextureObject_t tex1, cudaTextureObject_t tex2,\n"
"    unsigned char *dstRGB, int stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < WIDTH && y_pos < HEIGHT) {\n"
"    float src_xpos = SCALE_H * x_pos;\n"
"    float src_ypos = SCALE_V * y_pos;\n"
"    unsigned short y = tex2D<unsigned short>(tex0, src_xpos, src_ypos);\n"
"    ushort2 uv;\n"
"    unsigned short u, v;\n"
"    uint3 rgb;\n"
"    unsigned int clip_max = MAX_IN_VAL;\n"
"    src_xpos = CHROMA_SCALE_H * x_pos;\n"
"    src_ypos = CHROMA_SCALE_V * y_pos;\n"
"    uv = read_chroma (tex1, tex2, src_xpos, src_ypos);\n"
"    u = uv.x;\n"
"    v = uv.y;\n"
"    y = y >> IN_SHIFT;\n"
"    u = u >> IN_SHIFT;\n"
"    v = v >> IN_SHIFT;\n"
"    if (SWAP_UV) {\n"
"      unsigned short tmp = u;\n"
"      u = v;\n"
"      v = tmp;\n"
"    }\n"
     /* conversion matrix is scaled to higher bitdepth between in/out formats */
"    if (OUT_DEPTH > IN_DEPTH) {\n"
"      y = do_scale_pixel (y);\n"
"      u = do_scale_pixel (u);\n"
"      v = do_scale_pixel (v);\n"
"      clip_max = MASK;\n"
"    }"
"    rgb = yuv_to_rgb (y, u, v, clip_max);\n"
"    if (OUT_DEPTH < IN_DEPTH) {\n"
"      rgb.x = do_scale_pixel (rgb.x);\n"
"      rgb.y = do_scale_pixel (rgb.y);\n"
"      rgb.z = do_scale_pixel (rgb.z);\n"
"    }"
"    if (OUT_DEPTH > 8) {\n"
"      unsigned int packed_rgb = 0;\n"
       /* A is always MSB, we support only little endian system */
"      packed_rgb = 0xc000 << 16;\n"
"      packed_rgb |= (rgb.x << (30 - (R_IDX * 10)));\n"
"      packed_rgb |= (rgb.y << (30 - (G_IDX * 10)));\n"
"      packed_rgb |= (rgb.z << (30 - (B_IDX * 10)));\n"
"      *(unsigned int *)&dstRGB[x_pos * PSTRIDE + y_pos * stride] = packed_rgb;\n"
"    } else {\n"
"      dstRGB[x_pos * PSTRIDE + R_IDX + y_pos * stride] = (unsigned char) rgb.x;\n"
"      dstRGB[x_pos * PSTRIDE + G_IDX + y_pos * stride] = (unsigned char) rgb.y;\n"
"      dstRGB[x_pos * PSTRIDE + B_IDX + y_pos * stride] = (unsigned char) rgb.z;\n"
"      if (A_IDX >= 0 || X_IDX >= 0)\n"
"        dstRGB[x_pos * PSTRIDE + A_IDX + y_pos * stride] = 0xff;\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"}";

/**
 * GST_CUDA_KERNEL_FUNC_TO_ARGB:
 * @srcRGB: a CUDA global memory containing a RGB image
 * @dstRGB: a CUDA global memory to store unpacked ARGB image
 * @width: the width of @srcRGB and @dstRGB
 * @height: the height of @srcRGB and @dstRGB
 * @src_stride: the stride of @srcRGB
 * @src_pstride: the pixel stride of @srcRGB
 * @dst_stride: the stride of @dstRGB
 * @r_idx: the index of red component of @srcRGB
 * @g_idx: the index of green component of @srcRGB
 * @b_idx: the index of blue component of @srcRGB
 * @a_idx: the index of alpha component of @srcRGB
 *
 * Unpack a RGB image from @srcRGB and write the unpacked data into @dstRGB
 */
static const gchar unpack_to_ARGB[] =
"__global__ void\n"
GST_CUDA_KERNEL_FUNC_TO_ARGB
"(unsigned char *srcRGB, unsigned char *dstRGB, int width, int height,\n"
"    int src_stride, int src_pstride, int dst_stride,\n"
"    int r_idx, int g_idx, int b_idx, int a_idx)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    if (a_idx >= 0) {\n"
"      dstRGB[x_pos * 4 + y_pos * dst_stride] =\n"
"          srcRGB[x_pos * src_pstride + a_idx + y_pos * src_stride];\n"
"    } else {\n"
"      dstRGB[x_pos * 4 + y_pos * dst_stride] = 0xff;\n"
"    }\n"
"    dstRGB[x_pos * 4 + 1 + y_pos * dst_stride] =\n"
"        srcRGB[x_pos * src_pstride + r_idx + y_pos * src_stride];\n"
"    dstRGB[x_pos * 4 + 2 + y_pos * dst_stride] =\n"
"        srcRGB[x_pos * src_pstride + g_idx + y_pos * src_stride];\n"
"    dstRGB[x_pos * 4 + 3 + y_pos * dst_stride] =\n"
"        srcRGB[x_pos * src_pstride + b_idx + y_pos * src_stride];\n"
"  }\n"
"}\n";

/**
 * GST_CUDA_KERNEL_FUNC_TO_ARGB:
 * @srcRGB: a CUDA global memory containing a RGB image
 * @dstRGB: a CUDA global memory to store unpacked ARGB64 image
 * @width: the width of @srcRGB and @dstRGB
 * @height: the height of @srcRGB and @dstRGB
 * @src_stride: the stride of @srcRGB
 * @src_pstride: the pixel stride of @srcRGB
 * @dst_stride: the stride of @dstRGB
 * @r_idx: the index of red component of @srcRGB
 * @g_idx: the index of green component of @srcRGB
 * @b_idx: the index of blue component of @srcRGB
 * @a_idx: the index of alpha component of @srcRGB
 *
 * Unpack a RGB image from @srcRGB and write the unpacked data into @dstRGB
 */
static const gchar unpack_to_ARGB64[] =
"__global__ void\n"
GST_CUDA_KERNEL_FUNC_TO_ARGB
"(unsigned char *srcRGB, unsigned char *dstRGB, int width, int height,\n"
"    int src_stride, int src_pstride, int dst_stride,\n"
"    int r_idx, int g_idx, int b_idx, int a_idx)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    unsigned short a, r, g, b;\n"
"    unsigned int read_val;\n"
"    read_val = *(unsigned int *)&srcRGB[x_pos * src_pstride + y_pos * src_stride];\n"
"    a = (read_val >> 30) & 0x03;\n"
"    a = (a << 14) | (a << 12) | (a << 10) | (a << 8) | (a << 6) | (a << 4) | (a << 2) | (a << 0);\n"
"    r = ((read_val >> (30 - (r_idx * 10))) & 0x3ff);\n"
"    r = (r << 6) | (r >> 4);\n"
"    g = ((read_val >> (30 - (g_idx * 10))) & 0x3ff);\n"
"    g = (g << 6) | (g >> 4);\n"
"    b = ((read_val >> (30 - (b_idx * 10))) & 0x3ff);\n"
"    b = (b << 6) | (b >> 4);\n"
"    *(unsigned short *)&dstRGB[x_pos * 8 + y_pos * dst_stride] = 0xffff;\n"
"    *(unsigned short *)&dstRGB[x_pos * 8 + 2 + y_pos * dst_stride] = r;\n"
"    *(unsigned short *)&dstRGB[x_pos * 8 + 4 + y_pos * dst_stride] = g;\n"
"    *(unsigned short *)&dstRGB[x_pos * 8 + 6 + y_pos * dst_stride] = b;\n"
"  }\n"
"}\n";

/* CUDA kernel source for from RGB to YUV conversion and scale */
static const gchar templ_RGB_TO_YUV[] =
"extern \"C\"{\n"
"__constant__ float offset[3] = {%s, %s, %s};\n"
"__constant__ float ycoeff[3] = {%s, %s, %s};\n"
"__constant__ float ucoeff[3] = {%s, %s, %s};\n"
"__constant__ float vcoeff[3] = {%s, %s, %s};\n"
"\n"
"__constant__ float SCALE_H = %s;\n"
"__constant__ float SCALE_V = %s;\n"
"__constant__ float CHROMA_SCALE_H = %s;\n"
"__constant__ float CHROMA_SCALE_V = %s;\n"
"__constant__ int WIDTH = %d;\n"
"__constant__ int HEIGHT = %d;\n"
"__constant__ int CHROMA_WIDTH = %d;\n"
"__constant__ int CHROMA_HEIGHT = %d;\n"
"__constant__ int IN_DEPTH = %d;\n"
"__constant__ int OUT_DEPTH = %d;\n"
"__constant__ int PSTRIDE = %d;\n"
"__constant__ int CHROMA_PSTRIDE = %d;\n"
"__constant__ int IN_SHIFT = %d;\n"
"__constant__ int OUT_SHIFT = %d;\n"
"__constant__ int MASK = %d;\n"
"__constant__ int SWAP_UV = %d;\n"
"\n"
"__device__ unsigned short\n"
"do_scale_pixel (unsigned short val) \n"
"{\n"
"  unsigned int diff;\n"
"  if (OUT_DEPTH > IN_DEPTH) {\n"
"    diff = OUT_DEPTH - IN_DEPTH;\n"
"    return (val << diff) | (val >> (IN_DEPTH - diff));\n"
"  } else if (IN_DEPTH > OUT_DEPTH) {\n"
"    return val >> (IN_DEPTH - OUT_DEPTH);\n"
"  }\n"
"  return val;\n"
"}\n"
"\n"
"__device__ float\n"
"dot(float3 val, float *coeff)\n"
"{\n"
"  return val.x * coeff[0] + val.y * coeff[1] + val.z * coeff[2];\n"
"}\n"
"\n"
"__device__ uint3\n"
"rgb_to_yuv (unsigned short r, unsigned short g, unsigned short b,\n"
"    unsigned int max_val)\n"
"{\n"
"  float3 rgb = make_float3 (r, g, b);\n"
"  uint3 yuv;\n"
"  yuv.x = max ((unsigned int)(dot (rgb, ycoeff) + offset[0]), 0);\n"
"  yuv.y = max ((unsigned int)(dot (rgb, ucoeff) + offset[1]), 0);\n"
"  yuv.z = max ((unsigned int)(dot (rgb, vcoeff) + offset[2]), 0);\n"
"  yuv.x = min (yuv.x, max_val);\n"
"  yuv.y = min (yuv.y, max_val);\n"
"  yuv.z = min (yuv.z, max_val);\n"
"  return yuv;\n"
"}\n"
"\n"
/* __global__ void
 * GST_CUDA_KERNEL_FUNC_TO_ARGB
 */
"%s\n"
"\n"
/* __device__ ushort2
 * read_chroma (cudaTextureObject_t tex1, cudaTextureObject_t tex2, float x, float y);
 */
"%s\n"
"\n"
/* __device__ void
 * write_chroma (unsigned char *dst1, unsigned char *dst2, unsigned short u,
 *     unsigned short v, int x, int y, int pstride, int stride, int mask);
 */
"%s\n"
"\n"
"__global__ void\n"
GST_CUDA_KERNEL_FUNC_TO_Y444
"(cudaTextureObject_t srcRGB, unsigned char *dstY, int y_stride,\n"
"    unsigned char *dstU, int u_stride, unsigned char *dstV, int v_stride,\n"
"    int width, int height, int dst_pstride, int in_depth)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < width && y_pos < height) {\n"
"    ushort4 argb = tex2D<ushort4>(srcRGB, x_pos, y_pos);\n"
"    uint3 yuv;\n"
"    yuv = rgb_to_yuv (argb.y, argb.z, argb.w, (1 << in_depth) - 1);\n"
"    if (in_depth > 8) {\n"
"      *(unsigned short *)&dstY[x_pos * dst_pstride + y_pos * y_stride] = yuv.x;\n"
"      *(unsigned short *)&dstU[x_pos * dst_pstride + y_pos * u_stride] = yuv.y;\n"
"      *(unsigned short *)&dstV[x_pos * dst_pstride + y_pos * v_stride] = yuv.z;\n"
"    } else {\n"
"      dstY[x_pos * dst_pstride + y_pos * y_stride] = yuv.x;\n"
"      dstU[x_pos * dst_pstride + y_pos * u_stride] = yuv.y;\n"
"      dstV[x_pos * dst_pstride + y_pos * v_stride] = yuv.z;\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"__global__ void\n"
GST_CUDA_KERNEL_FUNC_Y444_TO_YUV
"(cudaTextureObject_t tex0, cudaTextureObject_t tex1, cudaTextureObject_t tex2,\n"
"    unsigned char *dst0, unsigned char *dst1, unsigned char *dst2,\n"
"    int stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < WIDTH && y_pos < HEIGHT) {\n"
"    float src_xpos = SCALE_H * x_pos;\n"
"    float src_ypos = SCALE_V * y_pos;\n"
"    unsigned short y = tex2D<unsigned short>(tex0, src_xpos, src_ypos);\n"
"    y = y >> IN_SHIFT;\n"
"    y = do_scale_pixel (y);\n"
"    y = y << OUT_SHIFT;\n"
"    if (OUT_DEPTH > 8) {\n"
"      *(unsigned short *)&dst0[x_pos * PSTRIDE + y_pos * stride] = (y & MASK);\n"
"    } else {\n"
"      dst0[x_pos * PSTRIDE + y_pos * stride] = y;\n"
"    }\n"
"  }\n"
"  if (x_pos < CHROMA_WIDTH && y_pos < CHROMA_HEIGHT) {\n"
"    float src_xpos = CHROMA_SCALE_H * x_pos;\n"
"    float src_ypos = CHROMA_SCALE_V * y_pos;\n"
"    unsigned short u, v;\n"
"    ushort2 uv;\n"
"    uv = read_chroma (tex1, tex2, src_xpos, src_ypos);\n"
"    u = uv.x;\n"
"    v = uv.y;\n"
"    u = u >> IN_SHIFT;\n"
"    v = v >> IN_SHIFT;\n"
"    u = do_scale_pixel (u);\n"
"    v = do_scale_pixel (v);\n"
"    u = u << OUT_SHIFT;\n"
"    v = v << OUT_SHIFT;\n"
"    if (SWAP_UV) {\n"
"      unsigned short tmp = u;\n"
"      u = v;\n"
"      v = tmp;\n"
"    }\n"
"    write_chroma (dst1,\n"
"      dst2, u, v, x_pos, y_pos, CHROMA_PSTRIDE, stride, MASK);\n"
"  }\n"
"}\n"
"\n"
"}";

/* CUDA kernel source for from RGB to RGB conversion and scale */
static const gchar templ_RGB_to_RGB[] =
"extern \"C\"{\n"
"__constant__ float SCALE_H = %s;\n"
"__constant__ float SCALE_V = %s;\n"
"__constant__ int WIDTH = %d;\n"
"__constant__ int HEIGHT = %d;\n"
"__constant__ int IN_DEPTH = %d;\n"
"__constant__ int OUT_DEPTH = %d;\n"
"__constant__ int PSTRIDE = %d;\n"
"__constant__ int R_IDX = %d;\n"
"__constant__ int G_IDX = %d;\n"
"__constant__ int B_IDX = %d;\n"
"__constant__ int A_IDX = %d;\n"
"__constant__ int X_IDX = %d;\n"
"\n"
"__device__ unsigned short\n"
"do_scale_pixel (unsigned short val) \n"
"{\n"
"  unsigned int diff;\n"
"  if (OUT_DEPTH > IN_DEPTH) {\n"
"    diff = OUT_DEPTH - IN_DEPTH;\n"
"    return (val << diff) | (val >> (IN_DEPTH - diff));\n"
"  } else if (IN_DEPTH > OUT_DEPTH) {\n"
"    return val >> (IN_DEPTH - OUT_DEPTH);\n"
"  }\n"
"  return val;\n"
"}\n"
"\n"
/* __global__ void
 * GST_CUDA_KERNEL_FUNC_TO_ARGB
 */
"%s\n"
"\n"
/* convert ARGB or ARGB64 to other RGB formats with scale */
"__global__ void\n"
GST_CUDA_KERNEL_FUNC_SCALE_RGB
"(cudaTextureObject_t srcRGB, unsigned char *dstRGB, int dst_stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos < WIDTH && y_pos < HEIGHT) {\n"
"    float src_xpos = SCALE_H * x_pos;\n"
"    float src_ypos = SCALE_V * y_pos;\n"
"    ushort4 argb = tex2D<ushort4>(srcRGB, src_xpos, src_ypos);\n"
"    argb.x = do_scale_pixel(argb.x);\n"
"    argb.y = do_scale_pixel(argb.y);\n"
"    argb.z = do_scale_pixel(argb.z);\n"
"    argb.w = do_scale_pixel(argb.w);\n"
     /* FIXME: RGB10A2_LE or BGR10A2_LE only */
"    if (OUT_DEPTH > 8) {\n"
"      unsigned int packed_rgb = 0;\n"
"      unsigned int a, r, g, b;"
"      a = (argb.x >> 8) & 0x3;\n"
"      r = argb.y & 0x3ff;\n"
"      g = argb.z & 0x3ff;\n"
"      b = argb.w & 0x3ff;\n"
       /* A is always MSB, we support only little endian system */
"      packed_rgb = a << 30;\n"
"      packed_rgb |= (r << (30 - (R_IDX * 10)));\n"
"      packed_rgb |= (g << (30 - (G_IDX * 10)));\n"
"      packed_rgb |= (b << (30 - (B_IDX * 10)));\n"
"      *(unsigned int *)&dstRGB[x_pos * 4 + y_pos * dst_stride] = packed_rgb;\n"
"    } else {\n"
"      if (A_IDX >= 0) {\n"
"        argb.x = do_scale_pixel(argb.x);\n"
"        dstRGB[x_pos * PSTRIDE + A_IDX + y_pos * dst_stride] = argb.x;\n"
"      } else if (X_IDX >= 0) {\n"
"        dstRGB[x_pos * PSTRIDE + X_IDX + y_pos * dst_stride] = 0xff;\n"
"      }\n"
"      dstRGB[x_pos * PSTRIDE + R_IDX + y_pos * dst_stride] = argb.y;\n"
"      dstRGB[x_pos * PSTRIDE + G_IDX + y_pos * dst_stride] = argb.z;\n"
"      dstRGB[x_pos * PSTRIDE + B_IDX + y_pos * dst_stride] = argb.w;\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"}";
/* *INDENT-ON* */

typedef struct
{
  gint R;
  gint G;
  gint B;
  gint A;
  gint X;
} GstCudaRGBOrder;

typedef struct
{
  CUdeviceptr device_ptr;
  gsize cuda_stride;
} GstCudaStageBuffer;

#define CONVERTER_MAX_NUM_FUNC 4

struct _GstCudaConverter
{
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  gboolean keep_size;

  gint texture_alignment;

  GstCudaContext *cuda_ctx;
  CUmodule cuda_module;
  CUfunction kernel_func[CONVERTER_MAX_NUM_FUNC];
  const gchar *func_names[CONVERTER_MAX_NUM_FUNC];
  gchar *kernel_source;
  gchar *ptx;
  GstCudaStageBuffer fallback_buffer[GST_VIDEO_MAX_PLANES];

    gboolean (*convert) (GstCudaConverter * convert, const GstCudaMemory * src,
      GstVideoInfo * in_info, GstCudaMemory * dst, GstVideoInfo * out_info,
      CUstream cuda_stream);

  const CUdeviceptr src;
  GstVideoInfo *cur_in_info;

  CUdeviceptr dest;
  GstVideoInfo *cur_out_info;

  /* rgb to {rgb, yuv} only */
  GstCudaRGBOrder in_rgb_order;
  GstCudaStageBuffer unpack_surface;
  GstCudaStageBuffer y444_surface[GST_VIDEO_MAX_PLANES];
};

#define LOAD_CUDA_FUNC(module,func,name) G_STMT_START { \
  if (!gst_cuda_result (CuModuleGetFunction (&(func), (module), name))) { \
    GST_ERROR ("failed to get %s function", (name)); \
    goto error; \
  } \
} G_STMT_END

/**
 * gst_cuda_converter_new:
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @cuda_ctx: (transfer none): a #GstCudaContext
 *
 * Create a new converter object to convert between @in_info and @out_info
 * with @config.
 *
 * Returns: a #GstCudaConverter or %NULL if conversion is not possible.
 */
GstCudaConverter *
gst_cuda_converter_new (GstVideoInfo * in_info, GstVideoInfo * out_info,
    GstCudaContext * cuda_ctx)
{
  GstCudaConverter *convert;
  gint i;

  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);
  g_return_val_if_fail (cuda_ctx != NULL, NULL);
  /* we won't ever do framerate conversion */
  g_return_val_if_fail (in_info->fps_n == out_info->fps_n, NULL);
  g_return_val_if_fail (in_info->fps_d == out_info->fps_d, NULL);
  /* we won't ever do deinterlace */
  g_return_val_if_fail (in_info->interlace_mode == out_info->interlace_mode,
      NULL);

  convert = g_new0 (GstCudaConverter, 1);

  convert->in_info = *in_info;
  convert->out_info = *out_info;

  /* FIXME: should return kernel source */
  if (!gst_cuda_context_push (cuda_ctx)) {
    GST_ERROR ("cannot push context");
    goto error;
  }

  if (!cuda_converter_lookup_path (convert))
    goto error;

  convert->ptx = gst_cuda_nvrtc_compile (convert->kernel_source);
  if (!convert->ptx) {
    GST_ERROR ("no PTX data to load");
    goto error;
  }

  GST_TRACE ("compiled convert ptx \n%s", convert->ptx);

  if (!gst_cuda_result (CuModuleLoadData (&convert->cuda_module, convert->ptx))) {
    gst_cuda_context_pop (NULL);
    GST_ERROR ("failed to load cuda module data");

    goto error;
  }

  for (i = 0; i < CONVERTER_MAX_NUM_FUNC; i++) {
    if (!convert->func_names[i])
      break;

    LOAD_CUDA_FUNC (convert->cuda_module, convert->kernel_func[i],
        convert->func_names[i]);
    GST_DEBUG ("kernel function \"%s\" loaded", convert->func_names[i]);
  }

  gst_cuda_context_pop (NULL);
  convert->cuda_ctx = gst_object_ref (cuda_ctx);
  convert->texture_alignment =
      gst_cuda_context_get_texture_alignment (cuda_ctx);

  g_free (convert->kernel_source);
  g_free (convert->ptx);
  convert->kernel_source = NULL;
  convert->ptx = NULL;

  return convert;

error:
  gst_cuda_context_pop (NULL);
  gst_cuda_converter_free (convert);

  return NULL;
}

/**
 * gst_video_converter_free:
 * @convert: a #GstCudaConverter
 *
 * Free @convert
 */
void
gst_cuda_converter_free (GstCudaConverter * convert)
{
  g_return_if_fail (convert != NULL);

  if (convert->cuda_ctx) {
    if (gst_cuda_context_push (convert->cuda_ctx)) {
      gint i;

      if (convert->cuda_module) {
        gst_cuda_result (CuModuleUnload (convert->cuda_module));
      }

      for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
        if (convert->fallback_buffer[i].device_ptr)
          gst_cuda_result (CuMemFree (convert->fallback_buffer[i].device_ptr));
        if (convert->y444_surface[i].device_ptr)
          gst_cuda_result (CuMemFree (convert->y444_surface[i].device_ptr));
      }

      if (convert->unpack_surface.device_ptr)
        gst_cuda_result (CuMemFree (convert->unpack_surface.device_ptr));

      gst_cuda_context_pop (NULL);
    }

    gst_object_unref (convert->cuda_ctx);
  }

  g_free (convert->kernel_source);
  g_free (convert->ptx);
  g_free (convert);
}

/**
 * gst_cuda_converter_frame:
 * @convert: a #GstCudaConverter
 * @src: a #GstCudaMemory
 * @in_info: a #GstVideoInfo representing @src
 * @dst: a #GstCudaMemory
 * @out_info: a #GstVideoInfo representing @dst
 * @cuda_stream: a #CUstream
 *
 * Convert the pixels of @src into @dest using @convert.
 * Called without gst_cuda_context_push() and gst_cuda_context_pop() by caller
 */
gboolean
gst_cuda_converter_frame (GstCudaConverter * convert, const GstCudaMemory * src,
    GstVideoInfo * in_info, GstCudaMemory * dst, GstVideoInfo * out_info,
    CUstream cuda_stream)
{
  gboolean ret;

  g_return_val_if_fail (convert, FALSE);
  g_return_val_if_fail (src, FALSE);
  g_return_val_if_fail (in_info, FALSE);
  g_return_val_if_fail (dst, FALSE);
  g_return_val_if_fail (out_info, FALSE);

  gst_cuda_context_push (convert->cuda_ctx);

  ret = gst_cuda_converter_frame_unlocked (convert,
      src, in_info, dst, out_info, cuda_stream);

  gst_cuda_context_pop (NULL);

  return ret;
}

/**
 * gst_cuda_converter_frame_unlocked:
 * @convert: a #GstCudaConverter
 * @src: a #GstCudaMemory
 * @in_info: a #GstVideoInfo representing @src
 * @dst: a #GstCudaMemory
 * @out_info: a #GstVideoInfo representing @dest
 * @cuda_stream: a #CUstream
 *
 * Convert the pixels of @src into @dest using @convert.
 * Caller should call this method after gst_cuda_context_push()
 */
gboolean
gst_cuda_converter_frame_unlocked (GstCudaConverter * convert,
    const GstCudaMemory * src, GstVideoInfo * in_info, GstCudaMemory * dst,
    GstVideoInfo * out_info, CUstream cuda_stream)
{
  g_return_val_if_fail (convert, FALSE);
  g_return_val_if_fail (src, FALSE);
  g_return_val_if_fail (in_info, FALSE);
  g_return_val_if_fail (dst, FALSE);
  g_return_val_if_fail (out_info, FALSE);

  return convert->convert (convert, src, in_info, dst, out_info, cuda_stream);
}

/* allocate fallback memory for texture alignment requirement */
static gboolean
convert_ensure_fallback_memory (GstCudaConverter * convert,
    GstVideoInfo * info, guint plane)
{
  CUresult ret;
  guint element_size = 8;

  if (convert->fallback_buffer[plane].device_ptr)
    return TRUE;

  if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) > 8)
    element_size = 16;

  ret = CuMemAllocPitch (&convert->fallback_buffer[plane].device_ptr,
      &convert->fallback_buffer[plane].cuda_stride,
      GST_VIDEO_INFO_COMP_WIDTH (info, plane) *
      GST_VIDEO_INFO_COMP_PSTRIDE (info, plane),
      GST_VIDEO_INFO_COMP_HEIGHT (info, plane), element_size);

  if (!gst_cuda_result (ret)) {
    GST_ERROR ("failed to allocated fallback memory");
    return FALSE;
  }

  return TRUE;
}

/* create a 2D CUDA texture without alignment check */
static CUtexObject
convert_create_texture_unchecked (const CUdeviceptr src, gint width,
    gint height, gint channels, gint stride, CUarray_format format,
    CUfilter_mode mode, CUstream cuda_stream)
{
  CUDA_TEXTURE_DESC texture_desc;
  CUDA_RESOURCE_DESC resource_desc;
  CUtexObject texture = 0;
  CUresult cuda_ret;

  memset (&texture_desc, 0, sizeof (CUDA_TEXTURE_DESC));
  memset (&resource_desc, 0, sizeof (CUDA_RESOURCE_DESC));

  resource_desc.resType = CU_RESOURCE_TYPE_PITCH2D;
  resource_desc.res.pitch2D.format = format;
  resource_desc.res.pitch2D.numChannels = channels;
  resource_desc.res.pitch2D.width = width;
  resource_desc.res.pitch2D.height = height;
  resource_desc.res.pitch2D.pitchInBytes = stride;
  resource_desc.res.pitch2D.devPtr = src;

  texture_desc.filterMode = mode;
  texture_desc.flags = CU_TRSF_READ_AS_INTEGER;

  gst_cuda_result (CuStreamSynchronize (cuda_stream));
  cuda_ret = CuTexObjectCreate (&texture, &resource_desc, &texture_desc, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("couldn't create texture");

    return 0;
  }

  return texture;
}

static CUtexObject
convert_create_texture (GstCudaConverter * convert, const GstCudaMemory * src,
    GstVideoInfo * info, guint plane, CUstream cuda_stream)
{
  CUarray_format format = CU_AD_FORMAT_UNSIGNED_INT8;
  guint channels = 1;
  CUdeviceptr src_ptr;
  gsize stride;
  CUresult cuda_ret;
  CUfilter_mode mode;

  if (GST_VIDEO_INFO_COMP_DEPTH (info, plane) > 8)
    format = CU_AD_FORMAT_UNSIGNED_INT16;

  /* FIXME: more graceful method ? */
  if (plane != 0 &&
      GST_VIDEO_INFO_N_PLANES (info) != GST_VIDEO_INFO_N_COMPONENTS (info)) {
    channels = 2;
  }

  src_ptr = src->data + src->offset[plane];
  stride = src->stride;

  if (convert->texture_alignment && (src_ptr % convert->texture_alignment)) {
    CUDA_MEMCPY2D copy_params = { 0, };

    if (!convert_ensure_fallback_memory (convert, info, plane))
      return 0;

    GST_LOG ("device memory was not aligned, copy to fallback memory");

    copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.srcPitch = stride;
    copy_params.srcDevice = (CUdeviceptr) src_ptr;

    copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.dstPitch = convert->fallback_buffer[plane].cuda_stride;
    copy_params.dstDevice = convert->fallback_buffer[plane].device_ptr;
    copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (info, plane)
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, plane);
    copy_params.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, plane);

    cuda_ret = CuMemcpy2DAsync (&copy_params, cuda_stream);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR ("failed to copy to fallback buffer");
      return 0;
    }

    src_ptr = convert->fallback_buffer[plane].device_ptr;
    stride = convert->fallback_buffer[plane].cuda_stride;
  }

  /* Use h/w linear interpolation only when resize is required.
   * Otherwise the image might be blurred */
  if (convert->keep_size)
    mode = CU_TR_FILTER_MODE_POINT;
  else
    mode = CU_TR_FILTER_MODE_LINEAR;

  return convert_create_texture_unchecked (src_ptr,
      GST_VIDEO_INFO_COMP_WIDTH (info, plane),
      GST_VIDEO_INFO_COMP_HEIGHT (info, plane), channels, stride, format, mode,
      cuda_stream);
}

/* main conversion function for YUV to YUV conversion */
static gboolean
convert_YUV_TO_YUV (GstCudaConverter * convert,
    const GstCudaMemory * src, GstVideoInfo * in_info, GstCudaMemory * dst,
    GstVideoInfo * out_info, CUstream cuda_stream)
{
  CUtexObject texture[GST_VIDEO_MAX_PLANES] = { 0, };
  CUresult cuda_ret;
  gboolean ret = FALSE;
  CUdeviceptr dst_ptr[GST_VIDEO_MAX_PLANES] = { 0, };
  gint dst_stride;
  gint width, height;
  gint i;

  gpointer kernel_args[] = { &texture[0], &texture[1], &texture[2],
    &dst_ptr[0], &dst_ptr[1], &dst_ptr[2], &dst_stride
  };

  /* conversion step
   * STEP 1: create CUtexObject per plane
   * STEP 2: call YUV to YUV conversion kernel function.
   *         resize, uv reordering and bitdepth conversion will be performed in
   *         the CUDA kernel function
   */

  /* map CUDA device memory to CUDA texture object */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (in_info); i++) {
    texture[i] = convert_create_texture (convert, src, in_info, i, cuda_stream);
    if (!texture[i]) {
      GST_ERROR ("couldn't create texture for %d th plane", i);
      goto done;
    }
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++)
    dst_ptr[i] = dst->data + dst->offset[i];

  dst_stride = dst->stride;

  width = GST_VIDEO_INFO_WIDTH (out_info);
  height = GST_VIDEO_INFO_HEIGHT (out_info);

  cuda_ret =
      CuLaunchKernel (convert->kernel_func[0], DIV_UP (width, CUDA_BLOCK_X),
      DIV_UP (height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not rescale plane");
    goto done;
  }

  ret = TRUE;
  gst_cuda_result (CuStreamSynchronize (cuda_stream));

done:
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (in_info); i++) {
    if (texture[i])
      gst_cuda_result (CuTexObjectDestroy (texture[i]));
  }

  return ret;
}

/* main conversion function for YUV to RGB conversion */
static gboolean
convert_YUV_TO_RGB (GstCudaConverter * convert,
    const GstCudaMemory * src, GstVideoInfo * in_info, GstCudaMemory * dst,
    GstVideoInfo * out_info, CUstream cuda_stream)
{
  CUtexObject texture[GST_VIDEO_MAX_PLANES] = { 0, };
  CUresult cuda_ret;
  gboolean ret = FALSE;
  CUdeviceptr dstRGB = 0;
  gint dst_stride;
  gint width, height;
  gint i;

  gpointer kernel_args[] = { &texture[0], &texture[1], &texture[2],
    &dstRGB, &dst_stride
  };

  /* conversion step
   * STEP 1: create CUtexObject per plane
   * STEP 2: call YUV to RGB conversion kernel function.
   *         resizing, argb ordering and bitdepth conversion will be performed in
   *         the CUDA kernel function
   */

  /* map CUDA device memory to CUDA texture object */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (in_info); i++) {
    texture[i] = convert_create_texture (convert, src, in_info, i, cuda_stream);
    if (!texture[i]) {
      GST_ERROR ("couldn't create texture for %d th plane", i);
      goto done;
    }
  }

  dstRGB = dst->data;
  dst_stride = dst->stride;

  width = GST_VIDEO_INFO_WIDTH (out_info);
  height = GST_VIDEO_INFO_HEIGHT (out_info);

  cuda_ret =
      CuLaunchKernel (convert->kernel_func[0], DIV_UP (width, CUDA_BLOCK_X),
      DIV_UP (height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not rescale plane");
    goto done;
  }

  ret = TRUE;
  gst_cuda_result (CuStreamSynchronize (cuda_stream));

done:
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (in_info); i++) {
    if (texture[i])
      gst_cuda_result (CuTexObjectDestroy (texture[i]));
  }

  return ret;
}

static gboolean
convert_UNPACK_RGB (GstCudaConverter * convert, CUfunction kernel_func,
    CUstream cuda_stream, const GstCudaMemory * src, GstVideoInfo * in_info,
    CUdeviceptr dst, gint dst_stride, GstCudaRGBOrder * rgb_order)
{
  CUdeviceptr srcRGB = 0;
  gint width, height;
  gint src_stride, src_pstride;
  CUresult cuda_ret;

  gpointer unpack_kernel_args[] = { &srcRGB, &dst,
    &width, &height,
    &src_stride, &src_pstride, &dst_stride,
    &convert->in_rgb_order.R, &convert->in_rgb_order.G,
    &convert->in_rgb_order.B, &convert->in_rgb_order.A,
  };

  srcRGB = src->data;
  src_stride = src->stride;

  width = GST_VIDEO_INFO_WIDTH (in_info);
  height = GST_VIDEO_INFO_HEIGHT (in_info);
  src_pstride = GST_VIDEO_INFO_COMP_PSTRIDE (in_info, 0);

  cuda_ret =
      CuLaunchKernel (kernel_func, DIV_UP (width, CUDA_BLOCK_X),
      DIV_UP (height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, unpack_kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not unpack rgb");
    return FALSE;
  }

  return TRUE;
}

static gboolean
convert_TO_Y444 (GstCudaConverter * convert, CUfunction kernel_func,
    CUstream cuda_stream, CUtexObject srcRGB, CUdeviceptr dstY, gint y_stride,
    CUdeviceptr dstU, gint u_stride, CUdeviceptr dstV, gint v_stride,
    gint width, gint height, gint pstride, gint bitdepth)
{
  CUresult cuda_ret;

  gpointer kernel_args[] = { &srcRGB, &dstY, &y_stride, &dstU, &u_stride, &dstV,
    &v_stride, &width, &height, &pstride, &bitdepth,
  };

  cuda_ret =
      CuLaunchKernel (kernel_func, DIV_UP (width, CUDA_BLOCK_X),
      DIV_UP (height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not unpack rgb");
    return FALSE;
  }

  return TRUE;
}

/* main conversion function for RGB to YUV conversion */
static gboolean
convert_RGB_TO_YUV (GstCudaConverter * convert,
    const GstCudaMemory * src, GstVideoInfo * in_info, GstCudaMemory * dst,
    GstVideoInfo * out_info, CUstream cuda_stream)
{
  CUtexObject texture = 0;
  CUtexObject yuv_texture[3] = { 0, };
  CUdeviceptr dst_ptr[GST_VIDEO_MAX_PLANES] = { 0, };
  CUresult cuda_ret;
  gboolean ret = FALSE;
  gint in_width, in_height;
  gint out_width, out_height;
  gint dst_stride;
  CUarray_format format = CU_AD_FORMAT_UNSIGNED_INT8;
  CUfilter_mode mode = CU_TR_FILTER_MODE_POINT;
  gint pstride = 1;
  gint bitdepth = 8;
  gint i;

  gpointer kernel_args[] = { &yuv_texture[0], &yuv_texture[1], &yuv_texture[2],
    &dst_ptr[0], &dst_ptr[1], &dst_ptr[2], &dst_stride
  };

  /* conversion step
   * STEP 1: unpack src RGB into ARGB or ARGB64 format
   * STEP 2: convert unpacked ARGB (or ARGB64) to Y444 (or Y444_16LE)
   * STEP 3: convert Y444 (or Y444_16LE) to final YUV format.
   *         resizing, bitdepth conversion, uv reordering will be performed in
   *         the CUDA kernel function
   */
  if (!convert_UNPACK_RGB (convert, convert->kernel_func[0], cuda_stream,
          src, in_info, convert->unpack_surface.device_ptr,
          convert->unpack_surface.cuda_stride, &convert->in_rgb_order)) {
    GST_ERROR ("could not unpack input rgb");

    goto done;
  }

  in_width = GST_VIDEO_INFO_WIDTH (in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (in_info);

  out_width = GST_VIDEO_INFO_WIDTH (out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (out_info);
  dst_stride = dst->stride;

  if (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0) > 8) {
    pstride = 2;
    bitdepth = 16;
    format = CU_AD_FORMAT_UNSIGNED_INT16;
  }

  texture =
      convert_create_texture_unchecked (convert->unpack_surface.device_ptr,
      in_width, in_height, 4, convert->unpack_surface.cuda_stride, format,
      mode, cuda_stream);

  if (!texture) {
    GST_ERROR ("could not create texture");
    goto done;
  }

  if (!convert_TO_Y444 (convert, convert->kernel_func[1], cuda_stream, texture,
          convert->y444_surface[0].device_ptr,
          convert->y444_surface[0].cuda_stride,
          convert->y444_surface[1].device_ptr,
          convert->y444_surface[1].cuda_stride,
          convert->y444_surface[2].device_ptr,
          convert->y444_surface[2].cuda_stride, in_width, in_height, pstride,
          bitdepth)) {
    GST_ERROR ("could not convert to Y444 or Y444_16LE");
    goto done;
  }

  /* Use h/w linear interpolation only when resize is required.
   * Otherwise the image might be blurred */
  if (convert->keep_size)
    mode = CU_TR_FILTER_MODE_POINT;
  else
    mode = CU_TR_FILTER_MODE_LINEAR;

  for (i = 0; i < 3; i++) {
    yuv_texture[i] =
        convert_create_texture_unchecked (convert->y444_surface[i].device_ptr,
        in_width, in_height, 1, convert->y444_surface[i].cuda_stride, format,
        mode, cuda_stream);

    if (!yuv_texture[i]) {
      GST_ERROR ("could not create %dth yuv texture", i);
      goto done;
    }
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++)
    dst_ptr[i] = dst->data + dst->offset[i];

  cuda_ret =
      CuLaunchKernel (convert->kernel_func[2], DIV_UP (out_width, CUDA_BLOCK_X),
      DIV_UP (out_height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not rescale plane");
    goto done;
  }

  ret = TRUE;
  gst_cuda_result (CuStreamSynchronize (cuda_stream));

done:
  if (texture)
    gst_cuda_result (CuTexObjectDestroy (texture));
  for (i = 0; i < 3; i++) {
    if (yuv_texture[i])
      gst_cuda_result (CuTexObjectDestroy (yuv_texture[i]));
  }

  return ret;
}

/* main conversion function for RGB to RGB conversion */
static gboolean
convert_RGB_TO_RGB (GstCudaConverter * convert,
    const GstCudaMemory * src, GstVideoInfo * in_info, GstCudaMemory * dst,
    GstVideoInfo * out_info, CUstream cuda_stream)
{
  CUtexObject texture = 0;
  CUresult cuda_ret;
  gboolean ret = FALSE;
  CUdeviceptr dstRGB = 0;
  gint in_width, in_height;
  gint out_width, out_height;
  gint dst_stride;
  CUfilter_mode mode;
  CUarray_format format = CU_AD_FORMAT_UNSIGNED_INT8;

  gpointer rescale_kernel_args[] = { &texture, &dstRGB, &dst_stride };

  /* conversion step
   * STEP 1: unpack src RGB into ARGB or ARGB64 format
   * STEP 2: convert ARGB (or ARGB64) to final RGB format.
   *         resizing, bitdepth conversion, argb reordering will be performed in
   *         the CUDA kernel function
   */

  if (!convert_UNPACK_RGB (convert, convert->kernel_func[0], cuda_stream,
          src, in_info, convert->unpack_surface.device_ptr,
          convert->unpack_surface.cuda_stride, &convert->in_rgb_order)) {
    GST_ERROR ("could not unpack input rgb");

    goto done;
  }

  in_width = GST_VIDEO_INFO_WIDTH (in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (in_info);

  out_width = GST_VIDEO_INFO_WIDTH (out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (out_info);

  dstRGB = dst->data;
  dst_stride = dst->stride;

  if (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0) > 8)
    format = CU_AD_FORMAT_UNSIGNED_INT16;

  /* Use h/w linear interpolation only when resize is required.
   * Otherwise the image might be blurred */
  if (convert->keep_size)
    mode = CU_TR_FILTER_MODE_POINT;
  else
    mode = CU_TR_FILTER_MODE_LINEAR;

  texture =
      convert_create_texture_unchecked (convert->unpack_surface.device_ptr,
      in_width, in_height, 4, convert->unpack_surface.cuda_stride, format,
      mode, cuda_stream);

  if (!texture) {
    GST_ERROR ("could not create texture");
    goto done;
  }

  cuda_ret =
      CuLaunchKernel (convert->kernel_func[1], DIV_UP (out_width, CUDA_BLOCK_X),
      DIV_UP (out_height, CUDA_BLOCK_Y), 1, CUDA_BLOCK_X, CUDA_BLOCK_Y, 1, 0,
      cuda_stream, rescale_kernel_args, NULL);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR ("could not rescale plane");
    goto done;
  }

  ret = TRUE;
  gst_cuda_result (CuStreamSynchronize (cuda_stream));

done:
  if (texture)
    gst_cuda_result (CuTexObjectDestroy (texture));

  return ret;
}

/* from video-converter.c */
typedef struct
{
  gdouble dm[4][4];
} MatrixData;

static void
color_matrix_set_identity (MatrixData * m)
{
  gint i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->dm[i][j] = (i == j);
    }
  }
}

static void
color_matrix_copy (MatrixData * d, const MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      d->dm[i][j] = s->dm[i][j];
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (MatrixData * dst, MatrixData * a, MatrixData * b)
{
  MatrixData tmp;
  gint i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      gdouble x = 0;
      for (k = 0; k < 4; k++) {
        x += a->dm[i][k] * b->dm[k][j];
      }
      tmp.dm[i][j] = x;
    }
  }
  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_offset_components (MatrixData * m, gdouble a1, gdouble a2,
    gdouble a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][3] = a1;
  a.dm[1][3] = a2;
  a.dm[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (MatrixData * m, gdouble a1, gdouble a2,
    gdouble a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][0] = a1;
  a.dm[1][1] = a2;
  a.dm[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_debug (const MatrixData * s)
{
  GST_DEBUG ("[%f %f %f %f]", s->dm[0][0], s->dm[0][1], s->dm[0][2],
      s->dm[0][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[1][0], s->dm[1][1], s->dm[1][2],
      s->dm[1][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[2][0], s->dm[2][1], s->dm[2][2],
      s->dm[2][3]);
  GST_DEBUG ("[%f %f %f %f]", s->dm[3][0], s->dm[3][1], s->dm[3][2],
      s->dm[3][3]);
}

static void
color_matrix_YCbCr_to_RGB (MatrixData * m, gdouble Kr, gdouble Kb)
{
  gdouble Kg = 1.0 - Kr - Kb;
  MatrixData k = {
    {
          {1., 0., 2 * (1 - Kr), 0.},
          {1., -2 * Kb * (1 - Kb) / Kg, -2 * Kr * (1 - Kr) / Kg, 0.},
          {1., 2 * (1 - Kb), 0., 0.},
          {0., 0., 0., 1.},
        }
  };

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_YCbCr (MatrixData * m, gdouble Kr, gdouble Kb)
{
  gdouble Kg = 1.0 - Kr - Kb;
  MatrixData k;
  gdouble x;

  k.dm[0][0] = Kr;
  k.dm[0][1] = Kg;
  k.dm[0][2] = Kb;
  k.dm[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.dm[1][0] = -x * Kr;
  k.dm[1][1] = -x * Kg;
  k.dm[1][2] = x * (1 - Kb);
  k.dm[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.dm[2][0] = x * (1 - Kr);
  k.dm[2][1] = -x * Kg;
  k.dm[2][2] = -x * Kb;
  k.dm[2][3] = 0;

  k.dm[3][0] = 0;
  k.dm[3][1] = 0;
  k.dm[3][2] = 0;
  k.dm[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
compute_matrix_to_RGB (GstCudaConverter * convert, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  /* bring color components to [0..1.0] range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_offset_components (data, -offset[0], -offset[1], -offset[2]);
  color_matrix_scale_components (data, 1 / ((float) scale[0]),
      1 / ((float) scale[1]), 1 / ((float) scale[2]));

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to R'G'B' space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_YCbCr_to_RGB (data, Kr, Kb);
  }
  color_matrix_debug (data);
}

static void
compute_matrix_to_YUV (GstCudaConverter * convert, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to YCbCr space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_RGB_to_YCbCr (data, Kr, Kb);
  }

  /* bring color components to nominal range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_scale_components (data, (float) scale[0], (float) scale[1],
      (float) scale[2]);
  color_matrix_offset_components (data, offset[0], offset[1], offset[2]);

  color_matrix_debug (data);
}

static gboolean
cuda_converter_get_matrix (GstCudaConverter * convert, MatrixData * matrix,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gboolean same_matrix, same_bits;
  guint in_bits, out_bits;

  in_bits = GST_VIDEO_INFO_COMP_DEPTH (in_info, 0);
  out_bits = GST_VIDEO_INFO_COMP_DEPTH (out_info, 0);

  same_bits = in_bits == out_bits;
  same_matrix = in_info->colorimetry.matrix == out_info->colorimetry.matrix;

  GST_DEBUG ("matrix %d -> %d (%d)", in_info->colorimetry.matrix,
      out_info->colorimetry.matrix, same_matrix);
  GST_DEBUG ("bits %d -> %d (%d)", in_bits, out_bits, same_bits);

  color_matrix_set_identity (matrix);

  if (same_bits && same_matrix) {
    GST_DEBUG ("conversion matrix is not required");

    return FALSE;
  }

  if (in_bits < out_bits) {
    gint scale = 1 << (out_bits - in_bits);
    color_matrix_scale_components (matrix,
        1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
  }

  GST_DEBUG ("to RGB matrix");
  compute_matrix_to_RGB (convert, matrix, in_info);
  GST_DEBUG ("current matrix");
  color_matrix_debug (matrix);

  GST_DEBUG ("to YUV matrix");
  compute_matrix_to_YUV (convert, matrix, out_info);
  GST_DEBUG ("current matrix");
  color_matrix_debug (matrix);

  if (in_bits > out_bits) {
    gint scale = 1 << (in_bits - out_bits);
    color_matrix_scale_components (matrix,
        (float) scale, (float) scale, (float) scale);
  }

  GST_DEBUG ("final matrix");
  color_matrix_debug (matrix);

  return TRUE;
}

static gboolean
is_uv_swapped (GstVideoFormat format)
{
  static GstVideoFormat swapped_formats[] = {
    GST_VIDEO_FORMAT_YV12,
    GST_VIDEO_FORMAT_NV21,
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (swapped_formats); i++) {
    if (format == swapped_formats[i])
      return TRUE;
  }

  return FALSE;
}

typedef struct
{
  const gchar *read_chroma;
  const gchar *write_chroma;
  const gchar *unpack_function;
  gfloat scale_h, scale_v;
  gfloat chroma_scale_h, chroma_scale_v;
  gint width, height;
  gint chroma_width, chroma_height;
  gint in_depth;
  gint out_depth;
  gint pstride, chroma_pstride;
  gint in_shift, out_shift;
  gint mask;
  gint swap_uv;
  /* RGBA specific variables */
  gint max_in_val;
  GstCudaRGBOrder rgb_order;
} GstCudaKernelTempl;

static gchar *
cuda_converter_generate_yuv_to_yuv_kernel_code (GstCudaConverter * convert,
    GstCudaKernelTempl * templ)
{
  gchar scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd (scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_h);
  g_ascii_formatd (scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_v);
  g_ascii_formatd (chroma_scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_h);
  g_ascii_formatd (chroma_scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_v);
  return g_strdup_printf (templ_YUV_TO_YUV, scale_h_str, scale_v_str,
      chroma_scale_h_str, chroma_scale_v_str, templ->width, templ->height,
      templ->chroma_width, templ->chroma_height, templ->in_depth,
      templ->out_depth, templ->pstride, templ->chroma_pstride, templ->in_shift,
      templ->out_shift, templ->mask, templ->swap_uv, templ->read_chroma,
      templ->write_chroma);
}

static gchar *
cuda_converter_generate_yuv_to_rgb_kernel_code (GstCudaConverter * convert,
    GstCudaKernelTempl * templ, MatrixData * matrix)
{
  gchar matrix_dm[4][4][G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  gint i, j;
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      g_ascii_formatd (matrix_dm[i][j], G_ASCII_DTOSTR_BUF_SIZE, "%f",
          matrix->dm[i][j]);
    }
  }
  g_ascii_formatd (scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_h);
  g_ascii_formatd (scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_v);
  g_ascii_formatd (chroma_scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_h);
  g_ascii_formatd (chroma_scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_v);
  return g_strdup_printf (templ_YUV_TO_RGB, matrix_dm[0][3], matrix_dm[1][3],
      matrix_dm[2][3], matrix_dm[0][0], matrix_dm[0][1], matrix_dm[0][2],
      matrix_dm[1][0], matrix_dm[1][1], matrix_dm[1][2], matrix_dm[2][0],
      matrix_dm[2][1], matrix_dm[2][2], scale_h_str, scale_v_str,
      chroma_scale_h_str, chroma_scale_v_str, templ->width, templ->height,
      templ->chroma_width, templ->chroma_height, templ->in_depth,
      templ->out_depth, templ->pstride, templ->chroma_pstride, templ->in_shift,
      templ->out_shift, templ->mask, templ->swap_uv, templ->max_in_val,
      templ->rgb_order.R, templ->rgb_order.G, templ->rgb_order.B,
      templ->rgb_order.A, templ->rgb_order.X, templ->read_chroma);
}

static gchar *
cuda_converter_generate_rgb_to_yuv_kernel_code (GstCudaConverter * convert,
    GstCudaKernelTempl * templ, MatrixData * matrix)
{
  gchar matrix_dm[4][4][G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar chroma_scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  gint i, j;
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      g_ascii_formatd (matrix_dm[i][j], G_ASCII_DTOSTR_BUF_SIZE, "%f",
          matrix->dm[i][j]);
    }
  }
  g_ascii_formatd (scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_h);
  g_ascii_formatd (scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_v);
  g_ascii_formatd (chroma_scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_h);
  g_ascii_formatd (chroma_scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f",
      templ->chroma_scale_v);
  return g_strdup_printf (templ_RGB_TO_YUV, matrix_dm[0][3], matrix_dm[1][3],
      matrix_dm[2][3], matrix_dm[0][0], matrix_dm[0][1], matrix_dm[0][2],
      matrix_dm[1][0], matrix_dm[1][1], matrix_dm[1][2], matrix_dm[2][0],
      matrix_dm[2][1], matrix_dm[2][2], scale_h_str, scale_v_str,
      chroma_scale_h_str, chroma_scale_v_str, templ->width, templ->height,
      templ->chroma_width, templ->chroma_height, templ->in_depth,
      templ->out_depth, templ->pstride, templ->chroma_pstride, templ->in_shift,
      templ->out_shift, templ->mask, templ->swap_uv, templ->unpack_function,
      templ->read_chroma, templ->write_chroma);
}

static gchar *
cuda_converter_generate_rgb_to_rgb_kernel_code (GstCudaConverter * convert,
    GstCudaKernelTempl * templ)
{
  gchar scale_h_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar scale_v_str[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd (scale_h_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_h);
  g_ascii_formatd (scale_v_str, G_ASCII_DTOSTR_BUF_SIZE, "%f", templ->scale_v);
  return g_strdup_printf (templ_RGB_to_RGB,
      scale_h_str, scale_v_str,
      templ->width, templ->height,
      templ->in_depth, templ->out_depth, templ->pstride,
      templ->rgb_order.R, templ->rgb_order.G,
      templ->rgb_order.B, templ->rgb_order.A, templ->rgb_order.X,
      templ->unpack_function);
}

#define SET_ORDER(o,r,g,b,a,x) G_STMT_START { \
  (o)->R = (r); \
  (o)->G = (g); \
  (o)->B = (b); \
  (o)->A = (a); \
  (o)->X = (x); \
} G_STMT_END

static void
cuda_converter_get_rgb_order (GstVideoFormat format, GstCudaRGBOrder * order)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:
      SET_ORDER (order, 0, 1, 2, 3, -1);
      break;
    case GST_VIDEO_FORMAT_RGBx:
      SET_ORDER (order, 0, 1, 2, -1, 3);
      break;
    case GST_VIDEO_FORMAT_BGRA:
      SET_ORDER (order, 2, 1, 0, 3, -1);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      SET_ORDER (order, 2, 1, 0, -1, 3);
      break;
    case GST_VIDEO_FORMAT_ARGB:
      SET_ORDER (order, 1, 2, 3, 0, -1);
      break;
    case GST_VIDEO_FORMAT_ABGR:
      SET_ORDER (order, 3, 2, 1, 0, -1);
      break;
    case GST_VIDEO_FORMAT_RGB:
      SET_ORDER (order, 0, 1, 2, -1, -1);
      break;
    case GST_VIDEO_FORMAT_BGR:
      SET_ORDER (order, 2, 1, 0, -1, -1);
      break;
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      SET_ORDER (order, 1, 2, 3, 0, -1);
      break;
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      SET_ORDER (order, 3, 2, 1, 0, -1);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static gboolean
cuda_converter_lookup_path (GstCudaConverter * convert)
{
  GstVideoFormat in_format, out_format;
  gboolean src_yuv, dst_yuv;
  gboolean src_planar, dst_planar;
  GstCudaKernelTempl templ = { 0, };
  GstVideoInfo *in_info, *out_info;
  gboolean ret = FALSE;
  CUresult cuda_ret;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  in_format = GST_VIDEO_INFO_FORMAT (in_info);
  out_format = GST_VIDEO_INFO_FORMAT (out_info);

  src_yuv = GST_VIDEO_INFO_IS_YUV (in_info);
  dst_yuv = GST_VIDEO_INFO_IS_YUV (out_info);

  src_planar = GST_VIDEO_INFO_N_PLANES (in_info) ==
      GST_VIDEO_INFO_N_COMPONENTS (in_info);
  dst_planar = GST_VIDEO_INFO_N_PLANES (out_info) ==
      GST_VIDEO_INFO_N_COMPONENTS (out_info);

  convert->keep_size = (GST_VIDEO_INFO_WIDTH (&convert->in_info) ==
      GST_VIDEO_INFO_WIDTH (&convert->out_info) &&
      GST_VIDEO_INFO_HEIGHT (&convert->in_info) ==
      GST_VIDEO_INFO_HEIGHT (&convert->out_info));

  templ.scale_h = (gfloat) GST_VIDEO_INFO_COMP_WIDTH (in_info, 0) /
      (gfloat) GST_VIDEO_INFO_COMP_WIDTH (out_info, 0);
  templ.scale_v = (gfloat) GST_VIDEO_INFO_COMP_HEIGHT (in_info, 0) /
      (gfloat) GST_VIDEO_INFO_COMP_HEIGHT (out_info, 0);
  templ.chroma_scale_h = (gfloat) GST_VIDEO_INFO_COMP_WIDTH (in_info, 1) /
      (gfloat) GST_VIDEO_INFO_COMP_WIDTH (out_info, 1);
  templ.chroma_scale_v = (gfloat) GST_VIDEO_INFO_COMP_HEIGHT (in_info, 1) /
      (gfloat) GST_VIDEO_INFO_COMP_HEIGHT (out_info, 1);
  templ.width = GST_VIDEO_INFO_COMP_WIDTH (out_info, 0);
  templ.height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, 0);
  templ.chroma_width = GST_VIDEO_INFO_COMP_WIDTH (out_info, 1);
  templ.chroma_height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, 1);

  templ.in_depth = GST_VIDEO_INFO_COMP_DEPTH (in_info, 0);
  templ.out_depth = GST_VIDEO_INFO_COMP_DEPTH (out_info, 0);
  templ.pstride = GST_VIDEO_INFO_COMP_PSTRIDE (out_info, 0);
  templ.chroma_pstride = GST_VIDEO_INFO_COMP_PSTRIDE (out_info, 1);
  templ.in_shift = in_info->finfo->shift[0];
  templ.out_shift = out_info->finfo->shift[0];
  templ.mask = ((1 << templ.out_depth) - 1) << templ.out_shift;
  templ.swap_uv = (is_uv_swapped (in_format) != is_uv_swapped (out_format));

  if (src_yuv && dst_yuv) {
    convert->convert = convert_YUV_TO_YUV;

    if (src_planar && dst_planar) {
      templ.read_chroma = READ_CHROMA_FROM_PLANAR;
      templ.write_chroma = WRITE_CHROMA_TO_PLANAR;
    } else if (!src_planar && dst_planar) {
      templ.read_chroma = READ_CHROMA_FROM_SEMI_PLANAR;
      templ.write_chroma = WRITE_CHROMA_TO_PLANAR;
    } else if (src_planar && !dst_planar) {
      templ.read_chroma = READ_CHROMA_FROM_PLANAR;
      templ.write_chroma = WRITE_CHROMA_TO_SEMI_PLANAR;
    } else {
      templ.read_chroma = READ_CHROMA_FROM_SEMI_PLANAR;
      templ.write_chroma = WRITE_CHROMA_TO_SEMI_PLANAR;
    }

    convert->kernel_source =
        cuda_converter_generate_yuv_to_yuv_kernel_code (convert, &templ);
    convert->func_names[0] = GST_CUDA_KERNEL_FUNC;

    ret = TRUE;
  } else if (src_yuv && !dst_yuv) {
    MatrixData matrix;

    if (src_planar) {
      templ.read_chroma = READ_CHROMA_FROM_PLANAR;
    } else {
      templ.read_chroma = READ_CHROMA_FROM_SEMI_PLANAR;
    }

    templ.max_in_val = (1 << templ.in_depth) - 1;
    cuda_converter_get_rgb_order (out_format, &templ.rgb_order);

    cuda_converter_get_matrix (convert, &matrix, in_info, out_info);
    convert->kernel_source =
        cuda_converter_generate_yuv_to_rgb_kernel_code (convert,
        &templ, &matrix);
    convert->func_names[0] = GST_CUDA_KERNEL_FUNC;

    convert->convert = convert_YUV_TO_RGB;

    ret = TRUE;
  } else if (!src_yuv && dst_yuv) {
    MatrixData matrix;
    gsize element_size = 8;
    GstVideoFormat unpack_format;
    GstVideoFormat y444_format;
    GstVideoInfo unpack_info;
    GstVideoInfo y444_info;
    gint i;

    if (dst_planar) {
      templ.write_chroma = WRITE_CHROMA_TO_PLANAR;
    } else {
      templ.write_chroma = WRITE_CHROMA_TO_SEMI_PLANAR;
    }
    templ.read_chroma = READ_CHROMA_FROM_PLANAR;

    cuda_converter_get_rgb_order (in_format, &convert->in_rgb_order);

    if (templ.in_depth > 8) {
      /* FIXME: RGB10A2_LE and BGR10A2_LE only */
      element_size = 16;
      unpack_format = GST_VIDEO_FORMAT_ARGB64;
      y444_format = GST_VIDEO_FORMAT_Y444_16LE;
      templ.unpack_function = unpack_to_ARGB64;
    } else {
      unpack_format = GST_VIDEO_FORMAT_ARGB;
      y444_format = GST_VIDEO_FORMAT_Y444;
      templ.unpack_function = unpack_to_ARGB;
    }

    gst_video_info_set_format (&unpack_info,
        unpack_format, GST_VIDEO_INFO_WIDTH (in_info),
        GST_VIDEO_INFO_HEIGHT (in_info));
    gst_video_info_set_format (&y444_info,
        y444_format, GST_VIDEO_INFO_WIDTH (in_info),
        GST_VIDEO_INFO_HEIGHT (in_info));

    templ.in_depth = GST_VIDEO_INFO_COMP_DEPTH (&unpack_info, 0);

    cuda_ret = CuMemAllocPitch (&convert->unpack_surface.device_ptr,
        &convert->unpack_surface.cuda_stride,
        GST_VIDEO_INFO_COMP_WIDTH (&unpack_info, 0) *
        GST_VIDEO_INFO_COMP_PSTRIDE (&unpack_info, 0),
        GST_VIDEO_INFO_HEIGHT (&unpack_info), element_size);

    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR ("couldn't alloc unpack surface");
      return FALSE;
    }

    for (i = 0; i < 3; i++) {
      cuda_ret = CuMemAllocPitch (&convert->y444_surface[i].device_ptr,
          &convert->y444_surface[i].cuda_stride,
          GST_VIDEO_INFO_COMP_WIDTH (&y444_info, i) *
          GST_VIDEO_INFO_COMP_PSTRIDE (&y444_info, i),
          GST_VIDEO_INFO_COMP_HEIGHT (&y444_info, i), element_size);

      if (!gst_cuda_result (cuda_ret)) {
        GST_ERROR ("couldn't alloc %dth y444 surface", i);
        return FALSE;
      }
    }

    cuda_converter_get_matrix (convert, &matrix, &unpack_info, &y444_info);

    convert->kernel_source =
        cuda_converter_generate_rgb_to_yuv_kernel_code (convert,
        &templ, &matrix);

    convert->func_names[0] = GST_CUDA_KERNEL_FUNC_TO_ARGB;
    convert->func_names[1] = GST_CUDA_KERNEL_FUNC_TO_Y444;
    convert->func_names[2] = GST_CUDA_KERNEL_FUNC_Y444_TO_YUV;

    convert->convert = convert_RGB_TO_YUV;

    ret = TRUE;
  } else {
    gsize element_size = 8;
    GstVideoFormat unpack_format;
    GstVideoInfo unpack_info;

    cuda_converter_get_rgb_order (in_format, &convert->in_rgb_order);
    cuda_converter_get_rgb_order (out_format, &templ.rgb_order);

    if (templ.in_depth > 8) {
      /* FIXME: RGB10A2_LE and BGR10A2_LE only */
      element_size = 16;
      unpack_format = GST_VIDEO_FORMAT_ARGB64;
      templ.unpack_function = unpack_to_ARGB64;
    } else {
      unpack_format = GST_VIDEO_FORMAT_ARGB;
      templ.unpack_function = unpack_to_ARGB;
    }

    gst_video_info_set_format (&unpack_info,
        unpack_format, GST_VIDEO_INFO_WIDTH (in_info),
        GST_VIDEO_INFO_HEIGHT (in_info));

    templ.in_depth = GST_VIDEO_INFO_COMP_DEPTH (&unpack_info, 0);

    cuda_ret = CuMemAllocPitch (&convert->unpack_surface.device_ptr,
        &convert->unpack_surface.cuda_stride,
        GST_VIDEO_INFO_COMP_WIDTH (&unpack_info, 0) *
        GST_VIDEO_INFO_COMP_PSTRIDE (&unpack_info, 0),
        GST_VIDEO_INFO_HEIGHT (&unpack_info), element_size);

    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR ("couldn't alloc unpack surface");
      return FALSE;
    }

    convert->kernel_source =
        cuda_converter_generate_rgb_to_rgb_kernel_code (convert, &templ);

    convert->func_names[0] = GST_CUDA_KERNEL_FUNC_TO_ARGB;
    convert->func_names[1] = GST_CUDA_KERNEL_FUNC_SCALE_RGB;

    convert->convert = convert_RGB_TO_RGB;

    ret = TRUE;
  }

  if (!ret) {
    GST_DEBUG ("no path found");

    return FALSE;
  }

  GST_TRACE ("configured CUDA kernel source\n%s", convert->kernel_source);

  return TRUE;
}
