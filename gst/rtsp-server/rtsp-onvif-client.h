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

#ifndef __GST_RTSP_ONVIF_CLIENT_H__
#define __GST_RTSP_ONVIF_CLIENT_H__

#include <gst/gst.h>
#include "rtsp-client.h"

#define GST_TYPE_RTSP_ONVIF_CLIENT              (gst_rtsp_onvif_client_get_type ())
#define GST_IS_RTSP_ONVIF_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_ONVIF_CLIENT))
#define GST_IS_RTSP_ONVIF_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_ONVIF_CLIENT))
#define GST_RTSP_ONVIF_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_ONVIF_CLIENT, GstRTSPOnvifClientClass))
#define GST_RTSP_ONVIF_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_ONVIF_CLIENT, GstRTSPOnvifClient))
#define GST_RTSP_ONVIF_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_ONVIF_CLIENT, GstRTSPOnvifClientClass))
#define GST_RTSP_ONVIF_CLIENT_CAST(obj)         ((GstRTSPOnvifClient*)(obj))
#define GST_RTSP_ONVIF_CLIENT_CLASS_CAST(klass) ((GstRTSPOnvifClientClass*)(klass))

typedef struct GstRTSPOnvifClientClass GstRTSPOnvifClientClass;
typedef struct GstRTSPOnvifClient GstRTSPOnvifClient;

struct GstRTSPOnvifClientClass
{
  GstRTSPClientClass parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct GstRTSPOnvifClient
{
  GstRTSPClient parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType gst_rtsp_onvif_client_get_type (void);

#endif /* __GST_RTSP_ONVIF_CLIENT_H__ */
