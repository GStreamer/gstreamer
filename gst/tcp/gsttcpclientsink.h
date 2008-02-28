/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_TCP_CLIENT_SINK_H__
#define __GST_TCP_CLIENT_SINK_H__


#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include "gsttcp.h"

G_BEGIN_DECLS

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include "gsttcp.h"

#define GST_TYPE_TCP_CLIENT_SINK \
  (gst_tcp_client_sink_get_type())
#define GST_TCP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCP_CLIENT_SINK,GstTCPClientSink))
#define GST_TCP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCP_CLIENT_SINK,GstTCPClientSinkClass))
#define GST_IS_TCP_CLIENT_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCP_CLIENT_SINK))
#define GST_IS_TCP_CLIENT_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCP_CLIENT_SINK))

typedef struct _GstTCPClientSink GstTCPClientSink;
typedef struct _GstTCPClientSinkClass GstTCPClientSinkClass;

typedef enum {
  GST_TCP_CLIENT_SINK_OPEN             = (GST_ELEMENT_FLAG_LAST << 0),

  GST_TCP_CLIENT_SINK_FLAG_LAST        = (GST_ELEMENT_FLAG_LAST << 2),
} GstTCPClientSinkFlags;

struct _GstTCPClientSink {
  GstBaseSink element;

  /* server information */
  int port;
  gchar *host;
  struct sockaddr_in server_sin;

  /* socket */
  GstPollFD sock_fd;

  size_t data_written; /* how much bytes have we written ? */
  GstTCPProtocol protocol; /* used with the protocol enum */
  gboolean caps_sent; /* whether or not we sent caps already */
};

struct _GstTCPClientSinkClass {
  GstBaseSinkClass parent_class;
};

GType gst_tcp_client_sink_get_type(void);

G_END_DECLS

#endif /* __GST_TCP_CLIENT_SINK_H__ */
