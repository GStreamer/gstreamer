/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_RTSP_ONVIF_SERVER_H__
#define __GST_RTSP_ONVIF_SERVER_H__

#include <gst/gst.h>
#include "rtsp-server.h"

#define GST_TYPE_RTSP_ONVIF_SERVER              (gst_rtsp_onvif_server_get_type ())
#define GST_IS_RTSP_ONVIF_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_ONVIF_SERVER))
#define GST_IS_RTSP_ONVIF_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_ONVIF_SERVER))
#define GST_RTSP_ONVIF_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_ONVIF_SERVER, GstRTSPOnvifServerClass))
#define GST_RTSP_ONVIF_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_ONVIF_SERVER, GstRTSPOnvifServer))
#define GST_RTSP_ONVIF_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_ONVIF_SERVER, GstRTSPOnvifServerClass))
#define GST_RTSP_ONVIF_SERVER_CAST(obj)         ((GstRTSPOnvifServer*)(obj))
#define GST_RTSP_ONVIF_SERVER_CLASS_CAST(klass) ((GstRTSPOnvifServerClass*)(klass))

typedef struct GstRTSPOnvifServerClass GstRTSPOnvifServerClass;
typedef struct GstRTSPOnvifServer GstRTSPOnvifServer;

struct GstRTSPOnvifServerClass
{
  GstRTSPServerClass parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct GstRTSPOnvifServer
{
  GstRTSPServer parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType gst_rtsp_onvif_server_get_type (void);
GST_RTSP_SERVER_API
GstRTSPServer *gst_rtsp_onvif_server_new (void);

#define GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT "www.onvif.org/ver20/backchannel"

#include "rtsp-onvif-client.h"
#include "rtsp-onvif-media-factory.h"
#include "rtsp-onvif-media.h"

#endif /* __GST_RTSP_ONVIF_SERVER_H__ */
