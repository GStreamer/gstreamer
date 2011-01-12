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

#include <gst/gst.h>
#include <gst/rtsp/gstrtspconnection.h>

#ifndef __GST_RTSP_CLIENT_H__
#define __GST_RTSP_CLIENT_H__

G_BEGIN_DECLS

typedef struct _GstRTSPClient GstRTSPClient;
typedef struct _GstRTSPClientClass GstRTSPClientClass;
typedef struct _GstRTSPClientState GstRTSPClientState;

#include "rtsp-server.h"
#include "rtsp-media.h"
#include "rtsp-media-mapping.h"
#include "rtsp-session-pool.h"
#include "rtsp-auth.h"

#define GST_TYPE_RTSP_CLIENT              (gst_rtsp_client_get_type ())
#define GST_IS_RTSP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_CLIENT))
#define GST_IS_RTSP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_CLIENT))
#define GST_RTSP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_CLIENT, GstRTSPClientClass))
#define GST_RTSP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_CLIENT, GstRTSPClient))
#define GST_RTSP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_CLIENT, GstRTSPClientClass))
#define GST_RTSP_CLIENT_CAST(obj)         ((GstRTSPClient*)(obj))
#define GST_RTSP_CLIENT_CLASS_CAST(klass) ((GstRTSPClientClass*)(klass))

/**
 * GstRTSPClientState:
 * @request: the complete request
 * @uri: the complete url parsed from @request
 * @method: the parsed method of @uri
 * @session: the session, can be NULL
 * @sessmedia: the session media for the url can be NULL
 * @factory: the media factory for the url, can be NULL.
 * @media: the session media for the url can be NULL
 * @response: the response
 *
 * Information passed around containing the client state of a request.
 */
struct _GstRTSPClientState{
  GstRTSPMessage      *request;
  GstRTSPUrl          *uri;
  GstRTSPMethod        method;
  GstRTSPSession      *session;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMediaFactory *factory;
  GstRTSPMedia        *media;
  GstRTSPMessage      *response;
};

/**
 * GstRTSPClient:
 *
 * @connection: the connection object handling the client request.
 * @watch: watch for the connection
 * @watchid: id of the watch
 * @ip: ip address used by the client to connect to us
 * @session_pool: handle to the session pool used by the client.
 * @media_mapping: handle to the media mapping used by the client.
 * @uri: cached uri
 * @media: cached media
 * @streams: a list of streams using @connection.
 * @sessions: a list of sessions managed by @connection.
 *
 * The client structure.
 */
struct _GstRTSPClient {
  GObject       parent;

  GstRTSPConnection *connection;
  GstRTSPWatch      *watch;
  guint              watchid;
  gchar             *server_ip;
  gboolean           is_ipv6;

  GstRTSPServer        *server;
  GstRTSPSessionPool   *session_pool;
  GstRTSPMediaMapping  *media_mapping;
  GstRTSPAuth          *auth;

  GstRTSPUrl     *uri;
  GstRTSPMedia   *media;

  GList *streams;
  GList *sessions;
};

struct _GstRTSPClientClass {
  GObjectClass  parent_class;

  /* signals */
  void     (*closed)        (GstRTSPClient *client);
};

GType                 gst_rtsp_client_get_type          (void);

GstRTSPClient *       gst_rtsp_client_new               (void);

void                  gst_rtsp_client_set_server        (GstRTSPClient * client, GstRTSPServer * server);
GstRTSPServer *       gst_rtsp_client_get_server        (GstRTSPClient * client);

void                  gst_rtsp_client_set_session_pool  (GstRTSPClient *client,
                                                         GstRTSPSessionPool *pool);
GstRTSPSessionPool *  gst_rtsp_client_get_session_pool  (GstRTSPClient *client);

void                  gst_rtsp_client_set_media_mapping (GstRTSPClient *client,
                                                         GstRTSPMediaMapping *mapping);
GstRTSPMediaMapping * gst_rtsp_client_get_media_mapping (GstRTSPClient *client);

void                  gst_rtsp_client_set_auth          (GstRTSPClient *client, GstRTSPAuth *auth);
GstRTSPAuth *         gst_rtsp_client_get_auth          (GstRTSPClient *client);


gboolean              gst_rtsp_client_accept            (GstRTSPClient *client,
                                                         GIOChannel *channel);

G_END_DECLS

#endif /* __GST_RTSP_CLIENT_H__ */
