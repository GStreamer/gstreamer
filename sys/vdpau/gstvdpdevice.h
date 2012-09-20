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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_VDP_DEVICE_H_
#define _GST_VDP_DEVICE_H_

#include <X11/Xlib.h>
#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VDP_DEVICE             (gst_vdp_device_get_type ())
#define GST_VDP_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_DEVICE, GstVdpDevice))
#define GST_VDP_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDP_DEVICE, GstVdpDeviceClass))
#define GST_IS_VDP_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_DEVICE))
#define GST_IS_VDP_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDP_DEVICE))
#define GST_VDP_DEVICE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_DEVICE, GstVdpDeviceClass))

typedef struct _GstVdpDeviceClass GstVdpDeviceClass;
typedef struct _GstVdpDevice GstVdpDevice;

struct _GstVdpDeviceClass
{
  GObjectClass parent_class;
};

struct _GstVdpDevice
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
  VdpVideoSurfacePutBitsYCbCr                     *vdp_video_surface_put_bits_ycbcr;

  VdpDecoderCreate                                *vdp_decoder_create;
  VdpDecoderDestroy                               *vdp_decoder_destroy;
  VdpDecoderRender                                *vdp_decoder_render;
  VdpDecoderQueryCapabilities                     *vdp_decoder_query_capabilities;
  VdpDecoderGetParameters                         *vdp_decoder_get_parameters;

  VdpVideoMixerCreate                             *vdp_video_mixer_create;
  VdpVideoMixerDestroy                            *vdp_video_mixer_destroy;
  VdpVideoMixerRender                             *vdp_video_mixer_render;
  VdpVideoMixerSetFeatureEnables                  *vdp_video_mixer_set_feature_enables;
  VdpVideoMixerSetAttributeValues                 *vdp_video_mixer_set_attribute_values;

  VdpOutputSurfaceCreate                          *vdp_output_surface_create;
  VdpOutputSurfaceDestroy                         *vdp_output_surface_destroy;
  VdpOutputSurfaceQueryCapabilities               *vdp_output_surface_query_capabilities;
  VdpOutputSurfaceGetBitsNative                   *vdp_output_surface_get_bits_native;

  VdpPresentationQueueTargetCreateX11             *vdp_presentation_queue_target_create_x11;
  VdpPresentationQueueTargetDestroy               *vdp_presentation_queue_target_destroy;
  
  VdpPresentationQueueCreate                      *vdp_presentation_queue_create;
  VdpPresentationQueueDestroy                     *vdp_presentation_queue_destroy;
  VdpPresentationQueueDisplay                     *vdp_presentation_queue_display;
  VdpPresentationQueueBlockUntilSurfaceIdle       *vdp_presentation_queue_block_until_surface_idle;
  VdpPresentationQueueSetBackgroundColor          *vdp_presentation_queue_set_background_color;
  VdpPresentationQueueQuerySurfaceStatus          *vdp_presentation_queue_query_surface_status;
};

GType gst_vdp_device_get_type (void);

GstVdpDevice *gst_vdp_get_device (const gchar *display_name, GError **error);

G_END_DECLS

#endif /* _GST_VDP_DEVICE_H_ */
