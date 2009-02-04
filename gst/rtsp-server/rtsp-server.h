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

#include "rtsp-session-pool.h"
#include "rtsp-media-mapping.h"
#include "rtsp-client.h"

#ifndef __GST_RTSP_SERVER_H__
#define __GST_RTSP_SERVER_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_SERVER              (gst_rtsp_server_get_type ())
#define GST_IS_RTSP_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_SERVER))
#define GST_IS_RTSP_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_SERVER))
#define GST_RTSP_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_SERVER, GstRTSPServerClass))
#define GST_RTSP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_SERVER, GstRTSPServer))
#define GST_RTSP_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_SERVER, GstRTSPServerClass))
#define GST_RTSP_SERVER_CAST(obj)         ((GstRTSPServer*)(obj))
#define GST_RTSP_SERVER_CLASS_CAST(klass) ((GstRTSPServerClass*)(klass))

typedef struct _GstRTSPServer GstRTSPServer;
typedef struct _GstRTSPServerClass GstRTSPServerClass;

struct _GstRTSPServer {
  GObject       parent;

  /* server information */
  gint    port;
  gint    backlog;
  gchar  *host;
  struct  sockaddr_in server_sin;

  /* socket and channels */
  GstPollFD    server_sock;
  GIOChannel  *io_channel;
  GSource     *io_watch;

  /* sessions on this server */
  GstRTSPSessionPool  *session_pool;

  /* media mapper for this server */
  GstRTSPMediaMapping *media_mapping;
};

/**
 * GstRTSPServerClass:
 *
 * @accept_client: Create, configure, accept and return a new GstRTSPClient 
 *   object that handles the new connection on @channel.
 *
 * The RTSP server class structure
 */
struct _GstRTSPServerClass {
  GObjectClass  parent_class;

  GstRTSPClient * (*accept_client) (GstRTSPServer *server, GIOChannel *channel);
};

GType                 gst_rtsp_server_get_type             (void);

GstRTSPServer *       gst_rtsp_server_new                  (void);

void                  gst_rtsp_server_set_port             (GstRTSPServer *server, gint port);
gint                  gst_rtsp_server_get_port             (GstRTSPServer *server);

void                  gst_rtsp_server_set_backlog          (GstRTSPServer *server, gint backlog);
gint                  gst_rtsp_server_get_backlog          (GstRTSPServer *server);

void                  gst_rtsp_server_set_session_pool     (GstRTSPServer *server, GstRTSPSessionPool *pool);
GstRTSPSessionPool *  gst_rtsp_server_get_session_pool     (GstRTSPServer *server);

void                  gst_rtsp_server_set_media_mapping    (GstRTSPServer *server, GstRTSPMediaMapping *mapping);
GstRTSPMediaMapping * gst_rtsp_server_get_media_mapping    (GstRTSPServer *server);

gboolean              gst_rtsp_server_io_func              (GIOChannel *channel, GIOCondition condition,
                                                            GstRTSPServer *server);

GIOChannel *          gst_rtsp_server_get_io_channel       (GstRTSPServer *server);
GSource *             gst_rtsp_server_create_watch         (GstRTSPServer *server);
guint                 gst_rtsp_server_attach               (GstRTSPServer *server, 
                                                            GMainContext *context);

G_END_DECLS

#endif /* __GST_RTSP_SERVER_H__ */
