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

    asm volatile ("      vmov.s32 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s32 q1, #0\n"
                  "1:"
                  "      vld1.16 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld1.16 {d20, d21, d22, d23}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      vmlal.s16 q1, d17, d21\n"
                  "      vmlal.s16 q0, d18, d22\n"
                  "      vmlal.s16 q1, d19, d23\n"
                  "      bne 1b\n"
                  "      vadd.s32 q0, q0, q1\n"
                  "2:"
                  "      cmp %[remainder], #0\n"
                  "      beq 4f\n"
                  "3:"
                  "      vld1.16 {d16}, [%[b]]!\n"
                  "      vld1.16 {d20}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      bne 3b\n"
                  "4:"
                  "      vadd.s32 d0, d0, d1\n"
                  "      vpadd.s32 d0, d0, d0\n"
                  "      vqrshrn.s32 d0, q0, #15\n"
                  "      vst1.s16 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o)
                  : "cc", "q0", "q1",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23");
}

static inline void
inner_product_gint16_linear_1_neon (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
    uint32_t remainder = len % 8;
    len = len - remainder;

    asm volatile ("      vmov.s16 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s16 q1, #0\n"
                  "1:"
                  "      vld1.16 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld1.16 {d20, d21}, [%[a]]!\n"
                  "      vmov.16 q11, q10\n"
                  "      vzip.16 q10, q11\n"
                  "      subs %[len], %[len], #8\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      vmlal.s16 q1, d17, d21\n"
                  "      vmlal.s16 q0, d18, d22\n"
                  "      vmlal.s16 q1, d19, d23\n"
                  "      bne 1b\n"
                  "      vadd.s32 q0, q0, q1\n"
                  "2:"
                  "      cmp %[remainder], #0\n"
                  "      beq 4f\n"
                  "3:"
                  "      vld1.16 {d16, d17}, [%[b]]!\n"
                  "      vld1.16 {d20}, [%[a]]!\n"
                  "      vmov.16 d21, d20\n"
                  "      vzip.16 d20, d21\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      vmlal.s16 q0, d17, d21\n"
                  "      bne 3b\n"
                  "4:"
                  "      vshrn.s32 d0, q0, #15\n"
                  "      vld1.16 {d20}, [%[ic]]\n"
                  "      vmull.s16 q0, d0, d20\n"
                  "      vadd.s32 d0, d0, d1\n"
                  "      vpadd.s32 d0, d0, d0\n"
                  "      vqrshrn.s32 d0, q0, #15\n"
                  "      vst1.s16 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23" , "memory");
}

static inline void
inner_product_gint16_cubic_1_neon (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
    asm volatile ("      vmov.s32 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s32 q1, #0\n"
                  "1:"
                  "      vld1.16 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld4.16 {d20[], d21[], d22[], d23[]}, [%[a]]!\n"
                  "      subs %[len], %[len], #4\n"
                  "      vmlal.s16 q0, d16, d20\n"
                  "      vmlal.s16 q1, d17, d21\n"
                  "      vmlal.s16 q0, d18, d22\n"
                  "      vmlal.s16 q1, d19, d23\n"
                  "      bne 1b\n"
                  "      vadd.s32 q0, q0, q1\n"
                  "2:"
                  "      vshrn.s32 d0, q0, #15\n"
                  "      vld1.16 {d20}, [%[ic]]\n"
                  "      vmull.s16 q0, d0, d20\n"
                  "      vadd.s32 d0, d0, d1\n"
                  "      vpadd.s32 d0, d0, d0\n"
                  "      vqrshrn.s32 d0, q0, #15\n"
                  "      vst1.s16 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23" , "memory");
}

static inline void
inner_product_gint32_none_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
    uint32_t remainder = len % 8;
    len = len - remainder;

    asm volatile ("      vmov.s64 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s64 q1, #0\n"
                  "1:"
                  "      vld1.32 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld1.32 {d20, d21, d22, d23}, [%[a]]!\n"
                  "      subs %[len], %[len], #8\n"
                  "      vmlal.s32 q0, d16, d20\n"
                  "      vmlal.s32 q1, d17, d21\n"
                  "      vmlal.s32 q0, d18, d22\n"
                  "      vmlal.s32 q1, d19, d23\n"
                  "      bne 1b\n"
                  "      vadd.s64 q0, q0, q1\n"
                  "2:"
                  "      cmp %[remainder], #0\n"
                  "      beq 4f\n"
                  "3:"
                  "      vld1.32 {d16, d17}, [%[b]]!\n"
                  "      vld1.32 {d20, d21}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmlal.s32 q0, d16, d20\n"
                  "      vmlal.s32 q0, d17, d21\n"
                  "      bne 3b\n"
                  "4:"
                  "      vadd.s64 d0, d0, d1\n"
                  "      vqrshrn.s64 d0, q0, #31\n"
                  "      vst1.s32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o)
                  : "cc", "q0", "q1",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23");
}

static inline void
inner_product_gint32_linear_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
    asm volatile ("      vmov.s64 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s64 q1, #0\n"
                  "1:"
                  "      vld1.s32 {d16, d17, d18, d19}, [%[b]]!\n"
                  "      vld2.s32 {d20[], d21[]}, [%[a]]!\n"
                  "      vld2.s32 {d22[], d23[]}, [%[a]]!\n"
                  "      subs %[len], %[len], #4\n"
                  "      vmlal.s32 q0, d16, d20\n"
                  "      vmlal.s32 q1, d17, d21\n"
                  "      vmlal.s32 q0, d18, d22\n"
                  "      vmlal.s32 q1, d19, d23\n"
                  "      bne 1b\n"
                  "      vadd.s64 q0, q0, q1\n"
                  "2:"
                  "      vld1.s32 {d20}, [%[ic]]\n"
                  "      vshrn.s64 d0, q0, #31\n"
                  "      vmull.s32 q0, d0, d20\n"
                  "      vadd.s64 d0, d0, d1\n"
                  "      vqrshrn.s64 d0, q0, #31\n"
                  "      vst1.s32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1",
                    "d16", "d17", "d18", "d19",
                    "d20", "d21", "d22", "d23", "memory");
}

static inline void
inner_product_gint32_cubic_1_neon (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
    asm volatile ("      vmov.s64 q0, #0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.s64 q1, #0\n"
                  "1:"
                  "      vld1.s32 {q4, q5}, [%[b]]!\n"
                  "      vld1.s32 {q6, q7}, [%[b]]!\n"
                  "      vld1.s32 {d16[], d17[]}, [%[a]]!\n"
                  "      vld1.s32 {d18[], d19[]}, [%[a]]!\n"
                  "      vld1.s32 {d20[], d21[]}, [%[a]]!\n"
                  "      vld1.s32 {d22[], d23[]}, [%[a]]!\n"
                  "      subs %[len], %[len], #4\n"
                  "      vmlal.s32 q0, d16, d8\n"
                  "      vmlal.s32 q1, d17, d9\n"
                  "      vmlal.s32 q0, d18, d10\n"
                  "      vmlal.s32 q1, d19, d11\n"
                  "      vmlal.s32 q0, d20, d12\n"
                  "      vmlal.s32 q1, d21, d13\n"
                  "      vmlal.s32 q0, d22, d14\n"
                  "      vmlal.s32 q1, d23, d15\n"
                  "      bne 1b\n"
                  "2:"
                  "      vld1.s32 {d20, d21}, [%[ic]]\n"
                  "      vshrn.s64 d16, q0, #31\n"
                  "      vshrn.s64 d17, q1, #31\n"
                  "      vmull.s32 q0, d20, d16\n"
                  "      vmlal.s32 q0, d21, d17\n"
                  "      vadd.s64 d0, d0, d1\n"
                  "      vqrshrn.s64 d0, q0, #31\n"
                  "      vst1.s32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1", "q4", "q5", "q6", "q7", "q8",
                    "q9", "q10", "q11", "memory");
}

static inline void
inner_product_gfloat_none_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
    uint32_t remainder = len % 16;
    len = len - remainder;

    asm volatile ("      vmov.f32 q0, #0.0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.f32 q1, #0.0\n"
                  "1:"
                  "      vld1.32 {q4, q5}, [%[b]]!\n"
                  "      vld1.32 {q8, q9}, [%[a]]!\n"
                  "      vld1.32 {q6, q7}, [%[b]]!\n"
                  "      vld1.32 {q10, q11}, [%[a]]!\n"
                  "      subs %[len], %[len], #16\n"
                  "      vmla.f32 q0, q4, q8\n"
                  "      vmla.f32 q1, q5, q9\n"
                  "      vmla.f32 q0, q6, q10\n"
                  "      vmla.f32 q1, q7, q11\n"
                  "      bne 1b\n"
                  "      vadd.f32 q0, q0, q1\n"
                  "2:"
                  "      cmp %[remainder], #0\n"
                  "      beq 4f\n"
                  "3:"
                  "      vld1.32 {q6}, [%[b]]!\n"
                  "      vld1.32 {q10}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmla.f32 q0, q6, q10\n"
                  "      bne 3b\n"
                  "4:"
                  "      vadd.f32 d0, d0, d1\n"
                  "      vpadd.f32 d0, d0, d0\n"
                  "      vst1.f32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o)
                  : "cc", "q0", "q1", "q4", "q5", "q6", "q7", "q8",
                    "q9", "q10", "q11");
}

static inline void
inner_product_gfloat_linear_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
    uint32_t remainder = len % 8;
    len = len - remainder;

    asm volatile ("      vmov.f32 q0, #0.0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.f32 q1, #0.0\n"
                  "1:"
                  "      vld2.f32 {q4, q5}, [%[b]]!\n"
                  "      vld2.f32 {q6, q7}, [%[b]]!\n"
                  "      vld1.f32 {q8, q9}, [%[a]]!\n"
                  "      subs %[len], %[len], #8\n"
                  "      vmla.f32 q0, q4, q8\n"
                  "      vmla.f32 q1, q5, q8\n"
                  "      vmla.f32 q0, q6, q9\n"
                  "      vmla.f32 q1, q7, q9\n"
                  "      bne 1b\n"
                  "      vadd.f32 q0, q0, q1\n"
                  "2:"
                  "      cmp %[remainder], #0\n"
                  "      beq 4f\n"
                  "3:"
                  "      vld2.f32 {q4}, [%[b]]!\n"
                  "      vld1.f32 {q8}, [%[a]]!\n"
                  "      subs %[remainder], %[remainder], #4\n"
                  "      vmla.f32 q0, q4, q8\n"
                  "      bne 3b\n"
                  "4:"
                  "      vld1.f32 {q10}, [%[ic]]\n"
                  "      vmul.f32 q0, q0, q10\n"
                  "      vadd.f32 d0, d0, d1\n"
                  "      vpadd.f32 d0, d0, d0\n"
                  "      vst1.f32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len), [remainder] "+r" (remainder)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1", "q4", "q5", "q6", "q7", "q8",
                    "q9", "q10", "q11", "memory");
}

static inline void
inner_product_gfloat_cubic_1_neon (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
    asm volatile ("      vmov.f32 q0, #0.0\n"
                  "      cmp %[len], #0\n"
                  "      beq 2f\n"
                  "      vmov.f32 q1, #0.0\n"
                  "1:"
                  "      vld1.f32 {q4, q5}, [%[b]]!\n"
                  "      vld1.f32 {q6, q7}, [%[b]]!\n"
                  "      vld1.f32 {d16[], d17[]}, [%[a]]!\n"
                  "      vld1.f32 {d18[], d19[]}, [%[a]]!\n"
                  "      vld1.f32 {d20[], d21[]}, [%[a]]!\n"
                  "      vld1.f32 {d22[], d23[]}, [%[a]]!\n"
                  "      subs %[len], %[len], #4\n"
                  "      vmla.f32 q0, q4, q8\n"
                  "      vmla.f32 q1, q5, q9\n"
                  "      vmla.f32 q0, q6, q10\n"
                  "      vmla.f32 q1, q7, q11\n"
                  "      bne 1b\n"
                  "      vadd.f32 q0, q0, q1\n"
                  "2:"
                  "      vld1.f32 {q10}, [%[ic]]\n"
                  "      vmul.f32 q0, q0, q10\n"
                  "      vadd.f32 d0, d0, d1\n"
                  "      vpadd.f32 d0, d0, d0\n"
                  "      vst1.f32 d0[0], [%[o]]\n"
                  : [a] "+r" (a), [b] "+r" (b),
                    [len] "+r" (len)
                  : [o] "r" (o), [ic] "r" (icoeff)
                  : "cc", "q0", "q1", "q4", "q5", "q6", "q7", "q8",
                    "q9", "q10", "q11", "memory");
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

static void
audio_resampler_check_neon (const gchar *target_name, const gchar *option)
{
  if (!strcmp (target_name, "neon")) {
    GST_DEBUG ("enable NEON optimisations");
    resample_gint16_none_1 = resample_gint16_none_1_neon;
    resample_gint16_linear_1 = resample_gint16_linear_1_neon;
    resample_gint16_cubic_1 = resample_gint16_cubic_1_neon;

    resample_gint32_none_1 = resample_gint32_none_1_neon;
    resample_gint32_linear_1 = resample_gint32_linear_1_neon;
    resample_gint32_cubic_1 = resample_gint32_cubic_1_neon;

    resample_gfloat_none_1 = resample_gfloat_none_1_neon;
    resample_gfloat_linear_1 = resample_gfloat_linear_1_neon;
    resample_gfloat_cubic_1 = resample_gfloat_cubic_1_neon;
  }
}
