/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "paint.h"

void
gst_smpte_paint_rect (guint8 *dest, gint stride, gint x, gint y, gint w, gint h, guint8 color)
{ 
  guint8 *d = dest + stride * y + x;
  gint i;
	      
  for (i = 0; i < h; i++) {
    memset (d, color, w);
    d += stride; 
  }
}

void
gst_smpte_paint_rect_s (guint8 *dest, gint stride, gint x, gint y, gint w, gint h, guint8 color)
{ 
  guint8 *d = dest + stride * y + x;
  gint i, j;
  gint border = 100;

  for (i = 0; i < h; i++) {
    if (w - border > 0) {
      memset (d, color, w - border);
    }

    for (j = 0; j < border - w; j++) {
      *(d+w+j) = (color*(border-j)/border);
    } 
    d += stride; 
  }
}
