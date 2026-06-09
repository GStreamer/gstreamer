/* GStreamer
 * Copyright (C) 2026 Felix-Gong <gongxiaofei24@iscas.ac.cn>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "audio-resampler-rvv.h"

#include <riscv_vector.h>
#include <gst/gstcpuid.h>

/* ===== gint16 inner_product functions ===== */

static void
inner_product_gint16_full_1_rvv (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint32 sum = 0;
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e16m4 (len - i);
    vint16m4_t va = __riscv_vle16_v_i16m4 (a + i, vl);
    vint16m4_t vb = __riscv_vle16_v_i16m4 (b + i, vl);
    vint32m8_t prod = __riscv_vwmul_vv_i32m8 (va, vb, vl);
    vint32m1_t acc = __riscv_vmv_v_x_i32m1 (0, 1);
    vint32m1_t red = __riscv_vredsum_vs_i32m8_i32m1 (prod, acc, vl);
    sum += __riscv_vmv_x_s_i32m1_i32 (red);
  }

  sum = (sum + ((gint32) 1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  *o = CLAMP (sum, -((gint32) 1 << 15), ((gint32) 1 << 15) - 1);
}

static void
inner_product_gint16_linear_1_rvv (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint32 res0 = 0, res1 = 0;
  const gint16 *c[2] = { (gint16 *) ((gint8 *) b + 0 * bstride),
    (gint16 *) ((gint8 *) b + 1 * bstride)
  };
  gint16 c0 = icoeff[0];
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e16m4 (len - i);
    vint16m4_t va = __riscv_vle16_v_i16m4 (a + i, vl);
    vint16m4_t vc0 = __riscv_vle16_v_i16m4 (c[0] + i, vl);
    vint16m4_t vc1 = __riscv_vle16_v_i16m4 (c[1] + i, vl);
    vint32m8_t p0 = __riscv_vwmul_vv_i32m8 (va, vc0, vl);
    vint32m8_t p1 = __riscv_vwmul_vv_i32m8 (va, vc1, vl);
    vint32m1_t acc = __riscv_vmv_v_x_i32m1 (0, 1);
    vint32m1_t r0 = __riscv_vredsum_vs_i32m8_i32m1 (p0, acc, vl);
    vint32m1_t r1 = __riscv_vredsum_vs_i32m8_i32m1 (p1, acc, vl);
    res0 += __riscv_vmv_x_s_i32m1_i32 (r0);
    res1 += __riscv_vmv_x_s_i32m1_i32 (r1);
  }

  res0 = res0 >> PRECISION_S16;
  res1 = res1 >> PRECISION_S16;
  res0 = ((gint32) (gint16) res0 - (gint32) (gint16) res1) * c0 +
      ((gint32) (gint16) res1 << PRECISION_S16);
  res0 = (res0 + ((gint32) 1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  *o = CLAMP (res0, -((gint32) 1 << 15), ((gint32) 1 << 15) - 1);
}

static void
inner_product_gint16_cubic_1_rvv (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint32 res[4] = { 0, 0, 0, 0 };
  const gint16 *c[4] = { (gint16 *) ((gint8 *) b + 0 * bstride),
    (gint16 *) ((gint8 *) b + 1 * bstride),
    (gint16 *) ((gint8 *) b + 2 * bstride),
    (gint16 *) ((gint8 *) b + 3 * bstride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e16m4 (len - i);
    vint16m4_t va = __riscv_vle16_v_i16m4 (a + i, vl);
    vint32m1_t acc = __riscv_vmv_v_x_i32m1 (0, 1);
    vint16m4_t vc;
    vint32m8_t p;
    vint32m1_t r;

    vc = __riscv_vle16_v_i16m4 (c[0] + i, vl);
    p = __riscv_vwmul_vv_i32m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i32m8_i32m1 (p, acc, vl);
    res[0] += __riscv_vmv_x_s_i32m1_i32 (r);

    vc = __riscv_vle16_v_i16m4 (c[1] + i, vl);
    p = __riscv_vwmul_vv_i32m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i32m8_i32m1 (p, acc, vl);
    res[1] += __riscv_vmv_x_s_i32m1_i32 (r);

    vc = __riscv_vle16_v_i16m4 (c[2] + i, vl);
    p = __riscv_vwmul_vv_i32m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i32m8_i32m1 (p, acc, vl);
    res[2] += __riscv_vmv_x_s_i32m1_i32 (r);

    vc = __riscv_vle16_v_i16m4 (c[3] + i, vl);
    p = __riscv_vwmul_vv_i32m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i32m8_i32m1 (p, acc, vl);
    res[3] += __riscv_vmv_x_s_i32m1_i32 (r);
  }

  res[0] = (gint32) (gint16) (res[0] >> PRECISION_S16) * (gint32) icoeff[0] +
      (gint32) (gint16) (res[1] >> PRECISION_S16) * (gint32) icoeff[1] +
      (gint32) (gint16) (res[2] >> PRECISION_S16) * (gint32) icoeff[2] +
      (gint32) (gint16) (res[3] >> PRECISION_S16) * (gint32) icoeff[3];
  res[0] = (res[0] + ((gint32) 1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  *o = CLAMP (res[0], -((gint32) 1 << 15), ((gint32) 1 << 15) - 1);
}

/* ===== gint32 inner_product functions ===== */

static void
inner_product_gint32_full_1_rvv (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
{
  gint64 sum = 0;
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vint32m4_t va = __riscv_vle32_v_i32m4 (a + i, vl);
    vint32m4_t vb = __riscv_vle32_v_i32m4 (b + i, vl);
    vint64m8_t prod = __riscv_vwmul_vv_i64m8 (va, vb, vl);
    vint64m1_t acc = __riscv_vmv_v_x_i64m1 (0, 1);
    vint64m1_t red = __riscv_vredsum_vs_i64m8_i64m1 (prod, acc, vl);
    sum += __riscv_vmv_x_s_i64m1_i64 (red);
  }

  sum = (sum + ((gint64) 1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (sum, -((gint64) 1 << 31), ((gint64) 1 << 31) - 1);
}

static void
inner_product_gint32_linear_1_rvv (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
{
  gint64 res0 = 0, res1 = 0;
  const gint32 *c[2] = { (gint32 *) ((gint8 *) b + 0 * bstride),
    (gint32 *) ((gint8 *) b + 1 * bstride)
  };
  gint32 c0 = icoeff[0];
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vint32m4_t va = __riscv_vle32_v_i32m4 (a + i, vl);
    vint32m4_t vc0 = __riscv_vle32_v_i32m4 (c[0] + i, vl);
    vint32m4_t vc1 = __riscv_vle32_v_i32m4 (c[1] + i, vl);
    vint64m8_t p0 = __riscv_vwmul_vv_i64m8 (va, vc0, vl);
    vint64m8_t p1 = __riscv_vwmul_vv_i64m8 (va, vc1, vl);
    vint64m1_t acc = __riscv_vmv_v_x_i64m1 (0, 1);
    vint64m1_t r0 = __riscv_vredsum_vs_i64m8_i64m1 (p0, acc, vl);
    vint64m1_t r1 = __riscv_vredsum_vs_i64m8_i64m1 (p1, acc, vl);
    res0 += __riscv_vmv_x_s_i64m1_i64 (r0);
    res1 += __riscv_vmv_x_s_i64m1_i64 (r1);
  }

  res0 = res0 >> PRECISION_S32;
  res1 = res1 >> PRECISION_S32;
  res0 = ((gint64) (gint32) res0 - (gint64) (gint32) res1) * c0 +
      ((gint64) (gint32) res1 << PRECISION_S32);
  res0 = (res0 + ((gint64) 1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res0, -((gint64) 1 << 31), ((gint64) 1 << 31) - 1);
}

static void
inner_product_gint32_cubic_1_rvv (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
{
  gint64 res[4] = { 0, 0, 0, 0 };
  const gint32 *c[4] = { (gint32 *) ((gint8 *) b + 0 * bstride),
    (gint32 *) ((gint8 *) b + 1 * bstride),
    (gint32 *) ((gint8 *) b + 2 * bstride),
    (gint32 *) ((gint8 *) b + 3 * bstride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vint32m4_t va = __riscv_vle32_v_i32m4 (a + i, vl);
    vint64m1_t acc = __riscv_vmv_v_x_i64m1 (0, 1);
    vint32m4_t vc;
    vint64m8_t p;
    vint64m1_t r;

    vc = __riscv_vle32_v_i32m4 (c[0] + i, vl);
    p = __riscv_vwmul_vv_i64m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i64m8_i64m1 (p, acc, vl);
    res[0] += __riscv_vmv_x_s_i64m1_i64 (r);

    vc = __riscv_vle32_v_i32m4 (c[1] + i, vl);
    p = __riscv_vwmul_vv_i64m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i64m8_i64m1 (p, acc, vl);
    res[1] += __riscv_vmv_x_s_i64m1_i64 (r);

    vc = __riscv_vle32_v_i32m4 (c[2] + i, vl);
    p = __riscv_vwmul_vv_i64m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i64m8_i64m1 (p, acc, vl);
    res[2] += __riscv_vmv_x_s_i64m1_i64 (r);

    vc = __riscv_vle32_v_i32m4 (c[3] + i, vl);
    p = __riscv_vwmul_vv_i64m8 (va, vc, vl);
    r = __riscv_vredsum_vs_i64m8_i64m1 (p, acc, vl);
    res[3] += __riscv_vmv_x_s_i64m1_i64 (r);
  }

  res[0] = (gint64) (gint32) (res[0] >> PRECISION_S32) * (gint64) icoeff[0] +
      (gint64) (gint32) (res[1] >> PRECISION_S32) * (gint64) icoeff[1] +
      (gint64) (gint32) (res[2] >> PRECISION_S32) * (gint64) icoeff[2] +
      (gint64) (gint32) (res[3] >> PRECISION_S32) * (gint64) icoeff[3];
  res[0] = (res[0] + ((gint64) 1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res[0], -((gint64) 1 << 31), ((gint64) 1 << 31) - 1);
}

/* ===== gfloat inner_product functions ===== */

static void
inner_product_gfloat_full_1_rvv (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
{
  gfloat sum = 0.0f;
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vfloat32m4_t va = __riscv_vle32_v_f32m4 (a + i, vl);
    vfloat32m4_t vb = __riscv_vle32_v_f32m4 (b + i, vl);
    vfloat32m4_t prod = __riscv_vfmul_vv_f32m4 (va, vb, vl);
    vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1 (0.0f, 1);
    vfloat32m1_t red = __riscv_vfredosum_vs_f32m4_f32m1 (prod, acc, vl);
    sum += __riscv_vfmv_f_s_f32m1_f32 (red);
  }

  *o = sum;
}

static void
inner_product_gfloat_linear_1_rvv (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
{
  gfloat res0 = 0.0f, res1 = 0.0f;
  const gfloat *c[2] = { (gfloat *) ((gint8 *) b + 0 * bstride),
    (gfloat *) ((gint8 *) b + 1 * bstride)
  };
  gfloat c0 = icoeff[0];
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vfloat32m4_t va = __riscv_vle32_v_f32m4 (a + i, vl);
    vfloat32m4_t vc0 = __riscv_vle32_v_f32m4 (c[0] + i, vl);
    vfloat32m4_t vc1 = __riscv_vle32_v_f32m4 (c[1] + i, vl);
    vfloat32m4_t p0 = __riscv_vfmul_vv_f32m4 (va, vc0, vl);
    vfloat32m4_t p1 = __riscv_vfmul_vv_f32m4 (va, vc1, vl);
    vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1 (0.0f, 1);
    vfloat32m1_t r0 = __riscv_vfredosum_vs_f32m4_f32m1 (p0, acc, vl);
    vfloat32m1_t r1 = __riscv_vfredosum_vs_f32m4_f32m1 (p1, acc, vl);
    res0 += __riscv_vfmv_f_s_f32m1_f32 (r0);
    res1 += __riscv_vfmv_f_s_f32m1_f32 (r1);
  }

  *o = (res0 - res1) * c0 + res1;
}

static void
inner_product_gfloat_cubic_1_rvv (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
{
  gfloat res[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  const gfloat *c[4] = { (gfloat *) ((gint8 *) b + 0 * bstride),
    (gfloat *) ((gint8 *) b + 1 * bstride),
    (gfloat *) ((gint8 *) b + 2 * bstride),
    (gfloat *) ((gint8 *) b + 3 * bstride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vfloat32m4_t va = __riscv_vle32_v_f32m4 (a + i, vl);
    vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1 (0.0f, 1);
    vfloat32m4_t vc;
    vfloat32m4_t p;
    vfloat32m1_t r;

    vc = __riscv_vle32_v_f32m4 (c[0] + i, vl);
    p = __riscv_vfmul_vv_f32m4 (va, vc, vl);
    r = __riscv_vfredosum_vs_f32m4_f32m1 (p, acc, vl);
    res[0] += __riscv_vfmv_f_s_f32m1_f32 (r);

    vc = __riscv_vle32_v_f32m4 (c[1] + i, vl);
    p = __riscv_vfmul_vv_f32m4 (va, vc, vl);
    r = __riscv_vfredosum_vs_f32m4_f32m1 (p, acc, vl);
    res[1] += __riscv_vfmv_f_s_f32m1_f32 (r);

    vc = __riscv_vle32_v_f32m4 (c[2] + i, vl);
    p = __riscv_vfmul_vv_f32m4 (va, vc, vl);
    r = __riscv_vfredosum_vs_f32m4_f32m1 (p, acc, vl);
    res[2] += __riscv_vfmv_f_s_f32m1_f32 (r);

    vc = __riscv_vle32_v_f32m4 (c[3] + i, vl);
    p = __riscv_vfmul_vv_f32m4 (va, vc, vl);
    r = __riscv_vfredosum_vs_f32m4_f32m1 (p, acc, vl);
    res[3] += __riscv_vfmv_f_s_f32m1_f32 (r);
  }

  *o = res[0] * icoeff[0] + res[1] * icoeff[1] +
      res[2] * icoeff[2] + res[3] * icoeff[3];
}

/* ===== gint16 interpolate functions ===== */

void
interpolate_gint16_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint16 *o = op, *a = ap, *ic = icp;
  gint32 c0 = ic[0];
  const gint16 *c[2] = { (gint16 *) ((gint8 *) a + 0 * astride),
    (gint16 *) ((gint8 *) a + 1 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e16m4 (len - i);
    vint16m4_t vc0 = __riscv_vle16_v_i16m4 (c[0] + i, vl);
    vint16m4_t vc1 = __riscv_vle16_v_i16m4 (c[1] + i, vl);

    vint32m8_t diff = __riscv_vwsub_vv_i32m8 (vc0, vc1, vl);
    vint32m8_t scaled = __riscv_vmul_vx_i32m8 (diff, c0, vl);
    vint32m8_t shifted =
        __riscv_vsll_vx_i32m8 (__riscv_vwadd_vx_i32m8 (vc1, 0, vl),
        PRECISION_S16, vl);
    vint32m8_t result = __riscv_vadd_vv_i32m8 (scaled, shifted, vl);
    vint32m8_t rounded = __riscv_vadd_vx_i32m8 (result,
        (gint32) 1 << (PRECISION_S16 - 1), vl);
    vint32m8_t final = __riscv_vsra_vx_i32m8 (rounded, PRECISION_S16, vl);

    /* Clamp to gint16 range and narrow */
    vint16m4_t clamped = __riscv_vncvt_x_x_w_i16m4 (final, vl);
    __riscv_vse16_v_i16m4 (o + i, clamped, vl);
  }
}

void
interpolate_gint16_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint16 *o = op, *a = ap, *ic = icp;
  gint32 c0 = ic[0], c1 = ic[1], c2 = ic[2], c3 = ic[3];
  const gint16 *c[4] = { (gint16 *) ((gint8 *) a + 0 * astride),
    (gint16 *) ((gint8 *) a + 1 * astride),
    (gint16 *) ((gint8 *) a + 2 * astride),
    (gint16 *) ((gint8 *) a + 3 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e16m4 (len - i);
    vint16m4_t vc0v = __riscv_vle16_v_i16m4 (c[0] + i, vl);
    vint16m4_t vc1v = __riscv_vle16_v_i16m4 (c[1] + i, vl);
    vint16m4_t vc2v = __riscv_vle16_v_i16m4 (c[2] + i, vl);
    vint16m4_t vc3v = __riscv_vle16_v_i16m4 (c[3] + i, vl);

    /* Widen to int32 and compute weighted sum */
    vint32m8_t w0 = __riscv_vwmul_vx_i32m8 (vc0v, c0, vl);
    vint32m8_t w1 = __riscv_vwmul_vx_i32m8 (vc1v, c1, vl);
    vint32m8_t w2 = __riscv_vwmul_vx_i32m8 (vc2v, c2, vl);
    vint32m8_t w3 = __riscv_vwmul_vx_i32m8 (vc3v, c3, vl);

    vint32m8_t sum = __riscv_vadd_vv_i32m8 (w0, w1, vl);
    sum = __riscv_vadd_vv_i32m8 (sum, w2, vl);
    sum = __riscv_vadd_vv_i32m8 (sum, w3, vl);

    /* Round and shift */
    vint32m8_t rounded = __riscv_vadd_vx_i32m8 (sum,
        (gint32) 1 << (PRECISION_S16 - 1), vl);
    vint32m8_t shifted = __riscv_vsra_vx_i32m8 (rounded, PRECISION_S16, vl);

    /* Clamp and narrow */
    vint16m4_t clamped = __riscv_vncvt_x_x_w_i16m4 (shifted, vl);
    __riscv_vse16_v_i16m4 (o + i, clamped, vl);
  }
}

/* ===== gint32 interpolate functions ===== */

void
interpolate_gint32_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint32 *o = op, *a = ap, *ic = icp;
  gint64 c0 = ic[0];
  const gint32 *c[2] = { (gint32 *) ((gint8 *) a + 0 * astride),
    (gint32 *) ((gint8 *) a + 1 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vint32m4_t vc0 = __riscv_vle32_v_i32m4 (c[0] + i, vl);
    vint32m4_t vc1 = __riscv_vle32_v_i32m4 (c[1] + i, vl);

    vint64m8_t diff = __riscv_vwsub_vv_i64m8 (vc0, vc1, vl);
    vint64m8_t scaled = __riscv_vmul_vx_i64m8 (diff, c0, vl);
    vint64m8_t shifted =
        __riscv_vsll_vx_i64m8 (__riscv_vwadd_vx_i64m8 (vc1, 0, vl),
        PRECISION_S32, vl);
    vint64m8_t result = __riscv_vadd_vv_i64m8 (scaled, shifted, vl);
    vint64m8_t rounded = __riscv_vadd_vx_i64m8 (result,
        (gint64) 1 << (PRECISION_S32 - 1), vl);
    vint64m8_t final = __riscv_vsra_vx_i64m8 (rounded, PRECISION_S32, vl);

    /* Narrow int64 to int32 with clamping */
    vint32m4_t clamped = __riscv_vncvt_x_x_w_i32m4 (final, vl);
    __riscv_vse32_v_i32m4 (o + i, clamped, vl);
  }
}

void
interpolate_gint32_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint32 *o = op, *a = ap, *ic = icp;
  gint64 c0 = ic[0], c1 = ic[1], c2 = ic[2], c3 = ic[3];
  const gint32 *c[4] = { (gint32 *) ((gint8 *) a + 0 * astride),
    (gint32 *) ((gint8 *) a + 1 * astride),
    (gint32 *) ((gint8 *) a + 2 * astride),
    (gint32 *) ((gint8 *) a + 3 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vint32m4_t vc0v = __riscv_vle32_v_i32m4 (c[0] + i, vl);
    vint32m4_t vc1v = __riscv_vle32_v_i32m4 (c[1] + i, vl);
    vint32m4_t vc2v = __riscv_vle32_v_i32m4 (c[2] + i, vl);
    vint32m4_t vc3v = __riscv_vle32_v_i32m4 (c[3] + i, vl);

    vint64m8_t w0 = __riscv_vwmul_vx_i64m8 (vc0v, c0, vl);
    vint64m8_t w1 = __riscv_vwmul_vx_i64m8 (vc1v, c1, vl);
    vint64m8_t w2 = __riscv_vwmul_vx_i64m8 (vc2v, c2, vl);
    vint64m8_t w3 = __riscv_vwmul_vx_i64m8 (vc3v, c3, vl);

    vint64m8_t sum = __riscv_vadd_vv_i64m8 (w0, w1, vl);
    sum = __riscv_vadd_vv_i64m8 (sum, w2, vl);
    sum = __riscv_vadd_vv_i64m8 (sum, w3, vl);

    vint64m8_t rounded = __riscv_vadd_vx_i64m8 (sum,
        (gint64) 1 << (PRECISION_S32 - 1), vl);
    vint64m8_t shifted = __riscv_vsra_vx_i64m8 (rounded, PRECISION_S32, vl);

    vint32m4_t clamped = __riscv_vncvt_x_x_w_i32m4 (shifted, vl);
    __riscv_vse32_v_i32m4 (o + i, clamped, vl);
  }
}

/* ===== gfloat interpolate functions ===== */

void
interpolate_gfloat_linear_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gfloat *o = op, *a = ap, *ic = icp;
  gfloat c0 = ic[0];
  const gfloat *c[2] = { (gfloat *) ((gint8 *) a + 0 * astride),
    (gfloat *) ((gint8 *) a + 1 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vfloat32m4_t vc0 = __riscv_vle32_v_f32m4 (c[0] + i, vl);
    vfloat32m4_t vc1 = __riscv_vle32_v_f32m4 (c[1] + i, vl);

    vfloat32m4_t diff = __riscv_vfsub_vv_f32m4 (vc0, vc1, vl);
    vfloat32m4_t scaled = __riscv_vfmul_vf_f32m4 (diff, c0, vl);
    vfloat32m4_t result = __riscv_vfadd_vv_f32m4 (scaled, vc1, vl);
    __riscv_vse32_v_f32m4 (o + i, result, vl);
  }
}

void
interpolate_gfloat_cubic_rvv (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gfloat *o = op, *a = ap, *ic = icp;
  gfloat c0 = ic[0], c1 = ic[1], c2 = ic[2], c3 = ic[3];
  const gfloat *c[4] = { (gfloat *) ((gint8 *) a + 0 * astride),
    (gfloat *) ((gint8 *) a + 1 * astride),
    (gfloat *) ((gint8 *) a + 2 * astride),
    (gfloat *) ((gint8 *) a + 3 * astride)
  };
  size_t vl;
  gint i;

  for (i = 0; i < len; i += vl) {
    vl = __riscv_vsetvl_e32m4 (len - i);
    vfloat32m4_t vc0v = __riscv_vle32_v_f32m4 (c[0] + i, vl);
    vfloat32m4_t vc1v = __riscv_vle32_v_f32m4 (c[1] + i, vl);
    vfloat32m4_t vc2v = __riscv_vle32_v_f32m4 (c[2] + i, vl);
    vfloat32m4_t vc3v = __riscv_vle32_v_f32m4 (c[3] + i, vl);

    vfloat32m4_t w0 = __riscv_vfmul_vf_f32m4 (vc0v, c0, vl);
    vfloat32m4_t w1 = __riscv_vfmul_vf_f32m4 (vc1v, c1, vl);
    vfloat32m4_t w2 = __riscv_vfmul_vf_f32m4 (vc2v, c2, vl);
    vfloat32m4_t w3 = __riscv_vfmul_vf_f32m4 (vc3v, c3, vl);

    vfloat32m4_t sum = __riscv_vfadd_vv_f32m4 (w0, w1, vl);
    sum = __riscv_vfadd_vv_f32m4 (sum, w2, vl);
    sum = __riscv_vfadd_vv_f32m4 (sum, w3, vl);
    __riscv_vse32_v_f32m4 (o + i, sum, vl);
  }
}

/* ===== Generate resample wrapper functions ===== */

MAKE_RESAMPLE_FUNC (gint16, full, 1, rvv);
MAKE_RESAMPLE_FUNC (gint16, linear, 1, rvv);
MAKE_RESAMPLE_FUNC (gint16, cubic, 1, rvv);

MAKE_RESAMPLE_FUNC (gint32, full, 1, rvv);
MAKE_RESAMPLE_FUNC (gint32, linear, 1, rvv);
MAKE_RESAMPLE_FUNC (gint32, cubic, 1, rvv);

MAKE_RESAMPLE_FUNC (gfloat, full, 1, rvv);
MAKE_RESAMPLE_FUNC (gfloat, linear, 1, rvv);
MAKE_RESAMPLE_FUNC (gfloat, cubic, 1, rvv);
