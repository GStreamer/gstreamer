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

#include "video.h"
#include "gstmetavideo.h"

static int get_size (GstVideoFormat format, int width, int height);
static int get_stride (GstVideoFormat format, int plane, int width);
static int fill_planes (GstVideoInfo * info);

typedef enum
{
  VIDEO_FLAG_YUV = (1 << 0),
  VIDEO_FLAG_RGB = (1 << 1),
  VIDEO_FLAG_GRAY = (1 << 2),
  VIDEO_FLAG_ALPHA = (1 << 3)
} VideoFlags;

typedef struct
{
  const gchar *fmt;
  GstVideoFormat format;
  guint32 fourcc;
  VideoFlags flags;
  guint n_comp;
  guint depth[GST_VIDEO_MAX_PLANES];
  guint n_planes;
} VideoFormat;

#define COMP0            0, { 0, 0, 0, 0 }
#define COMP8            1, { 8, 0, 0, 0 }
#define COMP888          3, { 8, 8, 8, 0 }
#define COMP8888         4, { 8, 8, 8, 8 }
#define COMP10_10_10     3, { 10, 10, 10, 0 }
#define COMP16           1, { 16, 0, 0, 0 }
#define COMP16_16_16     3, { 16, 16, 16, 0 }
#define COMP16_16_16_16  4, { 16, 16, 16, 16 }
#define COMP555          3, { 5, 5, 5, 0 }
#define COMP565          3, { 5, 6, 5, 0 }

#define MAKE_YUV_FORMAT(name, fourcc, comp) \
 { G_STRINGIFY(name), GST_VIDEO_FORMAT_ ##name, fourcc, VIDEO_FLAG_YUV, comp }
#define MAKE_YUVA_FORMAT(name, fourcc, comp) \
 { G_STRINGIFY(name), GST_VIDEO_FORMAT_ ##name, fourcc, VIDEO_FLAG_YUV | VIDEO_FLAG_ALPHA, comp }

#define MAKE_RGB_FORMAT(name, comp) \
 { G_STRINGIFY(name), GST_VIDEO_FORMAT_ ##name, 0x00000000, VIDEO_FLAG_RGB, comp }
#define MAKE_RGBA_FORMAT(name, comp) \
 { G_STRINGIFY(name), GST_VIDEO_FORMAT_ ##name, 0x00000000, VIDEO_FLAG_RGB | VIDEO_FLAG_ALPHA, comp }

#define MAKE_GRAY_FORMAT(name, comp) \
 { G_STRINGIFY(name), GST_VIDEO_FORMAT_ ##name, 0x00000000, VIDEO_FLAG_GRAY, comp }

static VideoFormat formats[] = {
  {"UNKNOWN", GST_VIDEO_FORMAT_UNKNOWN, 0x00000000, 0, COMP0},

  MAKE_YUV_FORMAT (I420, GST_MAKE_FOURCC ('I', '4', '2', '0'), COMP888),
  MAKE_YUV_FORMAT (YV12, GST_MAKE_FOURCC ('Y', 'V', '1', '2'), COMP888),
  MAKE_YUV_FORMAT (YUY2, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'), COMP888),
  MAKE_YUV_FORMAT (UYVY, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'), COMP888),
  MAKE_YUVA_FORMAT (AYUV, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'), COMP8888),
  MAKE_RGB_FORMAT (RGBx, COMP888),
  MAKE_RGB_FORMAT (BGRx, COMP888),
  MAKE_RGB_FORMAT (xRGB, COMP888),
  MAKE_RGB_FORMAT (xBGR, COMP888),
  MAKE_RGBA_FORMAT (RGBA, COMP8888),
  MAKE_RGBA_FORMAT (BGRA, COMP8888),
  MAKE_RGBA_FORMAT (ARGB, COMP8888),
  MAKE_RGBA_FORMAT (ABGR, COMP8888),
  MAKE_RGB_FORMAT (RGB, COMP888),
  MAKE_RGB_FORMAT (BGR, COMP888),

  MAKE_YUV_FORMAT (Y41B, GST_MAKE_FOURCC ('Y', '4', '1', 'B'), COMP888),
  MAKE_YUV_FORMAT (Y42B, GST_MAKE_FOURCC ('Y', '4', '2', 'B'), COMP888),
  MAKE_YUV_FORMAT (YVYU, GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'), COMP888),
  MAKE_YUV_FORMAT (Y444, GST_MAKE_FOURCC ('Y', '4', '4', '4'), COMP888),
  MAKE_YUV_FORMAT (v210, GST_MAKE_FOURCC ('v', '2', '1', '0'), COMP10_10_10),
  MAKE_YUV_FORMAT (v216, GST_MAKE_FOURCC ('v', '2', '1', '6'), COMP16_16_16),
  MAKE_YUV_FORMAT (NV12, GST_MAKE_FOURCC ('N', 'V', '1', '2'), COMP888),
  MAKE_YUV_FORMAT (NV21, GST_MAKE_FOURCC ('N', 'V', '2', '1'), COMP888),

  MAKE_GRAY_FORMAT (GRAY8, COMP8),
  MAKE_GRAY_FORMAT (GRAY16_BE, COMP16),
  MAKE_GRAY_FORMAT (GRAY16_LE, COMP16),

  MAKE_YUV_FORMAT (v308, GST_MAKE_FOURCC ('v', '3', '0', '8'), COMP888),
  MAKE_YUV_FORMAT (Y800, GST_MAKE_FOURCC ('Y', '8', '0', '0'), COMP8),
  MAKE_YUV_FORMAT (Y16, GST_MAKE_FOURCC ('Y', '1', '6', ' '), COMP16),

  MAKE_RGB_FORMAT (RGB16, COMP565),
  MAKE_RGB_FORMAT (BGR16, COMP565),
  MAKE_RGB_FORMAT (RGB15, COMP555),
  MAKE_RGB_FORMAT (BGR15, COMP555),

  MAKE_YUV_FORMAT (UYVP, GST_MAKE_FOURCC ('U', 'Y', 'V', 'P'), COMP10_10_10),
  MAKE_YUVA_FORMAT (A420, GST_MAKE_FOURCC ('A', '4', '2', '0'), COMP888),
  MAKE_RGBA_FORMAT (RGB8_PALETTED, COMP8888),
  MAKE_YUV_FORMAT (YUV9, GST_MAKE_FOURCC ('Y', 'U', 'V', '9'), COMP888),
  MAKE_YUV_FORMAT (YVU9, GST_MAKE_FOURCC ('Y', 'V', 'U', '9'), COMP888),
  MAKE_YUV_FORMAT (IYU1, GST_MAKE_FOURCC ('I', 'Y', 'U', '1'), COMP888),
  MAKE_RGBA_FORMAT (ARGB64, COMP16_16_16_16),
  MAKE_YUVA_FORMAT (AYUV64, 0x00000000, COMP16_16_16_16),
  MAKE_YUV_FORMAT (r210, GST_MAKE_FOURCC ('r', '2', '1', '0'), COMP10_10_10),
};

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

/**
 * gst_video_calculate_display_ratio:
 * @dar_n: Numerator of the calculated display_ratio
 * @dar_d: Denominator of the calculated display_ratio
 * @video_width: Width of the video frame in pixels
 * @video_height: Height of the video frame in pixels
 * @video_par_n: Numerator of the pixel aspect ratio of the input video.
 * @video_par_d: Denominator of the pixel aspect ratio of the input video.
 * @display_par_n: Numerator of the pixel aspect ratio of the display device
 * @display_par_d: Denominator of the pixel aspect ratio of the display device
 *
 * Given the Pixel Aspect Ratio and size of an input video frame, and the
 * pixel aspect ratio of the intended display device, calculates the actual
 * display ratio the video will be rendered with.
 *
 * Returns: A boolean indicating success and a calculated Display Ratio in the
 * dar_n and dar_d parameters.
 * The return value is FALSE in the case of integer overflow or other error.
 *
 * Since: 0.10.7
 */
gboolean
gst_video_calculate_display_ratio (guint * dar_n, guint * dar_d,
    guint video_width, guint video_height,
    guint video_par_n, guint video_par_d,
    guint display_par_n, guint display_par_d)
{
  gint num, den;
  gint tmp_n, tmp_d;

  g_return_val_if_fail (dar_n != NULL, FALSE);
  g_return_val_if_fail (dar_d != NULL, FALSE);

  /* Calculate (video_width * video_par_n * display_par_d) /
   * (video_height * video_par_d * display_par_n) */
  if (!gst_util_fraction_multiply (video_width, video_height, video_par_n,
          video_par_d, &tmp_n, &tmp_d))
    goto error_overflow;

  if (!gst_util_fraction_multiply (tmp_n, tmp_d, display_par_d, display_par_n,
          &num, &den))
    goto error_overflow;

  g_return_val_if_fail (num > 0, FALSE);
  g_return_val_if_fail (den > 0, FALSE);

  *dar_n = num;
  *dar_d = den;

  return TRUE;

  /* ERRORS */
error_overflow:
  {
    GST_WARNING ("overflow in multiply");
    return FALSE;
  }
}

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
    format = GST_VIDEO_FORMAT_RGB8_PALETTED;
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
 * Since: 0.10.16
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
      return GST_VIDEO_FORMAT_Y800;
    case GST_MAKE_FOURCC ('Y', '1', '6', ' '):
      return GST_VIDEO_FORMAT_Y16;
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

GstVideoFormat
gst_video_format_from_string (const gchar * format)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    if (strcmp (formats[i].fmt, format) == 0)
      return formats[i].format;
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
 * Since: 0.10.16
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

  return formats[format].fmt;
}

/**
 * gst_video_format_is_rgb:
 * @format: a #GstVideoFormat
 *
 * Determine whether the video format is an RGB format.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format represents RGB video
 */
gboolean
gst_video_format_is_rgb (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  if (format >= G_N_ELEMENTS (formats))
    return FALSE;

  return (formats[format].flags & VIDEO_FLAG_RGB) != 0;
}

/**
 * gst_video_format_is_yuv:
 * @format: a #GstVideoFormat
 *
 * Determine whether the video format is a YUV format.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format represents YUV video
 */
gboolean
gst_video_format_is_yuv (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  if (format >= G_N_ELEMENTS (formats))
    return FALSE;

  return (formats[format].flags & VIDEO_FLAG_YUV) != 0;
}

/**
 * gst_video_format_is_gray:
 * @format: a #GstVideoFormat
 *
 * Determine whether the video format is a grayscale format.
 *
 * Since: 0.10.29
 *
 * Returns: TRUE if @format represents grayscale video
 */
gboolean
gst_video_format_is_gray (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  if (format >= G_N_ELEMENTS (formats))
    return FALSE;

  return (formats[format].flags & VIDEO_FLAG_GRAY) != 0;
}

/**
 * gst_video_format_has_alpha:
 * @format: a #GstVideoFormat
 *
 * Returns TRUE or FALSE depending on if the video format provides an
 * alpha channel.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format has an alpha channel
 */
gboolean
gst_video_format_has_alpha (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  if (format >= G_N_ELEMENTS (formats))
    return FALSE;

  return (formats[format].flags & VIDEO_FLAG_ALPHA) != 0;
}

/**
 * gst_video_format_get_n_components:
 * @format: a #GstVideoFormat
 *
 * Get the number of components for @format.
 *
 * Returns: the number of components for @format.
 */
int
gst_video_format_get_n_components (GstVideoFormat format)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);

  if (format >= G_N_ELEMENTS (formats))
    return 0;

  return formats[format].n_comp;
}

/**
 * gst_video_format_get_component_depth:
 * @format: a #GstVideoFormat
 * @component: the video component (e.g. 0 for 'R' in RGB)
 *
 * Returns the number of bits used to encode an individual pixel of
 * a given @component.  Typically this is 8, although higher and lower
 * values are possible for some formats.
 *
 * Since: 0.10.33
 *
 * Returns: depth of component
 */
int
gst_video_format_get_component_depth (GstVideoFormat format, int component)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (component < GST_VIDEO_MAX_PLANES, 0);

  if (format >= G_N_ELEMENTS (formats))
    return FALSE;

  return formats[format].depth[component];
}

/**
 * gst_video_format_get_pixel_stride:
 * @format: a #GstVideoFormat
 * @component: the component index
 *
 * Calculates the pixel stride (number of bytes from one pixel to the
 * pixel to its immediate left) for the video component with an index
 * of @component.  See @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: pixel stride of component @component
 */
int
gst_video_format_get_pixel_stride (GstVideoFormat format, int component)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (component >= 0 && component <= 3, 0);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      return 1;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0) {
        return 2;
      } else {
        return 4;
      }
    case GST_VIDEO_FORMAT_IYU1:
      /* doesn't make much sense for IYU1 because it's 1 or 3
       * for luma depending on position */
      return 0;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_r210:
      return 4;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      return 2;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v308:
      return 3;
    case GST_VIDEO_FORMAT_v210:
      /* v210 is packed at the bit level, so pixel stride doesn't make sense */
      return 0;
    case GST_VIDEO_FORMAT_v216:
      if (component == 0) {
        return 4;
      } else {
        return 8;
      }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      if (component == 0) {
        return 1;
      } else {
        return 2;
      }
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y800:
      return 1;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y16:
      return 2;
    case GST_VIDEO_FORMAT_UYVP:
      /* UYVP is packed at the bit level, so pixel stride doesn't make sense */
      return 0;
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      return 1;
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      return 8;
    default:
      return 0;
  }
}

/**
 * gst_video_info_init:
 * @info: a #GstVideoInfo
 *
 * Initialize @info with default values.
 */
void
gst_video_info_init (GstVideoInfo * info)
{
  g_return_if_fail (info != NULL);

  memset (info, 0, sizeof (GstVideoInfo));
}

/**
 * gst_video_info_set_format:
 * @info: a #GstVideoInfo
 * @format: the format
 * @width: a width
 * @height: a height
 *
 * Set the default info for a video frame of @format and @width and @height.
 */
void
gst_video_info_set_format (GstVideoInfo * info, GstVideoFormat format,
    guint width, guint height)
{
  gint i;

  g_return_if_fail (info != NULL);
  g_return_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN);

  info->flags = 0;
  info->format = format;
  info->width = width;
  info->height = height;

  info->n_planes = formats[format].n_planes;
  info->size = get_size (format, info->width, info->height);
  fill_planes (info);

  for (i = 0; i < info->n_planes; i++) {
    info->plane[i].stride = get_stride (format, i, info->width);
  }
}

/**
 * gst_video_info_from_caps:
 * @info: a #GstVideoInfo
 * @caps: a #GstCaps
 *
 * Parse @caps and update @info.
 *
 * Returns: TRUE if @caps could be parsed
 */
gboolean
gst_video_info_from_caps (GstVideoInfo * info, const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *s;
  GstVideoFormat format;
  gint width, height;
  gint fps_n, fps_d;
  gboolean interlaced;
  gint par_n, par_d;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (structure, "video/x-raw"))
    goto wrong_name;

  if (!(s = gst_structure_get_string (structure, "format")))
    goto no_format;

  format = gst_video_format_from_string (s);
  if (format == GST_VIDEO_FORMAT_UNKNOWN)
    goto unknown_format;

  if (!gst_structure_get_int (structure, "width", &width))
    goto no_width;
  if (!gst_structure_get_int (structure, "height", &height))
    goto no_height;

  gst_video_info_set_format (info, format, width, height);

  if (gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)) {
    info->fps_n = fps_n;
    info->fps_d = fps_d;
  } else {
    info->fps_n = 0;
    info->fps_d = 1;
  }

  if (!gst_structure_get_boolean (structure, "interlaced", &interlaced))
    interlaced = FALSE;
  if (interlaced)
    info->flags |= GST_VIDEO_FLAG_INTERLACED;
  else
    info->flags &= ~GST_VIDEO_FLAG_INTERLACED;

  s = gst_structure_get_string (structure, "color-matrix");
  if (s)
    info->color_matrix = s;
  else
    info->color_matrix = "sdtv";

  s = gst_structure_get_string (structure, "chroma-site");
  if (s)
    info->chroma_site = s;
  else
    info->chroma_site = "mpeg2";

  if (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &par_n, &par_d)) {
    info->par_n = par_n;
    info->par_d = par_d;
  } else {
    info->par_n = 1;
    info->par_d = 1;
  }
  return TRUE;

  /* ERROR */
wrong_name:
  {
    return FALSE;
  }
no_format:
  {
    return FALSE;
  }
unknown_format:
  {
    return FALSE;
  }
no_width:
  {
    return FALSE;
  }
no_height:
  {
    return FALSE;
  }
}

/**
 * gst_video_info_to_caps:
 * @info: a #GstVideoInfo
 *
 * Convert the values of @info into a #GstCaps.
 *
 * Returns: a new #GstCaps containing the info of @info.
 */
GstCaps *
gst_video_info_to_caps (GstVideoInfo * info)
{
  GstCaps *caps;
  const gchar *format;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->format != GST_VIDEO_FORMAT_UNKNOWN, NULL);

  format = gst_video_format_to_string (info->format);
  g_return_val_if_fail (format != NULL, NULL);

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, info->width,
      "height", G_TYPE_INT, info->height,
      "framerate", GST_TYPE_FRACTION, info->fps_n, info->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, info->par_n, info->par_d, NULL);
  if (info->flags & GST_VIDEO_FLAG_INTERLACED)
    gst_caps_set_simple (caps, "interlaced", G_TYPE_BOOLEAN, TRUE, NULL);
  if (info->color_matrix)
    gst_caps_set_simple (caps, "color-matrix", G_TYPE_STRING,
        info->color_matrix, NULL);
  if (info->chroma_site)
    gst_caps_set_simple (caps, "chroma-site", G_TYPE_STRING, info->chroma_site,
        NULL);

  return caps;
}

/**
 * gst_video_frame_map:
 * @frame: pointer to #GstVideoFrame
 * @info: a #GstVideoInfo
 * @buffer: the buffer to map
 *
 * Use @info and @buffer to fill in the values of @frame.
 *
 * All video planes of @buffer will be mapped and the pointers will be set in
 * @frame->data.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_video_frame_map (GstVideoFrame * frame, GstVideoInfo * info,
    GstBuffer * buffer, GstMapFlags flags)
{
  GstMetaVideo *meta;
  gint i;
  guint8 *data;
  gsize size;

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  frame->buffer = buffer;
  meta = gst_buffer_get_meta_video (buffer);
  frame->meta = meta;

  if (meta) {
    /* FIXME use metadata */
  } else {
    /* copy the info */
    frame->info = *info;

    data = gst_buffer_map (buffer, &size, NULL, flags);

    /* do some sanity checks */
    if (size < info->size)
      goto invalid_size;

    /* set up pointers */
    for (i = 0; i < info->n_planes; i++) {
      frame->data[i] = data + info->plane[i].offset;
    }
  }
  return TRUE;

  /* ERRORS */
invalid_size:
  {
    gst_buffer_unmap (buffer, data, size);
    return FALSE;
  }
}

/**
 * gst_video_frame_unmap:
 * @frame: a #GstVideoFrame
 *
 * Unmap the memory previously mapped with gst_video_frame_map.
 */
void
gst_video_frame_unmap (GstVideoFrame * frame)
{
  GstBuffer *buffer;
  GstMetaVideo *meta;
  guint8 *data;

  g_return_if_fail (frame != NULL);

  buffer = frame->buffer;
  meta = frame->meta;

  if (meta) {
    /* FIXME use metadata */
  } else {
    data = frame->data[0];
    data -= frame->info.plane[0].offset;
    gst_buffer_unmap (buffer, data, -1);
  }
}

/**
 * get_stride:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 *
 * Calculates the row stride (number of bytes from one row of pixels to
 * the next) for the video component with an index of @component.  For
 * YUV video, Y, U, and V have component indices of 0, 1, and 2,
 * respectively.  For RGB video, R, G, and B have component indicies of
 * 0, 1, and 2, respectively.  Alpha channels, if present, have a component
 * index of 3.  The @width parameter always represents the width of the
 * video, not the component.
 *
 * Since: 0.10.16
 *
 * Returns: row stride of component @component
 */
static int
get_stride (GstVideoFormat format, int plane, int width)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (plane == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2);
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_r210:
      return width * 4;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      return GST_ROUND_UP_4 (width * 2);
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v308:
      return GST_ROUND_UP_4 (width * 3);
    case GST_VIDEO_FORMAT_Y41B:
      if (plane == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_16 (width) / 4;
      }
    case GST_VIDEO_FORMAT_Y42B:
      if (plane == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_8 (width) / 2;
      }
    case GST_VIDEO_FORMAT_Y444:
      return GST_ROUND_UP_4 (width);
    case GST_VIDEO_FORMAT_v210:
      return ((width + 47) / 48) * 128;
    case GST_VIDEO_FORMAT_v216:
      return GST_ROUND_UP_8 (width * 4);
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      return GST_ROUND_UP_4 (width);
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y800:
      return GST_ROUND_UP_4 (width);
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y16:
      return GST_ROUND_UP_4 (width * 2);
    case GST_VIDEO_FORMAT_UYVP:
      return GST_ROUND_UP_4 ((width * 2 * 5 + 3) / 4);
    case GST_VIDEO_FORMAT_A420:
      if (plane == 0 || plane == 3) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      }
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      return GST_ROUND_UP_4 (width);
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      if (plane == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4);
      }
    case GST_VIDEO_FORMAT_IYU1:
      return GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) +
          GST_ROUND_UP_4 (width) / 2);
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      return width * 8;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_width:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 *
 * Calculates the width of the component.  See
 * @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: width of component @component
 */
int
gst_video_format_get_component_width (GstVideoFormat format,
    int component, int width)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (component >= 0 && component <= 3, 0);
  g_return_val_if_fail (width > 0, 0);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_v216:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_UYVP:
      if (component == 0) {
        return width;
      } else {
        return GST_ROUND_UP_2 (width) / 2;
      }
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
    case GST_VIDEO_FORMAT_IYU1:
      if (component == 0) {
        return width;
      } else {
        return GST_ROUND_UP_4 (width) / 4;
      }
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_Y16:
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
    case GST_VIDEO_FORMAT_r210:
      return width;
    case GST_VIDEO_FORMAT_A420:
      if (component == 0 || component == 3) {
        return width;
      } else {
        return GST_ROUND_UP_2 (width) / 2;
      }
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_height:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @height: the height of video
 *
 * Calculates the height of the component.  See
 * @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: height of component @component
 */
int
gst_video_format_get_component_height (GstVideoFormat format,
    int component, int height)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (component >= 0 && component <= 3, 0);
  g_return_val_if_fail (height > 0, 0);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      if (component == 0) {
        return height;
      } else {
        return GST_ROUND_UP_2 (height) / 2;
      }
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_v216:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_Y16:
    case GST_VIDEO_FORMAT_UYVP:
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
    case GST_VIDEO_FORMAT_IYU1:
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
    case GST_VIDEO_FORMAT_r210:
      return height;
    case GST_VIDEO_FORMAT_A420:
      if (component == 0 || component == 3) {
        return height;
      } else {
        return GST_ROUND_UP_2 (height) / 2;
      }
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      if (component == 0) {
        return height;
      } else {
        return GST_ROUND_UP_4 (height) / 4;
      }
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_offset:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 * @height: the height of video
 *
 * Calculates the offset (in bytes) of the first pixel of the component
 * with index @component.  For packed formats, this will typically be a
 * small integer (0, 1, 2, 3).  For planar formats, this will be a
 * (relatively) large offset to the beginning of the second or third
 * component planes.  See @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: offset of component @component
 */
int
gst_video_format_get_component_offset (GstVideoFormat format,
    int component, int width, int height)
{
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (component >= 0 && component <= 3, 0);
  g_return_val_if_fail ((!gst_video_format_is_yuv (format)) || (width > 0
          && height > 0), 0);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      if (component == 2) {
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) +
            GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
            (GST_ROUND_UP_2 (height) / 2);
      }
      break;
    case GST_VIDEO_FORMAT_YV12:        /* same as I420, but components 1+2 swapped */
      if (component == 0)
        return 0;
      if (component == 2)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      if (component == 1) {
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) +
            GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
            (GST_ROUND_UP_2 (height) / 2);
      }
      break;
    case GST_VIDEO_FORMAT_YUY2:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 3;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      if (component == 0)
        return 0;
      if (component == 1)
        return 3;
      if (component == 2)
        return 1;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0)
        return 1;
      if (component == 1)
        return 0;
      if (component == 2)
        return 2;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      if (component == 0)
        return 1;
      if (component == 1)
        return 2;
      if (component == 2)
        return 3;
      if (component == 3)
        return 0;
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 2;
      if (component == 3)
        return 3;
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      if (component == 0)
        return 2;
      if (component == 1)
        return 1;
      if (component == 2)
        return 0;
      if (component == 3)
        return 3;
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      if (component == 0)
        return 1;
      if (component == 1)
        return 2;
      if (component == 2)
        return 3;
      if (component == 3)
        return 0;
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      if (component == 0)
        return 3;
      if (component == 1)
        return 2;
      if (component == 2)
        return 1;
      if (component == 3)
        return 0;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 2;
      break;
    case GST_VIDEO_FORMAT_BGR:
      if (component == 0)
        return 2;
      if (component == 1)
        return 1;
      if (component == 2)
        return 0;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * height;
      if (component == 2)
        return (GST_ROUND_UP_4 (width) +
            (GST_ROUND_UP_16 (width) / 4)) * height;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * height;
      if (component == 2)
        return (GST_ROUND_UP_4 (width) + (GST_ROUND_UP_8 (width) / 2)) * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
      return GST_ROUND_UP_4 (width) * height * component;
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_r210:
      /* v210 is bit-packed, so this doesn't make sense */
      return 0;
    case GST_VIDEO_FORMAT_v216:
      if (component == 0)
        return 0;
      if (component == 1)
        return 2;
      if (component == 2)
        return 6;
      break;
    case GST_VIDEO_FORMAT_NV12:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      if (component == 2)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) + 1;
      break;
    case GST_VIDEO_FORMAT_NV21:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) + 1;
      if (component == 2)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_Y16:
      return 0;
    case GST_VIDEO_FORMAT_UYVP:
      /* UYVP is bit-packed, so this doesn't make sense */
      return 0;
    case GST_VIDEO_FORMAT_A420:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      if (component == 2) {
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) +
            GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
            (GST_ROUND_UP_2 (height) / 2);
      }
      if (component == 3) {
        return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) +
            2 * GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
            (GST_ROUND_UP_2 (height) / 2);
      }
      break;
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      return 0;
    case GST_VIDEO_FORMAT_YUV9:
      if (component == 0)
        return 0;
      if (component == 1)
        return GST_ROUND_UP_4 (width) * height;
      if (component == 2) {
        return GST_ROUND_UP_4 (width) * height +
            GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4) *
            (GST_ROUND_UP_4 (height) / 4);
      }
      break;
    case GST_VIDEO_FORMAT_YVU9:
      if (component == 0)
        return 0;
      if (component == 1) {
        return GST_ROUND_UP_4 (width) * height +
            GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4) *
            (GST_ROUND_UP_4 (height) / 4);
      }
      if (component == 2)
        return GST_ROUND_UP_4 (width) * height;
      break;
    case GST_VIDEO_FORMAT_IYU1:
      if (component == 0)
        return 1;
      if (component == 1)
        return 0;
      if (component == 2)
        return 4;
      break;
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      if (component == 0)
        return 2;
      if (component == 1)
        return 4;
      if (component == 2)
        return 6;
      if (component == 3)
        return 0;
      break;
    default:
      break;
  }
  GST_WARNING ("unhandled format %d or component %d", format, component);
  return 0;
}

/**
 * get_plane_offset:
 * @format: a #GstVideoFormat
 * @plane: the plane index
 * @width: the width of video
 * @height: the height of video
 *
 * Calculates the offset (in bytes) of the first pixel of the plane
 * with index @plane.  For packed formats, this will typically be 0.
 * For planar formats, this will be a (relatively) large offset to the
 * beginning of the second or third plane planes.
 * See @gst_video_format_get_row_stride for a description
 * of the plane index.
 *
 * Since: 0.10.16
 *
 * Returns: offset of plane @plane
 */
static int
fill_planes (GstVideoInfo * info)
{
  int width;
  int height;

  width = info->width;
  height = info->height;

  switch (info->format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_r210:
    case GST_VIDEO_FORMAT_v216:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_Y16:
    case GST_VIDEO_FORMAT_UYVP:
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
    case GST_VIDEO_FORMAT_IYU1:
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      info->n_planes = 1;
      info->plane[0].offset = 0;
      break;
    case GST_VIDEO_FORMAT_I420:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      info->plane[2].offset = info->plane[1].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_YV12:        /* same as I420, but plane 1+2 swapped */
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[2].offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      info->plane[1].offset = info->plane[2].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_Y41B:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * height;
      info->plane[2].offset = (GST_ROUND_UP_4 (width) +
          (GST_ROUND_UP_16 (width) / 4)) * height;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * height;
      info->plane[2].offset =
          (GST_ROUND_UP_4 (width) + (GST_ROUND_UP_8 (width) / 2)) * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * height;
      info->plane[2].offset = GST_ROUND_UP_4 (width) * height * 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      info->n_planes = 2;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      break;
    case GST_VIDEO_FORMAT_A420:
      info->n_planes = 4;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      info->plane[2].offset = info->plane[1].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2);
      info->plane[3].offset = info->plane[2].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2);
      break;
    case GST_VIDEO_FORMAT_YUV9:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[1].offset = GST_ROUND_UP_4 (width) * height;
      info->plane[2].offset = info->plane[1].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4) *
          (GST_ROUND_UP_4 (height) / 4);
      break;
    case GST_VIDEO_FORMAT_YVU9:
      info->n_planes = 3;
      info->plane[0].offset = 0;
      info->plane[2].offset = GST_ROUND_UP_4 (width) * height;
      info->plane[1].offset = info->plane[2].offset +
          GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4) *
          (GST_ROUND_UP_4 (height) / 4);
      break;
    default:
      GST_WARNING ("unhandled format %d", info->format);
      break;
  }
  return 0;
}

/**
 * get_size:
 * @format: a #GstVideoFormat
 * @width: the width of video
 * @height: the height of video
 *
 * Calculates the total number of bytes in the raw video format.  This
 * number should be used when allocating a buffer for raw video.
 *
 * Returns: size (in bytes) of raw video format
 */
static int
get_size (GstVideoFormat format, int width, int height)
{
  int size;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (width > 0 && height > 0, 0);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      size = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      size += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2) * 2;
      return size;
    case GST_VIDEO_FORMAT_IYU1:
      return GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) +
          GST_ROUND_UP_4 (width) / 2) * height;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2) * height;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_r210:
      return width * 4 * height;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      return GST_ROUND_UP_4 (width * 2) * height;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v308:
      return GST_ROUND_UP_4 (width * 3) * height;
    case GST_VIDEO_FORMAT_Y41B:
      /* simplification of ROUNDUP4(w)*h + 2*((ROUNDUP16(w)/4)*h */
      return (GST_ROUND_UP_4 (width) + (GST_ROUND_UP_16 (width) / 2)) * height;
    case GST_VIDEO_FORMAT_Y42B:
      /* simplification of ROUNDUP4(w)*h + 2*(ROUNDUP8(w)/2)*h */
      return (GST_ROUND_UP_4 (width) + GST_ROUND_UP_8 (width)) * height;
    case GST_VIDEO_FORMAT_Y444:
      return GST_ROUND_UP_4 (width) * height * 3;
    case GST_VIDEO_FORMAT_v210:
      return ((width + 47) / 48) * 128 * height;
    case GST_VIDEO_FORMAT_v216:
      return GST_ROUND_UP_8 (width * 4) * height;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      return GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height) * 3 / 2;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      return GST_ROUND_UP_4 (width) * height;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_Y16:
      return GST_ROUND_UP_4 (width * 2) * height;
    case GST_VIDEO_FORMAT_UYVP:
      return GST_ROUND_UP_4 ((width * 2 * 5 + 3) / 4) * height;
    case GST_VIDEO_FORMAT_A420:
      size = 2 * GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      size += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2) * 2;
      return size;
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      size = GST_ROUND_UP_4 (width) * height;
      size += GST_ROUND_UP_4 (GST_ROUND_UP_4 (width) / 4) *
          (GST_ROUND_UP_4 (height) / 4) * 2;
      return size;
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      return width * 8 * height;
    default:
      return 0;
  }
}

/**
 * gst_video_format_convert:
 * @info: a #GstVideoInfo
 * @src_format: #GstFormat of the @src_value
 * @src_value: value to convert
 * @dest_format: #GstFormat of the @dest_value
 * @dest_value: pointer to destination value
 *
 * Converts among various #GstFormat types.  This function handles
 * GST_FORMAT_BYTES, GST_FORMAT_TIME, and GST_FORMAT_DEFAULT.  For
 * raw video, GST_FORMAT_DEFAULT corresponds to video frames.  This
 * function can be used to handle pad queries of the type GST_QUERY_CONVERT.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if the conversion was successful.
 */
gboolean
gst_video_info_convert (GstVideoInfo * info,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;
  int size, fps_n, fps_d;

  g_return_val_if_fail (info != NULL, 0);
  g_return_val_if_fail (info->format != GST_VIDEO_FORMAT_UNKNOWN, 0);
  g_return_val_if_fail (info->size > 0, 0);

  size = info->size;
  fps_n = info->fps_n;
  fps_d = info->fps_d;

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s to %s",
      src_value, gst_format_get_name (src_format),
      gst_format_get_name (dest_format));

  if (src_format == dest_format) {
    *dest_value = src_value;
    ret = TRUE;
    goto done;
  }

  if (src_value == -1) {
    *dest_value = -1;
    ret = TRUE;
    goto done;
  }

  /* bytes to frames */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_DEFAULT) {
    if (size != 0) {
      *dest_value = gst_util_uint64_scale_int (src_value, 1, size);
    } else {
      GST_ERROR ("blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to bytes */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_BYTES) {
    *dest_value = gst_util_uint64_scale_int (src_value, size, 1);
    ret = TRUE;
    goto done;
  }

  /* time to frames */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_DEFAULT) {
    if (fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          fps_n, GST_SECOND * fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to time */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_TIME) {
    if (fps_n != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * fps_d, fps_n);
    } else {
      GST_ERROR ("framerate numerator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* time to bytes */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    if (fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          fps_n * size, GST_SECOND * fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* bytes to time */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    if (fps_n != 0 && size != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * fps_d, fps_n * size);
    } else {
      GST_ERROR ("framerate denominator and/or blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

done:

  GST_DEBUG ("ret=%d result %" G_GINT64_FORMAT, ret, *dest_value);

  return ret;
}

#define GST_VIDEO_EVENT_STILL_STATE_NAME "GstEventStillFrame"

/**
 * gst_video_event_new_still_frame:
 * @in_still: boolean value for the still-frame state of the event.
 *
 * Creates a new Still Frame event. If @in_still is %TRUE, then the event
 * represents the start of a still frame sequence. If it is %FALSE, then
 * the event ends a still frame sequence.
 *
 * To parse an event created by gst_video_event_new_still_frame() use
 * gst_video_event_parse_still_frame().
 *
 * Returns: The new GstEvent
 * Since: 0.10.26
 */
GstEvent *
gst_video_event_new_still_frame (gboolean in_still)
{
  GstEvent *still_event;
  GstStructure *s;

  s = gst_structure_new (GST_VIDEO_EVENT_STILL_STATE_NAME,
      "still-state", G_TYPE_BOOLEAN, in_still, NULL);
  still_event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

  return still_event;
}

/**
 * gst_video_event_parse_still_frame:
 * @event: A #GstEvent to parse
 * @in_still: A boolean to receive the still-frame status from the event, or NULL
 *
 * Parse a #GstEvent, identify if it is a Still Frame event, and
 * return the still-frame state from the event if it is.
 * If the event represents the start of a still frame, the in_still
 * variable will be set to TRUE, otherwise FALSE. It is OK to pass NULL for the
 * in_still variable order to just check whether the event is a valid still-frame
 * event.
 *
 * Create a still frame event using gst_video_event_new_still_frame()
 *
 * Returns: %TRUE if the event is a valid still-frame event. %FALSE if not
 * Since: 0.10.26
 */
gboolean
gst_video_event_parse_still_frame (GstEvent * event, gboolean * in_still)
{
  const GstStructure *s;
  gboolean ev_still_state;

  g_return_val_if_fail (event != NULL, FALSE);

  if (GST_EVENT_TYPE (event) != GST_EVENT_CUSTOM_DOWNSTREAM)
    return FALSE;               /* Not a still frame event */

  s = gst_event_get_structure (event);
  if (s == NULL
      || !gst_structure_has_name (s, GST_VIDEO_EVENT_STILL_STATE_NAME))
    return FALSE;               /* Not a still frame event */
  if (!gst_structure_get_boolean (s, "still-state", &ev_still_state))
    return FALSE;               /* Not a still frame event */
  if (in_still)
    *in_still = ev_still_state;
  return TRUE;
}

/**
 * gst_video_parse_caps_framerate:
 * @caps: pointer to a #GstCaps instance
 * @fps_n: pointer to integer to hold numerator of frame rate (output)
 * @fps_d: pointer to integer to hold denominator of frame rate (output)
 *
 * Extracts the frame rate from @caps and places the values in the locations
 * pointed to by @fps_n and @fps_d.  Returns TRUE if the values could be
 * parsed correctly, FALSE if not.
 *
 * This function can be used with #GstCaps that have any media type; it
 * is not limited to formats handled by #GstVideoFormat.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @caps was parsed correctly.
 */
gboolean
gst_video_parse_caps_framerate (GstCaps * caps, int *fps_n, int *fps_d)
{
  GstStructure *structure;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  return gst_structure_get_fraction (structure, "framerate", fps_n, fps_d);
}

/**
 * gst_video_parse_caps_palette:
 * @caps: #GstCaps to parse
 *
 * Returns the palette data from the caps as a #GstBuffer. For
 * #GST_VIDEO_FORMAT_RGB8_PALETTED this is containing 256 #guint32
 * values, each containing ARGB colors in native endianness.
 *
 * Returns: a #GstBuffer containing the palette data. Unref after usage.
 * Since: 0.10.32
 */
GstBuffer *
gst_video_parse_caps_palette (GstCaps * caps)
{
  GstStructure *s;
  const GValue *p_v;
  GstBuffer *p;

  if (!gst_caps_is_fixed (caps))
    return NULL;

  s = gst_caps_get_structure (caps, 0);

  p_v = gst_structure_get_value (s, "palette_data");
  if (!p_v || !GST_VALUE_HOLDS_BUFFER (p_v))
    return NULL;

  p = gst_buffer_ref (gst_value_get_buffer (p_v));

  return p;
}
