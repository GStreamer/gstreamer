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

#if defined (HAVE_XMMINTRIN_H) && defined(__SSE__)
#include <xmmintrin.h>

static inline void
inner_product_gfloat_full_1_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
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
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
{
  gint i = 0;
  __m128 sum[2], t;
  const gfloat *c[2] = {(gfloat*)((gint8*)b + 0*bstride),
                        (gfloat*)((gint8*)b + 1*bstride)};

  sum[0] = sum[1] = _mm_setzero_ps ();

  for (; i < len; i += 8) {
    t = _mm_loadu_ps (a + i + 0);
    sum[0] = _mm_add_ps (sum[0], _mm_mul_ps (t, _mm_load_ps (c[0] + i + 0)));
    sum[1] = _mm_add_ps (sum[1], _mm_mul_ps (t, _mm_load_ps (c[1] + i + 0)));
    t = _mm_loadu_ps (a + i + 4);
    sum[0] = _mm_add_ps (sum[0], _mm_mul_ps (t, _mm_load_ps (c[0] + i + 4)));
    sum[1] = _mm_add_ps (sum[1], _mm_mul_ps (t, _mm_load_ps (c[1] + i + 4)));
  }
  sum[0] = _mm_mul_ps (_mm_sub_ps (sum[0], sum[1]), _mm_load1_ps (icoeff));
  sum[0] = _mm_add_ps (sum[0], sum[1]);
  sum[0] = _mm_add_ps (sum[0], _mm_movehl_ps (sum[0], sum[0]));
  sum[0] = _mm_add_ss (sum[0], _mm_shuffle_ps (sum[0], sum[0], 0x55));
  _mm_store_ss (o, sum[0]);
}

static inline void
inner_product_gfloat_cubic_1_sse (gfloat * o, const gfloat * a,
    const gfloat * b, gint len, const gfloat * icoeff, gint bstride)
{
  gint i = 0;
  __m128 sum[4];
  __m128 t, f = _mm_loadu_ps(icoeff);
  const gfloat *c[4] = {(gfloat*)((gint8*)b + 0*bstride),
                        (gfloat*)((gint8*)b + 1*bstride),
                        (gfloat*)((gint8*)b + 2*bstride),
                        (gfloat*)((gint8*)b + 3*bstride)};

  sum[0] = sum[1] = sum[2] = sum[3] = _mm_setzero_ps ();

  for (; i < len; i += 4) {
    t = _mm_loadu_ps (a + i);
    sum[0] = _mm_add_ps (sum[0], _mm_mul_ps (t, _mm_load_ps (c[0] + i)));
    sum[1] = _mm_add_ps (sum[1], _mm_mul_ps (t, _mm_load_ps (c[1] + i)));
    sum[2] = _mm_add_ps (sum[2], _mm_mul_ps (t, _mm_load_ps (c[2] + i)));
    sum[3] = _mm_add_ps (sum[3], _mm_mul_ps (t, _mm_load_ps (c[3] + i)));
  }
  sum[0] = _mm_mul_ps (sum[0], _mm_shuffle_ps (f, f, 0x00));
  sum[1] = _mm_mul_ps (sum[1], _mm_shuffle_ps (f, f, 0x55));
  sum[2] = _mm_mul_ps (sum[2], _mm_shuffle_ps (f, f, 0xaa));
  sum[3] = _mm_mul_ps (sum[3], _mm_shuffle_ps (f, f, 0xff));
  sum[0] = _mm_add_ps (sum[0], sum[1]);
  sum[2] = _mm_add_ps (sum[2], sum[3]);
  sum[0] = _mm_add_ps (sum[0], sum[2]);
  sum[0] = _mm_add_ps (sum[0], _mm_movehl_ps (sum[0], sum[0]));
  sum[0] = _mm_add_ss (sum[0], _mm_shuffle_ps (sum[0], sum[0], 0x55));
  _mm_store_ss (o, sum[0]);
}

MAKE_RESAMPLE_FUNC (gfloat, full, 1, sse);
MAKE_RESAMPLE_FUNC (gfloat, linear, 1, sse);
MAKE_RESAMPLE_FUNC (gfloat, cubic, 1, sse);

static void
interpolate_gfloat_linear_sse (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i;
  gfloat *o = op, *a = ap, *ic = icp;
  __m128 f[2], t1, t2;
  const gfloat *c[2] = {(gfloat*)((gint8*)a + 0*astride),
                        (gfloat*)((gint8*)a + 1*astride)};

  f[0] = _mm_load1_ps (ic+0);
  f[1] = _mm_load1_ps (ic+1);

  for (i = 0; i < len; i += 8) {
    t1 = _mm_mul_ps (_mm_load_ps (c[0] + i + 0), f[0]);
    t2 = _mm_mul_ps (_mm_load_ps (c[1] + i + 0), f[1]);
    _mm_store_ps (o + i + 0, _mm_add_ps (t1, t2));

    t1 = _mm_mul_ps (_mm_load_ps (c[0] + i + 4), f[0]);
    t2 = _mm_mul_ps (_mm_load_ps (c[1] + i + 4), f[1]);
    _mm_store_ps (o + i + 4, _mm_add_ps (t1, t2));
  }
}

static void
interpolate_gfloat_cubic_sse (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i;
  gfloat *o = op, *a = ap, *ic = icp;
  __m128 f[4], t[4];
  const gfloat *c[4] = {(gfloat*)((gint8*)a + 0*astride),
                        (gfloat*)((gint8*)a + 1*astride),
                        (gfloat*)((gint8*)a + 2*astride),
                        (gfloat*)((gint8*)a + 3*astride)};

  f[0] = _mm_load1_ps (ic+0);
  f[1] = _mm_load1_ps (ic+1);
  f[2] = _mm_load1_ps (ic+2);
  f[3] = _mm_load1_ps (ic+3);

  for (i = 0; i < len; i += 4) {
    t[0] = _mm_mul_ps (_mm_load_ps (c[0] + i + 0), f[0]);
    t[1] = _mm_mul_ps (_mm_load_ps (c[1] + i + 0), f[1]);
    t[2] = _mm_mul_ps (_mm_load_ps (c[2] + i + 0), f[2]);
    t[3] = _mm_mul_ps (_mm_load_ps (c[3] + i + 0), f[3]);
    t[0] = _mm_add_ps (t[0], t[1]);
    t[2] = _mm_add_ps (t[2], t[3]);
    _mm_store_ps (o + i + 0, _mm_add_ps (t[0], t[2]));
  }
}

#endif

#if defined (HAVE_EMMINTRIN_H) && defined(__SSE2__)
#include <emmintrin.h>

static inline void
inner_product_gint16_full_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint i;
  __m128i sum, t;

  sum = _mm_setzero_si128 ();

  for (i = 0; i < len; i += 16) {
    t = _mm_loadu_si128 ((__m128i *) (a + i));
    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (b + i + 0))));

    t = _mm_loadu_si128 ((__m128i *) (a + i + 8));
    sum = _mm_add_epi32 (sum, _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (b + i + 8))));
  }
  sum = _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (2, 3, 2, 3)));
  sum = _mm_add_epi32 (sum, _mm_shuffle_epi32 (sum, _MM_SHUFFLE (1, 1, 1, 1)));

  sum = _mm_add_epi32 (sum, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum = _mm_srai_epi32 (sum, PRECISION_S16);
  sum = _mm_packs_epi32 (sum, sum);
  *o = _mm_extract_epi16 (sum, 0);
}

static inline void
inner_product_gint16_linear_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint i = 0;
  __m128i sum[2], t;
  __m128i f = _mm_set_epi64x (0, *((gint64*)icoeff));
  const gint16 *c[2] = {(gint16*)((gint8*)b + 0*bstride),
                        (gint16*)((gint8*)b + 1*bstride)};

  sum[0] = sum[1] = _mm_setzero_si128 ();
  f = _mm_unpacklo_epi16 (f, sum[0]);

  for (; i < len; i += 16) {
    t = _mm_loadu_si128 ((__m128i *) (a + i + 0));
    sum[0] = _mm_add_epi32 (sum[0], _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (c[0] + i + 0))));
    sum[1] = _mm_add_epi32 (sum[1], _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (c[1] + i + 0))));

    t = _mm_loadu_si128 ((__m128i *) (a + i + 8));
    sum[0] = _mm_add_epi32 (sum[0], _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (c[0] + i + 8))));
    sum[1] = _mm_add_epi32 (sum[1], _mm_madd_epi16 (t, _mm_load_si128 ((__m128i *) (c[1] + i + 8))));
  }
  sum[0] = _mm_srai_epi32 (sum[0], PRECISION_S16);
  sum[1] = _mm_srai_epi32 (sum[1], PRECISION_S16);

  sum[0] = _mm_madd_epi16 (sum[0], _mm_shuffle_epi32 (f,  _MM_SHUFFLE (0, 0, 0, 0)));
  sum[1] = _mm_madd_epi16 (sum[1], _mm_shuffle_epi32 (f,  _MM_SHUFFLE (1, 1, 1, 1)));
  sum[0] = _mm_add_epi32 (sum[0], sum[1]);

  sum[0] = _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (2, 3, 2, 3)));
  sum[0] = _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (1, 1, 1, 1)));

  sum[0] = _mm_add_epi32 (sum[0], _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum[0] = _mm_srai_epi32 (sum[0], PRECISION_S16);
  sum[0] = _mm_packs_epi32 (sum[0], sum[0]);
  *o = _mm_extract_epi16 (sum[0], 0);
}

static inline void
inner_product_gint16_cubic_1_sse2 (gint16 * o, const gint16 * a,
    const gint16 * b, gint len, const gint16 * icoeff, gint bstride)
{
  gint i = 0;
  __m128i sum[4], t[4];
  __m128i f = _mm_set_epi64x (0, *((long long*)icoeff));
  const gint16 *c[4] = {(gint16*)((gint8*)b + 0*bstride),
                        (gint16*)((gint8*)b + 1*bstride),
                        (gint16*)((gint8*)b + 2*bstride),
                        (gint16*)((gint8*)b + 3*bstride)};

  sum[0] = sum[1] = sum[2] = sum[3] = _mm_setzero_si128 ();
  f = _mm_unpacklo_epi16 (f, sum[0]);

  for (; i < len; i += 8) {
    t[0] = _mm_loadu_si128 ((__m128i *) (a + i));
    sum[0] = _mm_add_epi32 (sum[0], _mm_madd_epi16 (t[0], _mm_load_si128 ((__m128i *) (c[0] + i))));
    sum[1] = _mm_add_epi32 (sum[1], _mm_madd_epi16 (t[0], _mm_load_si128 ((__m128i *) (c[1] + i))));
    sum[2] = _mm_add_epi32 (sum[2], _mm_madd_epi16 (t[0], _mm_load_si128 ((__m128i *) (c[2] + i))));
    sum[3] = _mm_add_epi32 (sum[3], _mm_madd_epi16 (t[0], _mm_load_si128 ((__m128i *) (c[3] + i))));
  }
  t[0] = _mm_unpacklo_epi32 (sum[0], sum[1]);
  t[1] = _mm_unpacklo_epi32 (sum[2], sum[3]);
  t[2] = _mm_unpackhi_epi32 (sum[0], sum[1]);
  t[3] = _mm_unpackhi_epi32 (sum[2], sum[3]);

  sum[0] = _mm_add_epi32 (_mm_unpacklo_epi64(t[0], t[1]), _mm_unpackhi_epi64(t[0], t[1]));
  sum[2] = _mm_add_epi32 (_mm_unpacklo_epi64(t[2], t[3]), _mm_unpackhi_epi64(t[2], t[3]));
  sum[0] = _mm_add_epi32 (sum[0], sum[2]);

  sum[0] = _mm_srai_epi32 (sum[0], PRECISION_S16);
  sum[0] = _mm_madd_epi16 (sum[0], f);

  sum[0] = _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (2, 3, 2, 3)));
  sum[0] = _mm_add_epi32 (sum[0], _mm_shuffle_epi32 (sum[0], _MM_SHUFFLE (1, 1, 1, 1)));

  sum[0] = _mm_add_epi32 (sum[0], _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
  sum[0] = _mm_srai_epi32 (sum[0], PRECISION_S16);
  sum[0] = _mm_packs_epi32 (sum[0], sum[0]);
  *o = _mm_extract_epi16 (sum[0], 0);
}

static inline void
inner_product_gdouble_full_1_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff, gint bstride)
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
    const gdouble * b, gint len, const gdouble * icoeff, gint bstride)
{
  gint i = 0;
  __m128d sum[2], t;
  const gdouble *c[2] = {(gdouble*)((gint8*)b + 0*bstride),
                         (gdouble*)((gint8*)b + 1*bstride)};

  sum[0] = sum[1] = _mm_setzero_pd ();

  for (; i < len; i += 4) {
    t = _mm_loadu_pd (a + i + 0);
    sum[0] = _mm_add_pd (sum[0], _mm_mul_pd (t, _mm_load_pd (c[0] + i + 0)));
    sum[1] = _mm_add_pd (sum[1], _mm_mul_pd (t, _mm_load_pd (c[1] + i + 0)));
    t = _mm_loadu_pd (a + i + 2);
    sum[0] = _mm_add_pd (sum[0], _mm_mul_pd (t, _mm_load_pd (c[0] + i + 2)));
    sum[1] = _mm_add_pd (sum[1], _mm_mul_pd (t, _mm_load_pd (c[1] + i + 2)));
  }
  sum[0] = _mm_mul_pd (_mm_sub_pd (sum[0], sum[1]), _mm_load1_pd (icoeff));
  sum[0] = _mm_add_pd (sum[0], sum[1]);
  sum[0] = _mm_add_sd (sum[0], _mm_unpackhi_pd (sum[0], sum[0]));
  _mm_store_sd (o, sum[0]);
}

static inline void
inner_product_gdouble_cubic_1_sse2 (gdouble * o, const gdouble * a,
    const gdouble * b, gint len, const gdouble * icoeff, gint bstride)
{
  gint i;
  __m128d f[2], sum[4], t;
  const gdouble *c[4] = {(gdouble*)((gint8*)b + 0*bstride),
                         (gdouble*)((gint8*)b + 1*bstride),
                         (gdouble*)((gint8*)b + 2*bstride),
                         (gdouble*)((gint8*)b + 3*bstride)};

  f[0] = _mm_loadu_pd (icoeff + 0);
  f[1] = _mm_loadu_pd (icoeff + 2);
  sum[0] = sum[1] = sum[2] = sum[3] = _mm_setzero_pd ();

  for (i = 0; i < len; i += 2) {
    t = _mm_loadu_pd (a + i + 0);
    sum[0] = _mm_add_pd (sum[0], _mm_mul_pd (t, _mm_load_pd (c[0] + i)));
    sum[1] = _mm_add_pd (sum[1], _mm_mul_pd (t, _mm_load_pd (c[1] + i)));
    sum[2] = _mm_add_pd (sum[2], _mm_mul_pd (t, _mm_load_pd (c[2] + i)));
    sum[3] = _mm_add_pd (sum[3], _mm_mul_pd (t, _mm_load_pd (c[3] + i)));
  }
  sum[0] = _mm_mul_pd (sum[0], _mm_shuffle_pd (f[0], f[0], _MM_SHUFFLE2 (0, 0)));
  sum[1] = _mm_mul_pd (sum[1], _mm_shuffle_pd (f[0], f[0], _MM_SHUFFLE2 (1, 1)));
  sum[2] = _mm_mul_pd (sum[2], _mm_shuffle_pd (f[1], f[1], _MM_SHUFFLE2 (0, 0)));
  sum[3] = _mm_mul_pd (sum[3], _mm_shuffle_pd (f[1], f[1], _MM_SHUFFLE2 (1, 1)));
  sum[0] = _mm_add_pd (sum[0], sum[1]);
  sum[2] = _mm_add_pd (sum[2], sum[3]);
  sum[0] = _mm_add_pd (sum[0], sum[2]);
  sum[0] = _mm_add_sd (sum[0], _mm_unpackhi_pd (sum[0], sum[0]));
  _mm_store_sd (o, sum[0]);
}

MAKE_RESAMPLE_FUNC (gint16, full, 1, sse2);
MAKE_RESAMPLE_FUNC (gint16, linear, 1, sse2);
MAKE_RESAMPLE_FUNC (gint16, cubic, 1, sse2);

MAKE_RESAMPLE_FUNC (gdouble, full, 1, sse2);
MAKE_RESAMPLE_FUNC (gdouble, linear, 1, sse2);
MAKE_RESAMPLE_FUNC (gdouble, cubic, 1, sse2);

static inline void
interpolate_gint16_linear_sse2 (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i = 0;
  gint16 *o = op, *a = ap, *ic = icp;
  __m128i ta, tb, t1, t2;
  __m128i f = _mm_set_epi64x (0, *((gint64*)ic));
  const gint16 *c[2] = {(gint16*)((gint8*)a + 0*astride),
                        (gint16*)((gint8*)a + 1*astride)};

  f = _mm_unpacklo_epi32 (f, f);
  f = _mm_unpacklo_epi64 (f, f);

  for (; i < len; i += 8) {
    ta = _mm_load_si128 ((__m128i *) (c[0] + i));
    tb = _mm_load_si128 ((__m128i *) (c[1] + i));

    t1 = _mm_madd_epi16 (_mm_unpacklo_epi16 (ta, tb), f);
    t2 = _mm_madd_epi16 (_mm_unpackhi_epi16 (ta, tb), f);

    t1 = _mm_add_epi32 (t1, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
    t2 = _mm_add_epi32 (t2, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));

    t1 = _mm_srai_epi32 (t1, PRECISION_S16);
    t2 = _mm_srai_epi32 (t2, PRECISION_S16);

    t1 = _mm_packs_epi32 (t1, t2);
    _mm_store_si128 ((__m128i *) (o + i), t1);
  }
}

static inline void
interpolate_gint16_cubic_sse2 (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i = 0;
  gint16 *o = op, *a = ap, *ic = icp;
  __m128i ta, tb, tl1, tl2, th1, th2;
  __m128i f[2];
  const gint16 *c[4] = {(gint16*)((gint8*)a + 0*astride),
                        (gint16*)((gint8*)a + 1*astride),
                        (gint16*)((gint8*)a + 2*astride),
                        (gint16*)((gint8*)a + 3*astride)};

  f[0] = _mm_set_epi16 (ic[1], ic[0], ic[1], ic[0], ic[1], ic[0], ic[1], ic[0]);
  f[1] = _mm_set_epi16 (ic[3], ic[2], ic[3], ic[2], ic[3], ic[2], ic[3], ic[2]);

  for (; i < len; i += 8) {
    ta = _mm_load_si128 ((__m128i *) (c[0] + i));
    tb = _mm_load_si128 ((__m128i *) (c[1] + i));

    tl1 = _mm_madd_epi16 (_mm_unpacklo_epi16 (ta, tb), f[0]);
    th1 = _mm_madd_epi16 (_mm_unpackhi_epi16 (ta, tb), f[0]);

    ta = _mm_load_si128 ((__m128i *) (c[2] + i));
    tb = _mm_load_si128 ((__m128i *) (c[3] + i));

    tl2 = _mm_madd_epi16 (_mm_unpacklo_epi16 (ta, tb), f[1]);
    th2 = _mm_madd_epi16 (_mm_unpackhi_epi16 (ta, tb), f[1]);

    tl1 = _mm_add_epi32 (tl1, tl2);
    th1 = _mm_add_epi32 (th1, th2);

    tl1 = _mm_add_epi32 (tl1, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));
    th1 = _mm_add_epi32 (th1, _mm_set1_epi32 (1 << (PRECISION_S16 - 1)));

    tl1 = _mm_srai_epi32 (tl1, PRECISION_S16);
    th1 = _mm_srai_epi32 (th1, PRECISION_S16);

    tl1 = _mm_packs_epi32 (tl1, th1);
    _mm_store_si128 ((__m128i *) (o + i), tl1);
  }
}

static void
interpolate_gdouble_linear_sse2 (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i;
  gdouble *o = op, *a = ap, *ic = icp;
  __m128d f[2], t1, t2;
  const gdouble *c[2] = {(gdouble*)((gint8*)a + 0*astride),
                         (gdouble*)((gint8*)a + 1*astride)};

  f[0] = _mm_load1_pd (ic+0);
  f[1] = _mm_load1_pd (ic+1);

  for (i = 0; i < len; i += 4) {
    t1 = _mm_mul_pd (_mm_load_pd (c[0] + i + 0), f[0]);
    t2 = _mm_mul_pd (_mm_load_pd (c[1] + i + 0), f[1]);
    _mm_store_pd (o + i + 0, _mm_add_pd (t1, t2));

    t1 = _mm_mul_pd (_mm_load_pd (c[0] + i + 2), f[0]);
    t2 = _mm_mul_pd (_mm_load_pd (c[1] + i + 2), f[1]);
    _mm_store_pd (o + i + 2, _mm_add_pd (t1, t2));
  }
}

static void
interpolate_gdouble_cubic_sse2 (gpointer op, const gpointer ap,
    gint len, const gpointer icp, gint astride)
{
  gint i;
  gdouble *o = op, *a = ap, *ic = icp;
  __m128d f[4], t[4];
  const gdouble *c[4] = {(gdouble*)((gint8*)a + 0*astride),
                         (gdouble*)((gint8*)a + 1*astride),
                         (gdouble*)((gint8*)a + 2*astride),
                         (gdouble*)((gint8*)a + 3*astride)};

  f[0] = _mm_load1_pd (ic+0);
  f[1] = _mm_load1_pd (ic+1);
  f[2] = _mm_load1_pd (ic+2);
  f[3] = _mm_load1_pd (ic+3);

  for (i = 0; i < len; i += 2) {
    t[0] = _mm_mul_pd (_mm_load_pd (c[0] + i + 0), f[0]);
    t[1] = _mm_mul_pd (_mm_load_pd (c[1] + i + 0), f[1]);
    t[2] = _mm_mul_pd (_mm_load_pd (c[2] + i + 0), f[2]);
    t[3] = _mm_mul_pd (_mm_load_pd (c[3] + i + 0), f[3]);
    t[0] = _mm_add_pd (t[0], t[1]);
    t[2] = _mm_add_pd (t[2], t[3]);
    _mm_store_pd (o + i + 0, _mm_add_pd (t[0], t[2]));
  }
}

#endif

#if 0
#define __SSE4_1__
#pragma GCC target("sse4.1")
#endif

#if defined (HAVE_SMMINTRIN_H) && defined(__SSE4_1__)
#include <smmintrin.h>

static inline void
inner_product_gint32_full_1_sse41 (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
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
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
{
  gint i = 0;
  gint64 res;
  __m128i sum[2], ta, tb;
  __m128i f = _mm_loadu_si128 ((__m128i *)icoeff);
  const gint32 *c[2] = {(gint32*)((gint8*)b + 0*bstride),
                        (gint32*)((gint8*)b + 1*bstride)};

  sum[0] = sum[1] = _mm_setzero_si128 ();

  for (; i < len; i += 4) {
    ta = _mm_loadu_si128 ((__m128i *)(a + i));

    tb = _mm_load_si128 ((__m128i *)(c[0] + i));
    sum[0] = _mm_add_epi64 (sum[0], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[0] = _mm_add_epi64 (sum[0], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));

    tb = _mm_load_si128 ((__m128i *)(c[1] + i));
    sum[1] = _mm_add_epi64 (sum[1], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[1] = _mm_add_epi64 (sum[1], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));
  }
  sum[0] = _mm_srli_epi64 (sum[0], PRECISION_S32);
  sum[1] = _mm_srli_epi64 (sum[1], PRECISION_S32);
  sum[0] = _mm_mul_epi32 (sum[0], _mm_shuffle_epi32 (f, _MM_SHUFFLE (0, 0, 0, 0)));
  sum[1] = _mm_mul_epi32 (sum[1], _mm_shuffle_epi32 (f, _MM_SHUFFLE (1, 1, 1, 1)));
  sum[0] = _mm_add_epi64 (sum[0], sum[1]);
  sum[0] = _mm_add_epi64 (sum[0], _mm_unpackhi_epi64 (sum[0], sum[0]));
  res = _mm_cvtsi128_si64 (sum[0]);

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

static inline void
inner_product_gint32_cubic_1_sse41 (gint32 * o, const gint32 * a,
    const gint32 * b, gint len, const gint32 * icoeff, gint bstride)
{
  gint i = 0;
  gint64 res;
  __m128i sum[4], ta, tb;
  __m128i f = _mm_loadu_si128 ((__m128i *)icoeff);
  const gint32 *c[4] = {(gint32*)((gint8*)b + 0*bstride),
                        (gint32*)((gint8*)b + 1*bstride),
                        (gint32*)((gint8*)b + 2*bstride),
                        (gint32*)((gint8*)b + 3*bstride)};

  sum[0] = sum[1] = sum[2] = sum[3] = _mm_setzero_si128 ();

  for (; i < len; i += 4) {
    ta = _mm_loadu_si128 ((__m128i *)(a + i));

    tb = _mm_load_si128 ((__m128i *)(c[0] + i));
    sum[0] = _mm_add_epi64 (sum[0], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[0] = _mm_add_epi64 (sum[0], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));

    tb = _mm_load_si128 ((__m128i *)(c[1] + i));
    sum[1] = _mm_add_epi64 (sum[1], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[1] = _mm_add_epi64 (sum[1], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));

    tb = _mm_load_si128 ((__m128i *)(c[2] + i));
    sum[2] = _mm_add_epi64 (sum[2], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[2] = _mm_add_epi64 (sum[2], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));

    tb = _mm_load_si128 ((__m128i *)(c[3] + i));
    sum[3] = _mm_add_epi64 (sum[3], _mm_mul_epi32 (_mm_unpacklo_epi32 (ta, ta),
              _mm_unpacklo_epi32 (tb, tb)));
    sum[3] = _mm_add_epi64 (sum[3], _mm_mul_epi32 (_mm_unpackhi_epi32 (ta, ta),
              _mm_unpackhi_epi32 (tb, tb)));
  }
  sum[0] = _mm_srli_epi64 (sum[0], PRECISION_S32);
  sum[1] = _mm_srli_epi64 (sum[1], PRECISION_S32);
  sum[2] = _mm_srli_epi64 (sum[2], PRECISION_S32);
  sum[3] = _mm_srli_epi64 (sum[3], PRECISION_S32);
  sum[0] = _mm_mul_epi32 (sum[0], _mm_shuffle_epi32 (f, _MM_SHUFFLE (0, 0, 0, 0)));
  sum[1] = _mm_mul_epi32 (sum[1], _mm_shuffle_epi32 (f, _MM_SHUFFLE (1, 1, 1, 1)));
  sum[2] = _mm_mul_epi32 (sum[2], _mm_shuffle_epi32 (f, _MM_SHUFFLE (2, 2, 2, 2)));
  sum[3] = _mm_mul_epi32 (sum[3], _mm_shuffle_epi32 (f, _MM_SHUFFLE (3, 3, 3, 3)));
  sum[0] = _mm_add_epi64 (sum[0], sum[1]);
  sum[2] = _mm_add_epi64 (sum[2], sum[3]);
  sum[0] = _mm_add_epi64 (sum[0], sum[2]);
  sum[0] = _mm_add_epi64 (sum[0], _mm_unpackhi_epi64 (sum[0], sum[0]));
  res = _mm_cvtsi128_si64 (sum[0]);

  res = (res + (1 << (PRECISION_S32 - 1))) >> PRECISION_S32;
  *o = CLAMP (res, -(1L << 31), (1L << 31) - 1);
}

MAKE_RESAMPLE_FUNC (gint32, full, 1, sse41);
MAKE_RESAMPLE_FUNC (gint32, linear, 1, sse41);
MAKE_RESAMPLE_FUNC (gint32, cubic, 1, sse41);
#endif

static void
audio_resampler_check_x86 (const gchar *option)
{
  if (!strcmp (option, "sse")) {
#if defined (HAVE_XMMINTRIN_H) && defined(__SSE__)
    GST_DEBUG ("enable SSE optimisations");
    resample_gfloat_full_1 = resample_gfloat_full_1_sse;
    resample_gfloat_linear_1 = resample_gfloat_linear_1_sse;
    resample_gfloat_cubic_1 = resample_gfloat_cubic_1_sse;

    interpolate_gfloat_linear = interpolate_gfloat_linear_sse;
    interpolate_gfloat_cubic = interpolate_gfloat_cubic_sse;
#else
    GST_DEBUG ("SSE optimisations not enabled");
#endif
  } else if (!strcmp (option, "sse2")) {
#if defined (HAVE_EMMINTRIN_H) && defined(__SSE2__)
    GST_DEBUG ("enable SSE2 optimisations");
    resample_gint16_full_1 = resample_gint16_full_1_sse2;
    resample_gint16_linear_1 = resample_gint16_linear_1_sse2;
    resample_gint16_cubic_1 = resample_gint16_cubic_1_sse2;

    interpolate_gint16_linear = interpolate_gint16_linear_sse2;
    interpolate_gint16_cubic = interpolate_gint16_cubic_sse2;

    resample_gdouble_full_1 = resample_gdouble_full_1_sse2;
    resample_gdouble_linear_1 = resample_gdouble_linear_1_sse2;
    resample_gdouble_cubic_1 = resample_gdouble_cubic_1_sse2;

    interpolate_gdouble_linear = interpolate_gdouble_linear_sse2;
    interpolate_gdouble_cubic = interpolate_gdouble_cubic_sse2;
#else
    GST_DEBUG ("SSE2 optimisations not enabled");
#endif
  } else if (!strcmp (option, "sse41")) {
#if defined (HAVE_SMMINTRIN_H) && defined(__SSE4_1__)
    GST_DEBUG ("enable SSE41 optimisations");
    resample_gint32_full_1 = resample_gint32_full_1_sse41;
    resample_gint32_linear_1 = resample_gint32_linear_1_sse41;
    resample_gint32_cubic_1 = resample_gint32_cubic_1_sse41;
#else
    GST_DEBUG ("SSE41 optimisations not enabled");
#endif
  }
}
