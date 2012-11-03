/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#ifndef __GST_DCCP_CLIENT_SINK_H__
#define __GST_DCCP_CLIENT_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#include "gstdccp_common.h"

#define GST_TYPE_DCCP_CLIENT_SINK \
  (gst_dccp_client_sink_get_type())
#define GST_DCCP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DCCP_CLIENT_SINK,GstDCCPClientSink))
#define GST_DCCP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DCCP_CLIENT_SINK,GstDCCPClientSinkClass))
#define GST_IS_DCCP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DCCP_CLIENT_SINK))
#define GST_IS_DCCP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DCCP_CLIENT_SINK))

typedef struct _GstDCCPClientSink GstDCCPClientSink;
typedef struct _GstDCCPClientSinkClass GstDCCPClientSinkClass;


/**
 * GstDCCPClientSink:
 *
 * dccpclientsink object structure.
 */
struct _GstDCCPClientSink
{
  GstBaseSink element;

  /* server information */
  int port;
  gchar *host;
  struct sockaddr_in server_sin;

  /* socket */
  int sock_fd;
  gboolean closed;

  int pksize;

  GstCaps *caps;
  uint8_t ccid;
};

struct _GstDCCPClientSinkClass
{
  GstBaseSinkClass parent_class;

  /* signals */
  void (*connected) (GstElement *sink, gint fd);
};

GType gst_dccp_client_sink_get_type (void);

G_END_DECLS

#endif /* __GST_DCCP_CLIENT_SRC_H__ */
