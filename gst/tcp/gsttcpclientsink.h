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


#ifndef __GST_TCPCLIENTSINK_H__
#define __GST_TCPCLIENTSINK_H__


#include <gst/gst.h>
#include "gsttcp.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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

#define GST_TYPE_TCPCLIENTSINK \
  (gst_tcpclientsink_get_type())
#define GST_TCPCLIENTSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPCLIENTSINK,GstTCPClientSink))
#define GST_TCPCLIENTSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPCLIENTSINK,GstTCPClientSink))
#define GST_IS_TCPCLIENTSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPCLIENTSINK))
#define GST_IS_TCPCLIENTSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPCLIENTSINK))

typedef struct _GstTCPClientSink GstTCPClientSink;
typedef struct _GstTCPClientSinkClass GstTCPClientSinkClass;

typedef enum {
  GST_TCPCLIENTSINK_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_TCPCLIENTSINK_FLAG_LAST        = GST_ELEMENT_FLAG_LAST + 2,
} GstTCPClientSinkFlags;

struct _GstTCPClientSink {
  GstElement element;

  /* pad */
  GstPad *sinkpad;

  /* server information */
  int port;
  gchar *host;

  /* socket */
  int sock_fd;

  size_t data_written; /* how much bytes have we written ? */
  GstTCPProtocolType protocol; /* used with the protocol enum */
  gboolean caps_sent; /* whether or not we sent caps already */

  guint mtu;
  GstClock *clock;
};

struct _GstTCPClientSinkClass {
  GstElementClass parent_class;
};

GType gst_tcpclientsink_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_TCPCLIENTSINK_H__ */
