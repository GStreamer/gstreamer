/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_TCPSERVERSINK_H__
#define __GST_TCPSERVERSINK_H__


#include <gst/gst.h>

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
#include "gstmultifdsink.h"

#define GST_TYPE_TCPSERVERSINK \
  (gst_tcpserversink_get_type())
#define GST_TCPSERVERSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPSERVERSINK,GstTCPServerSink))
#define GST_TCPSERVERSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPSERVERSINK,GstTCPServerSink))
#define GST_IS_TCPSERVERSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPSERVERSINK))
#define GST_IS_TCPSERVERSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPSERVERSINK))

typedef struct _GstTCPServerSink GstTCPServerSink;
typedef struct _GstTCPServerSinkClass GstTCPServerSinkClass;

typedef enum {
  GST_TCPSERVERSINK_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_TCPSERVERSINK_FLAG_LAST        = GST_ELEMENT_FLAG_LAST + 2,
} GstTCPServerSinkFlags;

struct _GstTCPServerSink {
  GstMultiFdSink element;

  /* server information */
  int server_port;
  gchar *host;
  struct sockaddr_in server_sin;

  /* socket */
  int server_sock_fd;
};

struct _GstTCPServerSinkClass {
  GstMultiFdSinkClass parent_class;
};

GType gst_tcpserversink_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_TCPSERVERSINK_H__ */
