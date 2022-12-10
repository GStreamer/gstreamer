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

#define DEBUG 1
#include "gst/vaapi/gstvaapidebug.h"

#if GST_VAAPI_USE_DRM
#include <drm_fourcc.h>
#endif

typedef struct _GstVideoFormatMapMap
{
  GstVideoFormat format;
  uint32_t drm_format;
  GstVaapiChromaType chroma_type;
  VAImageFormat va_format;
} GstVideoFormatMap;

#define VA_BYTE_ORDER_NOT_CARE 0

#if GST_VAAPI_USE_DRM
#define MAKE_DRM_FORMAT(DRM_FORMAT) G_PASTE(DRM_FORMAT_,DRM_FORMAT)
#else
#define MAKE_DRM_FORMAT(DRM_FORMAT) 0
#endif

#define DEF_YUV(BYTE_ORDER, FORMAT, DRM_FORMAT, FOURCC, BPP, SUB)       \
  { G_PASTE(GST_VIDEO_FORMAT_,FORMAT),                                  \
    MAKE_DRM_FORMAT(DRM_FORMAT),                                        \
    G_PASTE(GST_VAAPI_CHROMA_TYPE_YUV,SUB),                             \
    { VA_FOURCC FOURCC, BYTE_ORDER, BPP, }, }

#define DEF_RGB(BYTE_ORDER, FORMAT, DRM, FOURCC, BPP, DEPTH, R,G,B,A)   \
  { G_PASTE(GST_VIDEO_FORMAT_,FORMAT),                                  \
    MAKE_DRM_FORMAT(DRM),                                               \
    G_PASTE(GST_VAAPI_CHROMA_TYPE_RGB,BPP),                             \
    { VA_FOURCC FOURCC, BYTE_ORDER, BPP, DEPTH, R, G, B, A }, }

/* Image formats, listed in HW order preference */
/* XXX: The new added video format must be added to
 * GST_VAAPI_FORMATS_ALL in header file to make it available to all
 * vaapi element's pad cap template. */
/* *INDENT-OFF* */
static const GstVideoFormatMap gst_vaapi_video_default_formats[] = {
  /* LSB and MSB video formats definitions are unclear and ambiguous.
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
   *  RGBA in LSB, from CPU's perspective, it's RGBA order in memory,
   *  but when it is stored in memory, because CPU's little
   *  endianness, it will be re-ordered, with mask R:0x000000FF
   *  G:0x0000FF00 B:0x00FF0000 A:0xFF000000. In other words, from
   *  memory's perspective, RGBA LSB is equal as ABGR MSB.
   *
   * These definitions are mixed used all over the media system and we
   * need to correct the mapping form VA video format to GStreamer
   * video format in both manners, especially for RGB format.
   */

  /* YUV formats */
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, NV12, NV12, ('N', 'V', '1', '2'), 12, 420),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, YV12, YVU420, ('Y', 'V', '1', '2'), 12, 420),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, I420, YUV420, ('I', '4', '2', '0'), 12, 420),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, YUY2, YUYV, ('Y', 'U', 'Y', '2'), 16, 422),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, UYVY, UYVY, ('U', 'Y', 'V', 'Y'), 16, 422),

  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, Y444, YUV444, ('4', '4', '4', 'P'), 24, 444),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, GRAY8, INVALID, ('Y', '8', '0', '0'), 8, 400),

  DEF_YUV (VA_LSB_FIRST, P010_10LE, P010, ('P', '0', '1', '0'), 24, 420_10BPP),
  DEF_YUV (VA_LSB_FIRST, P012_LE, P012, ('P', '0', '1', '2'), 24, 420_12BPP),
  /* AYUV is a clear defined format by doc */
  DEF_YUV (VA_LSB_FIRST, VUYA, AYUV, ('A', 'Y', 'U', 'V'), 32, 444),

  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, Y210, Y210, ('Y', '2', '1', '0'), 32, 422_10BPP),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, Y410, Y410, ('Y', '4', '1', '0'), 32, 444_10BPP),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, Y212_LE, Y212, ('Y', '2', '1', '2'), 32, 422_12BPP),
  DEF_YUV (VA_BYTE_ORDER_NOT_CARE, Y412_LE, Y412, ('Y', '4', '1', '2'), 32, 444_12BPP),

  /* RGB formats */
  DEF_RGB (VA_LSB_FIRST, ARGB, BGRA8888, ('A', 'R', 'G', 'B'), 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),
  DEF_RGB (VA_LSB_FIRST, ARGB, BGRA8888, ('B', 'G', 'R', 'A'), 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),
  DEF_RGB (VA_MSB_FIRST, ARGB, BGRA8888, ('A', 'R', 'G', 'B'), 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),

  DEF_RGB (VA_LSB_FIRST, xRGB, BGRX8888, ('X', 'R', 'G', 'B'), 32, 24, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x00000000),
  DEF_RGB (VA_LSB_FIRST, xRGB, BGRX8888, ('B', 'G', 'R', 'X'), 32, 24, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x00000000),
  DEF_RGB (VA_MSB_FIRST, xRGB, BGRX8888, ('X', 'R', 'G', 'B'), 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),

  DEF_RGB (VA_LSB_FIRST, RGBA, ABGR8888, ('R', 'G', 'B', 'A'), 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),
  DEF_RGB (VA_LSB_FIRST, RGBA, ABGR8888, ('A', 'B', 'G', 'R'), 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),
  DEF_RGB (VA_MSB_FIRST, RGBA, ABGR8888, ('R', 'G', 'B', 'A'), 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),

  DEF_RGB (VA_LSB_FIRST, RGBx, XBGR8888, ('R', 'G', 'B', 'X'), 32, 24, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0x00000000),
  DEF_RGB (VA_LSB_FIRST, RGBx, XBGR8888, ('X', 'B', 'G', 'R'), 32, 24, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0x00000000),
  DEF_RGB (VA_MSB_FIRST, RGBx, XBGR8888, ('R', 'G', 'B', 'X'), 32, 24, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x00000000),

  DEF_RGB (VA_LSB_FIRST, ABGR, RGBA8888, ('A', 'B', 'G', 'R'), 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),
  DEF_RGB (VA_LSB_FIRST, ABGR, RGBA8888, ('R', 'G', 'B', 'A'), 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),
  DEF_RGB (VA_MSB_FIRST, ABGR, RGBA8888, ('A', 'B', 'G', 'R'), 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),

  DEF_RGB (VA_LSB_FIRST, xBGR, RGBX8888, ('X', 'B', 'G', 'R'), 32, 24, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x00000000),
  DEF_RGB (VA_LSB_FIRST, xBGR, RGBX8888, ('R', 'G', 'B', 'X'), 32, 24, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x00000000),
  DEF_RGB (VA_MSB_FIRST, xBGR, RGBX8888, ('X', 'B', 'G', 'R'), 32, 24, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0x00000000),

  DEF_RGB (VA_LSB_FIRST, BGRA, ARGB8888, ('B', 'G', 'R', 'A'), 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),
  DEF_RGB (VA_LSB_FIRST, BGRA, ARGB8888, ('A', 'R', 'G', 'B'), 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),
  DEF_RGB (VA_MSB_FIRST, BGRA, ARGB8888, ('B', 'G', 'R', 'A'), 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),

  DEF_RGB (VA_LSB_FIRST, BGRx, XRGB8888, ('B', 'G', 'R', 'X'), 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),
  DEF_RGB (VA_LSB_FIRST, BGRx, XRGB8888, ('X', 'R', 'G', 'B'), 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),
  DEF_RGB (VA_MSB_FIRST, BGRx, XRGB8888, ('B', 'G', 'R', 'X'), 32, 24, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x00000000),

  DEF_RGB (VA_BYTE_ORDER_NOT_CARE, RGB16, RGB565, ('R', 'G', '1', '6'), 16, 16,
      0x0000f800, 0x000007e0, 0x0000001f, 0x00000000),
  DEF_RGB (VA_BYTE_ORDER_NOT_CARE, RGB, RGB888, ('R', 'G', '2', '4'), 32, 24,
      0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000),
  DEF_RGB (VA_LSB_FIRST, BGR10A2_LE, ARGB2101010, ('A', 'R', '3', '0'), 32, 30,
      0x3ff00000, 0x000ffc00, 0x000003ff, 0x30000000),
  {0,}
};
/* *INDENT-ON* */

#undef DEF_RGB
#undef DEF_YUV

static GArray *gst_vaapi_video_formats_map = NULL;

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
  return (fmt1->red_mask == fmt2->red_mask &&
      fmt1->green_mask == fmt2->green_mask &&
      fmt1->blue_mask == fmt2->blue_mask &&
      fmt1->alpha_mask == fmt2->alpha_mask);
}

static inline gboolean
va_format_is_same (const VAImageFormat * fmt1, const VAImageFormat * fmt2)
{
  if (fmt1->fourcc != fmt2->fourcc)
    return FALSE;
  if (fmt1->byte_order != VA_BYTE_ORDER_NOT_CARE &&
      fmt2->byte_order != VA_BYTE_ORDER_NOT_CARE &&
      fmt1->byte_order != fmt2->byte_order)
    return FALSE;

  return va_format_is_rgb (fmt1) ? va_format_is_same_rgb (fmt1, fmt2) : TRUE;
}

static const GstVideoFormatMap *
get_map_in_default_by_gst_format (GstVideoFormat format)
{
  const GstVideoFormatMap *m;
  for (m = gst_vaapi_video_default_formats; m->format; m++) {
    if (m->format == format)
      return m;
  }
  return NULL;
}

static const GstVideoFormatMap *
get_map_in_default_by_va_format (const VAImageFormat * va_format)
{
  const GstVideoFormatMap *m, *n;

  n = NULL;
  for (m = gst_vaapi_video_default_formats; m->format; m++) {
    if (va_format_is_same (&m->va_format, va_format)) {
      /* Should not map to VAImageFormat to same GstVideoFormat */
      g_assert (n == NULL);
      n = m;
    }
  }
  return n;
}

static const GstVideoFormatMap *
get_map_by_gst_format (const GArray * formats, GstVideoFormat format)
{
  const GstVideoFormatMap *entry;
  guint i;

  if (!formats)
    return NULL;

  for (i = 0; i < formats->len; i++) {
    entry = &g_array_index (formats, GstVideoFormatMap, i);
    if (entry->format == format)
      return entry;
  }
  return NULL;
}

static const GstVideoFormatMap *
get_map_by_va_format (const VAImageFormat * va_format)
{
  const GArray *formats = gst_vaapi_video_formats_map;
  const GstVideoFormatMap *entry;
  guint i;

  if (!formats)
    return NULL;

  for (i = 0; i < formats->len; i++) {
    entry = &g_array_index (formats, GstVideoFormatMap, i);
    if (va_format_is_same (&entry->va_format, va_format))
      return entry;
  }
  return NULL;
}


static guint
get_fmt_score_in_default (GstVideoFormat format)
{
  const GstVideoFormatMap *const m = get_map_in_default_by_gst_format (format);

  return m ? (m - &gst_vaapi_video_default_formats[0]) : G_MAXUINT;
}

static gint
video_format_compare_by_score (gconstpointer a, gconstpointer b)
{
  const GstVideoFormatMap *m1 = (GstVideoFormatMap *) a;
  const GstVideoFormatMap *m2 = (GstVideoFormatMap *) b;

  return ((gint) get_fmt_score_in_default (m1->format) -
      (gint) get_fmt_score_in_default (m2->format));
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
  const GstVideoFormatMap *const m =
      get_map_by_gst_format (gst_vaapi_video_formats_map, format);
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
  const GstVideoFormatMap *const m =
      get_map_by_gst_format (gst_vaapi_video_formats_map, format);
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
  const GArray *map = gst_vaapi_video_formats_map;
  const GstVideoFormatMap *m;
  guint i;

  /* Note: VA fourcc values are now standardized and shall represent
     a unique format. The associated VAImageFormat is just a hint to
     determine RGBA component ordering */
  for (i = 0; i < map->len; i++) {
    m = &g_array_index (map, GstVideoFormatMap, i);
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
  const GstVideoFormatMap *const m = get_map_by_va_format (va_format);
  return m ? m->format : GST_VIDEO_FORMAT_UNKNOWN;
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
  const GstVideoFormatMap *const m =
      get_map_by_gst_format (gst_vaapi_video_formats_map, format);
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
  const GstVideoFormatMap *const m =
      get_map_by_gst_format (gst_vaapi_video_formats_map, format);
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
  return get_fmt_score_in_default (format);
}

/**
 * gst_vaapi_video_format_from_chroma:
 * @chroma_type: a #GstVaapiChromaType
 *
 * Returns the "preferred" pixel format that matches with
 * @chroma_type.
 *
 * Returns: the preferred pixel format for @chroma_type
 **/
GstVideoFormat
gst_vaapi_video_format_from_chroma (guint chroma_type)
{
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
    case GST_VAAPI_CHROMA_TYPE_YUV420_12BPP:
      return GST_VIDEO_FORMAT_P012_LE;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
      return GST_VIDEO_FORMAT_VUYA;
    case GST_VAAPI_CHROMA_TYPE_YUV422_10BPP:
      return GST_VIDEO_FORMAT_Y210;
    case GST_VAAPI_CHROMA_TYPE_YUV444_10BPP:
      return GST_VIDEO_FORMAT_Y410;
    case GST_VAAPI_CHROMA_TYPE_YUV444_12BPP:
      return GST_VIDEO_FORMAT_Y412_LE;
    case GST_VAAPI_CHROMA_TYPE_YUV422_12BPP:
      return GST_VIDEO_FORMAT_Y212_LE;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
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
  return gst_vaapi_video_format_from_chroma (chroma_type);
}

/**
 * gst_vaapi_video_format_get_formats_by_chroma:
 * @chroma: a #GstVaapiChromaType
 *
 * Get all #GstVideoFormat which belong to #GstVaapiChromaType.
 *
 * Returns: an array of #GstVideoFormat.
 **/
GArray *
gst_vaapi_video_format_get_formats_by_chroma (guint chroma)
{
  const GstVideoFormatMap *entry;
  GArray *formats;
  guint i;

  formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));
  if (!formats)
    return NULL;

  for (i = 0; i < gst_vaapi_video_formats_map->len; i++) {
    entry = &g_array_index (gst_vaapi_video_formats_map, GstVideoFormatMap, i);
    if (entry->chroma_type == chroma)
      g_array_append_val (formats, entry->format);
  }

  if (formats->len == 0) {
    g_array_unref (formats);
    return NULL;
  }

  return formats;
}

struct ImageFormatsData
{
  VAImageFormat *formats;
  guint n;
};

static gpointer
video_format_create_map_once (gpointer data)
{
  const GstVideoFormatMap *src_entry, *entry;
  guint i;
  VAImageFormat *formats = ((struct ImageFormatsData *) data)->formats;
  guint n = ((struct ImageFormatsData *) data)->n;
  GArray *array = NULL;

  array = g_array_new (FALSE, TRUE, sizeof (GstVideoFormatMap));
  if (array == NULL)
    return NULL;

  /* All the YUV format has no ambiguity */
  for (i = 0; i < G_N_ELEMENTS (gst_vaapi_video_default_formats); i++) {
    if (va_format_is_yuv (&gst_vaapi_video_default_formats[i].va_format))
      g_array_append_val (array, gst_vaapi_video_default_formats[i]);
  }

  if (formats) {
    for (i = 0; i < n; i++) {
      if (!va_format_is_rgb (&formats[i]))
        continue;

      src_entry = get_map_in_default_by_va_format (&formats[i]);
      if (src_entry) {
        entry = get_map_by_gst_format (array, src_entry->format);
        if (entry && !va_format_is_same (&entry->va_format, &formats[i])) {
          GST_INFO ("va_format1 with fourcc %" GST_FOURCC_FORMAT
              " byte order: %d, BPP: %d, depth %d, red mask 0x%4x,"
              " green mask 0x%4x, blue mask 0x%4x, alpha mask 0x%4x"
              " conflict with va_foramt2 fourcc %" GST_FOURCC_FORMAT
              " byte order: %d, BPP: %d, depth %d, red mask 0x%4x,"
              " green mask 0x%4x, blue mask 0x%4x, alpha mask 0x%4x."
              " Both map to the same GST format: %s, which is not"
              " allowed, va_format1 will be skipped",
              GST_FOURCC_ARGS (entry->va_format.fourcc),
              entry->va_format.byte_order, entry->va_format.bits_per_pixel,
              entry->va_format.depth, entry->va_format.red_mask,
              entry->va_format.green_mask, entry->va_format.blue_mask,
              entry->va_format.alpha_mask,
              GST_FOURCC_ARGS (formats[i].fourcc),
              formats[i].byte_order, formats[i].bits_per_pixel,
              formats[i].depth, formats[i].red_mask, formats[i].green_mask,
              formats[i].blue_mask, formats[i].alpha_mask,
              gst_video_format_to_string (entry->format));
          continue;
        }
        g_array_append_val (array, (*src_entry));
      }

      GST_LOG ("%s to map RGB va_format with fourcc: %"
          GST_FOURCC_FORMAT
          ", byte order: %d BPP: %d, depth %d, red mask %4x,"
          " green mask %4x, blue mask %4x, alpha mask %4x to %s gstreamer"
          " video format", src_entry ? "succeed" : "failed",
          GST_FOURCC_ARGS (formats[i].fourcc), formats[i].byte_order,
          formats[i].bits_per_pixel, formats[i].depth, formats[i].red_mask,
          formats[i].green_mask, formats[i].blue_mask, formats[i].alpha_mask,
          src_entry ? gst_video_format_to_string (src_entry->format) : "any");
    }
  }

  g_array_sort (array, video_format_compare_by_score);
  gst_vaapi_video_formats_map = array;
  return array;
}

/**
 * gst_vaapi_video_format_new_map:
 * @formats: all #VAImageFormat need to map
 * @n: the number of VAImageFormat
 *
 * Return: True if create successfully.
 **/
gboolean
gst_vaapi_video_format_create_map (VAImageFormat * formats, guint n)
{
  static GOnce once = G_ONCE_INIT;
  struct ImageFormatsData data = { formats, n };

  g_once (&once, video_format_create_map_once, &data);

  return once.retval != NULL;
}

/**
 * gst_vaapi_drm_format_from_va_fourcc:
 * @fourcc: a FOURCC value
 *
 * Converts a VA fourcc into the corresponding DRM_FORMAT_*. If no
 * matching fourcc was found, then DRM_FORMAT_INVALID is returned.
 *
 * Return value: the DRM_FORMAT_* corresponding to the VA @fourcc
 *
 * Since: 1.18
 */
guint
gst_vaapi_drm_format_from_va_fourcc (guint32 fourcc)
{
#if GST_VAAPI_USE_DRM
  const GArray *map = gst_vaapi_video_formats_map;
  const GstVideoFormatMap *m;
  guint i;

  if (!map)
    return GST_VIDEO_FORMAT_UNKNOWN;

  /* Note: VA fourcc values are now standardized and shall represent
     a unique format. The associated VAImageFormat is just a hint to
     determine RGBA component ordering */
  for (i = 0; i < map->len; i++) {
    m = &g_array_index (map, GstVideoFormatMap, i);
    if (m->va_format.fourcc == fourcc)
      return m->drm_format;
  }
  return DRM_FORMAT_INVALID;
#else
  return 0;
#endif
}

/**
 * gst_vaapi_video_format_from_drm_format:
 * @drm_format: a DRM format value
 *
 * Converts a DRM_FORMAT_* to the corresponding GstVideoFormat. If no
 * matching fourcc was found, then DRM_FORMAT_INVALID is returned.
 *
 * Return value: GstVideoFormat corresponding to the @drm_format
 *
 * Since: 1.18
 */
GstVideoFormat
gst_vaapi_video_format_from_drm_format (guint drm_format)
{
#if GST_VAAPI_USE_DRM
  const GArray *map = gst_vaapi_video_formats_map;
  const GstVideoFormatMap *m;
  guint i;

  if (!map)
    return GST_VIDEO_FORMAT_UNKNOWN;

  for (i = 0; i < map->len; i++) {
    m = &g_array_index (map, GstVideoFormatMap, i);
    if (m->drm_format == drm_format)
      return m->format;
  }
#endif
  return GST_VIDEO_FORMAT_UNKNOWN;
}
