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

static void gst_videoscale_scale_yuv(GstVideoScale *scale, unsigned char *src, unsigned char *dest); 

/* scalers */
static void generate_rowbytes(unsigned char *copy_row, int src_w, int dst_w, int bpp);
static void gst_videoscale_scale_nearest(GstVideoScale *scale, unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh); 
static void gst_videoscale_scale_plane_slow(GstVideoScale *scale, unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh); 

/* filters */
static unsigned char gst_videoscale_bilinear(unsigned char *src, double x, double y, int sw, int sh);
static unsigned char gst_videoscale_bicubic(unsigned char *src, double x, double y, int sw, int sh);

GstVideoScale *gst_videoscale_new(int sw, int sh, int dw, int dh, int format, GstVideoScaleMethod method) 
{
  GstVideoScale *new = g_malloc(sizeof(GstVideoScale));

  new->source_width = sw;
  new->source_height = sh;
  new->dest_width = dw;
  new->dest_height = dh;
  new->format = format;
  new->method = method;


  new->scale = gst_videoscale_scale_yuv;

  switch (method) {
    case GST_VIDEOSCALE_NEAREST:
      generate_rowbytes(new->copy_row, sw, dw, 1);
      new->scaler = gst_videoscale_scale_nearest;
      DEBUG("videoscale: scaling method NEAREST\n");
      break;
    case GST_VIDEOSCALE_BILINEAR:
      new->scaler = gst_videoscale_scale_plane_slow;
      new->filter = gst_videoscale_bilinear;
      break;
    case GST_VIDEOSCALE_BICUBIC:
      new->scaler = gst_videoscale_scale_plane_slow;
      new->filter = gst_videoscale_bicubic;
      break;
    default:
      g_print("videoscale: unsupported scaling method %d\n", method);
      break;
  }

  return new;
}

void gst_videoscale_destroy(GstVideoScale *scale) 
{
  g_free(scale);
}

static void gst_videoscale_scale_yuv(GstVideoScale *scale, unsigned char *src, unsigned char *dest) 
{
  int sw = scale->source_width;
  int sh = scale->source_height;
  int dw = scale->dest_width;
  int dh = scale->dest_height;
  
  DEBUG("videoscale: scaling YUV420 %dx%d to %dx%d\n", sw, sh, dw, dh);

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

static unsigned char gst_videoscale_bilinear(unsigned char *src, double x, double y, int sw, int sh) {
  int j=floor(x);
  int k=floor(y);
  double a=x-j;
  double b=y-k;
  double dest;
  int color;

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

static void gst_videoscale_scale_plane_slow(GstVideoScale *scale, unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh)
{
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
	*dest++ = scale->filter(src, xr, yr, sw, sh);
	//*dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh);
      }
    }
  }
  
}

#define PREFIX16        0x66
#define STORE_BYTE      0xAA
#define STORE_WORD      0xAB
#define LOAD_BYTE       0xAC
#define LOAD_WORD       0xAD
#define RETURN          0xC3

static void generate_rowbytes(unsigned char *copy_row, int src_w, int dst_w, int bpp)
{
  int i;
  int pos, inc;
  unsigned char *eip;
  unsigned char load, store;

  DEBUG("videoscale: setup scaling %p\n", copy_row);

  switch (bpp) {
    case 1:
      load = LOAD_BYTE;
      store = STORE_BYTE;
      break;
    case 2:
    case 4:
      load = LOAD_WORD;
      store = STORE_WORD;
      break;
    default:
      return;
  }
  pos = 0x10000;
  inc = (src_w << 16) / dst_w;
  eip = copy_row;
  for ( i=0; i<dst_w; ++i ) {
    while ( pos >= 0x10000L ) {
      if ( bpp == 2 ) {
        *eip++ = PREFIX16;
      }
      *eip++ = load;
      pos -= 0x10000L;
    }
    if ( bpp == 2 ) {
      *eip++ = PREFIX16;
    }
    *eip++ = store;
    pos += inc;
  }
  *eip++ = RETURN;
}


static void gst_videoscale_scale_nearest(GstVideoScale *scale, unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh) 
{
  int pos, inc, y;
  int u1, u2;
  
  scale->temp = scale->copy_row;
  
  DEBUG("videoscale: scaling nearest %p\n", scale->copy_row);

  pos = 0x10000;
  inc = (sh<<16)/dh;

  for (y = dh; y; y--) {

    while (pos >0x10000) {
      src += sw;
      pos-=0x10000;
    }
    __asm__ __volatile__ ("
	   movl %2, %%eax\n
           call *%%eax
           "
           : "=&D" (u1), "=&S" (u2)
           : "g" (scale->temp), "0" (dest), "1" (src)
           : "memory" );

    dest+= dw;

    pos += inc;
  }
  DEBUG("videoscale: scaling nearest done %p\n", scale->copy_row);
}
