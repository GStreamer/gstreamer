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


#ifndef __GST_TCPCLIENTSRC_H__
#define __GST_TCPCLIENTSRC_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <netdb.h>                        /* sockaddr_in */
#include <netinet/in.h>			  /* sockaddr_in */
#include <unistd.h>
#include "gsttcp.h"

#define GST_TYPE_TCPCLIENTSRC \
  (gst_tcpclientsrc_get_type())
#define GST_TCPCLIENTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPCLIENTSRC,GstTCPClientSrc))
#define GST_TCPCLIENTSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPCLIENTSRC,GstTCPClientSrc))
#define GST_IS_TCPCLIENTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPCLIENTSRC))
#define GST_IS_TCPCLIENTSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPCLIENTSRC))

typedef struct _GstTCPClientSrc GstTCPClientSrc;
typedef struct _GstTCPClientSrcClass GstTCPClientSrcClass;

typedef enum {
  GST_TCPCLIENTSRC_OPEN       = GST_ELEMENT_FLAG_LAST,

  GST_TCPCLIENTSRC_FLAG_LAST,
} GstTCPClientSrcFlags;

struct _GstTCPClientSrc {
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* server information */
  int port;
  gchar *host;
  struct sockaddr_in server_sin;

  /* socket */
  int sock_fd;

  /* number of bytes we've gotten */
  off_t curoffset;

  GstTCPProtocolType protocol; /* protocol used for reading data */
  gboolean caps_received;      /* if we have received caps yet */
  GstCaps *caps;
  GstClock *clock;

  gboolean send_discont;       /* TRUE when we need to send a discont */
  GstBuffer *buffer_after_discont; /* temporary storage for buffer */
};

struct _GstTCPClientSrcClass {
  GstElementClass parent_class;
};

GType gst_tcpclientsrc_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_TCPCLIENTSRC_H__ */
