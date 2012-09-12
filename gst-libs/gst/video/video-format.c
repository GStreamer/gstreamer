/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "video-format.h"

/**
 * SECTION:gstvideo
 * @short_description: Support library for video operations
 *
 * <refsect2>
 * <para>
 * This library contains some helper functions and includes the
 * videosink and videofilter base classes.
 * </para>
 * </refsect2>
 */

#include "video-orc.h"

/* Line conversion to AYUV */

#define GET_PLANE_STRIDE(plane) (stride(plane))
#define GET_PLANE_LINE(plane, line) \
  (gpointer)(((guint8*)(data[plane])) + stride[plane] * (line))

#define GET_COMP_STRIDE(comp) \
  GST_VIDEO_FORMAT_INFO_STRIDE (info, stride, comp)
#define GET_COMP_DATA(comp) \
  GST_VIDEO_FORMAT_INFO_DATA (info, data, comp)

#define GET_COMP_LINE(comp, line) \
  (gpointer)(((guint8*)GET_COMP_DATA (comp)) + \
      GET_COMP_STRIDE(comp) * (line))

#define GET_STRIDE()                 GET_PLANE_STRIDE (0)
#define GET_LINE(line)               GET_PLANE_LINE (0, line)

#define GET_Y_LINE(line)             GET_COMP_LINE(GST_VIDEO_COMP_Y, line)
#define GET_U_LINE(line)             GET_COMP_LINE(GST_VIDEO_COMP_U, line)
#define GET_V_LINE(line)             GET_COMP_LINE(GST_VIDEO_COMP_V, line)
#define GET_A_LINE(line)             GET_COMP_LINE(GST_VIDEO_COMP_A, line)

#define GET_Y_STRIDE()               GET_COMP_STRIDE(GST_VIDEO_COMP_Y)
#define GET_U_STRIDE()               GET_COMP_STRIDE(GST_VIDEO_COMP_U)
#define GET_V_STRIDE()               GET_COMP_STRIDE(GST_VIDEO_COMP_V)
#define GET_A_STRIDE()               GET_COMP_STRIDE(GST_VIDEO_COMP_A)

#define PACK_420 GST_VIDEO_FORMAT_AYUV, unpack_planar_420, 1, pack_planar_420
static void
unpack_planar_420 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_I420 (dest,
      GET_Y_LINE (y), GET_U_LINE (y >> 1), GET_V_LINE (y >> 1), width);
}

static void
pack_planar_420 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_I420 (GET_Y_LINE (y),
      GET_U_LINE (y >> 1), GET_V_LINE (y >> 1), src, width / 2);
}

#define PACK_YUY2 GST_VIDEO_FORMAT_AYUV, unpack_YUY2, 1, pack_YUY2
static void
unpack_YUY2 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_YUY2 (dest, GET_LINE (y), width / 2);
}

static void
pack_YUY2 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_YUY2 (GET_LINE (y), src, width / 2);
}

#define PACK_UYVY GST_VIDEO_FORMAT_AYUV, unpack_UYVY, 1, pack_UYVY
static void
unpack_UYVY (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_UYVY (dest, GET_LINE (y), width / 2);
}

static void
pack_UYVY (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_UYVY (GET_LINE (y), src, width / 2);
}

#define PACK_YVYU GST_VIDEO_FORMAT_AYUV, unpack_YVYU, 1, pack_YVYU
static void
unpack_YVYU (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_YVYU (dest, GET_LINE (y), width / 2);
}

static void
pack_YVYU (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_YVYU (GET_LINE (y), src, width / 2);
}

#define PACK_v308 GST_VIDEO_FORMAT_AYUV, unpack_v308, 1, pack_v308
static void
unpack_v308 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint8 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = s[i * 3 + 0];
    d[i * 4 + 2] = s[i * 3 + 1];
    d[i * 4 + 3] = s[i * 3 + 2];
  }
}

static void
pack_v308 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i * 3 + 0] = s[i * 4 + 1];
    d[i * 3 + 1] = s[i * 4 + 2];
    d[i * 3 + 2] = s[i * 4 + 3];
  }
}

#define PACK_AYUV GST_VIDEO_FORMAT_AYUV, unpack_copy4, 1, pack_copy4
#define PACK_ARGB GST_VIDEO_FORMAT_ARGB, unpack_copy4, 1, pack_copy4
static void
unpack_copy4 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  memcpy (dest, GET_LINE (y), width * 4);
}

static void
pack_copy4 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  memcpy (GET_LINE (y), src, width * 4);
}

#define PACK_v210 GST_VIDEO_FORMAT_AYUV64, unpack_v210, 1, pack_v210
static void
unpack_v210 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint16 *d = dest;

  for (i = 0; i < width; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u2, u4;
    guint16 v0, v2, v4;

    a0 = GST_READ_UINT32_LE (s + (i / 6) * 16 + 0);
    a1 = GST_READ_UINT32_LE (s + (i / 6) * 16 + 4);
    a2 = GST_READ_UINT32_LE (s + (i / 6) * 16 + 8);
    a3 = GST_READ_UINT32_LE (s + (i / 6) * 16 + 12);

    u0 = ((a0 >> 0) & 0x3ff) << 6;
    y0 = ((a0 >> 10) & 0x3ff) << 6;
    v0 = ((a0 >> 20) & 0x3ff) << 6;
    y1 = ((a1 >> 0) & 0x3ff) << 6;

    u2 = ((a1 >> 10) & 0x3ff) << 6;
    y2 = ((a1 >> 20) & 0x3ff) << 6;
    v2 = ((a2 >> 0) & 0x3ff) << 6;
    y3 = ((a2 >> 10) & 0x3ff) << 6;

    u4 = ((a2 >> 20) & 0x3ff) << 6;
    y4 = ((a3 >> 0) & 0x3ff) << 6;
    v4 = ((a3 >> 10) & 0x3ff) << 6;
    y5 = ((a3 >> 20) & 0x3ff) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      y0 |= (y0 >> 10);
      y1 |= (y1 >> 10);
      u0 |= (u0 >> 10);
      v0 |= (v0 >> 10);

      y2 |= (y2 >> 10);
      y3 |= (y3 >> 10);
      u2 |= (u2 >> 10);
      v2 |= (v2 >> 10);

      y4 |= (y4 >> 10);
      y5 |= (y5 >> 10);
      u4 |= (u4 >> 10);
      v4 |= (v4 >> 10);
    }

    d[4 * (i + 0) + 0] = 0xffff;
    d[4 * (i + 0) + 1] = y0;
    d[4 * (i + 0) + 2] = u0;
    d[4 * (i + 0) + 3] = v0;

    d[4 * (i + 1) + 0] = 0xffff;
    d[4 * (i + 1) + 1] = y1;
    d[4 * (i + 1) + 2] = u0;
    d[4 * (i + 1) + 3] = v0;

    d[4 * (i + 2) + 0] = 0xffff;
    d[4 * (i + 2) + 1] = y2;
    d[4 * (i + 2) + 2] = u2;
    d[4 * (i + 2) + 3] = v2;

    d[4 * (i + 3) + 0] = 0xffff;
    d[4 * (i + 3) + 1] = y3;
    d[4 * (i + 3) + 2] = u2;
    d[4 * (i + 3) + 3] = v2;

    d[4 * (i + 4) + 0] = 0xffff;
    d[4 * (i + 4) + 1] = y4;
    d[4 * (i + 4) + 2] = u4;
    d[4 * (i + 4) + 3] = v4;

    d[4 * (i + 5) + 0] = 0xffff;
    d[4 * (i + 5) + 1] = y5;
    d[4 * (i + 5) + 2] = u4;
    d[4 * (i + 5) + 3] = v4;
  }
}

static void
pack_v210 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u1, u2;
    guint16 v0, v1, v2;

    y0 = s[4 * (i + 0) + 1] >> 6;
    y1 = s[4 * (i + 1) + 1] >> 6;
    y2 = s[4 * (i + 2) + 1] >> 6;
    y3 = s[4 * (i + 3) + 1] >> 6;
    y4 = s[4 * (i + 4) + 1] >> 6;
    y5 = s[4 * (i + 5) + 1] >> 6;

    u0 = (s[4 * (i + 0) + 2] + s[4 * (i + 1) + 2] + 1) >> 7;
    u1 = (s[4 * (i + 2) + 2] + s[4 * (i + 3) + 2] + 1) >> 7;
    u2 = (s[4 * (i + 4) + 2] + s[4 * (i + 5) + 2] + 1) >> 7;

    v0 = (s[4 * (i + 0) + 3] + s[4 * (i + 1) + 3] + 1) >> 7;
    v1 = (s[4 * (i + 2) + 3] + s[4 * (i + 3) + 3] + 1) >> 7;
    v2 = (s[4 * (i + 4) + 3] + s[4 * (i + 5) + 3] + 1) >> 7;

    a0 = u0 | (y0 << 10) | (v0 << 20);
    a1 = y1 | (u1 << 10) | (y2 << 20);
    a2 = v1 | (y3 << 10) | (u2 << 20);
    a3 = y4 | (v2 << 10) | (y5 << 20);

    GST_WRITE_UINT32_LE (d + (i / 6) * 16 + 0, a0);
    GST_WRITE_UINT32_LE (d + (i / 6) * 16 + 4, a1);
    GST_WRITE_UINT32_LE (d + (i / 6) * 16 + 8, a2);
    GST_WRITE_UINT32_LE (d + (i / 6) * 16 + 12, a3);
  }
}

#define PACK_v216 GST_VIDEO_FORMAT_AYUV64, unpack_v216, 1, pack_v216
static void
unpack_v216 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint16 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = GST_READ_UINT16_LE (s + i * 4 + 2);
    d[i * 4 + 2] = GST_READ_UINT16_LE (s + (i >> 1) * 8 + 0);
    d[i * 4 + 3] = GST_READ_UINT16_LE (s + (i >> 1) * 8 + 4);
  }
}

static void
pack_v216 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width / 2; i++) {
    GST_WRITE_UINT16_LE (d + i * 8 + 0, s[(i * 2 + 0) * 4 + 2]);
    GST_WRITE_UINT16_LE (d + i * 8 + 2, s[(i * 2 + 0) * 4 + 1]);
    GST_WRITE_UINT16_LE (d + i * 8 + 4, s[(i * 2 + 0) * 4 + 3]);
    GST_WRITE_UINT16_LE (d + i * 8 + 6, s[(i * 2 + 1) * 4 + 1]);
  }
}

#define PACK_Y41B GST_VIDEO_FORMAT_AYUV, unpack_Y41B, 1, pack_Y41B
static void
unpack_Y41B (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_YUV9 (dest,
      GET_Y_LINE (y), GET_U_LINE (y), GET_V_LINE (y), width / 2);
}

static void
pack_Y41B (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *destY = GET_Y_LINE (y);
  guint8 *destU = GET_U_LINE (y);
  guint8 *destV = GET_V_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width - 3; i += 4) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];
    destY[i + 2] = s[i * 4 + 9];
    destY[i + 3] = s[i * 4 + 13];

    destU[i >> 2] =
        (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10] + s[i * 4 + 14] + 2) >> 2;
    destV[i >> 2] =
        (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11] + s[i * 4 + 15] + 2) >> 2;
  }

  if (i == width - 3) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];
    destY[i + 2] = s[i * 4 + 9];

    destU[i >> 2] = (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10] + 1) / 3;
    destV[i >> 2] = (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11] + 1) / 3;
  } else if (i == width - 2) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];

    destU[i >> 2] = (s[i * 4 + 2] + s[i * 4 + 6] + 1) >> 1;
    destV[i >> 2] = (s[i * 4 + 3] + s[i * 4 + 7] + 1) >> 1;
  } else if (i == width - 1) {
    destY[i + 1] = s[i * 4 + 5];

    destU[i >> 2] = s[i * 4 + 2];
    destV[i >> 2] = s[i * 4 + 3];
  }
}

#define PACK_Y42B GST_VIDEO_FORMAT_AYUV, unpack_Y42B, 1, pack_Y42B
static void
unpack_Y42B (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_Y42B (dest,
      GET_Y_LINE (y), GET_U_LINE (y), GET_V_LINE (y), width / 2);
}

static void
pack_Y42B (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_Y42B (GET_Y_LINE (y),
      GET_U_LINE (y), GET_V_LINE (y), src, width / 2);
}

#define PACK_Y444 GST_VIDEO_FORMAT_AYUV, unpack_Y444, 1, pack_Y444
static void
unpack_Y444 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_Y444 (dest, GET_Y_LINE (y), GET_U_LINE (y),
      GET_V_LINE (y), width);
}

static void
pack_Y444 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_Y444 (GET_Y_LINE (y), GET_U_LINE (y), GET_V_LINE (y), src,
      width);
}

#define PACK_GRAY8 GST_VIDEO_FORMAT_AYUV, unpack_GRAY8, 1, pack_GRAY8
static void
unpack_GRAY8 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_GRAY8 (dest, GET_LINE (y), width);
}

static void
pack_GRAY8 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_GRAY8 (GET_LINE (y), src, width);
}

#define PACK_GRAY16_BE GST_VIDEO_FORMAT_AYUV64, unpack_GRAY16_BE, 1, pack_GRAY16_BE
static void
unpack_GRAY16_BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint16 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = GST_READ_UINT16_BE (s + i);
    d[i * 4 + 2] = 0x8000;
    d[i * 4 + 3] = 0x8000;
  }
}

static void
pack_GRAY16_BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width; i++) {
    GST_WRITE_UINT16_BE (d + i, s[i * 4 + 1]);
  }
}

#define PACK_GRAY16_LE GST_VIDEO_FORMAT_AYUV64, unpack_GRAY16_LE, 1, pack_GRAY16_LE
static void
unpack_GRAY16_LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint16 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = GST_READ_UINT16_LE (s + i);
    d[i * 4 + 2] = 0x8000;
    d[i * 4 + 3] = 0x8000;
  }
}

static void
pack_GRAY16_LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width; i++) {
    GST_WRITE_UINT16_LE (d + i, s[i * 4 + 1]);
  }
}

#define PACK_RGB16 GST_VIDEO_FORMAT_ARGB, unpack_RGB16, 1, pack_RGB16
static void
unpack_RGB16 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint8 *d = dest, r, g, b;

  for (i = 0; i < width; i++) {
    r = ((s[i] >> 11) & 0x1f) << 3;
    g = ((s[i] >> 5) & 0x3f) << 2;
    b = ((s[i]) & 0x1f) << 3;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      r |= (r >> 5);
      g |= (g >> 6);
      b |= (b >> 5);
    }

    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = r;
    d[i * 4 + 2] = g;
    d[i * 4 + 3] = b;
  }
}

static void
pack_RGB16 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i] = ((s[i * 4 + 1] >> 3) << 11) |
        ((s[i * 4 + 2] >> 2) << 5) | (s[i * 4 + 3] >> 3);
  }
}

#define PACK_BGR16 GST_VIDEO_FORMAT_ARGB, unpack_BGR16, 1, pack_BGR16
static void
unpack_BGR16 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint8 *d = dest, r, g, b;

  for (i = 0; i < width; i++) {
    b = ((s[i] >> 11) & 0x1f) << 3;
    g = ((s[i] >> 5) & 0x3f) << 2;
    r = ((s[i]) & 0x1f) << 3;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      r |= (r >> 5);
      g |= (g >> 6);
      b |= (b >> 5);
    }

    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = r;
    d[i * 4 + 2] = g;
    d[i * 4 + 3] = b;
  }
}

static void
pack_BGR16 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i] = ((s[i * 4 + 3] >> 3) << 11) |
        ((s[i * 4 + 2] >> 2) << 5) | (s[i * 4 + 1] >> 3);
  }
}

#define PACK_RGB15 GST_VIDEO_FORMAT_ARGB, unpack_RGB15, 1, pack_RGB15
static void
unpack_RGB15 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint8 *d = dest, r, g, b;

  for (i = 0; i < width; i++) {
    r = ((s[i] >> 10) & 0x1f) << 3;
    g = ((s[i] >> 5) & 0x1f) << 3;
    b = ((s[i]) & 0x1f) << 3;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      r |= (r >> 5);
      g |= (g >> 5);
      b |= (b >> 5);
    }

    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = r;
    d[i * 4 + 2] = g;
    d[i * 4 + 3] = b;
  }
}

static void
pack_RGB15 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i] = ((s[i * 4 + 1] >> 3) << 10) |
        ((s[i * 4 + 2] >> 3) << 5) | (s[i * 4 + 3] >> 3);
  }
}

#define PACK_BGR15 GST_VIDEO_FORMAT_ARGB, unpack_BGR15, 1, pack_BGR15
static void
unpack_BGR15 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint16 *s = GET_LINE (y);
  guint8 *d = dest, r, g, b;

  for (i = 0; i < width; i++) {
    b = ((s[i] >> 10) & 0x1f) << 3;
    g = ((s[i] >> 5) & 0x1f) << 3;
    r = ((s[i]) & 0x1f) << 3;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      r |= (r >> 5);
      g |= (g >> 5);
      b |= (b >> 5);
    }

    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = r;
    d[i * 4 + 2] = g;
    d[i * 4 + 3] = b;
  }
}

static void
pack_BGR15 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i] = ((s[i * 4 + 3] >> 3) << 10) |
        ((s[i * 4 + 2] >> 3) << 5) | (s[i * 4 + 1] >> 3);
  }
}

#define PACK_BGRA GST_VIDEO_FORMAT_ARGB, unpack_BGRA, 1, pack_BGRA
static void
unpack_BGRA (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_BGRA (dest, GET_LINE (y), width);
}

static void
pack_BGRA (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_BGRA (GET_LINE (y), src, width);
}

#define PACK_ABGR GST_VIDEO_FORMAT_ARGB, unpack_ABGR, 1, pack_ABGR
static void
unpack_ABGR (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_ABGR (dest, GET_LINE (y), width);
}

static void
pack_ABGR (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_ABGR (GET_LINE (y), src, width);
}

#define PACK_RGBA GST_VIDEO_FORMAT_ARGB, unpack_RGBA, 1, pack_RGBA
static void
unpack_RGBA (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_RGBA (dest, GET_LINE (y), width);
}

static void
pack_RGBA (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_RGBA (GET_LINE (y), src, width);
}

#define PACK_RGB GST_VIDEO_FORMAT_ARGB, unpack_RGB, 1, pack_RGB
static void
unpack_RGB (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint8 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = s[i * 3 + 0];
    d[i * 4 + 2] = s[i * 3 + 1];
    d[i * 4 + 3] = s[i * 3 + 2];
  }
}

static void
pack_RGB (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i * 3 + 0] = s[i * 4 + 1];
    d[i * 3 + 1] = s[i * 4 + 2];
    d[i * 3 + 2] = s[i * 4 + 3];
  }
}

#define PACK_BGR GST_VIDEO_FORMAT_ARGB, unpack_BGR, 1, pack_BGR
static void
unpack_BGR (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint8 *d = dest;

  for (i = 0; i < width; i++) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = s[i * 3 + 2];
    d[i * 4 + 2] = s[i * 3 + 1];
    d[i * 4 + 3] = s[i * 3 + 0];
  }
}

static void
pack_BGR (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width; i++) {
    d[i * 3 + 0] = s[i * 4 + 3];
    d[i * 3 + 1] = s[i * 4 + 2];
    d[i * 3 + 2] = s[i * 4 + 1];
  }
}

#define PACK_NV12 GST_VIDEO_FORMAT_AYUV, unpack_NV12, 1, pack_NV12
static void
unpack_NV12 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_NV12 (dest,
      GET_PLANE_LINE (0, y), GET_PLANE_LINE (1, y >> 1), width / 2);
}

static void
pack_NV12 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_NV12 (GET_PLANE_LINE (0, y),
      GET_PLANE_LINE (1, y >> 1), src, width / 2);
}

#define PACK_NV21 GST_VIDEO_FORMAT_AYUV, unpack_NV21, 1, pack_NV21
static void
unpack_NV21 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_NV21 (dest,
      GET_PLANE_LINE (0, y), GET_PLANE_LINE (1, y >> 1), width / 2);
}

static void
pack_NV21 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_NV21 (GET_PLANE_LINE (0, y),
      GET_PLANE_LINE (1, y >> 1), src, width / 2);
}

#define PACK_UYVP GST_VIDEO_FORMAT_AYUV64, unpack_UYVP, 1, pack_UYVP
static void
unpack_UYVP (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint16 *d = dest;

  for (i = 0; i < width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    u0 = ((s[(i / 2) * 5 + 0] << 2) | (s[(i / 2) * 5 + 1] >> 6)) << 6;
    y0 = (((s[(i / 2) * 5 + 1] & 0x3f) << 4) | (s[(i / 2) * 5 + 2] >> 4)) << 6;
    v0 = (((s[(i / 2) * 5 + 2] & 0x0f) << 6) | (s[(i / 2) * 5 + 3] >> 2)) << 6;
    y1 = (((s[(i / 2) * 5 + 3] & 0x03) << 8) | s[(i / 2) * 5 + 4]) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      y0 |= (y0 >> 4);
      y1 |= (y1 >> 4);
      u0 |= (u0 >> 4);
      v0 |= (v0 >> 4);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = y0;
    d[i * 4 + 2] = u0;
    d[i * 4 + 3] = v0;

    d[i * 4 + 4] = 0xffff;
    d[i * 4 + 5] = y1;
    d[i * 4 + 6] = u0;
    d[i * 4 + 7] = v0;
  }
}

static void
pack_UYVP (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    y0 = s[4 * (i + 0) + 1];
    y1 = s[4 * (i + 1) + 1];
    u0 = (s[4 * (i + 0) + 2] + s[4 * (i + 1) + 2] + 1) >> 1;
    v0 = (s[4 * (i + 0) + 3] + s[4 * (i + 1) + 3] + 1) >> 1;

    d[(i / 2) * 5 + 0] = u0 >> 8;
    d[(i / 2) * 5 + 1] = (u0 & 0xc0) | y0 >> 10;
    d[(i / 2) * 5 + 2] = ((y0 & 0x3c0) >> 2) | (v0 >> 12);
    d[(i / 2) * 5 + 3] = ((v0 & 0xfc0) >> 4) | (y1 >> 14);
    d[(i / 2) * 5 + 4] = (y1 >> 6);
  }
}

#define PACK_A420 GST_VIDEO_FORMAT_AYUV, unpack_A420, 1, pack_A420
static void
unpack_A420 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_A420 (dest,
      GET_Y_LINE (y), GET_U_LINE (y >> 1), GET_V_LINE (y >> 1), GET_A_LINE (y),
      width);
}

static void
pack_A420 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  video_orc_pack_A420 (GET_Y_LINE (y),
      GET_U_LINE (y >> 1), GET_V_LINE (y >> 1), GET_A_LINE (y), src, width / 2);
}

#define PACK_RGB8P GST_VIDEO_FORMAT_ARGB, unpack_RGB8P, 1, pack_RGB8P
static void
unpack_RGB8P (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  const guint32 *p = data[1];
  guint8 *d = dest;

  for (i = 0; i < width; i++) {
    guint32 v = p[s[i]];
    d[i * 4 + 0] = (v >> 24) & 0xff;
    d[i * 4 + 1] = (v >> 16) & 0xff;
    d[i * 4 + 2] = (v >> 8) & 0xff;
    d[i * 4 + 3] = (v) & 0xff;
  }
}

static void
pack_RGB8P (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint8 *s = src;

  /* Use our poor man's palette, taken from ffmpegcolorspace too */
  for (i = 0; i < width; i++) {
    /* crude approximation for alpha ! */
    if (s[i * 4 + 0] < 0x80)
      d[i] = 6 * 6 * 6;
    else
      d[i] =
          ((((s[i * 4 + 1]) / 47) % 6) * 6 * 6 + (((s[i * 4 +
                          2]) / 47) % 6) * 6 + (((s[i * 4 + 3]) / 47) % 6));
  }
}

#define PACK_410 GST_VIDEO_FORMAT_AYUV, unpack_410, 1, pack_410
static void
unpack_410 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  video_orc_unpack_YUV9 (dest,
      GET_Y_LINE (y), GET_U_LINE (y >> 2), GET_V_LINE (y >> 2), width / 2);
}

static void
pack_410 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *destY = GET_Y_LINE (y);
  guint8 *destU = GET_U_LINE (y >> 2);
  guint8 *destV = GET_V_LINE (y >> 2);
  const guint8 *s = src;

  for (i = 0; i < width - 3; i += 4) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];
    destY[i + 2] = s[i * 4 + 9];
    destY[i + 3] = s[i * 4 + 13];
    if (y % 4 == 0) {
      destU[i >> 2] =
          (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10] + s[i * 4 + 14]) >> 2;
      destV[i >> 2] =
          (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11] + s[i * 4 + 15]) >> 2;
    }
  }

  if (i == width - 3) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];
    destY[i + 2] = s[i * 4 + 9];
    if (y % 4 == 0) {
      destU[i >> 2] = (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10]) / 3;
      destV[i >> 2] = (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11]) / 3;
    }
  } else if (i == width - 2) {
    destY[i] = s[i * 4 + 1];
    destY[i + 1] = s[i * 4 + 5];
    if (y % 4 == 0) {
      destU[i >> 2] = (s[i * 4 + 2] + s[i * 4 + 6]) >> 1;
      destV[i >> 2] = (s[i * 4 + 3] + s[i * 4 + 7]) >> 1;
    }
  } else if (i == width - 1) {
    destY[i] = s[i * 4 + 1];
    destU[i >> 2] = s[i * 4 + 2];
    destV[i >> 2] = s[i * 4 + 3];
  }
}

#define PACK_IYU1 GST_VIDEO_FORMAT_AYUV, unpack_IYU1, 1, pack_IYU1
static void
unpack_IYU1 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint8 *d = dest;

  for (i = 0; i < width - 3; i += 4) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 4] = 0xff;
    d[i * 4 + 8] = 0xff;
    d[i * 4 + 12] = 0xff;
    d[i * 4 + 1] = s[(i >> 2) * 6 + 1];
    d[i * 4 + 5] = s[(i >> 2) * 6 + 2];
    d[i * 4 + 9] = s[(i >> 2) * 6 + 4];
    d[i * 4 + 13] = s[(i >> 2) * 6 + 5];
    d[i * 4 + 2] = d[i * 4 + 6] = d[i * 4 + 10] = d[i * 4 + 14] =
        s[(i >> 2) * 6 + 0];
    d[i * 4 + 3] = d[i * 4 + 7] = d[i * 4 + 11] = d[i * 4 + 15] =
        s[(i >> 2) * 6 + 3];
  }

  if (i == width - 3) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 4] = 0xff;
    d[i * 4 + 8] = 0xff;
    d[i * 4 + 1] = s[(i >> 2) * 6 + 1];
    d[i * 4 + 5] = s[(i >> 2) * 6 + 2];
    d[i * 4 + 9] = s[(i >> 2) * 6 + 4];
    d[i * 4 + 2] = d[i * 4 + 6] = d[i * 4 + 10] = s[(i >> 2) * 6 + 0];
    d[i * 4 + 3] = d[i * 4 + 7] = d[i * 4 + 11] = s[(i >> 2) * 6 + 3];
  } else if (i == width - 2) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 4] = 0xff;
    d[i * 4 + 1] = s[(i >> 2) * 6 + 1];
    d[i * 4 + 5] = s[(i >> 2) * 6 + 2];
    d[i * 4 + 2] = d[i * 4 + 6] = s[(i >> 2) * 6 + 0];
    d[i * 4 + 3] = d[i * 4 + 7] = s[(i >> 2) * 6 + 3];
  } else if (i == width - 1) {
    d[i * 4 + 0] = 0xff;
    d[i * 4 + 1] = s[(i >> 2) * 6 + 1];
    d[i * 4 + 2] = s[(i >> 2) * 6 + 0];
    d[i * 4 + 3] = s[(i >> 2) * 6 + 3];
  }
}

static void
pack_IYU1 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint8 *s = src;

  for (i = 0; i < width - 3; i += 4) {
    d[(i >> 2) * 6 + 1] = s[i * 4 + 1];
    d[(i >> 2) * 6 + 2] = s[i * 4 + 5];
    d[(i >> 2) * 6 + 4] = s[i * 4 + 9];
    d[(i >> 2) * 6 + 5] = s[i * 4 + 13];
    d[(i >> 2) * 6 + 0] =
        (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10] + s[i * 4 + 14]) >> 2;
    d[(i >> 2) * 6 + 3] =
        (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11] + s[i * 4 + 15]) >> 2;
  }

  if (i == width - 3) {
    d[(i >> 2) * 6 + 1] = s[i * 4 + 1];
    d[(i >> 2) * 6 + 2] = s[i * 4 + 5];
    d[(i >> 2) * 6 + 4] = s[i * 4 + 9];
    d[(i >> 2) * 6 + 0] = (s[i * 4 + 2] + s[i * 4 + 6] + s[i * 4 + 10]) / 3;
    d[(i >> 2) * 6 + 3] = (s[i * 4 + 3] + s[i * 4 + 7] + s[i * 4 + 11]) / 3;
  } else if (i == width - 2) {
    d[(i >> 2) * 6 + 1] = s[i * 4 + 1];
    d[(i >> 2) * 6 + 2] = s[i * 4 + 5];
    d[(i >> 2) * 6 + 0] = (s[i * 4 + 2] + s[i * 4 + 6]) >> 1;
    d[(i >> 2) * 6 + 3] = (s[i * 4 + 3] + s[i * 4 + 7]) >> 1;
  } else if (i == width - 1) {
    d[(i >> 2) * 6 + 1] = s[i * 4 + 1];
    d[(i >> 2) * 6 + 0] = s[i * 4 + 2];
    d[(i >> 2) * 6 + 3] = s[i * 4 + 3];
  }
}

#define PACK_ARGB64 GST_VIDEO_FORMAT_ARGB64, unpack_copy8, 1, pack_copy8
#define PACK_AYUV64 GST_VIDEO_FORMAT_AYUV64, unpack_copy8, 1, pack_copy8
static void
unpack_copy8 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  memcpy (dest, GET_LINE (y), width * 8);
}

static void
pack_copy8 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  memcpy (GET_LINE (y), src, width * 8);
}

#define PACK_r210 GST_VIDEO_FORMAT_ARGB64, unpack_r210, 1, pack_r210
static void
unpack_r210 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  const guint8 *s = GET_LINE (y);
  guint16 *d = dest, R, G, B;

  for (i = 0; i < width; i++) {
    guint32 x = GST_READ_UINT32_BE (s + i * 4);

    R = ((x >> 14) & 0xffc0);
    G = ((x >> 4) & 0xffc0);
    B = ((x << 6) & 0xffc0);

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      R |= (R >> 10);
      G |= (G >> 10);
      B |= (B >> 10);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = R;
    d[i * 4 + 2] = G;
    d[i * 4 + 3] = B;
  }
}

static void
pack_r210 (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint8 *d = GET_LINE (y);
  const guint16 *s = src;

  for (i = 0; i < width; i++) {
    guint32 x = 0;
    x |= (s[i * 4 + 1] & 0xffc0) << 14;
    x |= (s[i * 4 + 2] & 0xffc0) << 4;
    x |= (s[i * 4 + 3] & 0xffc0) >> 6;
    GST_WRITE_UINT32_BE (d + i * 4, x);
  }
}

#define PACK_I420_10LE GST_VIDEO_FORMAT_AYUV64, unpack_I420_10LE, 1, pack_I420_10LE
static void
unpack_I420_10LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  guint16 *srcY = GET_Y_LINE (y);
  guint16 *srcU = GET_U_LINE (y >> 1);
  guint16 *srcV = GET_V_LINE (y >> 1);
  guint16 *d = dest, Y, U, V;

  for (i = 0; i < width; i++) {
    Y = GST_READ_UINT16_LE (srcY + i) << 6;
    U = GST_READ_UINT16_LE (srcU + (i >> 1)) << 6;
    V = GST_READ_UINT16_LE (srcV + (i >> 1)) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      Y |= (Y >> 10);
      U |= (U >> 10);
      V |= (V >> 10);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = Y;
    d[i * 4 + 2] = U;
    d[i * 4 + 3] = V;
  }
}

static void
pack_I420_10LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *destY = GET_Y_LINE (y);
  guint16 *destU = GET_U_LINE (y >> 1);
  guint16 *destV = GET_V_LINE (y >> 1);
  guint16 Y0, Y1, U, V;
  const guint16 *s = src;

  for (i = 0; i < width - 1; i += 2) {
    Y0 = (s[i * 4 + 1]) >> 6;
    Y1 = (s[i * 4 + 5]) >> 6;
    U = ((s[i * 4 + 2] + s[i * 4 + 6] + 1) >> 1) >> 6;
    V = ((s[i * 4 + 3] + s[i * 4 + 7] + 1) >> 1) >> 6;

    GST_WRITE_UINT16_LE (destY + i + 0, Y0);
    GST_WRITE_UINT16_LE (destY + i + 1, Y1);
    GST_WRITE_UINT16_LE (destU + (i >> 1), U);
    GST_WRITE_UINT16_LE (destV + (i >> 1), V);
  }
  if (i == width - 1) {
    Y0 = s[i * 4 + 1] >> 6;
    U = s[i * 4 + 2] >> 6;
    V = s[i * 4 + 3] >> 6;

    GST_WRITE_UINT16_LE (destY + i, Y0);
    GST_WRITE_UINT16_LE (destU + (i >> 1), U);
    GST_WRITE_UINT16_LE (destV + (i >> 1), V);
  }
}

#define PACK_I420_10BE GST_VIDEO_FORMAT_AYUV64, unpack_I420_10BE, 1, pack_I420_10BE
static void
unpack_I420_10BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  guint16 *srcY = GET_Y_LINE (y);
  guint16 *srcU = GET_U_LINE (y >> 1);
  guint16 *srcV = GET_V_LINE (y >> 1);
  guint16 *d = dest, Y, U, V;

  for (i = 0; i < width; i++) {
    Y = GST_READ_UINT16_BE (srcY + i) << 6;
    U = GST_READ_UINT16_BE (srcU + (i >> 1)) << 6;
    V = GST_READ_UINT16_BE (srcV + (i >> 1)) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      Y |= (Y >> 10);
      U |= (U >> 10);
      V |= (V >> 10);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = Y;
    d[i * 4 + 2] = U;
    d[i * 4 + 3] = V;
  }
}

static void
pack_I420_10BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *destY = GET_Y_LINE (y);
  guint16 *destU = GET_U_LINE (y >> 1);
  guint16 *destV = GET_V_LINE (y >> 1);
  guint16 Y0, Y1, U, V;
  const guint16 *s = src;

  for (i = 0; i < width - 1; i += 2) {
    Y0 = s[i * 4 + 1] >> 6;
    Y1 = s[i * 4 + 5] >> 6;
    U = ((s[i * 4 + 2] + s[i * 4 + 6] + 1) >> 1) >> 6;
    V = ((s[i * 4 + 3] + s[i * 4 + 7] + 1) >> 1) >> 6;

    GST_WRITE_UINT16_BE (destY + i + 0, Y0);
    GST_WRITE_UINT16_BE (destY + i + 1, Y1);
    GST_WRITE_UINT16_BE (destU + (i >> 1), U);
    GST_WRITE_UINT16_BE (destV + (i >> 1), V);
  }
  if (i == width - 1) {
    Y0 = s[i * 4 + 1] >> 6;
    U = s[i * 4 + 2] >> 6;
    V = s[i * 4 + 3] >> 6;

    GST_WRITE_UINT16_BE (destY + i, Y0);
    GST_WRITE_UINT16_BE (destU + (i >> 1), U);
    GST_WRITE_UINT16_BE (destV + (i >> 1), V);
  }
}

#define PACK_I422_10LE GST_VIDEO_FORMAT_AYUV64, unpack_I422_10LE, 1, pack_I422_10LE
static void
unpack_I422_10LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  guint16 *srcY = GET_Y_LINE (y);
  guint16 *srcU = GET_U_LINE (y);
  guint16 *srcV = GET_V_LINE (y);
  guint16 *d = dest, Y, U, V;

  for (i = 0; i < width; i++) {
    Y = GST_READ_UINT16_LE (srcY + i) << 6;
    U = GST_READ_UINT16_LE (srcU + (i >> 1)) << 6;
    V = GST_READ_UINT16_LE (srcV + (i >> 1)) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      Y |= (Y >> 10);
      U |= (U >> 10);
      V |= (V >> 10);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = Y;
    d[i * 4 + 2] = U;
    d[i * 4 + 3] = V;
  }
}

static void
pack_I422_10LE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *destY = GET_Y_LINE (y);
  guint16 *destU = GET_U_LINE (y);
  guint16 *destV = GET_V_LINE (y);
  guint16 Y0, Y1, U, V;
  const guint16 *s = src;

  for (i = 0; i < width - 1; i += 2) {
    Y0 = (s[i * 4 + 1]) >> 6;
    Y1 = (s[i * 4 + 5]) >> 6;
    U = ((s[i * 4 + 2] + s[i * 4 + 6] + 1) >> 1) >> 6;
    V = ((s[i * 4 + 3] + s[i * 4 + 7] + 1) >> 1) >> 6;

    GST_WRITE_UINT16_LE (destY + i + 0, Y0);
    GST_WRITE_UINT16_LE (destY + i + 1, Y1);
    GST_WRITE_UINT16_LE (destU + (i >> 1), U);
    GST_WRITE_UINT16_LE (destV + (i >> 1), V);
  }
  if (i == width - 1) {
    Y0 = s[i * 4 + 1] >> 6;
    U = s[i * 4 + 2] >> 6;
    V = s[i * 4 + 3] >> 6;

    GST_WRITE_UINT16_LE (destY + i, Y0);
    GST_WRITE_UINT16_LE (destU + (i >> 1), U);
    GST_WRITE_UINT16_LE (destV + (i >> 1), V);
  }
}

#define PACK_I422_10BE GST_VIDEO_FORMAT_AYUV64, unpack_I422_10BE, 1, pack_I422_10BE
static void
unpack_I422_10BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    gpointer dest, const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  int i;
  guint16 *srcY = GET_Y_LINE (y);
  guint16 *srcU = GET_U_LINE (y);
  guint16 *srcV = GET_V_LINE (y);
  guint16 *d = dest, Y, U, V;

  for (i = 0; i < width; i++) {
    Y = GST_READ_UINT16_BE (srcY + i) << 6;
    U = GST_READ_UINT16_BE (srcU + (i >> 1)) << 6;
    V = GST_READ_UINT16_BE (srcV + (i >> 1)) << 6;

    if (!(flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE)) {
      Y |= (Y >> 10);
      U |= (U >> 10);
      V |= (V >> 10);
    }

    d[i * 4 + 0] = 0xffff;
    d[i * 4 + 1] = Y;
    d[i * 4 + 2] = U;
    d[i * 4 + 3] = V;
  }
}

static void
pack_I422_10BE (const GstVideoFormatInfo * info, GstVideoPackFlags flags,
    const gpointer src, gint sstride, gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], GstVideoChromaSite chroma_site,
    gint y, gint width)
{
  int i;
  guint16 *destY = GET_Y_LINE (y);
  guint16 *destU = GET_U_LINE (y);
  guint16 *destV = GET_V_LINE (y);
  guint16 Y0, Y1, U, V;
  const guint16 *s = src;

  for (i = 0; i < width - 1; i += 2) {
    Y0 = s[i * 4 + 1] >> 6;
    Y1 = s[i * 4 + 5] >> 6;
    U = ((s[i * 4 + 2] + s[i * 4 + 6] + 1) >> 1) >> 6;
    V = ((s[i * 4 + 3] + s[i * 4 + 7] + 1) >> 1) >> 6;

    GST_WRITE_UINT16_BE (destY + i + 0, Y0);
    GST_WRITE_UINT16_BE (destY + i + 1, Y1);
    GST_WRITE_UINT16_BE (destU + (i >> 1), U);
    GST_WRITE_UINT16_BE (destV + (i >> 1), V);
  }
  if (i == width - 1) {
    Y0 = s[i * 4 + 1] >> 6;
    U = s[i * 4 + 2] >> 6;
    V = s[i * 4 + 3] >> 6;

    GST_WRITE_UINT16_BE (destY + i, Y0);
    GST_WRITE_UINT16_BE (destU + (i >> 1), U);
    GST_WRITE_UINT16_BE (destV + (i >> 1), V);
  }
}

typedef struct
{
  guint32 fourcc;
  GstVideoFormatInfo info;
} VideoFormat;

/* depths: bits, n_components, shift, depth */
#define DPTH0            0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
#define DPTH8            8, 1, { 0, 0, 0, 0 }, { 8, 0, 0, 0 }
#define DPTH8_32         8, 2, { 0, 0, 0, 0 }, { 8, 32, 0, 0 }
#define DPTH888          8, 3, { 0, 0, 0, 0 }, { 8, 8, 8, 0 }
#define DPTH8888         8, 4, { 0, 0, 0, 0 }, { 8, 8, 8, 8 }
#define DPTH10_10_10     10, 3, { 0, 0, 0, 0 }, { 10, 10, 10, 0 }
#define DPTH16           16, 1, { 0, 0, 0, 0 }, { 16, 0, 0, 0 }
#define DPTH16_16_16     16, 3, { 0, 0, 0, 0 }, { 16, 16, 16, 0 }
#define DPTH16_16_16_16  16, 4, { 0, 0, 0, 0 }, { 16, 16, 16, 16 }
#define DPTH555          16, 3, { 10, 5, 0, 0 }, { 5, 5, 5, 0 }
#define DPTH565          16, 3, { 11, 5, 0, 0 }, { 5, 6, 5, 0 }

/* pixel strides */
#define PSTR0             { 0, 0, 0, 0 }
#define PSTR1             { 1, 0, 0, 0 }
#define PSTR14            { 1, 4, 0, 0 }
#define PSTR111           { 1, 1, 1, 0 }
#define PSTR1111          { 1, 1, 1, 1 }
#define PSTR122           { 1, 2, 2, 0 }
#define PSTR2             { 2, 0, 0, 0 }
#define PSTR222           { 2, 2, 2, 0 }
#define PSTR244           { 2, 4, 4, 0 }
#define PSTR444           { 4, 4, 4, 0 }
#define PSTR4444          { 4, 4, 4, 4 }
#define PSTR333           { 3, 3, 3, 0 }
#define PSTR488           { 4, 8, 8, 0 }
#define PSTR8888          { 8, 8, 8, 8 }

/* planes */
#define PLANE_NA          0, { 0, 0, 0, 0 }
#define PLANE0            1, { 0, 0, 0, 0 }
#define PLANE01           2, { 0, 1, 0, 0 }
#define PLANE011          2, { 0, 1, 1, 0 }
#define PLANE012          3, { 0, 1, 2, 0 }
#define PLANE0123         4, { 0, 1, 2, 3 }
#define PLANE021          3, { 0, 2, 1, 0 }

/* offsets */
#define OFFS0             { 0, 0, 0, 0 }
#define OFFS013           { 0, 1, 3, 0 }
#define OFFS102           { 1, 0, 2, 0 }
#define OFFS1230          { 1, 2, 3, 0 }
#define OFFS012           { 0, 1, 2, 0 }
#define OFFS210           { 2, 1, 0, 0 }
#define OFFS123           { 1, 2, 3, 0 }
#define OFFS321           { 3, 2, 1, 0 }
#define OFFS0123          { 0, 1, 2, 3 }
#define OFFS2103          { 2, 1, 0, 3 }
#define OFFS3210          { 3, 2, 1, 0 }
#define OFFS031           { 0, 3, 1, 0 }
#define OFFS204           { 2, 0, 4, 0 }
#define OFFS001           { 0, 0, 1, 0 }
#define OFFS010           { 0, 1, 0, 0 }
#define OFFS104           { 1, 0, 4, 0 }
#define OFFS2460          { 2, 4, 6, 0 }

/* subsampling */
#define SUB410            { 0, 2, 2, 0 }, { 0, 2, 2, 0 }
#define SUB411            { 0, 2, 2, 0 }, { 0, 0, 0, 0 }
#define SUB420            { 0, 1, 1, 0 }, { 0, 1, 1, 0 }
#define SUB422            { 0, 1, 1, 0 }, { 0, 0, 0, 0 }
#define SUB4              { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
#define SUB44             { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
#define SUB444            { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
#define SUB4444           { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
#define SUB4204           { 0, 1, 1, 0 }, { 0, 1, 1, 0 }

#define MAKE_YUV_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack ) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV, depth, pstride, plane, offs, sub, pack } }
#define MAKE_YUV_LE_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack ) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_LE, depth, pstride, plane, offs, sub, pack } }
#define MAKE_YUVA_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_ALPHA, depth, pstride, plane, offs, sub, pack } }
#define MAKE_YUVA_PACK_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_ALPHA | GST_VIDEO_FORMAT_FLAG_UNPACK, depth, pstride, plane, offs, sub, pack } }
#define MAKE_YUVA_LE_PACK_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_ALPHA | GST_VIDEO_FORMAT_FLAG_UNPACK | GST_VIDEO_FORMAT_FLAG_LE, depth, pstride, plane, offs, sub, pack } }
#define MAKE_YUV_C_FORMAT(name, desc, fourcc, depth, pstride, plane, offs, sub, pack) \
 { fourcc, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_YUV | GST_VIDEO_FORMAT_FLAG_COMPLEX, depth, pstride, plane, offs, sub, pack } }

#define MAKE_RGB_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB, depth, pstride, plane, offs, sub, pack } }
#define MAKE_RGB_LE_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_LE, depth, pstride, plane, offs, sub, pack } }
#define MAKE_RGBA_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_ALPHA, depth, pstride, plane, offs, sub, pack } }
#define MAKE_RGBAP_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_ALPHA | GST_VIDEO_FORMAT_FLAG_PALETTE, depth, pstride, plane, offs, sub, pack } }
#define MAKE_RGBA_PACK_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_ALPHA | GST_VIDEO_FORMAT_FLAG_UNPACK, depth, pstride, plane, offs, sub, pack } }
#define MAKE_RGBA_LE_PACK_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_ALPHA | GST_VIDEO_FORMAT_FLAG_UNPACK | GST_VIDEO_FORMAT_FLAG_LE, depth, pstride, plane, offs, sub, pack } }

#define MAKE_GRAY_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_GRAY, depth, pstride, plane, offs, sub, pack } }
#define MAKE_GRAY_LE_FORMAT(name, desc, depth, pstride, plane, offs, sub, pack) \
 { 0x00000000, {GST_VIDEO_FORMAT_ ##name, G_STRINGIFY(name), desc, GST_VIDEO_FORMAT_FLAG_GRAY | GST_VIDEO_FORMAT_FLAG_LE, depth, pstride, plane, offs, sub, pack } }

static VideoFormat formats[] = {
  {0x00000000, {GST_VIDEO_FORMAT_UNKNOWN, "UNKNOWN", "unknown video", 0, DPTH0,
          PSTR0, PLANE_NA, OFFS0}},
  {0x00000000, {GST_VIDEO_FORMAT_ENCODED, "ENCODED", "encoded video",
          GST_VIDEO_FORMAT_FLAG_COMPLEX, DPTH0, PSTR0, PLANE_NA, OFFS0}},

  MAKE_YUV_FORMAT (I420, "raw video", GST_MAKE_FOURCC ('I', '4', '2', '0'),
      DPTH888, PSTR111, PLANE012, OFFS0, SUB420, PACK_420),
  MAKE_YUV_FORMAT (YV12, "raw video", GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
      DPTH888, PSTR111, PLANE021, OFFS0, SUB420, PACK_420),
  MAKE_YUV_FORMAT (YUY2, "raw video", GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
      DPTH888, PSTR244, PLANE0, OFFS013, SUB422, PACK_YUY2),
  MAKE_YUV_FORMAT (UYVY, "raw video", GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'),
      DPTH888, PSTR244, PLANE0, OFFS102, SUB422, PACK_UYVY),
  MAKE_YUVA_PACK_FORMAT (AYUV, "raw video", GST_MAKE_FOURCC ('A', 'Y', 'U',
          'V'), DPTH8888, PSTR4444, PLANE0, OFFS1230, SUB4444, PACK_AYUV),
  MAKE_RGB_FORMAT (RGBx, "raw video", DPTH888, PSTR444, PLANE0, OFFS012,
      SUB444, PACK_RGBA),
  MAKE_RGB_FORMAT (BGRx, "raw video", DPTH888, PSTR444, PLANE0, OFFS210,
      SUB444, PACK_BGRA),
  MAKE_RGB_FORMAT (xRGB, "raw video", DPTH888, PSTR444, PLANE0, OFFS123,
      SUB444, PACK_ARGB),
  MAKE_RGB_FORMAT (xBGR, "raw video", DPTH888, PSTR444, PLANE0, OFFS321,
      SUB444, PACK_ABGR),
  MAKE_RGBA_FORMAT (RGBA, "raw video", DPTH8888, PSTR4444, PLANE0, OFFS0123,
      SUB4444, PACK_RGBA),
  MAKE_RGBA_FORMAT (BGRA, "raw video", DPTH8888, PSTR4444, PLANE0, OFFS2103,
      SUB4444, PACK_BGRA),
  MAKE_RGBA_PACK_FORMAT (ARGB, "raw video", DPTH8888, PSTR4444, PLANE0,
      OFFS1230, SUB4444, PACK_ARGB),
  MAKE_RGBA_FORMAT (ABGR, "raw video", DPTH8888, PSTR4444, PLANE0, OFFS3210,
      SUB4444, PACK_ABGR),
  MAKE_RGB_FORMAT (RGB, "raw video", DPTH888, PSTR333, PLANE0, OFFS012, SUB444,
      PACK_RGB),
  MAKE_RGB_FORMAT (BGR, "raw video", DPTH888, PSTR333, PLANE0, OFFS210, SUB444,
      PACK_BGR),

  MAKE_YUV_FORMAT (Y41B, "raw video", GST_MAKE_FOURCC ('Y', '4', '1', 'B'),
      DPTH888, PSTR111, PLANE012, OFFS0, SUB411, PACK_Y41B),
  MAKE_YUV_FORMAT (Y42B, "raw video", GST_MAKE_FOURCC ('Y', '4', '2', 'B'),
      DPTH888, PSTR111, PLANE012, OFFS0, SUB422, PACK_Y42B),
  MAKE_YUV_FORMAT (YVYU, "raw video", GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'),
      DPTH888, PSTR244, PLANE0, OFFS031, SUB422, PACK_YVYU),
  MAKE_YUV_FORMAT (Y444, "raw video", GST_MAKE_FOURCC ('Y', '4', '4', '4'),
      DPTH888, PSTR111, PLANE012, OFFS0, SUB444, PACK_Y444),
  MAKE_YUV_C_FORMAT (v210, "raw video", GST_MAKE_FOURCC ('v', '2', '1', '0'),
      DPTH10_10_10, PSTR0, PLANE0, OFFS0, SUB422, PACK_v210),
  MAKE_YUV_FORMAT (v216, "raw video", GST_MAKE_FOURCC ('v', '2', '1', '6'),
      DPTH16_16_16, PSTR488, PLANE0, OFFS204, SUB422, PACK_v216),
  MAKE_YUV_FORMAT (NV12, "raw video", GST_MAKE_FOURCC ('N', 'V', '1', '2'),
      DPTH888, PSTR122, PLANE011, OFFS001, SUB420, PACK_NV12),
  MAKE_YUV_FORMAT (NV21, "raw video", GST_MAKE_FOURCC ('N', 'V', '2', '1'),
      DPTH888, PSTR122, PLANE011, OFFS010, SUB420, PACK_NV21),

  MAKE_GRAY_FORMAT (GRAY8, "raw video", DPTH8, PSTR1, PLANE0, OFFS0, SUB4,
      PACK_GRAY8),
  MAKE_GRAY_FORMAT (GRAY16_BE, "raw video", DPTH16, PSTR2, PLANE0, OFFS0, SUB4,
      PACK_GRAY16_BE),
  MAKE_GRAY_LE_FORMAT (GRAY16_LE, "raw video", DPTH16, PSTR2, PLANE0, OFFS0,
      SUB4, PACK_GRAY16_LE),

  MAKE_YUV_FORMAT (v308, "raw video", GST_MAKE_FOURCC ('v', '3', '0', '8'),
      DPTH888, PSTR333, PLANE0, OFFS012, SUB444, PACK_v308),

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  MAKE_RGB_LE_FORMAT (RGB16, "raw video", DPTH565, PSTR222, PLANE0, OFFS0,
      SUB444, PACK_RGB16),
  MAKE_RGB_LE_FORMAT (BGR16, "raw video", DPTH565, PSTR222, PLANE0, OFFS0,
      SUB444, PACK_BGR16),
  MAKE_RGB_LE_FORMAT (RGB15, "raw video", DPTH555, PSTR222, PLANE0, OFFS0,
      SUB444, PACK_RGB15),
  MAKE_RGB_LE_FORMAT (BGR15, "raw video", DPTH555, PSTR222, PLANE0, OFFS0,
      SUB444, PACK_BGR15),
#else
  MAKE_RGB_FORMAT (RGB16, "raw video", DPTH565, PSTR222, PLANE0, OFFS0, SUB444,
      PACK_RGB16),
  MAKE_RGB_FORMAT (BGR16, "raw video", DPTH565, PSTR222, PLANE0, OFFS0, SUB444,
      PACK_BGR16),
  MAKE_RGB_FORMAT (RGB15, "raw video", DPTH555, PSTR222, PLANE0, OFFS0, SUB444,
      PACK_RGB15),
  MAKE_RGB_FORMAT (BGR15, "raw video", DPTH555, PSTR222, PLANE0, OFFS0, SUB444,
      PACK_BGR15),
#endif

  MAKE_YUV_C_FORMAT (UYVP, "raw video", GST_MAKE_FOURCC ('U', 'Y', 'V', 'P'),
      DPTH10_10_10, PSTR0, PLANE0, OFFS0, SUB422, PACK_UYVP),
  MAKE_YUVA_FORMAT (A420, "raw video", GST_MAKE_FOURCC ('A', '4', '2', '0'),
      DPTH8888, PSTR1111, PLANE0123, OFFS0, SUB4204, PACK_A420),
  MAKE_RGBAP_FORMAT (RGB8P, "raw video", DPTH8_32, PSTR14, PLANE01,
      OFFS0, SUB44, PACK_RGB8P),
  MAKE_YUV_FORMAT (YUV9, "raw video", GST_MAKE_FOURCC ('Y', 'U', 'V', '9'),
      DPTH888, PSTR111, PLANE012, OFFS0, SUB410, PACK_410),
  MAKE_YUV_FORMAT (YVU9, "raw video", GST_MAKE_FOURCC ('Y', 'V', 'U', '9'),
      DPTH888, PSTR111, PLANE021, OFFS0, SUB410, PACK_410),
  MAKE_YUV_FORMAT (IYU1, "raw video", GST_MAKE_FOURCC ('I', 'Y', 'U', '1'),
      DPTH888, PSTR0, PLANE0, OFFS104, SUB411, PACK_IYU1),
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  MAKE_RGBA_LE_PACK_FORMAT (ARGB64, "raw video", DPTH16_16_16_16, PSTR8888,
      PLANE0,
      OFFS2460, SUB444, PACK_ARGB64),
  MAKE_YUVA_LE_PACK_FORMAT (AYUV64, "raw video", 0x00000000, DPTH16_16_16_16,
      PSTR8888, PLANE0, OFFS2460, SUB444, PACK_AYUV64),
#else
  MAKE_RGBA_PACK_FORMAT (ARGB64, "raw video", DPTH16_16_16_16, PSTR8888, PLANE0,
      OFFS2460, SUB444, PACK_ARGB64),
  MAKE_YUVA_PACK_FORMAT (AYUV64, "raw video", 0x00000000, DPTH16_16_16_16,
      PSTR8888, PLANE0, OFFS2460, SUB444, PACK_AYUV64),
#endif
  MAKE_RGB_FORMAT (r210, "raw video", DPTH10_10_10, PSTR444, PLANE0, OFFS0,
      SUB444, PACK_r210),
  MAKE_YUV_FORMAT (I420_10BE, "raw video", 0x00000000, DPTH10_10_10,
      PSTR222, PLANE012, OFFS0, SUB420, PACK_I420_10BE),
  MAKE_YUV_LE_FORMAT (I420_10LE, "raw video", 0x00000000, DPTH10_10_10,
      PSTR222, PLANE012, OFFS0, SUB420, PACK_I420_10LE),
  MAKE_YUV_FORMAT (I422_10BE, "raw video", 0x00000000, DPTH10_10_10,
      PSTR222, PLANE012, OFFS0, SUB422, PACK_I422_10BE),
  MAKE_YUV_LE_FORMAT (I422_10LE, "raw video", 0x00000000, DPTH10_10_10,
      PSTR222, PLANE012, OFFS0, SUB422, PACK_I422_10LE),
};

static GstVideoFormat
gst_video_format_from_rgb32_masks (int red_mask, int green_mask, int blue_mask)
{
  if (red_mask == 0xff000000 && green_mask == 0x00ff0000 &&
      blue_mask == 0x0000ff00) {
    return GST_VIDEO_FORMAT_RGBx;
  }
  if (red_mask == 0x0000ff00 && green_mask == 0x00ff0000 &&
      blue_mask == 0xff000000) {
    return GST_VIDEO_FORMAT_BGRx;
  }
  if (red_mask == 0x00ff0000 && green_mask == 0x0000ff00 &&
      blue_mask == 0x000000ff) {
    return GST_VIDEO_FORMAT_xRGB;
  }
  if (red_mask == 0x000000ff && green_mask == 0x0000ff00 &&
      blue_mask == 0x00ff0000) {
    return GST_VIDEO_FORMAT_xBGR;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

static GstVideoFormat
gst_video_format_from_rgba32_masks (int red_mask, int green_mask,
    int blue_mask, int alpha_mask)
{
  if (red_mask == 0xff000000 && green_mask == 0x00ff0000 &&
      blue_mask == 0x0000ff00 && alpha_mask == 0x000000ff) {
    return GST_VIDEO_FORMAT_RGBA;
  }
  if (red_mask == 0x0000ff00 && green_mask == 0x00ff0000 &&
      blue_mask == 0xff000000 && alpha_mask == 0x000000ff) {
    return GST_VIDEO_FORMAT_BGRA;
  }
  if (red_mask == 0x00ff0000 && green_mask == 0x0000ff00 &&
      blue_mask == 0x000000ff && alpha_mask == 0xff000000) {
    return GST_VIDEO_FORMAT_ARGB;
  }
  if (red_mask == 0x000000ff && green_mask == 0x0000ff00 &&
      blue_mask == 0x00ff0000 && alpha_mask == 0xff000000) {
    return GST_VIDEO_FORMAT_ABGR;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

static GstVideoFormat
gst_video_format_from_rgb24_masks (int red_mask, int green_mask, int blue_mask)
{
  if (red_mask == 0xff0000 && green_mask == 0x00ff00 && blue_mask == 0x0000ff) {
    return GST_VIDEO_FORMAT_RGB;
  }
  if (red_mask == 0x0000ff && green_mask == 0x00ff00 && blue_mask == 0xff0000) {
    return GST_VIDEO_FORMAT_BGR;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

#define GST_VIDEO_COMP1_MASK_16_INT 0xf800
#define GST_VIDEO_COMP2_MASK_16_INT 0x07e0
#define GST_VIDEO_COMP3_MASK_16_INT 0x001f

#define GST_VIDEO_COMP1_MASK_15_INT 0x7c00
#define GST_VIDEO_COMP2_MASK_15_INT 0x03e0
#define GST_VIDEO_COMP3_MASK_15_INT 0x001f

static GstVideoFormat
gst_video_format_from_rgb16_masks (int red_mask, int green_mask, int blue_mask)
{
  if (red_mask == GST_VIDEO_COMP1_MASK_16_INT
      && green_mask == GST_VIDEO_COMP2_MASK_16_INT
      && blue_mask == GST_VIDEO_COMP3_MASK_16_INT) {
    return GST_VIDEO_FORMAT_RGB16;
  }
  if (red_mask == GST_VIDEO_COMP3_MASK_16_INT
      && green_mask == GST_VIDEO_COMP2_MASK_16_INT
      && blue_mask == GST_VIDEO_COMP1_MASK_16_INT) {
    return GST_VIDEO_FORMAT_BGR16;
  }
  if (red_mask == GST_VIDEO_COMP1_MASK_15_INT
      && green_mask == GST_VIDEO_COMP2_MASK_15_INT
      && blue_mask == GST_VIDEO_COMP3_MASK_15_INT) {
    return GST_VIDEO_FORMAT_RGB15;
  }
  if (red_mask == GST_VIDEO_COMP3_MASK_15_INT
      && green_mask == GST_VIDEO_COMP2_MASK_15_INT
      && blue_mask == GST_VIDEO_COMP1_MASK_15_INT) {
    return GST_VIDEO_FORMAT_BGR15;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_video_format_from_masks:
 * @depth: the amount of bits used for a pixel
 * @bpp: the amount of bits used to store a pixel. This value is bigger than
 *   @depth
 * @endianness: the endianness of the masks
 * @red_mask: the red mask
 * @green_mask: the green mask
 * @blue_mask: the blue mask
 * @alpha_mask: the optional alpha mask
 *
 * Find the #GstVideoFormat for the given parameters.
 *
 * Returns: a #GstVideoFormat or GST_VIDEO_FORMAT_UNKNOWN when the parameters to
 * not specify a known format.
 */
GstVideoFormat
gst_video_format_from_masks (gint depth, gint bpp, gint endianness,
    gint red_mask, gint green_mask, gint blue_mask, gint alpha_mask)
{
  GstVideoFormat format;

  /* our caps system handles 24/32bpp RGB as big-endian. */
  if ((bpp == 24 || bpp == 32) && endianness == G_LITTLE_ENDIAN) {
    red_mask = GUINT32_TO_BE (red_mask);
    green_mask = GUINT32_TO_BE (green_mask);
    blue_mask = GUINT32_TO_BE (blue_mask);
    endianness = G_BIG_ENDIAN;
    if (bpp == 24) {
      red_mask >>= 8;
      green_mask >>= 8;
      blue_mask >>= 8;
    }
  }

  if (depth == 30 && bpp == 32) {
    format = GST_VIDEO_FORMAT_r210;
  } else if (depth == 24 && bpp == 32) {
    format = gst_video_format_from_rgb32_masks (red_mask, green_mask,
        blue_mask);
  } else if (depth == 32 && bpp == 32 && alpha_mask) {
    format = gst_video_format_from_rgba32_masks (red_mask, green_mask,
        blue_mask, alpha_mask);
  } else if (depth == 24 && bpp == 24) {
    format = gst_video_format_from_rgb24_masks (red_mask, green_mask,
        blue_mask);
  } else if ((depth == 15 || depth == 16) && bpp == 16 &&
      endianness == G_BYTE_ORDER) {
    format = gst_video_format_from_rgb16_masks (red_mask, green_mask,
        blue_mask);
  } else if (depth == 8 && bpp == 8) {
    format = GST_VIDEO_FORMAT_RGB8P;
  } else if (depth == 64 && bpp == 64) {
    format = gst_video_format_from_rgba32_masks (red_mask, green_mask,
        blue_mask, alpha_mask);
    if (format == GST_VIDEO_FORMAT_ARGB) {
      format = GST_VIDEO_FORMAT_ARGB64;
    } else {
      format = GST_VIDEO_FORMAT_UNKNOWN;
    }
  } else {
    format = GST_VIDEO_FORMAT_UNKNOWN;
  }
  return format;
}

/**
 * gst_video_format_from_fourcc:
 * @fourcc: a FOURCC value representing raw YUV video
 *
 * Converts a FOURCC value into the corresponding #GstVideoFormat.
 * If the FOURCC cannot be represented by #GstVideoFormat,
 * #GST_VIDEO_FORMAT_UNKNOWN is returned.
 *
 * Returns: the #GstVideoFormat describing the FOURCC value
 */
GstVideoFormat
gst_video_format_from_fourcc (guint32 fourcc)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return GST_VIDEO_FORMAT_I420;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      return GST_VIDEO_FORMAT_YV12;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      return GST_VIDEO_FORMAT_YUY2;
    case GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'):
      return GST_VIDEO_FORMAT_YVYU;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      return GST_VIDEO_FORMAT_UYVY;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      return GST_VIDEO_FORMAT_AYUV;
    case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
      return GST_VIDEO_FORMAT_Y41B;
    case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
      return GST_VIDEO_FORMAT_Y42B;
    case GST_MAKE_FOURCC ('Y', '4', '4', '4'):
      return GST_VIDEO_FORMAT_Y444;
    case GST_MAKE_FOURCC ('v', '2', '1', '0'):
      return GST_VIDEO_FORMAT_v210;
    case GST_MAKE_FOURCC ('v', '2', '1', '6'):
      return GST_VIDEO_FORMAT_v216;
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
      return GST_VIDEO_FORMAT_NV12;
    case GST_MAKE_FOURCC ('N', 'V', '2', '1'):
      return GST_VIDEO_FORMAT_NV21;
    case GST_MAKE_FOURCC ('v', '3', '0', '8'):
      return GST_VIDEO_FORMAT_v308;
    case GST_MAKE_FOURCC ('Y', '8', '0', '0'):
    case GST_MAKE_FOURCC ('Y', '8', ' ', ' '):
    case GST_MAKE_FOURCC ('G', 'R', 'E', 'Y'):
      return GST_VIDEO_FORMAT_GRAY8;
    case GST_MAKE_FOURCC ('Y', '1', '6', ' '):
      return GST_VIDEO_FORMAT_GRAY16_LE;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'P'):
      return GST_VIDEO_FORMAT_UYVP;
    case GST_MAKE_FOURCC ('A', '4', '2', '0'):
      return GST_VIDEO_FORMAT_A420;
    case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
      return GST_VIDEO_FORMAT_YUV9;
    case GST_MAKE_FOURCC ('Y', 'V', 'U', '9'):
      return GST_VIDEO_FORMAT_YVU9;
    case GST_MAKE_FOURCC ('I', 'Y', 'U', '1'):
      return GST_VIDEO_FORMAT_IYU1;
    case GST_MAKE_FOURCC ('A', 'Y', '6', '4'):
      return GST_VIDEO_FORMAT_AYUV64;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

/**
 * gst_video_format_from_string:
 * @format: a format string
 *
 * Convert the @format string to its #GstVideoFormat.
 *
 * Returns: the #GstVideoFormat for @format or GST_VIDEO_FORMAT_UNKNOWN when the
 * string is not a known format.
 */
GstVideoFormat
gst_video_format_from_string (const gchar * format)
{
  guint i;

  g_return_val_if_fail (format != NULL, GST_VIDEO_FORMAT_UNKNOWN);

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    if (strcmp (GST_VIDEO_FORMAT_INFO_NAME (&formats[i].info), format) == 0)
      return GST_VIDEO_FORMAT_INFO_FORMAT (&formats[i].info);
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}


/**
 * gst_video_format_to_fourcc:
 * @format: a #GstVideoFormat video format
 *
 * Converts a #GstVideoFormat value into the corresponding FOURCC.  Only
 * a few YUV formats have corresponding FOURCC values.  If @format has
 * no corresponding FOURCC value, 0 is returned.
 *
 * Returns: the FOURCC corresponding to @format
 */
guint32
gst_video_format_to_fourcc (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);

  if (format >= G_N_ELEMENTS (formats))
    return 0;

  return formats[format].fourcc;
}

const gchar *
gst_video_format_to_string (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, NULL);

  if (format >= G_N_ELEMENTS (formats))
    return NULL;

  return GST_VIDEO_FORMAT_INFO_NAME (&formats[format].info);
}

/**
 * gst_video_format_get_info:
 * @format: a #GstVideoFormat
 *
 * Get the #GstVideoFormatInfo for @format
 *
 * Returns: The #GstVideoFormatInfo for @format.
 */
const GstVideoFormatInfo *
gst_video_format_get_info (GstVideoFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (formats), NULL);

  return &formats[format].info;
}

typedef struct
{
  const gchar *name;
  GstVideoChromaSite site;
} ChromaSiteInfo;

static const ChromaSiteInfo chromasite[] = {
  {"jpeg", GST_VIDEO_CHROMA_SITE_JPEG},
  {"mpeg2", GST_VIDEO_CHROMA_SITE_MPEG2},
  {"dv", GST_VIDEO_CHROMA_SITE_DV}
};

/**
 * gst_video_chroma_from_string:
 * @s: a chromasite string
 *
 * Convert @s to a #GstVideoChromaSite
 *
 * Returns: a #GstVideoChromaSite or %GST_VIDEO_CHROMA_SITE_UNKNOWN when @s does
 * not contain a valid chroma description.
 */
GstVideoChromaSite
gst_video_chroma_from_string (const gchar * s)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (chromasite); i++) {
    if (g_str_equal (chromasite[i].name, s))
      return chromasite[i].site;
  }
  return GST_VIDEO_CHROMA_SITE_UNKNOWN;
}

/**
 * gst_video_chroma_to_string:
 * @site: a #GstVideoChromaSite
 *
 * Converts @site to its string representation.
 *
 * Returns: a string describing @site.
 */
const gchar *
gst_video_chroma_to_string (GstVideoChromaSite site)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (chromasite); i++) {
    if (chromasite[i].site == site)
      return chromasite[i].name;
  }
  return NULL;
}
