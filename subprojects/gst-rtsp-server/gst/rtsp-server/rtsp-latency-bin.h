/* GStreamer
 * Copyright (C) 2018 Ognyan Tonchev <ognyan@axis.com>
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

#ifndef __GST_RTSP_LATENCY_BIN_H__
#define __GST_RTSP_LATENCY_BIN_H__

#include <gst/gst.h>
#include "rtsp-server-prelude.h"

G_BEGIN_DECLS

typedef struct _GstRTSPLatencyBin GstRTSPLatencyBin;
typedef struct _GstRTSPLatencyBinClass GstRTSPLatencyBinClass;
typedef struct _GstRTSPLatencyBinPrivate GstRTSPLatencyBinPrivate;

#define GST_RTSP_LATENCY_BIN_TYPE                 (gst_rtsp_latency_bin_get_type ())
#define IS_GST_RTSP_LATENCY_BIN(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_RTSP_LATENCY_BIN_TYPE))
#define IS_GST_RTSP_LATENCY_BIN_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_RTSP_LATENCY_BIN_TYPE))
#define GST_RTSP_LATENCY_BIN_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_RTSP_LATENCY_BIN_TYPE, GstRTSPLatencyBinClass))
#define GST_RTSP_LATENCY_BIN(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_RTSP_LATENCY_BIN_TYPE, GstRTSPLatencyBin))
#define GST_RTSP_LATENCY_BIN_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_RTSP_LATENCY_BIN_TYPE, GstRTSPLatencyBinClass))
#define GST_RTSP_LATENCY_BIN_CAST(obj)            ((GstRTSPLatencyBin*)(obj))
#define GST_RTSP_LATENCY_BIN_CLASS_CAST(klass)    ((GstRTSPLatencyBinClass*)(klass))

struct _GstRTSPLatencyBin {
  GstBin parent;

  GstRTSPLatencyBinPrivate *priv;
};

struct _GstRTSPLatencyBinClass {
  GstBinClass parent_class;
};

GST_RTSP_SERVER_API
GType gst_rtsp_latency_bin_get_type (void);

GST_RTSP_SERVER_API
GstElement * gst_rtsp_latency_bin_new (GstElement * element);

G_END_DECLS

#endif /* __GST_RTSP_LATENCY_BIN_H__ */
