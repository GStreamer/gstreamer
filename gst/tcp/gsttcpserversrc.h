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


#ifndef __GST_TCPSERVERSRC_H__
#define __GST_TCPSERVERSRC_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "gsttcp.h"

#include <fcntl.h>

#define GST_TYPE_TCPSERVERSRC \
  (gst_tcpserversrc_get_type())
#define GST_TCPSERVERSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPSERVERSRC,GstTCPServerSrc))
#define GST_TCPSERVERSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPSERVERSRC,GstTCPServerSrc))
#define GST_IS_TCPSERVERSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPSERVERSRC))
#define GST_IS_TCPSERVERSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPSERVERSRC))

typedef struct _GstTCPServerSrc GstTCPServerSrc;
typedef struct _GstTCPServerSrcClass GstTCPServerSrcClass;

typedef enum {
  GST_TCPSERVERSRC_OPEN       = GST_ELEMENT_FLAG_LAST,

  GST_TCPSERVERSRC_FLAG_LAST,
} GstTCPServerSrcFlags;

struct _GstTCPServerSrc {
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* server information */
  int server_port;
  gchar *host;
  struct sockaddr_in server_sin;
  int server_sock_fd;

  /* client information */
  int client_sock_fd;

  /* number of bytes we've gotten */
  off_t curoffset;

  GstTCPProtocolType protocol; /* protocol used for reading data */
  gboolean caps_received;      /* if we have received caps yet */
  GstClock *clock;
};

struct _GstTCPServerSrcClass {
  GstElementClass parent_class;
};

GType gst_tcpserversrc_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_TCPSERVERSRC_H__ */
