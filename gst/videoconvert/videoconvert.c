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

#include "videoconvert.h"
#include <glib.h>
#include <string.h>
#include "gstvideoconvertorc.h"


static void videoconvert_convert_generic (VideoConvert * convert,
    GstVideoFrame * dest, const GstVideoFrame * src);
static void videoconvert_convert_lookup_fastpath (VideoConvert * convert);
static void videoconvert_convert_lookup_matrix (VideoConvert * convert);
static void videoconvert_dither_none (VideoConvert * convert, int j);
static void videoconvert_dither_verterr (VideoConvert * convert, int j);
static void videoconvert_dither_halftone (VideoConvert * convert, int j);


VideoConvert *
videoconvert_convert_new (GstVideoFormat to_format, ColorSpaceColorSpec to_spec,
    GstVideoFormat from_format, ColorSpaceColorSpec from_spec,
    int width, int height)
{
  const GstVideoFormatInfo *to_info, *from_info;
  VideoConvert *convert;
  int i;

  from_info = gst_video_format_get_info (from_format);
  to_info = gst_video_format_get_info (to_format);

  g_return_val_if_fail (!GST_VIDEO_FORMAT_INFO_IS_RGB (to_info)
      || to_spec == COLOR_SPEC_RGB, NULL);
  g_return_val_if_fail (!GST_VIDEO_FORMAT_INFO_IS_YUV (to_info)
      || to_spec == COLOR_SPEC_YUV_BT709
      || to_spec == COLOR_SPEC_YUV_BT470_6, NULL);
  g_return_val_if_fail (GST_VIDEO_FORMAT_INFO_IS_RGB (to_info)
      || GST_VIDEO_FORMAT_INFO_IS_YUV (to_info)
      || (GST_VIDEO_FORMAT_INFO_IS_GRAY (to_info) &&
          to_spec == COLOR_SPEC_GRAY), NULL);

  g_return_val_if_fail (!GST_VIDEO_FORMAT_INFO_IS_RGB (from_info)
      || from_spec == COLOR_SPEC_RGB, NULL);
  g_return_val_if_fail (!GST_VIDEO_FORMAT_INFO_IS_YUV (from_info)
      || from_spec == COLOR_SPEC_YUV_BT709
      || from_spec == COLOR_SPEC_YUV_BT470_6, NULL);
  g_return_val_if_fail (GST_VIDEO_FORMAT_INFO_IS_RGB (from_info)
      || GST_VIDEO_FORMAT_INFO_IS_YUV (from_info)
      || (GST_VIDEO_FORMAT_INFO_IS_GRAY (from_info) &&
          from_spec == COLOR_SPEC_GRAY), NULL);

  convert = g_malloc (sizeof (VideoConvert));
  memset (convert, 0, sizeof (VideoConvert));

  convert->to_format = to_format;
  convert->to_spec = to_spec;
  convert->from_format = from_format;
  convert->from_spec = from_spec;
  convert->height = height;
  convert->width = width;
  convert->convert = videoconvert_convert_generic;
  convert->dither16 = videoconvert_dither_none;

  if (to_info->depth[0] > 8 || from_info->depth[0] > 8) {
    convert->use_16bit = TRUE;
  } else {
    convert->use_16bit = FALSE;
  }

  videoconvert_convert_lookup_fastpath (convert);
  videoconvert_convert_lookup_matrix (convert);

  convert->tmpline = g_malloc (sizeof (guint8) * (width + 8) * 4);
  convert->tmpline16 = g_malloc (sizeof (guint16) * (width + 8) * 4);
  convert->errline = g_malloc (sizeof (guint16) * width * 4);

  if (to_format == GST_VIDEO_FORMAT_RGB8P) {
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
videoconvert_convert_free (VideoConvert * convert)
{
  g_free (convert->palette);
  g_free (convert->tmpline);
  g_free (convert->tmpline16);
  g_free (convert->errline);

  g_free (convert);
}

void
videoconvert_convert_set_interlaced (VideoConvert * convert,
    gboolean interlaced)
{
  convert->interlaced = interlaced;
}

void
videoconvert_convert_set_dither (VideoConvert * convert, int type)
{
  switch (type) {
    case 0:
    default:
      convert->dither16 = videoconvert_dither_none;
      break;
    case 1:
      convert->dither16 = videoconvert_dither_verterr;
      break;
    case 2:
      convert->dither16 = videoconvert_dither_halftone;
      break;
  }
}

void
videoconvert_convert_set_palette (VideoConvert * convert,
    const guint32 * palette)
{
  if (convert->palette == NULL) {
    convert->palette = g_malloc (sizeof (guint32) * 256);
  }
  memcpy (convert->palette, palette, sizeof (guint32) * 256);
}

const guint32 *
videoconvert_convert_get_palette (VideoConvert * convert)
{
  return convert->palette;
}

void
videoconvert_convert_convert (VideoConvert * convert,
    GstVideoFrame * dest, const GstVideoFrame * src)
{
  convert->convert (convert, dest, src);
}

static void
matrix_rgb_to_yuv_bt470_6 (VideoConvert * convert)
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
matrix_rgb_to_yuv_bt709 (VideoConvert * convert)
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
matrix_yuv_bt470_6_to_rgb (VideoConvert * convert)
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
matrix_yuv_bt709_to_rgb (VideoConvert * convert)
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
matrix_yuv_bt709_to_yuv_bt470_6 (VideoConvert * convert)
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
matrix_yuv_bt470_6_to_yuv_bt709 (VideoConvert * convert)
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
matrix_identity (VideoConvert * convert)
{
  /* do nothing */
}

static void
matrix16_rgb_to_yuv_bt470_6 (VideoConvert * convert)
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
matrix16_rgb_to_yuv_bt709 (VideoConvert * convert)
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
matrix16_yuv_bt470_6_to_rgb (VideoConvert * convert)
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
matrix16_yuv_bt709_to_rgb (VideoConvert * convert)
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
matrix16_yuv_bt709_to_yuv_bt470_6 (VideoConvert * convert)
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
matrix16_yuv_bt470_6_to_yuv_bt709 (VideoConvert * convert)
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
matrix16_identity (VideoConvert * convert)
{
  /* do nothing */
}



static void
videoconvert_convert_lookup_matrix (VideoConvert * convert)
{
  if (convert->from_spec == convert->to_spec) {
    GST_DEBUG ("using identity matrix");
    convert->matrix = matrix_identity;
    convert->matrix16 = matrix16_identity;
  } else if (convert->from_spec == COLOR_SPEC_RGB
      && convert->to_spec == COLOR_SPEC_YUV_BT470_6) {
    GST_DEBUG ("using RGB -> YUV BT470_6 matrix");
    convert->matrix = matrix_rgb_to_yuv_bt470_6;
    convert->matrix16 = matrix16_rgb_to_yuv_bt470_6;
  } else if (convert->from_spec == COLOR_SPEC_RGB
      && convert->to_spec == COLOR_SPEC_YUV_BT709) {
    GST_DEBUG ("using RGB -> YUV BT709 matrix");
    convert->matrix = matrix_rgb_to_yuv_bt709;
    convert->matrix16 = matrix16_rgb_to_yuv_bt709;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT470_6
      && convert->to_spec == COLOR_SPEC_RGB) {
    GST_DEBUG ("using YUV BT470_6 -> RGB matrix");
    convert->matrix = matrix_yuv_bt470_6_to_rgb;
    convert->matrix16 = matrix16_yuv_bt470_6_to_rgb;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT709
      && convert->to_spec == COLOR_SPEC_RGB) {
    GST_DEBUG ("using YUV BT709 -> RGB matrix");
    convert->matrix = matrix_yuv_bt709_to_rgb;
    convert->matrix16 = matrix16_yuv_bt709_to_rgb;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT709
      && convert->to_spec == COLOR_SPEC_YUV_BT470_6) {
    GST_DEBUG ("using YUV BT709 -> YUV BT470_6");
    convert->matrix = matrix_yuv_bt709_to_yuv_bt470_6;
    convert->matrix16 = matrix16_yuv_bt709_to_yuv_bt470_6;
  } else if (convert->from_spec == COLOR_SPEC_YUV_BT470_6
      && convert->to_spec == COLOR_SPEC_YUV_BT709) {
    GST_DEBUG ("using YUV BT470_6 -> YUV BT709");
    convert->matrix = matrix_yuv_bt470_6_to_yuv_bt709;
    convert->matrix16 = matrix16_yuv_bt470_6_to_yuv_bt709;
  } else {
    GST_DEBUG ("using identity matrix");
    convert->matrix = matrix_identity;
    convert->matrix16 = matrix16_identity;
  }
}

static void
videoconvert_dither_none (VideoConvert * convert, int j)
{
}

static void
videoconvert_dither_verterr (VideoConvert * convert, int j)
{
  int i;
  guint16 *tmpline = convert->tmpline16;
  guint16 *errline = convert->errline;
  unsigned int mask = 0xff;

  for (i = 0; i < 4 * convert->width; i++) {
    int x = tmpline[i] + errline[i];
    if (x > 65535)
      x = 65535;
    tmpline[i] = x;
    errline[i] = x & mask;
  }
}

static void
videoconvert_dither_halftone (VideoConvert * convert, int j)
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
    int x;
    x = tmpline[i] + halftone[(i >> 2) & 7][j & 7];
    if (x > 65535)
      x = 65535;
    tmpline[i] = x;
  }
}

#define TO_16(x) (((x)<<8) | (x))

#define UNPACK_FRAME(frame,dest,line,width) \
  frame->info.finfo->unpack_func (frame->info.finfo, GST_VIDEO_PACK_FLAG_NONE, \
      dest, frame->data, frame->info.stride, 0, line, width)
#define PACK_FRAME(frame,dest,line,width) \
  frame->info.finfo->pack_func (frame->info.finfo, GST_VIDEO_PACK_FLAG_NONE, \
      dest, 0, frame->data, frame->info.stride, frame->info.chroma_site, line, width);

static void
videoconvert_convert_generic (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i, j;
  const GstVideoFormatInfo *sfinfo, *dfinfo;
  gint width, height;
  guint src_bits, dest_bits;

  sfinfo = src->info.finfo;
  dfinfo = dest->info.finfo;

  src_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (sfinfo->unpack_format), 0);
  dest_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (dfinfo->unpack_format), 0);

  height = convert->height;
  width = convert->width;

  if (sfinfo->unpack_func == NULL) {
    GST_ERROR ("no unpack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (src)));
    return;
  }

  if (dfinfo->pack_func == NULL) {
    GST_ERROR ("no pack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (dest)));
    return;
  }

  for (j = 0; j < height; j++) {
    if (src_bits == 16) {
      UNPACK_FRAME (src, convert->tmpline16, j, width);
    } else {
      UNPACK_FRAME (src, convert->tmpline, j, width);

      if (dest_bits == 16)
        for (i = 0; i < width * 4; i++)
          convert->tmpline16[i] = TO_16 (convert->tmpline[i]);
    }

    if (dest_bits == 16 || src_bits == 16) {
      convert->matrix16 (convert);
      convert->dither16 (convert, j);
    } else {
      convert->matrix (convert);
    }

    if (dest_bits == 16) {
      PACK_FRAME (dest, convert->tmpline16, j, width);
    } else {
      if (src_bits == 16)
        for (i = 0; i < width * 4; i++)
          convert->tmpline[i] = convert->tmpline16[i] >> 8;

      PACK_FRAME (dest, convert->tmpline, j, width);
    }
  }
}

#define FRAME_GET_PLANE_STRIDE(frame, plane) \
  GST_VIDEO_FRAME_PLANE_STRIDE (frame, plane)
#define FRAME_GET_PLANE_LINE(frame, plane, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, plane))) + \
      FRAME_GET_PLANE_STRIDE (frame, plane) * (line))

#define FRAME_GET_COMP_STRIDE(frame, comp) \
  GST_VIDEO_FRAME_COMP_STRIDE (frame, comp)
#define FRAME_GET_COMP_LINE(frame, comp, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_COMP_DATA (frame, comp))) + \
      FRAME_GET_COMP_STRIDE (frame, comp) * (line))

#define FRAME_GET_STRIDE(frame)      FRAME_GET_PLANE_STRIDE (frame, 0)
#define FRAME_GET_LINE(frame,line)   FRAME_GET_PLANE_LINE (frame, 0, line)

#define FRAME_GET_Y_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_Y, line)
#define FRAME_GET_U_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_U, line)
#define FRAME_GET_V_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_V, line)
#define FRAME_GET_A_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_A, line)

#define FRAME_GET_Y_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_Y)
#define FRAME_GET_U_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_U)
#define FRAME_GET_V_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_V)
#define FRAME_GET_A_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_A)

/* Fast paths */

static void
convert_I420_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_YUY2 (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_UYVY (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_AYUV (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1), width);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_420_422 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_420_422 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);
}

static void
convert_I420_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_420_444 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_420_444 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i, h;
  gint width = convert->width;
  gint height = convert->height;

  h = height;
  if (width & 1)
    h--;

  for (i = 0; i < h; i += 2) {
    cogorc_convert_YUY2_I420 (FRAME_GET_Y_LINE (dest, i),
        FRAME_GET_Y_LINE (dest, i + 1),
        FRAME_GET_U_LINE (dest, i >> 1),
        FRAME_GET_V_LINE (dest, i >> 1),
        FRAME_GET_LINE (src, i), FRAME_GET_LINE (src, i + 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_Y444 (FRAME_GET_COMP_LINE (dest, 0, 0),
      FRAME_GET_COMP_STRIDE (dest, 0), FRAME_GET_COMP_LINE (dest, 1, 0),
      FRAME_GET_COMP_STRIDE (dest, 1), FRAME_GET_COMP_LINE (dest, 2, 0),
      FRAME_GET_COMP_STRIDE (dest, 2), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}


static void
convert_UYVY_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_UYVY_I420 (FRAME_GET_COMP_LINE (dest, 0, i),
        FRAME_GET_COMP_LINE (dest, 0, i + 1),
        FRAME_GET_COMP_LINE (dest, 1, i >> 1),
        FRAME_GET_COMP_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, i), FRAME_GET_LINE (src, i + 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_UYVY_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_UYVY_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_AYUV_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_I420 (FRAME_GET_Y_LINE (dest, 0),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (dest, 1),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      2 * FRAME_GET_STRIDE (src), FRAME_GET_LINE (src, 1),
      2 * FRAME_GET_STRIDE (src), width / 2, height / 2);
}

static void
convert_AYUV_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_AYUV_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_Y42B_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_422_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_422_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y42B_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_422_444 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  cogorc_planar_chroma_422_444 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width) / 2, height);
}

static void
convert_Y444_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_444_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_444_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y444_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_444_422 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  cogorc_planar_chroma_444_422 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width, height);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static void
convert_AYUV_ARGB (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_BGRA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_ABGR (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_RGBA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_I420_BGRA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  int quality = 0;
  gint width = convert->width;
  gint height = convert->height;

  if (quality > 3) {
    for (i = 0; i < height; i++) {
      if (i & 1) {
        cogorc_convert_I420_BGRA_avg (FRAME_GET_LINE (dest, i),
            FRAME_GET_Y_LINE (src, i),
            FRAME_GET_U_LINE (src, i >> 1),
            FRAME_GET_U_LINE (src, (i >> 1) + 1),
            FRAME_GET_V_LINE (src, i >> 1),
            FRAME_GET_V_LINE (src, (i >> 1) + 1), width);
      } else {
        cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
            FRAME_GET_Y_LINE (src, i),
            FRAME_GET_U_LINE (src, i >> 1),
            FRAME_GET_V_LINE (src, i >> 1), width);
      }
    }
  } else {
    for (i = 0; i < height; i++) {
      cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
          FRAME_GET_Y_LINE (src, i),
          FRAME_GET_U_LINE (src, i >> 1),
          FRAME_GET_V_LINE (src, i >> 1), width);
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
  void (*convert) (VideoConvert * convert, GstVideoFrame * dest,
      const GstVideoFrame * src);
} VideoTransform;
static const VideoTransform transforms[] = {
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
videoconvert_convert_lookup_fastpath (VideoConvert * convert)
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
