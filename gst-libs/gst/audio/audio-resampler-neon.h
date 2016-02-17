/* GStreamer
 * Copyright (C) <2016> Wim Taymans <wim.taymans@gmail.com>
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

static inline void
inner_product_gint16_none_1_neon (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
    uint32_t remainder = len % 16;
    len = len - remainder;

    asm volatile ("      cmp %[len], #0\n"
                  "      bne 1f\n"
                  "      vld1.16 {d16}, [%[b]]!\n"
                  "      vld1.16 {d20}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmull.s16 q0, d16, d20\n"
                  "      beq 5f\n" 
                  "      b 4f\n"
                  "1:"
                  "      vld1.16 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld1.16 {d20, d21, d22, d23}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmull.s16 q0, d16, d20\n"
                  "      vmlal.s16 q0, d17, d21\n"
                  "      vmlal.s16 q0, d18, d22\n"
                  "      vmlal.s16 q0, d19, d23\n"
                  "      beq 3f\n"
                  "2:"
                  "      vld1.16 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld1.16 {d20, d21, d22, d23}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      vmlal.s16 q0, d17, d21\n"
                  "      vmlal.s16 q0, d18, d22\n"
                  "      vmlal.s16 q0, d19, d23\n"
                  "      bne 2b\n"
                  "3:"
                  "      cmp %[remainder], #0\n"
                  "      beq 5f\n"
                  "4:"
                  "      vld1.16 {d16}, [%[b]]!\n"
                  "      vld1.16 {d20}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      bne 4b\n"
                  "5:"
                  "      vaddl.s32 q0, d0, d1\n"
                  "      vadd.s64 d0, d0, d1\n"
                  "      vqmovn.s64 d0, q0\n"
                  "      vqrshrn.s32 d0, q0, #15\n"
                  "      vst1.s16 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o)
                  : "cc", "q0",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23");
}

static inline void
inner_product_gint16_linear_1_neon (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
}

static inline void
inner_product_gint16_cubic_1_neon (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
}

static inline void
inner_product_gint32_none_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
}

static inline void
inner_product_gint32_linear_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
}

static inline void
inner_product_gint32_cubic_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
}

static inline void
inner_product_gfloat_none_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
    uint32_t remainder = len % 16;
    len = len - remainder;

    asm volatile ("      cmp %[len], #0\n"
                  "      bne 1f\n"
                  "      vld1.32 {q4}, [%[b]]!\n"
                  "      vld1.32 {q8}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmul.f32 q0, q4, q8\n"
                  "      bne 4f\n"
                  "      b 5f\n"
                  "1:"
                  "      vld1.32 {q4, q5}, [%[b]]!\n"
                  "      vld1.32 {q8, q9}, [%[a]]!\n"
                  "      vld1.32 {q6, q7}, [%[b]]!\n"
                  "      vld1.32 {q10, q11}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmul.f32 q0, q4, q8\n"
                  "      vmul.f32 q1, q5, q9\n"
                  "      vmul.f32 q2, q6, q10\n"
                  "      vmul.f32 q3, q7, q11\n"
                  "      beq 3f\n"
                  "2:"
                  "      vld1.32 {q4, q5}, [%[b]]!\n"
                  "      vld1.32 {q8, q9}, [%[a]]!\n"
                  "      vld1.32 {q6, q7}, [%[b]]!\n"
                  "      vld1.32 {q10, q11}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmla.f32 q0, q4, q8\n"
                  "      vmla.f32 q1, q5, q9\n"
                  "      vmla.f32 q2, q6, q10\n"
                  "      vmla.f32 q3, q7, q11\n"
                  "      bne 2b\n"
                  "3:"
                  "      vadd.f32 q4, q0, q1\n"
                  "      vadd.f32 q5, q2, q3\n"
                  "      cmp %[remainder], #0\n"
                  "      vadd.f32 q0, q4, q5\n"
                  "      beq 5f\n"
                  "4:"
                  "      vld1.32 {q6}, [%[b]]!\n"
                  "      vld1.32 {q10}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmla.f32 q0, q6, q10\n"
                  "      bne 4b\n"
                  "5:"
                  "      vadd.f32 d0, d0, d1\n"
                  "      vpadd.f32 d0, d0, d0\n"
                  "      vst1.f32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o)
                  : "cc", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8",
                    "q9", "q10", "q11");

}

static inline void
inner_product_gfloat_linear_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
}

static inline void
inner_product_gfloat_cubic_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
}

static inline void
inner_product_gdouble_none_1_neon (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
}

static inline void
inner_product_gdouble_linear_1_neon (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
}

static inline void
inner_product_gdouble_cubic_1_neon (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
}

static void
interpolate_gdouble_linear_neon (gdouble * o, const gdouble * a,
    gint len, const gdouble * icoeff)
{
}

static void
interpolate_gdouble_cubic_neon (gdouble * o, const gdouble * a,
    gint len, const gdouble * icoeff)
{
}

MAKE_RESAMPLE_FUNC (gint16, none, 1, neon);
MAKE_RESAMPLE_FUNC (gint16, linear, 1, neon);
MAKE_RESAMPLE_FUNC (gint16, cubic, 1, neon);

MAKE_RESAMPLE_FUNC (gint32, none, 1, neon);
MAKE_RESAMPLE_FUNC (gint32, linear, 1, neon);
MAKE_RESAMPLE_FUNC (gint32, cubic, 1, neon);

MAKE_RESAMPLE_FUNC (gfloat, none, 1, neon);
MAKE_RESAMPLE_FUNC (gfloat, linear, 1, neon);
MAKE_RESAMPLE_FUNC (gfloat, cubic, 1, neon);

MAKE_RESAMPLE_FUNC (gdouble, none, 1, neon);
MAKE_RESAMPLE_FUNC (gdouble, linear, 1, neon);
MAKE_RESAMPLE_FUNC (gdouble, cubic, 1, neon);

static void
audio_resampler_check_neon (const gchar *target_name, const gchar *option)
{
  if (!strcmp (target_name, "neon")) {
    GST_DEBUG ("enable NEON optimisations");
    resample_gint16_none_1 = resample_gint16_none_1_neon;

    resample_gfloat_none_1 = resample_gfloat_none_1_neon;

    if (0) {
      resample_gint16_linear_1 = resample_gint16_linear_1_neon;
      resample_gint16_cubic_1 = resample_gint16_cubic_1_neon;

      resample_gint32_none_1 = resample_gint32_none_1_neon;
      resample_gint32_linear_1 = resample_gint32_linear_1_neon;
      resample_gint32_cubic_1 = resample_gint32_cubic_1_neon;

      resample_gfloat_linear_1 = resample_gfloat_linear_1_neon;
      resample_gfloat_cubic_1 = resample_gfloat_cubic_1_neon;

      resample_gdouble_none_1 = resample_gdouble_none_1_neon;
      resample_gdouble_linear_1 = resample_gdouble_linear_1_neon;
      resample_gdouble_cubic_1 = resample_gdouble_cubic_1_neon;

      interpolate_gdouble_linear = interpolate_gdouble_linear_neon;
      interpolate_gdouble_cubic = interpolate_gdouble_cubic_neon;
    }
  }
}
