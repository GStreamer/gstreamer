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

#define RGB2YUV_SHIFT 8
#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 2 (if this is a
 * problem for anyone then tell me, and ill fix it)
 * chrominance data is only taken from every secound line others are ignored FIXME write HQ version
 */
void
gst_colorspace_rgb32_to_i420 (unsigned char *src, unsigned char *dest, guint width, guint height)
{
  int y;
  const int chrom_width = width >> 1;
  const int chrom_size = (width * height) >> 2;

  unsigned char *ydst = dest;
  unsigned char *udst = ydst + (width * height);
  unsigned char *vdst = udst + chrom_size;

  for (y = 0; y < height; y += 2) {
    int i;

    for (i = 0; i < chrom_width; i++) {
      unsigned int b = src[8 * i + 0];
      unsigned int g = src[8 * i + 1];
      unsigned int r = src[8 * i + 2];

      unsigned int Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      unsigned int V = ((RV * r + GV * g + BV * b) >> RGB2YUV_SHIFT) + 128;
      unsigned int U = ((RU * r + GU * g + BU * b) >> RGB2YUV_SHIFT) + 128;

      udst[i] = U;
      vdst[i] = V;
      ydst[2 * i] = Y;

      b = src[8 * i + 4];
      g = src[8 * i + 5];
      r = src[8 * i + 6];

      Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      ydst[2 * i + 1] = Y;
    }
    ydst += width;
    src += (width * 4);

    for (i = 0; i < chrom_width; i++) {
      unsigned int b = src[8 * i + 0];
      unsigned int g = src[8 * i + 1];
      unsigned int r = src[8 * i + 2];

      unsigned int Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;

      ydst[2 * i] = Y;

      b = src[8 * i + 4];
      g = src[8 * i + 5];
      r = src[8 * i + 6];

      Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      ydst[2 * i + 1] = Y;
    }
    udst += chrom_width;
    vdst += chrom_width;
    ydst += width;
    src += (width * 4);
  }
}

void
gst_colorspace_rgb32_to_yv12 (unsigned char *src, unsigned char *dest, guint width, guint height)
{
  int y;
  const int chrom_width = width >> 1;
  const int chrom_size = (width * height) >> 2;

  unsigned char *ydst = dest;
  unsigned char *vdst = ydst + (width * height);
  unsigned char *udst = vdst + chrom_size;

  for (y = 0; y < height; y += 2) {
    int i;

    for (i = 0; i < chrom_width; i++) {
      unsigned int b = src[8 * i + 0];
      unsigned int g = src[8 * i + 1];
      unsigned int r = src[8 * i + 2];

      unsigned int Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      unsigned int V = ((RV * r + GV * g + BV * b) >> RGB2YUV_SHIFT) + 128;
      unsigned int U = ((RU * r + GU * g + BU * b) >> RGB2YUV_SHIFT) + 128;

      udst[i] = U;
      vdst[i] = V;
      ydst[2 * i] = Y;

      b = src[8 * i + 4];
      g = src[8 * i + 5];
      r = src[8 * i + 6];

      Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      ydst[2 * i + 1] = Y;
    }
    ydst += width;
    src += (width * 4);

    for (i = 0; i < chrom_width; i++) {
      unsigned int b = src[8 * i + 0];
      unsigned int g = src[8 * i + 1];
      unsigned int r = src[8 * i + 2];

      unsigned int Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;

      ydst[2 * i] = Y;

      b = src[8 * i + 4];
      g = src[8 * i + 5];
      r = src[8 * i + 6];

      Y = ((RY * r + GY * g + BY * b) >> RGB2YUV_SHIFT) + 16;
      ydst[2 * i + 1] = Y;
    }
    udst += chrom_width;
    vdst += chrom_width;
    ydst += width;
    src += (width * 4);
  }
}
