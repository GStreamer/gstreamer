/* Gnome-Streamer
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

//#define DEBUG_ENABLED
#include <gst/gst.h>

#include <gstvideoscale.h>
#include <gst/meta/videoraw.h>

static void gst_videoscale_scale_plane(unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);

GstBuffer *gst_videoscale_scale(GstBuffer *src, int sw, int sh, int dw, int dh, int format) {
  GstBuffer *outbuf;
  char *source;
  char *dest;
  GstMeta *meta;
  
  DEBUG("videoscale: scaling %dx%d to %dx%d\n", sw, sh, dw, dh);

  source = GST_BUFFER_DATA(src);

  outbuf = gst_buffer_new();
  dest = GST_BUFFER_DATA(outbuf) = g_malloc((dw*dh*12)/8);
  GST_BUFFER_SIZE(outbuf) = (dw*dh*12)/8;

  meta = gst_buffer_get_first_meta(src);
  if (meta) {
    ((MetaVideoRaw *)meta)->width = dw;
    ((MetaVideoRaw *)meta)->height = dh;

    gst_buffer_add_meta(outbuf, meta);
  }

  gst_videoscale_scale_plane(source, dest, sw, sh, dw, dh);

  source += sw*sh;
  dest += dw*dh;

  dh = dh>>1;
  dw = dw>>1;
  sh = sh>>1;
  sw = sw>>1;

  gst_videoscale_scale_plane(source, dest, sw, sh, dw, dh);

  source += sw*sh;
  dest += dw*dh;

  gst_videoscale_scale_plane(source, dest, sw, sh, dw, dh);

  gst_buffer_unref(src);

  return outbuf;
}

static char gst_videoscale_interp_simple(unsigned char *src, int x, int y, int dw, int dh, int sw, int sh, int ix, int iy) {
  unsigned char *isourcep;
  int interp;
  int i,j;

  if (x>0) src--;
  if (x>dw-1) src--;
  if (y>0) src-=sw;
  if (y>dh-1) src-=sw;

  isourcep = src;
  interp =0;

  for (i =0; i<iy; i++) {
    for (j =0; j<ix; j++) {
      interp += *isourcep++;
    }
    isourcep = isourcep-ix+sw;
  }
  return interp/(ix*iy);
}

static char gst_videoscale_interp_other(unsigned char *src, int x, int y, int dw, int dh, int sw, int sh, int ix, int iy) {
  unsigned char *isourcep;
  int interp;
  int i,j;
  static int matrix[3][3] = {{1,2,1}, {2,3,2},{1,2,1}};

  if (x>0) src--;
  if (x>dw-1) src--;
  if (y>0) src-=sw;
  if (y>dh-1) src-=sw;

  isourcep = src;
  interp =0;

  for (i =0; i<iy; i++) {
    for (j =0; j<ix; j++) {
      //printf("%d %d %d %d\n", i, j, *isourcep, matrix[i][j]);
      interp += matrix[i][j]*(*isourcep++);
    }
    isourcep = isourcep-ix+sw;
  }
  //printf("%d\n", interp/15);
  return interp/15;
}

static void gst_videoscale_scale_plane(unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh) {

  int yinc = sh / dh;
  int srcys = sh % dh;
  int destys = dh;
  int dy = 2 * (srcys - destys);
  int incyE = 2 * srcys;
  int incyNE = 2 * (srcys - destys);
  int x, y;

  int xinc = sw / dw;
  int srcxs = sw % dw;
  int destxs = dw;
  int incxE = 2 * srcxs;
  int incxNE = 2 * (srcxs - destxs);
  int dx;
  unsigned char *sourcep;
  int srcinc = 0;
  int xskip, yskip =0;

  for (y = 0; y<dh; y++) {

    dx = 2 * (srcxs - destxs);
    sourcep = src + (srcinc*sw);

    for (x = dw; x; x--) {
      if (dx <= 0) {
	dx += incxE;
	xskip = 0;
      }
      else {
        dx += incxNE;
	xskip = 1;
	sourcep++;
      }
      sourcep += xinc;

      *dest++ = gst_videoscale_interp_other(sourcep, x, y, dw, dh, sw, sh, 3, 3);
    }
    if (dy <= 0) {
      dy += incyE;
      yskip = 0;
    }
    else {
      dy += incyNE;
      srcinc++;
      yskip = 1;
    }
    srcinc += yinc;
  }
}
