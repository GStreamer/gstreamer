/*
 *  video-format.h - Video format helpers for VA-API
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:videoformat
 * @short_description: Video format helpers for VA-API
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapisurface.h"
#include "video-format.h"

typedef struct
{
  GstVideoFormat format;
  GstVaapiChromaType chroma_type;
  VAImageFormat va_format;
} GstVideoFormatMap;

#define DEF_YUV(FORMAT, FOURCC, ENDIAN, BPP, SUB)                       \
  { G_PASTE(GST_VIDEO_FORMAT_,FORMAT),                                  \
    G_PASTE(GST_VAAPI_CHROMA_TYPE_YUV,SUB),                             \
    { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, }, }

#define DEF_RGB(FORMAT, FOURCC, ENDIAN, BPP, DEPTH, R,G,B,A)            \
  { G_PASTE(GST_VIDEO_FORMAT_,FORMAT),                                  \
    G_PASTE(GST_VAAPI_CHROMA_TYPE_RGB,BPP),                             \
    { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, DEPTH, R,G,B,A }, }

/* Image formats, listed in HW order preference */
/* *INDENT-OFF* */
static const GstVideoFormatMap gst_vaapi_video_formats[] = {
  DEF_YUV (NV12, ('N', 'V', '1', '2'), LSB, 12, 420),
  DEF_YUV (YV12, ('Y', 'V', '1', '2'), LSB, 12, 420),
  DEF_YUV (I420, ('I', '4', '2', '0'), LSB, 12, 420),
  DEF_YUV (YUY2, ('Y', 'U', 'Y', '2'), LSB, 16, 422),
  DEF_YUV (UYVY, ('U', 'Y', 'V', 'Y'), LSB, 16, 422),
  DEF_YUV (AYUV, ('A', 'Y', 'U', 'V'), LSB, 32, 444),
#if G_BYTE_ORDER == G_BIG_ENDIAN
  DEF_RGB (ARGB, ('A', 'R', 'G', 'B'), MSB, 32,
      32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
  DEF_RGB (ABGR, ('A', 'B', 'G', 'R'), MSB, 32,
      32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
  DEF_RGB (xRGB, ('X', 'R', 'G', 'B'), MSB, 32,
      24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000),
  DEF_RGB (xBGR, ('X', 'B', 'G', 'R'), MSB, 32,
      24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000),
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
  DEF_RGB (BGRA, ('B', 'G', 'R', 'A'), LSB, 32,
      32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
  DEF_RGB (RGBA, ('R', 'G', 'B', 'A'), LSB, 32,
      32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
  DEF_RGB (BGRx, ('B', 'G', 'R', 'X'), LSB, 32,
      24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000),
  DEF_RGB (RGBx, ('R', 'G', 'B', 'X'), LSB, 32,
      24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000),
#endif
  DEF_YUV (GRAY8, ('Y', '8', '0', '0'), LSB, 8, 400),
  DEF_YUV (P010_10LE, ('P', '0', '1', '0'), LSB, 24, 420_10BPP),
  {0,}
};
/* *INDENT-ON* */

#undef DEF_RGB
#undef DEF_YUV

static inline gboolean
va_format_is_rgb (const VAImageFormat * va_format)
{
  return va_format->depth != 0;
}

static inline gboolean
va_format_is_yuv (const VAImageFormat * va_format)
{
  return va_format->depth == 0;
}

static inline gboolean
va_format_is_same_rgb (const VAImageFormat * fmt1, const VAImageFormat * fmt2)
{
  return (fmt1->byte_order == fmt2->byte_order &&
      fmt1->red_mask == fmt2->red_mask &&
      fmt1->green_mask == fmt2->green_mask &&
      fmt1->blue_mask == fmt2->blue_mask &&
      fmt1->alpha_mask == fmt2->alpha_mask);
}

static inline gboolean
va_format_is_same (const VAImageFormat * fmt1, const VAImageFormat * fmt2)
{
  if (fmt1->fourcc != fmt2->fourcc)
    return FALSE;
  return va_format_is_rgb (fmt1) ? va_format_is_same_rgb (fmt1, fmt2) : TRUE;
}

static const GstVideoFormatMap *
get_map (GstVideoFormat format)
{
  const GstVideoFormatMap *m;

  for (m = gst_vaapi_video_formats; m->format; m++) {
    if (m->format == format)
      return m;
  }
  return NULL;
}

/**
 * gst_vaapi_video_format_to_string:
 * @format: a #GstVideoFormat
 *
 * Returns the string representation of the @format argument.
 *
 * Return value: string representation of @format, or %NULL if unknown
 *   or unsupported.
 */
const gchar *
gst_vaapi_video_format_to_string (GstVideoFormat format)
{
  return gst_video_format_to_string (format);
}

/**
 * gst_vaapi_video_format_is_rgb:
 * @format: a #GstVideoFormat
 *
 * Checks whether the format is an RGB format.
 *
 * Return value: %TRUE if @format is RGB format
 */
gboolean
gst_vaapi_video_format_is_rgb (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map (format);

  return m && va_format_is_rgb (&m->va_format);
}

/**
 * gst_vaapi_video_format_is_yuv:
 * @format: a #GstVideoFormat
 *
 * Checks whether the format is an YUV format.
 *
 * Return value: %TRUE if @format is YUV format
 */
gboolean
gst_vaapi_video_format_is_yuv (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map (format);

  return m && va_format_is_yuv (&m->va_format);
}

/**
 * gst_vaapi_video_format_from_va_fourcc:
 * @fourcc: a FOURCC value
 *
 * Converts a VA fourcc into the corresponding #GstVideoFormat. If no
 * matching fourcc was found, then zero is returned.
 *
 * Return value: the #GstVideoFormat corresponding to the VA @fourcc
 */
GstVideoFormat
gst_vaapi_video_format_from_va_fourcc (guint32 fourcc)
{
  const GstVideoFormatMap *m;

  /* Note: VA fourcc values are now standardized and shall represent
     a unique format. The associated VAImageFormat is just a hint to
     determine RGBA component ordering */
  for (m = gst_vaapi_video_formats; m->format; m++) {
    if (m->va_format.fourcc == fourcc)
      return m->format;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_vaapi_video_format_from_va_format:
 * @va_format: a #VAImageFormat
 *
 * Converts a VA image format into the corresponding #GstVideoFormat.
 * If the image format cannot be represented by #GstVideoFormat,
 * then zero is returned.
 *
 * Return value: the #GstVideoFormat describing the @va_format
 */
GstVideoFormat
gst_vaapi_video_format_from_va_format (const VAImageFormat * va_format)
{
  const GstVideoFormatMap *m;

  for (m = gst_vaapi_video_formats; m->format; m++) {
    if (va_format_is_same (&m->va_format, va_format))
      return m->format;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_vaapi_video_format_to_va_format:
 * @format: a #GstVideoFormat
 *
 * Converts a #GstVideoFormat into the corresponding VA image
 * format. If no matching VA image format was found, %NULL is returned
 * and this error must be reported to be fixed.
 *
 * Return value: the VA image format, or %NULL if none was found
 */
const VAImageFormat *
gst_vaapi_video_format_to_va_format (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map (format);

  return m ? &m->va_format : NULL;
}

/**
 * gst_vaapi_video_format_get_chroma_type:
 * @format: a #GstVideoFormat
 *
 * Converts a #GstVideoFormat into the corresponding #GstVaapiChromaType
 * format.
 *
 * Return value: the #GstVaapiChromaType format, or zero if no match
 *   was found.
 */
guint
gst_vaapi_video_format_get_chroma_type (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map (format);

  return m ? m->chroma_type : 0;
}

/**
 * gst_vaapi_video_format_get_score:
 * @format: a #GstVideoFormat
 *
 * Determines how "native" is this @format. The lower is the returned
 * score, the best format this is for the underlying hardware.
 *
 * Return value: the @format score, or %G_MAXUINT if none was found
 */
guint
gst_vaapi_video_format_get_score (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map (format);

  return m ? (m - &gst_vaapi_video_formats[0]) : G_MAXUINT;
}

/**
 * gst_vaapi_video_format_get_best_native:
 * @format: a #GstVideoFormat
 *
 * Returns the best "native" pixel format that matches a particular
 * color-space.
 *
 * Returns: the #GstVideoFormat with the corresponding best native
 * format for #GstVaapiSurface
 **/
GstVideoFormat
gst_vaapi_video_format_get_best_native (GstVideoFormat format)
{
  GstVaapiChromaType chroma_type;

  if (format == GST_VIDEO_FORMAT_ENCODED)
    return GST_VIDEO_FORMAT_NV12;

  chroma_type = gst_vaapi_video_format_get_chroma_type (format);
  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV422:
      return GST_VIDEO_FORMAT_YUY2;
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      return GST_VIDEO_FORMAT_GRAY8;
    case GST_VAAPI_CHROMA_TYPE_YUV420:
    case GST_VAAPI_CHROMA_TYPE_RGB32:  /* GstVideoGLTextureUploadMeta */
      return GST_VIDEO_FORMAT_NV12;
    case GST_VAAPI_CHROMA_TYPE_YUV420_10BPP:
      return GST_VIDEO_FORMAT_P010_10LE;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  };
}
