/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"

GST_DEBUG_CATEGORY_EXTERN (dvdspu_debug);
#define GST_CAT_DEFAULT dvdspu_debug

void
gstspu_clear_comp_buffers (SpuState * state)
{
  /* The area to clear is the line inside the disp_rect, each entry 4 bytes,
   * of the sub-sampled UV planes. */
  gint16 left = state->comp_left / 2;
  gint16 right = state->comp_right / 2;
  gint16 uv_width = sizeof (guint32) * (right - left + 1);

  memset (state->comp_bufs[0] + left, 0, uv_width);
  memset (state->comp_bufs[1] + left, 0, uv_width);
  memset (state->comp_bufs[2] + left, 0, uv_width);
}

void
gstspu_blend_comp_buffers (SpuState * state, guint8 * planes[3])
{
  gint16 uv_end;
  gint16 left, x;
  guint8 *out_U;
  guint8 *out_V;
  guint32 *in_U;
  guint32 *in_V;
  guint32 *in_A;
  gint16 comp_last_x = state->comp_right;

  if (comp_last_x < state->comp_left)
    return;                     /* Didn't draw in the comp buffers, nothing to do... */

#if 0
  GST_LOG ("Blending comp buffers from x=%d to x=%d",
      state->comp_left, state->comp_right);
#endif

  /* Set up the output pointers */
  out_U = planes[1];            /* U plane */
  out_V = planes[2];            /* V plane */

  /* Input starts at the first pixel of the compositing buffer */
  in_U = state->comp_bufs[0];   /* U comp buffer */
  in_V = state->comp_bufs[1];   /* V comp buffer */
  in_A = state->comp_bufs[2];   /* A comp buffer */

  /* Calculate how many pixels to blend based on the maximum X value that was 
   * drawn in the render_line function, divided by 2 (rounding up) to account 
   * for UV sub-sampling */
  uv_end = (comp_last_x + 1) / 2;
  left = state->comp_left / 2;

  out_U += left * GST_VIDEO_INFO_COMP_PSTRIDE (&state->info, 1);
  out_V += left * GST_VIDEO_INFO_COMP_PSTRIDE (&state->info, 2);
  for (x = left; x < uv_end; x++) {
    guint32 tmp;
    /* Each entry in the compositing buffer is 4 summed pixels, so the
     * inverse alpha is (4 * 0xff) - in_A[x] */
    guint16 inv_A = (4 * 0xff) - in_A[x];

    tmp = in_U[x] + inv_A * *out_U;
    *out_U = (guint8) (tmp / (4 * 0xff));

    tmp = in_V[x] + inv_A * *out_V;
    *out_V = (guint8) (tmp / (4 * 0xff));

    out_U += GST_VIDEO_INFO_COMP_PSTRIDE (&state->info, 1);
    out_V += GST_VIDEO_INFO_COMP_PSTRIDE (&state->info, 2);
  }
}
