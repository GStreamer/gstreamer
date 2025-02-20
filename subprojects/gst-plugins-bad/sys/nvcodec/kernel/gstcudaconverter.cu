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
struct ColorMatrix
{
  float CoeffX[3];
  float CoeffY[3];
  float CoeffZ[3];
  float Offset[3];
  float Min[3];
  float Max[3];
};

struct ConstBuffer
{
  ColorMatrix matrix;
  int width;
  int height;
  int left;
  int top;
  int right;
  int bottom;
  int view_width;
  int view_height;
  float border_x;
  float border_y;
  float border_z;
  float border_w;
  int fill_border;
  int video_direction;
  float alpha;
  int do_blend;
  int do_convert;
};

__device__ inline float
dot (const float coeff[3], float3 val)
{
  return coeff[0] * val.x + coeff[1] * val.y + coeff[2] * val.z;
}

__device__ inline float
clamp (float val, float min_val, float max_val)
{
  return max (min_val, min (val, max_val));
}

__device__ inline float3
clamp3 (float3 val, const float min_val[3], const float max_val[3])
{
  return make_float3 (clamp (val.x, min_val[0], max_val[0]),
      clamp (val.y, min_val[1], max_val[2]),
      clamp (val.z, min_val[1], max_val[2]));
}

__device__ inline unsigned char
scale_to_2bits (float val)
{
  return (unsigned short) __float2int_rz (val * 3.0);
}

__device__ inline unsigned char
scale_to_uchar (float val)
{
  return (unsigned char) __float2int_rz (val * 255.0);
}

__device__ inline unsigned short
scale_to_ushort (float val)
{
  return (unsigned short) __float2int_rz (val * 65535.0);
}

__device__ inline unsigned short
scale_to_10bits (float val)
{
  return (unsigned short) __float2int_rz (val * 1023.0);
}

__device__ inline unsigned short
scale_to_12bits (float val)
{
  return (unsigned short) __float2int_rz (val * 4095.0);
}

__device__ inline unsigned char
blend_uchar (unsigned char dst, float src, float src_alpha)
{
  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor
  float src_val = src * src_alpha;
  float dst_val = __int2float_rz (dst) / 255.0 * (1.0 - src_alpha);
  return scale_to_uchar(clamp(src_val + dst_val, 0, 1.0));
}

__device__ inline unsigned short
blend_ushort (unsigned short dst, float src, float src_alpha)
{
  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor
  float src_val = src * src_alpha;
  float dst_val = __int2float_rz (dst) / 65535.0 * (1.0 - src_alpha);
  return scale_to_ushort(clamp(src_val + dst_val, 0, 1.0));
}

__device__ inline unsigned short
blend_10bits (unsigned short dst, float src, float src_alpha)
{
  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor
  float src_val = src * src_alpha;
  float dst_val = __int2float_rz (dst) / 1023.0 * (1.0 - src_alpha);
  return scale_to_10bits(clamp(src_val + dst_val, 0, 1.0));
}

__device__ inline unsigned short
blend_12bits (unsigned short dst, float src, float src_alpha)
{
  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor
  float src_val = src * src_alpha;
  float dst_val = __int2float_rz (dst) / 4095.0 * (1.0 - src_alpha);
  return scale_to_12bits(clamp(src_val + dst_val, 0, 1.0));
}

struct IConverter
{
  __device__ virtual float3
  Execute (float3 sample, const ColorMatrix * matrix) = 0;
};

struct ConvertSimple : public IConverter
{
  __device__ float3
  Execute (float3 sample, const ColorMatrix * matrix)
  {
    float3 out;
    out.x = dot (matrix->CoeffX, sample);
    out.y = dot (matrix->CoeffY, sample);
    out.z = dot (matrix->CoeffZ, sample);
    out.x += matrix->Offset[0];
    out.y += matrix->Offset[1];
    out.z += matrix->Offset[2];
    return clamp3 (out, matrix->Min, matrix->Max);
  }
};

struct ISampler
{
  __device__ virtual float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y) = 0;
};

struct SampleI420 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float u = tex2D<float>(tex1, x, y);
    float v = tex2D<float>(tex2, x, y);
    return make_float4 (luma, u, v, 1);
  }
};

struct SampleYV12 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float u = tex2D<float>(tex2, x, y);
    float v = tex2D<float>(tex1, x, y);
    return make_float4 (luma, u, v, 1);
  }
};

struct SampleI420_10 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float u = tex2D<float>(tex1, x, y);
    float v = tex2D<float>(tex2, x, y);
    /* (1 << 6) to scale [0, 1.0) range */
    return make_float4 (luma * 64, u * 64, v * 64, 1);
  }
};

struct SampleI420_12 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float u = tex2D<float>(tex1, x, y);
    float v = tex2D<float>(tex2, x, y);
    /* (1 << 4) to scale [0, 1.0) range */
    return make_float4 (luma * 16, u * 16, v * 16, 1);
  }
};

struct SampleNV12 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float2 uv = tex2D<float2>(tex1, x, y);
    return make_float4 (luma, uv.x, uv.y, 1);
  }
};

struct SampleNV21 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float luma = tex2D<float>(tex0, x, y);
    float2 vu = tex2D<float2>(tex1, x, y);
    return make_float4 (luma, vu.y, vu.x, 1);
  }
};

struct SampleRGBA : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    return tex2D<float4>(tex0, x, y);
  }
};

struct SampleBGRA : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float4 bgra = tex2D<float4>(tex0, x, y);
    return make_float4 (bgra.z, bgra.y, bgra.x, bgra.w);
  }
};

struct SampleRGBx : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float4 rgbx = tex2D<float4>(tex0, x, y);
    rgbx.w = 1;
    return rgbx;
  }
};

struct SampleBGRx : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float4 bgrx = tex2D<float4>(tex0, x, y);
    return make_float4 (bgrx.z, bgrx.y, bgrx.x, 1);
  }
};

struct SampleARGB : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
   float4 argb = tex2D<float4>(tex0, x, y);
   return make_float4 (argb.y, argb.z, argb.w, argb.x);
  }
};

struct SampleABGR : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
   float4 abgr = tex2D<float4>(tex0, x, y);
   return make_float4 (abgr.w, abgr.z, abgr.y, abgr.x);
  }
};

struct SampleRGBP : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float r = tex2D<float>(tex0, x, y);
    float g = tex2D<float>(tex1, x, y);
    float b = tex2D<float>(tex2, x, y);
    return make_float4 (r, g, b, 1);
  }
};

struct SampleBGRP : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float b = tex2D<float>(tex0, x, y);
    float g = tex2D<float>(tex1, x, y);
    float r = tex2D<float>(tex2, x, y);
    return make_float4 (r, g, b, 1);
  }
};

struct SampleGBR : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float g = tex2D<float>(tex0, x, y);
    float b = tex2D<float>(tex1, x, y);
    float r = tex2D<float>(tex2, x, y);
    return make_float4 (r, g, b, 1);
  }
};

struct SampleGBR_10 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float g = tex2D<float>(tex0, x, y);
    float b = tex2D<float>(tex1, x, y);
    float r = tex2D<float>(tex2, x, y);
    /* (1 << 6) to scale [0, 1.0) range */
    return make_float4 (r * 64, g * 64, b * 64, 1);
  }
};

struct SampleGBR_12 : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float g = tex2D<float>(tex0, x, y);
    float b = tex2D<float>(tex1, x, y);
    float r = tex2D<float>(tex2, x, y);
    /* (1 << 4) to scale [0, 1.0) range */
    return make_float4 (r * 16, g * 16, b * 16, 1);
  }
};

struct SampleGBRA : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float g = tex2D<float>(tex0, x, y);
    float b = tex2D<float>(tex1, x, y);
    float r = tex2D<float>(tex2, x, y);
    float a = tex2D<float>(tex3, x, y);
    return make_float4 (r, g, b, a);
  }
};

struct SampleVUYA : public ISampler
{
  __device__ float4
  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)
  {
    float4 vuya = tex2D<float4>(tex0, x, y);
    return make_float4 (vuya.z, vuya.y, vuya.x, vuya.w);
  }
};

struct IOutput
{
  __device__ virtual void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1) = 0;

  __device__ virtual void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
        unsigned char * dst3, float4 sample, int x, int y, int stride0,
        int stride1) = 0;
};

struct OutputI420 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    dst0[x + y * stride0] = scale_to_uchar (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x / 2 + (y / 2) * stride1;
      dst1[pos] = scale_to_uchar (sample.y);
      dst2[pos] = scale_to_uchar (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x / 2 + (y / 2) * stride1;
      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
      dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);
    }
  }
};

struct OutputYV12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    dst0[x + y * stride0] = scale_to_uchar (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x / 2 + (y / 2) * stride1;
      dst1[pos] = scale_to_uchar (sample.z);
      dst2[pos] = scale_to_uchar (sample.y);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x / 2 + (y / 2) * stride1;
      dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);
      dst2[pos] = blend_uchar (dst2[pos], sample.y, sample.w);
    }
  }
};

struct OutputNV12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    dst0[x + y * stride0] = scale_to_uchar (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x + (y / 2) * stride1;
      dst1[pos] = scale_to_uchar (sample.y);
      dst1[pos + 1] = scale_to_uchar (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x + (y / 2) * stride1;
      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
      dst1[pos + 1] = blend_uchar (dst1[pos + 1], sample.z, sample.w);
    }
  }
};

struct OutputNV21 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    dst0[x + y * stride0] = scale_to_uchar (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x + (y / 2) * stride1;
      dst1[pos] = scale_to_uchar (sample.z);
      dst1[pos + 1] = scale_to_uchar (sample.y);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x + (y / 2) * stride1;
      dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);
      dst1[pos + 1] = blend_uchar (dst1[pos + 1], sample.y, sample.w);
    }
  }
};

struct OutputP010 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_ushort (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x * 2 + (y / 2) * stride1;
      *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.y);
      *(unsigned short *) &dst1[pos + 2] = scale_to_ushort (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_ushort (*target, sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x * 2 + (y / 2) * stride1;
      target = (unsigned short *) &dst1[pos];
      *target = blend_ushort (*target, sample.y, sample.w);
      target = (unsigned short *) &dst1[pos + 2];
      *target = blend_ushort (*target, sample.z, sample.w);
    }
  }
};

struct OutputI420_10 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_10bits (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x + (y / 2) * stride1;
      *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);
      *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_10bits (*target, sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x * 2 + (y / 2) * stride1;
      target = (unsigned short *) &dst1[pos];
      *target = blend_10bits (*target, sample.y, sample.w);
      target = (unsigned short *) &dst2[pos];
      *target = blend_10bits (*target, sample.z, sample.w);
    }
  }
};

struct OutputI420_12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_12bits (sample.x);
    if (x % 2 == 0 && y % 2 == 0) {
      unsigned int pos = x + (y / 2) * stride1;
      *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);
      *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_12bits (*target, sample.x, sample.w);
    if (x % 2 == 0 && y % 2 == 0) {
      pos = x * 2 + (y / 2) * stride1;
      target = (unsigned short *) &dst1[pos];
      *target = blend_12bits (*target, sample.y, sample.w);
      target = (unsigned short *) &dst2[pos];
      *target = blend_12bits (*target, sample.z, sample.w);
    }
  }
};

struct OutputY444 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = scale_to_uchar (sample.x);
    dst1[pos] = scale_to_uchar (sample.y);
    dst2[pos] = scale_to_uchar (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
    dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);
  }
};

struct OutputY444_10 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_10bits (sample.x);
    *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);
    *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_10bits (*target, sample.x, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_10bits (*target, sample.y, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_10bits (*target, sample.z, sample.w);
  }
};

struct OutputY444_12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_12bits (sample.x);
    *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);
    *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_12bits (*target, sample.x, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_12bits (*target, sample.y, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_12bits (*target, sample.z, sample.w);
  }
};

struct OutputY444_16 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_ushort (sample.x);
    *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.y);
    *(unsigned short *) &dst2[pos] = scale_to_ushort (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_ushort (*target, sample.x, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_ushort (*target, sample.y, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_ushort (*target, sample.z, sample.w);
  }
};

struct OutputRGBA : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.x);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.z);
    dst0[pos + 3] = scale_to_uchar (sample.w);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);
    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);
  }
};

struct OutputRGBx : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.x);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.z);
    dst0[pos + 3] = 255;
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);
    dst0[pos + 3] = 255;
  }
};

struct OutputBGRA : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.z);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.x);
    dst0[pos + 3] = scale_to_uchar (sample.w);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);
    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);
  }
};

struct OutputBGRx : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.z);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.x);
    dst0[pos + 3] = 255;
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);
    dst0[pos + 3] = 255;
  }
};

struct OutputARGB : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.w);
    dst0[pos + 1] = scale_to_uchar (sample.x);
    dst0[pos + 2] = scale_to_uchar (sample.y);
    dst0[pos + 3] = scale_to_uchar (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], 1.0, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.x, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.y, sample.w);
    dst0[pos + 3] = blend_uchar (dst0[pos + 3], sample.z, sample.w);
  }
};

struct OutputABGR : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.w);
    dst0[pos + 1] = scale_to_uchar (sample.z);
    dst0[pos + 2] = scale_to_uchar (sample.y);
    dst0[pos + 3] = scale_to_uchar (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], 1.0, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.z, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.y, sample.w);
    dst0[pos + 3] = blend_uchar (dst0[pos + 3], sample.x, sample.w);
  }
};

struct OutputRGB : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 3 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.x);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 3 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);
  }
};

struct OutputBGR : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 3 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.z);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 3 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);
  }
};

__device__ inline ushort3
unpack_rgb10a2 (unsigned int val)
{
  unsigned short r, g, b;
  r = (val & 0x3ff);
  r = (r << 6) | (r >> 4);
  g = ((val >> 10) & 0x3ff);
  g = (g << 6) | (g >> 4);
  b = ((val >> 20) & 0x3ff);
  b = (b << 6) | (b >> 4);
  return make_ushort3 (r, g, b);
}

struct OutputRGB10A2 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);
    unsigned int packed_rgb = alpha << 30;
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.x));
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.y)) << 10;
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.z)) << 20;
    *(unsigned int *) &dst0[x * 4 + y * stride0] = packed_rgb;
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int * target = (unsigned int *) &dst0[x * 4 + y * stride0];
    ushort3 val = unpack_rgb10a2 (*target);
    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);
    unsigned int packed_rgb = alpha << 30;
    packed_rgb |= ((unsigned int) blend_10bits (val.x, sample.x, sample.w));
    packed_rgb |= ((unsigned int) blend_10bits (val.y, sample.y, sample.w)) << 10;
    packed_rgb |= ((unsigned int) blend_10bits (val.z, sample.z, sample.w)) << 20;
    *target = packed_rgb;
  }
};

__device__ inline ushort3
unpack_bgr10a2 (unsigned int val)
{
  unsigned short r, g, b;
  b = (val & 0x3ff);
  b = (b << 6) | (b >> 4);
  g = ((val >> 10) & 0x3ff);
  g = (g << 6) | (g >> 4);
  r = ((val >> 20) & 0x3ff);
  r = (r << 6) | (r >> 4);
  return make_ushort3 (r, g, b);
}

struct OutputBGR10A2 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int alpha = (unsigned int) scale_to_2bits (sample.x);
    unsigned int packed_rgb = alpha << 30;
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.x)) << 20;
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.y)) << 10;
    packed_rgb |= ((unsigned int) scale_to_10bits (sample.z));
    *(unsigned int *) &dst0[x * 4 + y * stride0] = packed_rgb;
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int * target = (unsigned int *) &dst0[x * 4 + y * stride0];
    ushort3 val = unpack_bgr10a2 (*target);
    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);
    unsigned int packed_rgb = alpha << 30;
    packed_rgb |= ((unsigned int) blend_10bits (val.x, sample.x, sample.w)) << 20;
    packed_rgb |= ((unsigned int) blend_10bits (val.y, sample.y, sample.w)) << 10;
    packed_rgb |= ((unsigned int) blend_10bits (val.z, sample.z, sample.w));
    *target = packed_rgb;
  }
};

struct OutputY42B : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    dst0[x + y * stride0] = scale_to_uchar (sample.x);
    if (x % 2 == 0) {
      unsigned int pos = x / 2 + y * stride1;
      dst1[pos] = scale_to_uchar (sample.y);
      dst2[pos] = scale_to_uchar (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    if (x % 2 == 0) {
      pos = x / 2 + y * stride1;
      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
      dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);
    }
  }
};

struct OutputI422_10 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_10bits (sample.x);
    if (x % 2 == 0) {
      unsigned int pos = x + y * stride1;
      *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);
      *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_10bits (*target, sample.x, sample.w);
    if (x % 2 == 0) {
      pos = x / 2 + y * stride1;
      target = (unsigned short *) &dst1[pos];
      *target = blend_10bits (*target, sample.y, sample.w);
      target = (unsigned short *) &dst2[pos];
      *target = blend_10bits (*target, sample.z, sample.w);
    }
  }
};

struct OutputI422_12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_12bits (sample.x);
    if (x % 2 == 0) {
      unsigned int pos = x + y * stride1;
      *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);
      *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);
    }
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    unsigned int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_12bits (*target, sample.x, sample.w);
    if (x % 2 == 0) {
      pos = x / 2 + y * stride1;
      target = (unsigned short *) &dst1[pos];
      *target = blend_12bits (*target, sample.y, sample.w);
      target = (unsigned short *) &dst2[pos];
      *target = blend_12bits (*target, sample.z, sample.w);
    }
  }
};

struct OutputRGBP : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = scale_to_uchar (sample.x);
    dst1[pos] = scale_to_uchar (sample.y);
    dst2[pos] = scale_to_uchar (sample.z);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);
    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
    dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);
  }
};

struct OutputBGRP : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = scale_to_uchar (sample.z);
    dst1[pos] = scale_to_uchar (sample.y);
    dst2[pos] = scale_to_uchar (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);
    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);
    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);
  }
};

struct OutputGBR : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = scale_to_uchar (sample.y);
    dst1[pos] = scale_to_uchar (sample.z);
    dst2[pos] = scale_to_uchar (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.y, sample.w);
    dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);
    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);
  }
};

struct OutputGBR_10 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_10bits (sample.y);
    *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.z);
    *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_10bits (*target, sample.y, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_10bits (*target, sample.z, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_10bits (*target, sample.x, sample.w);
  }
};

struct OutputGBR_12 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_12bits (sample.y);
    *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.z);
    *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_12bits (*target, sample.y, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_12bits (*target, sample.z, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_12bits (*target, sample.x, sample.w);
  }
};

struct OutputGBR_16 : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    *(unsigned short *) &dst0[pos] = scale_to_ushort (sample.y);
    *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.z);
    *(unsigned short *) &dst2[pos] = scale_to_ushort (sample.x);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 2 + y * stride0;
    unsigned short * target = (unsigned short *) &dst0[pos];
    *target = blend_ushort (*target, sample.y, sample.w);
    target = (unsigned short *) &dst1[pos];
    *target = blend_ushort (*target, sample.z, sample.w);
    target = (unsigned short *) &dst2[pos];
    *target = blend_ushort (*target, sample.x, sample.w);
  }
};

struct OutputGBRA : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = scale_to_uchar (sample.y);
    dst1[pos] = scale_to_uchar (sample.z);
    dst2[pos] = scale_to_uchar (sample.x);
    dst3[pos] = scale_to_uchar (sample.w);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.y, sample.w);
    dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);
    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);
    dst3[pos] = blend_uchar (dst3[pos], 1.0, sample.w);
  }
};

struct OutputVUYA : public IOutput
{
  __device__ void
  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = scale_to_uchar (sample.z);
    dst0[pos + 1] = scale_to_uchar (sample.y);
    dst0[pos + 2] = scale_to_uchar (sample.x);
    dst0[pos + 3] = scale_to_uchar (sample.w);
  }

  __device__ void
  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,
      unsigned char * dst3, float4 sample, int x, int y, int stride0,
      int stride1)
  {
    int pos = x * 4 + y * stride0;
    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);
    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);
    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);
    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);
  }
};

__device__ inline float2
rotate_identity (float x, float y)
{
  return make_float2(x, y);
}

__device__ inline float2
rotate_90r (float x, float y)
{
  return make_float2(y, 1.0 - x);
}

__device__ inline float2
rotate_180 (float x, float y)
{
  return make_float2(1.0 - x, 1.0 - y);
}

__device__ inline float2
rotate_90l (float x, float y)
{
  return make_float2(1.0 - y, x);
}

__device__ inline float2
rotate_horiz (float x, float y)
{
  return make_float2(1.0 - x, y);
}

__device__ inline float2
rotate_vert (float x, float y)
{
  return make_float2(x, 1.0 - y);
}

__device__ inline float2
rotate_ul_lr (float x, float y)
{
  return make_float2(y, x);
}

__device__ inline float2
rotate_ur_ll (float x, float y)
{
  return make_float2(1.0 - y, 1.0 - x);
}
__device__ inline float2
do_rotate (float x, float y, int direction)
{
  switch (direction) {
    case 1:
      return rotate_90r (x, y);
    case 2:
      return rotate_180 (x, y);
    case 3:
      return rotate_90l (x, y);
    case 4:
      return rotate_horiz (x, y);
    case 5:
      return rotate_vert (x, y);
    case 6:
      return rotate_ul_lr (x, y);
    case 7:
      return rotate_ur_ll (x, y);
    default:
      return rotate_identity (x, y);
  }
}

extern "C" {
__global__ void
GstCudaConverterMain (cudaTextureObject_t tex0, cudaTextureObject_t tex1,
    cudaTextureObject_t tex2, cudaTextureObject_t tex3, unsigned char * dst0,
    unsigned char * dst1, unsigned char * dst2, unsigned char * dst3,
    int stride0, int stride1, ConstBuffer const_buf, int off_x, int off_y)
{
  ConvertSimple g_converter;
  SAMPLER g_sampler;
  OUTPUT g_output;
  int x_pos = blockIdx.x * blockDim.x + threadIdx.x + off_x;
  int y_pos = blockIdx.y * blockDim.y + threadIdx.y + off_y;
  float4 sample;
  if (x_pos >= const_buf.width || y_pos >= const_buf.height ||
      const_buf.view_width <= 0 || const_buf.view_height <= 0)
    return;
  if (x_pos < const_buf.left || x_pos >= const_buf.right ||
      y_pos < const_buf.top || y_pos >= const_buf.bottom) {
    if (!const_buf.fill_border)
      return;
    sample = make_float4 (const_buf.border_x, const_buf.border_y,
       const_buf.border_z, const_buf.border_w);
  } else {
    float x = (__int2float_rz (x_pos - const_buf.left) + 0.5) / const_buf.view_width;
    if (x < 0.0 || x > 1.0)
      return;
    float y = (__int2float_rz (y_pos - const_buf.top) + 0.5) / const_buf.view_height;
    if (y < 0.0 || y > 1.0)
      return;
    float2 rotated = do_rotate (x, y, const_buf.video_direction);
    float4 s = g_sampler.Execute (tex0, tex1, tex2, tex3, rotated.x, rotated.y);
    float3 rgb = make_float3 (s.x, s.y, s.z);
    float3 yuv;
    if (const_buf.do_convert)
      yuv = g_converter.Execute (rgb, &const_buf.matrix);
    else
      yuv = rgb;
    sample = make_float4 (yuv.x, yuv.y, yuv.z, s.w);
  }
  sample.w = sample.w * const_buf.alpha;
  if (!const_buf.do_blend) {
    g_output.Write (dst0, dst1, dst2, dst3, sample, x_pos, y_pos, stride0, stride1);
  } else {
    g_output.Blend (dst0, dst1, dst2, dst3, sample, x_pos, y_pos, stride0, stride1);
  }
}
}
#else
static const char GstCudaConverterMain_str[] =
"struct ColorMatrix\n"
"{\n"
"  float CoeffX[3];\n"
"  float CoeffY[3];\n"
"  float CoeffZ[3];\n"
"  float Offset[3];\n"
"  float Min[3];\n"
"  float Max[3];\n"
"};\n"
"\n"
"struct ConstBuffer\n"
"{\n"
"  ColorMatrix matrix;\n"
"  int width;\n"
"  int height;\n"
"  int left;\n"
"  int top;\n"
"  int right;\n"
"  int bottom;\n"
"  int view_width;\n"
"  int view_height;\n"
"  float border_x;\n"
"  float border_y;\n"
"  float border_z;\n"
"  float border_w;\n"
"  int fill_border;\n"
"  int video_direction;\n"
"  float alpha;\n"
"  int do_blend;\n"
"  int do_convert;\n"
"};\n"
"\n"
"__device__ inline float\n"
"dot (const float coeff[3], float3 val)\n"
"{\n"
"  return coeff[0] * val.x + coeff[1] * val.y + coeff[2] * val.z;\n"
"}\n"
"\n"
"__device__ inline float\n"
"clamp (float val, float min_val, float max_val)\n"
"{\n"
"  return max (min_val, min (val, max_val));\n"
"}\n"
"\n"
"__device__ inline float3\n"
"clamp3 (float3 val, const float min_val[3], const float max_val[3])\n"
"{\n"
"  return make_float3 (clamp (val.x, min_val[0], max_val[0]),\n"
"      clamp (val.y, min_val[1], max_val[2]),\n"
"      clamp (val.z, min_val[1], max_val[2]));\n"
"}\n"
"\n"
"__device__ inline unsigned char\n"
"scale_to_2bits (float val)\n"
"{\n"
"  return (unsigned short) __float2int_rz (val * 3.0);\n"
"}\n"
"\n"
"__device__ inline unsigned char\n"
"scale_to_uchar (float val)\n"
"{\n"
"  return (unsigned char) __float2int_rz (val * 255.0);\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"scale_to_ushort (float val)\n"
"{\n"
"  return (unsigned short) __float2int_rz (val * 65535.0);\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"scale_to_10bits (float val)\n"
"{\n"
"  return (unsigned short) __float2int_rz (val * 1023.0);\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"scale_to_12bits (float val)\n"
"{\n"
"  return (unsigned short) __float2int_rz (val * 4095.0);\n"
"}\n"
"\n"
"__device__ inline unsigned char\n"
"blend_uchar (unsigned char dst, float src, float src_alpha)\n"
"{\n"
"  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor\n"
"  float src_val = src * src_alpha;\n"
"  float dst_val = __int2float_rz (dst) / 255.0 * (1.0 - src_alpha);\n"
"  return scale_to_uchar(clamp(src_val + dst_val, 0, 1.0));\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"blend_ushort (unsigned short dst, float src, float src_alpha)\n"
"{\n"
"  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor\n"
"  float src_val = src * src_alpha;\n"
"  float dst_val = __int2float_rz (dst) / 65535.0 * (1.0 - src_alpha);\n"
"  return scale_to_ushort(clamp(src_val + dst_val, 0, 1.0));\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"blend_10bits (unsigned short dst, float src, float src_alpha)\n"
"{\n"
"  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor\n"
"  float src_val = src * src_alpha;\n"
"  float dst_val = __int2float_rz (dst) / 1023.0 * (1.0 - src_alpha);\n"
"  return scale_to_10bits(clamp(src_val + dst_val, 0, 1.0));\n"
"}\n"
"\n"
"__device__ inline unsigned short\n"
"blend_12bits (unsigned short dst, float src, float src_alpha)\n"
"{\n"
"  // DstColor' = SrcA * SrcColor + (1 - SrcA) DstColor\n"
"  float src_val = src * src_alpha;\n"
"  float dst_val = __int2float_rz (dst) / 4095.0 * (1.0 - src_alpha);\n"
"  return scale_to_12bits(clamp(src_val + dst_val, 0, 1.0));\n"
"}\n"
"\n"
"struct IConverter\n"
"{\n"
"  __device__ virtual float3\n"
"  Execute (float3 sample, const ColorMatrix * matrix) = 0;\n"
"};\n"
"\n"
"struct ConvertSimple : public IConverter\n"
"{\n"
"  __device__ float3\n"
"  Execute (float3 sample, const ColorMatrix * matrix)\n"
"  {\n"
"    float3 out;\n"
"    out.x = dot (matrix->CoeffX, sample);\n"
"    out.y = dot (matrix->CoeffY, sample);\n"
"    out.z = dot (matrix->CoeffZ, sample);\n"
"    out.x += matrix->Offset[0];\n"
"    out.y += matrix->Offset[1];\n"
"    out.z += matrix->Offset[2];\n"
"    return clamp3 (out, matrix->Min, matrix->Max);\n"
"  }\n"
"};\n"
"\n"
"struct ISampler\n"
"{\n"
"  __device__ virtual float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y) = 0;\n"
"};\n"
"\n"
"struct SampleI420 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float u = tex2D<float>(tex1, x, y);\n"
"    float v = tex2D<float>(tex2, x, y);\n"
"    return make_float4 (luma, u, v, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleYV12 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float u = tex2D<float>(tex2, x, y);\n"
"    float v = tex2D<float>(tex1, x, y);\n"
"    return make_float4 (luma, u, v, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleI420_10 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float u = tex2D<float>(tex1, x, y);\n"
"    float v = tex2D<float>(tex2, x, y);\n"
"    /* (1 << 6) to scale [0, 1.0) range */\n"
"    return make_float4 (luma * 64, u * 64, v * 64, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleI420_12 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float u = tex2D<float>(tex1, x, y);\n"
"    float v = tex2D<float>(tex2, x, y);\n"
"    /* (1 << 4) to scale [0, 1.0) range */\n"
"    return make_float4 (luma * 16, u * 16, v * 16, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleNV12 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float2 uv = tex2D<float2>(tex1, x, y);\n"
"    return make_float4 (luma, uv.x, uv.y, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleNV21 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float luma = tex2D<float>(tex0, x, y);\n"
"    float2 vu = tex2D<float2>(tex1, x, y);\n"
"    return make_float4 (luma, vu.y, vu.x, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleRGBA : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    return tex2D<float4>(tex0, x, y);\n"
"  }\n"
"};\n"
"\n"
"struct SampleBGRA : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float4 bgra = tex2D<float4>(tex0, x, y);\n"
"    return make_float4 (bgra.z, bgra.y, bgra.x, bgra.w);\n"
"  }\n"
"};\n"
"\n"
"struct SampleRGBx : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float4 rgbx = tex2D<float4>(tex0, x, y);\n"
"    rgbx.w = 1;\n"
"    return rgbx;\n"
"  }\n"
"};\n"
"\n"
"struct SampleBGRx : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float4 bgrx = tex2D<float4>(tex0, x, y);\n"
"    return make_float4 (bgrx.z, bgrx.y, bgrx.x, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleARGB : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"   float4 argb = tex2D<float4>(tex0, x, y);\n"
"   return make_float4 (argb.y, argb.z, argb.w, argb.x);\n"
"  }\n"
"};\n"
"\n"
"struct SampleABGR : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"   float4 abgr = tex2D<float4>(tex0, x, y);\n"
"   return make_float4 (abgr.w, abgr.z, abgr.y, abgr.x);\n"
"  }\n"
"};\n"
"\n"
"struct SampleRGBP : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float r = tex2D<float>(tex0, x, y);\n"
"    float g = tex2D<float>(tex1, x, y);\n"
"    float b = tex2D<float>(tex2, x, y);\n"
"    return make_float4 (r, g, b, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleBGRP : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float b = tex2D<float>(tex0, x, y);\n"
"    float g = tex2D<float>(tex1, x, y);\n"
"    float r = tex2D<float>(tex2, x, y);\n"
"    return make_float4 (r, g, b, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleGBR : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float g = tex2D<float>(tex0, x, y);\n"
"    float b = tex2D<float>(tex1, x, y);\n"
"    float r = tex2D<float>(tex2, x, y);\n"
"    return make_float4 (r, g, b, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleGBR_10 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float g = tex2D<float>(tex0, x, y);\n"
"    float b = tex2D<float>(tex1, x, y);\n"
"    float r = tex2D<float>(tex2, x, y);\n"
"    /* (1 << 6) to scale [0, 1.0) range */\n"
"    return make_float4 (r * 64, g * 64, b * 64, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleGBR_12 : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float g = tex2D<float>(tex0, x, y);\n"
"    float b = tex2D<float>(tex1, x, y);\n"
"    float r = tex2D<float>(tex2, x, y);\n"
"    /* (1 << 4) to scale [0, 1.0) range */\n"
"    return make_float4 (r * 16, g * 16, b * 16, 1);\n"
"  }\n"
"};\n"
"\n"
"struct SampleGBRA : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float g = tex2D<float>(tex0, x, y);\n"
"    float b = tex2D<float>(tex1, x, y);\n"
"    float r = tex2D<float>(tex2, x, y);\n"
"    float a = tex2D<float>(tex3, x, y);\n"
"    return make_float4 (r, g, b, a);\n"
"  }\n"
"};\n"
"\n"
"struct SampleVUYA : public ISampler\n"
"{\n"
"  __device__ float4\n"
"  Execute (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"      cudaTextureObject_t tex2, cudaTextureObject_t tex3, float x, float y)\n"
"  {\n"
"    float4 vuya = tex2D<float4>(tex0, x, y);\n"
"    return make_float4 (vuya.z, vuya.y, vuya.x, vuya.w);\n"
"  }\n"
"};\n"
"\n"
"struct IOutput\n"
"{\n"
"  __device__ virtual void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1) = 0;\n"
"\n"
"  __device__ virtual void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"        unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"        int stride1) = 0;\n"
"};\n"
"\n"
"struct OutputI420 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    dst0[x + y * stride0] = scale_to_uchar (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x / 2 + (y / 2) * stride1;\n"
"      dst1[pos] = scale_to_uchar (sample.y);\n"
"      dst2[pos] = scale_to_uchar (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x / 2 + (y / 2) * stride1;\n"
"      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"      dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputYV12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    dst0[x + y * stride0] = scale_to_uchar (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x / 2 + (y / 2) * stride1;\n"
"      dst1[pos] = scale_to_uchar (sample.z);\n"
"      dst2[pos] = scale_to_uchar (sample.y);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x / 2 + (y / 2) * stride1;\n"
"      dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);\n"
"      dst2[pos] = blend_uchar (dst2[pos], sample.y, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputNV12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    dst0[x + y * stride0] = scale_to_uchar (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x + (y / 2) * stride1;\n"
"      dst1[pos] = scale_to_uchar (sample.y);\n"
"      dst1[pos + 1] = scale_to_uchar (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x + (y / 2) * stride1;\n"
"      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"      dst1[pos + 1] = blend_uchar (dst1[pos + 1], sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputNV21 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    dst0[x + y * stride0] = scale_to_uchar (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x + (y / 2) * stride1;\n"
"      dst1[pos] = scale_to_uchar (sample.z);\n"
"      dst1[pos + 1] = scale_to_uchar (sample.y);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x + (y / 2) * stride1;\n"
"      dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);\n"
"      dst1[pos + 1] = blend_uchar (dst1[pos + 1], sample.y, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputP010 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_ushort (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x * 2 + (y / 2) * stride1;\n"
"      *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.y);\n"
"      *(unsigned short *) &dst1[pos + 2] = scale_to_ushort (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_ushort (*target, sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x * 2 + (y / 2) * stride1;\n"
"      target = (unsigned short *) &dst1[pos];\n"
"      *target = blend_ushort (*target, sample.y, sample.w);\n"
"      target = (unsigned short *) &dst1[pos + 2];\n"
"      *target = blend_ushort (*target, sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputI420_10 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_10bits (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x + (y / 2) * stride1;\n"
"      *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);\n"
"      *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_10bits (*target, sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x * 2 + (y / 2) * stride1;\n"
"      target = (unsigned short *) &dst1[pos];\n"
"      *target = blend_10bits (*target, sample.y, sample.w);\n"
"      target = (unsigned short *) &dst2[pos];\n"
"      *target = blend_10bits (*target, sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputI420_12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_12bits (sample.x);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      unsigned int pos = x + (y / 2) * stride1;\n"
"      *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);\n"
"      *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_12bits (*target, sample.x, sample.w);\n"
"    if (x % 2 == 0 && y % 2 == 0) {\n"
"      pos = x * 2 + (y / 2) * stride1;\n"
"      target = (unsigned short *) &dst1[pos];\n"
"      *target = blend_12bits (*target, sample.y, sample.w);\n"
"      target = (unsigned short *) &dst2[pos];\n"
"      *target = blend_12bits (*target, sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputY444 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.x);\n"
"    dst1[pos] = scale_to_uchar (sample.y);\n"
"    dst2[pos] = scale_to_uchar (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"    dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputY444_10 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_10bits (sample.x);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_10bits (*target, sample.x, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_10bits (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_10bits (*target, sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputY444_12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_12bits (sample.x);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_12bits (*target, sample.x, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_12bits (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_12bits (*target, sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputY444_16 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_ushort (sample.x);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.y);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_ushort (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_ushort (*target, sample.x, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_ushort (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_ushort (*target, sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputRGBA : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 3] = scale_to_uchar (sample.w);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);\n"
"    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputRGBx : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 3] = 255;\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);\n"
"    dst0[pos + 3] = 255;\n"
"  }\n"
"};\n"
"\n"
"struct OutputBGRA : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 3] = scale_to_uchar (sample.w);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);\n"
"    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputBGRx : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 3] = 255;\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);\n"
"    dst0[pos + 3] = 255;\n"
"  }\n"
"};\n"
"\n"
"struct OutputARGB : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.w);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 3] = scale_to_uchar (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], 1.0, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.x, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.y, sample.w);\n"
"    dst0[pos + 3] = blend_uchar (dst0[pos + 3], sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputABGR : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.w);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 3] = scale_to_uchar (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], 1.0, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.z, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.y, sample.w);\n"
"    dst0[pos + 3] = blend_uchar (dst0[pos + 3], sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputRGB : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 3 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 3 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputBGR : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 3 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 3 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"__device__ inline ushort3\n"
"unpack_rgb10a2 (unsigned int val)\n"
"{\n"
"  unsigned short r, g, b;\n"
"  r = (val & 0x3ff);\n"
"  r = (r << 6) | (r >> 4);\n"
"  g = ((val >> 10) & 0x3ff);\n"
"  g = (g << 6) | (g >> 4);\n"
"  b = ((val >> 20) & 0x3ff);\n"
"  b = (b << 6) | (b >> 4);\n"
"  return make_ushort3 (r, g, b);\n"
"}\n"
"\n"
"struct OutputRGB10A2 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);\n"
"    unsigned int packed_rgb = alpha << 30;\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.x));\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.y)) << 10;\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.z)) << 20;\n"
"    *(unsigned int *) &dst0[x * 4 + y * stride0] = packed_rgb;\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int * target = (unsigned int *) &dst0[x * 4 + y * stride0];\n"
"    ushort3 val = unpack_rgb10a2 (*target);\n"
"    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);\n"
"    unsigned int packed_rgb = alpha << 30;\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.x, sample.x, sample.w));\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.y, sample.y, sample.w)) << 10;\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.z, sample.z, sample.w)) << 20;\n"
"    *target = packed_rgb;\n"
"  }\n"
"};\n"
"\n"
"__device__ inline ushort3\n"
"unpack_bgr10a2 (unsigned int val)\n"
"{\n"
"  unsigned short r, g, b;\n"
"  b = (val & 0x3ff);\n"
"  b = (b << 6) | (b >> 4);\n"
"  g = ((val >> 10) & 0x3ff);\n"
"  g = (g << 6) | (g >> 4);\n"
"  r = ((val >> 20) & 0x3ff);\n"
"  r = (r << 6) | (r >> 4);\n"
"  return make_ushort3 (r, g, b);\n"
"}\n"
"\n"
"struct OutputBGR10A2 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int alpha = (unsigned int) scale_to_2bits (sample.x);\n"
"    unsigned int packed_rgb = alpha << 30;\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.x)) << 20;\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.y)) << 10;\n"
"    packed_rgb |= ((unsigned int) scale_to_10bits (sample.z));\n"
"    *(unsigned int *) &dst0[x * 4 + y * stride0] = packed_rgb;\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int * target = (unsigned int *) &dst0[x * 4 + y * stride0];\n"
"    ushort3 val = unpack_bgr10a2 (*target);\n"
"    unsigned int alpha = (unsigned int) scale_to_2bits (sample.w);\n"
"    unsigned int packed_rgb = alpha << 30;\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.x, sample.x, sample.w)) << 20;\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.y, sample.y, sample.w)) << 10;\n"
"    packed_rgb |= ((unsigned int) blend_10bits (val.z, sample.z, sample.w));\n"
"    *target = packed_rgb;\n"
"  }\n"
"};\n"
"\n"
"struct OutputY42B : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    dst0[x + y * stride0] = scale_to_uchar (sample.x);\n"
"    if (x % 2 == 0) {\n"
"      unsigned int pos = x / 2 + y * stride1;\n"
"      dst1[pos] = scale_to_uchar (sample.y);\n"
"      dst2[pos] = scale_to_uchar (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    if (x % 2 == 0) {\n"
"      pos = x / 2 + y * stride1;\n"
"      dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"      dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputI422_10 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_10bits (sample.x);\n"
"    if (x % 2 == 0) {\n"
"      unsigned int pos = x + y * stride1;\n"
"      *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.y);\n"
"      *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_10bits (*target, sample.x, sample.w);\n"
"    if (x % 2 == 0) {\n"
"      pos = x / 2 + y * stride1;\n"
"      target = (unsigned short *) &dst1[pos];\n"
"      *target = blend_10bits (*target, sample.y, sample.w);\n"
"      target = (unsigned short *) &dst2[pos];\n"
"      *target = blend_10bits (*target, sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputI422_12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    *(unsigned short *) &dst0[x * 2 + y * stride0] = scale_to_12bits (sample.x);\n"
"    if (x % 2 == 0) {\n"
"      unsigned int pos = x + y * stride1;\n"
"      *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.y);\n"
"      *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.z);\n"
"    }\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    unsigned int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_12bits (*target, sample.x, sample.w);\n"
"    if (x % 2 == 0) {\n"
"      pos = x / 2 + y * stride1;\n"
"      target = (unsigned short *) &dst1[pos];\n"
"      *target = blend_12bits (*target, sample.y, sample.w);\n"
"      target = (unsigned short *) &dst2[pos];\n"
"      *target = blend_12bits (*target, sample.z, sample.w);\n"
"    }\n"
"  }\n"
"};\n"
"\n"
"struct OutputRGBP : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.x);\n"
"    dst1[pos] = scale_to_uchar (sample.y);\n"
"    dst2[pos] = scale_to_uchar (sample.z);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.x, sample.w);\n"
"    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"    dst2[pos] = blend_uchar (dst2[pos], sample.z, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputBGRP : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.z);\n"
"    dst1[pos] = scale_to_uchar (sample.y);\n"
"    dst2[pos] = scale_to_uchar (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);\n"
"    dst1[pos] = blend_uchar (dst1[pos], sample.y, sample.w);\n"
"    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputGBR : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.y);\n"
"    dst1[pos] = scale_to_uchar (sample.z);\n"
"    dst2[pos] = scale_to_uchar (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.y, sample.w);\n"
"    dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);\n"
"    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputGBR_10 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_10bits (sample.y);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_10bits (sample.z);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_10bits (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_10bits (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_10bits (*target, sample.z, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_10bits (*target, sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputGBR_12 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_12bits (sample.y);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_12bits (sample.z);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_12bits (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_12bits (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_12bits (*target, sample.z, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_12bits (*target, sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputGBR_16 : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    *(unsigned short *) &dst0[pos] = scale_to_ushort (sample.y);\n"
"    *(unsigned short *) &dst1[pos] = scale_to_ushort (sample.z);\n"
"    *(unsigned short *) &dst2[pos] = scale_to_ushort (sample.x);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 2 + y * stride0;\n"
"    unsigned short * target = (unsigned short *) &dst0[pos];\n"
"    *target = blend_ushort (*target, sample.y, sample.w);\n"
"    target = (unsigned short *) &dst1[pos];\n"
"    *target = blend_ushort (*target, sample.z, sample.w);\n"
"    target = (unsigned short *) &dst2[pos];\n"
"    *target = blend_ushort (*target, sample.x, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputGBRA : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.y);\n"
"    dst1[pos] = scale_to_uchar (sample.z);\n"
"    dst2[pos] = scale_to_uchar (sample.x);\n"
"    dst3[pos] = scale_to_uchar (sample.w);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.y, sample.w);\n"
"    dst1[pos] = blend_uchar (dst1[pos], sample.z, sample.w);\n"
"    dst2[pos] = blend_uchar (dst2[pos], sample.x, sample.w);\n"
"    dst3[pos] = blend_uchar (dst3[pos], 1.0, sample.w);\n"
"  }\n"
"};\n"
"\n"
"struct OutputVUYA : public IOutput\n"
"{\n"
"  __device__ void\n"
"  Write (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = scale_to_uchar (sample.z);\n"
"    dst0[pos + 1] = scale_to_uchar (sample.y);\n"
"    dst0[pos + 2] = scale_to_uchar (sample.x);\n"
"    dst0[pos + 3] = scale_to_uchar (sample.w);\n"
"  }\n"
"\n"
"  __device__ void\n"
"  Blend (unsigned char * dst0, unsigned char * dst1, unsigned char * dst2,\n"
"      unsigned char * dst3, float4 sample, int x, int y, int stride0,\n"
"      int stride1)\n"
"  {\n"
"    int pos = x * 4 + y * stride0;\n"
"    dst0[pos] = blend_uchar (dst0[pos], sample.z, sample.w);\n"
"    dst0[pos + 1] = blend_uchar (dst0[pos + 1], sample.y, sample.w);\n"
"    dst0[pos + 2] = blend_uchar (dst0[pos + 2], sample.x, sample.w);\n"
"    dst0[pos + 3] = blend_uchar (dst0[pos + 3], 1.0, sample.w);\n"
"  }\n"
"};\n"
"\n"
"__device__ inline float2\n"
"rotate_identity (float x, float y)\n"
"{\n"
"  return make_float2(x, y);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_90r (float x, float y)\n"
"{\n"
"  return make_float2(y, 1.0 - x);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_180 (float x, float y)\n"
"{\n"
"  return make_float2(1.0 - x, 1.0 - y);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_90l (float x, float y)\n"
"{\n"
"  return make_float2(1.0 - y, x);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_horiz (float x, float y)\n"
"{\n"
"  return make_float2(1.0 - x, y);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_vert (float x, float y)\n"
"{\n"
"  return make_float2(x, 1.0 - y);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_ul_lr (float x, float y)\n"
"{\n"
"  return make_float2(y, x);\n"
"}\n"
"\n"
"__device__ inline float2\n"
"rotate_ur_ll (float x, float y)\n"
"{\n"
"  return make_float2(1.0 - y, 1.0 - x);\n"
"}\n"
"__device__ inline float2\n"
"do_rotate (float x, float y, int direction)\n"
"{\n"
"  switch (direction) {\n"
"    case 1:\n"
"      return rotate_90r (x, y);\n"
"    case 2:\n"
"      return rotate_180 (x, y);\n"
"    case 3:\n"
"      return rotate_90l (x, y);\n"
"    case 4:\n"
"      return rotate_horiz (x, y);\n"
"    case 5:\n"
"      return rotate_vert (x, y);\n"
"    case 6:\n"
"      return rotate_ul_lr (x, y);\n"
"    case 7:\n"
"      return rotate_ur_ll (x, y);\n"
"    default:\n"
"      return rotate_identity (x, y);\n"
"  }\n"
"}\n"
"\n"
"extern \"C\" {\n"
"__global__ void\n"
"GstCudaConverterMain (cudaTextureObject_t tex0, cudaTextureObject_t tex1,\n"
"    cudaTextureObject_t tex2, cudaTextureObject_t tex3, unsigned char * dst0,\n"
"    unsigned char * dst1, unsigned char * dst2, unsigned char * dst3,\n"
"    int stride0, int stride1, ConstBuffer const_buf, int off_x, int off_y)\n"
"{\n"
"  ConvertSimple g_converter;\n"
"  SAMPLER g_sampler;\n"
"  OUTPUT g_output;\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x + off_x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y + off_y;\n"
"  float4 sample;\n"
"  if (x_pos >= const_buf.width || y_pos >= const_buf.height ||\n"
"      const_buf.view_width <= 0 || const_buf.view_height <= 0)\n"
"    return;\n"
"  if (x_pos < const_buf.left || x_pos >= const_buf.right ||\n"
"      y_pos < const_buf.top || y_pos >= const_buf.bottom) {\n"
"    if (!const_buf.fill_border)\n"
"      return;\n"
"    sample = make_float4 (const_buf.border_x, const_buf.border_y,\n"
"       const_buf.border_z, const_buf.border_w);\n"
"  } else {\n"
"    float x = (__int2float_rz (x_pos - const_buf.left) + 0.5) / const_buf.view_width;\n"
"    if (x < 0.0 || x > 1.0)\n"
"      return;\n"
"    float y = (__int2float_rz (y_pos - const_buf.top) + 0.5) / const_buf.view_height;\n"
"    if (y < 0.0 || y > 1.0)\n"
"      return;\n"
"    float2 rotated = do_rotate (x, y, const_buf.video_direction);\n"
"    float4 s = g_sampler.Execute (tex0, tex1, tex2, tex3, rotated.x, rotated.y);\n"
"    float3 rgb = make_float3 (s.x, s.y, s.z);\n"
"    float3 yuv;\n"
"    if (const_buf.do_convert)\n"
"      yuv = g_converter.Execute (rgb, &const_buf.matrix);\n"
"    else\n"
"      yuv = rgb;\n"
"    sample = make_float4 (yuv.x, yuv.y, yuv.z, s.w);\n"
"  }\n"
"  sample.w = sample.w * const_buf.alpha;\n"
"  if (!const_buf.do_blend) {\n"
"    g_output.Write (dst0, dst1, dst2, dst3, sample, x_pos, y_pos, stride0, stride1);\n"
"  } else {\n"
"    g_output.Blend (dst0, dst1, dst2, dst3, sample, x_pos, y_pos, stride0, stride1);\n"
"  }\n"
"}\n"
"}\n"
"\n";
#endif