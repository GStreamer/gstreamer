/* Gstreamer video blending utility functions
 *
 * Copied/pasted from gst/videoconvert/videoconvert.c
 *    Copyright (C) 2010 David Schleef <ds@schleef.org>
 *    Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Copyright (C) <2011> Intel Corporation
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-blend.h"
#include "videoblendorc.h"

#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("video-blending", 0,
        "video blending");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}

#else

#define ensure_debug_category() /* NOOP */

#endif /* GST_DISABLE_GST_DEBUG */

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define ARGB_A 3
# define ARGB_R 2
# define ARGB_G 1
# define ARGB_B 0
#else
# define ARGB_A 0
# define ARGB_R 1
# define ARGB_G 2
# define ARGB_B 3
#endif

/* Copy/pasted from 0.11 video.c */
static int
fill_planes (GstBlendVideoFormatInfo * info)
{
  gint width, height;

  width = info->width;
  height = info->height;

  switch (info->fmt) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      info->stride[0] = GST_ROUND_UP_4 (width * 2);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_r210:
      info->stride[0] = width * 4;
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      info->stride[0] = GST_ROUND_UP_4 (width * 2);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v308:
      info->stride[0] = GST_ROUND_UP_4 (width * 3);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_v210:
      info->stride[0] = ((width + 47) / 48) * 128;
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_v216:
      info->stride[0] = GST_ROUND_UP_8 (width * 4);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y800:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y16:
      info->stride[0] = GST_ROUND_UP_4 (width * 2);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_UYVP:
      info->stride[0] = GST_ROUND_UP_4 ((width * 2 * 5 + 3) / 4);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_IYU1:
      info->stride[0] = GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) +
          GST_ROUND_UP_4 (width) / 2);
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      info->stride[0] = width * 8;
      info->offset[0] = 0;
      break;
    case GST_VIDEO_FORMAT_I420:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * GST_ROUND_UP_2 (height);
      info->offset[2] = info->offset[1] +
          info->stride[1] * (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_YV12:        /* same as I420, but plane 1+2 swapped */
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[2] = info->stride[0] * GST_ROUND_UP_2 (height);
      info->offset[1] = info->offset[2] +
          info->stride[1] * (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_Y41B:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_16 (width) / 4;
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * height;
      info->offset[2] = info->offset[1] + info->stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*((ROUNDUP16(w)/4)*h */
      break;
    case GST_VIDEO_FORMAT_Y42B:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_8 (width) / 2;
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * height;
      info->offset[2] = info->offset[1] + info->stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*(ROUNDUP8(w)/2)*h */
      break;
    case GST_VIDEO_FORMAT_Y444:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = info->stride[0];
      info->stride[2] = info->stride[0];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * height;
      info->offset[2] = info->offset[1] * 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = info->stride[0];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * GST_ROUND_UP_2 (height);
      break;
    case GST_VIDEO_FORMAT_A420:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      info->stride[2] = info->stride[1];
      info->stride[3] = info->stride[0];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * GST_ROUND_UP_2 (height);
      info->offset[2] = info->offset[1] +
          info->stride[1] * (GST_ROUND_UP_2 (height) / 2);
      info->offset[3] = info->offset[2] +
          info->stride[2] * (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_YUV9:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4);
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[1] = info->stride[0] * height;
      info->offset[2] = info->offset[1] +
          info->stride[1] * (GST_ROUND_UP_4 (height) / 4);
      break;
    case GST_VIDEO_FORMAT_YVU9:
      info->stride[0] = GST_ROUND_UP_4 (width);
      info->stride[1] = GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4);
      info->stride[2] = info->stride[1];
      info->offset[0] = 0;
      info->offset[2] = info->stride[0] * height;
      info->offset[1] = info->offset[2] +
          info->stride[1] * (GST_ROUND_UP_4 (height) / 4);
      break;
    case GST_VIDEO_FORMAT_UNKNOWN:
      GST_ERROR ("invalid format");
      g_warning ("invalid format");
      break;
  }
  return 0;
}

typedef struct
{
  GstVideoFormat format;
  void (*getline) (guint8 * dest, const GstBlendVideoFormatInfo * src,
      guint xoff, int j);
  void (*putline) (GstBlendVideoFormatInfo * dest,
      GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff,
      int j);
  void (*matrix) (guint8 * tmpline, guint width);
} GetPutLine;


#define GET_LINE(info, comp, line) \
  (info)->pixels + info->offset[(comp)] + ((info)->stride[(comp)] * (line))

/* Line conversion to AYUV */

/* Supports YV12 as well */
static void
getline_I420 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_I420 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (src, 2, j >> 1) + GST_ROUND_UP_2 (xoff / 2), src->width);
}

/* Supports YV12 as well */
static void
putline_I420 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_I420 (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (dest, 2, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      line, srcinfo->width / 2);
}

static void
getline_YUY2 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_YUY2 (dest, GET_LINE (src, 0, j) +
      (GST_ROUND_UP_2 (xoff * 4) / 2), src->width / 2);
}

static void
putline_YUY2 (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  cogorc_putline_YUY2 (GET_LINE (dest, 0,
          j) + (GST_ROUND_UP_2 (xoff * 4) / 2), line, srcinfo->width / 2);
}


static void
getline_AYUV (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  memcpy (dest, GET_LINE (src, 0, j) + (xoff * 4), (src->width - xoff) * 4);
}

static void
putline_AYUV (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  memcpy (GET_LINE (dest, 0, j) + (xoff * 4), line, srcinfo->width * 4);
}

static void
getline_UYVY (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_UYVY (dest, GET_LINE (src, 0, j) + xoff * 2, src->width / 2);
}

static void
putline_UYVY (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  cogorc_putline_UYVY (GET_LINE (dest, 0, j) +
      (GST_ROUND_UP_2 (xoff * 4) / 2), line, srcinfo->width / 2);
}

static void
getline_v308 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j) + GST_ROUND_UP_2 (xoff * 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 0];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 2];
  }
}

static void
putline_v308 (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + GST_ROUND_UP_2 (xoff * 3);

  for (i = 0; i < srcinfo->width; i++) {
    destline[i * 3 + 0] = line[i * 4 + 1];
    destline[i * 3 + 1] = line[i * 4 + 2];
    destline[i * 3 + 2] = line[i * 4 + 3];
  }
}

static void
getline_v210 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j) + GST_ROUND_UP_2 (xoff * 4) / 5;

  for (i = 0; i < src->width; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u2, u4;
    guint16 v0, v2, v4;

    a0 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 0);
    a1 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 4);
    a2 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 8);
    a3 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 12);

    u0 = ((a0 >> 0) & 0x3ff) >> 2;
    y0 = ((a0 >> 10) & 0x3ff) >> 2;
    v0 = ((a0 >> 20) & 0x3ff) >> 2;
    y1 = ((a1 >> 0) & 0x3ff) >> 2;

    u2 = ((a1 >> 10) & 0x3ff) >> 2;
    y2 = ((a1 >> 20) & 0x3ff) >> 2;
    v2 = ((a2 >> 0) & 0x3ff) >> 2;
    y3 = ((a2 >> 10) & 0x3ff) >> 2;

    u4 = ((a2 >> 20) & 0x3ff) >> 2;
    y4 = ((a3 >> 0) & 0x3ff) >> 2;
    v4 = ((a3 >> 10) & 0x3ff) >> 2;
    y5 = ((a3 >> 20) & 0x3ff) >> 2;

    dest[4 * (i + 0) + 0] = 0xff;
    dest[4 * (i + 0) + 1] = y0;
    dest[4 * (i + 0) + 2] = u0;
    dest[4 * (i + 0) + 3] = v0;

    dest[4 * (i + 1) + 0] = 0xff;
    dest[4 * (i + 1) + 1] = y1;
    dest[4 * (i + 1) + 2] = u0;
    dest[4 * (i + 1) + 3] = v0;

    dest[4 * (i + 2) + 0] = 0xff;
    dest[4 * (i + 2) + 1] = y2;
    dest[4 * (i + 2) + 2] = u2;
    dest[4 * (i + 2) + 3] = v2;

    dest[4 * (i + 3) + 0] = 0xff;
    dest[4 * (i + 3) + 1] = y3;
    dest[4 * (i + 3) + 2] = u2;
    dest[4 * (i + 3) + 3] = v2;

    dest[4 * (i + 4) + 0] = 0xff;
    dest[4 * (i + 4) + 1] = y4;
    dest[4 * (i + 4) + 2] = u4;
    dest[4 * (i + 4) + 3] = v4;

    dest[4 * (i + 5) + 0] = 0xff;
    dest[4 * (i + 5) + 1] = y5;
    dest[4 * (i + 5) + 2] = u4;
    dest[4 * (i + 5) + 3] = v4;

  }

}

static void
putline_v210 (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + GST_ROUND_UP_2 (xoff * 4) / 5;


  for (i = 0; i < srcinfo->width + 5; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u1, u2;
    guint16 v0, v1, v2;

    y0 = line[4 * (i + 0) + 1] << 2;
    y1 = line[4 * (i + 1) + 1] << 2;
    y2 = line[4 * (i + 2) + 1] << 2;
    y3 = line[4 * (i + 3) + 1] << 2;
    y4 = line[4 * (i + 4) + 1] << 2;
    y5 = line[4 * (i + 5) + 1] << 2;

    u0 = (line[4 * (i + 0) + 2] + line[4 * (i + 1) + 2]) << 1;
    u1 = (line[4 * (i + 2) + 2] + line[4 * (i + 3) + 2]) << 1;
    u2 = (line[4 * (i + 4) + 2] + line[4 * (i + 5) + 2]) << 1;

    v0 = (line[4 * (i + 0) + 3] + line[4 * (i + 1) + 3]) << 1;
    v1 = (line[4 * (i + 2) + 3] + line[4 * (i + 3) + 3]) << 1;
    v2 = (line[4 * (i + 4) + 3] + line[4 * (i + 5) + 3]) << 1;

    a0 = u0 | (y0 << 10) | (v0 << 20);
    a1 = y1 | (u1 << 10) | (y2 << 20);
    a2 = v1 | (y3 << 10) | (u2 << 20);
    a3 = y4 | (v2 << 10) | (y5 << 20);

    GST_WRITE_UINT32_LE (destline + (i / 6) * 16 + 0, a0);
    GST_WRITE_UINT32_LE (destline + (i / 6) * 16 + 4, a1);
    GST_WRITE_UINT32_LE (destline + (i / 6) * 16 + 8, a2);
    GST_WRITE_UINT32_LE (destline + (i / 6) * 16 + 12, a3);
  }
}

static void
getline_v216 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j) + GST_ROUND_UP_2 (xoff + 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = GST_READ_UINT16_LE (srcline + i * 4 + 2) >> 8;
    dest[i * 4 + 2] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 0) >> 8;
    dest[i * 4 + 3] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 4) >> 8;
  }
}

static void
putline_v216 (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + GST_ROUND_UP_2 (xoff + 3);

  for (i = 0; i < srcinfo->width / 2; i++) {
    GST_WRITE_UINT16_LE (destline + i * 8 + 0, line[(i * 2 + 0) * 4 + 2] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 2, line[(i * 2 + 0) * 4 + 1] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 4, line[(i * 2 + 1) * 4 + 3] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 8, line[(i * 2 + 0) * 4 + 1] << 8);
  }
}

static void
getline_Y41B (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_YUV9 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j) + (xoff / 4), GET_LINE (src, 2, j) + (xoff / 4),
      src->width / 2);
}

static void
putline_Y41B (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destlineY = GET_LINE (dest, 0, j) + xoff;
  guint8 *destlineU = GET_LINE (dest, 1, j) + (xoff / 4);
  guint8 *destlineV = GET_LINE (dest, 2, j) + (xoff / 4);

  for (i = 0; i < srcinfo->width - 3; i += 4) {
    destlineY[i] = line[i * 4 + 1];
    destlineY[i + 1] = line[i * 4 + 5];
    destlineY[i + 2] = line[i * 4 + 9];
    destlineY[i + 3] = line[i * 4 + 13];

    destlineU[i >> 2] =
        (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10] + line[i * 4 +
            14] + 2) >> 2;
    destlineV[i >> 2] =
        (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11] + line[i * 4 +
            15] + 2) >> 2;
  }

  if (i == srcinfo->width - 3) {
    destlineY[i] = line[i * 4 + 1];
    destlineY[i + 1] = line[i * 4 + 5];
    destlineY[i + 2] = line[i * 4 + 9];

    destlineU[i >> 2] =
        (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10] + 1) / 3;
    destlineV[i >> 2] =
        (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11] + 1) / 3;
  } else if (i == srcinfo->width - 2) {
    destlineY[i] = line[i * 4 + 1];
    destlineY[i + 1] = line[i * 4 + 5];

    destlineU[i >> 2] = (line[i * 4 + 2] + line[i * 4 + 6] + 1) >> 1;
    destlineV[i >> 2] = (line[i * 4 + 3] + line[i * 4 + 7] + 1) >> 1;
  } else if (i == srcinfo->width - 1) {
    destlineY[i + 1] = line[i * 4 + 5];

    destlineU[i >> 2] = line[i * 4 + 2];
    destlineV[i >> 2] = line[i * 4 + 3];
  }
}

static void
getline_Y42B (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_Y42B (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (src, 2, j) + GST_ROUND_UP_2 (xoff / 2), src->width / 2);
}

static void
putline_Y42B (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_Y42B (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (dest, 2, j) + GST_ROUND_UP_2 (xoff / 2), line,
      srcinfo->width / 2);
}

static void
getline_Y444 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_Y444 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j) + xoff, GET_LINE (src, 2, j) + xoff, src->width);
}

static void
putline_Y444 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_Y444 (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j) + xoff,
      GET_LINE (dest, 2, j) + xoff, line, srcinfo->width);
}

static void
getline_Y800 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_Y800 (dest, GET_LINE (src, 0, j) + xoff, src->width);
}

static void
putline_Y800 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_Y800 (GET_LINE (dest, 0, j) + xoff, line, srcinfo->width);
}

static void
getline_Y16 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_Y16 (dest, GET_LINE (src, 0, j) + xoff * 2, src->width);
}

static void
putline_Y16 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_Y16 (GET_LINE (dest, 0, j) + xoff * 2, line, srcinfo->width);
}

static void
getline_NV12 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_NV12 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j >> 1) + xoff, src->width / 2);
}

static void
putline_NV12 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_NV12 (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j >> 1) + xoff, line, srcinfo->width / 2);
}

static void
getline_NV21 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_NV21 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j >> 1) + xoff, src->width / 2);
}

static void
putline_NV21 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_NV21 (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j >> 1) + xoff, line, srcinfo->width / 2);
}

static void
getline_UYVP (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j)
      + xoff * 3;

  for (i = 0; i < src->width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    u0 = (srcline[(i / 2) * 5 + 0] << 2) | (srcline[(i / 2) * 5 + 1] >> 6);

    y0 = ((srcline[(i / 2) * 5 + 1] & 0x3f) << 4) |
        (srcline[(i / 2) * 5 + 2] >> 4);

    v0 = ((srcline[(i / 2) * 5 + 2] & 0x0f) << 6) |
        (srcline[(i / 2) * 5 + 3] >> 2);

    y1 = ((srcline[(i / 2) * 5 + 3] & 0x03) << 8) | srcline[(i / 2) * 5 + 4];

    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = y0 >> 2;
    dest[i * 4 + 2] = u0 >> 2;
    dest[i * 4 + 3] = v0 >> 2;
    dest[i * 4 + 4] = 0xff;
    dest[i * 4 + 5] = y1 >> 2;
    dest[i * 4 + 6] = u0 >> 2;
    dest[i * 4 + 7] = v0 >> 2;
  }
}

static void
putline_UYVP (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + xoff * 3;

  for (i = 0; i < srcinfo->width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    y0 = line[4 * (i + 0) + 1];
    y1 = line[4 * (i + 1) + 1];
    u0 = (line[4 * (i + 0) + 2] + line[4 * (i + 1) + 2] + 1) >> 1;
    v0 = (line[4 * (i + 0) + 3] + line[4 * (i + 1) + 3] + 1) >> 1;

    destline[(i / 2) * 5 + 0] = u0;
    destline[(i / 2) * 5 + 1] = y0 >> 2;
    destline[(i / 2) * 5 + 2] = (y0 << 6) | (v0 >> 4);
    destline[(i / 2) * 5 + 3] = (v0 << 4) | (y1 >> 2);
    destline[(i / 2) * 5 + 4] = (y1 << 2);
  }
}

static void
getline_A420 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_A420 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (src, 2, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (src, 3, j) + GST_ROUND_UP_2 (xoff / 2), src->width);
}

static void
putline_A420 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_A420 (GET_LINE (dest, 0, j) + xoff,
      GET_LINE (dest, 1, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (dest, 2, j >> 1) + GST_ROUND_UP_2 (xoff / 2),
      GET_LINE (dest, 3, j) + GST_ROUND_UP_2 (xoff / 2), line,
      srcinfo->width / 2);
}

static void
getline_YUV9 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_YUV9 (dest,
      GET_LINE (src, 0, j) + xoff,
      GET_LINE (src, 1, j >> 2) + GST_ROUND_UP_4 (xoff / 4),
      GET_LINE (src, 2, j >> 2) + GST_ROUND_UP_4 (xoff / 4), src->width / 2);
}

static void
putline_YUV9 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destY = GET_LINE (dest, 0, j) + xoff;
  guint8 *destU = GET_LINE (dest, 1, j >> 2) + GST_ROUND_UP_4 (xoff / 4);
  guint8 *destV = GET_LINE (dest, 2, j >> 2) + GST_ROUND_UP_4 (xoff / 4);
  guint width = srcinfo->width;

  for (i = 0; i < width - 3; i += 4) {
    destY[i] = line[i * 4 + 1];
    destY[i + 1] = line[i * 4 + 5];
    destY[i + 2] = line[i * 4 + 9];
    destY[i + 3] = line[i * 4 + 13];
    if (j % 4 == 0) {
      destU[i >> 2] =
          (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10] + line[i * 4 +
              14]) >> 2;
      destV[i >> 2] =
          (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11] + line[i * 4 +
              15]) >> 2;
    }
  }

  if (i == width - 3) {
    destY[i] = line[i * 4 + 1];
    destY[i + 1] = line[i * 4 + 5];
    destY[i + 2] = line[i * 4 + 9];
    if (j % 4 == 0) {
      destU[i >> 2] =
          (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10]) / 3;
      destV[i >> 2] =
          (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11]) / 3;
    }
  } else if (i == width - 2) {
    destY[i] = line[i * 4 + 1];
    destY[i + 1] = line[i * 4 + 5];
    if (j % 4 == 0) {
      destU[i >> 2] = (line[i * 4 + 2] + line[i * 4 + 6]) >> 1;
      destV[i >> 2] = (line[i * 4 + 3] + line[i * 4 + 7]) >> 1;
    }
  } else if (i == width - 1) {
    destY[i] = line[i * 4 + 1];
    destU[i >> 2] = line[i * 4 + 2];
    destV[i >> 2] = line[i * 4 + 3];
  }
}

static void
getline_IYU1 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline =
      GET_LINE (src, 0, j) + GST_ROUND_UP_2 ((xoff * 3) / 2);
  guint width = src->width;

  for (i = 0; i < width - 3; i += 4) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 4] = 0xff;
    dest[i * 4 + 8] = 0xff;
    dest[i * 4 + 12] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 5] = srcline[(i >> 2) * 6 + 2];
    dest[i * 4 + 9] = srcline[(i >> 2) * 6 + 4];
    dest[i * 4 + 13] = srcline[(i >> 2) * 6 + 5];
    dest[i * 4 + 2] = dest[i * 4 + 6] = dest[i * 4 + 10] = dest[i * 4 + 14] =
        srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = dest[i * 4 + 7] = dest[i * 4 + 11] = dest[i * 4 + 15] =
        srcline[(i >> 2) * 6 + 3];
  }

  if (i == width - 3) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 4] = 0xff;
    dest[i * 4 + 8] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 5] = srcline[(i >> 2) * 6 + 2];
    dest[i * 4 + 9] = srcline[(i >> 2) * 6 + 4];
    dest[i * 4 + 2] = dest[i * 4 + 6] = dest[i * 4 + 10] =
        srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = dest[i * 4 + 7] = dest[i * 4 + 11] =
        srcline[(i >> 2) * 6 + 3];
  } else if (i == width - 2) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 4] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 5] = srcline[(i >> 2) * 6 + 2];
    dest[i * 4 + 2] = dest[i * 4 + 6] = srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = dest[i * 4 + 7] = srcline[(i >> 2) * 6 + 3];
  } else if (i == width - 1) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 2] = srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = srcline[(i >> 2) * 6 + 3];
  }
}

static void
putline_IYU1 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + GST_ROUND_UP_2 ((xoff * 3) / 2);
  guint width = srcinfo->width;

  for (i = 0; i < width - 3; i += 4) {
    destline[(i >> 2) * 6 + 1] = line[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = line[i * 4 + 5];
    destline[(i >> 2) * 6 + 4] = line[i * 4 + 9];
    destline[(i >> 2) * 6 + 5] = line[i * 4 + 13];
    destline[(i >> 2) * 6 + 0] =
        (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10] + line[i * 4 +
            14]) >> 2;
    destline[(i >> 2) * 6 + 3] =
        (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11] + line[i * 4 +
            15]) >> 2;
  }

  if (i == width - 3) {
    destline[(i >> 2) * 6 + 1] = line[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = line[i * 4 + 5];
    destline[(i >> 2) * 6 + 4] = line[i * 4 + 9];
    destline[(i >> 2) * 6 + 0] =
        (line[i * 4 + 2] + line[i * 4 + 6] + line[i * 4 + 10]) / 3;
    destline[(i >> 2) * 6 + 3] =
        (line[i * 4 + 3] + line[i * 4 + 7] + line[i * 4 + 11]) / 3;
  } else if (i == width - 2) {
    destline[(i >> 2) * 6 + 1] = line[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = line[i * 4 + 5];
    destline[(i >> 2) * 6 + 0] = (line[i * 4 + 2] + line[i * 4 + 6]) >> 1;
    destline[(i >> 2) * 6 + 3] = (line[i * 4 + 3] + line[i * 4 + 7]) >> 1;
  } else if (i == width - 1) {
    destline[(i >> 2) * 6 + 1] = line[i * 4 + 1];
    destline[(i >> 2) * 6 + 0] = line[i * 4 + 2];
    destline[(i >> 2) * 6 + 3] = line[i * 4 + 3];
  }
}


/* Line conversion to ARGB */
static void
getline_RGB (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j) + xoff * 3;

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 0];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 2];
  }
}

static void
putline_RGB (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + xoff * 3;

  for (i = 0; i < srcinfo->width; i++) {
    destline[i * 3 + 0] = line[i * 4 + 1];
    destline[i * 3 + 1] = line[i * 4 + 2];
    destline[i * 3 + 2] = line[i * 4 + 3];
  }
}

static void
getline_BGR (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint8 *srcline = GET_LINE (src, 0, j) + xoff * 3;

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 2];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 0];
  }
}

static void
putline_BGR (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  int i;
  guint8 *destline = GET_LINE (dest, 0, j) + xoff * 3;

  for (i = 0; i < srcinfo->width; i++) {
    destline[i * 3 + 0] = line[i * 4 + 3];
    destline[i * 3 + 1] = line[i * 4 + 2];
    destline[i * 3 + 2] = line[i * 4 + 1];
  }
}

static void
getline_RGBA (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_RGBA (dest, GET_LINE (src, 0, j) + (4 * xoff), src->width);
}

static void
putline_RGBA (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  cogorc_putline_RGBA (GET_LINE (dest, 0, j) + (4 * xoff),
      line, srcinfo->width);
}

static void
getline_ARGB (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{

  memcpy (dest, GET_LINE (src, 0, j), (src->width - xoff) * 4);
}

static void
putline_ARGB (GstBlendVideoFormatInfo * dest, GstBlendVideoFormatInfo * srcinfo,
    const guint8 * line, guint xoff, int j)
{
  memcpy (GET_LINE (dest, 0, j) + (xoff * 4), line, srcinfo->width * 4);
}

static void
getline_RGB16 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) GET_LINE (src, 0, j)
      + (xoff * 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = ((srcline[i] >> 11) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x3f) << 2;
    dest[i * 4 + 3] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_RGB16 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint16 *destline = (guint16 *) GET_LINE (dest, 0, j) + (xoff * 3);

  for (i = 0; i < srcinfo->width; i++) {
    destline[i] = ((line[i * 4 + 1] >> 3) << 11) | ((line[i * 4 +
                2] >> 2) << 5) | (line[i * 4 + 3] >> 3);
  }
}

static void
getline_RGB15 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) GET_LINE (src, 0, j)
      + (xoff * 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = ((srcline[i] >> 10) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x1f) << 3;
    dest[i * 4 + 3] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_RGB15 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint16 *destline = (guint16 *) GET_LINE (dest, 0, j) + (xoff * 3);

  for (i = 0; i < srcinfo->width; i++) {
    destline[i] = ((line[i * 4 + 1] >> 3) << 10) | ((line[i * 4 +
                2] >> 3) << 5) | (line[i * 4 + 3] >> 3);
  }
}

static void
getline_BGR15 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) GET_LINE (src, 0, j)
      + (xoff * 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 3] = ((srcline[i] >> 10) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x1f) << 3;
    dest[i * 4 + 1] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_BGR15 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint16 *destline = (guint16 *) GET_LINE (dest, 0, j) + (xoff * 3);

  for (i = 0; i < srcinfo->width; i++) {
    destline[i] = ((line[i * 4 + 3] >> 3) << 10) | ((line[i * 4 +
                2] >> 3) << 5) | (line[i * 4 + 1] >> 3);
  }
}

static void
getline_BGR16 (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) GET_LINE (src, 0, j)
      + (xoff * 3);

  for (i = 0; i < src->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 3] = ((srcline[i] >> 11) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x3f) << 2;
    dest[i * 4 + 1] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_BGR16 (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  int i;
  guint16 *destline = (guint16 *) GET_LINE (dest, 0, j) + (xoff * 3);

  for (i = 0; i < srcinfo->width; i++) {
    destline[i] = ((line[i * 4 + 3] >> 3) << 11) | ((line[i * 4 +
                2] >> 2) << 5) | (line[i * 4 + 1] >> 3);
  }
}

static void
getline_BGRA (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_BGRA (dest, GET_LINE (src, 0, j) + xoff * 4, src->width);
}

static void
putline_BGRA (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_BGRA (GET_LINE (dest, 0, j) + xoff * 4, line, srcinfo->width);
}

static void
getline_ABGR (guint8 * dest, const GstBlendVideoFormatInfo * src, guint xoff,
    int j)
{
  cogorc_getline_ABGR (dest, GET_LINE (src, 0, j) + (xoff * 4), src->width);
}

static void
putline_ABGR (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * srcinfo, const guint8 * line, guint xoff, int j)
{
  cogorc_putline_ABGR (GET_LINE (dest, 0, j) + (xoff * 4),
      line, srcinfo->width);
}

static const GetPutLine lines[] = {
  /* YUV lines conversion */
  {GST_VIDEO_FORMAT_I420, getline_I420, putline_I420},
  {GST_VIDEO_FORMAT_YV12, getline_I420, putline_I420},
  {GST_VIDEO_FORMAT_AYUV, getline_AYUV, putline_AYUV},
  {GST_VIDEO_FORMAT_YUY2, getline_YUY2, putline_YUY2},
  {GST_VIDEO_FORMAT_UYVY, getline_UYVY, putline_UYVY},
  {GST_VIDEO_FORMAT_v308, getline_v308, putline_v308},
  {GST_VIDEO_FORMAT_v210, getline_v210, putline_v210},
  {GST_VIDEO_FORMAT_v216, getline_v216, putline_v216},
  {GST_VIDEO_FORMAT_Y41B, getline_Y41B, putline_Y41B},
  {GST_VIDEO_FORMAT_Y42B, getline_Y42B, putline_Y42B},
  {GST_VIDEO_FORMAT_Y444, getline_Y444, putline_Y444},
  {GST_VIDEO_FORMAT_Y800, getline_Y800, putline_Y800},
  {GST_VIDEO_FORMAT_Y16, getline_Y16, putline_Y16},
  {GST_VIDEO_FORMAT_NV12, getline_NV12, putline_NV12},
  {GST_VIDEO_FORMAT_NV21, getline_NV21, putline_NV21},
  {GST_VIDEO_FORMAT_UYVP, getline_UYVP, putline_UYVP},
  {GST_VIDEO_FORMAT_A420, getline_A420, putline_A420},
  {GST_VIDEO_FORMAT_YUV9, getline_YUV9, putline_YUV9},
  {GST_VIDEO_FORMAT_IYU1, getline_IYU1, putline_IYU1},

  /* ARGB lines conversion */
  {GST_VIDEO_FORMAT_RGB, getline_RGB, putline_RGB},
  {GST_VIDEO_FORMAT_BGR, getline_BGR, putline_BGR},
  {GST_VIDEO_FORMAT_RGBx, getline_RGBA, putline_RGBA},
  {GST_VIDEO_FORMAT_RGBA, getline_RGBA, putline_RGBA},
  {GST_VIDEO_FORMAT_ARGB, getline_ARGB, putline_ARGB},
  {GST_VIDEO_FORMAT_RGB16, getline_RGB16, putline_RGB16},
  {GST_VIDEO_FORMAT_BGR16, getline_BGR16, putline_BGR16},
  {GST_VIDEO_FORMAT_BGR15, getline_BGR15, putline_BGR15},
  {GST_VIDEO_FORMAT_RGB15, getline_RGB15, putline_RGB15},
  {GST_VIDEO_FORMAT_BGRA, getline_BGRA, putline_BGRA},
  {GST_VIDEO_FORMAT_ABGR, getline_ABGR, putline_ABGR},
  {GST_VIDEO_FORMAT_BGRx, getline_BGRA, putline_BGRA}
};

static void
matrix_identity (guint8 * tmpline, guint width)
{
}

static void
matrix_rgb_to_yuv (guint8 * tmpline, guint width)
{
  int i;
  int r, g, b;
  int y, u, v;

  for (i = 0; i < width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (47 * r + 157 * g + 16 * b + 4096) >> 8;
    u = (-26 * r - 87 * g + 112 * b + 32768) >> 8;
    v = (112 * r - 102 * g - 10 * b + 32768) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 255);
  }
}

static void
matrix_yuv_to_rgb (guint8 * tmpline, guint width)
{
  int i;
  int r, g, b;
  int y, u, v;

  for (i = 0; i < width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (298 * y + 459 * v - 63514) >> 8;
    g = (298 * y - 55 * u - 136 * v + 19681) >> 8;
    b = (298 * y + 541 * u - 73988) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 255);
  }
}

static gboolean
lookup_getput (GetPutLine * getput, GstVideoFormat fmt)
{
  int i;

  getput->getline = NULL;
  getput->putline = NULL;
  getput->matrix = matrix_identity;

  for (i = 0; i < sizeof (lines) / sizeof (lines[0]); i++) {
    if (lines[i].format == fmt) {
      getput->getline = lines[i].getline;
      getput->putline = lines[i].putline;

      return TRUE;
    }
  }
  GST_WARNING ("Conversion from %i not supported", fmt);

  return FALSE;
}

#define BLEND(ret, alpha, v0, v1) \
{ \
  ret = (v0 * alpha + v1 * (255 - alpha)) / 255; \
}

void
video_blend_scale_linear_RGBA (GstBlendVideoFormatInfo * src,
    gint dest_height, gint dest_width)
{
  int acc;
  int y_increment;
  int x_increment;
  int y1;
  int i;
  int j;
  int x;
  int dest_size;
  guint dest_stride = dest_width * 4;
  guint src_stride = src->width * 4;

  guint8 *tmpbuf = g_malloc (dest_width * 8 * 4);
  guint8 *dest_pixels =
      g_malloc (gst_video_format_get_size (src->fmt, dest_height,
          dest_width));

  if (dest_height == 1)
    y_increment = 0;
  else
    y_increment = ((src->height - 1) << 16) / (dest_height - 1) - 1;

  if (dest_width == 1)
    x_increment = 0;
  else
    x_increment = ((src->width - 1) << 16) / (dest_width - 1) - 1;

  dest_size = dest_width * 4;

#define LINE(x) ((tmpbuf) + (dest_size)*((x)&1))

  acc = 0;
  orc_resample_bilinear_u32 (LINE (0), src->pixels, 0, x_increment, dest_width);
  y1 = 0;
  for (i = 0; i < dest_height; i++) {
    j = acc >> 16;
    x = acc & 0xffff;

    if (x == 0) {
      memcpy (dest_pixels + i * dest_stride, LINE (j), dest_size);
    } else {
      if (j > y1) {
        orc_resample_bilinear_u32 (LINE (j),
            src->pixels + j * src_stride, 0, x_increment, dest_width);
        y1++;
      }
      if (j >= y1) {
        orc_resample_bilinear_u32 (LINE (j + 1),
            src->pixels + (j + 1) * src_stride, 0, x_increment, dest_width);
        y1++;
      }
      orc_merge_linear_u8 (dest_pixels + i * dest_stride,
          LINE (j), LINE (j + 1), (x >> 8), dest_width * 4);
    }

    acc += y_increment;
  }

  /* Update src, our reference to the old src->pixels is lost */
  video_blend_format_info_init (src, dest_pixels, dest_height, dest_width,
      src->fmt);

  g_free (tmpbuf);
}

/* video_blend:
 * @dest: The #GstBlendVideoFormatInfo where to blend @src in
 * @src: the #GstBlendVideoFormatInfo that we want to blend into
 * @dest
 * @x: The x offset in pixel where the @src image should be blended
 * @y: the y offset in pixel where the @src image should be blended
 *
 * Lets you blend the @src image into the @dest image
 */
gboolean
video_blend (GstBlendVideoFormatInfo * dest,
    GstBlendVideoFormatInfo * src, guint x, guint y)
{
  guint i, j;
  guint8 alpha;
  GetPutLine getputdest, getputsrc;

  gint src_stride = src->width * 4;
  guint8 *tmpdestline = g_malloc (sizeof (guint8) * (dest->width + 8) * 4);
  guint8 *tmpsrcline = g_malloc (sizeof (guint8) * (dest->width + 8) * 4);

  ensure_debug_category ();


  if (!lookup_getput (&getputdest, dest->fmt))
    goto failed;

  if (!lookup_getput (&getputsrc, src->fmt))
    goto failed;

  if (gst_video_format_is_rgb (src->fmt) != gst_video_format_is_rgb (dest->fmt))
    getputsrc.matrix = gst_video_format_is_rgb (src->fmt) ?
        matrix_rgb_to_yuv : matrix_yuv_to_rgb;

  /* adjust src pointers for negative sizes */
  if (x < 0) {
    src += -x * 4;
    src->width -= -x;
    x = 0;
  }

  if (y < 0) {
    src += -y * src_stride;
    src->height -= -y;
    y = 0;
  }

  /* adjust width/height if the src is bigger than dest */
  if (x + src->width > dest->width)
    src->width = dest->width - x;

  if (y + src->height > dest->height)
    src->height = dest->height - y;

  /* Mainloop doing the needed conversions, and blending */
  for (i = y; i < y + src->height; i++) {

    getputdest.getline (tmpdestline, dest, x, i);
    getputsrc.getline (tmpsrcline, src, 0, (i - y));

    getputsrc.matrix (tmpsrcline, src->width);

    /* Here dest and src are both either in AYUV or ARGB
     * TODO: Make the orc version working properly*/
    for (j = 0; j < src->width * 4; j += 4) {
      alpha = tmpsrcline[j];

      BLEND (tmpdestline[j + 1], alpha, tmpsrcline[j + 1], tmpdestline[j + 1]);
      BLEND (tmpdestline[j + 2], alpha, tmpsrcline[j + 2], tmpdestline[j + 2]);
      BLEND (tmpdestline[j + 3], alpha, tmpsrcline[j + 3], tmpdestline[j + 3]);
    }

    /* FIXME
     * #if G_BYTE_ORDER == LITTLE_ENDIAN
     * orc_blend_little (tmpdestline, tmpsrcline, dest->width);
     * #else
     * orc_blend_big (tmpdestline, tmpsrcline, src->width);
     * #endif
     */

    getputdest.putline (dest, src, tmpdestline, x, i);

  }

  g_free (tmpdestline);
  g_free (tmpsrcline);

  return TRUE;

failed:
  GST_WARNING ("Could not do the blending");
  g_free (tmpdestline);
  g_free (tmpsrcline);

  return FALSE;
}

/* video_blend_format_info_init:
 * @info: The #GstBlendVideoFormatInfo to initialize
 * @pixels: The pixels data in @fmt format
 * @height: The height of the image
 * @width: the width of the image
 * @fmt: The #GstVideoFormat of the image
 *
 * Initializes a GstBlendVideoFormatInfo.
 * This function can be called on already initialized instances.
 */
void
video_blend_format_info_init (GstBlendVideoFormatInfo * info,
    guint8 * pixels, guint height, guint width, GstVideoFormat fmt)
{
  guint nb_component = gst_video_format_has_alpha (fmt) ? 4 : 3;

  ensure_debug_category ();

  GST_DEBUG
      ("Initializing video bleding info, height %i, width %i, fmt %i nb_component %i",
      height, width, fmt, nb_component);

  info->width = width;
  info->height = height;
  info->pixels = pixels;
  info->fmt = fmt;
  info->size = gst_video_format_get_size (fmt, height, width);

  fill_planes (info);
}
