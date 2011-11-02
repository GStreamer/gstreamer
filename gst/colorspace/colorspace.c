/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "colorspace.h"
#include <glib.h>
#include <string.h>
#include "gstcolorspaceorc.h"


static void colorspace_convert_generic (ColorspaceConvert * convert,
    guint8 * dest, const guint8 * src);
static void colorspace_convert_lookup_fastpath (ColorspaceConvert * convert);
static void colorspace_convert_lookup_getput (ColorspaceConvert * convert);
static void colorspace_dither_none (ColorspaceConvert * convert, int j);
static void colorspace_dither_verterr (ColorspaceConvert * convert, int j);
static void colorspace_dither_halftone (ColorspaceConvert * convert, int j);


ColorspaceConvert *
colorspace_convert_new (GstVideoFormat to_format, ColorSpaceColorSpec to_spec,
    GstVideoFormat from_format, ColorSpaceColorSpec from_spec,
    int width, int height)
{
  ColorspaceConvert *convert;
  int i;

  g_return_val_if_fail (!gst_video_format_is_rgb (to_format)
      || to_spec == COLOR_SPEC_RGB, NULL);
  g_return_val_if_fail (!gst_video_format_is_yuv (to_format)
      || to_spec == COLOR_SPEC_YUV_BT709
      || to_spec == COLOR_SPEC_YUV_BT470_6
      || to_spec == COLOR_SPEC_YUV_JPEG, NULL);
  g_return_val_if_fail (gst_video_format_is_rgb (to_format)
      || gst_video_format_is_yuv (to_format)
      || (gst_video_format_is_gray (to_format) &&
          to_spec == COLOR_SPEC_GRAY), NULL);

  g_return_val_if_fail (!gst_video_format_is_rgb (from_format)
      || from_spec == COLOR_SPEC_RGB, NULL);
  g_return_val_if_fail (!gst_video_format_is_yuv (from_format)
      || from_spec == COLOR_SPEC_YUV_BT709
      || from_spec == COLOR_SPEC_YUV_BT470_6
      || from_spec == COLOR_SPEC_YUV_JPEG, NULL);
  g_return_val_if_fail (gst_video_format_is_rgb (from_format)
      || gst_video_format_is_yuv (from_format)
      || (gst_video_format_is_gray (from_format) &&
          from_spec == COLOR_SPEC_GRAY), NULL);

  convert = g_malloc (sizeof (ColorspaceConvert));
  memset (convert, 0, sizeof (ColorspaceConvert));

  convert->to_format = to_format;
  convert->to_spec = to_spec;
  convert->from_format = from_format;
  convert->from_spec = from_spec;
  convert->height = height;
  convert->width = width;
  convert->convert = colorspace_convert_generic;
  convert->dither16 = colorspace_dither_none;

  if (gst_video_format_get_component_depth (to_format, 0) > 8 ||
      gst_video_format_get_component_depth (from_format, 0) > 8) {
    convert->use_16bit = TRUE;
  } else {
    convert->use_16bit = FALSE;
  }

  for (i = 0; i < 4; i++) {
    convert->dest_stride[i] = gst_video_format_get_row_stride (to_format, i,
        width);
    convert->dest_offset[i] = gst_video_format_get_component_offset (to_format,
        i, width, height);
    if (i == 0)
      convert->dest_offset[i] = 0;

    convert->src_stride[i] = gst_video_format_get_row_stride (from_format, i,
        width);
    convert->src_offset[i] = gst_video_format_get_component_offset (from_format,
        i, width, height);
    if (i == 0)
      convert->src_offset[i] = 0;

    GST_DEBUG ("%d: dest %d %d src %d %d", i,
        convert->dest_stride[i], convert->dest_offset[i],
        convert->src_stride[i], convert->src_offset[i]);
  }

  colorspace_convert_lookup_fastpath (convert);
  colorspace_convert_lookup_getput (convert);

  convert->tmpline = g_malloc (sizeof (guint8) * (width + 8) * 4);
  convert->tmpline16 = g_malloc (sizeof (guint16) * (width + 8) * 4);
  convert->errline = g_malloc (sizeof (guint16) * width * 4);

  if (to_format == GST_VIDEO_FORMAT_RGB8_PALETTED) {
    /* build poor man's palette, taken from ffmpegcolorspace */
    static const guint8 pal_value[6] = { 0x00, 0x33, 0x66, 0x99, 0xcc, 0xff };
    guint32 *palette;
    gint r, g, b;

    convert->palette = palette = g_new (guint32, 256);
    i = 0;
    for (r = 0; r < 6; r++) {
      for (g = 0; g < 6; g++) {
        for (b = 0; b < 6; b++) {
          palette[i++] =
              (0xffU << 24) | (pal_value[r] << 16) | (pal_value[g] << 8) |
              pal_value[b];
        }
      }
    }
    palette[i++] = 0;           /* 100% transparent, i == 6*6*6 */
    while (i < 256)
      palette[i++] = 0xff000000;
  }

  return convert;
}

void
colorspace_convert_free (ColorspaceConvert * convert)
{
  g_free (convert->palette);
  g_free (convert->tmpline);
  g_free (convert->tmpline16);
  g_free (convert->errline);

  g_free (convert);
}

void
colorspace_convert_set_interlaced (ColorspaceConvert * convert,
    gboolean interlaced)
{
  convert->interlaced = interlaced;
}

void
colorspace_convert_set_dither (ColorspaceConvert * convert, int type)
{
  switch (type) {
    case 0:
    default:
      convert->dither16 = colorspace_dither_none;
      break;
    case 1:
      convert->dither16 = colorspace_dither_verterr;
      break;
    case 2:
      convert->dither16 = colorspace_dither_halftone;
      break;
  }
}

void
colorspace_convert_set_palette (ColorspaceConvert * convert,
    const guint32 * palette)
{
  if (convert->palette == NULL) {
    convert->palette = g_malloc (sizeof (guint32) * 256);
  }
  memcpy (convert->palette, palette, sizeof (guint32) * 256);
}

const guint32 *
colorspace_convert_get_palette (ColorspaceConvert * convert)
{
  return convert->palette;
}

void
colorspace_convert_convert (ColorspaceConvert * convert,
    guint8 * dest, const guint8 * src)
{
  convert->convert (convert, dest, src);
}

/* Line conversion to AYUV */

#define FRAME_GET_LINE(dir, comp, line) \
  ((dir) + convert-> dir ## _offset[(comp)] + convert-> dir ## _stride[(comp)] * (line))

static void
getline_I420 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_I420 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j >> 1),
      FRAME_GET_LINE (src, 2, j >> 1), convert->width);
}

static void
putline_I420 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_I420 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j >> 1),
      FRAME_GET_LINE (dest, 2, j >> 1), src, convert->width / 2);
}

static void
getline_YV12 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_I420 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j >> 1),
      FRAME_GET_LINE (src, 2, j >> 1), convert->width);
}

static void
putline_YV12 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_I420 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j >> 1),
      FRAME_GET_LINE (dest, 2, j >> 1), src, convert->width / 2);
}

static void
getline_YUY2 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_YUY2 (dest, FRAME_GET_LINE (src, 0, j), convert->width / 2);
}

static void
putline_YUY2 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_YUY2 (FRAME_GET_LINE (dest, 0, j), src, convert->width / 2);
}

static void
getline_UYVY (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_UYVY (dest, FRAME_GET_LINE (src, 0, j), convert->width / 2);
}

static void
putline_UYVY (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_UYVY (FRAME_GET_LINE (dest, 0, j), src, convert->width / 2);
}

static void
getline_YVYU (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_YVYU (dest, FRAME_GET_LINE (src, 0, j), convert->width / 2);
}

static void
putline_YVYU (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_YVYU (FRAME_GET_LINE (dest, 0, j), src, convert->width / 2);
}

static void
getline_v308 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 0];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 2];
  }
}

static void
putline_v308 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i * 3 + 0] = src[i * 4 + 1];
    destline[i * 3 + 1] = src[i * 4 + 2];
    destline[i * 3 + 2] = src[i * 4 + 3];
  }
}

static void
getline_AYUV (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  memcpy (dest, FRAME_GET_LINE (src, 0, j), convert->width * 4);
}

static void
putline_AYUV (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  memcpy (FRAME_GET_LINE (dest, 0, j), src, convert->width * 4);
}

#if 0
static void
getline_v410 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = GST_READ_UINT16_LE (srcline + i * 8 + 0);
    dest[i * 4 + 1] = GST_READ_UINT16_LE (srcline + i * 8 + 2);
    dest[i * 4 + 2] = GST_READ_UINT16_LE (srcline + i * 8 + 4);
    dest[i * 4 + 3] = GST_READ_UINT16_LE (srcline + i * 8 + 6);
  }
}
#endif

static void
getline_v210 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);

  for (i = 0; i < convert->width; i += 6) {
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
putline_v210 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);

  for (i = 0; i < convert->width + 5; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u1, u2;
    guint16 v0, v1, v2;

    y0 = src[4 * (i + 0) + 1] << 2;
    y1 = src[4 * (i + 1) + 1] << 2;
    y2 = src[4 * (i + 2) + 1] << 2;
    y3 = src[4 * (i + 3) + 1] << 2;
    y4 = src[4 * (i + 4) + 1] << 2;
    y5 = src[4 * (i + 5) + 1] << 2;

    u0 = (src[4 * (i + 0) + 2] + src[4 * (i + 1) + 2]) << 1;
    u1 = (src[4 * (i + 2) + 2] + src[4 * (i + 3) + 2]) << 1;
    u2 = (src[4 * (i + 4) + 2] + src[4 * (i + 5) + 2]) << 1;

    v0 = (src[4 * (i + 0) + 3] + src[4 * (i + 1) + 3]) << 1;
    v1 = (src[4 * (i + 2) + 3] + src[4 * (i + 3) + 3]) << 1;
    v2 = (src[4 * (i + 4) + 3] + src[4 * (i + 5) + 3]) << 1;

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
getline16_v210 (ColorspaceConvert * convert, guint16 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);

  for (i = 0; i < convert->width; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u2, u4;
    guint16 v0, v2, v4;

    a0 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 0);
    a1 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 4);
    a2 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 8);
    a3 = GST_READ_UINT32_LE (srcline + (i / 6) * 16 + 12);

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

    dest[4 * (i + 0) + 0] = 0xffff;
    dest[4 * (i + 0) + 1] = y0;
    dest[4 * (i + 0) + 2] = u0;
    dest[4 * (i + 0) + 3] = v0;

    dest[4 * (i + 1) + 0] = 0xffff;
    dest[4 * (i + 1) + 1] = y1;
    dest[4 * (i + 1) + 2] = u0;
    dest[4 * (i + 1) + 3] = v0;

    dest[4 * (i + 2) + 0] = 0xffff;
    dest[4 * (i + 2) + 1] = y2;
    dest[4 * (i + 2) + 2] = u2;
    dest[4 * (i + 2) + 3] = v2;

    dest[4 * (i + 3) + 0] = 0xffff;
    dest[4 * (i + 3) + 1] = y3;
    dest[4 * (i + 3) + 2] = u2;
    dest[4 * (i + 3) + 3] = v2;

    dest[4 * (i + 4) + 0] = 0xffff;
    dest[4 * (i + 4) + 1] = y4;
    dest[4 * (i + 4) + 2] = u4;
    dest[4 * (i + 4) + 3] = v4;

    dest[4 * (i + 5) + 0] = 0xffff;
    dest[4 * (i + 5) + 1] = y5;
    dest[4 * (i + 5) + 2] = u4;
    dest[4 * (i + 5) + 3] = v4;

  }
}

static void
putline16_v210 (ColorspaceConvert * convert, guint8 * dest, const guint16 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);

  for (i = 0; i < convert->width + 5; i += 6) {
    guint32 a0, a1, a2, a3;
    guint16 y0, y1, y2, y3, y4, y5;
    guint16 u0, u1, u2;
    guint16 v0, v1, v2;

    y0 = src[4 * (i + 0) + 1] >> 6;
    y1 = src[4 * (i + 1) + 1] >> 6;
    y2 = src[4 * (i + 2) + 1] >> 6;
    y3 = src[4 * (i + 3) + 1] >> 6;
    y4 = src[4 * (i + 4) + 1] >> 6;
    y5 = src[4 * (i + 5) + 1] >> 6;

    u0 = (src[4 * (i + 0) + 2] + src[4 * (i + 1) + 2] + 1) >> 7;
    u1 = (src[4 * (i + 2) + 2] + src[4 * (i + 3) + 2] + 1) >> 7;
    u2 = (src[4 * (i + 4) + 2] + src[4 * (i + 5) + 2] + 1) >> 7;

    v0 = (src[4 * (i + 0) + 3] + src[4 * (i + 1) + 3] + 1) >> 7;
    v1 = (src[4 * (i + 2) + 3] + src[4 * (i + 3) + 3] + 1) >> 7;
    v2 = (src[4 * (i + 4) + 3] + src[4 * (i + 5) + 3] + 1) >> 7;

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
getline_v216 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = GST_READ_UINT16_LE (srcline + i * 4 + 2) >> 8;
    dest[i * 4 + 2] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 0) >> 8;
    dest[i * 4 + 3] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 4) >> 8;
  }
}

static void
putline_v216 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width / 2; i++) {
    GST_WRITE_UINT16_LE (destline + i * 8 + 0, src[(i * 2 + 0) * 4 + 2] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 2, src[(i * 2 + 0) * 4 + 1] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 4, src[(i * 2 + 1) * 4 + 3] << 8);
    GST_WRITE_UINT16_LE (destline + i * 8 + 8, src[(i * 2 + 0) * 4 + 1] << 8);
  }
}

static void
getline16_v216 (ColorspaceConvert * convert, guint16 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xffff;
    dest[i * 4 + 1] = GST_READ_UINT16_LE (srcline + i * 4 + 2);
    dest[i * 4 + 2] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 0);
    dest[i * 4 + 3] = GST_READ_UINT16_LE (srcline + (i >> 1) * 8 + 4);
  }
}

static void
putline16_v216 (ColorspaceConvert * convert, guint8 * dest, const guint16 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width / 2; i++) {
    GST_WRITE_UINT16_LE (destline + i * 8 + 0, src[(i * 2 + 0) * 4 + 2]);
    GST_WRITE_UINT16_LE (destline + i * 8 + 2, src[(i * 2 + 0) * 4 + 1]);
    GST_WRITE_UINT16_LE (destline + i * 8 + 4, src[(i * 2 + 1) * 4 + 3]);
    GST_WRITE_UINT16_LE (destline + i * 8 + 8, src[(i * 2 + 0) * 4 + 1]);
  }
}

static void
getline_Y41B (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_YUV9 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j), FRAME_GET_LINE (src, 2, j),
      convert->width / 2);
}

static void
putline_Y41B (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destlineY = FRAME_GET_LINE (dest, 0, j);
  guint8 *destlineU = FRAME_GET_LINE (dest, 1, j);
  guint8 *destlineV = FRAME_GET_LINE (dest, 2, j);

  for (i = 0; i < convert->width - 3; i += 4) {
    destlineY[i] = src[i * 4 + 1];
    destlineY[i + 1] = src[i * 4 + 5];
    destlineY[i + 2] = src[i * 4 + 9];
    destlineY[i + 3] = src[i * 4 + 13];

    destlineU[i >> 2] =
        (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10] + src[i * 4 + 14] +
        2) >> 2;
    destlineV[i >> 2] =
        (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11] + src[i * 4 + 15] +
        2) >> 2;
  }

  if (i == convert->width - 3) {
    destlineY[i] = src[i * 4 + 1];
    destlineY[i + 1] = src[i * 4 + 5];
    destlineY[i + 2] = src[i * 4 + 9];

    destlineU[i >> 2] =
        (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10] + 1) / 3;
    destlineV[i >> 2] =
        (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11] + 1) / 3;
  } else if (i == convert->width - 2) {
    destlineY[i] = src[i * 4 + 1];
    destlineY[i + 1] = src[i * 4 + 5];

    destlineU[i >> 2] = (src[i * 4 + 2] + src[i * 4 + 6] + 1) >> 1;
    destlineV[i >> 2] = (src[i * 4 + 3] + src[i * 4 + 7] + 1) >> 1;
  } else if (i == convert->width - 1) {
    destlineY[i + 1] = src[i * 4 + 5];

    destlineU[i >> 2] = src[i * 4 + 2];
    destlineV[i >> 2] = src[i * 4 + 3];
  }
}

static void
getline_Y42B (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_Y42B (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j),
      FRAME_GET_LINE (src, 2, j), convert->width / 2);
}

static void
putline_Y42B (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_Y42B (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j),
      FRAME_GET_LINE (dest, 2, j), src, convert->width / 2);
}

static void
getline_Y444 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_Y444 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j), FRAME_GET_LINE (src, 2, j), convert->width);
}

static void
putline_Y444 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_Y444 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j),
      FRAME_GET_LINE (dest, 2, j), src, convert->width);
}

static void
getline_Y800 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_Y800 (dest, FRAME_GET_LINE (src, 0, j), convert->width);
}

static void
putline_Y800 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_Y800 (FRAME_GET_LINE (dest, 0, j), src, convert->width);
}

static void
getline_Y16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_Y16 (dest, FRAME_GET_LINE (src, 0, j), convert->width);
}

static void
putline_Y16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_Y16 (FRAME_GET_LINE (dest, 0, j), src, convert->width);
}

static void
getline_RGB16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = ((srcline[i] >> 11) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x3f) << 2;
    dest[i * 4 + 3] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_RGB16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint16 *destline = (guint16 *) FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i] =
        ((src[i * 4 + 1] >> 3) << 11) | ((src[i * 4 +
                2] >> 2) << 5) | (src[i * 4 + 3] >> 3);
  }
}

static void
getline_BGR16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 3] = ((srcline[i] >> 11) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x3f) << 2;
    dest[i * 4 + 1] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_BGR16 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint16 *destline = (guint16 *) FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i] =
        ((src[i * 4 + 3] >> 3) << 11) | ((src[i * 4 +
                2] >> 2) << 5) | (src[i * 4 + 1] >> 3);
  }
}

static void
getline_RGB15 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = ((srcline[i] >> 10) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x1f) << 3;
    dest[i * 4 + 3] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_RGB15 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint16 *destline = (guint16 *) FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i] =
        ((src[i * 4 + 1] >> 3) << 10) | ((src[i * 4 +
                2] >> 3) << 5) | (src[i * 4 + 3] >> 3);
  }
}

static void
getline_BGR15 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 3] = ((srcline[i] >> 10) & 0x1f) << 3;
    dest[i * 4 + 2] = ((srcline[i] >> 5) & 0x1f) << 3;
    dest[i * 4 + 1] = ((srcline[i]) & 0x1f) << 3;
  }
}

static void
putline_BGR15 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint16 *destline = (guint16 *) FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i] =
        ((src[i * 4 + 3] >> 3) << 10) | ((src[i * 4 +
                2] >> 3) << 5) | (src[i * 4 + 1] >> 3);
  }
}

static void
getline_BGRA (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_BGRA (dest, FRAME_GET_LINE (src, 0, j), convert->width);
}

static void
putline_BGRA (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_BGRA (FRAME_GET_LINE (dest, 0, j), src, convert->width);
}

static void
getline_ABGR (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_ABGR (dest, FRAME_GET_LINE (src, 0, j), convert->width);
}

static void
putline_ABGR (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_ABGR (FRAME_GET_LINE (dest, 0, j), src, convert->width);
}

static void
getline_RGBA (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_RGBA (dest, FRAME_GET_LINE (src, 0, j), convert->width);
}

static void
putline_RGBA (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_RGBA (FRAME_GET_LINE (dest, 0, j), src, convert->width);
}

static void
getline_RGB (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 0];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 2];
  }
}

static void
putline_RGB (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i * 3 + 0] = src[i * 4 + 1];
    destline[i * 3 + 1] = src[i * 4 + 2];
    destline[i * 3 + 2] = src[i * 4 + 3];
  }
}

static void
getline_BGR (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[i * 3 + 2];
    dest[i * 4 + 2] = srcline[i * 3 + 1];
    dest[i * 4 + 3] = srcline[i * 3 + 0];
  }
}

static void
putline_BGR (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    destline[i * 3 + 0] = src[i * 4 + 3];
    destline[i * 3 + 1] = src[i * 4 + 2];
    destline[i * 3 + 2] = src[i * 4 + 1];
  }
}

static void
getline_NV12 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_NV12 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j >> 1), convert->width / 2);
}

static void
putline_NV12 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_NV12 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j >> 1), src, convert->width / 2);
}

static void
getline_NV21 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_NV21 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 2, j >> 1), convert->width / 2);
}

static void
putline_NV21 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_NV21 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 2, j >> 1), src, convert->width / 2);
}

static void
getline_UYVP (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;

  for (i = 0; i < convert->width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    u0 = (src[(i / 2) * 5 + 0] << 2) | (src[(i / 2) * 5 + 1] >> 6);
    y0 = ((src[(i / 2) * 5 + 1] & 0x3f) << 4) | (src[(i / 2) * 5 + 2] >> 4);
    v0 = ((src[(i / 2) * 5 + 2] & 0x0f) << 6) | (src[(i / 2) * 5 + 3] >> 2);
    y1 = ((src[(i / 2) * 5 + 3] & 0x03) << 8) | src[(i / 2) * 5 + 4];

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
putline_UYVP (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;

  for (i = 0; i < convert->width; i += 2) {
    guint16 y0, y1;
    guint16 u0;
    guint16 v0;

    y0 = src[4 * (i + 0) + 1];
    y1 = src[4 * (i + 1) + 1];
    u0 = (src[4 * (i + 0) + 2] + src[4 * (i + 1) + 2] + 1) >> 1;
    v0 = (src[4 * (i + 0) + 3] + src[4 * (i + 1) + 3] + 1) >> 1;

    dest[(i / 2) * 5 + 0] = u0;
    dest[(i / 2) * 5 + 1] = y0 >> 2;
    dest[(i / 2) * 5 + 2] = (y0 << 6) | (v0 >> 4);
    dest[(i / 2) * 5 + 3] = (v0 << 4) | (y1 >> 2);
    dest[(i / 2) * 5 + 4] = (y1 << 2);
  }
}

static void
getline_A420 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_A420 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j >> 1),
      FRAME_GET_LINE (src, 2, j >> 1),
      FRAME_GET_LINE (src, 3, j), convert->width);
}

static void
putline_A420 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_putline_A420 (FRAME_GET_LINE (dest, 0, j),
      FRAME_GET_LINE (dest, 1, j >> 1),
      FRAME_GET_LINE (dest, 2, j >> 1),
      FRAME_GET_LINE (dest, 3, j), src, convert->width / 2);
}

static void
getline_RGB8P (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    guint32 v = convert->palette[srcline[i]];
    dest[i * 4 + 0] = (v >> 24) & 0xff;
    dest[i * 4 + 1] = (v >> 16) & 0xff;
    dest[i * 4 + 2] = (v >> 8) & 0xff;
    dest[i * 4 + 3] = (v) & 0xff;
  }
}

static void
putline_RGB8P (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  /* Use our poor man's palette, taken from ffmpegcolorspace too */
  for (i = 0; i < convert->width; i++) {
    /* crude approximation for alpha ! */
    if (src[i * 4 + 0] < 0x80)
      destline[i] = 6 * 6 * 6;
    else
      destline[i] =
          ((((src[i * 4 + 1]) / 47) % 6) * 6 * 6 + (((src[i * 4 +
                          2]) / 47) % 6) * 6 + (((src[i * 4 + 3]) / 47) % 6));
  }
}


static void
getline_YUV9 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  cogorc_getline_YUV9 (dest,
      FRAME_GET_LINE (src, 0, j),
      FRAME_GET_LINE (src, 1, j >> 2),
      FRAME_GET_LINE (src, 2, j >> 2), convert->width / 2);
}

static void
putline_YUV9 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destY = FRAME_GET_LINE (dest, 0, j);
  guint8 *destU = FRAME_GET_LINE (dest, 1, j >> 2);
  guint8 *destV = FRAME_GET_LINE (dest, 2, j >> 2);

  for (i = 0; i < convert->width - 3; i += 4) {
    destY[i] = src[i * 4 + 1];
    destY[i + 1] = src[i * 4 + 5];
    destY[i + 2] = src[i * 4 + 9];
    destY[i + 3] = src[i * 4 + 13];
    if (j % 4 == 0) {
      destU[i >> 2] =
          (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10] + src[i * 4 +
              14]) >> 2;
      destV[i >> 2] =
          (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11] + src[i * 4 +
              15]) >> 2;
    }
  }

  if (i == convert->width - 3) {
    destY[i] = src[i * 4 + 1];
    destY[i + 1] = src[i * 4 + 5];
    destY[i + 2] = src[i * 4 + 9];
    if (j % 4 == 0) {
      destU[i >> 2] = (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10]) / 3;
      destV[i >> 2] = (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11]) / 3;
    }
  } else if (i == convert->width - 2) {
    destY[i] = src[i * 4 + 1];
    destY[i + 1] = src[i * 4 + 5];
    if (j % 4 == 0) {
      destU[i >> 2] = (src[i * 4 + 2] + src[i * 4 + 6]) >> 1;
      destV[i >> 2] = (src[i * 4 + 3] + src[i * 4 + 7]) >> 1;
    }
  } else if (i == convert->width - 1) {
    destY[i] = src[i * 4 + 1];
    destU[i >> 2] = src[i * 4 + 2];
    destV[i >> 2] = src[i * 4 + 3];
  }
}

static void
getline_IYU1 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);

  for (i = 0; i < convert->width - 3; i += 4) {
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

  if (i == convert->width - 3) {
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
  } else if (i == convert->width - 2) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 4] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 5] = srcline[(i >> 2) * 6 + 2];
    dest[i * 4 + 2] = dest[i * 4 + 6] = srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = dest[i * 4 + 7] = srcline[(i >> 2) * 6 + 3];
  } else if (i == convert->width - 1) {
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = srcline[(i >> 2) * 6 + 1];
    dest[i * 4 + 2] = srcline[(i >> 2) * 6 + 0];
    dest[i * 4 + 3] = srcline[(i >> 2) * 6 + 3];
  }
}

static void
putline_IYU1 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);

  for (i = 0; i < convert->width - 3; i += 4) {
    destline[(i >> 2) * 6 + 1] = src[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = src[i * 4 + 5];
    destline[(i >> 2) * 6 + 4] = src[i * 4 + 9];
    destline[(i >> 2) * 6 + 5] = src[i * 4 + 13];
    destline[(i >> 2) * 6 + 0] =
        (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10] + src[i * 4 +
            14]) >> 2;
    destline[(i >> 2) * 6 + 3] =
        (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11] + src[i * 4 +
            15]) >> 2;
  }

  if (i == convert->width - 3) {
    destline[(i >> 2) * 6 + 1] = src[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = src[i * 4 + 5];
    destline[(i >> 2) * 6 + 4] = src[i * 4 + 9];
    destline[(i >> 2) * 6 + 0] =
        (src[i * 4 + 2] + src[i * 4 + 6] + src[i * 4 + 10]) / 3;
    destline[(i >> 2) * 6 + 3] =
        (src[i * 4 + 3] + src[i * 4 + 7] + src[i * 4 + 11]) / 3;
  } else if (i == convert->width - 2) {
    destline[(i >> 2) * 6 + 1] = src[i * 4 + 1];
    destline[(i >> 2) * 6 + 2] = src[i * 4 + 5];
    destline[(i >> 2) * 6 + 0] = (src[i * 4 + 2] + src[i * 4 + 6]) >> 1;
    destline[(i >> 2) * 6 + 3] = (src[i * 4 + 3] + src[i * 4 + 7]) >> 1;
  } else if (i == convert->width - 1) {
    destline[(i >> 2) * 6 + 1] = src[i * 4 + 1];
    destline[(i >> 2) * 6 + 0] = src[i * 4 + 2];
    destline[(i >> 2) * 6 + 3] = src[i * 4 + 3];
  }
}

static void
getline_AY64 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint16 *srcline = (const guint16 *) FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width * 4; i++) {
    dest[i] = srcline[i] >> 8;
  }
}

static void
putline_AY64 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint16 *destline = (guint16 *) FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width * 4; i++) {
    destline[i] = src[i] << 8;
  }
}

static void
getline16_AY64 (ColorspaceConvert * convert, guint16 * dest, const guint8 * src,
    int j)
{
  memcpy (dest, FRAME_GET_LINE (src, 0, j), convert->width * 8);
}

static void
putline16_AY64 (ColorspaceConvert * convert, guint8 * dest, const guint16 * src,
    int j)
{
  memcpy (FRAME_GET_LINE (dest, 0, j), src, convert->width * 8);
}

static void
getline_r210 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    guint8 x;
    dest[i * 4 + 0] = 0xff;
    x = GST_READ_UINT32_BE (srcline + i * 4);
    dest[i * 4 + 1] = (x >> 22) & 0xff;
    dest[i * 4 + 2] = (x >> 12) & 0xff;
    dest[i * 4 + 3] = (x >> 2) & 0xff;
  }
}

static void
putline_r210 (ColorspaceConvert * convert, guint8 * dest, const guint8 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width / 2; i++) {
    guint32 x = 0;
    x |= src[i * 4 + 1] << 22;
    x |= (src[i * 4 + 1] & 0xc0) << 14;
    x |= src[i * 4 + 2] << 12;
    x |= (src[i * 4 + 2] & 0xc0) << 10;
    x |= src[i * 4 + 3] << 2;
    x |= (src[i * 4 + 3] & 0xc0) >> 6;
    GST_WRITE_UINT32_BE (destline + i * 4, x);
  }
}

static void
getline16_r210 (ColorspaceConvert * convert, guint16 * dest, const guint8 * src,
    int j)
{
  int i;
  const guint8 *srcline = FRAME_GET_LINE (src, 0, j);
  for (i = 0; i < convert->width; i++) {
    guint32 x;
    dest[i * 4 + 0] = 0xffff;
    x = GST_READ_UINT32_BE (srcline + i * 4);
    dest[i * 4 + 1] = ((x >> 14) & 0xffc0) | (x >> 24);
    dest[i * 4 + 2] = ((x >> 4) & 0xffc0) | ((x >> 14) & 0x3f);
    dest[i * 4 + 3] = ((x << 6) & 0xffc0) | ((x >> 4) & 0x3f);
  }
}

static void
putline16_r210 (ColorspaceConvert * convert, guint8 * dest, const guint16 * src,
    int j)
{
  int i;
  guint8 *destline = FRAME_GET_LINE (dest, 0, j);
  for (i = 0; i < convert->width; i++) {
    guint32 x = 0;
    x |= (src[i * 4 + 1] & 0xffc0) << 14;
    x |= (src[i * 4 + 2] & 0xffc0) << 4;
    x |= (src[i * 4 + 3] & 0xffc0) >> 6;
    GST_WRITE_UINT32_BE (destline + i * 4, x);
  }
}

static void
getline16_convert (ColorspaceConvert * convert, guint16 * dest,
    const guint8 * src, int j)
{
  int i;
  convert->getline (convert, convert->tmpline, src, j);
  for (i = 0; i < convert->width * 4; i++) {
    dest[i] = convert->tmpline[i] << 8;
  }
}

static void
putline16_convert (ColorspaceConvert * convert, guint8 * dest,
    const guint16 * src, int j)
{
  int i;
  for (i = 0; i < convert->width * 4; i++) {
    convert->tmpline[i] = src[i] >> 8;
  }
  convert->putline (convert, dest, convert->tmpline, j);
}

typedef struct
{
  GstVideoFormat format;
  void (*getline) (ColorspaceConvert * convert, guint8 * dest,
      const guint8 * src, int j);
  void (*putline) (ColorspaceConvert * convert, guint8 * dest,
      const guint8 * src, int j);
  void (*getline16) (ColorspaceConvert * convert, guint16 * dest,
      const guint8 * src, int j);
  void (*putline16) (ColorspaceConvert * convert, guint8 * dest,
      const guint16 * src, int j);
} ColorspaceLine;
static const ColorspaceLine lines[] = {
  {GST_VIDEO_FORMAT_I420, getline_I420, putline_I420},
  {GST_VIDEO_FORMAT_YV12, getline_YV12, putline_YV12},
  {GST_VIDEO_FORMAT_YUY2, getline_YUY2, putline_YUY2},
  {GST_VIDEO_FORMAT_UYVY, getline_UYVY, putline_UYVY},
  {GST_VIDEO_FORMAT_AYUV, getline_AYUV, putline_AYUV},
  {GST_VIDEO_FORMAT_RGBx, getline_RGBA, putline_RGBA},
  {GST_VIDEO_FORMAT_BGRx, getline_BGRA, putline_BGRA},
  {GST_VIDEO_FORMAT_xRGB, getline_AYUV, putline_AYUV},
  {GST_VIDEO_FORMAT_xBGR, getline_ABGR, putline_ABGR},
  {GST_VIDEO_FORMAT_RGBA, getline_RGBA, putline_RGBA},
  {GST_VIDEO_FORMAT_BGRA, getline_BGRA, putline_BGRA},
  {GST_VIDEO_FORMAT_ARGB, getline_AYUV, putline_AYUV},
  {GST_VIDEO_FORMAT_ABGR, getline_ABGR, putline_ABGR},
  {GST_VIDEO_FORMAT_RGB, getline_RGB, putline_RGB},
  {GST_VIDEO_FORMAT_BGR, getline_BGR, putline_BGR},
  {GST_VIDEO_FORMAT_Y41B, getline_Y41B, putline_Y41B},
  {GST_VIDEO_FORMAT_Y42B, getline_Y42B, putline_Y42B},
  {GST_VIDEO_FORMAT_YVYU, getline_YVYU, putline_YVYU},
  {GST_VIDEO_FORMAT_Y444, getline_Y444, putline_Y444},
  {GST_VIDEO_FORMAT_v210, getline_v210, putline_v210,
      getline16_v210, putline16_v210},
  {GST_VIDEO_FORMAT_v216, getline_v216, putline_v216,
      getline16_v216, putline16_v216},
  {GST_VIDEO_FORMAT_NV12, getline_NV12, putline_NV12},
  {GST_VIDEO_FORMAT_NV21, getline_NV21, putline_NV21},
  //{GST_VIDEO_FORMAT_GRAY8, getline_GRAY8, putline_GRAY8},
  //{GST_VIDEO_FORMAT_GRAY16_BE, getline_GRAY16_BE, putline_GRAY16_BE},
  //{GST_VIDEO_FORMAT_GRAY16_LE, getline_GRAY16_LE, putline_GRAY16_LE},
  {GST_VIDEO_FORMAT_v308, getline_v308, putline_v308},
  {GST_VIDEO_FORMAT_Y800, getline_Y800, putline_Y800},
  {GST_VIDEO_FORMAT_Y16, getline_Y16, putline_Y16},
  {GST_VIDEO_FORMAT_RGB16, getline_RGB16, putline_RGB16},
  {GST_VIDEO_FORMAT_BGR16, getline_BGR16, putline_BGR16},
  {GST_VIDEO_FORMAT_RGB15, getline_RGB15, putline_RGB15},
  {GST_VIDEO_FORMAT_BGR15, getline_BGR15, putline_BGR15},
  {GST_VIDEO_FORMAT_UYVP, getline_UYVP, putline_UYVP},
  {GST_VIDEO_FORMAT_A420, getline_A420, putline_A420}
  , {GST_VIDEO_FORMAT_RGB8_PALETTED, getline_RGB8P, putline_RGB8P},
  {GST_VIDEO_FORMAT_YUV9, getline_YUV9, putline_YUV9},
  {GST_VIDEO_FORMAT_YVU9, getline_YUV9, putline_YUV9},  /* alias */
  {GST_VIDEO_FORMAT_IYU1, getline_IYU1, putline_IYU1},
  {GST_VIDEO_FORMAT_ARGB64, getline_AY64, putline_AY64, getline16_AY64,
      putline16_AY64},
  {GST_VIDEO_FORMAT_AYUV64, getline_AY64, putline_AY64, getline16_AY64,
      putline16_AY64},
  {GST_VIDEO_FORMAT_r210, getline_r210, putline_r210, getline16_r210,
      putline16_r210}
};

static void
matrix_rgb_to_yuv_bt470_6 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (66 * r + 129 * g + 25 * b + 4096) >> 8;
    u = (-38 * r - 74 * g + 112 * b + 32768) >> 8;
    v = (112 * r - 94 * g - 18 * b + 32768) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 255);
  }
}

static void
matrix_rgb_to_yuv_bt709 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
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
matrix_yuv_bt470_6_to_rgb (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (298 * y + 409 * v - 57068) >> 8;
    g = (298 * y - 100 * u - 208 * v + 34707) >> 8;
    b = (298 * y + 516 * u - 70870) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 255);
  }
}

static void
matrix_yuv_bt709_to_rgb (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
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

static void
matrix_yuv_bt709_to_yuv_bt470_6 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (256 * y + 25 * u + 49 * v - 9536) >> 8;
    g = (253 * u - 28 * v + 3958) >> 8;
    b = (-19 * u + 252 * v + 2918) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 255);
  }
}

static void
matrix_yuv_bt470_6_to_yuv_bt709 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (256 * y - 30 * u - 53 * v + 10600) >> 8;
    g = (261 * u + 29 * v - 4367) >> 8;
    b = (19 * u + 262 * v - 3289) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 255);
  }
}

static void
matrix_identity (ColorspaceConvert * convert)
{
  /* do nothing */
}

static void
matrix16_rgb_to_yuv_bt470_6 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (66 * r + 129 * g + 25 * b + 4096 * 256) >> 8;
    u = (-38 * r - 74 * g + 112 * b + 32768 * 256) >> 8;
    v = (112 * r - 94 * g - 18 * b + 32768 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}

static void
matrix16_rgb_to_yuv_bt709 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (47 * r + 157 * g + 16 * b + 4096 * 256) >> 8;
    u = (-26 * r - 87 * g + 112 * b + 32768 * 256) >> 8;
    v = (112 * r - 102 * g - 10 * b + 32768 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}

static void
matrix16_yuv_bt470_6_to_rgb (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (298 * y + 409 * v - 57068 * 256) >> 8;
    g = (298 * y - 100 * u - 208 * v + 34707 * 256) >> 8;
    b = (298 * y + 516 * u - 70870 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 65535);
  }
}

static void
matrix16_yuv_bt709_to_rgb (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (298 * y + 459 * v - 63514 * 256) >> 8;
    g = (298 * y - 55 * u - 136 * v + 19681 * 256) >> 8;
    b = (298 * y + 541 * u - 73988 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 65535);
  }
}

static void
matrix16_yuv_bt709_to_yuv_bt470_6 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (256 * y + 25 * u + 49 * v - 9536 * 256) >> 8;
    g = (253 * u - 28 * v + 3958 * 256) >> 8;
    b = (-19 * u + 252 * v + 2918 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 65535);
  }
}

static void
matrix16_yuv_bt470_6_to_yuv_bt709 (ColorspaceConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    y = tmpline[i * 4 + 1];
    u = tmpline[i * 4 + 2];
    v = tmpline[i * 4 + 3];

    r = (256 * y - 30 * u - 53 * v + 10600 * 256) >> 8;
    g = (261 * u + 29 * v - 4367 * 256) >> 8;
    b = (19 * u + 262 * v - 3289 * 256) >> 8;

    tmpline[i * 4 + 1] = CLAMP (r, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (g, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (b, 0, 65535);
  }
}

static void
matrix16_identity (ColorspaceConvert * convert)
{
  /* do nothing */
}



static void
colorspace_convert_lookup_getput (ColorspaceConvert * convert)
{
  int i;

  convert->getline = NULL;
  convert->getline16 = NULL;
  for (i = 0; i < sizeof (lines) / sizeof (lines[0]); i++) {
    if (lines[i].format == convert->from_format) {
      convert->getline = lines[i].getline;
      convert->getline16 = lines[i].getline16;
      break;
    }
  }
  convert->putline = NULL;
  convert->putline16 = NULL;
  for (i = 0; i < sizeof (lines) / sizeof (lines[0]); i++) {
    if (lines[i].format == convert->to_format) {
      convert->putline = lines[i].putline;
      convert->putline16 = lines[i].putline16;
      break;
    }
  }
  GST_DEBUG ("get %p put %p", convert->getline, convert->putline);

  if (convert->getline16 == NULL) {
    convert->getline16 = getline16_convert;
  }
  if (convert->putline16 == NULL) {
    convert->putline16 = putline16_convert;
  }

  if (convert->from_spec == convert->to_spec) {
    convert->matrix = matrix_identity;
    convert->matrix16 = matrix16_identity;
  } else if (convert->from_spec == COLOR_SPEC_RGB
      && convert->to_spec == COLOR_SPEC_YUV_BT470_6) {
    convert->matrix = matrix_rgb_to_yuv_bt470_6;
    convert->matrix16 = matrix16_rgb_to_yuv_bt470_6;
  } else if (convert->from_spec == COLOR_SPEC_RGB
      && convert->to_spec == COLOR_SPEC_YUV_BT709) {
    convert->matrix = matrix_rgb_to_yuv_bt709;
    convert->matrix16 = matrix16_rgb_to_yuv_bt709;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT470_6
      && convert->to_spec == COLOR_SPEC_RGB) {
    convert->matrix = matrix_yuv_bt470_6_to_rgb;
    convert->matrix16 = matrix16_yuv_bt470_6_to_rgb;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT709
      && convert->to_spec == COLOR_SPEC_RGB) {
    convert->matrix = matrix_yuv_bt709_to_rgb;
    convert->matrix16 = matrix16_yuv_bt709_to_rgb;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT709
      && convert->to_spec == COLOR_SPEC_YUV_BT470_6) {
    convert->matrix = matrix_yuv_bt709_to_yuv_bt470_6;
    convert->matrix16 = matrix16_yuv_bt709_to_yuv_bt470_6;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT470_6
      && convert->to_spec == COLOR_SPEC_YUV_BT709) {
    convert->matrix = matrix_yuv_bt470_6_to_yuv_bt709;
    convert->matrix16 = matrix16_yuv_bt470_6_to_yuv_bt709;
  }
}

static void
colorspace_convert_generic (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int j;

  if (convert->getline == NULL) {
    GST_ERROR ("no getline");
    return;
  }

  if (convert->putline == NULL) {
    GST_ERROR ("no putline");
    return;
  }

  if (convert->use_16bit) {
    for (j = 0; j < convert->height; j++) {
      convert->getline16 (convert, convert->tmpline16, src, j);
      convert->matrix16 (convert);
      convert->dither16 (convert, j);
      convert->putline16 (convert, dest, convert->tmpline16, j);
    }
  } else {
    for (j = 0; j < convert->height; j++) {
      convert->getline (convert, convert->tmpline, src, j);
      convert->matrix (convert);
      convert->putline (convert, dest, convert->tmpline, j);
    }
  }
}

static void
colorspace_dither_none (ColorspaceConvert * convert, int j)
{
}

static void
colorspace_dither_verterr (ColorspaceConvert * convert, int j)
{
  int i;
  guint16 *tmpline = convert->tmpline16;
  guint16 *errline = convert->errline;

  for (i = 0; i < 4 * convert->width; i++) {
    tmpline[i] += errline[i];
    errline[i] = tmpline[i] & 0xff;
  }
}

static void
colorspace_dither_halftone (ColorspaceConvert * convert, int j)
{
  int i;
  guint16 *tmpline = convert->tmpline16;
  static guint16 halftone[8][8] = {
    {0, 128, 32, 160, 8, 136, 40, 168},
    {192, 64, 224, 96, 200, 72, 232, 104},
    {48, 176, 16, 144, 56, 184, 24, 152},
    {240, 112, 208, 80, 248, 120, 216, 88},
    {12, 240, 44, 172, 4, 132, 36, 164},
    {204, 76, 236, 108, 196, 68, 228, 100},
    {60, 188, 28, 156, 52, 180, 20, 148},
    {252, 142, 220, 92, 244, 116, 212, 84}
  };

  for (i = 0; i < convert->width * 4; i++) {
    tmpline[i] += halftone[(i >> 2) & 7][j & 7];
  }
}

/* Fast paths */

static void
convert_I420_YUY2 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i;

  for (i = 0; i < GST_ROUND_DOWN_2 (convert->height); i += 2) {
    cogorc_convert_I420_YUY2 (FRAME_GET_LINE (dest, 0, i),
        FRAME_GET_LINE (dest, 0, i + 1),
        FRAME_GET_LINE (src, 0, i),
        FRAME_GET_LINE (src, 0, i + 1),
        FRAME_GET_LINE (src, 1, i >> 1),
        FRAME_GET_LINE (src, 2, i >> 1), (convert->width + 1) / 2);
  }

  /* now handle last line */
  if (convert->height & 1) {
    getline_I420 (convert, convert->tmpline, src, convert->height - 1);
    putline_YUY2 (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_I420_UYVY (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i;

  for (i = 0; i < GST_ROUND_DOWN_2 (convert->height); i += 2) {
    cogorc_convert_I420_UYVY (FRAME_GET_LINE (dest, 0, i),
        FRAME_GET_LINE (dest, 0, i + 1),
        FRAME_GET_LINE (src, 0, i),
        FRAME_GET_LINE (src, 0, i + 1),
        FRAME_GET_LINE (src, 1, i >> 1),
        FRAME_GET_LINE (src, 2, i >> 1), (convert->width + 1) / 2);
  }

  /* now handle last line */
  if (convert->height & 1) {
    getline_I420 (convert, convert->tmpline, src, convert->height - 1);
    putline_UYVY (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_I420_AYUV (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i;

  for (i = 0; i < GST_ROUND_DOWN_2 (convert->height); i += 2) {
    cogorc_convert_I420_AYUV (FRAME_GET_LINE (dest, 0, i),
        FRAME_GET_LINE (dest, 0, i + 1),
        FRAME_GET_LINE (src, 0, i),
        FRAME_GET_LINE (src, 0, i + 1),
        FRAME_GET_LINE (src, 1, i >> 1),
        FRAME_GET_LINE (src, 2, i >> 1), convert->width);
  }

  /* now handle last line */
  if (convert->height & 1) {
    getline_I420 (convert, convert->tmpline, src, convert->height - 1);
    putline_AYUV (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_I420_Y42B (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_420_422 (FRAME_GET_LINE (dest, 1, 0),
      2 * convert->dest_stride[1], FRAME_GET_LINE (dest, 1, 1),
      2 * convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], (convert->width + 1) / 2, convert->height / 2);

  cogorc_planar_chroma_420_422 (FRAME_GET_LINE (dest, 2, 0),
      2 * convert->dest_stride[2], FRAME_GET_LINE (dest, 2, 1),
      2 * convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height / 2);
}

static void
convert_I420_Y444 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_420_444 (FRAME_GET_LINE (dest, 1, 0),
      2 * convert->dest_stride[1], FRAME_GET_LINE (dest, 1, 1),
      2 * convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], (convert->width + 1) / 2, convert->height / 2);

  cogorc_planar_chroma_420_444 (FRAME_GET_LINE (dest, 2, 0),
      2 * convert->dest_stride[2], FRAME_GET_LINE (dest, 2, 1),
      2 * convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height / 2);

  /* now handle last line */
  if (convert->height & 1) {
    getline_I420 (convert, convert->tmpline, src, convert->height - 1);
    putline_Y444 (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_YUY2_I420 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i, h;

  h = convert->height;
  if (convert->width & 1)
    h--;

  for (i = 0; i < h; i += 2) {
    cogorc_convert_YUY2_I420 (FRAME_GET_LINE (dest, 0, i),
        FRAME_GET_LINE (dest, 0, i + 1),
        FRAME_GET_LINE (dest, 1, i >> 1),
        FRAME_GET_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, 0, i),
        FRAME_GET_LINE (src, 0, i + 1), (convert->width + 1) / 2);
  }

  /* now handle last line */
  if (convert->height & 1) {
    getline_YUY2 (convert, convert->tmpline, src, convert->height - 1);
    putline_I420 (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_YUY2_AYUV (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_YUY2_AYUV (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2,
      convert->height & 1 ? convert->height - 1 : convert->height);

  /* now handle last line */
  if (convert->height & 1) {
    getline_YUY2 (convert, convert->tmpline, src, convert->height - 1);
    putline_AYUV (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_YUY2_Y42B (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_YUY2_Y42B (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2, convert->height);
}

static void
convert_YUY2_Y444 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_YUY2_Y444 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2, convert->height);
}


static void
convert_UYVY_I420 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i;

  for (i = 0; i < GST_ROUND_DOWN_2 (convert->height); i += 2) {
    cogorc_convert_UYVY_I420 (FRAME_GET_LINE (dest, 0, i),
        FRAME_GET_LINE (dest, 0, i + 1),
        FRAME_GET_LINE (dest, 1, i >> 1),
        FRAME_GET_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, 0, i),
        FRAME_GET_LINE (src, 0, i + 1), (convert->width + 1) / 2);
  }

  /* now handle last line */
  if (convert->height & 1) {
    getline_UYVY (convert, convert->tmpline, src, convert->height - 1);
    putline_I420 (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_UYVY_AYUV (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_UYVY_AYUV (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2,
      convert->height & 1 ? convert->height - 1 : convert->height);

  /* now handle last line */
  if (convert->height & 1) {
    getline_UYVY (convert, convert->tmpline, src, convert->height - 1);
    putline_AYUV (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_UYVY_YUY2 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_UYVY_YUY2 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2, convert->height);
}

static void
convert_UYVY_Y42B (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_UYVY_Y42B (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2, convert->height);
}

static void
convert_UYVY_Y444 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_UYVY_Y444 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2, convert->height);
}

static void
convert_AYUV_I420 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_I420 (FRAME_GET_LINE (dest, 0, 0),
      2 * convert->dest_stride[0], FRAME_GET_LINE (dest, 0, 1),
      2 * convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      2 * convert->src_stride[0], FRAME_GET_LINE (src, 0, 1),
      2 * convert->src_stride[0], convert->width / 2, convert->height / 2);
}

static void
convert_AYUV_YUY2 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_YUY2 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width / 2, convert->height);
}

static void
convert_AYUV_UYVY (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_UYVY (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width / 2, convert->height);
}

static void
convert_AYUV_Y42B (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_Y42B (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], (convert->width + 1) / 2,
      convert->height & 1 ? convert->height - 1 : convert->height);

  /* now handle last line */
  if (convert->height & 1) {
    getline_AYUV (convert, convert->tmpline, src, convert->height - 1);
    putline_Y42B (convert, dest, convert->tmpline, convert->height - 1);
  }

}

static void
convert_AYUV_Y444 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_Y444 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width, convert->height);
}

static void
convert_Y42B_I420 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_422_420 (FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      2 * convert->src_stride[1], FRAME_GET_LINE (src, 1, 1),
      2 * convert->src_stride[1], (convert->width + 1) / 2,
      convert->height / 2);

  cogorc_planar_chroma_422_420 (FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      2 * convert->src_stride[2], FRAME_GET_LINE (src, 2, 1),
      2 * convert->src_stride[2], (convert->width + 1) / 2,
      convert->height / 2);

  /* now handle last line */
  if (convert->height & 1) {
    getline_Y42B (convert, convert->tmpline, src, convert->height - 1);
    putline_I420 (convert, dest, convert->tmpline, convert->height - 1);
  }

}

static void
convert_Y42B_Y444 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_422_444 (FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], (convert->width + 1) / 2, convert->height);

  cogorc_planar_chroma_422_444 (FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y42B_YUY2 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y42B_YUY2 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y42B_UYVY (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y42B_UYVY (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y42B_AYUV (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y42B_AYUV (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width) / 2, convert->height);
}

static void
convert_Y444_I420 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_444_420 (FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      2 * convert->src_stride[1], FRAME_GET_LINE (src, 1, 1),
      2 * convert->src_stride[1], (convert->width + 1) / 2,
      convert->height / 2);

  cogorc_planar_chroma_444_420 (FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      2 * convert->src_stride[2], FRAME_GET_LINE (src, 2, 1),
      2 * convert->src_stride[2], (convert->width + 1) / 2,
      convert->height / 2);

  /* now handle last line */
  if (convert->height & 1) {
    getline_Y444 (convert, convert->tmpline, src, convert->height - 1);
    putline_I420 (convert, dest, convert->tmpline, convert->height - 1);
  }
}

static void
convert_Y444_Y42B (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_memcpy_2d (FRAME_GET_LINE (dest, 0, 0), convert->dest_stride[0],
      FRAME_GET_LINE (src, 0, 0), convert->src_stride[0],
      convert->width, convert->height);

  cogorc_planar_chroma_444_422 (FRAME_GET_LINE (dest, 1, 0),
      convert->dest_stride[1], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], (convert->width + 1) / 2, convert->height);

  cogorc_planar_chroma_444_422 (FRAME_GET_LINE (dest, 2, 0),
      convert->dest_stride[2], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y444_YUY2 (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y444_YUY2 (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y444_UYVY (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y444_UYVY (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], (convert->width + 1) / 2, convert->height);
}

static void
convert_Y444_AYUV (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_Y444_AYUV (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], FRAME_GET_LINE (src, 1, 0),
      convert->src_stride[1], FRAME_GET_LINE (src, 2, 0),
      convert->src_stride[2], convert->width, convert->height);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static void
convert_AYUV_ARGB (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width, convert->height);
}

static void
convert_AYUV_BGRA (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width, convert->height);
}

static void
convert_AYUV_ABGR (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width, convert->height);
}

static void
convert_AYUV_RGBA (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  cogorc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0, 0),
      convert->dest_stride[0], FRAME_GET_LINE (src, 0, 0),
      convert->src_stride[0], convert->width, convert->height);
}

static void
convert_I420_BGRA (ColorspaceConvert * convert, guint8 * dest,
    const guint8 * src)
{
  int i;
  int quality = 0;

  if (quality > 3) {
    for (i = 0; i < convert->height; i++) {
      if (i & 1) {
        cogorc_convert_I420_BGRA_avg (FRAME_GET_LINE (dest, 0, i),
            FRAME_GET_LINE (src, 0, i),
            FRAME_GET_LINE (src, 1, i >> 1),
            FRAME_GET_LINE (src, 1, (i >> 1) + 1),
            FRAME_GET_LINE (src, 2, i >> 1),
            FRAME_GET_LINE (src, 2, (i >> 1) + 1), convert->width);
      } else {
        cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, 0, i),
            FRAME_GET_LINE (src, 0, i),
            FRAME_GET_LINE (src, 1, i >> 1),
            FRAME_GET_LINE (src, 2, i >> 1), convert->width);
      }
    }
  } else {
    for (i = 0; i < convert->height; i++) {
      cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, 0, i),
          FRAME_GET_LINE (src, 0, i),
          FRAME_GET_LINE (src, 1, i >> 1),
          FRAME_GET_LINE (src, 2, i >> 1), convert->width);
    }
  }
}
#endif



/* Fast paths */

typedef struct
{
  GstVideoFormat from_format;
  ColorSpaceColorSpec from_spec;
  GstVideoFormat to_format;
  ColorSpaceColorSpec to_spec;
  gboolean keeps_color_spec;
  void (*convert) (ColorspaceConvert * convert, guint8 * dest,
      const guint8 * src);
} ColorspaceTransform;
static const ColorspaceTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_YUY2,
      COLOR_SPEC_NONE, TRUE, convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_UYVY,
      COLOR_SPEC_NONE, TRUE, convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_AYUV,
      COLOR_SPEC_NONE, TRUE, convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y42B,
      COLOR_SPEC_NONE, TRUE, convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y444,
      COLOR_SPEC_NONE, TRUE, convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_I420,
      COLOR_SPEC_NONE, TRUE, convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, TRUE, convert_UYVY_YUY2},    /* alias */
  {GST_VIDEO_FORMAT_YUY2, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_AYUV,
      COLOR_SPEC_NONE, TRUE, convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y42B,
      COLOR_SPEC_NONE, TRUE, convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y444,
      COLOR_SPEC_NONE, TRUE, convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_I420,
      COLOR_SPEC_NONE, TRUE, convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_YUY2,
      COLOR_SPEC_NONE, TRUE, convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_AYUV,
      COLOR_SPEC_NONE, TRUE, convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y42B,
      COLOR_SPEC_NONE, TRUE, convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y444,
      COLOR_SPEC_NONE, TRUE, convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_I420,
      COLOR_SPEC_NONE, TRUE, convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_YUY2,
      COLOR_SPEC_NONE, TRUE, convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_UYVY,
      COLOR_SPEC_NONE, TRUE, convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y42B,
      COLOR_SPEC_NONE, TRUE, convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y444,
      COLOR_SPEC_NONE, TRUE, convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_I420,
      COLOR_SPEC_NONE, TRUE, convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_YUY2,
      COLOR_SPEC_NONE, TRUE, convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_UYVY,
      COLOR_SPEC_NONE, TRUE, convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_AYUV,
      COLOR_SPEC_NONE, TRUE, convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y444,
      COLOR_SPEC_NONE, TRUE, convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_I420,
      COLOR_SPEC_NONE, TRUE, convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_YUY2,
      COLOR_SPEC_NONE, TRUE, convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_UYVY,
      COLOR_SPEC_NONE, TRUE, convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_AYUV,
      COLOR_SPEC_NONE, TRUE, convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, COLOR_SPEC_NONE, GST_VIDEO_FORMAT_Y42B,
      COLOR_SPEC_NONE, TRUE, convert_Y444_Y42B},

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_ARGB,
      COLOR_SPEC_RGB, FALSE, convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_BGRA,
      COLOR_SPEC_RGB, FALSE, convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_xRGB, COLOR_SPEC_RGB, FALSE, convert_AYUV_ARGB},     /* alias */
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_BGRx, COLOR_SPEC_RGB, FALSE, convert_AYUV_BGRA},     /* alias */
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_ABGR,
      COLOR_SPEC_RGB, FALSE, convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_RGBA,
      COLOR_SPEC_RGB, FALSE, convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_xBGR, COLOR_SPEC_RGB, FALSE, convert_AYUV_ABGR},     /* alias */
  {GST_VIDEO_FORMAT_AYUV, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_RGBx, COLOR_SPEC_RGB, FALSE, convert_AYUV_RGBA},     /* alias */

  {GST_VIDEO_FORMAT_I420, COLOR_SPEC_YUV_BT470_6, GST_VIDEO_FORMAT_BGRA,
      COLOR_SPEC_RGB, FALSE, convert_I420_BGRA},
#endif
};

static void
colorspace_convert_lookup_fastpath (ColorspaceConvert * convert)
{
  int i;

  for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
    if (transforms[i].to_format == convert->to_format &&
        transforms[i].from_format == convert->from_format &&
        (transforms[i].keeps_color_spec ||
            (transforms[i].from_spec == convert->from_spec &&
                transforms[i].to_spec == convert->to_spec))) {
      convert->convert = transforms[i].convert;
      return;
    }
  }
}
