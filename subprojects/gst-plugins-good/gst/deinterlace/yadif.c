/*
 * GStreamer
 * Copyright (C) 2019 Jan Schmidt <jan@centricular.com>
 *
 * Portions of this file extracted from libav
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
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
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#ifdef HAVE_ORC
#include <orc/orc.h>
#include <orc/orcsse.h>
#endif
#include "gstdeinterlacemethod.h"
#include "yadif.h"

#if    __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96) || defined(__clang__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE inline
#endif
#ifndef ORC_RESTRICT
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define ORC_RESTRICT restrict
#elif defined(__GNUC__) && __GNUC__ >= 4
#define ORC_RESTRICT __restrict__
#else
#define ORC_RESTRICT
#endif
#endif

#define GST_TYPE_DEINTERLACE_METHOD_YADIF	(gst_deinterlace_method_yadif_get_type ())
#define GST_IS_DEINTERLACE_METHOD_YADIF(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_YADIF))
#define GST_IS_DEINTERLACE_METHOD_YADIF_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_YADIF))
#define GST_DEINTERLACE_METHOD_YADIF_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_YADIF, GstDeinterlaceMethodYadifClass))
#define GST_DEINTERLACE_METHOD_YADIF(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_YADIF, GstDeinterlaceMethodYadif))
#define GST_DEINTERLACE_METHOD_YADIF_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_YADIF, GstDeinterlaceMethodYadifClass))
#define GST_DEINTERLACE_METHOD_YADIF_CAST(obj)	((GstDeinterlaceMethodYadif*)(obj))

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodYadif;
typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodYadifClass;

G_DEFINE_TYPE (GstDeinterlaceMethodYadif,
    gst_deinterlace_method_yadif, GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
filter_scanline_yadif (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size,
    int colors, int y_alternates_every);

static void
filter_scanline_yadif_planar (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_planar_16bits (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_semiplanar (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_packed_4 (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_packed_yvyu (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_packed_uyvy (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_scanline_yadif_packed_3 (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint size);

static void
filter_line_c_planar_mode0 (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void
filter_line_c_planar_mode2 (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void
filter_line_c_planar_mode0_16bits (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void
filter_line_c_planar_mode2_16bits (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void (*filter_mode2) (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void (*filter_mode0) (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void (*filter_mode2_16bits) (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void (*filter_mode0_16bits) (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w);

static void
copy_scanline (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines, guint size)
{
  memcpy (out, scanlines->m0, size);
}

static void
    gst_deinterlace_method_yadif_class_init
    (GstDeinterlaceMethodYadifClass * klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;

  dim_class->name = "YADIF Adaptive Deinterlacer";
  dim_class->nick = "yadif";
  dim_class->fields_required = 5;
  dim_class->latency = 2;

  dism_class->copy_scanline_planar_y = copy_scanline;
  dism_class->copy_scanline_planar_u = copy_scanline;
  dism_class->copy_scanline_planar_v = copy_scanline;
  dism_class->copy_scanline_yuy2 = copy_scanline;
  dism_class->copy_scanline_yvyu = copy_scanline;
  dism_class->copy_scanline_uyvy = copy_scanline;
  dism_class->copy_scanline_ayuv = copy_scanline;
  dism_class->copy_scanline_argb = copy_scanline;
  dism_class->copy_scanline_abgr = copy_scanline;
  dism_class->copy_scanline_rgba = copy_scanline;
  dism_class->copy_scanline_bgra = copy_scanline;
  dism_class->copy_scanline_rgb = copy_scanline;
  dism_class->copy_scanline_bgr = copy_scanline;
  dism_class->copy_scanline_nv12 = copy_scanline;
  dism_class->copy_scanline_nv21 = copy_scanline;
  dism_class->copy_scanline_planar_y_16bits = copy_scanline;
  dism_class->copy_scanline_planar_u_16bits = copy_scanline;
  dism_class->copy_scanline_planar_v_16bits = copy_scanline;

  dism_class->interpolate_scanline_planar_y = filter_scanline_yadif_planar;
  dism_class->interpolate_scanline_planar_u = filter_scanline_yadif_planar;
  dism_class->interpolate_scanline_planar_v = filter_scanline_yadif_planar;
  dism_class->interpolate_scanline_yuy2 = filter_scanline_yadif_packed_yvyu;
  dism_class->interpolate_scanline_yvyu = filter_scanline_yadif_packed_yvyu;
  dism_class->interpolate_scanline_uyvy = filter_scanline_yadif_packed_uyvy;
  dism_class->interpolate_scanline_ayuv = filter_scanline_yadif_packed_4;
  dism_class->interpolate_scanline_argb = filter_scanline_yadif_packed_4;
  dism_class->interpolate_scanline_abgr = filter_scanline_yadif_packed_4;
  dism_class->interpolate_scanline_rgba = filter_scanline_yadif_packed_4;
  dism_class->interpolate_scanline_bgra = filter_scanline_yadif_packed_4;
  dism_class->interpolate_scanline_rgb = filter_scanline_yadif_packed_3;
  dism_class->interpolate_scanline_bgr = filter_scanline_yadif_packed_3;
  dism_class->interpolate_scanline_nv12 = filter_scanline_yadif_semiplanar;
  dism_class->interpolate_scanline_nv21 = filter_scanline_yadif_semiplanar;
  dism_class->interpolate_scanline_planar_y_16bits =
      filter_scanline_yadif_planar_16bits;
  dism_class->interpolate_scanline_planar_u_16bits =
      filter_scanline_yadif_planar_16bits;
  dism_class->interpolate_scanline_planar_v_16bits =
      filter_scanline_yadif_planar_16bits;
}

#define FFABS(a) ABS(a)
#define FFMIN(a,b) MIN(a,b)
#define FFMAX(a,b) MAX(a,b)
#define FFMAX3(a,b,c) FFMAX(FFMAX(a,b),c)
#define FFMIN3(a,b,c) FFMIN(FFMIN(a,b),c)


#define CHECK(j)\
    {   int score = FFABS(stzero[x - colors2 + j] - sbzero[x - colors2 - j])\
                  + FFABS(stzero[x  + j] - sbzero[x  - j])\
                  + FFABS(stzero[x + colors2 + j] - sbzero[x + colors2 - j]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (stzero[x  + j] + sbzero[x - j])>>1;\

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge) G_STMT_START { \
    for (x = start;  x < end; x++) { \
        int c = stzero[x]; \
        int d = (smone[x] + smp[x])>>1; \
        int e = sbzero[x]; \
        int temporal_diff0 = FFABS(smone[x] - smp[x]); \
        int temporal_diff1 =(FFABS(sttwo[x] - c) + FFABS(sbtwo[x] - e) )>>1; \
        int temporal_diff2 =(FFABS(stptwo[x] - c) + FFABS(sbptwo[x] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
        int colors2 = colors; \
        if ((y_alternates_every == 1 && (x%2 == 0)) || \
           (y_alternates_every == 2 && (x%2 == 1))) \
          colors2 = 2; \
 \
        if (is_not_edge) {\
            int spatial_score = FFABS(stzero[x-colors2] - sbzero[x-colors2]) + FFABS(c-e) \
                              + FFABS(stzero[x+colors2] - sbzero[x+colors2]); \
            CHECK(-1 * colors2) CHECK(-2 * colors2) }} }} \
            CHECK(colors2) CHECK(2 * colors2) }} }} \
        }\
 \
        if (!(mode&2)) { \
            int b = (sttone[x] + sttp[x])>>1; \
            int f = (sbbone[x] + sbbp[x])>>1; \
            int max = FFMAX3(d - e, d - c, FFMIN(b - c, f - e)); \
            int min = FFMIN3(d - e, d - c, FFMAX(b - c, f - e)); \
 \
            diff = FFMAX3(diff, min, -max); \
        } \
 \
        if (spatial_pred > d + diff) \
           spatial_pred = d + diff; \
        else if (spatial_pred < d - diff) \
           spatial_pred = d - diff; \
 \
        sdst[x] = spatial_pred; \
 \
    } \
} G_STMT_END

ALWAYS_INLINE static void
filter_line_c (guint8 * sdst, const guint8 * stzero, const guint8 * sbzero,
    const guint8 * smone, const guint8 * smp, const guint8 * sttwo,
    const guint8 * sbtwo, const guint8 * stptwo, const guint8 * sbptwo,
    const guint8 * sttone, const guint8 * sttp, const guint8 * sbbone,
    const guint8 * sbbp, int w, int colors, int y_alternates_every, int start,
    int end, int mode)
{
  int x;

  /* The function is called for processing the middle
   * pixels of each line, excluding 3 at each end.
   * This allows the FILTER macro to be
   * called so that it processes all the pixels normally.  A constant value of
   * true for is_not_edge lets the compiler ignore the if statement. */
  FILTER (start, end, 1);
}

#define MAX_ALIGN 8

ALWAYS_INLINE static void
filter_line_c_planar (void *ORC_RESTRICT dst, const void *ORC_RESTRICT tzero,
    const void *ORC_RESTRICT bzero, const void *ORC_RESTRICT mone,
    const void *ORC_RESTRICT mp, const void *ORC_RESTRICT ttwo,
    const void *ORC_RESTRICT btwo, const void *ORC_RESTRICT tptwo,
    const void *ORC_RESTRICT bptwo, const void *ORC_RESTRICT ttone,
    const void *ORC_RESTRICT ttp, const void *ORC_RESTRICT bbone,
    const void *ORC_RESTRICT bbp, int w, int mode)
{
  int x;
  const int start = 0;
  const int colors = 1;
  const int y_alternates_every = 0;
  /* hardcode colors = 1, bpp = 1 */
  const int end = w;
  guint8 *sdst = (guint8 *) dst + 3;
  guint8 *stzero = (guint8 *) tzero + 3;
  guint8 *sbzero = (guint8 *) bzero + 3;
  guint8 *smone = (guint8 *) mone + 3;
  guint8 *smp = (guint8 *) mp + 3;
  guint8 *sttwo = (guint8 *) ttwo + 3;
  guint8 *sbtwo = (guint8 *) btwo + 3;
  guint8 *stptwo = (guint8 *) tptwo + 3;
  guint8 *sbptwo = (guint8 *) bptwo + 3;
  guint8 *sttone = (guint8 *) ttone + 3;
  guint8 *sttp = (guint8 *) ttp + 3;
  guint8 *sbbone = (guint8 *) bbone + 3;
  guint8 *sbbp = (guint8 *) bbp + 3;
  /* The function is called for processing the middle
   * pixels of each line, excluding 3 at each end.
   * This allows the FILTER macro to be
   * called so that it processes all the pixels normally.  A constant value of
   * true for is_not_edge lets the compiler ignore the if statement. */
  FILTER (start, end, 1);
}

ALWAYS_INLINE static void
filter_line_c_planar_16bits (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero,
    const void *ORC_RESTRICT bzero, const void *ORC_RESTRICT mone,
    const void *ORC_RESTRICT mp, const void *ORC_RESTRICT ttwo,
    const void *ORC_RESTRICT btwo, const void *ORC_RESTRICT tptwo,
    const void *ORC_RESTRICT bptwo, const void *ORC_RESTRICT ttone,
    const void *ORC_RESTRICT ttp, const void *ORC_RESTRICT bbone,
    const void *ORC_RESTRICT bbp, int w, int mode)
{
  int x;
  const int start = 0;
  const int colors = 1;
  const int y_alternates_every = 0;
  const int end = w;
  guint16 *sdst = (guint16 *) dst + 3;
  guint16 *stzero = (guint16 *) tzero + 3;
  guint16 *sbzero = (guint16 *) bzero + 3;
  guint16 *smone = (guint16 *) mone + 3;
  guint16 *smp = (guint16 *) mp + 3;
  guint16 *sttwo = (guint16 *) ttwo + 3;
  guint16 *sbtwo = (guint16 *) btwo + 3;
  guint16 *stptwo = (guint16 *) tptwo + 3;
  guint16 *sbptwo = (guint16 *) bptwo + 3;
  guint16 *sttone = (guint16 *) ttone + 3;
  guint16 *sttp = (guint16 *) ttp + 3;
  guint16 *sbbone = (guint16 *) bbone + 3;
  guint16 *sbbp = (guint16 *) bbp + 3;
  /* The function is called for processing the middle
   * pixels of each line, excluding 3 at each end.
   * This allows the FILTER macro to be
   * called so that it processes all the pixels normally.  A constant value of
   * true for is_not_edge lets the compiler ignore the if statement. */
  FILTER (start, end, 1);
}

ALWAYS_INLINE G_GNUC_UNUSED static void
filter_line_c_planar_mode0 (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w)
{
  filter_line_c_planar (dst, tzero, bzero, mone, mp, ttwo, btwo, tptwo, bptwo,
      ttone, ttp, bbone, bbp, w, 0);
}

ALWAYS_INLINE G_GNUC_UNUSED static void
filter_line_c_planar_mode2 (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w)
{
  filter_line_c_planar (dst, tzero, bzero, mone, mp, ttwo, btwo, tptwo, bptwo,
      ttone, ttp, bbone, bbp, w, 2);
}

ALWAYS_INLINE G_GNUC_UNUSED static void
filter_line_c_planar_mode0_16bits (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w)
{
  filter_line_c_planar_16bits (dst, tzero, bzero, mone, mp, ttwo, btwo, tptwo,
      bptwo, ttone, ttp, bbone, bbp, w, 0);
}

ALWAYS_INLINE G_GNUC_UNUSED static void
filter_line_c_planar_mode2_16bits (void *ORC_RESTRICT dst,
    const void *ORC_RESTRICT tzero, const void *ORC_RESTRICT bzero,
    const void *ORC_RESTRICT mone, const void *ORC_RESTRICT mp,
    const void *ORC_RESTRICT ttwo, const void *ORC_RESTRICT btwo,
    const void *ORC_RESTRICT tptwo, const void *ORC_RESTRICT bptwo,
    const void *ORC_RESTRICT ttone, const void *ORC_RESTRICT ttp,
    const void *ORC_RESTRICT bbone, const void *ORC_RESTRICT bbp, int w)
{
  filter_line_c_planar_16bits (dst, tzero, bzero, mone, mp, ttwo, btwo, tptwo,
      bptwo, ttone, ttp, bbone, bbp, w, 2);
}

ALWAYS_INLINE static void
filter_edges (guint8 * sdst, const guint8 * stzero, const guint8 * sbzero,
    const guint8 * smone, const guint8 * smp, const guint8 * sttwo,
    const guint8 * sbtwo, const guint8 * stptwo, const guint8 * sbptwo,
    const guint8 * sttone, const guint8 * sttp, const guint8 * sbbone,
    const guint8 * sbbp, int w, int colors, int y_alternates_every,
    int mode, const int bpp)
{
  int x;
  const int edge = colors * (MAX_ALIGN / bpp);
  const int border = 3 * colors;

  /* Only edge pixels need to be processed here.  A constant value of false
   * for is_not_edge should let the compiler ignore the whole branch. */
  FILTER (0, border, 0);
  FILTER (w - edge, w - border, 1);
  FILTER (w - border, w, 0);
}

ALWAYS_INLINE static void
filter_edges_16bits (guint8 * sdst_, const guint8 * stzero_,
    const guint8 * sbzero_,
    const guint8 * smone_, const guint8 * smp_, const guint8 * sttwo_,
    const guint8 * sbtwo_, const guint8 * stptwo_, const guint8 * sbptwo_,
    const guint8 * sttone_, const guint8 * sttp_, const guint8 * sbbone_,
    const guint8 * sbbp_, int w, int colors, int y_alternates_every,
    int mode, const int bpp)
{
  int x;
  const int edge = colors * (MAX_ALIGN / bpp);
  const int border = 3 * colors;
  guint16 *sdst = (guint16 *) sdst_;
  guint16 *stzero = (guint16 *) stzero_;
  guint16 *sbzero = (guint16 *) sbzero_;
  guint16 *smone = (guint16 *) smone_;
  guint16 *smp = (guint16 *) smp_;
  guint16 *sttwo = (guint16 *) sttwo_;
  guint16 *sbtwo = (guint16 *) sbtwo_;
  guint16 *stptwo = (guint16 *) stptwo_;
  guint16 *sbptwo = (guint16 *) sbptwo_;
  guint16 *sttone = (guint16 *) sttone_;
  guint16 *sttp = (guint16 *) sttp_;
  guint16 *sbbone = (guint16 *) sbbone_;
  guint16 *sbbp = (guint16 *) sbbp_;

  /* Only edge pixels need to be processed here.  A constant value of false
   * for is_not_edge should let the compiler ignore the whole branch. */
  FILTER (0, border, 0);
  FILTER (w - edge, w - border, 1);
  FILTER (w - border, w, 0);
}

static void
filter_scanline_yadif_semiplanar (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 2, 0);
}

static void
filter_scanline_yadif_packed_3 (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 3, 0);
}

static void
filter_scanline_yadif_packed_4 (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 4, 0);
}

static void
filter_scanline_yadif_packed_yvyu (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 4, 1);
}

static void
filter_scanline_yadif_packed_uyvy (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 4, 2);
}

ALWAYS_INLINE static void
filter_scanline_yadif (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size,
    int colors, int y_alternates_every)
{
  guint8 *dst = out;
  const int bpp = 1;            // Hard code 8-bit atm
  int w = size / bpp;
  int edge = colors * MAX_ALIGN / bpp;
  GstDeinterlaceScanlineData s = *s_orig;

  int mode = (s.tt1 == NULL || s.bb1 == NULL || s.ttp == NULL
      || s.bbp == NULL) ? 2 : 0;

  /* When starting up, some data might not yet be available, so use the current frame */
  if (s.m1 == NULL)
    s.m1 = s.mp;
  if (s.tt1 == NULL)
    s.tt1 = s.ttp;
  if (s.bb1 == NULL)
    s.bb1 = s.bbp;
  if (s.t2 == NULL)
    s.t2 = s.tp2;
  if (s.b2 == NULL)
    s.b2 = s.bp2;

  filter_edges (dst, s.t0, s.b0, s.m1, s.mp, s.t2, s.b2, s.tp2, s.bp2, s.tt1,
      s.ttp, s.bb1, s.bbp, w, colors, y_alternates_every, mode, bpp);
  filter_line_c (dst, s.t0, s.b0, s.m1, s.mp, s.t2, s.b2, s.tp2, s.bp2, s.tt1,
      s.ttp, s.bb1, s.bbp, w, colors, y_alternates_every, colors * 3, w - edge,
      mode);
}

ALWAYS_INLINE static void
filter_scanline_yadif_planar (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  guint8 *dst = out;
  const int bpp = 1;            // Hard code 8-bit atm
  int w = size / bpp;
  int edge = MAX_ALIGN / bpp;
  GstDeinterlaceScanlineData s = *s_orig;

  int mode = (s.tt1 == NULL || s.bb1 == NULL || s.ttp == NULL
      || s.bbp == NULL) ? 2 : 0;

  /* When starting up, some data might not yet be available, so use the current frame */
  if (s.m1 == NULL)
    s.m1 = s.mp;
  if (s.tt1 == NULL)
    s.tt1 = s.ttp;
  if (s.bb1 == NULL)
    s.bb1 = s.bbp;
  if (s.t2 == NULL)
    s.t2 = s.tp2;
  if (s.b2 == NULL)
    s.b2 = s.bp2;

  filter_edges (dst, s.t0, s.b0, s.m1, s.mp, s.t2, s.b2, s.tp2, s.bp2, s.tt1,
      s.ttp, s.bb1, s.bbp, w, 1, 0, mode, bpp);
  if (mode == 0)
    filter_mode0 (dst, (void *) s.t0, (void *) s.b0, (void *) s.m1,
        (void *) s.mp, (void *) s.t2, (void *) s.b2, (void *) s.tp2,
        (void *) s.bp2, (void *) s.tt1, (void *) s.ttp, (void *) s.bb1,
        (void *) s.bbp, w - edge);
  else
    filter_mode2 (dst, (void *) s.t0, (void *) s.b0, (void *) s.m1,
        (void *) s.mp, (void *) s.t2, (void *) s.b2, (void *) s.tp2,
        (void *) s.bp2, (void *) s.tt1, (void *) s.ttp, (void *) s.bb1,
        (void *) s.bbp, w - edge);
}

ALWAYS_INLINE static void
filter_scanline_yadif_planar_16bits (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  guint8 *dst = out;
  const int bpp = 2;
  int w = size / bpp;
  int edge = MAX_ALIGN / bpp;
  GstDeinterlaceScanlineData s = *s_orig;

  int mode = (s.tt1 == NULL || s.bb1 == NULL || s.ttp == NULL
      || s.bbp == NULL) ? 2 : 0;

  /* When starting up, some data might not yet be available, so use the current frame */
  if (s.m1 == NULL)
    s.m1 = s.mp;
  if (s.tt1 == NULL)
    s.tt1 = s.ttp;
  if (s.bb1 == NULL)
    s.bb1 = s.bbp;
  if (s.t2 == NULL)
    s.t2 = s.tp2;
  if (s.b2 == NULL)
    s.b2 = s.bp2;

  filter_edges_16bits (dst, s.t0, s.b0, s.m1, s.mp, s.t2, s.b2, s.tp2, s.bp2,
      s.tt1, s.ttp, s.bb1, s.bbp, w, 1, 0, mode, bpp);
  if (mode == 0)
    filter_mode0_16bits (dst, (void *) s.t0, (void *) s.b0, (void *) s.m1,
        (void *) s.mp, (void *) s.t2, (void *) s.b2, (void *) s.tp2,
        (void *) s.bp2, (void *) s.tt1, (void *) s.ttp, (void *) s.bb1,
        (void *) s.bbp, w - edge);
  else
    filter_mode2_16bits (dst, (void *) s.t0, (void *) s.b0, (void *) s.m1,
        (void *) s.mp, (void *) s.t2, (void *) s.b2, (void *) s.tp2,
        (void *) s.bp2, (void *) s.tt1, (void *) s.ttp, (void *) s.bb1,
        (void *) s.bbp, w - edge);
}

static void
gst_deinterlace_method_yadif_init (GstDeinterlaceMethodYadif * self)
{
  /* TODO: add asm support for high bitdepth */
#if (defined __x86_64__ || defined _M_X64) && defined HAVE_NASM
  if (
#  if defined HAVE_ORC
      orc_sse_get_cpu_flags () & ORC_TARGET_SSE_SSSE3
#  elif defined __SSSE3__
      TRUE
#  else
      FALSE
#  endif
      ) {
    GST_DEBUG ("SSSE3 optimization enabled");
    filter_mode0 = gst_yadif_filter_line_mode0_ssse3;
    filter_mode2 = gst_yadif_filter_line_mode2_ssse3;
    filter_mode0_16bits = filter_line_c_planar_mode0_16bits;
    filter_mode2_16bits = filter_line_c_planar_mode2_16bits;
  } else {
    GST_DEBUG ("SSE2 optimization enabled");
    filter_mode0 = gst_yadif_filter_line_mode0_sse2;
    filter_mode2 = gst_yadif_filter_line_mode2_sse2;
    filter_mode0_16bits = filter_line_c_planar_mode0_16bits;
    filter_mode2_16bits = filter_line_c_planar_mode2_16bits;
  }
#else
  {
    GST_DEBUG ("SSE optimization disabled");
    filter_mode0 = filter_line_c_planar_mode0;
    filter_mode2 = filter_line_c_planar_mode2;
    filter_mode0_16bits = filter_line_c_planar_mode0_16bits;
    filter_mode2_16bits = filter_line_c_planar_mode2_16bits;
  }
#endif
}
