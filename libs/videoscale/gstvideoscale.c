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
#include <stdlib.h>
#include <math.h>

#include "config.h"
#include "gstvideoscale.h"
#ifdef HAVE_CPU_I386
#include "gstscale_x86.h"
#endif

static void	gst_videoscale_scale_yuv		(GstVideoScale *scale, unsigned char *src, unsigned char *dest);
static void	gst_videoscale_scale_rgb		(GstVideoScale *scale, unsigned char *src, unsigned char *dest);

/* scalers */
static void	gst_videoscale_scale_nearest		(GstVideoScale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);
static void	gst_videoscale_scale_plane_slow		(GstVideoScale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);
static void	gst_videoscale_scale_point_sample	(GstVideoScale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);

/* filters */
static unsigned char gst_videoscale_bilinear		(unsigned char *src, double x, double y, int sw, int sh);
static unsigned char gst_videoscale_bicubic		(unsigned char *src, double x, double y, int sw, int sh);

GstVideoScale*
gst_videoscale_new (gint sw, gint sh, gint dw, gint dh,
		    GstColorSpaceType format, GstVideoScaleMethod method)
{
  GstVideoScale *new = g_malloc(sizeof(GstVideoScale));

  new->source_width = sw;
  new->source_height = sh;
  new->dest_width = dw;
  new->dest_height = dh;
  new->format = format;
  new->method = method;


  switch (format) {
    case GST_COLORSPACE_YUV420P:
      new->scale = gst_videoscale_scale_yuv;
      new->scale_bytes = 1;
      break;
    case GST_COLORSPACE_RGB555:
    case GST_COLORSPACE_RGB565:
    case GST_COLORSPACE_BGR555:
    case GST_COLORSPACE_BGR565:
      new->scale = gst_videoscale_scale_rgb;
      new->scale_bytes = 2;
      break;
    case GST_COLORSPACE_RGB32:
    case GST_COLORSPACE_BGR32:
      new->scale = gst_videoscale_scale_rgb;
      new->scale_bytes = 4;
      break;
    default:
      g_print("videoscale: unsupported video format %d\n", format);
      g_free(new);
      return NULL;
  }

  switch (method) {
    case GST_VIDEOSCALE_POINT_SAMPLE:
      new->scaler = gst_videoscale_scale_point_sample;
      GST_DEBUG (0,"videoscale: scaling method POINT_SAMPLE\n");
      break;
    case GST_VIDEOSCALE_NEAREST:
#ifdef HAVE_CPU_I386
      gst_videoscale_generate_rowbytes_x86 (new->copy_row, sw, dw, new->scale_bytes);
      new->scaler = gst_videoscale_scale_nearest_x86;
#else
      new->scaler = gst_videoscale_scale_nearest;
#endif
      GST_DEBUG (0,"videoscale: scaling method NEAREST\n");
      break;
    case GST_VIDEOSCALE_BILINEAR:
      new->scaler = gst_videoscale_scale_plane_slow;
      new->filter = gst_videoscale_bilinear;
      GST_DEBUG (0,"videoscale: scaling method BILINEAR\n");
      break;
    case GST_VIDEOSCALE_BICUBIC:
      new->scaler = gst_videoscale_scale_plane_slow;
      new->filter = gst_videoscale_bicubic;
      GST_DEBUG (0,"videoscale: scaling method BICUBIC\n");
      break;
    default:
      g_print("videoscale: unsupported scaling method %d\n", method);
      g_free(new);
      return NULL;
  }

  return new;
}

void
gst_videoscale_destroy (GstVideoScale *scale)
{
  g_free(scale);
}

static void
gst_videoscale_scale_rgb (GstVideoScale *scale, unsigned char *src, unsigned char *dest)
{
  int sw = scale->source_width;
  int sh = scale->source_height;
  int dw = scale->dest_width;
  int dh = scale->dest_height;
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
gst_videoscale_scale_yuv (GstVideoScale *scale, unsigned char *src, unsigned char *dest)
{
  int sw = scale->source_width;
  int sh = scale->source_height;
  int dw = scale->dest_width;
  int dh = scale->dest_height;

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

  printf("videoscale: scaling bilinear %f %f %dx%d\n", x, y, sw, sh);

  dest=(1-a)*(1-b)*RC(j,k)+
       a*(1-b)*RC(j+1,k);

  k = MIN(sh-1, k);
  dest+= b*(1-a)*RC(j,k+1)+
       a*b*RC(j+1,k+1);

  color=rint(dest);
  if (color<0) color=abs(color);  // cannot have negative values !
  //if (color<0) color=0;  // cannot have negative values !
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
  if (color<0) color=abs(color);  // cannot have negative values !
  if (color>255) color=255;

  return (unsigned char) color;
}

static void
gst_videoscale_scale_plane_slow (GstVideoScale *scale, unsigned char *src, unsigned char *dest,
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
	//*dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh);
      }
    }
  }
}

static void
gst_videoscale_scale_point_sample (GstVideoScale *scale, unsigned char *src, unsigned char *dest,
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
gst_videoscale_scale_nearest (GstVideoScale *scale,
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
