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


#ifndef __GST_TCPSRC_H__
#define __GST_TCPSRC_H__

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
#include "gsttcpplugin.h"

#include <fcntl.h>

#define GST_TYPE_TCPSRC \
  (gst_tcpsrc_get_type())
#define GST_TCPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPSRC,GstTCPSrc))
#define GST_TCPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPSRC,GstTCPSrc))
#define GST_IS_TCPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPSRC))
#define GST_IS_TCPSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPSRC))

typedef struct _GstTCPSrc GstTCPSrc;
typedef struct _GstTCPSrcClass GstTCPSrcClass;

typedef enum {
  GST_TCPSRC_OPEN       = GST_ELEMENT_FLAG_LAST,
  GST_TCPSRC_1ST_BUF,
  GST_TCPSRC_CONNECTED,

  GST_TCPSRC_FLAG_LAST,
} GstTCPSrcFlags;

struct _GstTCPSrc {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  int port;
  int sock;
  int client_sock;
  int control_sock;
/*  gboolean socket_options;*/
  Gst_TCP_Control control;

  struct sockaddr_in myaddr;
  GstClock *clock;
};

struct _GstTCPSrcClass {
  GstElementClass parent_class;
};

GType gst_tcpsrc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_TCPSRC_H__ */
