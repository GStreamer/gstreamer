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

#include "gstvdpauvideobuffer.h"

static GObjectClass *gst_vdpau_video_buffer_parent_class;

static void
gst_vdpau_video_buffer_finalize (GstVdpauVideoBuffer * buffer)
{
  GstVdpauDevice *device = buffer->device;
  VdpStatus status;

  status = device->vdp_video_surface_destroy (buffer->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR
        ("Couldn't destroy the buffers VdpVideoSurface, error returned was: %s",
        device->vdp_get_error_string (status));

  g_object_unref (buffer->device);

  GST_MINI_OBJECT_CLASS (gst_vdpau_video_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (buffer));
}

static void
gst_vdpau_video_buffer_init (GstVdpauVideoBuffer * buffer, gpointer g_class)
{
  buffer->device = NULL;
  buffer->surface = VDP_INVALID_HANDLE;
}

static void
gst_vdpau_video_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdpau_video_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdpau_video_buffer_finalize;
}


GType
gst_vdpau_video_buffer_get_type (void)
{
  static GType _gst_vdpau_video_buffer_type;

  if (G_UNLIKELY (_gst_vdpau_video_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_vdpau_video_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstVdpauVideoBuffer),
      0,
      (GInstanceInitFunc) gst_vdpau_video_buffer_init,
      NULL
    };
    _gst_vdpau_video_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstVdpauVideoBuffer", &info, 0);
  }
  return _gst_vdpau_video_buffer_type;
}


GstVdpauVideoBuffer *
gst_vdpau_video_buffer_new (GstVdpauDevice * device, VdpChromaType chroma_type,
    gint width, gint height)
{
  GstVdpauVideoBuffer *buffer;
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
      (GstVdpauVideoBuffer *) gst_mini_object_new (GST_TYPE_VDPAU_VIDEO_BUFFER);

  buffer->device = g_object_ref (device);
  buffer->surface = surface;

  return buffer;
}
