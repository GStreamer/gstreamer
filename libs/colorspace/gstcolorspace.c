/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

//#define DEBUG_ENABLED
#include <gst/gst.h>

#include <gstcolorspace.h>

extern GstColorSpaceConverter gst_colorspace_rgb2rgb_get_converter(GstColorSpace srcspace, 
		                                                      GstColorSpace destspace);
extern GstColorSpaceConverter gst_colorspace_yuv2rgb_get_converter(GstColorSpace srcspace, 
		                                                      GstColorSpace destspace);
extern GstColorSpaceConverter gst_colorspace_rgb2yuv_get_converter(GstColorSpace srcspace, 
		                                                      GstColorSpace destspace);
extern GstColorSpaceConverter gst_colorspace_yuv2yuv_get_converter(GstColorSpace srcspace, 
		                                                      GstColorSpace destspace);

GstBuffer *gst_colorspace_convert(GstBuffer *src, GstColorSpace dest) {
  switch (dest) {
    default:
      break;
  }

  return src;
}

GstColorSpaceConverter gst_colorspace_get_converter(GstColorSpace srcspace, GstColorSpace destspace) {
  DEBUG("gst_colorspace: get converter\n");
  if (GST_COLORSPACE_IS_RGB_TYPE(srcspace)) {
    if (GST_COLORSPACE_IS_RGB_TYPE(destspace)) {
      return gst_colorspace_rgb2rgb_get_converter(srcspace, destspace);
    }
    else {
      //return gst_colorspace_rgb2yuv_get_converter(srcspace, destspace);
    }
  }
  else if (GST_COLORSPACE_IS_YUV_TYPE(srcspace)) {
    if (GST_COLORSPACE_IS_RGB_TYPE(destspace)) {
      return gst_colorspace_yuv2rgb_get_converter(srcspace, destspace);
    }
    else {
      //return gst_colorspace_yuv2yuv_get_converter(srcspace, destspace);
    }
  }
  else {
    return NULL;
  }
  return NULL;
}
