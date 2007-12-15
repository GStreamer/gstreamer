/* gstvideo-common.c
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
#include "config.h"
#endif

#include <gstvideo-common.h>


gboolean
gst_video_parse_caps (GstCaps * caps, GstVideoFormat * format, int *width,
    int *height)
{
  GstStructure *structure;
  gboolean ok = TRUE;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (format) {
    if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
      guint32 fourcc;

      ok &= gst_structure_get_fourcc (structure, "format", &fourcc);

      *format = gst_video_fourcc_to_format (fourcc);
      if (*format == GST_VIDEO_FORMAT_UNKNOWN) {
        ok = FALSE;
      }
    } else if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
      int depth;
      int bpp;
      int endianness;
      int red_mask;
      int green_mask;
      int blue_mask;

      ok &= gst_structure_get_int (structure, "depth", &depth);
      ok &= gst_structure_get_int (structure, "bpp", &bpp);
      ok &= gst_structure_get_int (structure, "endianness", &endianness);
      ok &= gst_structure_get_int (structure, "red_mask", &red_mask);
      ok &= gst_structure_get_int (structure, "green_mask", &green_mask);
      ok &= gst_structure_get_int (structure, "blue_mask", &blue_mask);

      if (depth != 24 || bpp != 32 || endianness != G_BIG_ENDIAN) {
        ok = FALSE;
      } else {
        *format = gst_video_rgb32_masks_to_format (red_mask, green_mask,
            blue_mask);
        if (*format == GST_VIDEO_FORMAT_UNKNOWN) {
          ok = FALSE;
        }
      }
    } else {
      ok = FALSE;
    }
  }

  if (width) {
    ok &= gst_structure_get_int (structure, "width", width);
  }

  if (height) {
    ok &= gst_structure_get_int (structure, "height", height);
  }

  return ok;
}

gboolean
gst_video_parse_caps_framerate (GstCaps * caps, int *fps_n, int *fps_d)
{
  GstStructure *structure;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  return gst_structure_get_fraction (structure, "framerate", fps_n, fps_d);
}

gboolean
gst_video_parse_caps_pixel_aspect_ratio (GstCaps * caps, int *par_n, int *par_d)
{
  GstStructure *structure;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          par_n, par_d)) {
    *par_n = 1;
    *par_d = 1;
  }
  return TRUE;
}

GstCaps *
gst_video_create_caps (GstVideoFormat format, int width, int height,
    int framerate_n, int framerate_d, int par_n, int par_d)
{
  if (gst_video_format_is_yuv (format)) {
    return gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, gst_video_format_to_fourcc (format),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
  }
  if (gst_video_format_is_rgb (format)) {
    int red_mask;
    int blue_mask;
    int green_mask;

    red_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 0, width,
        height);
    green_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 1, width,
        height);
    blue_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 2, width,
        height);

    return gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, 32,
        "depth", G_TYPE_INT, 24,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN,
        "red_mask", G_TYPE_INT, red_mask,
        "green_mask", G_TYPE_INT, green_mask,
        "blue_mask", G_TYPE_INT, blue_mask,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
  }
  return NULL;
}

GstVideoFormat
gst_video_fourcc_to_format (guint32 fourcc)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return GST_VIDEO_FORMAT_I420;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      return GST_VIDEO_FORMAT_YV12;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      return GST_VIDEO_FORMAT_YUY2;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      return GST_VIDEO_FORMAT_UYVY;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      return GST_VIDEO_FORMAT_AYUV;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

guint32
gst_video_format_to_fourcc (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return GST_MAKE_FOURCC ('I', '4', '2', '0');
    case GST_VIDEO_FORMAT_YV12:
      return GST_MAKE_FOURCC ('Y', 'V', '1', '2');
    case GST_VIDEO_FORMAT_YUY2:
      return GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
    case GST_VIDEO_FORMAT_UYVY:
      return GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
    case GST_VIDEO_FORMAT_AYUV:
      return GST_MAKE_FOURCC ('A', 'Y', 'U', 'V');
    default:
      return 0;
  }
}

GstVideoFormat
gst_video_rgb32_masks_to_format (int red_mask, int green_mask, int blue_mask)
{
  if (red_mask == 0xff000000 && green_mask == 0x00ff0000 &&
      blue_mask == 0x0000ff00) {
    return GST_VIDEO_FORMAT_RGBx;
  }
  if (red_mask == 0x0000ff00 && green_mask == 0x00ff0000 &&
      blue_mask == 0xff000000) {
    return GST_VIDEO_FORMAT_BGRx;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

gboolean
gst_video_format_is_rgb (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
      return FALSE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return TRUE;
    default:
      return FALSE;
  }
}

gboolean
gst_video_format_is_yuv (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
      return TRUE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return FALSE;
    default:
      return FALSE;
  }
}

gboolean
gst_video_format_has_alpha (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return FALSE;
    case GST_VIDEO_FORMAT_AYUV:
      return TRUE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return FALSE;
    default:
      return FALSE;
  }
}

int
gst_video_format_get_row_stride (GstVideoFormat format, int component,
    int width)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2);
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return width * 4;
    default:
      return 0;
  }
}

int
gst_video_format_get_pixel_stride (GstVideoFormat format, int component)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return 1;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0) {
        return 2;
      } else {
        return 4;
      }
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return 4;
    default:
      return 0;
  }
}

int
gst_video_format_get_component_width (GstVideoFormat format, int component,
    int width)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0) {
        return width;
      } else {
        return GST_ROUND_UP_2 (width) / 2;
      }
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return width;
    default:
      return 0;
  }
}

int
gst_video_format_get_component_height (GstVideoFormat format, int component,
    int height)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return height;
      } else {
        return GST_ROUND_UP_2 (height) / 2;
      }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return height;
    default:
      return 0;
  }
}

int
gst_video_format_get_component_offset (GstVideoFormat format, int component,
    int width, int height)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return 0;
      } else {
        int offset;

        offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
        if (component == 2) {
          offset += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
              (GST_ROUND_UP_2 (height) / 2);
        }
        return offset;
      }
    case GST_VIDEO_FORMAT_YUY2:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 3;
      return 0;
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0)
        return 1;
      if (component == 1)
        return 0;
      if (component == 2)
        return 2;
      return 0;
    case GST_VIDEO_FORMAT_AYUV:
      if (component == 0)
        return 1;
      if (component == 1)
        return 2;
      if (component == 2)
        return 3;
      if (component == 3)
        return 0;
      return 0;
    case GST_VIDEO_FORMAT_RGBx:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 2;
      if (component == 3)
        return 3;
      return 0;
    case GST_VIDEO_FORMAT_BGRx:
      if (component == 0)
        return 2;
      if (component == 1)
        return 1;
      if (component == 2)
        return 0;
      if (component == 3)
        return 3;
      return 0;
    default:
      return 0;
  }
}

int
gst_video_format_get_size (GstVideoFormat format, int width, int height)
{
  int size;

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      size = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      size += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2) * 2;
      return size;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2) * height;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return width * 4 * height;
    default:
      return 0;
  }
}


gboolean
gst_video_convert (GstVideoFormat format, int width, int height,
    int fps_n, int fps_d,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;
  int size;

  size = gst_video_format_get_size (format, width, height);

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
