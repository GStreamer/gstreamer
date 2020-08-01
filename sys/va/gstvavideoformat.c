/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gstvavideoformat.h"

#define VA_NSB_FIRST 0          /* No Significant Bit  */

/* XXX(victor): add RGB fourcc runtime checkups for screwed drivers */
/* *INDENT-OFF* */
static const struct FormatMap
{
  GstVideoFormat format;
  guint va_rtformat;
  VAImageFormat va_format;
} format_map[] = {
#define F(format, fourcc, rtformat, order, bpp, depth, r, g, b, a) {      \
    G_PASTE (GST_VIDEO_FORMAT_, format),                                \
    G_PASTE (VA_RT_FORMAT_, rtformat),                             \
    { VA_FOURCC fourcc, G_PASTE (G_PASTE (VA_, order), _FIRST),    \
      bpp, depth, r, g, b, a } }
#define G(format, fourcc, rtformat, order, bpp) \
    F (format, fourcc, rtformat, order, bpp, 0, 0, 0 ,0, 0)
  G (NV12, ('N', 'V', '1', '2'), YUV420, NSB, 12),
  G (NV21, ('N', 'V', '2', '1'), YUV420, NSB, 21),
  G (VUYA, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
  F (RGBA, ('R', 'G', 'B', 'A'), RGB32, LSB, 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),
  /* F (????, RGBX), */
  F (BGRA, ('B', 'G', 'R', 'A'), RGB32, LSB, 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),
  F (ARGB, ('A', 'R', 'G', 'B'), RGB32, LSB, 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),
  /* F (????, XRGB), */
  F (ABGR, ('A', 'B', 'G', 'R'), RGB32, LSB, 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),
  /* F (????, XBGR), */
  G (UYVY, ('U', 'Y', 'V', 'Y'), YUV422, NSB, 16),
  G (YUY2, ('Y', 'U', 'Y', '2'), YUV422, NSB, 16),
  G (AYUV, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
  /* F (????, NV11), */
  G (YV12, ('Y', 'V', '1', '2'), YUV420, NSB, 12),
  /* F (????, P208), */
  G (I420, ('I', '4', '2', '0'), YUV420, NSB, 12),
  /* F (????, YV24), */
  /* F (????, YV32), */
  /* F (????, Y800), */
  /* F (????, IMC3), */
  /* F (????, 411P), */
  /* F (????, 411R), */
  /* F (????, 422H), */
  /* F (????, 422V), */
  /* F (????, 444P), */
  /* F (????, RGBP), */
  /* F (????, BGRP), */
  /* F (????, RGB565), */
  /* F (????, BGR565), */
  G (Y210, ('Y', '2', '1', '0'), YUV422_10, NSB, 32),
  /* F (????, Y216), */
  G (Y410, ('Y', '4', '1', '0'), YUV444_10, NSB, 32),
  /* F (????, Y416), */
  /* F (????, YV16), */
  G (P010_10LE, ('P', '0', '1', '0'), YUV420_10, NSB, 24),
  /* F (P016_LE, P016, ????), */
  /* F (????, I010), */
  /* F (????, IYUV), */
  /* F (????, A2R10G10B10), */
  /* F (????, A2B10G10R10), */
  /* F (????, X2R10G10B10), */
  /* F (????, X2B10G10R10), */
  G (GRAY8, ('Y', '8', '0', '0'), YUV400, NSB, 8),
  /* F (????, Y16), */
  /* G (VYUY, VYUY, YUV422), */
  /* G (YVYU, YVYU, YUV422), */
  /* F (ARGB64, ARGB64, ????), */
  /* F (????, ABGR64), */
#undef F
#undef G
};
/* *INDENT-ON* */

static const struct FormatMap *
get_format_map_from_va_fourcc (guint va_fourcc)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].va_format.fourcc == va_fourcc)
      return &format_map[i];
  }

  return NULL;
}

static const struct FormatMap *
get_format_map_from_video_format (GstVideoFormat format)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == format)
      return &format_map[i];
  }

  return NULL;
}

static inline gboolean
va_format_is_rgb (const VAImageFormat * va_format)
{
  return va_format->depth != 0;
}

static inline gboolean
va_format_is_same_rgb (const VAImageFormat * fmt1, const VAImageFormat * fmt2)
{
  return (fmt1->red_mask == fmt2->red_mask
      && fmt1->green_mask == fmt2->green_mask
      && fmt1->blue_mask == fmt2->blue_mask
      && fmt1->alpha_mask == fmt2->alpha_mask);
}

static inline gboolean
va_format_is_same (const VAImageFormat * fmt1, const VAImageFormat * fmt2)
{
  if (fmt1->fourcc != fmt2->fourcc)
    return FALSE;
  if (fmt1->byte_order != VA_NSB_FIRST
      && fmt2->byte_order != VA_NSB_FIRST
      && fmt1->byte_order != fmt2->byte_order)
    return FALSE;
  return va_format_is_rgb (fmt1) ? va_format_is_same_rgb (fmt1, fmt2) : TRUE;
}

static const struct FormatMap *
get_format_map_from_va_image_format (const VAImageFormat * va_format)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (va_format_is_same (&format_map[i].va_format, va_format))
      return &format_map[i];
  }

  return NULL;
}

GstVideoFormat
gst_va_video_format_from_va_fourcc (guint va_fourcc)
{
  const struct FormatMap *map = get_format_map_from_va_fourcc (va_fourcc);

  return map ? map->format : GST_VIDEO_FORMAT_UNKNOWN;
}

guint
gst_va_fourcc_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? map->va_format.fourcc : 0;
}

guint
gst_va_chroma_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? map->va_rtformat : 0;
}

const VAImageFormat *
gst_va_image_format_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? &map->va_format : NULL;
}

GstVideoFormat
gst_va_video_format_from_va_image_format (const VAImageFormat * va_format)
{
  const struct FormatMap *map = get_format_map_from_va_image_format (va_format);

  return map ? map->format : GST_VIDEO_FORMAT_UNKNOWN;
}

GstVideoFormat
gst_va_video_surface_format_from_image_format (GstVideoFormat image_format,
    GArray * surface_formats)
{
  GstVideoFormat surface_format;
  guint i, image_chroma, surface_chroma;

  if (image_format == GST_VIDEO_FORMAT_UNKNOWN)
    return GST_VIDEO_FORMAT_UNKNOWN;

  if (!surface_formats || surface_formats->len == 0)
    return GST_VIDEO_FORMAT_UNKNOWN;

  image_chroma = gst_va_chroma_from_video_format (image_format);
  if (image_chroma == 0)
    return GST_VIDEO_FORMAT_UNKNOWN;

  for (i = 0; i < surface_formats->len; i++) {
    surface_format = g_array_index (surface_formats, GstVideoFormat, i);

    if (surface_format == image_format)
      return surface_format;

    surface_chroma = gst_va_chroma_from_video_format (surface_format);

    if (surface_chroma == image_chroma)
      return surface_format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}
