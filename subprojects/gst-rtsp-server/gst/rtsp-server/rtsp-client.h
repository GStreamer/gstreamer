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
typedef struct _GstRTSPClientPrivate GstRTSPClientPrivate;

#include "rtsp-server-prelude.h"
#include "rtsp-context.h"
#include "rtsp-mount-points.h"
#include "rtsp-sdp.h"
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
 * GstRTSPClientSendMessagesFunc:
 * @client: a #GstRTSPClient
 * @messages: #GstRTSPMessage
 * @n_messages: number of messages
 * @close: close the connection
 * @user_data: user data when registering the callback
 *
 * This callback is called when @client wants to send @messages. When @close is
 * %TRUE, the connection should be closed when the message has been sent.
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.16
 */
typedef gboolean (*GstRTSPClientSendMessagesFunc)      (GstRTSPClient *client,
                                                        GstRTSPMessage *messages,
                                                        guint n_messages,
                                                        gboolean close,
                                                        gpointer user_data);

/**
 * GstRTSPClient:
 *
 * The client object represents the connection and its state with a client.
 */
struct _GstRTSPClient {
  GObject       parent;

  /*< private >*/
  GstRTSPClientPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstRTSPClientClass:
 * @create_sdp: called when the SDP needs to be created for media.
 * @configure_client_media: called when the stream in media needs to be configured.
 *    The default implementation will configure the blocksize on the payloader when
 *    spcified in the request headers.
 * @configure_client_transport: called when the client transport needs to be
 *    configured.
 * @params_set: set parameters. This function should also initialize the
 *    RTSP response(ctx->response) via a call to gst_rtsp_message_init_response()
 * @params_get: get parameters. This function should also initialize the
 *    RTSP response(ctx->response) via a call to gst_rtsp_message_init_response()
 * @make_path_from_uri: called to create path from uri.
 * @adjust_play_mode: called to give the application the possibility to adjust
 *    the range, seek flags, rate and rate-control. Since 1.18
 * @adjust_play_response: called to give the implementation the possibility to
 *    adjust the response to a play request, for example if extra headers were
 *    parsed when #GstRTSPClientClass.adjust_play_mode was called. Since 1.18
 * @tunnel_http_response: called when a response to the GET request is about to
 *   be sent for a tunneled connection. The response can be modified. Since: 1.4
 *
 * The client class structure.
 */
struct _GstRTSPClientClass {
  GObjectClass  parent_class;

  GstSDPMessage * (*create_sdp) (GstRTSPClient *client, GstRTSPMedia *media);
  gboolean        (*configure_client_media)     (GstRTSPClient * client,
                                                 GstRTSPMedia * media, GstRTSPStream * stream,
                                                 GstRTSPContext * ctx);
  gboolean        (*configure_client_transport) (GstRTSPClient * client,
                                                 GstRTSPContext * ctx,
                                                 GstRTSPTransport * ct);
  GstRTSPResult   (*params_set) (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPResult   (*params_get) (GstRTSPClient *client, GstRTSPContext *ctx);
  gchar *         (*make_path_from_uri) (GstRTSPClient *client, const GstRTSPUrl *uri);
  GstRTSPStatusCode (*adjust_play_mode) (GstRTSPClient * client,
                                         GstRTSPContext * context,
                                         GstRTSPTimeRange ** range,
                                         GstSeekFlags * flags,
                                         gdouble * rate,
                                         GstClockTime * trickmode_interval,
                                         gboolean * enable_rate_control);
  GstRTSPStatusCode (*adjust_play_response) (GstRTSPClient * client,
                                            GstRTSPContext * context);

  /* signals */
  void     (*closed)                  (GstRTSPClient *client);
  void     (*new_session)             (GstRTSPClient *client, GstRTSPSession *session);
  void     (*options_request)         (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*describe_request)        (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*setup_request)           (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*play_request)            (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*pause_request)           (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*teardown_request)        (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*set_parameter_request)   (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*get_parameter_request)   (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*handle_response)         (GstRTSPClient *client, GstRTSPContext *ctx);

  void     (*tunnel_http_response)    (GstRTSPClient * client, GstRTSPMessage * request,
                                       GstRTSPMessage * response);
  void     (*send_message)            (GstRTSPClient * client, GstRTSPContext *ctx,
                                       GstRTSPMessage * response);

  gboolean (*handle_sdp)              (GstRTSPClient *client, GstRTSPContext *ctx, GstRTSPMedia *media, GstSDPMessage *sdp);

  void     (*announce_request)        (GstRTSPClient *client, GstRTSPContext *ctx);
  void     (*record_request)          (GstRTSPClient *client, GstRTSPContext *ctx);
  gchar*   (*check_requirements)      (GstRTSPClient *client, GstRTSPContext *ctx, gchar ** arr);

  GstRTSPStatusCode (*pre_options_request)       (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_describe_request)      (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_setup_request)         (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_play_request)          (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_pause_request)         (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_teardown_request)      (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_set_parameter_request) (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_get_parameter_request) (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_announce_request)      (GstRTSPClient *client, GstRTSPContext *ctx);
  GstRTSPStatusCode (*pre_record_request)        (GstRTSPClient *client, GstRTSPContext *ctx);

  /**
   * GstRTSPClientClass::adjust_error_code:
   * @client: a #GstRTSPClient
   * @ctx: a #GstRTSPContext
   * @code: a #GstRTSPStatusCode
   *
   * Called before sending error response to give the application the
   * possibility to adjust the error code.
   *
   * Returns: a #GstRTSPStatusCode, containing the adjusted error code.
   *
   * Since: 1.22
   */
  GstRTSPStatusCode (*adjust_error_code)         (GstRTSPClient *client, GstRTSPContext *ctx, GstRTSPStatusCode code);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE-19];
};

GST_RTSP_SERVER_API
GType                 gst_rtsp_client_get_type          (void);

GST_RTSP_SERVER_API
GstRTSPClient *       gst_rtsp_client_new               (void) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_session_pool  (GstRTSPClient *client,
                                                         GstRTSPSessionPool *pool);

GST_RTSP_SERVER_API
GstRTSPSessionPool *  gst_rtsp_client_get_session_pool  (GstRTSPClient *client) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_mount_points  (GstRTSPClient *client,
                                                         GstRTSPMountPoints *mounts);

GST_RTSP_SERVER_API
GstRTSPMountPoints *  gst_rtsp_client_get_mount_points  (GstRTSPClient *client) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_content_length_limit (GstRTSPClient *client, guint limit);

GST_RTSP_SERVER_API
guint                 gst_rtsp_client_get_content_length_limit (GstRTSPClient *client);

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_auth          (GstRTSPClient *client, GstRTSPAuth *auth);

GST_RTSP_SERVER_API
GstRTSPAuth *         gst_rtsp_client_get_auth          (GstRTSPClient *client) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_thread_pool   (GstRTSPClient *client, GstRTSPThreadPool *pool);

GST_RTSP_SERVER_API
GstRTSPThreadPool *   gst_rtsp_client_get_thread_pool   (GstRTSPClient *client) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
gboolean              gst_rtsp_client_set_connection    (GstRTSPClient *client, GstRTSPConnection *conn);

GST_RTSP_SERVER_API
GstRTSPConnection *   gst_rtsp_client_get_connection    (GstRTSPClient *client);

GST_RTSP_SERVER_API
guint                 gst_rtsp_client_attach            (GstRTSPClient *client,
                                                         GMainContext *context);

GST_RTSP_SERVER_API
void                  gst_rtsp_client_close             (GstRTSPClient * client);

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_send_func     (GstRTSPClient *client,
                                                         GstRTSPClientSendFunc func,
                                                         gpointer user_data,
                                                         GDestroyNotify notify);

GST_RTSP_SERVER_API
void                  gst_rtsp_client_set_send_messages_func (GstRTSPClient *client,
                                                              GstRTSPClientSendMessagesFunc func,
                                                              gpointer user_data,
                                                              GDestroyNotify notify);

GST_RTSP_SERVER_API
GstRTSPResult         gst_rtsp_client_handle_message    (GstRTSPClient *client,
                                                         GstRTSPMessage *message);

GST_RTSP_SERVER_API
GstRTSPResult         gst_rtsp_client_send_message      (GstRTSPClient * client,
                                                         GstRTSPSession *session,
                                                         GstRTSPMessage *message);
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
 * A value of #GST_RTSP_FILTER_REF will add @sess to the result #GList of
 * gst_rtsp_client_session_filter().
 *
 * Returns: a #GstRTSPFilterResult.
 */
typedef GstRTSPFilterResult (*GstRTSPClientSessionFilterFunc)  (GstRTSPClient *client,
                                                                GstRTSPSession *sess,
                                                                gpointer user_data);

GST_RTSP_SERVER_API
GList *                gst_rtsp_client_session_filter    (GstRTSPClient *client,
                                                          GstRTSPClientSessionFilterFunc func,
                                                          gpointer user_data) G_GNUC_WARN_UNUSED_RESULT;

GST_RTSP_SERVER_API
GstRTSPStreamTransport * gst_rtsp_client_get_stream_transport (GstRTSPClient *client,
                                                               guint8 channel);


#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPClient, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_CLIENT_H__ */
