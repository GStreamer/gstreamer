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

#define DEBUG_ENABLED
#include <stdlib.h>
#include <math.h>

#include "config.h"
#include "gstvideoscale.h"
#undef HAVE_CPU_I386
#ifdef HAVE_CPU_I386
#include "videoscale_x86.h"
#endif

static void	gst_videoscale_scale_yuv		(GstVideoscale *scale, unsigned char *src, unsigned char *dest);
static void	gst_videoscale_scale_rgb		(GstVideoscale *scale, unsigned char *src, unsigned char *dest);

/* scalers */
static void	gst_videoscale_scale_nearest		(GstVideoscale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);
static void	gst_videoscale_scale_plane_slow		(GstVideoscale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);
static void	gst_videoscale_scale_point_sample	(GstVideoscale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);

/* filters */
static unsigned char gst_videoscale_bilinear		(unsigned char *src, double x, double y, int sw, int sh);
static unsigned char gst_videoscale_bicubic		(unsigned char *src, double x, double y, int sw, int sh);

void
gst_videoscale_setup (GstVideoscale *scale)
{
  switch (scale->format) {
    case GST_MAKE_FOURCC('I','4','2','0'):
      scale->scale_cc = gst_videoscale_scale_yuv;
      scale->scale_bytes = 1;
      break;
    case GST_MAKE_FOURCC('R','G','B',' '):
      scale->scale_cc = gst_videoscale_scale_rgb;
      /* XXX */
      /*scale->scale_bytes = gst_caps_get_int(scale->srcpad->caps,"bpp")/8; */
      break;
    default:
      g_print("videoscale: unsupported video format %08x\n", scale->format);
      return; /* XXX */
  }

  switch (scale->method) {
    case GST_VIDEOSCALE_POINT_SAMPLE:
      scale->scaler = gst_videoscale_scale_point_sample;
      GST_DEBUG (0,"videoscale: scaling method POINT_SAMPLE\n");
      break;
    case GST_VIDEOSCALE_NEAREST:
#ifdef HAVE_CPU_I386
      gst_videoscale_generate_rowbytes_x86 (scale->copy_row, scale->width,
		      scale->targetwidth, scale->scale_bytes);
      scale->scaler = gst_videoscale_scale_nearest_x86;
#else
      scale->scaler = gst_videoscale_scale_nearest;
#endif
      GST_DEBUG (0,"videoscale: scaling method NEAREST\n");
      break;
    case GST_VIDEOSCALE_BILINEAR:
      scale->scaler = gst_videoscale_scale_plane_slow;
      scale->filter = gst_videoscale_bilinear;
      GST_DEBUG (0,"videoscale: scaling method BILINEAR\n");
      break;
    case GST_VIDEOSCALE_BICUBIC:
      scale->scaler = gst_videoscale_scale_plane_slow;
      scale->filter = gst_videoscale_bicubic;
      GST_DEBUG (0,"videoscale: scaling method BICUBIC\n");
      break;
    default:
      g_print("videoscale: unsupported scaling method %d\n", scale->method);
      return; /* XXX */
  }

  return; /* XXX */
}

static void
gst_videoscale_scale_rgb (GstVideoscale *scale, unsigned char *src, unsigned char *dest)
{
  int sw = scale->width;
  int sh = scale->height;
  int dw = scale->targetwidth;
  int dh = scale->targetheight;
  GST_DEBUG (0,"videoscale: scaling RGB %dx%d to %dx%d\n", sw, sh, dw, dh);

  switch (scale->scale_bytes) {
    case 2:
     dw = ((dw + 1) & ~1) << 1;
     sw = sw<<1;
     break;
    case 4:
     dw = ((dw + 2) & ~3) << 2;
     sw = sw<<2;
     break;
   default:
     break;
  }

  GST_DEBUG (0,"videoscale: %p %p\n", src, dest);
  scale->scaler(scale, src, dest, sw, sh, dw, dh);
}

static void
gst_videoscale_scale_yuv (GstVideoscale *scale, unsigned char *src, unsigned char *dest)
{
  int sw = scale->width;
  int sh = scale->height;
  int dw = scale->targetwidth;
  int dh = scale->targetheight;

  GST_DEBUG (0,"videoscale: scaling YUV420P %dx%d to %dx%d\n", sw, sh, dw, dh);

  scale->scaler(scale, src, dest, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  dh = dh>>1;
  dw = dw>>1;
  sh = sh>>1;
  sw = sw>>1;

  scale->scaler(scale, src, dest, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  scale->scaler(scale, src, dest, sw, sh, dw, dh);
}

#define RC(x,y) *(src+(int)(x)+(int)((y)*sw))

static unsigned char
gst_videoscale_bilinear (unsigned char *src, double x, double y, int sw, int sh)
{
  int j=floor(x);
  int k=floor(y);
  double a=x-j;
  double b=y-k;
  double dest;
  int color;

  GST_DEBUG(0,"videoscale: scaling bilinear %f %f %dx%d\n", x, y, sw, sh);

  dest=(1-a)*(1-b)*RC(j,k)+
       a*(1-b)*RC(j+1,k);

  k = MIN(sh-1, k);
  dest+= b*(1-a)*RC(j,k+1)+
       a*b*RC(j+1,k+1);

  color=rint(dest);
  if (color<0) color=abs(color);  /* cannot have negative values ! */
  /*if (color<0) color=0;  // cannot have negative values ! */
  if (color>255) color=255;

  return (unsigned char) color;
}

static unsigned char
gst_videoscale_bicubic (unsigned char *src, double x, double y, int sw, int sh)
{
  int j=floor(x);
  int k=floor(y), k2;
  double a=x-j;
  double b=y-k;
  double dest;
  int color;
  double t1, t2, t3, t4;
  double a1, a2, a3, a4;

  GST_DEBUG (0,"videoscale: scaling bicubic %dx%d\n", sw, sh);

  a1 = -a*(1-a)*(1-a);
  a2 = (1-2*a*a+a*a*a);
  a3 = a*(1+a-a*a);
  a4 = a*a*(1-a);

  k2 = MAX(0, k-1);
  t1=a1*RC(j-1,k2)+	a2*RC(j,k2)+	a3*RC(j+1,k2)-	a4*RC(j+2,k2);
  t2=a1*RC(j-1,k)+	a2*RC(j,k)+	a3*RC(j+1,k)-	a4*RC(j+2,k);
  k2 = MIN(sh, k+1);
  t3=a1*RC(j-1,k2)+	a2*RC(j,k2)+	a3*RC(j+1,k2)-	a4*RC(j+2,k2);
  k2 = MIN(sh, k+2);
  t4=a1*RC(j-1,k2)+	a2*RC(j,k2)+	a3*RC(j+1,k2)-	a4*RC(j+2,k2);

  dest= -b*(1-b)*(1-b)*t1+ (1-2*b*b+b*b*b)*t2+ b*(1+b-b*b)*t3+ b*b*(b-1)*t4;

  color=rint(dest);
  if (color<0) color=abs(color);  /* cannot have negative values ! */
  if (color>255) color=255;

  return (unsigned char) color;
}

static void
gst_videoscale_scale_plane_slow (GstVideoscale *scale, unsigned char *src, unsigned char *dest,
		                 int sw, int sh, int dw, int dh)
{
  double zoomx = ((double)dw)/(double)sw;
  double zoomy = ((double)dh)/(double)sh;
  double xr, yr;
  int x, y;

  GST_DEBUG (0,"videoscale: scale plane slow %dx%d %dx%d %g %g %p %p\n", sw, sh, dw, dh, zoomx, zoomy, src, dest);

  for (y=0; y<dh; y++) {
    yr = ((double)y)/zoomy;
    for (x=0; x<dw; x++) {
      xr = ((double)x)/zoomx;

      GST_DEBUG (0,"videoscale: scale plane slow %g %g %p\n", xr, yr, (src+(int)(x)+(int)((y)*sw)));

      if (floor(xr) == xr && floor(yr) == yr){
        GST_DEBUG (0,"videoscale: scale plane %g %g %p %p\n", xr, yr, (src+(int)(x)+(int)((y)*sw)), dest);
	*dest++ = RC(xr, yr);
      }
      else {
	*dest++ = scale->filter(src, xr, yr, sw, sh);
	/**dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh); */
      }
    }
  }
}

static void
gst_videoscale_scale_point_sample (GstVideoscale *scale, unsigned char *src, unsigned char *dest,
		                   int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  int sum, xcount, ycount, loop;
  unsigned char *srcp, *srcp2;

  GST_DEBUG (0,"videoscale: scaling nearest point sample %p %p %d\n", src, dest, dw);

  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    ycount = 1;
    srcp = src;
    while (ypos >0x10000) {
      ycount++;
      ypos-=0x10000;
      src += sw;
    }

    xpos = 0x10000;
    for ( x=dw; x; x-- ) {
      xcount = 0;
      sum=0;
      while ( xpos >= 0x10000L ) {
	loop = ycount;
        srcp2 = srcp;
        while (loop--) {
          sum += *srcp2;
          srcp2 += sw;
	}
	srcp++;
        xcount++;
        xpos -= 0x10000L;
      }
      *dest++ = sum/(xcount*ycount);
      xpos += xinc;
    }

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest (GstVideoscale *scale,
		              unsigned char *src,
			      unsigned char *dest,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;

  GST_DEBUG (0, "videoscale: scaling nearest %p %p %d %d\n", src, dest, dw, scale->scale_bytes);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw;
    }

    xpos = 0x10000;

    switch (scale->scale_bytes) {
      case 4:
      {
        guint32 *destp = (guint32 *)dest;
        guint32 *srcp = (guint32 *)src;

        for ( x=dw>>2; x; x-- ) {
          while ( xpos >= 0x10000L ) {
	    srcp++;
            xpos -= 0x10000L;
          }
	  *destp++ = *srcp;
          xpos += xinc;
        }
	break;
      }
      case 2:
      {
        guint16 *destp = (guint16 *)dest;
        guint16 *srcp = (guint16 *)src;

        for ( x=dw>>1; x; x-- ) {
          while ( xpos >= 0x10000L ) {
	    srcp++;
            xpos -= 0x10000L;
          }
	  *destp++ = *srcp;
          xpos += xinc;
        }
	break;
      }
      case 1:
      {
        guchar *destp = dest;
        guchar *srcp = src;

        for ( x=dw; x; x-- ) {
          while ( xpos >= 0x10000L ) {
	    srcp++;
            xpos -= 0x10000L;
          }
	  *destp++ = *srcp;
          xpos += xinc;
        }
      }
    }
    dest += dw;

    ypos += yinc;
  }
}

