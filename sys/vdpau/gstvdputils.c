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

GstCaps *
gst_vdp_get_video_caps (GstVdpDevice * device, gint chroma_format)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < N_CHROMA_TYPES; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    if (chroma_format != chroma_types[i])
      continue;

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
  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return NULL;
  }

  return caps;
}
