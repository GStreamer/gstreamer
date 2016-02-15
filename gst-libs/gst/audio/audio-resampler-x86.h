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

#if defined (HAVE_XMMINTRIN_H) && defined(__SSE__)
#include <xmmintrin.h>

static inline void
inner_product_gfloat_none_1_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
  gint i = 0;
  __m128 sum = _mm_setzero_ps ();

  for (; i < len; i += 8) {
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + i + 0),
            _mm_load_ps (b + i + 0)));
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + i + 4),
            _mm_load_ps (b + i + 4)));
  }
  sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
  sum = _mm_add_ss (sum, _mm_shuffle_ps (sum, sum, 0x55));
  _mm_store_ss (o, sum);
}

static inline void
inner_product_gfloat_linear_1_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
  gint i = 0;
  __m128 sum, t;
  __m128 f = _mm_loadu_ps(icoeff);

  sum = _mm_setzero_ps ();
  for (; i < len; i += 4) {
    t = _mm_loadu_ps (a + i);
    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_unpacklo_ps (t, t),
          _mm_load_ps (b + 2 * (i + 0))));
    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_unpackhi_ps (t, t),
          _mm_load_ps (b + 2 * (i + 2))));
  }
  sum = _mm_mul_ps (sum, f);
  sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
  sum = _mm_add_ss (sum, _mm_shuffle_ps (sum, sum, 0x55));
  _mm_store_ss (o, sum);
}

static inline void
inner_product_gfloat_cubic_1_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
  gint i = 0;
  __m128 sum = _mm_setzero_ps ();
  __m128 f = _mm_loadu_ps(icoeff);

  for (; i < len; i += 2) {
    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_load1_ps (a + i + 0),
          _mm_load_ps (b + 4 * (i + 0))));
    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_load1_ps (a + i + 1),
          _mm_load_ps (b + 4 * (i + 1))));
  }
  sum = _mm_mul_ps (sum, f);
  sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
  sum = _mm_add_ss (sum, _mm_shuffle_ps (sum, sum, 0x55));
  _mm_store_ss (o, sum);
}

static inline void
inner_product_gfloat_none_2_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff)
{
  gint i = 0;
  __m128 sum = _mm_setzero_ps (), t;

  for (; i < len; i += 8) {
    t = _mm_load_ps (b + i);
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + 2 * i + 0),
            _mm_unpacklo_ps (t, t)));
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + 2 * i + 4),
            _mm_unpackhi_ps (t, t)));

    t = _mm_load_ps (b + i + 4);
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + 2 * i + 8),
            _mm_unpacklo_ps (t, t)));
    sum =
        _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + 2 * i + 12),
            _mm_unpackhi_ps (t, t)));
  }
  sum = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
  *(gint64*)o = _mm_cvtsi128_si64 ((__m128i)sum);
}

MAKE_RESAMPLE_FUNC (gfloat, none, 1, sse);
MAKE_RESAMPLE_FUNC (gfloat, linear, 1, sse);
MAKE_RESAMPLE_FUNC (gfloat, cubic, 1, sse);

MAKE_RESAMPLE_FUNC (gfloat, none, 2, sse);
#endif

#if defined (HAVE_EMMINTRIN_H) && defined(__SSE2__)
#include <emmintrin.h>

static inline void
inner_product_gint16_none_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
  gint i = 0;
  __m128i sum, ta, tb;

  sum = _mm_setzero_si128 ();

  for (; i < len; i += 8) {
    ta = _mm_loadu_si128 ((__m128i *) (a + i));
    tb = _mm_load_si128 ((__m128i *) (b + i));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, tb));
  }
  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (2, 3, 2,
              3)));
  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (1, 1, 1,
              1)));

  sum = _mm_add_epi32 (sum, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_packs_epi32 (sum, sum);
  *o = _mm_extract_epi16 (sum, 0);
}

static inline void
inner_product_gint16_linear_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
  gint i = 0;
  __m128i sum, t, ta, tb;
  __m128i f = _mm_cvtsi64_si128 (*((long long*)icoeff));

  sum = _mm_setzero_si128 ();
  f = _mm_unpacklo_epi16 (f, sum);

  for (; i < len; i += 8) {
    t = _mm_loadu_si128 ((__m128i *) (a + i));

    ta = _mm_unpacklo_epi32 (t, t);
    tb = _mm_load_si128 ((__m128i *) (b + 2 * i + 0));
    tb = _mm_shufflelo_epi16 (tb, _MM_SHUFFLE (3,1,2,0));
    tb = _mm_shufflehi_epi16 (tb, _MM_SHUFFLE (3,1,2,0));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, tb));

    ta = _mm_unpackhi_epi32 (t, t);
    tb = _mm_load_si128 ((__m128i *) (b + 2 * i + 8));
    tb = _mm_shufflelo_epi16 (tb, _MM_SHUFFLE (3,1,2,0));
    tb = _mm_shufflehi_epi16 (tb, _MM_SHUFFLE (3,1,2,0));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, tb));
  }
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_madd_epi16 (sum, f);

  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (2, 3, 2,
              3)));
  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (1, 1, 1,
              1)));

  sum = _mm_add_epi32 (sum, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_packs_epi32 (sum, sum);
  *o = _mm_extract_epi16 (sum, 0);
}

static inline void
inner_product_gint16_cubic_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
  gint i = 0;
  __m128i sum, ta, tb;
  __m128i f = _mm_cvtsi64_si128 (*((long long*)icoeff));

  sum = _mm_setzero_si128 ();
  f = _mm_unpacklo_epi16 (f, sum);

  for (; i < len; i += 2) {
    ta = _mm_cvtsi32_si128 (*(gint32*)(a + i));
    ta = _mm_unpacklo_epi32 (ta, ta);
    ta = _mm_unpacklo_epi32 (ta, ta);

    tb = _mm_unpacklo_epi16 (_mm_cvtsi64_si128 (*(gint64*)(b + 4 * i + 0)),
                             _mm_cvtsi64_si128 (*(gint64*)(b + 4 * i + 4)));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, tb));
  }
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_madd_epi16 (sum, f);

  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (2, 3, 2,
              3)));
  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (1, 1, 1,
              1)));

  sum = _mm_add_epi32 (sum, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_packs_epi32 (sum, sum);
  *o = _mm_extract_epi16 (sum, 0);
}

static inline void
inner_product_gdouble_none_1_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d sum = _mm_setzero_pd ();

  for (; i < len; i += 8) {
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 0),
            _mm_load_pd (b + i + 0)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 2),
            _mm_load_pd (b + i + 2)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 4),
            _mm_load_pd (b + i + 4)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + i + 6),
            _mm_load_pd (b + i + 6)));
  }
  sum = _mm_add_sd (sum, _mm_unpackhi_pd (sum, sum));
  _mm_store_sd (o, sum);
}

static inline void
inner_product_gdouble_linear_1_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d sum = _mm_setzero_pd ();
  __m128d f = _mm_loadu_pd (icoeff);

  for (; i < len; i += 4) {
    sum = _mm_add_pd (sum, _mm_mul_pd (_mm_load1_pd (a + i + 0), _mm_load_pd (b + 2 * i + 0)));
    sum = _mm_add_pd (sum, _mm_mul_pd (_mm_load1_pd (a + i + 1), _mm_load_pd (b + 2 * i + 2)));
    sum = _mm_add_pd (sum, _mm_mul_pd (_mm_load1_pd (a + i + 2), _mm_load_pd (b + 2 * i + 4)));
    sum = _mm_add_pd (sum, _mm_mul_pd (_mm_load1_pd (a + i + 3), _mm_load_pd (b + 2 * i + 6)));
  }
  sum = _mm_mul_pd (sum, f);
  sum = _mm_add_sd (sum, _mm_unpackhi_pd (sum, sum));
  _mm_store_sd (o, sum);
}

static inline void
inner_product_gdouble_cubic_1_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d sum1 = _mm_setzero_pd (), t;
  __m128d sum2 = _mm_setzero_pd ();
  __m128d f1 = _mm_loadu_pd (icoeff);
  __m128d f2 = _mm_loadu_pd (icoeff+2);

  for (; i < len; i += 2) {
    t = _mm_load1_pd (a + i + 0);
    sum1 = _mm_add_pd (sum1, _mm_mul_pd (t, _mm_load_pd (b + 4 * i + 0)));
    sum2 = _mm_add_pd (sum2, _mm_mul_pd (t, _mm_load_pd (b + 4 * i + 2)));

    t = _mm_load1_pd (a + i + 1);
    sum1 = _mm_add_pd (sum1, _mm_mul_pd (t, _mm_load_pd (b + 4 * i + 4)));
    sum2 = _mm_add_pd (sum2, _mm_mul_pd (t, _mm_load_pd (b + 4 * i + 6)));
  }
  sum1 = _mm_mul_pd (sum1, f1);
  sum2 = _mm_mul_pd (sum2, f2);
  sum1 = _mm_add_pd (sum1, sum2);
  sum1 = _mm_add_sd (sum1, _mm_unpackhi_pd (sum1, sum1));
  _mm_store_sd (o, sum1);
}

static inline void
inner_product_gint16_none_2_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff)
{
  gint i = 0;
  __m128i sum, ta, tb, t1;

  sum = _mm_setzero_si128 ();

  for (; i < len; i += 8) {
    tb = _mm_load_si128 ((__m128i *) (b + i));

    t1 = _mm_unpacklo_epi16 (tb, tb);
    ta = _mm_loadu_si128 ((__m128i *) (a + 2 * i));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, t1));

    t1 = _mm_unpackhi_epi16 (tb, tb);
    ta = _mm_loadu_si128 ((__m128i *) (a + 2 * i + 8));

    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (ta, t1));
  }
  sum =
      _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (2, 3, 2,
              3)));

  sum = _mm_add_epi32 (sum, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_packs_epi32 (sum, sum);
  *(gint32*)o = _mm_cvtsi128_si32 (sum);
}

static inline void
inner_product_gdouble_none_2_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d sum = _mm_setzero_pd (), t;

  for (; i < len; i += 4) {
    t = _mm_load_pd (b + i);
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i),
            _mm_unpacklo_pd (t, t)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 2),
            _mm_unpackhi_pd (t, t)));

    t = _mm_load_pd (b + i + 2);
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 4),
            _mm_unpacklo_pd (t, t)));
    sum =
        _mm_add_pd (sum, _mm_mul_pd (_mm_loadu_pd (a + 2 * i + 6),
            _mm_unpackhi_pd (t, t)));
  }
  _mm_store_pd (o, sum);
}

MAKE_RESAMPLE_FUNC (gint16, none, 1, sse2);
MAKE_RESAMPLE_FUNC (gint16, linear, 1, sse2);
MAKE_RESAMPLE_FUNC (gint16, cubic, 1, sse2);

MAKE_RESAMPLE_FUNC (gdouble, none, 1, sse2);
MAKE_RESAMPLE_FUNC (gdouble, linear, 1, sse2);
MAKE_RESAMPLE_FUNC (gdouble, cubic, 1, sse2);

MAKE_RESAMPLE_FUNC (gint16, none, 2, sse2);
MAKE_RESAMPLE_FUNC (gdouble, none, 2, sse2);

static void
interpolate_gdouble_linear_sse2 (gdouble * o, const gdouble * a,
    gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d f = _mm_loadu_pd (icoeff), t1, t2;

  for (; i < len; i += 2) {
    t1 = _mm_mul_pd (_mm_load_pd (a + 2*i + 0), f);
    t1 = _mm_add_sd (t1, _mm_unpackhi_pd (t1, t1));
    t2 = _mm_mul_pd (_mm_load_pd (a + 2*i + 2), f);
    t2 = _mm_add_sd (t2, _mm_unpackhi_pd (t2, t2));

    _mm_store_pd (o + i, _mm_unpacklo_pd (t1, t2));
  }
}

static void
interpolate_gdouble_cubic_sse2 (gdouble * o, const gdouble * a,
    gint len, const gdouble * icoeff)
{
  gint i = 0;
  __m128d t1, t2;
  __m128d f1 = _mm_loadu_pd (icoeff);
  __m128d f2 = _mm_loadu_pd (icoeff+2);

  for (; i < len; i += 2) {
    t1 = _mm_add_pd (_mm_mul_pd (_mm_load_pd (a + 4*i + 0), f1),
                     _mm_mul_pd (_mm_load_pd (a + 4*i + 2), f2));
    t1 = _mm_add_sd (t1, _mm_unpackhi_pd (t1, t1));

    t2 = _mm_add_pd (_mm_mul_pd (_mm_load_pd (a + 4*i + 4), f1),
                     _mm_mul_pd (_mm_load_pd (a + 4*i + 6), f2));
    t2 = _mm_add_sd (t2, _mm_unpackhi_pd (t2, t2));

    _mm_store_pd (o + i, _mm_unpacklo_pd (t1, t2));
  }
}

#endif

#if defined (HAVE_SMMINTRIN_H) && defined(__SSE4_1__)
#include <smmintrin.h>

static inline void
inner_product_gint32_none_1_sse41 (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
  gint i = 0;
  __m128i sum, ta, tb;
  gint64 res;

  sum = _mm_setzero_si128 ();

  for (; i < len; i += 8) {
    ta = _mm_loadu_si128 ((__m128i *) (a + i));
    tb = _mm_load_si128 ((__m128i *) (b + i));

    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
            _mm_unpacklo_epi32 (tb, tb)));
    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
            _mm_unpackhi_epi32 (tb, tb)));

    ta = _mm_loadu_si128 ((__m128i *) (a + i + 4));
    tb = _mm_load_si128 ((__m128i *) (b + i + 4));

    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
            _mm_unpacklo_epi32 (tb, tb)));
    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
            _mm_unpackhi_epi32 (tb, tb)));
  }
  sum = _mm_add_epi64 (sum, _mm_unpackhi_epi64 (sum, sum));
  res = _mm_cvtsi128_si64 (sum);

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

static inline void
inner_product_gint32_linear_1_sse41 (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
  gint i = 0;
  gint64 res;
  __m128i sum, t, ta, tb;
  __m128i f = _mm_loadu_si128 ((__m128i *)icoeff);

  sum = _mm_setzero_si128 ();
  f = _mm_unpacklo_epi32 (f, f);

  for (; i < len; i += 4) {
    t = _mm_loadu_si128 ((__m128i *)(a + i));

    ta = _mm_unpacklo_epi32 (t, t);
    tb = _mm_load_si128 ((__m128i *)(b + 2*i + 0));

    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpacklo_epi64 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpackhi_epi64 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));

    ta = _mm_unpackhi_epi32 (t, t);
    tb = _mm_load_si128 ((__m128i *)(b + 2*i + 4));

    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpacklo_epi64 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum =
        _mm_add_epi64 (sum, _mm_mul_epi32 (_mm_unpackhi_epi64 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));
  }
  sum = _mm_srli_epi64 (sum, PRECISION_S32);
  sum = _mm_mul_epi32 (sum, f);
  sum = _mm_add_epi64 (sum, _mm_unpackhi_epi64 (sum, sum));
  res = _mm_cvtsi128_si64 (sum);

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

static inline void
inner_product_gint32_cubic_1_sse41 (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff)
{
  gint i = 0;
  gint64 res;
  __m128i sum1, sum2, t, ta, tb;
  __m128i f = _mm_loadu_si128 ((__m128i *)icoeff), f1, f2;

  sum1 = sum2 = _mm_setzero_si128 ();
  f1 = _mm_unpacklo_epi32 (f, f);
  f2 = _mm_unpackhi_epi32 (f, f);

  for (; i < len; i += 2) {
    t = _mm_cvtsi64_si128 (*(gint64 *)(a + i));
    t = _mm_unpacklo_epi32 (t, t);

    ta = _mm_unpacklo_epi64 (t, t);
    tb = _mm_load_si128 ((__m128i *)(b + 4*i + 0));

    sum1 =
        _mm_add_epi64 (sum1, _mm_mul_epi32 (ta, _mm_unpacklo_epi32 (tb, tb)));
    sum2 =
        _mm_add_epi64 (sum2, _mm_mul_epi32 (ta, _mm_unpackhi_epi32 (tb, tb)));

    ta = _mm_unpackhi_epi64 (t, t);
    tb = _mm_load_si128 ((__m128i *)(b + 4*i + 4));

    sum1 =
        _mm_add_epi64 (sum1, _mm_mul_epi32 (ta, _mm_unpacklo_epi32 (tb, tb)));
    sum2 =
        _mm_add_epi64 (sum2, _mm_mul_epi32 (ta, _mm_unpackhi_epi32 (tb, tb)));
  }
  sum1 = _mm_srli_epi64 (sum1, PRECISION_S32);
  sum2 = _mm_srli_epi64 (sum2, PRECISION_S32);
  sum1 = _mm_mul_epi32 (sum1, f1);
  sum2 = _mm_mul_epi32 (sum2, f2);
  sum1 = _mm_add_epi64 (sum1, sum2);
  sum1 = _mm_add_epi64 (sum1, _mm_unpackhi_epi64 (sum1, sum1));
  res = _mm_cvtsi128_si64 (sum1);

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

MAKE_RESAMPLE_FUNC (gint32, none, 1, sse41);
MAKE_RESAMPLE_FUNC (gint32, linear, 1, sse41);
MAKE_RESAMPLE_FUNC (gint32, cubic, 1, sse41);
#endif

static void
audio_resampler_check_x86 (const gchar *option)
{
  if (!strcmp (option, "sse")) {
#if defined (HAVE_XMMINTRIN_H) && defined(__SSE__)
    GST_DEBUG ("enable SSE optimisations");
    resample_gfloat_none_1 = resample_gfloat_none_1_sse;
    resample_gfloat_linear_1 = resample_gfloat_linear_1_sse;
    resample_gfloat_cubic_1 = resample_gfloat_cubic_1_sse;

    resample_gfloat_none_2 = resample_gfloat_none_2_sse;
#else
    GST_DEBUG ("SSE optimisations not enabled");
#endif
  } else if (!strcmp (option, "sse2")) {
#if defined (HAVE_EMMINTRIN_H) && defined(__SSE2__)
    GST_DEBUG ("enable SSE2 optimisations");
    resample_gint16_none_1 = resample_gint16_none_1_sse2;
    resample_gint16_linear_1 = resample_gint16_linear_1_sse2;
    resample_gint16_cubic_1 = resample_gint16_cubic_1_sse2;

    resample_gfloat_none_1 = resample_gfloat_none_1_sse;
    resample_gfloat_linear_1 = resample_gfloat_linear_1_sse;
    resample_gfloat_cubic_1 = resample_gfloat_cubic_1_sse;

    resample_gdouble_none_1 = resample_gdouble_none_1_sse2;
    resample_gdouble_linear_1 = resample_gdouble_linear_1_sse2;
    resample_gdouble_cubic_1 = resample_gdouble_cubic_1_sse2;

    resample_gint16_none_2 = resample_gint16_none_2_sse2;
    resample_gfloat_none_2 = resample_gfloat_none_2_sse;
    resample_gdouble_none_2 = resample_gdouble_none_2_sse2;

    interpolate_gdouble_linear = interpolate_gdouble_linear_sse2;
    interpolate_gdouble_cubic = interpolate_gdouble_cubic_sse2;
#else
    GST_DEBUG ("SSE2 optimisations not enabled");
#endif
  } else if (!strcmp (option, "sse41")) {
#if defined (HAVE_SMMINTRIN_H) && defined(__SSE4_1__)
    GST_DEBUG ("enable SSE41 optimisations");
    resample_gint32_none_1 = resample_gint32_none_1_sse41;
    resample_gint32_linear_1 = resample_gint32_linear_1_sse41;
    resample_gint32_cubic_1 = resample_gint32_cubic_1_sse41;
#else
    GST_DEBUG ("SSE41 optimisations not enabled");
#endif
  }
}
