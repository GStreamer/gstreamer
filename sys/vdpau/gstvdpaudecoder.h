/* 
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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
 
#ifndef __GST_VDPAU_DECODER_H__
#define __GST_VDPAU_DECODER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <X11/Xlib.h>
#include <vdpau/vdpau.h>

G_BEGIN_DECLS

#define GST_TYPE_VDPAU_DECODER            (gst_vdpaudecoder_get_type())
#define GST_VDPAU_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDPAU_DECODER,GstVdpauDecoder))
#define GST_VDPAU_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDPAU_DECODER,GstVdpauDecoderClass))
#define GST_VDPAU_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDPAU_DECODER, GstVdpauDecoderClass))
#define GST_IS_VDPAU_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDPAU_DECODER))
#define GST_IS_VDPAU_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDPAU_DECODER))

typedef struct _GstVdpauDecoder      GstVdpauDecoder;
typedef struct _GstVdpauDecoderClass GstVdpauDecoderClass;
typedef struct _VdpauFunctions VdpauFunctions;

struct _GstVdpauDecoder {
  GstElement element;

  gchar *display_name;
  Display *display;
  VdpDevice device;

  VdpauFunctions *functions;

  GstPad *src;
  GstPad *sink;

  GstCaps *src_caps;

  gint width, height;
  guint32 format;

  gboolean silent;
};

struct _GstVdpauDecoderClass {
  GstBaseTransformClass parent_class;

  gboolean (*set_caps) (GstVdpauDecoder *dec, GstCaps *caps);
};

struct _VdpauFunctions {
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

GType gst_vdpaudecoder_get_type (void);

gboolean gst_vdpaudecoder_push_video_surface (GstVdpauDecoder * dec, VdpVideoSurface surface);

G_END_DECLS

#endif /* __GST_VDPAU_DECODER_H__ */
