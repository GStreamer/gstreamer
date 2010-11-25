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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstopencvutils.h"

static gboolean
gst_opencv_get_ipl_depth_and_channels (GstStructure * structure,
    gint * ipldepth, gint * channels, GError ** err)
{
  gint depth, bpp;

  if (!gst_structure_get_int (structure, "depth", &depth) ||
      !gst_structure_get_int (structure, "bpp", &bpp)) {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "No depth/bpp in caps");
    return FALSE;
  }

  if (depth != bpp) {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Depth and bpp should be equal");
    return FALSE;
  }

  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    *channels = 3;
  } else if (gst_structure_has_name (structure, "video/x-raw-gray")) {
    *channels = 1;
  } else {
    g_set_error (err, GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION,
        "Unsupported caps %s", gst_structure_get_name (structure));
    return FALSE;
  }

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
  return
      gst_opencv_parse_iplimage_params_from_structure (gst_caps_get_structure
      (caps, 0), width, height, ipldepth, channels, err);
}

GstCaps *
gst_opencv_caps_from_cv_image_type (int cv_type)
{
  GstCaps *caps = gst_caps_new_empty ();
  switch (cv_type) {
    case CV_8UC1:
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_GRAY8));
      break;
    case CV_8UC3:
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_RGB));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_BGR));
      break;
    case CV_8UC4:
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_RGBx));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_xRGB));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_BGRx));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_xBGR));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_RGBA));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_ARGB));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_BGRA));
      gst_caps_append (caps, gst_caps_from_string (GST_VIDEO_CAPS_ABGR));
      break;
    case CV_16UC1:
      gst_caps_append (caps,
          gst_caps_from_string (GST_VIDEO_CAPS_GRAY16 ("1234")));
      gst_caps_append (caps,
          gst_caps_from_string (GST_VIDEO_CAPS_GRAY16 ("4321")));
      break;
  }
  return caps;
}
