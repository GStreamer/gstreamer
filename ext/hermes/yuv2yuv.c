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

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>

/*#undef HAVE_LIBMMX */

#ifdef HAVE_LIBMMX
#include "mmx.h"
#endif

void gst_colorspace_yuy2_to_i420(unsigned char *src, unsigned char *dest, guint width, guint height) 
{
  int size, i, j;
  guchar *desty, *destr, *destb;

  size = width * height;

  desty = dest;
  destr = desty + size;
  destb = destr + (size>>2);

  for (i=0; i<height; i++) {
    for (j=0; j<(width>>1); j++) {
      *desty++ = *src;
      *desty++ = *(src+2);
      if ((i&1) == 0) {
        *destr++ = *(src+1);
        *destb++ = *(src+3);
      }
      src += 4;
    }
  }
}

void gst_colorspace_i420_to_yv12(unsigned char *src, unsigned char *dest, guint width, guint height) 
{
  int size, i;
  guint8 *destcr, *destcb;
  
  size = width * height;

  memcpy (dest, src, size);

  src += size;
  destcr = dest + size;
  size >>=2;
  destcb = destcr + size;

  i=size;
  while (i--) 
    *destcb++ = *src++;
  i=size;
  while (i--) 
    *destcr++ = *src++;
}

