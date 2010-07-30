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

#include "gstvdpvideobuffer.h"

#include "gstvdputils.h"

static void
gst_vdp_video_remove_pixel_aspect_ratio (GstStructure * structure)
{
  gint par_n, par_d;

  if (gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_n,
          &par_d)) {
    gint width;

    gst_structure_get_int (structure, "width", &width);
    width = gst_util_uint64_scale_int (width, par_n, par_d);
    gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);

    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }
}

GstCaps *
gst_vdp_video_to_output_caps (GstCaps * video_caps)
{
  GstCaps *output_caps;
  gint i;

  g_return_val_if_fail (GST_IS_CAPS (video_caps), NULL);

  output_caps = gst_caps_copy (video_caps);
  for (i = 0; i < gst_caps_get_size (video_caps); i++) {

    GstStructure *structure, *rgb_structure;

    structure = gst_caps_get_structure (output_caps, i);
    if (!gst_structure_has_name (structure, "video/x-vdpau-video"))
      goto not_video_error;

    rgb_structure = gst_structure_copy (structure);

    gst_structure_set_name (structure, "video/x-vdpau-output");
    gst_structure_remove_field (structure, "chroma-type");
    gst_vdp_video_remove_pixel_aspect_ratio (structure);

    gst_structure_set_name (rgb_structure, "video/x-raw-rgb");
    gst_structure_remove_field (rgb_structure, "chroma-type");
    gst_vdp_video_remove_pixel_aspect_ratio (rgb_structure);
    gst_caps_append_structure (output_caps, rgb_structure);
  }

  return output_caps;

error:
  gst_caps_unref (output_caps);
  return NULL;

not_video_error:
  GST_WARNING ("The caps weren't of type \"video/x-vdpau-video\"");
  goto error;
}

GstCaps *
gst_vdp_yuv_to_video_caps (GstCaps * yuv_caps)
{
  GstCaps *video_caps;
  gint i;

  g_return_val_if_fail (GST_IS_CAPS (yuv_caps), NULL);

  video_caps = gst_caps_copy (yuv_caps);
  for (i = 0; i < gst_caps_get_size (video_caps); i++) {
    GstStructure *structure;
    guint32 fourcc;
    VdpChromaType chroma_type;

    structure = gst_caps_get_structure (video_caps, i);
    if (!gst_structure_has_name (structure, "video/x-raw-yuv"))
      goto not_yuv_error;

    if (!gst_structure_get_fourcc (structure, "format", &fourcc))
      goto no_format_error;

    chroma_type = -1;
    for (i = 0; i < G_N_ELEMENTS (formats); i++) {
      if (formats[i].fourcc == fourcc) {
        chroma_type = formats[i].chroma_type;
        break;
      }
    }

    if (chroma_type == -1)
      goto no_chroma_error;

    /* now we transform the caps */
    gst_structure_set_name (structure, "video/x-vdpau-video");
    gst_structure_remove_field (structure, "format");
    gst_structure_set (structure, "chroma-type", G_TYPE_INT, chroma_type, NULL);
  }

  return video_caps;

error:
  gst_caps_unref (video_caps);
  return NULL;

not_yuv_error:
  GST_WARNING ("The caps weren't of type \"video/x-raw-yuv\"");
  goto error;

no_format_error:
  GST_WARNING ("The caps didn't have a \"fourcc\" field");
  goto error;

no_chroma_error:
  GST_WARNING ("The caps had an invalid \"fourcc\" field");
  goto error;

}
