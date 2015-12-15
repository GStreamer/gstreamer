/*
 * Copyright (C) 2013 David Schleef <ds@schleef.org>
 * Copyright (C) 2013 Rdio, Inc. <ingestions@rd.io>
 * Copyright (C) 2006-2010 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <gstyadif.h>
#include <string.h>

#undef NDEBUG
#include <assert.h>

#define FFABS(a) ABS(a)
#define FFMIN(a,b) MIN(a,b)
#define FFMAX(a,b) MAX(a,b)
#define FFMAX3(a,b,c) FFMAX(FFMAX(a,b),c)
#define FFMIN3(a,b,c) FFMIN(FFMIN(a,b),c)


#define PERM_RWP AV_PERM_WRITE | AV_PERM_PRESERVE | AV_PERM_REUSE

#define CHECK(j)\
    {   int score = FFABS(cur[mrefs-1+(j)] - cur[prefs-1-(j)])\
                  + FFABS(cur[mrefs  +(j)] - cur[prefs  -(j)])\
                  + FFABS(cur[mrefs+1+(j)] - cur[prefs+1-(j)]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[mrefs  +(j)] + cur[prefs  -(j)])>>1;\

#define FILTER \
    for (x = 0;  x < w; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0])>>1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) )>>1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
        int spatial_score = -1; \
 \
        if (mrefs > 0 && prefs > 0) { \
            spatial_score = FFABS(cur[mrefs - 1] - cur[prefs - 1]) + FFABS(c-e) \
                            + FFABS(cur[mrefs + 1] - cur[prefs + 1]) - 1; \
 \
            CHECK(-1) CHECK(-2) }} }} \
            CHECK( 1) CHECK( 2) }} }} \
        } \
        if (mode < 2) { \
            int b = (prev2[2 * mrefs] + next2[2 * mrefs])>>1; \
            int f = (prev2[2 * prefs] + next2[2 * prefs])>>1; \
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
        dst[0] = spatial_pred; \
 \
        dst++; \
        cur++; \
        prev++; \
        next++; \
        prev2++; \
        next2++; \
    }

static void
filter_line_c (guint8 * dst,
    guint8 * prev, guint8 * cur, guint8 * next,
    int w, int prefs, int mrefs, int parity, int mode)
{
  int x;
  guint8 *prev2 = parity ? prev : cur;
  guint8 *next2 = parity ? cur : next;

FILTER}

#if 0
static void
filter_line_c_16bit (guint16 * dst,
    guint16 * prev, guint16 * cur, guint16 * next,
    int w, int prefs, int mrefs, int parity, int mode)
{
  int x;
  guint16 *prev2 = parity ? prev : cur;
  guint16 *next2 = parity ? cur : next;
  mrefs /= 2;
  prefs /= 2;

FILTER}
#endif

void yadif_filter (GstYadif * yadif, int parity, int tff);
#ifdef HAVE_CPU_X86_64
void filter_line_x86_64 (guint8 * dst,
    guint8 * prev, guint8 * cur, guint8 * next,
    int w, int prefs, int mrefs, int parity, int mode);
#endif

void
yadif_filter (GstYadif * yadif, int parity, int tff)
{
  int y, i;
  const GstVideoInfo *vi = &yadif->video_info;
  const GstVideoFormatInfo *vfi = vi->finfo;

  for (i = 0; i < GST_VIDEO_FORMAT_INFO_N_COMPONENTS (vfi); i++) {
    int w = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (vfi, i, vi->width);
    int h = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (vfi, i, vi->height);
    int refs = GST_VIDEO_INFO_COMP_STRIDE (vi, i);
    int df = GST_VIDEO_INFO_COMP_PSTRIDE (vi, i);
    guint8 *prev_data = GST_VIDEO_FRAME_COMP_DATA (&yadif->prev_frame, i);
    guint8 *cur_data = GST_VIDEO_FRAME_COMP_DATA (&yadif->cur_frame, i);
    guint8 *next_data = GST_VIDEO_FRAME_COMP_DATA (&yadif->next_frame, i);
    guint8 *dest_data = GST_VIDEO_FRAME_COMP_DATA (&yadif->dest_frame, i);

    for (y = 0; y < h; y++) {
      if ((y ^ parity) & 1) {
        guint8 *prev = prev_data + y * refs;
        guint8 *cur = cur_data + y * refs;
        guint8 *next = next_data + y * refs;
        guint8 *dst = dest_data + y * refs;
        int mode = ((y == 1) || (y + 2 == h)) ? 2 : yadif->mode;
#if HAVE_CPU_X86_64
        if (0) {
          filter_line_c (dst, prev, cur, next, w,
              y + 1 < h ? refs : -refs, y ? -refs : refs, parity ^ tff, mode);
        } else {
          filter_line_x86_64 (dst, prev, cur, next, w,
              y + 1 < h ? refs : -refs, y ? -refs : refs, parity ^ tff, mode);
        }
#else
        filter_line_c (dst, prev, cur, next, w,
            y + 1 < h ? refs : -refs, y ? -refs : refs, parity ^ tff, mode);
#endif
      } else {
        guint8 *dst = dest_data + y * refs;
        guint8 *cur = cur_data + y * refs;

        memcpy (dst, cur, w * df);
      }
    }
  }

#if 0
  emms_c ();
#endif
}
