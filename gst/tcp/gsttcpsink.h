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


#ifndef __GST_TCPSINK_H__
#define __GST_TCPSINK_H__


#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

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

#define GST_TYPE_TCPSINK \
  (gst_tcpsink_get_type())
#define GST_TCPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TCPSINK,GstTCPSink))
#define GST_TCPSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TCPSINK,GstTCPSink))
#define GST_IS_TCPSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TCPSINK))
#define GST_IS_TCPSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TCPSINK))

  typedef struct _GstTCPSink GstTCPSink;
  typedef struct _GstTCPSinkClass GstTCPSinkClass;

  typedef enum
  {
    GST_TCPSINK_OPEN = GST_ELEMENT_FLAG_LAST,

    GST_TCPSINK_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
  } GstTCPSinkFlags;

  struct _GstTCPSink
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad, *srcpad;

    int sock;
    struct sockaddr_in theiraddr;
    Gst_TCP_Control control;

    gint port;
    gchar *host;

    guint mtu;

    GstClock *clock;
  };

  struct _GstTCPSinkClass
  {
    GstElementClass parent_class;
  };

  GType gst_tcpsink_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_TCPSINK_H__ */
