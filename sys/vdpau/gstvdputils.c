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
gst_vdp_video_to_yuv_caps (GstCaps * caps, GstVdpDevice * device)
{
  GstCaps *new_caps, *allowed_caps, *result;
  gint i;
  GstStructure *structure;

  new_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    gint chroma_type;
    GSList *fourcc = NULL, *iter;

    structure = gst_caps_get_structure (caps, i);

    if (gst_structure_get_int (structure, "chroma-type", &chroma_type)) {
      /* calculate fourcc from chroma_type */
      for (i = 0; i < G_N_ELEMENTS (formats); i++) {
        if (formats[i].chroma_type == chroma_type) {
          fourcc = g_slist_append (fourcc, GINT_TO_POINTER (formats[i].fourcc));
        }
      }
    } else {
      for (i = 0; i < G_N_ELEMENTS (formats); i++) {
        fourcc = g_slist_append (fourcc, GINT_TO_POINTER (formats[i].fourcc));
      }
    }

    for (iter = fourcc; iter; iter = iter->next) {
      GstStructure *new_struct = gst_structure_copy (structure);

      gst_structure_set_name (new_struct, "video/x-raw-yuv");
      gst_structure_remove_field (new_struct, "chroma-type");
      gst_structure_set (new_struct, "format", GST_TYPE_FOURCC,
          GPOINTER_TO_INT (iter->data), NULL);

      gst_caps_append_structure (new_caps, new_struct);
    }

    g_slist_free (fourcc);
  }
  structure = gst_caps_get_structure (caps, 0);

  if (device) {
    allowed_caps = gst_vdp_video_buffer_get_allowed_yuv_caps (device);
    result = gst_caps_intersect (new_caps, allowed_caps);

    gst_caps_unref (new_caps);
    gst_caps_unref (allowed_caps);
  } else
    result = new_caps;

  return result;
}

GstCaps *
gst_vdp_yuv_to_video_caps (GstCaps * caps, GstVdpDevice * device)
{
  GstCaps *new_caps, *result;
  gint i;

  new_caps = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (new_caps); i++) {
    GstStructure *structure = gst_caps_get_structure (new_caps, i);
    guint32 fourcc;

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      gint chroma_type = -1;

      /* calculate chroma type from fourcc */
      for (i = 0; i < G_N_ELEMENTS (formats); i++) {
        if (formats[i].fourcc == fourcc) {
          chroma_type = formats[i].chroma_type;
          break;
        }
      }
      gst_structure_remove_field (structure, "format");
      gst_structure_set (structure, "chroma-type", G_TYPE_INT, chroma_type,
          NULL);
    } else
      gst_structure_set (structure, "chroma-type", GST_TYPE_INT_RANGE, 0, 2,
          NULL);

    gst_structure_set_name (structure, "video/x-vdpau-video");
  }

  if (device) {
    GstCaps *allowed_caps;

    allowed_caps = gst_vdp_video_buffer_get_allowed_video_caps (device);
    result = gst_caps_intersect (new_caps, allowed_caps);

    gst_caps_unref (new_caps);
    gst_caps_unref (allowed_caps);
  } else
    result = new_caps;

  return result;
}

GstCaps *
gst_vdp_yuv_to_output_caps (GstCaps * caps)
{
  GstCaps *result;
  gint i;

  result = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (result); i++) {
    GstStructure *structure = gst_caps_get_structure (result, i);

    gst_structure_set_name (structure, "video/x-vdpau-output");
    gst_structure_remove_field (structure, "format");
  }

  return result;
}

GstCaps *
gst_vdp_video_to_output_caps (GstCaps * caps)
{
  GstCaps *result;
  gint i;

  result = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (result); i++) {
    GstStructure *structure = gst_caps_get_structure (result, i);
    gint par_n, par_d;

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
  }

  return result;
}
