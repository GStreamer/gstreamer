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

#define DEBUG_ENABLED
#include <gst/gst.h>
#include <stdlib.h>
#include <math.h>
#include <videoscale.h>
#include <string.h>

#include "config.h"
#include "gstvideoscale.h"
#undef HAVE_CPU_I386
#ifdef HAVE_CPU_I386
#include "videoscale_x86.h"
#endif

/* scalers */
static void	gst_videoscale_scale_nearest		(GstVideoscale *scale, unsigned char *dest, unsigned char *src,
							 int sw, int sh, int dw, int dh);
#if 0
static void	gst_videoscale_scale_plane_slow		(GstVideoscale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);
static void	gst_videoscale_scale_point_sample	(GstVideoscale *scale, unsigned char *src, unsigned char *dest,
							 int sw, int sh, int dw, int dh);

/* filters */
static unsigned char gst_videoscale_bilinear		(unsigned char *src, double x, double y, int sw, int sh);
static unsigned char gst_videoscale_bicubic		(unsigned char *src, double x, double y, int sw, int sh);
#endif

static void gst_videoscale_planar411 (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_planar400 (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_packed422 (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_packed422rev (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_32bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_24bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src);
static void gst_videoscale_16bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src);

static void gst_videoscale_scale_nearest_str2 (GstVideoscale *scale,
	unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_str4 (GstVideoscale *scale,
	unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_32bit (GstVideoscale *scale,
	unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_24bit (GstVideoscale *scale,
	unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_16bit (GstVideoscale *scale,
	unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);

struct videoscale_format_struct videoscale_formats[] = {
	/* packed */
	{ "YUY2", 16, gst_videoscale_packed422, },
	{ "UYVY", 16, gst_videoscale_packed422rev, },
	{ "Y422", 16, gst_videoscale_packed422rev, },
	{ "UYNV", 16, gst_videoscale_packed422rev, },
	{ "YVYU", 16, gst_videoscale_packed422, },
	/* planar */
	{ "YV12", 12, gst_videoscale_planar411, },
	{ "I420", 12, gst_videoscale_planar411, },
	{ "IYUV", 12, gst_videoscale_planar411, },
	{ "Y800", 8,  gst_videoscale_planar400, },
	{ "Y8  ", 8,  gst_videoscale_planar400, },
	/* RGB */
	{ "RGB ", 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x00ff0000, 0x0000ff00, 0x000000ff },
	{ "RGB ", 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x000000ff, 0x0000ff00, 0x00ff0000 },
	{ "RGB ", 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0xff000000, 0x00ff0000, 0x0000ff00 },
	{ "RGB ", 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x0000ff00, 0x00ff0000, 0xff000000 },
	{ "RGB ", 24, gst_videoscale_24bit, 24, G_BIG_ENDIAN, 0xff0000, 0x00ff00, 0x0000ff },
	{ "RGB ", 24, gst_videoscale_24bit, 24, G_BIG_ENDIAN, 0x0000ff, 0x00ff00, 0xff0000 },
	{ "RGB ", 16, gst_videoscale_16bit, 16, G_BYTE_ORDER, 0xf800, 0x07e0, 0x001f },
	{ "RGB ", 16, gst_videoscale_16bit, 15, G_BYTE_ORDER, 0x7c00, 0x03e0, 0x001f },
};

int videoscale_n_formats = sizeof(videoscale_formats)/sizeof(videoscale_formats[0]);

GstCaps *
videoscale_get_caps(struct videoscale_format_struct *format)
{
  unsigned int fourcc;
  GstCaps *caps;

  if(format->scale==NULL)
    return NULL;

  fourcc = GST_MAKE_FOURCC(format->fourcc[0],format->fourcc[1],format->fourcc[2],format->fourcc[3]);

  if(format->bpp){
    caps = GST_CAPS_NEW ("videoscale", "video/raw",
		"format", GST_PROPS_FOURCC (fourcc),
		"depth", GST_PROPS_INT(format->bpp),
		"bpp", GST_PROPS_INT(format->depth),
		"endianness", GST_PROPS_INT(format->endianness),
		"red_mask", GST_PROPS_INT(format->red_mask),
		"green_mask", GST_PROPS_INT(format->green_mask),
		"blue_mask", GST_PROPS_INT(format->blue_mask));
  }else{
    caps = GST_CAPS_NEW ("videoscale", "video/raw",
		"format", GST_PROPS_FOURCC (fourcc));
  }

  return caps;
}

struct videoscale_format_struct *
videoscale_find_by_caps(GstCaps *caps)
{
  int i;

  GST_DEBUG ("finding %p",caps);

  g_return_val_if_fail(caps != NULL, NULL);

  for (i = 0; i < videoscale_n_formats; i++){
    GstCaps *c;

    c = videoscale_get_caps(videoscale_formats + i);
    if(c){
      if(gst_caps_is_always_compatible(caps, c)){
        gst_caps_unref(c);
        return videoscale_formats + i;
      }
      gst_caps_unref(c);
    }
  }

  return NULL;
}

void
gst_videoscale_setup (GstVideoscale *videoscale)
{
  GST_DEBUG ("format=%p \"%s\" from %dx%d to %dx%d",
		videoscale->format, videoscale->format->fourcc,
		videoscale->from_width, videoscale->from_height,
		videoscale->to_width, videoscale->to_height);

  if(videoscale->to_width==0 || videoscale->to_height==0 ||
  	videoscale->from_width==0 || videoscale->from_height==0){
    return;
  }

  if(videoscale->to_width == videoscale->from_width &&
	videoscale->to_height == videoscale->from_height){
    GST_DEBUG ("videoscale: using passthru");
    videoscale->passthru = TRUE;
    videoscale->inited = TRUE;
    return;
  }

  GST_DEBUG ("videoscale: scaling method POINT_SAMPLE");

  videoscale->from_buf_size = (videoscale->from_width * videoscale->from_height
		  * videoscale->format->depth) / 8;
  videoscale->to_buf_size = (videoscale->to_width * videoscale->to_height
		  * videoscale->format->depth) / 8;

  videoscale->inited = TRUE;
}

#if 0
static void
gst_videoscale_scale_rgb (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;
  GST_DEBUG ("videoscale: scaling RGB %dx%d to %dx%d", sw, sh, dw, dh);

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

  GST_DEBUG ("videoscale: %p %p", src, dest);
  //scale->scaler(scale, src, dest, sw, sh, dw, dh);
}
#endif

static void
gst_videoscale_planar411 (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling planar 4:1:1 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest(scale, dest, src, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  dh = dh>>1;
  dw = dw>>1;
  sh = sh>>1;
  sw = sw>>1;

  gst_videoscale_scale_nearest(scale, dest, src, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  gst_videoscale_scale_nearest(scale, dest, src, sw, sh, dw, dh);
}

static void
gst_videoscale_planar400 (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling Y-only %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest(scale, dest, src, sw, sh, dw, dh);
}

static void
gst_videoscale_packed422 (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling 4:2:2 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_str2(scale, dest, src, sw, sh, dw, dh);
  gst_videoscale_scale_nearest_str4(scale, dest+1, src+1, sw/2, sh, dw/2, dh);
  gst_videoscale_scale_nearest_str4(scale, dest+3, src+3, sw/2, sh, dw/2, dh);

}

static void
gst_videoscale_packed422rev (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling 4:2:2 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_str2(scale, dest+1, src, sw, sh, dw, dh);
  gst_videoscale_scale_nearest_str4(scale, dest, src+1, sw/2, sh, dw/2, dh);
  gst_videoscale_scale_nearest_str4(scale, dest+2, src+3, sw/2, sh, dw/2, dh);

}

static void
gst_videoscale_32bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling 32bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_32bit(scale, dest, src, sw, sh, dw, dh);

}

static void
gst_videoscale_24bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling 24bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_24bit(scale, dest, src, sw, sh, dw, dh);

}

static void
gst_videoscale_16bit (GstVideoscale *scale, unsigned char *dest, unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_DEBUG ("videoscale: scaling 16bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_16bit(scale, dest, src, sw, sh, dw, dh);

}

#if 0
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

  GST_DEBUG ("videoscale: scaling bilinear %f %f %dx%d", x, y, sw, sh);

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

  GST_DEBUG ("videoscale: scaling bicubic %dx%d", sw, sh);

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

  GST_DEBUG ("videoscale: scale plane slow %dx%d %dx%d %g %g %p %p", sw, sh, dw, dh, zoomx, zoomy, src, dest);

  for (y=0; y<dh; y++) {
    yr = ((double)y)/zoomy;
    for (x=0; x<dw; x++) {
      xr = ((double)x)/zoomx;

      GST_DEBUG ("videoscale: scale plane slow %g %g %p", xr, yr, (src+(int)(x)+(int)((y)*sw)));

      if (floor(xr) == xr && floor(yr) == yr){
        GST_DEBUG ("videoscale: scale plane %g %g %p %p", xr, yr, (src+(int)(x)+(int)((y)*sw)), dest);
	*dest++ = RC(xr, yr);
      }
      else {
	*dest++ = scale->filter(src, xr, yr, sw, sh);
	/**dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh); */
      }
    }
  }
}
#endif

#if 0
static void
gst_videoscale_scale_point_sample (GstVideoscale *scale, unsigned char *src, unsigned char *dest,
		                   int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  int sum, xcount, ycount, loop;
  unsigned char *srcp, *srcp2;

  GST_DEBUG ("videoscale: scaling nearest point sample %p %p %d", src, dest, dw);

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
#endif

static void
gst_videoscale_scale_nearest (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp++;
        xpos -= 0x10000L;
      }
      *destp++ = *srcp;
      xpos += xinc;
    }
    dest += dw;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_str2 (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw*2;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp+=2;
        xpos -= 0x10000L;
      }
      *destp = *srcp;
      destp+=2;
      xpos += xinc;
    }
    dest += dw*2;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_str4 (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw*4;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp+=4;
        xpos -= 0x10000L;
      }
      *destp = *srcp;
      destp+=4;
      xpos += xinc;
    }
    dest += dw*4;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_32bit (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw*4;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp+=4;
        xpos -= 0x10000L;
      }
      *(guint32 *)destp = *(guint32 *)srcp;
      destp+=4;
      xpos += xinc;
    }
    dest += dw*4;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_24bit (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw*3;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp+=3;
        xpos -= 0x10000L;
      }
      destp[0] = srcp[0];
      destp[1] = srcp[1];
      destp[2] = srcp[2];
      destp+=3;
      xpos += xinc;
    }
    dest += dw*3;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_16bit (GstVideoscale *scale,
			      unsigned char *dest,
		              unsigned char *src,
			      int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp = dest;
  guchar *srcp = src;

  GST_DEBUG ("videoscale: scaling nearest %p %p %d", src, dest, dw);


  ypos = 0x10000;
  yinc = (sh<<16)/dh;
  xinc = (sw<<16)/dw;

  for (y = dh; y; y--) {

    while (ypos >0x10000) {
      ypos-=0x10000;
      src += sw*2;
    }

    xpos = 0x10000;

    srcp = src;
    destp = dest;

    for ( x=dw; x; x-- ) {
      while ( xpos >= 0x10000L ) {
        srcp+=2;
        xpos -= 0x10000L;
      }
      destp[0] = srcp[0];
      destp[1] = srcp[1];
      destp+=2;
      xpos += xinc;
    }
    dest += dw*2;

    ypos += yinc;
  }
}

