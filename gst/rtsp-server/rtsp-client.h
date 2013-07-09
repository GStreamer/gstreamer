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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>
#include <gst/rtsp/gstrtspconnection.h>

#ifndef __GST_RTSP_CLIENT_H__
#define __GST_RTSP_CLIENT_H__

G_BEGIN_DECLS

typedef struct _GstRTSPClient GstRTSPClient;
typedef struct _GstRTSPClientClass GstRTSPClientClass;
typedef struct _GstRTSPClientState GstRTSPClientState;
typedef struct _GstRTSPClientPrivate GstRTSPClientPrivate;

#include "rtsp-media.h"
#include "rtsp-mount-points.h"
#include "rtsp-session-pool.h"
#include "rtsp-session-media.h"
#include "rtsp-auth.h"
#include "rtsp-token.h"
#include "rtsp-sdp.h"

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
 * @client: the client
 * @request: the complete request
 * @uri: the complete url parsed from @request
 * @method: the parsed method of @uri
 * @auth: the current auth object or NULL
 * @token: authorisation token
 * @session: the session, can be NULL
 * @sessmedia: the session media for the url can be NULL
 * @factory: the media factory for the url, can be NULL.
 * @media: the media for the url can be NULL
 * @stream: the stream for the url can be NULL
 * @response: the response
 *
 * Information passed around containing the client state of a request.
 */
struct _GstRTSPClientState {
  GstRTSPClient       *client;
  GstRTSPMessage      *request;
  GstRTSPUrl          *uri;
  GstRTSPMethod        method;
  GstRTSPAuth         *auth;
  GstRTSPToken        *token;
  GstRTSPSession      *session;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMediaFactory *factory;
  GstRTSPMedia        *media;
  GstRTSPStream       *stream;
  GstRTSPMessage      *response;
};

GstRTSPClientState * gst_rtsp_client_state_get_current   (void);

/**
 * GstRTSPClientSendFunc:
 * @client: a #GstRTSPClient
 * @message: a #GstRTSPMessage
 * @close: close the connection
 * @user_data: user data when registering the callback
 *
 * This callback is called when @client wants to send @message. When @close is
 * %TRUE, the connection should be closed when the message has been sent.
 *
 * Returns: %TRUE on success.
 */
typedef gboolean (*GstRTSPClientSendFunc)      (GstRTSPClient *client,
                                                GstRTSPMessage *message,
                                                gboolean close,
                                                gpointer user_data);

/**
 * GstRTSPClient:
 *
 * The client structure.
 */
struct _GstRTSPClient {
  GObject       parent;

  GstRTSPClientPrivate *priv;
};

/**
 * GstRTSPClientClass:
 * @params_set: set parameters. This function should also initialize the
 * RTSP response(state->response) via a call to gst_rtsp_message_init_response()
 * @params_get: get parameters. This function should also initialize the
 * RTSP response(state->response) via a call to gst_rtsp_message_init_response()
 *
 * The client class structure.
 */
struct _GstRTSPClientClass {
  GObjectClass  parent_class;

  GstSDPMessage * (*create_sdp) (GstRTSPClient *client, GstRTSPMedia *media);
  gboolean        (*configure_client_transport) (GstRTSPClient * client,
                                                 GstRTSPClientState * state,
                                                 GstRTSPTransport * ct);
  GstRTSPResult   (*params_set) (GstRTSPClient *client, GstRTSPClientState *state);
  GstRTSPResult   (*params_get) (GstRTSPClient *client, GstRTSPClientState *state);

  /* signals */
  void     (*closed)                  (GstRTSPClient *client);
  void     (*new_session)             (GstRTSPClient *client, GstRTSPSession *session);
  void     (*options_request)         (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*describe_request)        (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*setup_request)           (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*play_request)            (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*pause_request)           (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*teardown_request)        (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*set_parameter_request)   (GstRTSPClient *client, GstRTSPClientState *state);
  void     (*get_parameter_request)   (GstRTSPClient *client, GstRTSPClientState *state);
};

GType                 gst_rtsp_client_get_type          (void);

GstRTSPClient *       gst_rtsp_client_new               (void);

void                  gst_rtsp_client_set_session_pool  (GstRTSPClient *client,
                                                         GstRTSPSessionPool *pool);
GstRTSPSessionPool *  gst_rtsp_client_get_session_pool  (GstRTSPClient *client);

void                  gst_rtsp_client_set_mount_points  (GstRTSPClient *client,
                                                         GstRTSPMountPoints *mounts);
GstRTSPMountPoints *  gst_rtsp_client_get_mount_points  (GstRTSPClient *client);

void                  gst_rtsp_client_set_use_client_settings (GstRTSPClient * client,
                                                               gboolean use_client_settings);
gboolean              gst_rtsp_client_get_use_client_settings (GstRTSPClient * client);

void                  gst_rtsp_client_set_auth          (GstRTSPClient *client, GstRTSPAuth *auth);
GstRTSPAuth *         gst_rtsp_client_get_auth          (GstRTSPClient *client);

gboolean              gst_rtsp_client_set_connection    (GstRTSPClient *client, GstRTSPConnection *conn);
GstRTSPConnection *   gst_rtsp_client_get_connection    (GstRTSPClient *client);

void                  gst_rtsp_client_set_send_func     (GstRTSPClient *client,
                                                         GstRTSPClientSendFunc func,
                                                         gpointer user_data,
                                                         GDestroyNotify notify);
GstRTSPResult         gst_rtsp_client_handle_message    (GstRTSPClient *client,
                                                         GstRTSPMessage *message);

GstRTSPResult         gst_rtsp_client_send_request      (GstRTSPClient * client,
                                                         GstRTSPSession *session,
                                                         GstRTSPMessage *request);
guint                 gst_rtsp_client_attach            (GstRTSPClient *client,
                                                         GMainContext *context);

/**
 * GstRTSPClientSessionFilterFunc:
 * @client: a #GstRTSPClient object
 * @sess: a #GstRTSPSession in @client
 * @user_data: user data that has been given to gst_rtsp_client_session_filter()
 *
 * This function will be called by the gst_rtsp_client_session_filter(). An
 * implementation should return a value of #GstRTSPFilterResult.
 *
 * When this function returns #GST_RTSP_FILTER_REMOVE, @sess will be removed
 * from @client.
 *
 * A return value of #GST_RTSP_FILTER_KEEP will leave @sess untouched in
 * @client.
 *
 * A value of GST_RTSP_FILTER_REF will add @sess to the result #GList of
 * gst_rtsp_client_session_filter().
 *
 * Returns: a #GstRTSPFilterResult.
 */
typedef GstRTSPFilterResult (*GstRTSPClientSessionFilterFunc)  (GstRTSPClient *client,
                                                                GstRTSPSession *sess,
                                                                gpointer user_data);

GList *                gst_rtsp_client_session_filter    (GstRTSPClient *client,
                                                          GstRTSPClientSessionFilterFunc func,
                                                          gpointer user_data);



G_END_DECLS

#endif /* __GST_RTSP_CLIENT_H__ */
