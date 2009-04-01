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

#ifndef _GST_VDPAU_DEVICE_H_
#define _GST_VDPAU_DEVICE_H_

#include <X11/Xlib.h>
#include <vdpau/vdpau.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_VDPAU_DEVICE             (gst_vdpau_device_get_type ())
#define GST_VDPAU_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDPAU_DEVICE, GstVdpauDevice))
#define GST_VDPAU_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDPAU_DEVICE, GstVdpauDeviceClass))
#define GST_IS_VDPAU_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDPAU_DEVICE))
#define GST_IS_VDPAU_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDPAU_DEVICE))
#define GST_VDPAU_DEVICE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDPAU_DEVICE, GstVdpauDeviceClass))

typedef struct _GstVdpauDeviceClass GstVdpauDeviceClass;
typedef struct _GstVdpauDevice GstVdpauDevice;

struct _GstVdpauDeviceClass
{
  GObjectClass parent_class;
};

struct _GstVdpauDevice
{
  GObject object;

  gchar *display_name;
  Display *display;
  VdpDevice device;

  VdpDeviceDestroy                                *vdp_device_destroy;
  VdpGetProcAddress                               *vdp_get_proc_address;
  VdpGetErrorString                               *vdp_get_error_string;

  VdpVideoSurfaceCreate                           *vdp_video_surface_create;
  VdpVideoSurfaceDestroy                          *vdp_video_surface_destroy;
  VdpVideoSurfaceQueryCapabilities                *vdp_video_surface_query_capabilities;
  VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *vdp_video_surface_query_ycbcr_capabilities;
  VdpVideoSurfaceGetParameters                    *vdp_video_surface_get_parameters;
  VdpVideoSurfaceGetBitsYCbCr                     *vdp_video_surface_get_bits_ycbcr;

  VdpDecoderCreate                                *vdp_decoder_create;
  VdpDecoderDestroy                               *vdp_decoder_destroy;
  VdpDecoderRender                                *vdp_decoder_render;
  VdpDecoderQueryCapabilities                     *vdp_decoder_query_capabilities;
  VdpDecoderGetParameters                         *vdp_decoder_get_parameters;
};

GType gst_vdpau_device_get_type (void) G_GNUC_CONST;

GstVdpauDevice *gst_vdpau_device_new (const gchar *display_name);

GstVdpauDevice *gst_vdpau_get_device (const gchar *display_name);

G_END_DECLS

#endif /* _GST_VDPAU_DEVICE_H_ */
