/*
 *
 *  rgb2rgb.c, Software RGB to YUV convertor
 *  Written by Nick Kurshev.
 *  palette & yuv & runtime cpu stuff by Michael (michaelni@gmx.at) (under GPL)
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>

#include <gst/gst.h>

#define Y_FROM_RGB(r,g,b) 	((9798 * (r) + 19235 * (g) + 3736 * (b)) >> 15)
#define U_FROM_BY(b,y) 		((16122 * ((b) - (y))) >> 15) + 128;
#define V_FROM_RY(r,y) 		((25203 * ((r) - (y))) >> 15) + 128;

static void
gst_colorspace_rgb32_to_yuv (unsigned char *src, 
		             unsigned char *ydst,
		             unsigned char *udst,
		             unsigned char *vdst,
			     guint width, guint height)
{
  int y;
  const int chrom_width = width >> 1;
  int Y;
  int b, g, r;

  for (y = height; y; y -= 2) {
    int i;

    for (i = chrom_width; i; i--) {
      b = *src++;
      g = *src++;
      r = *src++;
      src++;

      Y = Y_FROM_RGB (r,g,b);

      *ydst++ = Y;
      *udst++ = U_FROM_BY (b,Y);
      *vdst++ = V_FROM_RY (r,Y);

      b = *src++;
      g = *src++;
      r = *src++; src++;

      *ydst++ = Y_FROM_RGB (r,g,b);
    }

    for (i = width; i; i--) {
      b = *src++;
      g = *src++;
      r = *src++; src++;
      *ydst++ = Y_FROM_RGB (r,g,b);
    }
  }
}

void
gst_colorspace_rgb32_to_i420 (unsigned char *src, unsigned char *dest, guint width, guint height)
{
  unsigned char *ydst = dest;
  unsigned char *udst = ydst + (width * height);
  unsigned char *vdst = udst + ((width * height) >> 2);

  gst_colorspace_rgb32_to_yuv (src, ydst, udst, vdst, width, height);
}

void
gst_colorspace_rgb32_to_yv12 (unsigned char *src, unsigned char *dest, guint width, guint height)
{
  unsigned char *ydst = dest;
  unsigned char *vdst = ydst + (width * height);
  unsigned char *udst = vdst + ((width * height) >> 2);

  gst_colorspace_rgb32_to_yuv (src, ydst, udst, vdst, width, height);
}
