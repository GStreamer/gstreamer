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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <stdlib.h>
#include <math.h>
#include <videoscale.h>
#include <string.h>

#include "gstvideoscale.h"
#undef HAVE_CPU_I386
#ifdef HAVE_CPU_I386
#include "videoscale_x86.h"
#endif

#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

/* scalers */
static void gst_videoscale_scale_nearest (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
#if 0
static void gst_videoscale_scale_plane_slow (GstVideoscale * scale,
    unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_point_sample (GstVideoscale * scale,
    unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);

/* filters */
static unsigned char gst_videoscale_bilinear (unsigned char *src, double x,
    double y, int sw, int sh);
static unsigned char gst_videoscale_bicubic (unsigned char *src, double x,
    double y, int sw, int sh);
#endif

static void gst_videoscale_planar411 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src);
static void gst_videoscale_planar400 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src);
static void gst_videoscale_packed422 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src);
static void gst_videoscale_packed422rev (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src);
static void gst_videoscale_32bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src);
static void gst_videoscale_24bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src);
static void gst_videoscale_16bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src);

static void gst_videoscale_scale_nearest_str2 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_str4 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_32bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_24bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);
static void gst_videoscale_scale_nearest_16bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh);

#define fourcc_YUY2 GST_MAKE_FOURCC('Y','U','Y','2')
#define fourcc_UYVY GST_MAKE_FOURCC('U','Y','V','Y')
#define fourcc_Y422 GST_MAKE_FOURCC('Y','4','2','2')
#define fourcc_UYNV GST_MAKE_FOURCC('U','Y','N','V')
#define fourcc_YVYU GST_MAKE_FOURCC('Y','V','Y','U')
#define fourcc_YV12 GST_MAKE_FOURCC('Y','V','1','2')
#define fourcc_I420 GST_MAKE_FOURCC('I','4','2','0')
#define fourcc_Y800 GST_MAKE_FOURCC('Y','8','0','0')
#define fourcc_RGB_ GST_MAKE_FOURCC('R','G','B',' ')

struct videoscale_format_struct videoscale_formats[] = {
  /* packed */
  {fourcc_YUY2, 16, gst_videoscale_packed422,},
  {fourcc_UYVY, 16, gst_videoscale_packed422rev,},
  {fourcc_Y422, 16, gst_videoscale_packed422rev,},
  {fourcc_UYNV, 16, gst_videoscale_packed422rev,},
  {fourcc_YVYU, 16, gst_videoscale_packed422,},
  /* planar */
  {fourcc_YV12, 12, gst_videoscale_planar411,},
  {fourcc_I420, 12, gst_videoscale_planar411,},
  {fourcc_Y800, 8, gst_videoscale_planar400,},
  /* RGB */
  {fourcc_RGB_, 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x00ff0000,
      0x0000ff00, 0x000000ff},
  {fourcc_RGB_, 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x000000ff,
      0x0000ff00, 0x00ff0000},
  {fourcc_RGB_, 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0xff000000,
      0x00ff0000, 0x0000ff00},
  {fourcc_RGB_, 32, gst_videoscale_32bit, 24, G_BIG_ENDIAN, 0x0000ff00,
      0x00ff0000, 0xff000000},
  {fourcc_RGB_, 24, gst_videoscale_24bit, 24, G_BIG_ENDIAN, 0xff0000, 0x00ff00,
      0x0000ff},
  {fourcc_RGB_, 24, gst_videoscale_24bit, 24, G_BIG_ENDIAN, 0x0000ff, 0x00ff00,
      0xff0000},
  {fourcc_RGB_, 16, gst_videoscale_16bit, 16, G_BYTE_ORDER, 0xf800, 0x07e0,
      0x001f},
  {fourcc_RGB_, 16, gst_videoscale_16bit, 15, G_BYTE_ORDER, 0x7c00, 0x03e0,
      0x001f},
};

int videoscale_n_formats =
    sizeof (videoscale_formats) / sizeof (videoscale_formats[0]);

GstStructure *
videoscale_get_structure (struct videoscale_format_struct *format)
{
  GstStructure *structure;

  if (format->scale == NULL)
    return NULL;

  if (format->depth) {
    structure = gst_structure_new ("video/x-raw-rgb",
        "depth", G_TYPE_INT, format->depth,
        "bpp", G_TYPE_INT, format->bpp,
        "endianness", G_TYPE_INT, format->endianness,
        "red_mask", G_TYPE_INT, format->red_mask,
        "green_mask", G_TYPE_INT, format->green_mask,
        "blue_mask", G_TYPE_INT, format->blue_mask, NULL);
  } else {
    structure = gst_structure_new ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, format->fourcc, NULL);
  }

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 16, G_MAXINT,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);

  return structure;
}

struct videoscale_format_struct *
videoscale_find_by_structure (GstStructure * structure)
{
  int i;
  gboolean ret;
  struct videoscale_format_struct *format;

  GST_DEBUG ("finding %s", gst_structure_to_string (structure));

  g_return_val_if_fail (structure != NULL, NULL);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    unsigned int fourcc;

    ret = gst_structure_get_fourcc (structure, "format", &fourcc);
    if (!ret)
      return NULL;
    for (i = 0; i < videoscale_n_formats; i++) {
      format = videoscale_formats + i;
      if (format->depth == 0 && format->fourcc == fourcc) {
        return format;
      }
    }
  } else {
    int bpp;
    int depth;
    int endianness;
    int red_mask;
    int green_mask;
    int blue_mask;

    ret = gst_structure_get_int (structure, "bpp", &bpp);
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "endianness", &endianness);
    ret &= gst_structure_get_int (structure, "red_mask", &red_mask);
    ret &= gst_structure_get_int (structure, "green_mask", &green_mask);
    ret &= gst_structure_get_int (structure, "blue_mask", &blue_mask);
    if (!ret)
      return NULL;
    for (i = 0; i < videoscale_n_formats; i++) {
      format = videoscale_formats + i;
      if (format->bpp == bpp && format->depth == depth &&
          format->endianness == endianness && format->red_mask == red_mask &&
          format->green_mask == green_mask && format->blue_mask == blue_mask) {
        return format;
      }
    }
  }

  return NULL;
}

void
gst_videoscale_setup (GstVideoscale * videoscale)
{
  g_return_if_fail (GST_IS_VIDEOSCALE (videoscale));
  g_return_if_fail (videoscale->format != NULL);
  gint from_stride, to_stride;

  GST_DEBUG_OBJECT (videoscale, "format=%p " GST_FOURCC_FORMAT
      " from %dx%d to %dx%d, %d bpp",
      videoscale->format,
      GST_FOURCC_ARGS (videoscale->format->fourcc),
      videoscale->from_width, videoscale->from_height,
      videoscale->to_width, videoscale->to_height, videoscale->format->bpp);

  if (videoscale->to_width == 0 || videoscale->to_height == 0 ||
      videoscale->from_width == 0 || videoscale->from_height == 0) {
    g_critical ("bad sizes %dx%d %dx%d",
        videoscale->from_width, videoscale->from_height,
        videoscale->to_width, videoscale->to_height);
    return;
  }

  if (videoscale->to_width == videoscale->from_width &&
      videoscale->to_height == videoscale->from_height) {
    GST_DEBUG_OBJECT (videoscale, "using passthru");
    videoscale->passthru = TRUE;
    videoscale->inited = TRUE;
    return;
  }

  GST_DEBUG_OBJECT (videoscale, "scaling method POINT_SAMPLE");

  /* FIXME: we should get from and to strides from caps.  For now we conform
   * to videotestsrc's idea of it, which is to round w * bytespp to nearest
   * multiple of 4 */
  from_stride = ROUND_UP_4 (videoscale->from_width *
      (ROUND_UP_8 (videoscale->format->bpp) / 8));
  to_stride = ROUND_UP_4 (videoscale->to_width *
      (ROUND_UP_8 (videoscale->format->bpp) / 8));
  GST_DEBUG_OBJECT (videoscale, "from_stride %d to_stride %d",
      from_stride, to_stride);
  videoscale->from_buf_size = from_stride * videoscale->from_height;
  videoscale->to_buf_size = to_stride * videoscale->to_height;

  videoscale->passthru = FALSE;
  videoscale->inited = TRUE;
}

#if 0
static void
gst_videoscale_scale_rgb (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling RGB %dx%d to %dx%d", sw, sh, dw, dh);

  switch (scale->scale_bytes) {
    case 2:
      dw = ((dw + 1) & ~1) << 1;
      sw = sw << 1;
      break;
    case 4:
      dw = ((dw + 2) & ~3) << 2;
      sw = sw << 2;
      break;
    default:
      break;
  }

  GST_LOG_OBJECT (scale, "%p %p", src, dest);
  //scale->scaler(scale, src, dest, sw, sh, dw, dh);
}
#endif

static void
gst_videoscale_planar411 (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling planar 4:1:1 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest (scale, dest, src, sw, sh, dw, dh);

  src += sw * sh;
  dest += dw * dh;

  dh = dh >> 1;
  dw = dw >> 1;
  sh = sh >> 1;
  sw = sw >> 1;

  gst_videoscale_scale_nearest (scale, dest, src, sw, sh, dw, dh);

  src += sw * sh;
  dest += dw * dh;

  gst_videoscale_scale_nearest (scale, dest, src, sw, sh, dw, dh);
}

static void
gst_videoscale_planar400 (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling Y-only %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest (scale, dest, src, sw, sh, dw, dh);
}

static void
gst_videoscale_packed422 (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling 4:2:2 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_str2 (scale, dest, src, sw, sh, dw, dh);
  gst_videoscale_scale_nearest_str4 (scale, dest + 1, src + 1, sw / 2, sh,
      dw / 2, dh);
  gst_videoscale_scale_nearest_str4 (scale, dest + 3, src + 3, sw / 2, sh,
      dw / 2, dh);

}

static void
gst_videoscale_packed422rev (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling 4:2:2 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_str2 (scale, dest + 1, src, sw, sh, dw, dh);
  gst_videoscale_scale_nearest_str4 (scale, dest, src + 1, sw / 2, sh, dw / 2,
      dh);
  gst_videoscale_scale_nearest_str4 (scale, dest + 2, src + 3, sw / 2, sh,
      dw / 2, dh);

}

static void
gst_videoscale_32bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling 32bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_32bit (scale, dest, src, sw, sh, dw, dh);

}

static void
gst_videoscale_24bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling 24bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_24bit (scale, dest, src, sw, sh, dw, dh);

}

static void
gst_videoscale_16bit (GstVideoscale * scale, unsigned char *dest,
    unsigned char *src)
{
  int sw = scale->from_width;
  int sh = scale->from_height;
  int dw = scale->to_width;
  int dh = scale->to_height;

  GST_LOG_OBJECT (scale, "scaling 16bit %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoscale_scale_nearest_16bit (scale, dest, src, sw, sh, dw, dh);

}

#if 0
#define RC(x,y) *(src+(int)(x)+(int)((y)*sw))

static unsigned char
gst_videoscale_bilinear (unsigned char *src, double x, double y, int sw, int sh)
{
  int j = floor (x);
  int k = floor (y);
  double a = x - j;
  double b = y - k;
  double dest;
  int color;

  GST_LOG_OBJECT (scale, "scaling bilinear %f %f %dx%d", x, y, sw, sh);

  dest = (1 - a) * (1 - b) * RC (j, k) + a * (1 - b) * RC (j + 1, k);

  k = MIN (sh - 1, k);
  dest += b * (1 - a) * RC (j, k + 1) + a * b * RC (j + 1, k + 1);

  color = rint (dest);
  if (color < 0)
    color = abs (color);        /* cannot have negative values ! */
  /*if (color<0) color=0;  // cannot have negative values ! */
  if (color > 255)
    color = 255;

  return (unsigned char) color;
}

static unsigned char
gst_videoscale_bicubic (unsigned char *src, double x, double y, int sw, int sh)
{
  int j = floor (x);
  int k = floor (y), k2;
  double a = x - j;
  double b = y - k;
  double dest;
  int color;
  double t1, t2, t3, t4;
  double a1, a2, a3, a4;

  GST_LOG_OBJECT (scale, "scaling bicubic %dx%d", sw, sh);

  a1 = -a * (1 - a) * (1 - a);
  a2 = (1 - 2 * a * a + a * a * a);
  a3 = a * (1 + a - a * a);
  a4 = a * a * (1 - a);

  k2 = MAX (0, k - 1);
  t1 = a1 * RC (j - 1, k2) + a2 * RC (j, k2) + a3 * RC (j + 1,
      k2) - a4 * RC (j + 2, k2);
  t2 = a1 * RC (j - 1, k) + a2 * RC (j, k) + a3 * RC (j + 1,
      k) - a4 * RC (j + 2, k);
  k2 = MIN (sh, k + 1);
  t3 = a1 * RC (j - 1, k2) + a2 * RC (j, k2) + a3 * RC (j + 1,
      k2) - a4 * RC (j + 2, k2);
  k2 = MIN (sh, k + 2);
  t4 = a1 * RC (j - 1, k2) + a2 * RC (j, k2) + a3 * RC (j + 1,
      k2) - a4 * RC (j + 2, k2);

  dest =
      -b * (1 - b) * (1 - b) * t1 + (1 - 2 * b * b + b * b * b) * t2 + b * (1 +
      b - b * b) * t3 + b * b * (b - 1) * t4;

  color = rint (dest);
  if (color < 0)
    color = abs (color);        /* cannot have negative values ! */
  if (color > 255)
    color = 255;

  return (unsigned char) color;
}

static void
gst_videoscale_scale_plane_slow (GstVideoscale * scale, unsigned char *src,
    unsigned char *dest, int sw, int sh, int dw, int dh)
{
  double zoomx = ((double) dw) / (double) sw;
  double zoomy = ((double) dh) / (double) sh;
  double xr, yr;
  int x, y;

  GST_LOG_OBJECT (scale, "scale plane slow %dx%d %dx%d %g %g %p %p", sw, sh,
      dw, dh, zoomx, zoomy, src, dest);

  for (y = 0; y < dh; y++) {
    yr = ((double) y) / zoomy;
    for (x = 0; x < dw; x++) {
      xr = ((double) x) / zoomx;

      GST_LOG_OBJECT (scale, "scale plane slow %g %g %p", xr, yr,
          (src + (int) (x) + (int) ((y) * sw)));

      if (floor (xr) == xr && floor (yr) == yr) {
        GST_LOG_OBJECT (scale, "scale plane %g %g %p %p", xr, yr,
            (src + (int) (x) + (int) ((y) * sw)), dest);
        *dest++ = RC (xr, yr);
      } else {
        *dest++ = scale->filter (src, xr, yr, sw, sh);
        /**dest++ = gst_videoscale_bicubic(src, xr, yr, sw, sh); */
      }
    }
  }
}
#endif

#if 0
static void
gst_videoscale_scale_point_sample (GstVideoscale * scale, unsigned char *src,
    unsigned char *dest, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  int sum, xcount, ycount, loop;
  unsigned char *srcp, *srcp2;

  GST_LOG_OBJECT (scale, "scaling nearest point sample %p %p %d", src, dest,
      dw);

  ypos = 0x10000;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {

    ycount = 1;
    srcp = src;
    while (ypos > 0x10000) {
      ycount++;
      ypos -= 0x10000;
      src += sw;
    }

    xpos = 0x10000;
    for (x = dw; x; x--) {
      xcount = 0;
      sum = 0;
      while (xpos >= 0x10000L) {
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
      *dest++ = sum / (xcount * ycount);
      xpos += xinc;
    }

    ypos += yinc;
  }
}
#endif

static void
gst_videoscale_scale_nearest (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;

  GST_LOG_OBJECT (scale, "scaling nearest %p %p %d", src, dest, dw);


  ypos = 0;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {
    if (ypos >= 0x10000) {
      src += (ypos >> 16) * sw;
      ypos &= 0xffff;
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {
        srcp += (xpos >> 16);
        xpos &= 0xffff;
      }
      *destp++ = *srcp;
      xpos += xinc;
    }
    dest += dw;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_str2 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;

  GST_LOG_OBJECT (scale, "scaling nearest %p %p %d", src, dest, dw);


  ypos = 0;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {

    if (ypos >= 0x10000) {
      src += (ypos >> 16) * sw * 2;
      ypos &= 0xffff;
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {
        srcp += (xpos >> 16) * 2;
        xpos &= 0xffff;
      }
      *destp = *srcp;
      destp += 2;
      xpos += xinc;
    }
    dest += dw * 2;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_str4 (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;

  GST_LOG_OBJECT (scale, "scaling nearest %p %p %d", src, dest, dw);


  ypos = 0;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {

    if (ypos >= 0x10000) {
      src += (ypos >> 16) * sw * 4;
      ypos &= 0xffff;
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {
        srcp += (xpos >> 16) * 4;
        xpos &= 0xffff;
      }
      *destp = *srcp;
      destp += 4;
      xpos += xinc;
    }
    dest += dw * 4;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_32bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;

  GST_LOG_OBJECT (scale, "scaling nearest %p %p %d", src, dest, dw);

  /* given how videoscale rounds off stride to nearest multiple of 4,
   * we don't have stride issues for 32 bit */
  ypos = 0;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {

    if (ypos >= 0x10000) {
      src += (ypos >> 16) * sw * 4;
      ypos &= 0xffff;
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {
        srcp += (xpos >> 16) * 4;
        xpos &= 0xffff;
      }
      *(guint32 *) destp = *(guint32 *) srcp;
      destp += 4;
      xpos += xinc;
    }
    dest += dw * 4;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_24bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;
  int sstride, dstride;         /* row strides in bytes */

  GST_LOG_OBJECT (scale, "scaling nearest %p %p %d", src, dest, dw);

  /* FIXME: strides should be gotten from caps; for now we do it Just Like
     videotestsrc, which means round off to next multiple of 4 bytes */
  sstride = ROUND_UP_4 (sw * 3);
  dstride = ROUND_UP_4 (dw * 3);

  ypos = 0;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {

    if (ypos >= 0x10000) {
      src += (ypos >> 16) * sstride;
      ypos &= 0xffff;
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {
        srcp += (xpos >> 16) * 3;
        xpos &= 0xffff;
      }
      destp[0] = srcp[0];
      destp[1] = srcp[1];
      destp[2] = srcp[2];
      destp += 3;
      xpos += xinc;
    }
    dest += dstride;

    ypos += yinc;
  }
}

static void
gst_videoscale_scale_nearest_16bit (GstVideoscale * scale,
    unsigned char *dest, unsigned char *src, int sw, int sh, int dw, int dh)
{
  int ypos, yinc, y;
  int xpos, xinc, x;
  guchar *destp;
  guchar *srcp;
  int sstride, dstride;         /* row strides in bytes */

  GST_LOG_OBJECT (scale, "scaling nearest from %p to %p, destination width %d",
      src, dest, dw);

  /* FIXME: strides should be gotten from caps; for now we do it Just Like
     videotestsrc, which means round off to next multiple of 4 bytes */
  sstride = sw * 2;
  if (sw % 2 == 1)
    sstride += 2;
  dstride = dw * 2;
  if (dw % 2 == 1)
    dstride += 2;

  ypos = 0;
  yinc = (sh << 16) / dh;       /* 16 bit fixed point arithmetic */
  xinc = (sw << 16) / dw;

  /* go over all destination lines */
  for (y = dh; y; y--) {        /* faster than 0 .. dh */

    if (ypos >= 0x10000) {      /* ypos >= 1 ? */
      src += (ypos >> 16) * sstride;    /* go down round(ypos) src lines */
      ypos &= 0xffff;           /* ypos %= 1 */
    }

    xpos = 0;

    srcp = src;
    destp = dest;

    /* go over all destination pixels for each line */
    for (x = dw; x; x--) {
      if (xpos >= 0x10000) {    /* xpos >= 1 ? */
        srcp += (xpos >> 16) * 2;       /* go right round(xpos) src pixels */
        xpos &= 0xffff;         /* xpos %= 1 */
      }
      destp[0] = srcp[0];
      destp[1] = srcp[1];
      destp += 2;               /* go right one destination pixel */
      xpos += xinc;
    }
    dest += dstride;            /* go down one destination line */

    ypos += yinc;
  }
}
