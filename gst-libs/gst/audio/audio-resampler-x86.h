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

#ifdef HAVE_EMMINTRIN_H
#include <emmintrin.h>

static inline void
inner_product_gint16_1_sse2 (gint16 * o, const gint16 * a, const gint16 * b, gint len)
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
inner_product_gfloat_1_sse (gfloat * o, const gfloat * a, const gfloat * b, gint len)
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
inner_product_gdouble_1_sse2 (gdouble * o, const gdouble * a, const gdouble * b,
    gint len)
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
inner_product_gint16_2_sse2 (gint16 * o, const gint16 * a, const gint16 * b, gint len)
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
inner_product_gdouble_2_sse2 (gdouble * o, const gdouble * a, const gdouble * b,
    gint len)
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

MAKE_RESAMPLE_FUNC (gint16, 1, sse2);
MAKE_RESAMPLE_FUNC (gfloat, 1, sse);
MAKE_RESAMPLE_FUNC (gdouble, 1, sse2);
MAKE_RESAMPLE_FUNC (gint16, 2, sse2);
MAKE_RESAMPLE_FUNC (gdouble, 2, sse2);
#endif

static void
audio_resampler_check_x86 (const gchar *option)
{
#ifdef HAVE_EMMINTRIN_H
  if (!strcmp (option, "sse")) {
    GST_DEBUG ("enable SSE optimisations");
    resample_gfloat_1 = resample_gfloat_1_sse;
  } else if (!strcmp (option, "sse2")) {
    GST_DEBUG ("enable SSE2 optimisations");
    resample_gint16_1 = resample_gint16_1_sse2;
    resample_gdouble_1 = resample_gdouble_1_sse2;
    resample_gint16_2 = resample_gint16_2_sse2;
    resample_gdouble_2 = resample_gdouble_2_sse2;
  }
#endif
}
