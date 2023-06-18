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
#ifndef G_OS_WIN32
#include <libdrm/drm_fourcc.h>
#endif

#define GST_CAT_DEFAULT gst_va_display_debug
GST_DEBUG_CATEGORY_EXTERN (gst_va_display_debug);

#define VA_NSB_FIRST 0          /* No Significant Bit  */

/* *INDENT-OFF* */
static struct FormatMap
{
  GstVideoFormat format;
  guint va_rtformat;
  VAImageFormat va_format;
  /* The drm fourcc may have different definition from VA */
  guint drm_fourcc;
} format_map[] = {
#ifndef G_OS_WIN32
#define F(format, drm, fourcc, rtformat, order, bpp, depth, r, g, b, a) { \
    G_PASTE (GST_VIDEO_FORMAT_, format),                                \
    G_PASTE (VA_RT_FORMAT_, rtformat),                             \
    { VA_FOURCC fourcc, G_PASTE (G_PASTE (VA_, order), _FIRST),    \
      bpp, depth, r, g, b, a }, G_PASTE (DRM_FORMAT_, drm) }
#else
#define F(format, drm, fourcc, rtformat, order, bpp, depth, r, g, b, a) { \
    G_PASTE (GST_VIDEO_FORMAT_, format),                                \
    G_PASTE (VA_RT_FORMAT_, rtformat),                             \
    { VA_FOURCC fourcc, G_PASTE (G_PASTE (VA_, order), _FIRST),    \
      bpp, depth, r, g, b, a }, 0 /* DRM_FORMAT_INVALID */ }
#endif
#define G(format, drm, fourcc, rtformat, order, bpp) \
    F (format, drm, fourcc, rtformat, order, bpp, 0, 0, 0 ,0, 0)
  G (NV12, NV12, ('N', 'V', '1', '2'), YUV420, NSB, 12),
  G (NV21, NV21, ('N', 'V', '2', '1'), YUV420, NSB, 21),
  G (VUYA, AYUV, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
  F (RGBA, RGBA8888, ('R', 'G', 'B', 'A'), RGB32, LSB, 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),
  F (RGBx, RGBX8888, ('R', 'G', 'B', 'X'), RGB32, LSB, 32, 24, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0x00000000),
  F (BGRA, BGRA8888, ('B', 'G', 'R', 'A'), RGB32, LSB, 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),
  F (ARGB, ARGB8888, ('A', 'R', 'G', 'B'), RGB32, LSB, 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),
  F (xRGB, XRGB8888, ('X', 'R', 'G', 'B'), RGB32, LSB, 32, 24, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x00000000),
  F (ABGR, ABGR8888, ('A', 'B', 'G', 'R'), RGB32, LSB, 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),
  F (xBGR, XBGR8888, ('X', 'B', 'G', 'R'), RGB32, LSB, 32, 24, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x00000000),
  F (BGRx, BGRX8888, ('B', 'G', 'R', 'X'), RGB32, LSB, 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),
  G (UYVY, UYVY, ('U', 'Y', 'V', 'Y'), YUV422, NSB, 16),
  G (YUY2, YUYV, ('Y', 'U', 'Y', '2'), YUV422, NSB, 16),
  G (AYUV, AYUV, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
  /* F (????, NV11), */
  G (YV12, YVU420, ('Y', 'V', '1', '2'), YUV420, NSB, 12),
  /* F (????, P208), */
  G (I420, YUV420, ('I', '4', '2', '0'), YUV420, NSB, 12),
  /* F (????, YV24), */
  /* F (????, YV32), */
  /* F (????, Y800), */
  /* F (????, IMC3), */
  /* F (????, 411P), */
  /* F (????, 411R), */
  G (Y42B, YUV422, ('4', '2', '2', 'H'), YUV422, LSB, 16),
  /* F (????, 422V), */
  /* F (????, 444P), */
  /* No RGBP support in drm fourcc */
  G (RGBP, INVALID, ('R', 'G', 'B', 'P'), RGBP, LSB, 8),
  /* F (????, BGRP), */
  /* F (????, RGB565), */
  /* F (????, BGR565), */
  G (Y210, Y210, ('Y', '2', '1', '0'), YUV422_10, NSB, 32),
  /* F (????, Y216), */
  G (Y410, Y410, ('Y', '4', '1', '0'), YUV444_10, NSB, 32),
  G (Y212_LE, Y212, ('Y', '2', '1', '2'), YUV422_12, NSB, 32),
  G (Y412_LE, Y412, ('Y', '4', '1', '2'), YUV444_12, NSB, 32),
  /* F (????, Y416), */
  /* F (????, YV16), */
  G (P010_10LE, P010, ('P', '0', '1', '0'), YUV420_10, NSB, 24),
  G (P012_LE, P012, ('P', '0', '1', '2'), YUV420_12, NSB, 24),
  /* F (P016_LE, P016, ????), */
  /* F (????, I010), */
  /* F (????, IYUV), */
  /* F (????, A2R10G10B10), */
  /* F (????, A2B10G10R10), */
  /* F (????, X2R10G10B10), */
  /* F (????, X2B10G10R10), */
  /* No GRAY8 support in drm fourcc */
  G (GRAY8, INVALID, ('Y', '8', '0', '0'), YUV400, NSB, 8),
  G (Y444, YUV444, ('4', '4', '4', 'P'), YUV444, NSB, 24),
  /* F (????, Y16), */
  /* G (VYUY, VYUY, YUV422), */
  /* G (YVYU, YVYU, YUV422), */
  /* F (ARGB64, ARGB64, ????), */
  /* F (????, ABGR64), */
  F (RGB16, RGB565, ('R', 'G', '1', '6'), RGB16, NSB, 16, 16, 0x0000f800,
     0x000007e0, 0x0000001f, 0x00000000),
  F (RGB, RGB888, ('R', 'G', '2', '4'), RGB32, NSB, 32, 24, 0x00ff0000,
     0x0000ff00, 0x000000ff, 0x00000000),
  F (BGR10A2_LE, ARGB2101010, ('A', 'R', '3', '0'), RGB32, LSB, 32, 30,
     0x3ff00000, 0x000ffc00, 0x000003ff, 0x30000000),
#undef F
#undef G
};

static const struct RBG32FormatMap
{
  GstVideoFormat format;
  guint drm_fourcc;
  VAImageFormat va_format[2];
} rgb32_format_map[] = {
#define  F(fourcc, order, bpp, depth, r, g, b, a)                       \
  {  VA_FOURCC fourcc, G_PASTE (G_PASTE (VA_, order), _FIRST), bpp, depth, r, g, b, a }
#define  A(fourcc, order, r, g, b, a) F (fourcc, order, 32, 32, r, g, b, a)
#define  X(fourcc, order, r, g, b) F (fourcc, order, 32, 24, r, g, b, 0x0)
#ifndef G_OS_WIN32
#define  D(format, drm_fourcc) G_PASTE (GST_VIDEO_FORMAT_, format), G_PASTE (DRM_FORMAT_, drm_fourcc)
#else
#define  D(format, drm_fourcc) G_PASTE (GST_VIDEO_FORMAT_, format), 0 /* DRM_FORMAT_INVALID */
#endif
  { D (ARGB, BGRA8888), {
      A (('B', 'G', 'R', 'A'), LSB, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff),
      A (('A', 'R', 'G', 'B'), MSB, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
    } },
  { D (RGBA, ABGR8888), {
      A (('A', 'B', 'G', 'R'), LSB, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
      A (('R', 'G', 'B', 'A'), MSB, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff),
    } },
  { D (ABGR, RGBA8888), {
      A (('R', 'G', 'B', 'A'), LSB, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff),
      A (('A', 'B', 'G', 'R'), MSB, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
    } },
  { D (BGRA, ARGB8888), {
      A (('A', 'R', 'G', 'B'), LSB, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
      A (('B', 'G', 'R', 'A'), MSB, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff),
    } },
  { D (xRGB, BGRX8888), {
      X (('B', 'G', 'R', 'X'), LSB, 0x0000ff00, 0x00ff0000, 0xff000000),
      X (('X', 'R', 'G', 'B'), MSB, 0x00ff0000, 0x0000ff00, 0x000000ff),
    } },
  { D (RGBx, XBGR8888), {
      X (('X', 'B', 'G', 'R'), LSB, 0x000000ff, 0x0000ff00, 0x00ff0000),
      X (('R', 'G', 'B', 'X'), MSB, 0xff000000, 0x00ff0000, 0x0000ff00),
    } },
  { D (xBGR, RGBX8888), {
      X (('R', 'G', 'B', 'X'), LSB, 0xff000000, 0x00ff0000, 0x0000ff00),
      X (('X', 'B', 'G', 'R'), MSB, 0x000000ff, 0x0000ff00, 0x00ff0000),
    } },
  { D (BGRx, XRGB8888), {
      X (('X', 'R', 'G', 'B'), LSB, 0x00ff0000, 0x0000ff00, 0x000000ff),
      X (('B', 'G', 'R', 'X'), MSB, 0x0000ff00, 0x00ff0000, 0xff000000),
    } },
#undef X
#undef A
#undef F
#undef D
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
get_format_map_from_drm_fourcc (guint drm_fourcc)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].drm_fourcc == drm_fourcc)
      return &format_map[i];
  }

  return NULL;
}

static struct FormatMap *
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
gst_va_video_format_from_va_fourcc (guint fourcc)
{
  const struct FormatMap *map = get_format_map_from_va_fourcc (fourcc);

  return map ? map->format : GST_VIDEO_FORMAT_UNKNOWN;
}

guint
gst_va_fourcc_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? map->va_format.fourcc : 0;
}

GstVideoFormat
gst_va_video_format_from_drm_fourcc (guint fourcc)
{
  const struct FormatMap *map = get_format_map_from_drm_fourcc (fourcc);

  return map ? map->format : GST_VIDEO_FORMAT_UNKNOWN;
}

guint
gst_va_drm_fourcc_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? map->drm_fourcc : 0;
}

guint
gst_va_chroma_from_video_format (GstVideoFormat format)
{
  const struct FormatMap *map = get_format_map_from_video_format (format);

  return map ? map->va_rtformat : 0;
}

guint
gst_va_chroma_from_va_fourcc (guint va_fourcc)
{
  const struct FormatMap *map = get_format_map_from_va_fourcc (va_fourcc);

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

/*
 * XXX: Not all the surfaces formats can be converted into every image
 * format when mapped. This funtion will return the #GstVideoFormat
 * that a surface will map when it is asked for a @image_format.
 *
 * Current implementation only seeks for @image_format in
 * @surface_formats.
 */
GstVideoFormat
gst_va_video_surface_format_from_image_format (GstVideoFormat image_format,
    GArray * surface_formats)
{
  GstVideoFormat surface_format;
  guint i, image_chroma;

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
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

/* Convert the GstVideoInfoDmaDrm into a traditional GstVideoInfo
   with recognized format. */
gboolean
gst_va_dma_drm_info_to_video_info (const GstVideoInfoDmaDrm * drm_info,
    GstVideoInfo * info)
{
  GstVideoFormat video_format;
  GstVideoInfo tmp_info;
  guint i;

  g_return_val_if_fail (drm_info, FALSE);
  g_return_val_if_fail (info, FALSE);

  if (GST_VIDEO_INFO_FORMAT (&drm_info->vinfo) != GST_VIDEO_FORMAT_ENCODED) {
    *info = drm_info->vinfo;
    return TRUE;
  }

  /* The non linear DMA format will be recognized as FORMAT_ENCODED,
     but we still need to know its real format to set the info such
     as pitch and stride. Because va plugins have its internal mapping
     between drm fourcc and video format, we do not use the standard
     conversion API here. */
  video_format = gst_va_video_format_from_drm_fourcc (drm_info->drm_fourcc);
  if (video_format == GST_VIDEO_FORMAT_UNKNOWN)
    return FALSE;

  if (!gst_video_info_set_format (&tmp_info, video_format,
          GST_VIDEO_INFO_WIDTH (&drm_info->vinfo),
          GST_VIDEO_INFO_HEIGHT (&drm_info->vinfo)))
    return FALSE;

  *info = drm_info->vinfo;
  info->finfo = tmp_info.finfo;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    info->stride[i] = tmp_info.stride[i];
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    info->offset[i] = tmp_info.offset[i];
  info->size = tmp_info.size;

  return TRUE;
}

static GstVideoFormat
find_gst_video_format_in_rgb32_map (VAImageFormat * image_format,
    guint * drm_fourcc)
{
  guint i, j;

  for (i = 0; i < G_N_ELEMENTS (rgb32_format_map); i++) {
    for (j = 0; j < G_N_ELEMENTS (rgb32_format_map[i].va_format); j++) {
      if (va_format_is_same (&rgb32_format_map[i].va_format[j], image_format)) {
        *drm_fourcc = rgb32_format_map[i].drm_fourcc;
        return rgb32_format_map[i].format;
      }
    }
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

struct ImageFormatArray
{
  VAImageFormat *image_formats;
  gint len;
};

static gpointer
fix_map (gpointer data)
{
  struct ImageFormatArray *args = data;
  GstVideoFormat format;
  VAImageFormat *image_format;
  struct FormatMap *map;
  guint drm_fourcc = 0;
  guint i;

  for (i = 0; i < args->len; i++) {
    image_format = &args->image_formats[i];
    if (!va_format_is_rgb (image_format))
      continue;
    format = find_gst_video_format_in_rgb32_map (image_format, &drm_fourcc);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;
    map = get_format_map_from_video_format (format);
    if (!map)
      continue;
    if (va_format_is_same (&map->va_format, image_format))
      continue;

    map->va_format = *image_format;
    map->drm_fourcc = drm_fourcc;

    GST_INFO ("GST_VIDEO_FORMAT_%s => { fourcc %" GST_FOURCC_FORMAT ", "
        "drm fourcc %" GST_FOURCC_FORMAT ", %s, bpp %d, depth %d, "
        "R %#010x, G %#010x, B %#010x, A %#010x }",
        gst_video_format_to_string (map->format),
        GST_FOURCC_ARGS (map->va_format.fourcc),
        GST_FOURCC_ARGS (map->drm_fourcc),
        (map->va_format.byte_order == 1) ? "LSB" : "MSB",
        map->va_format.bits_per_pixel, map->va_format.depth,
        map->va_format.red_mask, map->va_format.green_mask,
        map->va_format.blue_mask, map->va_format.alpha_mask);
  }

  return NULL;
}

/* XXX: RGB32 LSB VAImageFormats don't map statically with GStreamer
 * color formats. Each driver does what they want.
 *
 * For MSB, there is no ambiguity: same order in define, memory and
 * CPU. For example,
 *
 *  RGBA is RGBA in memory and RGBA with channel mask R:0xFF0000
 *  G:0x00FF0000 B:0x0000FF00 A:0x000000FF in CPU.
 *
 * For LSB, CPU's perspective and memory's perspective are
 * different. For example,
 *
 *  From CPU's perspective, it's RGBA order in memory, but when it is
 *  stored in memory, because CPU's little endianness, it will be
 *  re-ordered, with mask R:0x000000FF G:0x0000FF00 B:0x00FF0000
 *  A:0xFF000000.
 *
 *  In other words, from memory's perspective, RGBA LSB is equal as
 *  ABGR MSB.
 *
 * These definitions are mixed used all over the media system and we
 * need to correct the mapping form VA video format to GStreamer
 * video format in both manners.
 *
 * https://gitlab.freedesktop.org/gstreamer/gstreamer-vaapi/-/merge_requests/123
 */
void
gst_va_video_format_fix_map (VAImageFormat * image_formats, gint num)
{
  static GOnce once = G_ONCE_INIT;
  struct ImageFormatArray args = { image_formats, num };

  g_once (&once, fix_map, &args);
}
