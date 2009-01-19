/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>
#include <gst/rtsp/gstrtspconnection.h>

#ifndef __GST_RTSP_CLIENT_H__
#define __GST_RTSP_CLIENT_H__

#include "rtsp-media.h"
#include "rtsp-session-pool.h"

G_BEGIN_DECLS

#define GST_TYPE_RTSP_CLIENT              (gst_rtsp_client_get_type ())
#define GST_IS_RTSP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_CLIENT))
#define GST_IS_RTSP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_CLIENT))
#define GST_RTSP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_CLIENT, GstRTSPClientClass))
#define GST_RTSP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_CLIENT, GstRTSPClient))
#define GST_RTSP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_CLIENT, GstRTSPClientClass))
#define GST_RTSP_CLIENT_CAST(obj)         ((GstRTSPClient*)(obj))
#define GST_RTSP_CLIENT_CLASS_CAST(klass) ((GstRTSPClientClass*)(klass))

typedef struct _GstRTSPClient GstRTSPClient;
typedef struct _GstRTSPClientClass GstRTSPClientClass;

struct _GstRTSPClient {
  GObject       parent;

  GstRTSPConnection *connection;
  struct sockaddr_in address;

  GstRTSPMedia *media;

  GstRTSPSessionPool *pool;

  GThread *thread;
};

struct _GstRTSPClientClass {
  GObjectClass  parent_class;
};

GType                gst_rtsp_client_get_type          (void);

GstRTSPClient *      gst_rtsp_client_new               (void);

void                 gst_rtsp_client_set_session_pool  (GstRTSPClient *client, 
                                                        GstRTSPSessionPool *pool);
GstRTSPSessionPool * gst_rtsp_client_get_session_pool  (GstRTSPClient *client);

gboolean             gst_rtsp_client_accept            (GstRTSPClient *client, 
                                                        GIOChannel *channel);

G_END_DECLS

#endif /* __GST_RTSP_CLIENT_H__ */
