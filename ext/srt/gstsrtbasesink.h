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

#ifndef __GST_SRT_BASE_SINK_H__
#define __GST_SRT_BASE_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gio/gio.h>

#include <srt/srt.h>

G_BEGIN_DECLS

#define GST_TYPE_SRT_BASE_SINK              (gst_srt_base_sink_get_type ())
#define GST_IS_SRT_BASE_SINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SRT_BASE_SINK))
#define GST_IS_SRT_BASE_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SRT_BASE_SINK))
#define GST_SRT_BASE_SINK_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SRT_BASE_SINK, GstSRTBaseSinkClass))
#define GST_SRT_BASE_SINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SRT_BASE_SINK, GstSRTBaseSink))
#define GST_SRT_BASE_SINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SRT_BASE_SINK, GstSRTBaseSinkClass))
#define GST_SRT_BASE_SINK_CAST(obj)         ((GstSRTBaseSink*)(obj))
#define GST_SRT_BASE_SINK_CLASS_CAST(klass) ((GstSRTBaseSinkClass*)(klass))

typedef struct _GstSRTBaseSink GstSRTBaseSink;
typedef struct _GstSRTBaseSinkClass GstSRTBaseSinkClass;

struct _GstSRTBaseSink {
  GstBaseSink parent;

  GstUri *uri;
  GstBufferList *headers;
  gint latency;
  gchar *passphrase;
  gint key_length;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSRTBaseSinkClass {
  GstBaseSinkClass parent_class;

  /* ask the subclass to send a buffer */
  gboolean (*send_buffer)       (GstSRTBaseSink *self, const GstMapInfo *mapinfo);

  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_EXPORT
GType gst_srt_base_sink_get_type (void);

typedef gboolean (*GstSRTBaseSinkSendCallback) (GstSRTBaseSink *sink,
    const GstMapInfo *mapinfo, gpointer user_data);

gboolean gst_srt_base_sink_send_headers (GstSRTBaseSink *sink,
    GstSRTBaseSinkSendCallback send_cb, gpointer user_data);

GstStructure * gst_srt_base_sink_get_stats (GSocketAddress *sockaddr,
    SRTSOCKET sock);


G_END_DECLS

#endif /* __GST_SRT_BASE_SINK_H__ */
