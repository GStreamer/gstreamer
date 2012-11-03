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

#ifndef __GST_DCCP_SERVER_SINK_H__
#define __GST_DCCP_SERVER_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS


#include "gstdccp_common.h"
#include <pthread.h>

#define GST_TYPE_DCCP_SERVER_SINK \
  (gst_dccp_server_sink_get_type())
#define GST_DCCP_SERVER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DCCP_SERVER_SINK,GstDCCPServerSink))
#define GST_DCCP_SERVER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DCCP_SERVER_SINK,GstDCCPServerSinkClass))
#define GST_IS_DCCP_SERVER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DCCP_SERVER_SINK))
#define GST_IS_DCCP_SERVER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DCCP_SERVER_SINK))

typedef struct _GstDCCPServerSink GstDCCPServerSink;
typedef struct _GstDCCPServerSinkClass GstDCCPServerSinkClass;

typedef struct _Client Client;

struct _Client
{
  GstDCCPServerSink *server;
  GstBuffer * buf;
  int socket;
  int pksize;
  GstFlowReturn flow_status;
};

struct _GstDCCPServerSink
{
  GstBaseSink element;

  /* server information */
  int port;
  struct sockaddr_in server_sin;

  /* socket */
  int sock_fd;

  /* multiple clients */
  GList *clients;

  /* properties */
  int client_sock_fd;
  uint8_t ccid;
  gboolean wait_connections;
  gboolean closed;
};

struct _GstDCCPServerSinkClass
{
  GstBaseSinkClass parent_class;

  /* signals */
  void (*connected) (GstElement *sink, gint fd);
};

GType gst_dccp_server_sink_get_type (void);

G_END_DECLS

#endif /* __GST_DCCP_SERVER_SINK_H__ */
