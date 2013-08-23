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

static gboolean
gst_opencv_get_ipl_depth_and_channels (GstStructure * structure,
    gint * ipldepth, gint * channels, GError ** err)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  const GstVideoFormatInfo *info;
  gint depth = 0, i;
  const gchar *s;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    if (!(s = gst_structure_get_string (structure, "format")))
      return FALSE;
    format = gst_video_format_from_string (s);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      return FALSE;
  }

  info = gst_video_format_get_info (format);

  if (GST_VIDEO_FORMAT_INFO_IS_RGB (info))
    *channels = 3;
  else if (GST_VIDEO_FORMAT_INFO_IS_GRAY (info))
    *channels = 1;
  else {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Unsupported structure %s", gst_structure_get_name (structure));
    return FALSE;
  }

  for (i = 0; i < GST_VIDEO_FORMAT_INFO_N_COMPONENTS (info); i++)
    depth += GST_VIDEO_FORMAT_INFO_DEPTH (info, i);

  if (depth / *channels == 8) {
    /* TODO signdness? */
    *ipldepth = IPL_DEPTH_8U;
  } else if (depth / *channels == 16) {
    *ipldepth = IPL_DEPTH_16U;
  } else {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Unsupported depth/channels %d/%d", depth, *channels);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_opencv_parse_iplimage_params_from_structure (GstStructure * structure,
    gint * width, gint * height, gint * ipldepth, gint * channels,
    GError ** err)
{
  if (!gst_opencv_get_ipl_depth_and_channels (structure, ipldepth, channels,
          err)) {
    return FALSE;
  }

  if (!gst_structure_get_int (structure, "width", width) ||
      !gst_structure_get_int (structure, "height", height)) {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "No width/height in caps");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_opencv_parse_iplimage_params_from_caps (GstCaps * caps, gint * width,
    gint * height, gint * ipldepth, gint * channels, GError ** err)
{
  GstVideoInfo info;
  gint i, depth = 0;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR ("Failed to get the videoinfo from caps");
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "No width/heighti/depth/channels in caps");
    return FALSE;
  }

  *width = GST_VIDEO_INFO_WIDTH (&info);
  *height = GST_VIDEO_INFO_HEIGHT (&info);
  if (GST_VIDEO_INFO_IS_RGB (&info))
    *channels = 3;
  else if (GST_VIDEO_INFO_IS_GRAY (&info))
    *channels = 1;
  else {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Unsupported caps %s", gst_caps_to_string (caps));
    return FALSE;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&info); i++)
    depth += GST_VIDEO_INFO_COMP_DEPTH (&info, i);

  if (depth / *channels == 8) {
    /* TODO signdness? */
    *ipldepth = IPL_DEPTH_8U;
  } else if (depth / *channels == 16) {
    *ipldepth = IPL_DEPTH_16U;
  } else {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Unsupported depth/channels %d/%d", depth, *channels);
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
