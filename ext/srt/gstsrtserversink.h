/* GStreamer
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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

#ifndef __GST_SRT_SERVER_SINK_H__
#define __GST_SRT_SERVER_SINK_H__

#include "gstsrtbasesink.h"

G_BEGIN_DECLS

#define GST_TYPE_SRT_SERVER_SINK              (gst_srt_server_sink_get_type ())
#define GST_IS_SRT_SERVER_SINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SRT_SERVER_SINK))
#define GST_IS_SRT_SERVER_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SRT_SERVER_SINK))
#define GST_SRT_SERVER_SINK_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SRT_SERVER_SINK, GstSRTServerSinkClass))
#define GST_SRT_SERVER_SINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SRT_SERVER_SINK, GstSRTServerSink))
#define GST_SRT_SERVER_SINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SRT_SERVER_SINK, GstSRTServerSinkClass))
#define GST_SRT_SERVER_SINK_CAST(obj)         ((GstSRTServerSink*)(obj))
#define GST_SRT_SERVER_SINK_CLASS_CAST(klass) ((GstSRTServerSinkClass*)(klass))

typedef struct _GstSRTServerSink GstSRTServerSink;
typedef struct _GstSRTServerSinkClass GstSRTServerSinkClass;
typedef struct _GstSRTServerSinkPrivate GstSRTServerSinkPrivate;

struct _GstSRTServerSink {
  GstSRTBaseSink parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSRTServerSinkClass {
  GstSRTBaseSinkClass parent_class;

  void (*client_added)      (GstSRTServerSink *self, int sock, struct sockaddr *addr, int addr_len);
  void (*client_removed)    (GstSRTServerSink *self, int sock, struct sockaddr *addr, int addr_len);

  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_EXPORT
GType gst_srt_server_sink_get_type (void);

G_END_DECLS

#endif /* __GST_SRT_SERVER_SINK_H__ */
