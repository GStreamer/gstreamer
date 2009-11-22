/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#include <gst/vdpau/gstvdpvideobuffer.h>

#include "gstvdputils.h"

GstCaps *
gst_vdp_yuv_to_output_caps (GstCaps * caps)
{
  GstCaps *result;
  gint i;

  result = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure, *rgb_structure;

    structure = gst_caps_get_structure (result, i);
    rgb_structure = gst_structure_copy (structure);

    gst_structure_set_name (structure, "video/x-vdpau-output");
    gst_structure_remove_field (structure, "format");

    gst_structure_set_name (rgb_structure, "video/x-raw-rgb");
    gst_structure_remove_field (rgb_structure, "format");
    gst_caps_append_structure (result, rgb_structure);
  }

  return result;
}

GstCaps *
gst_vdp_video_to_output_caps (GstCaps * caps)
{
  GstCaps *result;
  gint i;

  result = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {

    GstStructure *structure, *rgb_structure;
    gint par_n, par_d;

    structure = gst_caps_get_structure (result, i);
    rgb_structure = gst_structure_copy (structure);

    gst_structure_set_name (structure, "video/x-vdpau-output");
    gst_structure_remove_field (structure, "chroma-type");

    if (gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_n,
            &par_d)) {
      gint width;

      gst_structure_get_int (structure, "width", &width);
      width = gst_util_uint64_scale_int (width, par_n, par_d);
      gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);

      gst_structure_remove_field (structure, "pixel-aspect-ratio");
    }

    gst_structure_set_name (rgb_structure, "video/x-raw-rgb");
    gst_structure_remove_field (rgb_structure, "chroma-type");
    gst_caps_append_structure (result, rgb_structure);
  }

  return result;
}
