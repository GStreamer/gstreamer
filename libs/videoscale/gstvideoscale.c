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
#include <math.h>

#include <gst/gst.h>

#include <gstvideoscale.h>
#include <gst/meta/videoraw.h>

static void gst_videoscale_scale_plane(unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_plane_slow(unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);

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

  gst_videoscale_scale_plane_slow(source, dest, sw, sh, dw, dh);

  source += sw*sh;
  dest += dw*dh;

  dh = dh>>1;
  dw = dw>>1;
  sh = sh>>1;
  sw = sw>>1;

  gst_videoscale_scale_plane_slow(source, dest, sw, sh, dw, dh);

  source += sw*sh;
  dest += dw*dh;

  gst_videoscale_scale_plane_slow(source, dest, sw, sh, dw, dh);

  gst_buffer_unref(src);

  return outbuf;
}

#define RC(x,y) *(src+(int)(x)+(int)((y)*sw))

static unsigned char gst_videoscale_bilinear(unsigned char *src, double x, double y, int sw) {
  int j=floor(x);
  int k=floor(y);
  double a=x-j;
  double b=y-k;
  double dest;
  int color;

  dest=(1-a)*(1-b)*RC(j,k)+
       a*(1-b)*RC(j+1,k)+
       b*(1-a)*RC(j,k+1)+
       a*b*RC(j+1,k+1);

  color=rint(dest);
  if (color<0) color=abs(color);  // cannot have negative values !
  //if (color<0) color=0;  // cannot have negative values !
  if (color>255) color=255;

  return (unsigned char) color;
}

static unsigned char gst_videoscale_bicubic(unsigned char *src, double x, double y, int sw, int sh) {
  int j=floor(x);
  int k=floor(y), k2;
  double a=x-j;
  double b=y-k;
  double dest;
  int color;
  double t1, t2, t3, t4;
  double a1, a2, a3, a4;

  a1 = -a*(1-a)*(1-a);
  a2 = (1-2*a*a+a*a*a);
  a3 = a*(1+a-a*a);
  a4 = a*a*(1-a);

  k2 = MAX(0, k-1);
  t1=a1*RC(j-1,k2)+  	a2*RC(j,k2)+ 	a3*RC(j+1,k2)- 	a4*RC(j+2,k2);
  t2=a1*RC(j-1,k)+ 	a2*RC(j,k)+ 	a3*RC(j+1,k)- 	a4*RC(j+2,k); 
  k2 = MIN(sh, k+1);
  t3=a1*RC(j-1,k2)+ 	a2*RC(j,k2)+ 	a3*RC(j+1,k2)- 	a4*RC(j+2,k2);
  k2 = MIN(sh, k+2);
  t4=a1*RC(j-1,k2)+ 	a2*RC(j,k2)+ 	a3*RC(j+1,k2)- 	a4*RC(j+2,k2);

  dest= -b*(1-b)*(1-b)*t1+ (1-2*b*b+b*b*b)*t2+ b*(1+b-b*b)*t3+ b*b*(b-1)*t4;

  color=rint(dest);
  if (color<0) color=abs(color);  // cannot have negative values !
  if (color>255) color=255;

  return (unsigned char) color;
}

static void gst_videoscale_scale_plane_slow(unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh) {
  double zoomx = ((double)dw)/(double)sw;
  double zoomy = ((double)dh)/(double)sh;
  double xr, yr;
  int x, y;

  for (y=0; y<dh; y++) {
    yr = ((double)y)/zoomy;
    for (x=0; x<dw; x++) {
      xr = ((double)x)/zoomx;

      if (floor(xr) == xr && floor(yr) == yr){
	*dest++ = RC(xr, yr);
      }
      else {
	//*dest++ = gst_videoscale_bilinear(src, xr, yr, sw);
	*dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh);
      }
    }
  }
  
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

      *dest++ = *sourcep;
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
