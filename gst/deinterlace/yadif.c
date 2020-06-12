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
}

static void
gst_deinterlace_method_yadif_init (GstDeinterlaceMethodYadif * self)
{
}

#define FFABS(a) ABS(a)
#define FFMIN(a,b) MIN(a,b)
#define FFMAX(a,b) MAX(a,b)
#define FFMAX3(a,b,c) FFMAX(FFMAX(a,b),c)
#define FFMIN3(a,b,c) FFMIN(FFMIN(a,b),c)

#define CHECK(j)\
    {   int score = FFABS(s->t0[x - colors2 * (1 + (j))] - s->b0[x - colors2 * (1 - (j))])\
                  + FFABS(s->t0[x  + colors2 * (j)] - s->b0[x  -colors2 * (j)])\
                  + FFABS(s->t0[x + colors2 * (1 + (j))] - s->b0[x + colors2 * (1 - (j))]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (s->t0[x  +colors2 * ((j))] + s->b0[x - colors2 * (j)])>>1;\

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge) \
    for (x = start;  x < end; x++) { \
        int c = s->t0[x]; \
        int d = (s->m1[x] + s->mp[x])>>1; \
        int e = s->b0[x]; \
        int temporal_diff0 = FFABS(s->m1[x] - s->mp[x]); \
        int temporal_diff1 =(FFABS(s->t2[x] - c) + FFABS(s->b2[x] - e) )>>1; \
        int temporal_diff2 =(FFABS(s->tp2[x] - c) + FFABS(s->bp2[x] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
        int colors2 = colors; \
        if ((y_alternates_every == 1 && (x%2 == 0)) || \
           (y_alternates_every == 2 && (x%2 == 1))) \
          colors2 = 2; \
 \
        if (is_not_edge) {\
            int spatial_score = FFABS(s->t0[x-colors2] - s->b0[x-colors2]) + FFABS(c-e) \
                              + FFABS(s->t0[x+colors2] - s->b0[x+colors2]); \
            CHECK(-1) CHECK(-2) }} }} \
            CHECK( 1) CHECK( 2) }} }} \
        }\
 \
        if (!(mode&2)) { \
            int b = (s->tt1[x] + s->ttp[x])>>1; \
            int f = (s->bb1[x] + s->bbp[x])>>1; \
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
        dst[x] = spatial_pred; \
 \
    }

ALWAYS_INLINE static void
filter_line_c (guint8 * dst,
    const GstDeinterlaceScanlineData * s, int start, int end, int mode,
    int colors, int y_alternates_every)
{
  int x;
  /* The function is called for processing the middle
   * pixels of each line, excluding 3 at each end.
   * This allows the FILTER macro to be
   * called so that it processes all the pixels normally.  A constant value of
   * true for is_not_edge lets the compiler ignore the if statement. */
  FILTER (start, end, 1)
}

#define MAX_ALIGN 8

ALWAYS_INLINE static void
filter_edges (guint8 * dst,
    const GstDeinterlaceScanlineData * s, int w, int mode, const int bpp,
    const int colors, int y_alternates_every)
{
  int x;
  const int edge = colors * (MAX_ALIGN / bpp);
  const int border = 3 * colors;

  /* Only edge pixels need to be processed here.  A constant value of false
   * for is_not_edge should let the compiler ignore the whole branch. */
  FILTER (0, border, 0)
      FILTER (w - edge, w - border, 1)
      FILTER (w - border, w, 0)
}

static void
filter_scanline_yadif_planar (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * s_orig, guint size)
{
  filter_scanline_yadif (self, out, s_orig, size, 1, 0);
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

  filter_edges (dst, &s, w, mode, bpp, colors, y_alternates_every);
  filter_line_c (dst, &s, colors * 3, w - edge, mode, colors,
      y_alternates_every);
}
