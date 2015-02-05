/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *                    2015 Jan Schmidt <jan@centricular.com>
 *
 * gstclock-linreg.c: Linear regression implementation, used in clock slaving
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

#include "gst_private.h"
#include <time.h>

#include "gstclock.h"
#include "gstinfo.h"
#include "gstutils.h"
#include "glib-compat-private.h"

/* Compute log2 of the passed 64-bit number by finding the highest set bit */
static guint
gst_log2 (GstClockTime in)
{
  const guint64 b[] =
      { 0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000, 0xFFFFFFFF00000000LL };
  const guint64 S[] = { 1, 2, 4, 8, 16, 32 };
  int i;

  guint count = 0;
  for (i = 5; i >= 0; i--) {
    if (in & b[i]) {
      in >>= S[i];
      count |= S[i];
    }
  }

  return count;
}

/* http://mathworld.wolfram.com/LeastSquaresFitting.html
 * with SLAVE_LOCK
 */
gboolean
_priv_gst_do_linear_regression (GstClockTime * times, guint n,
    GstClockTime * m_num, GstClockTime * m_denom, GstClockTime * b,
    GstClockTime * xbase, gdouble * r_squared)
{
  GstClockTime *newx, *newy;
  GstClockTime xmin, ymin, xbar, ybar, xbar4, ybar4;
  GstClockTime xmax, ymax;
  GstClockTimeDiff sxx, sxy, syy;
  GstClockTime *x, *y;
  gint i, j;
  gint pshift = 0;
  gint max_bits;

  xbar = ybar = sxx = syy = sxy = 0;

  x = times;
  y = times + 2;

  xmin = ymin = G_MAXUINT64;
  xmax = ymax = 0;
  for (i = j = 0; i < n; i++, j += 4) {
    xmin = MIN (xmin, x[j]);
    ymin = MIN (ymin, y[j]);

    xmax = MAX (xmax, x[j]);
    ymax = MAX (ymax, y[j]);
  }

  newx = times + 1;
  newy = times + 3;

  /* strip off unnecessary bits of precision */
  for (i = j = 0; i < n; i++, j += 4) {
    newx[j] = x[j] - xmin;
    newy[j] = y[j] - ymin;
  }

#ifdef DEBUGGING_ENABLED
  GST_CAT_DEBUG (GST_CAT_CLOCK, "reduced numbers:");
  for (i = j = 0; i < n; i++, j += 4)
    GST_CAT_DEBUG (GST_CAT_CLOCK,
        "  %" G_GUINT64_FORMAT "  %" G_GUINT64_FORMAT, newx[j], newy[j]);
#endif

  /* have to do this precisely otherwise the results are pretty much useless.
   * should guarantee that none of these accumulators can overflow */

  /* quantities on the order of 1e10 to 1e13 -> 30-35 bits;
   * window size a max of 2^10, so
   this addition could end up around 2^45 or so -- ample headroom */
  for (i = j = 0; i < n; i++, j += 4) {
    /* Just in case assumptions about headroom prove false, let's check */
    if ((newx[j] > 0 && G_MAXUINT64 - xbar <= newx[j]) ||
        (newy[j] > 0 && G_MAXUINT64 - ybar <= newy[j])) {
      GST_CAT_WARNING (GST_CAT_CLOCK,
          "Regression overflowed in clock slaving! xbar %"
          G_GUINT64_FORMAT " newx[j] %" G_GUINT64_FORMAT " ybar %"
          G_GUINT64_FORMAT " newy[j] %" G_GUINT64_FORMAT, xbar, newx[j], ybar,
          newy[j]);
      return FALSE;
    }

    xbar += newx[j];
    ybar += newy[j];
  }
  xbar /= n;
  ybar /= n;

  /* multiplying directly would give quantities on the order of 1e20-1e26 ->
   * 60 bits to 70 bits times the window size that's 80 which is too much.
   * Instead we (1) subtract off the xbar*ybar in the loop instead of after,
   * to avoid accumulation; (2) shift off some estimated number of bits from
   * each multiplicand to limit the expected ceiling. For strange
   * distributions of input values, things can still overflow, in which
   * case we drop precision and retry - at most a few times, in practice rarely
   */

  /* Guess how many bits we might need for the usual distribution of input,
   * with a fallback loop that drops precision if things go pear-shaped */
  max_bits = gst_log2 (MAX (xmax - xmin, ymax - ymin)) * 7 / 8 + gst_log2 (n);
  if (max_bits > 64)
    pshift = max_bits - 64;

  i = 0;
  do {
#ifdef DEBUGGING_ENABLED
    GST_CAT_DEBUG (GST_CAT_CLOCK,
        "Restarting regression with precision shift %u", pshift);
#endif

    xbar4 = xbar >> pshift;
    ybar4 = ybar >> pshift;
    sxx = syy = sxy = 0;
    for (i = j = 0; i < n; i++, j += 4) {
      GstClockTime newx4, newy4;
      GstClockTimeDiff tmp;

      newx4 = newx[j] >> pshift;
      newy4 = newy[j] >> pshift;

      tmp = (newx4 + xbar4) * (newx4 - xbar4);
      if (G_UNLIKELY (tmp > 0 && sxx > 0 && (G_MAXINT64 - sxx <= tmp))) {
        do {
          /* Drop some precision and restart */
          pshift++;
          sxx /= 4;
          tmp /= 4;
        } while (G_MAXINT64 - sxx <= tmp);
        break;
      } else if (G_UNLIKELY (tmp < 0 && sxx < 0 && (G_MAXINT64 - sxx >= tmp))) {
        do {
          /* Drop some precision and restart */
          pshift++;
          sxx /= 4;
          tmp /= 4;
        } while (G_MININT64 - sxx >= tmp);
        break;
      }
      sxx += tmp;

      tmp = newy4 * newy4 - ybar4 * ybar4;
      if (G_UNLIKELY (tmp > 0 && syy > 0 && (G_MAXINT64 - syy <= tmp))) {
        do {
          pshift++;
          syy /= 4;
          tmp /= 4;
        } while (G_MAXINT64 - syy <= tmp);
        break;
      } else if (G_UNLIKELY (tmp < 0 && syy < 0 && (G_MAXINT64 - syy >= tmp))) {
        do {
          pshift++;
          syy /= 4;
          tmp /= 4;
        } while (G_MININT64 - syy >= tmp);
        break;
      }
      syy += tmp;

      tmp = newx4 * newy4 - xbar4 * ybar4;
      if (G_UNLIKELY (tmp > 0 && sxy > 0 && (G_MAXINT64 - sxy <= tmp))) {
        do {
          pshift++;
          sxy /= 4;
          tmp /= 4;
        } while (G_MAXINT64 - sxy <= tmp);
        break;
      } else if (G_UNLIKELY (tmp < 0 && sxy < 0 && (G_MININT64 - sxy >= tmp))) {
        do {
          pshift++;
          sxy /= 4;
          tmp /= 4;
        } while (G_MININT64 - sxy >= tmp);
        break;
      }
      sxy += tmp;
    }
  } while (i < n);

  if (G_UNLIKELY (sxx == 0))
    goto invalid;

  *m_num = sxy;
  *m_denom = sxx;
  *b = (ymin + ybar) - gst_util_uint64_scale (xbar, *m_num, *m_denom);
  /* Report base starting from the most recent observation */
  *xbase = xmax;
  *b += gst_util_uint64_scale (xmax - xmin, *m_num, *m_denom);

  *r_squared = ((double) sxy * (double) sxy) / ((double) sxx * (double) syy);

#ifdef DEBUGGING_ENABLED
  GST_CAT_DEBUG (GST_CAT_CLOCK, "  m      = %g", ((double) *m_num) / *m_denom);
  GST_CAT_DEBUG (GST_CAT_CLOCK, "  b      = %" G_GUINT64_FORMAT, *b);
  GST_CAT_DEBUG (GST_CAT_CLOCK, "  xbase  = %" G_GUINT64_FORMAT, *xbase);
  GST_CAT_DEBUG (GST_CAT_CLOCK, "  r2     = %g", *r_squared);
#endif

  return TRUE;

invalid:
  {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "sxx == 0, regression failed");
    return FALSE;
  }
}
