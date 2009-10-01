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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdpvideobuffer.h"

GstVdpVideoBuffer *
gst_vdp_video_buffer_new (GstVdpDevice * device, VdpChromaType chroma_type,
    gint width, gint height)
{
  GstVdpVideoBuffer *buffer;
  VdpStatus status;
  VdpVideoSurface surface;

  status = device->vdp_video_surface_create (device->device, chroma_type, width,
      height, &surface);
  if (status != VDP_STATUS_OK) {
    GST_ERROR ("Couldn't create a VdpVideoSurface, error returned was: %s",
        device->vdp_get_error_string (status));
    return NULL;
  }

  buffer =
      (GstVdpVideoBuffer *) gst_mini_object_new (GST_TYPE_VDP_VIDEO_BUFFER);

  buffer->device = g_object_ref (device);
  buffer->surface = surface;

  return buffer;
}

static GObjectClass *gst_vdp_video_buffer_parent_class;

static void
gst_vdp_video_buffer_finalize (GstVdpVideoBuffer * buffer)
{
  GstVdpDevice *device;
  VdpStatus status;

  device = buffer->device;

  status = device->vdp_video_surface_destroy (buffer->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR
        ("Couldn't destroy the buffers VdpVideoSurface, error returned was: %s",
        device->vdp_get_error_string (status));

  g_object_unref (buffer->device);

  GST_MINI_OBJECT_CLASS (gst_vdp_video_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (buffer));
}

static void
gst_vdp_video_buffer_init (GstVdpVideoBuffer * buffer, gpointer g_class)
{
  buffer->device = NULL;
  buffer->surface = VDP_INVALID_HANDLE;
}

static void
gst_vdp_video_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdp_video_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdp_video_buffer_finalize;
}


GType
gst_vdp_video_buffer_get_type (void)
{
  static GType _gst_vdp_video_buffer_type;

  if (G_UNLIKELY (_gst_vdp_video_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_vdp_video_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstVdpVideoBuffer),
      0,
      (GInstanceInitFunc) gst_vdp_video_buffer_init,
      NULL
    };
    _gst_vdp_video_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstVdpVideoBuffer", &info, 0);
  }
  return _gst_vdp_video_buffer_type;
}

GstCaps *
gst_vdp_video_buffer_get_allowed_yuv_caps (GstVdpDevice * device)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (chroma_types); i++) {
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

      for (j = 0; j < G_N_ELEMENTS (formats); j++) {
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
gst_vdp_video_buffer_get_allowed_video_caps (GstVdpDevice * device)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (chroma_types); i++) {
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
