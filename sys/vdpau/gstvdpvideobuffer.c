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


void
gst_vdp_video_buffer_add_reference (GstVdpVideoBuffer * buffer,
    GstVdpVideoBuffer * buf)
{
  g_assert (GST_IS_VDP_VIDEO_BUFFER (buffer));
  g_assert (GST_IS_VDP_VIDEO_BUFFER (buf));

  gst_buffer_ref (GST_BUFFER (buf));
  buffer->refs = g_slist_prepend (buffer->refs, buf);
}

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
  GSList *iter;
  GstVdpDevice *device;
  VdpStatus status;

  device = buffer->device;

  status = device->vdp_video_surface_destroy (buffer->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR
        ("Couldn't destroy the buffers VdpVideoSurface, error returned was: %s",
        device->vdp_get_error_string (status));

  g_object_unref (buffer->device);

  for (iter = buffer->refs; iter; iter = g_slist_next (iter)) {
    GstBuffer *buf;

    buf = (GstBuffer *) (iter->data);
    gst_buffer_unref (buf);
  }
  g_slist_free (buffer->refs);

  GST_MINI_OBJECT_CLASS (gst_vdp_video_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (buffer));
}

static void
gst_vdp_video_buffer_init (GstVdpVideoBuffer * buffer, gpointer g_class)
{
  buffer->device = NULL;
  buffer->surface = VDP_INVALID_HANDLE;

  buffer->refs = NULL;
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
