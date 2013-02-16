/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oleavr@soundrop.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_MIO_VIDEO_DEVICE_H__
#define __GST_MIO_VIDEO_DEVICE_H__

#include <gst/gst.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_TYPE_MIO_VIDEO_DEVICE \
  (gst_mio_video_device_get_type ())
#define GST_MIO_VIDEO_DEVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIO_VIDEO_DEVICE, GstMIOVideoDevice))
#define GST_MIO_VIDEO_DEVICE_CAST(obj) \
  ((GstMIOVideoDevice *) (obj))
#define GST_MIO_VIDEO_DEVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIO_VIDEO_DEVICE, GstMIOVideoDeviceClass))
#define GST_IS_MIO_VIDEO_DEVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIO_VIDEO_DEVICE))
#define GST_IS_MIO_VIDEO_DEVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIO_VIDEO_DEVICE))

typedef struct _GstMIOVideoDevice         GstMIOVideoDevice;
typedef struct _GstMIOVideoDeviceClass    GstMIOVideoDeviceClass;

struct _GstMIOVideoDevice
{
  GObject parent;

  GstCoreMediaCtx *ctx;
  TundraObjectID handle;

  gchar *cached_uid;
  gchar *cached_name;
  TundraDeviceTransportType cached_transport;
  GstCaps *cached_caps;
  CMFormatDescriptionRef selected_format;
  gint selected_fps_n, selected_fps_d;
};

struct _GstMIOVideoDeviceClass
{
  GObjectClass parent_class;
};

GType gst_mio_video_device_get_type (void);

TundraObjectID gst_mio_video_device_get_handle (GstMIOVideoDevice * self);
const gchar * gst_mio_video_device_get_uid (GstMIOVideoDevice * self);
const gchar * gst_mio_video_device_get_name (GstMIOVideoDevice * self);
TundraDeviceTransportType gst_mio_video_device_get_transport_type (
    GstMIOVideoDevice * self);

gboolean gst_mio_video_device_open (GstMIOVideoDevice * self);
void gst_mio_video_device_close (GstMIOVideoDevice * self);

GstCaps * gst_mio_video_device_get_available_caps (GstMIOVideoDevice * self);
gboolean gst_mio_video_device_set_caps (GstMIOVideoDevice * self,
    GstCaps * caps);
CMFormatDescriptionRef gst_mio_video_device_get_selected_format (
    GstMIOVideoDevice * self);
GstClockTime gst_mio_video_device_get_duration (GstMIOVideoDevice * self);

void gst_mio_video_device_print_debug_info (GstMIOVideoDevice * self);

GList * gst_mio_video_device_list_create (GstCoreMediaCtx * ctx);
void gst_mio_video_device_list_destroy (GList * devices);

G_END_DECLS

#endif /* __GST_MIO_VIDEO_DEVICE_H__ */
