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

#include "gstvdputils.h"

static GstCaps *
gst_vdp_get_allowed_yuv_caps (GstVdpDevice * device)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < N_CHROMA_TYPES; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    status =
        device->vdp_video_surface_query_capabilities (device->device,
        chroma_types[i], &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE) {
      GST_ERROR_OBJECT (device,
          "Could not get query VDPAU video surface capabilites, "
          "Error returned from vdpau was: %s",
          device->vdp_get_error_string (status));

      goto error;
    }
    if (is_supported) {
      gint j;

      for (j = 0; j < N_FORMATS; j++) {
        if (formats[j].chroma_type != chroma_types[i])
          continue;

        status =
            device->vdp_video_surface_query_ycbcr_capabilities (device->device,
            formats[j].chroma_type, formats[j].format, &is_supported);
        if (status != VDP_STATUS_OK
            && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT) {
          GST_ERROR_OBJECT (device, "Could not query VDPAU YCbCr capabilites, "
              "Error returned from vdpau was: %s",
              device->vdp_get_error_string (status));

          goto error;
        }

        if (is_supported) {
          GstCaps *format_caps;

          format_caps = gst_caps_new_simple ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, formats[j].fourcc,
              "width", GST_TYPE_INT_RANGE, 1, max_w,
              "height", GST_TYPE_INT_RANGE, 1, max_h, NULL);
          gst_caps_append (caps, format_caps);
        }
      }
    }
  }

error:
  return caps;
}

GstCaps *
gst_vdp_video_to_yuv_caps (GstCaps * caps)
{
  GstCaps *new_caps, *allowed_caps, *result;
  gint i;
  GstStructure *structure;
  const GValue *value;
  GstVdpDevice *device = NULL;

  new_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    gint chroma_type;
    GSList *fourcc = NULL, *iter;

    structure = gst_caps_get_structure (caps, i);

    if (gst_structure_get_int (structure, "chroma-type", &chroma_type)) {
      /* calculate fourcc from chroma_type */
      for (i = 0; i < N_FORMATS; i++) {
        if (formats[i].chroma_type == chroma_type) {
          fourcc = g_slist_append (fourcc, GINT_TO_POINTER (formats[i].fourcc));
        }
      }
    } else {
      for (i = 0; i < N_FORMATS; i++) {
        fourcc = g_slist_append (fourcc, GINT_TO_POINTER (formats[i].fourcc));
      }
    }

    for (iter = fourcc; iter; iter = iter->next) {
      GstStructure *new_struct = gst_structure_copy (structure);

      gst_structure_set_name (new_struct, "video/x-raw-yuv");
      gst_structure_remove_field (new_struct, "chroma-type");
      gst_structure_remove_field (new_struct, "device");
      gst_structure_set (new_struct, "format", GST_TYPE_FOURCC,
          GPOINTER_TO_INT (iter->data), NULL);

      gst_caps_append_structure (new_caps, new_struct);
    }

    g_slist_free (fourcc);
  }
  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "device");
  if (value)
    device = g_value_get_object (value);

  if (device) {
    allowed_caps = gst_vdp_get_allowed_yuv_caps (device);
    result = gst_caps_intersect (new_caps, allowed_caps);

    gst_caps_unref (new_caps);
    gst_caps_unref (allowed_caps);
  } else
    result = new_caps;

  return result;
}

static GstCaps *
gst_vdp_get_allowed_video_caps (GstVdpDevice * device)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < N_CHROMA_TYPES; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    status =
        device->vdp_video_surface_query_capabilities (device->device,
        chroma_types[i], &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE) {
      GST_ERROR_OBJECT (device,
          "Could not get query VDPAU video surface capabilites, "
          "Error returned from vdpau was: %s",
          device->vdp_get_error_string (status));

      goto error;
    }

    if (is_supported) {
      GstCaps *format_caps;

      format_caps = gst_caps_new_simple ("video/x-vdpau-video",
          "chroma-type", G_TYPE_INT, chroma_types[i],
          "width", GST_TYPE_INT_RANGE, 1, max_w,
          "height", GST_TYPE_INT_RANGE, 1, max_h, NULL);
      gst_caps_append (caps, format_caps);
    }
  }

error:
  return caps;
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
      for (i = 0; i < N_FORMATS; i++) {
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
    if (device)
      gst_structure_set (structure, "device", G_TYPE_OBJECT, device, NULL);
  }

  if (device) {
    GstCaps *allowed_caps;

    allowed_caps = gst_vdp_get_allowed_video_caps (device);
    result = gst_caps_intersect (new_caps, allowed_caps);

    gst_caps_unref (new_caps);
    gst_caps_unref (allowed_caps);
  } else
    result = new_caps;

  return result;
}
