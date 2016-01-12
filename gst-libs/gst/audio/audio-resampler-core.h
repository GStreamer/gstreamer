/* GStreamer
 * Copyright (C) <2015> Wim Taymans <wim.taymans@gmail.com>
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


#define PRECISION_S16 15
#define PRECISION_S32 30

#ifdef HAVE_EMMINTRIN_H
#include <emmintrin.h>
#endif

static inline void
inner_product_gdouble_1 (gdouble * o, const gdouble * a, const gdouble * b,
    gint len)
{
  gint i = 0;
  gdouble res;
#ifdef HAVE_EMMINTRIN_H
  __m128d sum = _mm_setzero_pd ();

  for (; i < len - 7; i += 8) {
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 0),
            _mm_loadu_pd (b + i + 0)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 2),
            _mm_loadu_pd (b + i + 2)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 4),
            _mm_loadu_pd (b + i + 4)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 6),
            _mm_loadu_pd (b + i + 6)));
  }
  sum = _mm_add_sd (sum, _mm_unpackhi_pd (sum, sum));
  _mm_store_sd (&res, sum);
#else
  res = 0.0;
#endif

  for (; i < len; i++)
    res += a[i] * b[i];

  *o = res;
}

static inline void
inner_product_gfloat_1 (gfloat * o, const gfloat * a, const gfloat * b, gint len)
{
  gint i = 0;
  gfloat res;
#ifdef HAVE_EMMINTRIN_H
  __m128 sum = _mm_setzero_ps ();

  for (; i < len - 7; i += 8) {
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + i + 0),
            _mm_loadu_ps (b + i + 0)));
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + i + 4),
            _mm_loadu_ps (b + i + 4)));
  }
  sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
  sum = _mm_add_ss (sum, _mm_shuffle_ps (sum, sum, 0x55));
  _mm_store_ss (&res, sum);
#else
  res = 0.0;
#endif

  for (; i < len; i++)
    res += a[i] * b[i];

  *o = res;
}

static inline void
inner_product_gint32_1 (gint32 * o, const gint32 * a, const gint32 * b, gint len)
{
  gint i = 0;
  gint64 res = 0;

  for (; i < len; i++)
    res += (gint64) a[i] * (gint64) b[i];

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

static inline void
inner_product_gint16_1 (gint16 * o, const gint16 * a, const gint16 * b, gint len)
{
  gint i = 0;
  gint32 res = 0;
#ifdef HAVE_EMMINTRIN_H
  __m128i sum[2], ta, tb;
  __m128i t1[2];

  sum[0] = _mm_setzero_si128 ();
  sum[1] = _mm_setzero_si128 ();

  for (; i < len - 7; i += 8) {
    ta = _mm_loadu_si128 ((__m128i *) (a + i));
    tb = _mm_loadu_si128 ((__m128i *) (b + i));

    t1[0] = _mm_mullo_epi16 (ta, tb);
    t1[1] = _mm_mulhi_epi16 (ta, tb);

    sum[0] = _mm_add_epi32 (sum[0], _mm_unpacklo_epi16 (t1[0], t1[1]));
    sum[1] = _mm_add_epi32 (sum[1], _mm_unpackhi_epi16 (t1[0], t1[1]));
  }
  sum[0] = _mm_add_epi32 (sum[0], sum[1]);
  sum[0] =
      _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (2, 3, 2,
              3)));
  sum[0] =
      _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (1, 1, 1,
              1)));
  res = _mm_cvtsi128_si32 (sum[0]);
#else
  res = 0;
#endif

  for (; i < len; i++)
    res += (gint32) a[i] * (gint32) b[i];

  res = (res + (1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  *o = CLAMP (res, -(1L << 15), (1L << 15) - 1);
}

static inline void
inner_product_gdouble_2 (gdouble * o, const gdouble * a, const gdouble * b,
    gint len)
{
  gint i = 0;
  gdouble r[2];
#ifdef HAVE_EMMINTRIN_H
  __m128d sum = _mm_setzero_pd (), t;

  for (; i < len - 3; i += 4) {
    t = _mm_loadu_pd (b + i);
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i),
            _mm_unpacklo_pd (t, t)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 2),
            _mm_unpackhi_pd (t, t)));

    t = _mm_loadu_pd (b + i + 2);
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 4),
            _mm_unpacklo_pd (t, t)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 6),
            _mm_unpackhi_pd (t, t)));
  }
  _mm_store_pd (r, sum);
#else
  r[0] = 0.0;
  r[1] = 0.0;
#endif

  for (; i < len; i++) {
    r[0] += a[2 * i] * b[i];
    r[1] += a[2 * i + 1] * b[i];
  }
  o[0] = r[0];
  o[1] = r[1];
}

static inline void
inner_product_gint16_2 (gint16 * o, const gint16 * a, const gint16 * b, gint len)
{
  gint i = 0;
  gint32 r[2];
#ifdef HAVE_EMMINTRIN_H
  guint64 r64;
  __m128i sum[2], ta, tb;
  __m128i t1[2];

  sum[0] = _mm_setzero_si128 ();
  sum[1] = _mm_setzero_si128 ();

  for (; i < len - 7; i += 8) {
    tb = _mm_loadu_si128 ((__m128i *) (b + i));

    t1[1] = _mm_unpacklo_epi16 (tb, tb);

    ta = _mm_loadu_si128 ((__m128i *) (a + 2 * i));
    t1[0] = _mm_mullo_epi16 (ta, t1[1]);
    t1[1] = _mm_mulhi_epi16 (ta, t1[1]);

    sum[0] = _mm_add_epi32 (sum[0], _mm_unpacklo_epi16 (t1[0], t1[1]));
    sum[1] = _mm_add_epi32 (sum[1], _mm_unpackhi_epi16 (t1[0], t1[1]));

    t1[1] = _mm_unpackhi_epi16 (tb, tb);

    ta = _mm_loadu_si128 ((__m128i *) (a + 2 * i + 8));
    t1[0] = _mm_mullo_epi16 (ta, t1[1]);
    t1[1] = _mm_mulhi_epi16 (ta, t1[1]);

    sum[0] = _mm_add_epi32 (sum[0], _mm_unpacklo_epi16 (t1[0], t1[1]));
    sum[1] = _mm_add_epi32 (sum[1], _mm_unpackhi_epi16 (t1[0], t1[1]));
  }
  sum[0] = _mm_add_epi32 (sum[0], sum[1]);
  sum[0] =
      _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (2, 3, 2,
              3)));
  r64 = _mm_cvtsi128_si64 (sum[0]);
  r[0] = r64 >> 32;
  r[1] = r64 & 0xffffffff;
#else
  r[0] = 0;
  r[1] = 0;
#endif

  for (; i < len; i++) {
    r[0] += (gint32) a[2 * i] * (gint32) b[i];
    r[1] += (gint32) a[2 * i + 1] * (gint32) b[i];
  }
  r[0] = (r[0] + (1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  r[1] = (r[1] + (1 << (PRECISION_S16 - 1))) >> PRECISION_S16;
  o[0] = CLAMP (r[0], -(1L << 15), (1L << 15) - 1);
  o[1] = CLAMP (r[1], -(1L << 15), (1L << 15) - 1);
}
