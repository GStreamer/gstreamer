/* GStreamer
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 *
 * gstopencvutils.c: miscellaneous utility functions
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

#include "gstopencvutils.h"
#include <opencv2/core/core_c.h>

gboolean
gst_opencv_parse_iplimage_params_from_caps (GstCaps * caps, gint * width,
    gint * height, gint * ipldepth, gint * channels, GError ** err)
{
  GstVideoInfo info;
  gchar *caps_str;

  if (!gst_video_info_from_caps (&info, caps)) {
    caps_str = gst_caps_to_string (caps);
    GST_ERROR ("Failed to get video info from caps %s", caps_str);
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Failed to get video info from caps %s", caps_str);
    g_free (caps_str);
    return FALSE;
  }

  return gst_opencv_iplimage_params_from_video_info (&info, width, height,
          ipldepth, channels, err);
}

gboolean
gst_opencv_iplimage_params_from_video_info (GstVideoInfo * info, gint * width,
    gint * height, gint * ipldepth, gint * channels, GError ** err)
{
  GstVideoFormat format;
  int cv_type;

  format = GST_VIDEO_INFO_FORMAT (info);
  if (!gst_opencv_cv_image_type_from_video_format (format, &cv_type, err)) {
    return FALSE;
  }

  *width = GST_VIDEO_INFO_WIDTH (info);
  *height = GST_VIDEO_INFO_HEIGHT (info);

  *ipldepth = cvIplDepth (cv_type);
  *channels = CV_MAT_CN (cv_type);

  return TRUE;
}

gboolean
gst_opencv_cv_image_type_from_video_format (GstVideoFormat format,
    int * cv_type, GError ** err)
{
  const gchar *format_str;

  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
      *cv_type = CV_8UC1;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      *cv_type = CV_8UC3;
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ABGR:
      *cv_type = CV_8UC4;
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      *cv_type = CV_16UC1;
      break;
    default:
      format_str = gst_video_format_to_string (format);
      g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
          "Unsupported video format %s", format_str);
      return FALSE;
  }

  return TRUE;
}

GstCaps *
gst_opencv_caps_from_cv_image_type (int cv_type)
{
  GstCaps *c = gst_caps_new_empty ();
  switch (cv_type) {
    case CV_8UC1:
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("GRAY8")));
      break;
    case CV_8UC3:
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("RGB")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("BGR")));
      break;
    case CV_8UC4:
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("RGBx")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("xRGB")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("BGRx")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("xBGR")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("RGBA")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("ARGB")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("BGRA")));
      gst_caps_append (c, gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("ABGR")));
      break;
    case CV_16UC1:
      gst_caps_append (c,
          gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("GRAY16_LE")));
      gst_caps_append (c,
          gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("GRAY16_BE")));
      break;
  }
  return c;
}
