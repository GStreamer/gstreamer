/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2004> Benjamin Otte <otte@gnome.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoimage.h"
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#endif

#include <string.h>

const GstVideoColor GST_VIDEO_COLOR_WHITE = { 255, 128, 128, 255, 255, 255 };
const GstVideoColor GST_VIDEO_COLOR_YELLOW = { 226, 0, 155, 255, 255, 0 };
const GstVideoColor GST_VIDEO_COLOR_CYAN = { 179, 170, 0, 0, 255, 255 };
const GstVideoColor GST_VIDEO_COLOR_GREEN = { 150, 46, 21, 0, 255, 0 };
const GstVideoColor GST_VIDEO_COLOR_MAGENTA = { 105, 212, 235, 255, 0, 255 };
const GstVideoColor GST_VIDEO_COLOR_RED = { 76, 85, 255, 255, 0, 0 };
const GstVideoColor GST_VIDEO_COLOR_BLUE = { 29, 255, 107, 0, 0, 255 };
const GstVideoColor GST_VIDEO_COLOR_BLACK = { 16, 128, 128, 0, 0, 0 };
const GstVideoColor GST_VIDEO_COLOR_NEG_I = { 16, 198, 21, 0, 0, 128 };
const GstVideoColor GST_VIDEO_COLOR_POS_Q = { 16, 235, 198, 0, 128, 255 };
const GstVideoColor GST_VIDEO_COLOR_SUPER_BLACK = { 0, 128, 128, 0, 0, 0 };
const GstVideoColor GST_VIDEO_COLOR_DARK_GREY = { 32, 128, 128, 32, 32, 32 };

const GstVideoFormat *
gst_video_format_find_by_structure (const GstStructure * structure)
{
  int i;
  const char *media_type = gst_structure_get_name (structure);
  int ret;

  g_return_val_if_fail (structure, NULL);

  if (strcmp (media_type, "video/x-raw-yuv") == 0) {
    char *s;
    int fourcc;
    guint32 format;

    ret = gst_structure_get_fourcc (structure, "format", &format);
    if (!ret)
      return NULL;
    for (i = 0; i < gst_video_format_count; i++) {
      s = gst_video_format_list[i].fourcc;
      //g_print("testing " GST_FOURCC_FORMAT " and %s\n", GST_FOURCC_ARGS(format), s);
      fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
      if (fourcc == format) {
        return gst_video_format_list + i;
      }
    }
  } else if (strcmp (media_type, "video/x-raw-rgb") == 0) {
    int red_mask;
    int green_mask;
    int blue_mask;
    int depth;
    int bpp;

    ret = gst_structure_get_int (structure, "red_mask", &red_mask);
    ret &= gst_structure_get_int (structure, "green_mask", &green_mask);
    ret &= gst_structure_get_int (structure, "blue_mask", &blue_mask);
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "bpp", &bpp);

    for (i = 0; i < gst_video_format_count; i++) {
      if (strcmp (gst_video_format_list[i].fourcc, "RGB ") == 0 &&
          gst_video_format_list[i].red_mask == red_mask &&
          gst_video_format_list[i].green_mask == green_mask &&
          gst_video_format_list[i].blue_mask == blue_mask &&
          gst_video_format_list[i].depth == depth &&
          gst_video_format_list[i].bitspp == bpp) {
        return gst_video_format_list + i;

      }
    }
    return NULL;
  }

  g_critical ("format not found for media type %s", media_type);

  return NULL;
}

const GstVideoFormat *
gst_video_format_find_by_fourcc (int find_fourcc)
{
  int i;

  for (i = 0; i < gst_video_format_count; i++) {
    char *s;
    int fourcc;

    s = gst_video_format_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    if (find_fourcc == fourcc) {
      /* If YUV format, it's good */
      if (!gst_video_format_list[i].ext_caps) {
        return gst_video_format_list + i;
      }

      return gst_video_format_list + i;
    }
  }
  return NULL;
}

const GstVideoFormat *
gst_video_format_find_by_name (const char *name)
{
  int i;

  for (i = 0; i < gst_video_format_count; i++) {
    if (strcmp (name, gst_video_format_list[i].name) == 0) {
      return gst_video_format_list + i;
    }
  }
  return NULL;
}


GstStructure *
gst_video_format_get_structure (const GstVideoFormat * format)
{
  unsigned int fourcc;

  g_return_val_if_fail (format, NULL);

  fourcc =
      GST_MAKE_FOURCC (format->fourcc[0], format->fourcc[1], format->fourcc[2],
      format->fourcc[3]);

  if (format->ext_caps) {
    int endianness;

    if (format->bitspp == 16) {
      endianness = G_BYTE_ORDER;
    } else {
      endianness = G_BIG_ENDIAN;
    }
    return gst_structure_new ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, format->bitspp,
        "endianness", G_TYPE_INT, endianness,
        "depth", G_TYPE_INT, format->depth,
        "red_mask", G_TYPE_INT, format->red_mask,
        "green_mask", G_TYPE_INT, format->green_mask,
        "blue_mask", G_TYPE_INT, format->blue_mask, NULL);
  } else {
    return gst_structure_new ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  }
}

/* returns the size in bytes for one video frame of the given dimensions
 * given the fourcc in GstVideotestsrc */
guint
gst_video_format_get_size (const GstVideoFormat * format, guint w, guint h)
{
  GstVideoImage p = { 0 };

  g_return_val_if_fail (format != NULL, 0);
  g_return_val_if_fail (w > 0, 0);
  g_return_val_if_fail (h > 0, 0);

  gst_video_image_setup (&p, format, NULL, w, h);

  return (unsigned long) p.endptr;
}

void
gst_video_image_setup (GstVideoImage * image, const GstVideoFormat * format,
    guint8 * data, guint w, guint h)
{
  g_return_if_fail (image != NULL);
  g_return_if_fail (format != NULL);
  g_return_if_fail (w > 0);
  g_return_if_fail (h > 0);

  image->width = w;
  image->height = h;
  image->format = format;
  format->paint_setup (image, data);
}

void
gst_video_image_paint_hline (GstVideoImage * image, gint x, gint y, gint w,
    const GstVideoColor * c)
{
  g_return_if_fail (image != NULL);
  g_return_if_fail (c != NULL);
  g_return_if_fail (w > 0);

  /* check coords */
  if (y < 0 || y >= image->height)
    return;
  if (x < 0) {
    if (x + w < 0)
      return;
    w += x;
    x = 0;
  }
  if (x >= image->width)
    return;
  if (x + w > image->width) {
    w = image->width - x;
  }
  image->format->paint_hline (image, x, y, w, c);
}

void
gst_video_image_draw_rectangle (GstVideoImage * image, gint x, gint y,
    gint w, gint h, const GstVideoColor * c, gboolean filled)
{
  gint i;

  g_return_if_fail (image != NULL);
  g_return_if_fail (c != NULL);
  g_return_if_fail (w > 0);
  g_return_if_fail (h > 0);

  /* check coords */
  if (x < 0) {
    if (x + w < 0)
      return;
    w += x;
    x = 0;
  }
  if (x >= image->width)
    return;
  if (x + w > image->width) {
    w = image->width - x;
  }
  if (y < 0) {
    if (y + h < 0)
      return;
    h += y;
    y = 0;
  }
  if (y >= image->height)
    return;
  if (y + h > image->height) {
    y = image->height - y;
  }

  if (filled) {
    for (i = 0; i < h; i++) {
      image->format->paint_hline (image, x, y + i, w, c);
    }
  } else {
    h--;
    image->format->paint_hline (image, x, y, w, c);
    for (i = 1; i < h; i++) {
      image->format->paint_hline (image, x, y + i, 1, c);
      image->format->paint_hline (image, x + w - 1, y + i, 1, c);
    }
    image->format->paint_hline (image, x, y + h, w, c);
  }
}

void
gst_video_image_copy_hline (GstVideoImage * dest, gint xdest, gint ydest,
    GstVideoImage * src, gint xsrc, gint ysrc, gint w)
{
  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (dest->format == src->format);
  g_return_if_fail (w > 0);

  /* check width coords */
  if (xdest >= dest->width)
    return;
  if (xsrc >= src->width)
    return;
  if (xdest < 0) {
    xsrc -= xdest;
    w += xdest;
    xdest = 0;
  }
  if (xsrc < 0) {
    xdest -= xsrc;
    w += xsrc;
    xsrc = 0;
  }
  if (w <= 0)
    return;
  if (xdest + w > dest->width)
    w = dest->width - xdest;
  if (xsrc + w > src->width)
    w = src->width - xsrc;
  /* check height coords */
  if (ysrc >= src->height || ysrc < 0)
    return;
  if (ydest >= dest->height || ydest < 0)
    return;

  dest->format->copy_hline (dest, xdest, ydest, src, xsrc, ysrc, w);
}

void
gst_video_image_copy_area (GstVideoImage * dest, gint xdest, gint ydest,
    GstVideoImage * src, gint xsrc, gint ysrc, gint w, gint h)
{
  gint i;

  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (dest->format == src->format);
  g_return_if_fail (w > 0);
  g_return_if_fail (h > 0);

  /* check width coords */
  if (xdest >= dest->width)
    return;
  if (xsrc >= src->width)
    return;
  if (xdest < 0) {
    xsrc -= xdest;
    w += xdest;
    xdest = 0;
  }
  if (xsrc < 0) {
    xdest -= xsrc;
    w += xsrc;
    xsrc = 0;
  }
  if (w <= 0)
    return;
  if (xdest + w > dest->width)
    w = dest->width - xdest;
  if (xsrc + w > src->width)
    w = src->width - xsrc;
  /* check height coords */
  if (ydest >= dest->height)
    return;
  if (ysrc >= src->height)
    return;
  if (ydest < 0) {
    ysrc -= ydest;
    h += ydest;
    ydest = 0;
  }
  if (ysrc < 0) {
    ydest -= ysrc;
    h += ysrc;
    ysrc = 0;
  }
  if (h <= 0)
    return;
  if (ydest + h > dest->height)
    h = dest->height - ydest;
  if (ysrc + h > src->height)
    h = src->height - ysrc;

  for (i = 0; i < h; i++) {
    dest->format->copy_hline (dest, xdest, ydest + i, src, xsrc, ysrc + i, w);
  }
}


#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

static void
paint_setup_I420 (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * ROUND_UP_2 (p->height);
  p->ustride = ROUND_UP_8 (p->width) / 2;
  p->vp = p->up + p->ustride * ROUND_UP_2 (p->height) / 2;
  p->vstride = ROUND_UP_8 (p->ystride) / 2;
  p->endptr = p->vp + p->vstride * ROUND_UP_2 (p->height) / 2;
}

static void
paint_hline_I420 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;
  int offset1 = (y / 2) * p->ustride;

  memset (p->yp + offset + x, c->Y, w);
  memset (p->up + offset1 + x1, c->U, x2 - x1);
  memset (p->vp + offset1 + x1, c->V, x2 - x1);
}

static void
copy_hline_I420 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int destoffset1 = (ydest / 2) * dest->ustride;
  int srcoffset = ysrc * src->ystride;
  int srcoffset1 = (ysrc / 2) * src->ustride;

  memcpy (dest->yp + destoffset + xdest, src->yp + srcoffset + xsrc, w);
  memcpy (dest->up + destoffset1 + xdest / 2, src->up + srcoffset1 + xsrc / 2,
      w / 2);
  memcpy (dest->vp + destoffset1 + xdest / 2, src->vp + srcoffset1 + xsrc / 2,
      w / 2);
}

static void
paint_setup_YV12 (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * ROUND_UP_2 (p->height);
  p->vstride = ROUND_UP_8 (p->ystride) / 2;
  p->up = p->vp + p->vstride * ROUND_UP_2 (p->height) / 2;
  p->ustride = ROUND_UP_8 (p->ystride) / 2;
  p->endptr = p->up + p->ustride * ROUND_UP_2 (p->height) / 2;
}

static void
paint_setup_YUY2 (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 1;
  p->vp = dest + 3;
  p->ystride = ROUND_UP_2 (p->width) * 2;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_UYVY (GstVideoImage * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest;
  p->vp = dest + 2;
  p->ystride = ROUND_UP_2 (p->width) * 2;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_YVYU (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 3;
  p->vp = dest + 1;
  p->ystride = ROUND_UP_2 (p->width * 2);
  p->endptr = dest + p->ystride * p->height;
}

#ifndef HAVE_LIBOIL
void
oil_splat_u8 (guint8 * dest, int dstr, guint8 val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += dstr;
  }
}
#endif

static void
paint_hline_YUY2 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 2, 2, c->Y, w);
  oil_splat_u8 (p->up + offset + x1 * 4, 4, c->U, x2 - x1);
  oil_splat_u8 (p->vp + offset + x1 * 4, 4, c->V, x2 - x1);
}

static void
copy_hline_YUY2 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ysrc * src->ystride;

  memcpy (dest->yp + destoffset + xdest * 2, src->yp + srcoffset + xsrc * 2,
      w * 2);
}

static void
paint_setup_IYU2 (GstVideoImage * p, char *dest)
{
  /* untested */
  p->yp = dest + 1;
  p->up = dest + 0;
  p->vp = dest + 2;
  p->ystride = ROUND_UP_4 (p->width * 3);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_hline_IYU2 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset;

  offset = y * p->ystride;
  oil_splat_u8 (p->yp + offset + x * 3, 3, c->Y, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, c->U, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, c->V, w);
}

static void
copy_hline_IYU2 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ydest * src->ystride;

  memcpy (dest->yp + destoffset + xdest * 3, src->yp + srcoffset + xsrc * 3,
      w * 3);
}

static void
paint_setup_Y41B (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = ROUND_UP_8 (p->width) / 4;
  p->vp = p->up + p->ustride * p->height;
  p->vstride = ROUND_UP_8 (p->width) / 4;
  p->endptr = p->vp + p->vstride * p->height;
}

static void
paint_hline_Y41B (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int x1 = x / 4;
  int x2 = (x + w) / 4;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  memset (p->yp + offset + x, c->Y, w);
  memset (p->up + offset1 + x1, c->U, x2 - x1);
  memset (p->vp + offset1 + x1, c->V, x2 - x1);
}

static void
copy_hline_Y41B (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int destoffset1 = ydest * dest->ustride;
  int srcoffset = ysrc * src->ystride;
  int srcoffset1 = ysrc * src->ustride;

  memcpy (dest->yp + destoffset + xdest, src->yp + srcoffset + xsrc, w);
  memcpy (dest->up + destoffset1 + xdest / 4, src->up + srcoffset1 + xsrc / 4,
      w / 4);
  memcpy (dest->vp + destoffset1 + xdest / 4, src->vp + srcoffset1 + xsrc / 4,
      w / 4);
}

static void
paint_setup_Y42B (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = ROUND_UP_8 (p->width) / 2;
  p->vp = p->up + p->ustride * p->height;
  p->vstride = ROUND_UP_8 (p->width) / 2;
  p->endptr = p->vp + p->vstride * p->height;
}

static void
paint_hline_Y42B (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  memset (p->yp + offset + x, c->Y, w);
  memset (p->up + offset1 + x1, c->U, x2 - x1);
  memset (p->vp + offset1 + x1, c->V, x2 - x1);
}

static void
copy_hline_Y42B (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int destoffset1 = ydest * dest->ustride;
  int srcoffset = ysrc * src->ystride;
  int srcoffset1 = ysrc * src->ustride;

  memcpy (dest->yp + destoffset + xdest, src->yp + srcoffset + xsrc, w);
  memcpy (dest->up + destoffset1 + xdest / 2, src->up + srcoffset1 + xsrc / 2,
      w / 2);
  memcpy (dest->vp + destoffset1 + xdest / 2, src->vp + srcoffset1 + xsrc / 2,
      w / 2);
}

static void
paint_setup_Y800 (GstVideoImage * p, char *dest)
{
  /* untested */
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_hline_Y800 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset = y * p->ystride;

  memset (p->yp + offset + x, c->Y, w);
}

static void
copy_hline_Y800 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ysrc * src->ystride;

  memcpy (dest->yp + destoffset + xdest, src->yp + srcoffset + xsrc, w);
}

static void
paint_setup_YVU9 (GstVideoImage * p, char *dest)
{
  int h = ROUND_UP_4 (p->height);

  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * ROUND_UP_4 (p->height);
  p->vstride = ROUND_UP_4 (p->ystride / 4);
  p->up = p->vp + p->vstride * ROUND_UP_4 (h / 4);
  p->ustride = ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->up + p->ustride * ROUND_UP_4 (h / 4);
}

static void
paint_setup_YUV9 (GstVideoImage * p, char *dest)
{
  /* untested */
  int h = ROUND_UP_4 (p->height);

  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * h;
  p->ustride = ROUND_UP_4 (p->ystride / 4);
  p->vp = p->up + p->ustride * ROUND_UP_4 (h / 4);
  p->vstride = ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->vp + p->vstride * ROUND_UP_4 (h / 4);
}

static void
paint_hline_YUV9 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int x1 = x / 4;
  int x2 = (x + w) / 4;
  int offset = y * p->ystride;
  int offset1 = (y / 4) * p->ustride;

  memset (p->yp + offset + x, c->Y, w);
  memset (p->up + offset1 + x1, c->U, x2 - x1);
  memset (p->vp + offset1 + x1, c->V, x2 - x1);
}

static void
copy_hline_YUV9 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int destoffset1 = ydest * dest->ustride;
  int srcoffset = ysrc * src->ystride;
  int srcoffset1 = ysrc * src->ustride;

  memcpy (dest->yp + destoffset + xdest, src->yp + srcoffset + xsrc, w);
  memcpy (dest->up + destoffset1 + xdest / 4, src->up + srcoffset1 + xsrc / 4,
      w / 4);
  memcpy (dest->vp + destoffset1 + xdest / 4, src->vp + srcoffset1 + xsrc / 4,
      w / 4);
}

static void
paint_setup_xRGB8888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest + 2;
  p->vp = dest + 3;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_xBGR8888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 3;
  p->up = dest + 2;
  p->vp = dest + 1;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_RGBx8888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_BGRx8888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_RGB888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->ystride = ROUND_UP_4 (p->width * 3);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_BGR888 (GstVideoImage * p, char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
  p->ystride = ROUND_UP_4 (p->width * 3);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_str4 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 4, 4, c->R, w);
  oil_splat_u8 (p->up + offset + x * 4, 4, c->G, w);
  oil_splat_u8 (p->vp + offset + x * 4, 4, c->B, w);
}

static void
copy_hline_str4 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ysrc * src->ystride;

  memcpy (dest->yp + destoffset + xdest * 4, src->yp + srcoffset + xsrc * 4,
      w * 4);
}

static void
paint_hline_str3 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 3, 3, c->R, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, c->G, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, c->B, w);
}

static void
copy_hline_str3 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ysrc * src->ystride;

  memcpy (dest->yp + destoffset + xdest * 3, src->yp + srcoffset + xsrc * 3,
      w * 3);
}

static void
paint_setup_RGB565 (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width * 2);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_RGB565 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset = y * p->ystride;
  unsigned int a, b;

  a = (c->R & 0xf8) | (c->G >> 5);
  b = ((c->G << 3) & 0xe0) | (c->B >> 3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, b, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, a, w);
#else
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, a, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, b, w);
#endif
}

static void
copy_hline_str2 (GstVideoImage * dest, int xdest, int ydest,
    GstVideoImage * src, int xsrc, int ysrc, int w)
{
  int destoffset = ydest * dest->ystride;
  int srcoffset = ysrc * src->ystride;

  memcpy (dest->yp + destoffset + xdest * 2, src->yp + srcoffset + xsrc * 2,
      w * 2);
}

static void
paint_setup_xRGB1555 (GstVideoImage * p, char *dest)
{
  p->yp = dest;
  p->ystride = ROUND_UP_4 (p->width * 2);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_xRGB1555 (GstVideoImage * p, int x, int y, int w,
    const GstVideoColor * c)
{
  int offset = y * p->ystride;
  unsigned int a, b;

  a = ((c->R >> 1) & 0x7c) | (c->G >> 6);
  b = ((c->G << 2) & 0xe0) | (c->B >> 3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, b, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, a, w);
#else
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, a, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, b, w);
#endif
}

const GstVideoFormat gst_video_format_list[] = {
/* packed */
  {"YUY2", "YUY2", 16, paint_setup_YUY2, paint_hline_YUY2, copy_hline_YUY2},
  {"UYVY", "UYVY", 16, paint_setup_UYVY, paint_hline_YUY2, copy_hline_YUY2},
  {"Y422", "Y422", 16, paint_setup_UYVY, paint_hline_YUY2, copy_hline_YUY2},
  {"UYNV", "UYNV", 16, paint_setup_UYVY, paint_hline_YUY2, copy_hline_YUY2},    /* FIXME: UYNV? */
  {"YVYU", "YVYU", 16, paint_setup_YVYU, paint_hline_YUY2, copy_hline_YUY2},

  /* interlaced */
  /*{ "IUYV", "IUY2", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* inverted */
  /*{ "cyuv", "cyuv", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41P", "Y41P", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* interlaced */
  /*{ "IY41", "IY41", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y211", "Y211", 8, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41T", "Y41T", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "Y42P", "Y42P", 16, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "CLJR", "CLJR", 8, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "IYU1", "IYU1", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  {"IYU2", "IYU2", 24, paint_setup_IYU2, paint_hline_IYU2, copy_hline_IYU2},

/* planar */
  /* YVU9 */
  {"YVU9", "YVU9", 9, paint_setup_YVU9, paint_hline_YUV9, copy_hline_YUV9},
  /* YUV9 */
  {"YUV9", "YUV9", 9, paint_setup_YUV9, paint_hline_YUV9, copy_hline_YUV9},
  /* IF09 */
  /* YV12 */
  {"YV12", "YV12", 12, paint_setup_YV12, paint_hline_I420, copy_hline_I420},
  /* I420 */
  {"I420", "I420", 12, paint_setup_I420, paint_hline_I420, copy_hline_I420},
  /* NV12 */
  /* NV21 */
  /* CLPL */
  /* Y41B */
  {"Y41B", "Y41B", 12, paint_setup_Y41B, paint_hline_Y41B, copy_hline_Y41B},
  /* Y42B */
  {"Y42B", "Y42B", 16, paint_setup_Y42B, paint_hline_Y42B, copy_hline_Y42B},
  /* Y800 grayscale */
  {"Y800", "Y800", 8, paint_setup_Y800, paint_hline_Y800, copy_hline_Y800},

  {"RGB ", "xRGB8888", 32, paint_setup_xRGB8888, paint_hline_str4,
        copy_hline_str4,
      1, 24, 0x00ff0000, 0x0000ff00, 0x000000ff},
  {"RGB ", "xBGR8888", 32, paint_setup_xBGR8888, paint_hline_str4,
        copy_hline_str4,
      1, 24, 0x000000ff, 0x0000ff00, 0x00ff0000},
  {"RGB ", "RGBx8888", 32, paint_setup_RGBx8888, paint_hline_str4,
        copy_hline_str4,
      1, 24, 0xff000000, 0x00ff0000, 0x0000ff00},
  {"RGB ", "BGRx8888", 32, paint_setup_BGRx8888, paint_hline_str4,
        copy_hline_str4,
      1, 24, 0x0000ff00, 0x00ff0000, 0xff000000},
  {"RGB ", "RGB888", 24, paint_setup_RGB888, paint_hline_str3, copy_hline_str3,
      1, 24, 0x00ff0000, 0x0000ff00, 0x000000ff},
  {"RGB ", "BGR888", 24, paint_setup_BGR888, paint_hline_str3, copy_hline_str3,
      1, 24, 0x000000ff, 0x0000ff00, 0x00ff0000},
  {"RGB ", "RGB565", 16, paint_setup_RGB565, paint_hline_RGB565,
        copy_hline_str2,
      1, 16, 0x0000f800, 0x000007e0, 0x0000001f},
  {"RGB ", "xRGB1555", 16, paint_setup_xRGB1555, paint_hline_xRGB1555,
        copy_hline_str2,
      1, 15, 0x00007c00, 0x000003e0, 0x0000001f},
};
const guint gst_video_format_count = G_N_ELEMENTS (gst_video_format_list);
