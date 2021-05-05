/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2015 Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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
/**
 * SECTION:rtsp-client
 * @short_description: A client connection state
 * @see_also: #GstRTSPServer, #GstRTSPThreadPool
 *
 * The client object handles the connection with a client for as long as a TCP
 * connection is open.
 *
 * A #GstRTSPClient is created by #GstRTSPServer when a new connection is
 * accepted and it inherits the #GstRTSPMountPoints, #GstRTSPSessionPool,
 * #GstRTSPAuth and #GstRTSPThreadPool from the server.
 *
 * The client connection should be configured with the #GstRTSPConnection using
 * gst_rtsp_client_set_connection() before it can be attached to a #GMainContext
 * using gst_rtsp_client_attach(). From then on the client will handle requests
 * on the connection.
 *
 * Use gst_rtsp_client_session_filter() to iterate or modify all the
 * #GstRTSPSession objects managed by the client object.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include <gst/sdp/gstmikey.h>
#include <gst/rtsp/gstrtsp-enumtypes.h>

#include "rtsp-client.h"
#include "rtsp-sdp.h"
#include "rtsp-params.h"
#include "rtsp-server-internal.h"

typedef enum
{
  TUNNEL_STATE_UNKNOWN,
  TUNNEL_STATE_GET,
  TUNNEL_STATE_POST
} GstRTSPTunnelState;

/* locking order:
 * send_lock, lock, tunnels_lock
 */

struct _GstRTSPClientPrivate
{
  GMutex lock;                  /* protects everything else */
  GMutex send_lock;
  GMutex watch_lock;
  GstRTSPConnection *connection;
  GstRTSPWatch *watch;
  GMainContext *watch_context;
  gchar *server_ip;
  gboolean is_ipv6;

  /* protected by send_lock */
  GstRTSPClientSendFunc send_func;
  gpointer send_data;
  GDestroyNotify send_notify;
  GstRTSPClientSendMessagesFunc send_messages_func;
  gpointer send_messages_data;
  GDestroyNotify send_messages_notify;
  GArray *data_seqs;

  GstRTSPSessionPool *session_pool;
  gulong session_removed_id;
  GstRTSPMountPoints *mount_points;
  GstRTSPAuth *auth;
  GstRTSPThreadPool *thread_pool;

  /* used to cache the media in the last requested DESCRIBE so that
   * we can pick it up in the next SETUP immediately */
  gchar *path;
  GstRTSPMedia *media;

  GHashTable *transports;
  GList *sessions;
  guint sessions_cookie;

  gboolean drop_backlog;
  gint post_session_timeout;

  guint content_length_limit;

  gboolean had_session;
  GSource *rtsp_ctrl_timeout;
  guint rtsp_ctrl_timeout_cnt;

  /* The version currently being used */
  GstRTSPVersion version;

  GHashTable *pipelined_requests;       /* pipelined_request_id -> session_id */
  GstRTSPTunnelState tstate;
};

typedef struct
{
  guint8 channel;
  guint seq;
} DataSeq;

static GMutex tunnels_lock;
static GHashTable *tunnels;     /* protected by tunnels_lock */

#define WATCH_BACKLOG_SIZE              100

#define DEFAULT_SESSION_POOL            NULL
#define DEFAULT_MOUNT_POINTS            NULL
#define DEFAULT_DROP_BACKLOG            TRUE
#define DEFAULT_POST_SESSION_TIMEOUT    -1

#define RTSP_CTRL_CB_INTERVAL           1
#define RTSP_CTRL_TIMEOUT_VALUE         60

enum
{
  PROP_0,
  PROP_SESSION_POOL,
  PROP_MOUNT_POINTS,
  PROP_DROP_BACKLOG,
  PROP_POST_SESSION_TIMEOUT,
  PROP_LAST
};

enum
{
  SIGNAL_CLOSED,
  SIGNAL_NEW_SESSION,
  SIGNAL_PRE_OPTIONS_REQUEST,
  SIGNAL_OPTIONS_REQUEST,
  SIGNAL_PRE_DESCRIBE_REQUEST,
  SIGNAL_DESCRIBE_REQUEST,
  SIGNAL_PRE_SETUP_REQUEST,
  SIGNAL_SETUP_REQUEST,
  SIGNAL_PRE_PLAY_REQUEST,
  SIGNAL_PLAY_REQUEST,
  SIGNAL_PRE_PAUSE_REQUEST,
  SIGNAL_PAUSE_REQUEST,
  SIGNAL_PRE_TEARDOWN_REQUEST,
  SIGNAL_TEARDOWN_REQUEST,
  SIGNAL_PRE_SET_PARAMETER_REQUEST,
  SIGNAL_SET_PARAMETER_REQUEST,
  SIGNAL_PRE_GET_PARAMETER_REQUEST,
  SIGNAL_GET_PARAMETER_REQUEST,
  SIGNAL_HANDLE_RESPONSE,
  SIGNAL_SEND_MESSAGE,
  SIGNAL_PRE_ANNOUNCE_REQUEST,
  SIGNAL_ANNOUNCE_REQUEST,
  SIGNAL_PRE_RECORD_REQUEST,
  SIGNAL_RECORD_REQUEST,
  SIGNAL_CHECK_REQUIREMENTS,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_client_debug);
#define GST_CAT_DEFAULT rtsp_client_debug

static guint gst_rtsp_client_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_finalize (GObject * obj);

static void rtsp_ctrl_timeout_remove (GstRTSPClient * client);

static GstSDPMessage *create_sdp (GstRTSPClient * client, GstRTSPMedia * media);
static gboolean handle_sdp (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPMedia * media, GstSDPMessage * sdp);
static gboolean default_configure_client_media (GstRTSPClient * client,
    GstRTSPMedia * media, GstRTSPStream * stream, GstRTSPContext * ctx);
static gboolean default_configure_client_transport (GstRTSPClient * client,
    GstRTSPContext * ctx, GstRTSPTransport * ct);
static GstRTSPResult default_params_set (GstRTSPClient * client,
    GstRTSPContext * ctx);
static GstRTSPResult default_params_get (GstRTSPClient * client,
    GstRTSPContext * ctx);
static gchar *default_make_path_from_uri (GstRTSPClient * client,
    const GstRTSPUrl * uri);
static void client_session_removed (GstRTSPSessionPool * pool,
    GstRTSPSession * session, GstRTSPClient * client);
static GstRTSPStatusCode default_pre_signal_handler (GstRTSPClient * client,
    GstRTSPContext * ctx);
static gboolean pre_signal_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer data);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPClient, gst_rtsp_client, G_TYPE_OBJECT);

static void
gst_rtsp_client_class_init (GstRTSPClientClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_client_get_property;
  gobject_class->set_property = gst_rtsp_client_set_property;
  gobject_class->finalize = gst_rtsp_client_finalize;

  klass->create_sdp = create_sdp;
  klass->handle_sdp = handle_sdp;
  klass->configure_client_media = default_configure_client_media;
  klass->configure_client_transport = default_configure_client_transport;
  klass->params_set = default_params_set;
  klass->params_get = default_params_get;
  klass->make_path_from_uri = default_make_path_from_uri;

  klass->pre_options_request = default_pre_signal_handler;
  klass->pre_describe_request = default_pre_signal_handler;
  klass->pre_setup_request = default_pre_signal_handler;
  klass->pre_play_request = default_pre_signal_handler;
  klass->pre_pause_request = default_pre_signal_handler;
  klass->pre_teardown_request = default_pre_signal_handler;
  klass->pre_set_parameter_request = default_pre_signal_handler;
  klass->pre_get_parameter_request = default_pre_signal_handler;
  klass->pre_announce_request = default_pre_signal_handler;
  klass->pre_record_request = default_pre_signal_handler;

  g_object_class_install_property (gobject_class, PROP_SESSION_POOL,
      g_param_spec_object ("session-pool", "Session Pool",
          "The session pool to use for client session",
          GST_TYPE_RTSP_SESSION_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MOUNT_POINTS,
      g_param_spec_object ("mount-points", "Mount Points",
          "The mount points to use for client session",
          GST_TYPE_RTSP_MOUNT_POINTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DROP_BACKLOG,
      g_param_spec_boolean ("drop-backlog", "Drop Backlog",
          "Drop data when the backlog queue is full",
          DEFAULT_DROP_BACKLOG, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClient::post-session-timeout:
   *
   * An extra tcp timeout ( > 0) after session timeout, in seconds.
   * The tcp connection will be kept alive until this timeout happens to give
   * the client a possibility to reuse the connection.
   * 0 means that the connection will be closed immediately after the session
   * timeout.
   *
   * Default value is -1 seconds, meaning that we let the system close
   * the connection.
   *
   * Since: 1.18
   */
  g_object_class_install_property (gobject_class, PROP_POST_SESSION_TIMEOUT,
      g_param_spec_int ("post-session-timeout", "Post Session Timeout",
          "An extra TCP connection timeout after session timeout", G_MININT,
          G_MAXINT, DEFAULT_POST_SESSION_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_client_signals[SIGNAL_CLOSED] =
      g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPClientClass, closed), NULL, NULL, NULL,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_client_signals[SIGNAL_NEW_SESSION] =
      g_signal_new ("new-session", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPClientClass, new_session), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_SESSION);

  /**
   * GstRTSPClient::pre-options-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_OPTIONS_REQUEST] =
      g_signal_new ("pre-options-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_options_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::options-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_OPTIONS_REQUEST] =
      g_signal_new ("options-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, options_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-describe-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_DESCRIBE_REQUEST] =
      g_signal_new ("pre-describe-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_describe_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::describe-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_DESCRIBE_REQUEST] =
      g_signal_new ("describe-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, describe_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-setup-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_SETUP_REQUEST] =
      g_signal_new ("pre-setup-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_setup_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::setup-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_SETUP_REQUEST] =
      g_signal_new ("setup-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, setup_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-play-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_PLAY_REQUEST] =
      g_signal_new ("pre-play-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_play_request), pre_signal_accumulator, NULL,
      NULL, GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::play-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_PLAY_REQUEST] =
      g_signal_new ("play-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, play_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-pause-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_PAUSE_REQUEST] =
      g_signal_new ("pre-pause-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_pause_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pause-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_PAUSE_REQUEST] =
      g_signal_new ("pause-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, pause_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-teardown-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_TEARDOWN_REQUEST] =
      g_signal_new ("pre-teardown-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_teardown_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::teardown-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_TEARDOWN_REQUEST] =
      g_signal_new ("teardown-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, teardown_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-set-parameter-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_SET_PARAMETER_REQUEST] =
      g_signal_new ("pre-set-parameter-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_set_parameter_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::set-parameter-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_SET_PARAMETER_REQUEST] =
      g_signal_new ("set-parameter-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          set_parameter_request), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-get-parameter-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_GET_PARAMETER_REQUEST] =
      g_signal_new ("pre-get-parameter-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_get_parameter_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::get-parameter-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_GET_PARAMETER_REQUEST] =
      g_signal_new ("get-parameter-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          get_parameter_request), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::handle-response:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_HANDLE_RESPONSE] =
      g_signal_new ("handle-response", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          handle_response), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::send-message:
   * @client: The RTSP client
   * @session: (type GstRtspServer.RTSPSession): The session
   * @message: (type GstRtsp.RTSPMessage): The message
   */
  gst_rtsp_client_signals[SIGNAL_SEND_MESSAGE] =
      g_signal_new ("send-message", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          send_message), NULL, NULL, NULL,
      G_TYPE_NONE, 2, GST_TYPE_RTSP_CONTEXT, G_TYPE_POINTER);

  /**
   * GstRTSPClient::pre-announce-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_ANNOUNCE_REQUEST] =
      g_signal_new ("pre-announce-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_announce_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::announce-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_ANNOUNCE_REQUEST] =
      g_signal_new ("announce-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, announce_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::pre-record-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   *
   * Returns: a #GstRTSPStatusCode, GST_RTSP_STS_OK in case of success,
   *          otherwise an appropriate return code
   *
   * Since: 1.12
   */
  gst_rtsp_client_signals[SIGNAL_PRE_RECORD_REQUEST] =
      g_signal_new ("pre-record-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          pre_record_request), pre_signal_accumulator, NULL, NULL,
      GST_TYPE_RTSP_STATUS_CODE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::record-request:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   */
  gst_rtsp_client_signals[SIGNAL_RECORD_REQUEST] =
      g_signal_new ("record-request", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass, record_request),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CONTEXT);

  /**
   * GstRTSPClient::check-requirements:
   * @client: a #GstRTSPClient
   * @ctx: (type GstRtspServer.RTSPContext): a #GstRTSPContext
   * @arr: a NULL-terminated array of strings
   *
   * Returns: a newly allocated string with comma-separated list of
   *          unsupported options. An empty string must be returned if
   *          all options are supported.
   *
   * Since: 1.6
   */
  gst_rtsp_client_signals[SIGNAL_CHECK_REQUIREMENTS] =
      g_signal_new ("check-requirements", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPClientClass,
          check_requirements), NULL, NULL, NULL,
      G_TYPE_STRING, 2, GST_TYPE_RTSP_CONTEXT, G_TYPE_STRV);

  tunnels =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  g_mutex_init (&tunnels_lock);

  GST_DEBUG_CATEGORY_INIT (rtsp_client_debug, "rtspclient", 0, "GstRTSPClient");
}

static void
gst_rtsp_client_init (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = gst_rtsp_client_get_instance_private (client);

  client->priv = priv;

  g_mutex_init (&priv->lock);
  g_mutex_init (&priv->send_lock);
  g_mutex_init (&priv->watch_lock);
  priv->data_seqs = g_array_new (FALSE, FALSE, sizeof (DataSeq));
  priv->drop_backlog = DEFAULT_DROP_BACKLOG;
  priv->post_session_timeout = DEFAULT_POST_SESSION_TIMEOUT;
  priv->transports =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      g_object_unref);
  priv->pipelined_requests = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, g_free);
  priv->tstate = TUNNEL_STATE_UNKNOWN;
  priv->content_length_limit = G_MAXUINT;
}

static GstRTSPFilterResult
filter_session_media (GstRTSPSession * sess, GstRTSPSessionMedia * sessmedia,
    gpointer user_data)
{
  gboolean *closed = user_data;
  GstRTSPMedia *media;
  guint i, n_streams;
  gboolean is_all_udp = TRUE;

  media = gst_rtsp_session_media_get_media (sessmedia);
  n_streams = gst_rtsp_media_n_streams (media);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStreamTransport *transport =
        gst_rtsp_session_media_get_transport (sessmedia, i);
    const GstRTSPTransport *rtsp_transport;

    if (!transport)
      continue;

    rtsp_transport = gst_rtsp_stream_transport_get_transport (transport);
    if (rtsp_transport
        && rtsp_transport->lower_transport != GST_RTSP_LOWER_TRANS_UDP
        && rtsp_transport->lower_transport != GST_RTSP_LOWER_TRANS_UDP_MCAST) {
      is_all_udp = FALSE;
      break;
    }
  }

  if (!is_all_udp || gst_rtsp_media_is_stop_on_disconnect (media)) {
    gst_rtsp_session_media_set_state (sessmedia, GST_STATE_NULL);
    return GST_RTSP_FILTER_REMOVE;
  } else {
    *closed = FALSE;
    return GST_RTSP_FILTER_KEEP;
  }
}

static void
client_watch_session (GstRTSPClient * client, GstRTSPSession * session)
{
  GstRTSPClientPrivate *priv = client->priv;

  g_mutex_lock (&priv->lock);
  /* check if we already know about this session */
  if (g_list_find (priv->sessions, session) == NULL) {
    GST_INFO ("watching session %p", session);

    priv->sessions = g_list_prepend (priv->sessions, g_object_ref (session));
    priv->sessions_cookie++;

    /* connect removed session handler, it will be disconnected when the last
     * session gets removed  */
    if (priv->session_removed_id == 0)
      priv->session_removed_id = g_signal_connect_data (priv->session_pool,
          "session-removed", G_CALLBACK (client_session_removed),
          g_object_ref (client), (GClosureNotify) g_object_unref, 0);
  }
  g_mutex_unlock (&priv->lock);

  return;
}

/* should be called with lock */
static void
client_unwatch_session (GstRTSPClient * client, GstRTSPSession * session,
    GList * link)
{
  GstRTSPClientPrivate *priv = client->priv;

  GST_INFO ("client %p: unwatch session %p", client, session);

  if (link == NULL) {
    link = g_list_find (priv->sessions, session);
    if (link == NULL)
      return;
  }

  priv->sessions = g_list_delete_link (priv->sessions, link);
  priv->sessions_cookie++;

  /* if this was the last session, disconnect the handler.
   * This will also drop the extra client ref */
  if (!priv->sessions) {
    g_signal_handler_disconnect (priv->session_pool, priv->session_removed_id);
    priv->session_removed_id = 0;
  }

  if (!priv->drop_backlog) {
    /* unlink all media managed in this session */
    gst_rtsp_session_filter (session, filter_session_media, client);
  }

  /* remove the session */
  g_object_unref (session);
}

static GstRTSPFilterResult
cleanup_session (GstRTSPClient * client, GstRTSPSession * sess,
    gpointer user_data)
{
  gboolean *closed = user_data;
  GstRTSPClientPrivate *priv = client->priv;

  if (priv->drop_backlog) {
    /* unlink all media managed in this session. This needs to happen
     * without the client lock, so we really want to do it here. */
    gst_rtsp_session_filter (sess, filter_session_media, user_data);
  }

  if (*closed)
    return GST_RTSP_FILTER_REMOVE;
  else
    return GST_RTSP_FILTER_KEEP;
}

static void
clean_cached_media (GstRTSPClient * client, gboolean unprepare)
{
  GstRTSPClientPrivate *priv = client->priv;

  if (priv->path) {
    g_free (priv->path);
    priv->path = NULL;
  }
  if (priv->media) {
    if (unprepare)
      gst_rtsp_media_unprepare (priv->media);
    g_object_unref (priv->media);
    priv->media = NULL;
  }
}

/* A client is finalized when the connection is broken */
static void
gst_rtsp_client_finalize (GObject * obj)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (obj);
  GstRTSPClientPrivate *priv = client->priv;

  GST_INFO ("finalize client %p", client);

  /* the watch and related state should be cleared before finalize
   * as the watch actually holds a strong reference to the client */
  g_assert (priv->watch == NULL);
  g_assert (priv->rtsp_ctrl_timeout == NULL);

  if (priv->watch_context) {
    g_main_context_unref (priv->watch_context);
    priv->watch_context = NULL;
  }

  gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
  gst_rtsp_client_set_send_messages_func (client, NULL, NULL, NULL);

  /* all sessions should have been removed by now. We keep a ref to
   * the client object for the session removed handler. The ref is
   * dropped when the last session is removed from the list. */
  g_assert (priv->sessions == NULL);
  g_assert (priv->session_removed_id == 0);

  g_array_unref (priv->data_seqs);
  g_hash_table_unref (priv->transports);
  g_hash_table_unref (priv->pipelined_requests);

  if (priv->connection)
    gst_rtsp_connection_free (priv->connection);
  if (priv->session_pool) {
    g_object_unref (priv->session_pool);
  }
  if (priv->mount_points)
    g_object_unref (priv->mount_points);
  if (priv->auth)
    g_object_unref (priv->auth);
  if (priv->thread_pool)
    g_object_unref (priv->thread_pool);

  clean_cached_media (client, TRUE);

  g_free (priv->server_ip);
  g_mutex_clear (&priv->lock);
  g_mutex_clear (&priv->send_lock);
  g_mutex_clear (&priv->watch_lock);

  G_OBJECT_CLASS (gst_rtsp_client_parent_class)->finalize (obj);
}

static void
gst_rtsp_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);
  GstRTSPClientPrivate *priv = client->priv;

  switch (propid) {
    case PROP_SESSION_POOL:
      g_value_take_object (value, gst_rtsp_client_get_session_pool (client));
      break;
    case PROP_MOUNT_POINTS:
      g_value_take_object (value, gst_rtsp_client_get_mount_points (client));
      break;
    case PROP_DROP_BACKLOG:
      g_value_set_boolean (value, priv->drop_backlog);
      break;
    case PROP_POST_SESSION_TIMEOUT:
      g_value_set_int (value, priv->post_session_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (object);
  GstRTSPClientPrivate *priv = client->priv;

  switch (propid) {
    case PROP_SESSION_POOL:
      gst_rtsp_client_set_session_pool (client, g_value_get_object (value));
      break;
    case PROP_MOUNT_POINTS:
      gst_rtsp_client_set_mount_points (client, g_value_get_object (value));
      break;
    case PROP_DROP_BACKLOG:
      g_mutex_lock (&priv->lock);
      priv->drop_backlog = g_value_get_boolean (value);
      g_mutex_unlock (&priv->lock);
      break;
    case PROP_POST_SESSION_TIMEOUT:
      g_mutex_lock (&priv->lock);
      priv->post_session_timeout = g_value_get_int (value);
      g_mutex_unlock (&priv->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_client_new:
 *
 * Create a new #GstRTSPClient instance.
 *
 * Returns: (transfer full): a new #GstRTSPClient
 */
GstRTSPClient *
gst_rtsp_client_new (void)
{
  GstRTSPClient *result;

  result = g_object_new (GST_TYPE_RTSP_CLIENT, NULL);

  return result;
}

static void
send_message (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPMessage * message, gboolean close)
{
  GstRTSPClientPrivate *priv = client->priv;

  gst_rtsp_message_add_header (message, GST_RTSP_HDR_SERVER,
      "GStreamer RTSP server");

  /* remove any previous header */
  gst_rtsp_message_remove_header (message, GST_RTSP_HDR_SESSION, -1);

  /* add the new session header for new session ids */
  if (ctx->session) {
    gst_rtsp_message_take_header (message, GST_RTSP_HDR_SESSION,
        gst_rtsp_session_get_header (ctx->session));
  }

  if (gst_debug_category_get_threshold (rtsp_client_debug) >= GST_LEVEL_LOG) {
    gst_rtsp_message_dump (message);
  }

  if (close)
    gst_rtsp_message_add_header (message, GST_RTSP_HDR_CONNECTION, "close");

  if (ctx->request)
    message->type_data.response.version =
        ctx->request->type_data.request.version;

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_SEND_MESSAGE],
      0, ctx, message);

  g_mutex_lock (&priv->send_lock);
  if (priv->send_messages_func) {
    priv->send_messages_func (client, message, 1, close, priv->send_data);
  } else if (priv->send_func) {
    priv->send_func (client, message, close, priv->send_data);
  }
  g_mutex_unlock (&priv->send_lock);

  gst_rtsp_message_unset (message);
}

static void
send_generic_response (GstRTSPClient * client, GstRTSPStatusCode code,
    GstRTSPContext * ctx)
{
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  ctx->session = NULL;

  send_message (client, ctx, ctx->response, FALSE);
}

static void
send_option_not_supported_response (GstRTSPClient * client,
    GstRTSPContext * ctx, const gchar * unsupported_options)
{
  GstRTSPStatusCode code = GST_RTSP_STS_OPTION_NOT_SUPPORTED;

  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  if (unsupported_options != NULL) {
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_UNSUPPORTED,
        unsupported_options);
  }

  ctx->session = NULL;

  send_message (client, ctx, ctx->response, FALSE);
}

static gboolean
paths_are_equal (const gchar * path1, const gchar * path2, gint len2)
{
  if (path1 == NULL || path2 == NULL)
    return FALSE;

  if (strlen (path1) != len2)
    return FALSE;

  if (strncmp (path1, path2, len2))
    return FALSE;

  return TRUE;
}

/* this function is called to initially find the media for the DESCRIBE request
 * but is cached for when the same client (without breaking the connection) is
 * doing a setup for the exact same url. */
static GstRTSPMedia *
find_media (GstRTSPClient * client, GstRTSPContext * ctx, gchar * path,
    gint * matched)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  gint path_len;

  /* find the longest matching factory for the uri first */
  if (!(factory = gst_rtsp_mount_points_match (priv->mount_points,
              path, matched)))
    goto no_factory;

  ctx->factory = factory;

  if (!gst_rtsp_auth_check (GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_ACCESS))
    goto no_factory_access;

  if (!gst_rtsp_auth_check (GST_RTSP_AUTH_CHECK_MEDIA_FACTORY_CONSTRUCT))
    goto not_authorized;

  if (matched)
    path_len = *matched;
  else
    path_len = strlen (path);

  if (!paths_are_equal (priv->path, path, path_len)) {
    /* remove any previously cached values before we try to construct a new
     * media for uri */
    clean_cached_media (client, TRUE);

    /* prepare the media and add it to the pipeline */
    if (!(media = gst_rtsp_media_factory_construct (factory, ctx->uri)))
      goto no_media;

    ctx->media = media;

    if (!(gst_rtsp_media_get_transport_mode (media) &
            GST_RTSP_TRANSPORT_MODE_RECORD)) {
      GstRTSPThread *thread;

      thread = gst_rtsp_thread_pool_get_thread (priv->thread_pool,
          GST_RTSP_THREAD_TYPE_MEDIA, ctx);
      if (thread == NULL)
        goto no_thread;

      /* prepare the media */
      if (!gst_rtsp_media_prepare (media, thread))
        goto no_prepare;
    }

    /* now keep track of the uri and the media */
    priv->path = g_strndup (path, path_len);
    priv->media = media;
  } else {
    /* we have seen this path before, used cached media */
    media = priv->media;
    ctx->media = media;
    GST_INFO ("reusing cached media %p for path %s", media, priv->path);
  }

  g_object_unref (factory);
  ctx->factory = NULL;

  if (media)
    g_object_ref (media);

  return media;

  /* ERRORS */
no_factory:
  {
    GST_ERROR ("client %p: no factory for path %s", client, path);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return NULL;
  }
no_factory_access:
  {
    g_object_unref (factory);
    ctx->factory = NULL;
    GST_ERROR ("client %p: not authorized to see factory path %s", client,
        path);
    /* error reply is already sent */
    return NULL;
  }
not_authorized:
  {
    g_object_unref (factory);
    ctx->factory = NULL;
    GST_ERROR ("client %p: not authorized for factory path %s", client, path);
    /* error reply is already sent */
    return NULL;
  }
no_media:
  {
    GST_ERROR ("client %p: can't create media", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    g_object_unref (factory);
    ctx->factory = NULL;
    return NULL;
  }
no_thread:
  {
    GST_ERROR ("client %p: can't create thread", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    g_object_unref (media);
    ctx->media = NULL;
    g_object_unref (factory);
    ctx->factory = NULL;
    return NULL;
  }
no_prepare:
  {
    GST_ERROR ("client %p: can't prepare media", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    g_object_unref (media);
    ctx->media = NULL;
    g_object_unref (factory);
    ctx->factory = NULL;
    return NULL;
  }
}

static inline DataSeq *
get_data_seq_element (GstRTSPClient * client, guint8 channel)
{
  GstRTSPClientPrivate *priv = client->priv;
  GArray *data_seqs = priv->data_seqs;
  gint i = 0;

  while (i < data_seqs->len) {
    DataSeq *data_seq = &g_array_index (data_seqs, DataSeq, i);
    if (data_seq->channel == channel)
      return data_seq;
    i++;
  }

  return NULL;
}

static void
add_data_seq (GstRTSPClient * client, guint8 channel)
{
  GstRTSPClientPrivate *priv = client->priv;
  DataSeq data_seq = {.channel = channel,.seq = 0 };

  if (get_data_seq_element (client, channel) == NULL)
    g_array_append_val (priv->data_seqs, data_seq);
}

static void
set_data_seq (GstRTSPClient * client, guint8 channel, guint seq)
{
  DataSeq *data_seq;

  data_seq = get_data_seq_element (client, channel);
  g_assert_nonnull (data_seq);
  data_seq->seq = seq;
}

static guint
get_data_seq (GstRTSPClient * client, guint8 channel)
{
  DataSeq *data_seq;

  data_seq = get_data_seq_element (client, channel);
  g_assert_nonnull (data_seq);
  return data_seq->seq;
}

static gboolean
get_data_channel (GstRTSPClient * client, guint seq, guint8 * channel)
{
  GstRTSPClientPrivate *priv = client->priv;
  GArray *data_seqs = priv->data_seqs;
  gint i = 0;

  while (i < data_seqs->len) {
    DataSeq *data_seq = &g_array_index (data_seqs, DataSeq, i);
    if (data_seq->seq == seq) {
      *channel = data_seq->channel;
      return TRUE;
    }
    i++;
  }

  return FALSE;
}

static gboolean
do_close (gpointer user_data)
{
  GstRTSPClient *client = user_data;

  gst_rtsp_client_close (client);

  return G_SOURCE_REMOVE;
}

static gboolean
do_send_data (GstBuffer * buffer, guint8 channel, GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPMessage message = { 0 };
  gboolean ret = TRUE;

  gst_rtsp_message_init_data (&message, channel);

  gst_rtsp_message_set_body_buffer (&message, buffer);

  g_mutex_lock (&priv->send_lock);
  if (get_data_seq (client, channel) != 0) {
    GST_WARNING ("already a queued data message for channel %d", channel);
    g_mutex_unlock (&priv->send_lock);
    return FALSE;
  }
  if (priv->send_messages_func) {
    ret =
        priv->send_messages_func (client, &message, 1, FALSE, priv->send_data);
  } else if (priv->send_func) {
    ret = priv->send_func (client, &message, FALSE, priv->send_data);
  }
  g_mutex_unlock (&priv->send_lock);

  gst_rtsp_message_unset (&message);

  if (!ret) {
    GSource *idle_src;

    /* close in watch context */
    idle_src = g_idle_source_new ();
    g_source_set_callback (idle_src, do_close, client, NULL);
    g_source_attach (idle_src, priv->watch_context);
    g_source_unref (idle_src);
  }

  return ret;
}

static gboolean
do_check_back_pressure (guint8 channel, GstRTSPClient * client)
{
  return get_data_seq (client, channel) != 0;
}

static gboolean
do_send_data_list (GstBufferList * buffer_list, guint8 channel,
    GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  gboolean ret = TRUE;
  guint i, n = gst_buffer_list_length (buffer_list);
  GstRTSPMessage *messages;

  g_mutex_lock (&priv->send_lock);
  if (get_data_seq (client, channel) != 0) {
    GST_WARNING ("already a queued data message for channel %d", channel);
    g_mutex_unlock (&priv->send_lock);
    return FALSE;
  }

  messages = g_newa (GstRTSPMessage, n);
  memset (messages, 0, sizeof (GstRTSPMessage) * n);
  for (i = 0; i < n; i++) {
    GstBuffer *buffer = gst_buffer_list_get (buffer_list, i);
    gst_rtsp_message_init_data (&messages[i], channel);
    gst_rtsp_message_set_body_buffer (&messages[i], buffer);
  }

  if (priv->send_messages_func) {
    ret =
        priv->send_messages_func (client, messages, n, FALSE, priv->send_data);
  } else if (priv->send_func) {
    for (i = 0; i < n; i++) {
      ret = priv->send_func (client, &messages[i], FALSE, priv->send_data);
      if (!ret)
        break;
    }
  }
  g_mutex_unlock (&priv->send_lock);

  for (i = 0; i < n; i++) {
    gst_rtsp_message_unset (&messages[i]);
  }

  if (!ret) {
    GSource *idle_src;

    /* close in watch context */
    idle_src = g_idle_source_new ();
    g_source_set_callback (idle_src, do_close, client, NULL);
    g_source_attach (idle_src, priv->watch_context);
    g_source_unref (idle_src);
  }

  return ret;
}

/**
 * gst_rtsp_client_close:
 * @client: a #GstRTSPClient
 *
 * Close the connection of @client and remove all media it was managing.
 *
 * Since: 1.4
 */
void
gst_rtsp_client_close (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  const gchar *tunnelid;

  GST_DEBUG ("client %p: closing connection", client);

  g_mutex_lock (&priv->watch_lock);

  /* Work around the lack of thread safety of gst_rtsp_connection_close */
  if (priv->watch) {
    gst_rtsp_watch_set_flushing (priv->watch, TRUE);
  }

  if (priv->connection) {
    if ((tunnelid = gst_rtsp_connection_get_tunnelid (priv->connection))) {
      g_mutex_lock (&tunnels_lock);
      /* remove from tunnelids */
      g_hash_table_remove (tunnels, tunnelid);
      g_mutex_unlock (&tunnels_lock);
    }
    gst_rtsp_connection_flush (priv->connection, TRUE);
    gst_rtsp_connection_close (priv->connection);
  }

  if (priv->watch) {
    GST_DEBUG ("client %p: destroying watch", client);
    g_source_destroy ((GSource *) priv->watch);
    priv->watch = NULL;
    gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
    gst_rtsp_client_set_send_messages_func (client, NULL, NULL, NULL);
    rtsp_ctrl_timeout_remove (client);
  }

  g_mutex_unlock (&priv->watch_lock);
}

static gchar *
default_make_path_from_uri (GstRTSPClient * client, const GstRTSPUrl * uri)
{
  gchar *path;

  if (uri->query)
    path = g_strconcat (uri->abspath, "?", uri->query, NULL);
  else
    path = g_strdup (uri->abspath);

  return path;
}

/* Default signal handler function for all "pre-command" signals, like
 * pre-options-request. It just returns the RTSP return code 200.
 * Subclasses can override this to get another default behaviour.
 */
static GstRTSPStatusCode
default_pre_signal_handler (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GST_LOG_OBJECT (client, "returning GST_RTSP_STS_OK");
  return GST_RTSP_STS_OK;
}

/* The pre-signal accumulator function checks the return value of the signal
 * handlers. If any of them returns an RTSP status code that does not start
 * with 2 it will return FALSE, no more signal handlers will be called, and
 * this last RTSP status code will be the result of the signal emission.
 */
static gboolean
pre_signal_accumulator (GSignalInvocationHint * ihint, GValue * return_accu,
    const GValue * handler_return, gpointer data)
{
  GstRTSPStatusCode handler_value = g_value_get_enum (handler_return);
  GstRTSPStatusCode accumulated_value = g_value_get_enum (return_accu);

  if (handler_value < 200 || handler_value > 299) {
    GST_DEBUG ("handler_value : %d, returning FALSE", handler_value);
    g_value_set_enum (return_accu, handler_value);
    return FALSE;
  }

  /* the accumulated value is initiated to 0 by GLib. if current handler value is
   * bigger then use that instead
   *
   * FIXME: Should we prioritize the 2xx codes in a smarter way?
   *        Like, "201 Created" > "250 Low On Storage Space" > "200 OK"?
   */
  if (handler_value > accumulated_value)
    g_value_set_enum (return_accu, handler_value);

  return TRUE;
}

/* The cleanup_transports function is called from handle_teardown_request() to
 * remove any stream transports from the newly closed session that were added to
 * priv->transports in handle_setup_request(). This is done to avoid forwarding
 * data from the client on a channel that we just closed.
 */
static void
cleanup_transports (GstRTSPClient * client, GPtrArray * transports)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPStreamTransport *stream_transport;
  const GstRTSPTransport *rtsp_transport;
  guint i;

  GST_LOG_OBJECT (client, "potentially removing %u transports",
      transports->len);

  for (i = 0; i < transports->len; i++) {
    stream_transport = g_ptr_array_index (transports, i);
    if (stream_transport == NULL) {
      GST_LOG_OBJECT (client, "stream transport %u is NULL, continue", i);
      continue;
    }

    rtsp_transport = gst_rtsp_stream_transport_get_transport (stream_transport);
    if (rtsp_transport == NULL) {
      GST_LOG_OBJECT (client, "RTSP transport %u is NULL, continue", i);
      continue;
    }

    /* priv->transport only stores transports where RTP is tunneled over RTSP */
    if (rtsp_transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
      if (!g_hash_table_remove (priv->transports,
              GINT_TO_POINTER (rtsp_transport->interleaved.min))) {
        GST_WARNING_OBJECT (client,
            "failed removing transport with key '%d' from priv->transports",
            rtsp_transport->interleaved.min);
      }
      if (!g_hash_table_remove (priv->transports,
              GINT_TO_POINTER (rtsp_transport->interleaved.max))) {
        GST_WARNING_OBJECT (client,
            "failed removing transport with key '%d' from priv->transports",
            rtsp_transport->interleaved.max);
      }
    } else {
      GST_LOG_OBJECT (client, "transport %u not RTP/RTSP, skip it", i);
    }
  }
}

static gboolean
handle_teardown_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPClientClass *klass;
  GstRTSPSession *session;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMedia *media;
  GstRTSPStatusCode code;
  gchar *path;
  gint matched;
  gboolean keep_session;
  GstRTSPStatusCode sig_result;
  GPtrArray *session_media_transports;

  if (!ctx->session)
    goto no_session;

  session = ctx->session;

  if (!ctx->uri)
    goto no_uri;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  path = klass->make_path_from_uri (client, ctx->uri);

  /* get a handle to the configuration of the media in the session */
  sessmedia = gst_rtsp_session_get_media (session, path, &matched);
  if (!sessmedia)
    goto not_found;

  /* only aggregate control for now.. */
  if (path[matched] != '\0')
    goto no_aggregate;

  g_free (path);

  ctx->sessmedia = sessmedia;

  media = gst_rtsp_session_media_get_media (sessmedia);
  g_object_ref (media);
  gst_rtsp_media_lock (media);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_TEARDOWN_REQUEST],
      0, ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  /* get a reference to the transports in the session media so we can clean up
   * our priv->transports before returning */
  session_media_transports = gst_rtsp_session_media_get_transports (sessmedia);

  /* we emit the signal before closing the connection */
  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_TEARDOWN_REQUEST],
      0, ctx);

  gst_rtsp_session_media_set_state (sessmedia, GST_STATE_NULL);

  /* unmanage the media in the session, returns false if all media session
   * are torn down. */
  keep_session = gst_rtsp_session_release_media (session, sessmedia);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  send_message (client, ctx, ctx->response, TRUE);

  if (!keep_session) {
    /* remove the session */
    gst_rtsp_session_pool_remove (priv->session_pool, session);
  }

  gst_rtsp_media_unlock (media);
  g_object_unref (media);

  /* remove all transports that were present in the session media which we just
   * unmanaged from the priv->transports array, so we do not try to handle data
   * on channels that were just closed */
  cleanup_transports (client, session_media_transports);
  g_ptr_array_unref (session_media_transports);

  return TRUE;

  /* ERRORS */
no_session:
  {
    GST_ERROR ("client %p: no session", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    return FALSE;
  }
no_uri:
  {
    GST_ERROR ("client %p: no uri supplied", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
not_found:
  {
    GST_ERROR ("client %p: no media for uri", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    g_free (path);
    return FALSE;
  }
no_aggregate:
  {
    GST_ERROR ("client %p: no aggregate path %s", client, path);
    send_generic_response (client,
        GST_RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED, ctx);
    g_free (path);
    return FALSE;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
}

static GstRTSPResult
default_params_set (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPResult res;

  res = gst_rtsp_params_set (client, ctx);

  return res;
}

static GstRTSPResult
default_params_get (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPResult res;

  res = gst_rtsp_params_get (client, ctx);

  return res;
}

static gboolean
handle_get_param_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;
  GstRTSPStatusCode sig_result;

  g_signal_emit (client,
      gst_rtsp_client_signals[SIGNAL_PRE_GET_PARAMETER_REQUEST], 0, ctx,
      &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  res = gst_rtsp_message_get_body (ctx->request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0 || !data || strlen ((char *) data) == 0) {
    if (ctx->request->type_data.request.version >= GST_RTSP_VERSION_2_0) {
      GST_ERROR_OBJECT (client, "Using PLAY request for keep-alive is forbidden"
          " in RTSP 2.0");
      goto bad_request;
    }

    /* no body (or only '\0'), keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, ctx);
  } else {
    /* there is a body, handle the params */
    res = GST_RTSP_CLIENT_GET_CLASS (client)->params_get (client, ctx);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_message (client, ctx, ctx->response, FALSE);
  }

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_GET_PARAMETER_REQUEST],
      0, ctx);

  return TRUE;

  /* ERRORS */
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    return FALSE;
  }
bad_request:
  {
    GST_ERROR ("client %p: bad request", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
}

static gboolean
handle_set_param_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPResult res;
  guint8 *data;
  guint size;
  GstRTSPStatusCode sig_result;

  g_signal_emit (client,
      gst_rtsp_client_signals[SIGNAL_PRE_SET_PARAMETER_REQUEST], 0, ctx,
      &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  res = gst_rtsp_message_get_body (ctx->request, &data, &size);
  if (res != GST_RTSP_OK)
    goto bad_request;

  if (size == 0 || !data || strlen ((char *) data) == 0) {
    /* no body (or only '\0'), keep-alive request */
    send_generic_response (client, GST_RTSP_STS_OK, ctx);
  } else {
    /* there is a body, handle the params */
    res = GST_RTSP_CLIENT_GET_CLASS (client)->params_set (client, ctx);
    if (res != GST_RTSP_OK)
      goto bad_request;

    send_message (client, ctx, ctx->response, FALSE);
  }

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_SET_PARAMETER_REQUEST],
      0, ctx);

  return TRUE;

  /* ERRORS */
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    return FALSE;
  }
bad_request:
  {
    GST_ERROR ("client %p: bad request", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
}

static gboolean
handle_pause_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPSession *session;
  GstRTSPClientClass *klass;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMedia *media;
  GstRTSPStatusCode code;
  GstRTSPState rtspstate;
  gchar *path;
  gint matched;
  GstRTSPStatusCode sig_result;
  guint i, n;

  if (!(session = ctx->session))
    goto no_session;

  if (!ctx->uri)
    goto no_uri;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  path = klass->make_path_from_uri (client, ctx->uri);

  /* get a handle to the configuration of the media in the session */
  sessmedia = gst_rtsp_session_get_media (session, path, &matched);
  if (!sessmedia)
    goto not_found;

  if (path[matched] != '\0')
    goto no_aggregate;

  g_free (path);

  media = gst_rtsp_session_media_get_media (sessmedia);
  g_object_ref (media);
  gst_rtsp_media_lock (media);
  n = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n; i++) {
    GstRTSPStream *stream = gst_rtsp_media_get_stream (media, i);

    if (gst_rtsp_stream_get_publish_clock_mode (stream) ==
        GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET)
      goto not_supported;
  }

  ctx->sessmedia = sessmedia;

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_PAUSE_REQUEST], 0,
      ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  rtspstate = gst_rtsp_session_media_get_rtsp_state (sessmedia);
  /* the session state must be playing or recording */
  if (rtspstate != GST_RTSP_STATE_PLAYING &&
      rtspstate != GST_RTSP_STATE_RECORDING)
    goto invalid_state;

  /* then pause sending */
  gst_rtsp_session_media_set_state (sessmedia, GST_STATE_PAUSED);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  send_message (client, ctx, ctx->response, FALSE);

  /* the state is now READY */
  gst_rtsp_session_media_set_rtsp_state (sessmedia, GST_RTSP_STATE_READY);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PAUSE_REQUEST], 0, ctx);

  gst_rtsp_media_unlock (media);
  g_object_unref (media);

  return TRUE;

  /* ERRORS */
no_session:
  {
    GST_ERROR ("client %p: no session", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    return FALSE;
  }
no_uri:
  {
    GST_ERROR ("client %p: no uri supplied", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
not_found:
  {
    GST_ERROR ("client %p: no media for uri", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    g_free (path);
    return FALSE;
  }
no_aggregate:
  {
    GST_ERROR ("client %p: no aggregate path %s", client, path);
    send_generic_response (client,
        GST_RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED, ctx);
    g_free (path);
    return FALSE;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
invalid_state:
  {
    GST_ERROR ("client %p: not PLAYING or RECORDING", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
not_supported:
  {
    GST_ERROR ("client %p: pausing not supported", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
}

/* convert @url and @path to a URL used as a content base for the factory
 * located at @path */
static gchar *
make_base_url (GstRTSPClient * client, GstRTSPUrl * url, const gchar * path)
{
  GstRTSPUrl tmp;
  gchar *result;
  const gchar *trail;

  /* check for trailing '/' and append one */
  trail = (path[strlen (path) - 1] != '/' ? "/" : "");

  tmp = *url;
  tmp.user = NULL;
  tmp.passwd = NULL;
  tmp.abspath = g_strdup_printf ("%s%s", path, trail);
  tmp.query = NULL;
  result = gst_rtsp_url_get_request_uri (&tmp);
  g_free (tmp.abspath);

  return result;
}

/* Check if the given header of type double is present and, if so,
 * put it's value in the supplied variable.
 */
static GstRTSPStatusCode
parse_header_value_double (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPHeaderField header, gboolean * present, gdouble * value)
{
  GstRTSPResult res;
  gchar *str;
  gchar *end;

  res = gst_rtsp_message_get_header (ctx->request, header, &str, 0);
  if (res == GST_RTSP_OK) {
    *value = g_ascii_strtod (str, &end);
    if (end == str)
      goto parse_header_failed;

    GST_DEBUG ("client %p: got '%s', value %f", client,
        gst_rtsp_header_as_text (header), *value);
    *present = TRUE;
  } else {
    *present = FALSE;
  }

  return GST_RTSP_STS_OK;

parse_header_failed:
  {
    GST_ERROR ("client %p: failed parsing '%s' header", client,
        gst_rtsp_header_as_text (header));
    return GST_RTSP_STS_BAD_REQUEST;
  }
}

/* Parse scale and speed headers, if present, and set the rate to
 * (rate * scale * speed) */
static GstRTSPStatusCode
parse_scale_and_speed (GstRTSPClient * client, GstRTSPContext * ctx,
    gboolean * scale_present, gboolean * speed_present, gdouble * rate,
    GstSeekFlags * flags)
{
  gdouble scale = 1.0;
  gdouble speed = 1.0;
  GstRTSPStatusCode status;

  GST_DEBUG ("got rate %f", *rate);

  status = parse_header_value_double (client, ctx, GST_RTSP_HDR_SCALE,
      scale_present, &scale);
  if (status != GST_RTSP_STS_OK)
    return status;

  if (*scale_present) {
    GST_DEBUG ("got Scale %f", scale);
    if (scale == 0)
      goto bad_scale_value;
    *rate *= scale;

    if (ABS (scale) != 1.0)
      *flags |= GST_SEEK_FLAG_TRICKMODE;
  }

  GST_DEBUG ("rate after parsing Scale %f", *rate);

  status = parse_header_value_double (client, ctx, GST_RTSP_HDR_SPEED,
      speed_present, &speed);
  if (status != GST_RTSP_STS_OK)
    return status;

  if (*speed_present) {
    GST_DEBUG ("got Speed %f", speed);
    if (speed <= 0)
      goto bad_speed_value;
    *rate *= speed;
  }

  GST_DEBUG ("rate after parsing Speed %f", *rate);

  return status;

bad_scale_value:
  {
    GST_ERROR ("client %p: bad 'Scale' header value (%f)", client, scale);
    return GST_RTSP_STS_BAD_REQUEST;
  }
bad_speed_value:
  {
    GST_ERROR ("client %p: bad 'Speed' header value (%f)", client, speed);
    return GST_RTSP_STS_BAD_REQUEST;
  }
}

static GstRTSPStatusCode
setup_play_mode (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPRangeUnit * unit, gboolean * scale_present, gboolean * speed_present)
{
  gchar *str;
  GstRTSPResult res;
  GstRTSPTimeRange *range = NULL;
  gdouble rate = 1.0;
  GstSeekFlags flags = GST_SEEK_FLAG_NONE;
  GstRTSPClientClass *klass = GST_RTSP_CLIENT_GET_CLASS (client);
  GstRTSPStatusCode rtsp_status_code;
  GstClockTime trickmode_interval = 0;
  gboolean enable_rate_control = TRUE;

  /* parse the range header if we have one */
  res = gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_RANGE, &str, 0);
  if (res == GST_RTSP_OK) {
    gchar *seek_style = NULL;

    res = gst_rtsp_range_parse (str, &range);
    if (res != GST_RTSP_OK)
      goto parse_range_failed;

    *unit = range->unit;

    /* parse seek style header, if present */
    res = gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_SEEK_STYLE,
        &seek_style, 0);

    if (res == GST_RTSP_OK) {
      if (g_strcmp0 (seek_style, "RAP") == 0)
        flags = GST_SEEK_FLAG_ACCURATE;
      else if (g_strcmp0 (seek_style, "CoRAP") == 0)
        flags = GST_SEEK_FLAG_KEY_UNIT;
      else if (g_strcmp0 (seek_style, "First-Prior") == 0)
        flags = GST_SEEK_FLAG_KEY_UNIT & GST_SEEK_FLAG_SNAP_BEFORE;
      else if (g_strcmp0 (seek_style, "Next") == 0)
        flags = GST_SEEK_FLAG_KEY_UNIT & GST_SEEK_FLAG_SNAP_AFTER;
      else
        GST_FIXME_OBJECT (client, "Add support for seek style %s", seek_style);
    } else if (range->min.type == GST_RTSP_TIME_END) {
      flags = GST_SEEK_FLAG_ACCURATE;
    } else {
      flags = GST_SEEK_FLAG_KEY_UNIT;
    }

    if (seek_style)
      gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_SEEK_STYLE,
          seek_style);
  } else {
    flags = GST_SEEK_FLAG_ACCURATE;
  }

  /* check for scale and/or speed headers
   * we will set the seek rate to (speed * scale) and let the media decide
   * the resulting scale and speed. in the response we will use rate and applied
   * rate from the resulting segment as values for the speed and scale headers
   * respectively */
  rtsp_status_code = parse_scale_and_speed (client, ctx, scale_present,
      speed_present, &rate, &flags);
  if (rtsp_status_code != GST_RTSP_STS_OK)
    goto scale_speed_failed;

  /* give the application a chance to tweak range, flags, or rate */
  if (klass->adjust_play_mode != NULL) {
    rtsp_status_code =
        klass->adjust_play_mode (client, ctx, &range, &flags, &rate,
        &trickmode_interval, &enable_rate_control);
    if (rtsp_status_code != GST_RTSP_STS_OK)
      goto adjust_play_mode_failed;
  }

  gst_rtsp_media_set_rate_control (ctx->media, enable_rate_control);

  /* now do the seek with the seek options */
  gst_rtsp_media_seek_trickmode (ctx->media, range, flags, rate,
      trickmode_interval);
  if (range != NULL)
    gst_rtsp_range_free (range);

  if (gst_rtsp_media_get_status (ctx->media) == GST_RTSP_MEDIA_STATUS_ERROR)
    goto seek_failed;

  return GST_RTSP_STS_OK;

parse_range_failed:
  {
    GST_ERROR ("client %p: failed parsing range header", client);
    return GST_RTSP_STS_BAD_REQUEST;
  }
scale_speed_failed:
  {
    if (range != NULL)
      gst_rtsp_range_free (range);
    GST_ERROR ("client %p: failed parsing Scale or Speed headers", client);
    return rtsp_status_code;
  }
adjust_play_mode_failed:
  {
    GST_ERROR ("client %p: sub class returned bad code (%d)", client,
        rtsp_status_code);
    if (range != NULL)
      gst_rtsp_range_free (range);
    return rtsp_status_code;
  }
seek_failed:
  {
    GST_ERROR ("client %p: seek failed", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
}

static gboolean
handle_play_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPSession *session;
  GstRTSPClientClass *klass;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMedia *media;
  GstRTSPStatusCode code;
  GstRTSPUrl *uri;
  gchar *str;
  GstRTSPState rtspstate;
  GstRTSPRangeUnit unit = GST_RTSP_RANGE_NPT;
  gchar *path, *rtpinfo = NULL;
  gint matched;
  GstRTSPStatusCode sig_result;
  GPtrArray *transports;
  gboolean scale_present;
  gboolean speed_present;
  gdouble rate;
  gdouble applied_rate;

  if (!(session = ctx->session))
    goto no_session;

  if (!(uri = ctx->uri))
    goto no_uri;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  path = klass->make_path_from_uri (client, uri);

  /* get a handle to the configuration of the media in the session */
  sessmedia = gst_rtsp_session_get_media (session, path, &matched);
  if (!sessmedia)
    goto not_found;

  if (path[matched] != '\0')
    goto no_aggregate;

  g_free (path);

  ctx->sessmedia = sessmedia;
  ctx->media = media = gst_rtsp_session_media_get_media (sessmedia);

  g_object_ref (media);
  gst_rtsp_media_lock (media);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_PLAY_REQUEST], 0,
      ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  if (!(gst_rtsp_media_get_transport_mode (media) &
          GST_RTSP_TRANSPORT_MODE_PLAY))
    goto unsupported_mode;

  /* the session state must be playing or ready */
  rtspstate = gst_rtsp_session_media_get_rtsp_state (sessmedia);
  if (rtspstate != GST_RTSP_STATE_PLAYING && rtspstate != GST_RTSP_STATE_READY)
    goto invalid_state;

  /* update the pipeline */
  transports = gst_rtsp_session_media_get_transports (sessmedia);
  if (!gst_rtsp_media_complete_pipeline (media, transports)) {
    g_ptr_array_unref (transports);
    goto pipeline_error;
  }
  g_ptr_array_unref (transports);

  /* in play we first unsuspend, media could be suspended from SDP or PAUSED */
  if (!gst_rtsp_media_unsuspend (media))
    goto unsuspend_failed;

  code = setup_play_mode (client, ctx, &unit, &scale_present, &speed_present);
  if (code != GST_RTSP_STS_OK)
    goto invalid_mode;

  /* grab RTPInfo from the media now */
  if (gst_rtsp_media_has_completed_sender (media) &&
      !(rtpinfo = gst_rtsp_session_media_get_rtpinfo (sessmedia)))
    goto rtp_info_error;

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  /* add the RTP-Info header */
  if (rtpinfo)
    gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_RTP_INFO,
        rtpinfo);

  /* add the range */
  str = gst_rtsp_media_get_range_string (media, TRUE, unit);
  if (str)
    gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_RANGE, str);

  if (gst_rtsp_media_has_completed_sender (media)) {
    /* the scale and speed headers must always be added if they were present in
     * the request. however, even if they were not, we still add them if
     * applied_rate or rate deviate from the "normal", i.e. 1.0 */
    if (!gst_rtsp_media_get_rates (media, &rate, &applied_rate))
      goto get_rates_error;
    g_assert (rate != 0 && applied_rate != 0);

    if (scale_present || applied_rate != 1.0)
      gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_SCALE,
          g_strdup_printf ("%1.3f", applied_rate));

    if (speed_present || rate != 1.0)
      gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_SPEED,
          g_strdup_printf ("%1.3f", rate));
  }

  if (klass->adjust_play_response) {
    code = klass->adjust_play_response (client, ctx);
    if (code != GST_RTSP_STS_OK)
      goto adjust_play_response_failed;
  }

  send_message (client, ctx, ctx->response, FALSE);

  /* start playing after sending the response */
  gst_rtsp_session_media_set_state (sessmedia, GST_STATE_PLAYING);

  gst_rtsp_session_media_set_rtsp_state (sessmedia, GST_RTSP_STATE_PLAYING);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PLAY_REQUEST], 0, ctx);

  gst_rtsp_media_unlock (media);
  g_object_unref (media);

  return TRUE;

  /* ERRORS */
no_session:
  {
    GST_ERROR ("client %p: no session", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    return FALSE;
  }
no_uri:
  {
    GST_ERROR ("client %p: no uri supplied", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
not_found:
  {
    GST_ERROR ("client %p: media not found", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_aggregate:
  {
    GST_ERROR ("client %p: no aggregate path %s", client, path);
    send_generic_response (client,
        GST_RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED, ctx);
    g_free (path);
    return FALSE;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
invalid_state:
  {
    GST_ERROR ("client %p: not PLAYING or READY", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
pipeline_error:
  {
    GST_ERROR ("client %p: failed to configure the pipeline", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
unsuspend_failed:
  {
    GST_ERROR ("client %p: unsuspend failed", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
invalid_mode:
  {
    GST_ERROR ("client %p: seek failed", client);
    send_generic_response (client, code, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
unsupported_mode:
  {
    GST_ERROR ("client %p: media does not support PLAY", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_ALLOWED, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
get_rates_error:
  {
    GST_ERROR ("client %p: failed obtaining rate and applied_rate", client);
    send_generic_response (client, GST_RTSP_STS_INTERNAL_SERVER_ERROR, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
adjust_play_response_failed:
  {
    GST_ERROR ("client %p: failed to adjust play response", client);
    send_generic_response (client, code, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
rtp_info_error:
  {
    GST_ERROR ("client %p: failed to add RTP-Info", client);
    send_generic_response (client, GST_RTSP_STS_INTERNAL_SERVER_ERROR, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
}

static void
do_keepalive (GstRTSPSession * session)
{
  GST_INFO ("keep session %p alive", session);
  gst_rtsp_session_touch (session);
}

/* parse @transport and return a valid transport in @tr. only transports
 * supported by @stream are returned. Returns FALSE if no valid transport
 * was found. */
static gboolean
parse_transport (const char *transport, GstRTSPStream * stream,
    GstRTSPTransport * tr)
{
  gint i;
  gboolean res;
  gchar **transports;

  res = FALSE;
  gst_rtsp_transport_init (tr);

  GST_DEBUG ("parsing transports %s", transport);

  transports = g_strsplit (transport, ",", 0);

  /* loop through the transports, try to parse */
  for (i = 0; transports[i]; i++) {
    g_strstrip (transports[i]);
    res = gst_rtsp_transport_parse (transports[i], tr);
    if (res != GST_RTSP_OK) {
      /* no valid transport, search some more */
      GST_WARNING ("could not parse transport %s", transports[i]);
      goto next;
    }

    /* we have a transport, see if it's supported */
    if (!gst_rtsp_stream_is_transport_supported (stream, tr)) {
      GST_WARNING ("unsupported transport %s", transports[i]);
      goto next;
    }

    /* we have a valid transport */
    GST_INFO ("found valid transport %s", transports[i]);
    res = TRUE;
    break;

  next:
    gst_rtsp_transport_init (tr);
  }
  g_strfreev (transports);

  return res;
}

static gboolean
default_configure_client_media (GstRTSPClient * client, GstRTSPMedia * media,
    GstRTSPStream * stream, GstRTSPContext * ctx)
{
  GstRTSPMessage *request = ctx->request;
  gchar *blocksize_str;

  if (!gst_rtsp_stream_is_sender (stream))
    return TRUE;

  if (gst_rtsp_message_get_header (request, GST_RTSP_HDR_BLOCKSIZE,
          &blocksize_str, 0) == GST_RTSP_OK) {
    guint64 blocksize;
    gchar *end;

    blocksize = g_ascii_strtoull (blocksize_str, &end, 10);
    if (end == blocksize_str)
      goto parse_failed;

    /* we don't want to change the mtu when this media
     * can be shared because it impacts other clients */
    if (gst_rtsp_media_is_shared (media))
      goto done;

    if (blocksize > G_MAXUINT)
      blocksize = G_MAXUINT;

    gst_rtsp_stream_set_mtu (stream, blocksize);
  }
done:
  return TRUE;

  /* ERRORS */
parse_failed:
  {
    GST_ERROR_OBJECT (client, "failed to parse blocksize");
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
}

static gboolean
default_configure_client_transport (GstRTSPClient * client,
    GstRTSPContext * ctx, GstRTSPTransport * ct)
{
  GstRTSPClientPrivate *priv = client->priv;

  /* we have a valid transport now, set the destination of the client. */
  if (ct->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST ||
      ct->lower_transport == GST_RTSP_LOWER_TRANS_UDP) {
    /* allocate UDP ports */
    GSocketFamily family;
    gboolean use_client_settings = FALSE;

    family = priv->is_ipv6 ? G_SOCKET_FAMILY_IPV6 : G_SOCKET_FAMILY_IPV4;

    if ((ct->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) &&
        gst_rtsp_auth_check (GST_RTSP_AUTH_CHECK_TRANSPORT_CLIENT_SETTINGS) &&
        (ct->destination != NULL)) {

      if (!gst_rtsp_stream_verify_mcast_ttl (ctx->stream, ct->ttl))
        goto error_ttl;

      use_client_settings = TRUE;
    }

    /* We need to allocate the sockets for both families before starting
     * multiudpsink, otherwise multiudpsink won't accept new clients with
     * a different family.
     */
    /* FIXME: could be more adequately solved by making it possible
     * to set a socket on multiudpsink after it has already been started */
    if (!gst_rtsp_stream_allocate_udp_sockets (ctx->stream,
            G_SOCKET_FAMILY_IPV4, ct, use_client_settings)
        && family == G_SOCKET_FAMILY_IPV4)
      goto error_allocating_ports;

    if (!gst_rtsp_stream_allocate_udp_sockets (ctx->stream,
            G_SOCKET_FAMILY_IPV6, ct, use_client_settings)
        && family == G_SOCKET_FAMILY_IPV6)
      goto error_allocating_ports;

    if (ct->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
      if (use_client_settings) {
        /* FIXME: the address has been successfully allocated, however, in
         * the use_client_settings case we need to verify that the allocated
         * address is the one requested by the client and if this address is
         * an allowed destination. Verifying this via the address pool in not
         * the proper way as the address pool should only be used for choosing
         * the server-selected address/port pairs. */
        GSocket *rtp_socket;
        guint ttl;

        rtp_socket =
            gst_rtsp_stream_get_rtp_multicast_socket (ctx->stream, family);
        if (rtp_socket == NULL)
          goto no_socket;
        ttl = g_socket_get_multicast_ttl (rtp_socket);
        g_object_unref (rtp_socket);
        if (ct->ttl < ttl) {
          /* use the maximum ttl that is requested by multicast clients */
          GST_DEBUG ("requested ttl %u, but keeping ttl %u", ct->ttl, ttl);
          ct->ttl = ttl;
        }

      } else {
        GstRTSPAddress *addr = NULL;

        g_free (ct->destination);
        addr = gst_rtsp_stream_get_multicast_address (ctx->stream, family);
        if (addr == NULL)
          goto no_address;
        ct->destination = g_strdup (addr->address);
        ct->port.min = addr->port;
        ct->port.max = addr->port + addr->n_ports - 1;
        ct->ttl = addr->ttl;
        gst_rtsp_address_free (addr);
      }

      if (!gst_rtsp_stream_add_multicast_client_address (ctx->stream,
              ct->destination, ct->port.min, ct->port.max, family))
        goto error_mcast_transport;

    } else {
      GstRTSPUrl *url;

      url = gst_rtsp_connection_get_url (priv->connection);
      g_free (ct->destination);
      ct->destination = g_strdup (url->host);
    }
  } else {
    GstRTSPUrl *url;

    url = gst_rtsp_connection_get_url (priv->connection);
    g_free (ct->destination);
    ct->destination = g_strdup (url->host);

    if (ct->lower_transport & GST_RTSP_LOWER_TRANS_TCP) {
      GSocket *sock;
      GSocketAddress *addr;

      sock = gst_rtsp_connection_get_read_socket (priv->connection);
      if ((addr = g_socket_get_remote_address (sock, NULL))) {
        /* our read port is the sender port of client */
        ct->client_port.min =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr));
        g_object_unref (addr);
      }
      if ((addr = g_socket_get_local_address (sock, NULL))) {
        ct->server_port.max =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr));
        g_object_unref (addr);
      }
      sock = gst_rtsp_connection_get_write_socket (priv->connection);
      if ((addr = g_socket_get_remote_address (sock, NULL))) {
        /* our write port is the receiver port of client */
        ct->client_port.max =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr));
        g_object_unref (addr);
      }
      if ((addr = g_socket_get_local_address (sock, NULL))) {
        ct->server_port.min =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr));
        g_object_unref (addr);
      }
      /* check if the client selected channels for TCP */
      if (ct->interleaved.min == -1 || ct->interleaved.max == -1) {
        gst_rtsp_session_media_alloc_channels (ctx->sessmedia,
            &ct->interleaved);
      }
      /* alloc new channels if they are already taken */
      while (g_hash_table_contains (priv->transports,
              GINT_TO_POINTER (ct->interleaved.min))
          || g_hash_table_contains (priv->transports,
              GINT_TO_POINTER (ct->interleaved.max))) {
        gst_rtsp_session_media_alloc_channels (ctx->sessmedia,
            &ct->interleaved);
        if (ct->interleaved.max > 255)
          goto error_allocating_channels;
      }
    }
  }
  return TRUE;

  /* ERRORS */
error_ttl:
  {
    GST_ERROR_OBJECT (client,
        "Failed to allocate UDP ports: invalid ttl value");
    return FALSE;
  }
error_allocating_ports:
  {
    GST_ERROR_OBJECT (client, "Failed to allocate UDP ports");
    return FALSE;
  }
no_address:
  {
    GST_ERROR_OBJECT (client, "Failed to acquire address for stream");
    return FALSE;
  }
no_socket:
  {
    GST_ERROR_OBJECT (client, "Failed to get UDP socket");
    return FALSE;
  }
error_mcast_transport:
  {
    GST_ERROR_OBJECT (client, "Failed to add multicast client transport");
    return FALSE;
  }
error_allocating_channels:
  {
    GST_ERROR_OBJECT (client, "Failed to allocate interleaved channels");
    return FALSE;
  }
}

static GstRTSPTransport *
make_server_transport (GstRTSPClient * client, GstRTSPMedia * media,
    GstRTSPContext * ctx, GstRTSPTransport * ct)
{
  GstRTSPTransport *st;
  GInetAddress *addr;
  GSocketFamily family;

  /* prepare the server transport */
  gst_rtsp_transport_new (&st);

  st->trans = ct->trans;
  st->profile = ct->profile;
  st->lower_transport = ct->lower_transport;
  st->mode_play = ct->mode_play;
  st->mode_record = ct->mode_record;

  addr = g_inet_address_new_from_string (ct->destination);

  if (!addr) {
    GST_ERROR ("failed to get inet addr from client destination");
    family = G_SOCKET_FAMILY_IPV4;
  } else {
    family = g_inet_address_get_family (addr);
    g_object_unref (addr);
    addr = NULL;
  }

  switch (st->lower_transport) {
    case GST_RTSP_LOWER_TRANS_UDP:
      st->client_port = ct->client_port;
      gst_rtsp_stream_get_server_port (ctx->stream, &st->server_port, family);
      break;
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
      st->port = ct->port;
      st->destination = g_strdup (ct->destination);
      st->ttl = ct->ttl;
      break;
    case GST_RTSP_LOWER_TRANS_TCP:
      st->interleaved = ct->interleaved;
      st->client_port = ct->client_port;
      st->server_port = ct->server_port;
    default:
      break;
  }

  if ((gst_rtsp_media_get_transport_mode (media) &
          GST_RTSP_TRANSPORT_MODE_PLAY))
    gst_rtsp_stream_get_ssrc (ctx->stream, &st->ssrc);

  return st;
}

static void
rtsp_ctrl_timeout_remove_unlocked (GstRTSPClientPrivate * priv)
{
  if (priv->rtsp_ctrl_timeout != NULL) {
    GST_DEBUG ("rtsp control session removed timeout %p.",
        priv->rtsp_ctrl_timeout);
    g_source_destroy (priv->rtsp_ctrl_timeout);
    g_source_unref (priv->rtsp_ctrl_timeout);
    priv->rtsp_ctrl_timeout = NULL;
    priv->rtsp_ctrl_timeout_cnt = 0;
  }
}

static void
rtsp_ctrl_timeout_remove (GstRTSPClient * client)
{
  g_mutex_lock (&client->priv->lock);
  rtsp_ctrl_timeout_remove_unlocked (client->priv);
  g_mutex_unlock (&client->priv->lock);
}

static void
rtsp_ctrl_timeout_destroy_notify (gpointer user_data)
{
  GWeakRef *client_weak_ref = (GWeakRef *) user_data;

  g_weak_ref_clear (client_weak_ref);
  g_free (client_weak_ref);
}

static gboolean
rtsp_ctrl_timeout_cb (gpointer user_data)
{
  gboolean res = G_SOURCE_CONTINUE;
  GstRTSPClientPrivate *priv;
  GWeakRef *client_weak_ref = (GWeakRef *) user_data;
  GstRTSPClient *client = (GstRTSPClient *) g_weak_ref_get (client_weak_ref);

  if (client == NULL) {
    return G_SOURCE_REMOVE;
  }

  priv = client->priv;
  g_mutex_lock (&priv->lock);
  priv->rtsp_ctrl_timeout_cnt += RTSP_CTRL_CB_INTERVAL;

  if ((priv->rtsp_ctrl_timeout_cnt > RTSP_CTRL_TIMEOUT_VALUE)
      || (priv->had_session
          && priv->rtsp_ctrl_timeout_cnt > priv->post_session_timeout)) {
    GST_DEBUG ("rtsp control session timeout %p expired, closing client.",
        priv->rtsp_ctrl_timeout);
    rtsp_ctrl_timeout_remove_unlocked (client->priv);

    res = G_SOURCE_REMOVE;
  }

  g_mutex_unlock (&priv->lock);

  if (res == G_SOURCE_REMOVE) {
    gst_rtsp_client_close (client);
  }

  g_object_unref (client);

  return res;
}

static gchar *
stream_make_keymgmt (GstRTSPClient * client, const gchar * location,
    GstRTSPStream * stream)
{
  gchar *base64, *result = NULL;
  GstMIKEYMessage *mikey_msg;
  GstCaps *srtcpparams;
  GstElement *rtcp_encoder;
  gint srtcp_cipher, srtp_cipher;
  gint srtcp_auth, srtp_auth;
  GstBuffer *key;
  GType ciphertype, authtype;
  GEnumClass *cipher_enum, *auth_enum;
  GEnumValue *srtcp_cipher_value, *srtp_cipher_value, *srtcp_auth_value,
      *srtp_auth_value;

  rtcp_encoder = gst_rtsp_stream_get_srtp_encoder (stream);

  if (!rtcp_encoder)
    goto done;

  ciphertype = g_type_from_name ("GstSrtpCipherType");
  authtype = g_type_from_name ("GstSrtpAuthType");

  cipher_enum = g_type_class_ref (ciphertype);
  auth_enum = g_type_class_ref (authtype);

  /* We need to bring the encoder to READY so that it generates its key */
  gst_element_set_state (rtcp_encoder, GST_STATE_READY);

  g_object_get (rtcp_encoder, "rtcp-cipher", &srtcp_cipher, "rtcp-auth",
      &srtcp_auth, "rtp-cipher", &srtp_cipher, "rtp-auth", &srtp_auth, "key",
      &key, NULL);
  g_object_unref (rtcp_encoder);

  srtcp_cipher_value = g_enum_get_value (cipher_enum, srtcp_cipher);
  srtp_cipher_value = g_enum_get_value (cipher_enum, srtp_cipher);
  srtcp_auth_value = g_enum_get_value (auth_enum, srtcp_auth);
  srtp_auth_value = g_enum_get_value (auth_enum, srtp_auth);

  g_type_class_unref (cipher_enum);
  g_type_class_unref (auth_enum);

  srtcpparams = gst_caps_new_simple ("application/x-srtcp",
      "srtcp-cipher", G_TYPE_STRING, srtcp_cipher_value->value_nick,
      "srtcp-auth", G_TYPE_STRING, srtcp_auth_value->value_nick,
      "srtp-cipher", G_TYPE_STRING, srtp_cipher_value->value_nick,
      "srtp-auth", G_TYPE_STRING, srtp_auth_value->value_nick,
      "srtp-key", GST_TYPE_BUFFER, key, NULL);

  mikey_msg = gst_mikey_message_new_from_caps (srtcpparams);
  if (mikey_msg) {
    guint send_ssrc;

    gst_rtsp_stream_get_ssrc (stream, &send_ssrc);
    gst_mikey_message_add_cs_srtp (mikey_msg, 0, send_ssrc, 0);

    base64 = gst_mikey_message_base64_encode (mikey_msg);
    gst_mikey_message_unref (mikey_msg);

    if (base64) {
      result = gst_sdp_make_keymgmt (location, base64);
      g_free (base64);
    }
  }

done:
  return result;
}

static gboolean
handle_setup_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPResult res;
  GstRTSPUrl *uri;
  gchar *transport, *keymgmt;
  GstRTSPTransport *ct, *st;
  GstRTSPStatusCode code;
  GstRTSPSession *session;
  GstRTSPStreamTransport *trans;
  gchar *trans_str;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMedia *media;
  GstRTSPStream *stream;
  GstRTSPState rtspstate;
  GstRTSPClientClass *klass;
  gchar *path, *control = NULL;
  gint matched;
  gboolean new_session = FALSE;
  GstRTSPStatusCode sig_result;
  gchar *pipelined_request_id = NULL, *accept_range = NULL;

  if (!ctx->uri)
    goto no_uri;

  uri = ctx->uri;
  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  path = klass->make_path_from_uri (client, uri);

  /* parse the transport */
  res =
      gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_TRANSPORT,
      &transport, 0);
  if (res != GST_RTSP_OK)
    goto no_transport;

  /* Handle Pipelined-requests if using >= 2.0 */
  if (ctx->request->type_data.request.version >= GST_RTSP_VERSION_2_0)
    gst_rtsp_message_get_header (ctx->request,
        GST_RTSP_HDR_PIPELINED_REQUESTS, &pipelined_request_id, 0);

  /* we create the session after parsing stuff so that we don't make
   * a session for malformed requests */
  if (priv->session_pool == NULL)
    goto no_pool;

  session = ctx->session;

  if (session) {
    g_object_ref (session);
    /* get a handle to the configuration of the media in the session, this can
     * return NULL if this is a new url to manage in this session. */
    sessmedia = gst_rtsp_session_get_media (session, path, &matched);
  } else {
    /* we need a new media configuration in this session */
    sessmedia = NULL;
  }

  /* we have no session media, find one and manage it */
  if (sessmedia == NULL) {
    /* get a handle to the configuration of the media in the session */
    media = find_media (client, ctx, path, &matched);
    /* need to suspend the media, if the protocol has changed */
    if (media != NULL) {
      gst_rtsp_media_lock (media);
      gst_rtsp_media_suspend (media);
    }
  } else {
    if ((media = gst_rtsp_session_media_get_media (sessmedia))) {
      g_object_ref (media);
      gst_rtsp_media_lock (media);
    } else {
      goto media_not_found;
    }
  }
  /* no media, not found then */
  if (media == NULL)
    goto media_not_found_no_reply;

  if (path[matched] == '\0') {
    if (gst_rtsp_media_n_streams (media) == 1) {
      stream = gst_rtsp_media_get_stream (media, 0);
    } else {
      goto control_not_found;
    }
  } else {
    /* path is what matched. */
    path[matched] = '\0';
    /* control is remainder */
    control = &path[matched + 1];

    /* find the stream now using the control part */
    stream = gst_rtsp_media_find_stream (media, control);
  }

  if (stream == NULL)
    goto stream_not_found;

  /* now we have a uri identifying a valid media and stream */
  ctx->stream = stream;
  ctx->media = media;

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_SETUP_REQUEST], 0,
      ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  if (session == NULL) {
    /* create a session if this fails we probably reached our session limit or
     * something. */
    if (!(session = gst_rtsp_session_pool_create (priv->session_pool)))
      goto service_unavailable;

    /* Pipelined requests should be cleared between sessions */
    g_hash_table_remove_all (priv->pipelined_requests);

    /* make sure this client is closed when the session is closed */
    client_watch_session (client, session);

    new_session = TRUE;
    /* signal new session */
    g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_NEW_SESSION], 0,
        session);

    ctx->session = session;
  }

  if (pipelined_request_id) {
    g_hash_table_insert (client->priv->pipelined_requests,
        g_strdup (pipelined_request_id),
        g_strdup (gst_rtsp_session_get_sessionid (session)));
  }
  /* Remember that we had at least one session in the past */
  priv->had_session = TRUE;
  rtsp_ctrl_timeout_remove (client);

  if (!klass->configure_client_media (client, media, stream, ctx))
    goto configure_media_failed_no_reply;

  gst_rtsp_transport_new (&ct);

  /* parse and find a usable supported transport */
  if (!parse_transport (transport, stream, ct))
    goto unsupported_transports;

  if ((ct->mode_play
          && !(gst_rtsp_media_get_transport_mode (media) &
              GST_RTSP_TRANSPORT_MODE_PLAY)) || (ct->mode_record
          && !(gst_rtsp_media_get_transport_mode (media) &
              GST_RTSP_TRANSPORT_MODE_RECORD)))
    goto unsupported_mode;

  /* parse the keymgmt */
  if (gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_KEYMGMT,
          &keymgmt, 0) == GST_RTSP_OK) {
    if (!gst_rtsp_stream_handle_keymgmt (ctx->stream, keymgmt))
      goto keymgmt_error;
  }

  if (gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_ACCEPT_RANGES,
          &accept_range, 0) == GST_RTSP_OK) {
    GEnumValue *runit = NULL;
    gint i;
    gchar **valid_ranges;
    GEnumClass *runit_class = g_type_class_ref (GST_TYPE_RTSP_RANGE_UNIT);

    gst_rtsp_message_dump (ctx->request);
    valid_ranges = g_strsplit (accept_range, ",", -1);

    for (i = 0; valid_ranges[i]; i++) {
      gchar *range = valid_ranges[i];

      while (*range == ' ')
        range++;

      runit = g_enum_get_value_by_nick (runit_class, range);
      if (runit)
        break;
    }
    g_strfreev (valid_ranges);
    g_type_class_unref (runit_class);

    if (!runit)
      goto unsupported_range_unit;
  }

  if (sessmedia == NULL) {
    /* manage the media in our session now, if not done already  */
    sessmedia =
        gst_rtsp_session_manage_media (session, path, g_object_ref (media));
    /* if we stil have no media, error */
    if (sessmedia == NULL)
      goto sessmedia_unavailable;

    /* don't cache media anymore */
    clean_cached_media (client, FALSE);
  }

  ctx->sessmedia = sessmedia;

  /* update the client transport */
  if (!klass->configure_client_transport (client, ctx, ct))
    goto unsupported_client_transport;

  /* set in the session media transport */
  trans = gst_rtsp_session_media_set_transport (sessmedia, stream, ct);

  ctx->trans = trans;

  /* configure the url used to set this transport, this we will use when
   * generating the response for the PLAY request */
  gst_rtsp_stream_transport_set_url (trans, uri);
  /* configure keepalive for this transport */
  gst_rtsp_stream_transport_set_keepalive (trans,
      (GstRTSPKeepAliveFunc) do_keepalive, session, NULL);

  if (ct->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
    /* our callbacks to send data on this TCP connection */
    gst_rtsp_stream_transport_set_callbacks (trans,
        (GstRTSPSendFunc) do_send_data,
        (GstRTSPSendFunc) do_send_data, client, NULL);
    gst_rtsp_stream_transport_set_list_callbacks (trans,
        (GstRTSPSendListFunc) do_send_data_list,
        (GstRTSPSendListFunc) do_send_data_list, client, NULL);

    gst_rtsp_stream_transport_set_back_pressure_callback (trans,
        (GstRTSPBackPressureFunc) do_check_back_pressure, client, NULL);

    g_hash_table_insert (priv->transports,
        GINT_TO_POINTER (ct->interleaved.min), trans);
    g_object_ref (trans);
    g_hash_table_insert (priv->transports,
        GINT_TO_POINTER (ct->interleaved.max), trans);
    g_object_ref (trans);
    add_data_seq (client, ct->interleaved.min);
    add_data_seq (client, ct->interleaved.max);
  }

  /* create and serialize the server transport */
  st = make_server_transport (client, media, ctx, ct);
  trans_str = gst_rtsp_transport_as_text (st);
  gst_rtsp_transport_free (st);

  /* construct the response now */
  code = GST_RTSP_STS_OK;
  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_TRANSPORT,
      trans_str);
  g_free (trans_str);

  if (pipelined_request_id)
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_PIPELINED_REQUESTS,
        pipelined_request_id);

  if (ctx->request->type_data.request.version >= GST_RTSP_VERSION_2_0) {
    GstClockTimeDiff seekable = gst_rtsp_media_seekable (media);
    GString *media_properties = g_string_new (NULL);

    if (seekable == -1)
      g_string_append (media_properties,
          "No-Seeking,Time-Progressing,Time-Duration=0.0");
    else if (seekable == 0)
      g_string_append (media_properties, "Beginning-Only");
    else if (seekable == G_MAXINT64)
      g_string_append (media_properties, "Random-Access");
    else
      g_string_append_printf (media_properties,
          "Random-Access=%f, Unlimited, Immutable",
          (gdouble) seekable / GST_SECOND);

    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_MEDIA_PROPERTIES,
        media_properties->str);
    g_string_free (media_properties, TRUE);
    /* TODO Check how Accept-Ranges should be filled */
    gst_rtsp_message_add_header (ctx->request, GST_RTSP_HDR_ACCEPT_RANGES,
        "npt, clock, smpte, clock");
  }

  send_message (client, ctx, ctx->response, FALSE);

  /* update the state */
  rtspstate = gst_rtsp_session_media_get_rtsp_state (sessmedia);
  switch (rtspstate) {
    case GST_RTSP_STATE_PLAYING:
    case GST_RTSP_STATE_RECORDING:
    case GST_RTSP_STATE_READY:
      /* no state change */
      break;
    default:
      gst_rtsp_session_media_set_rtsp_state (sessmedia, GST_RTSP_STATE_READY);
      break;
  }

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_SETUP_REQUEST], 0, ctx);

  gst_rtsp_media_unlock (media);
  g_object_unref (media);
  g_object_unref (session);
  g_free (path);

  return TRUE;

  /* ERRORS */
no_uri:
  {
    GST_ERROR ("client %p: no uri", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
no_transport:
  {
    GST_ERROR ("client %p: no transport", client);
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, ctx);
    goto cleanup_path;
  }
no_pool:
  {
    GST_ERROR ("client %p: no session pool configured", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    goto cleanup_path;
  }
media_not_found_no_reply:
  {
    GST_ERROR ("client %p: media '%s' not found", client, path);
    /* error reply is already sent */
    goto cleanup_session;
  }
media_not_found:
  {
    GST_ERROR ("client %p: media '%s' not found", client, path);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    goto cleanup_session;
  }
control_not_found:
  {
    GST_ERROR ("client %p: no control in path '%s'", client, path);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    goto cleanup_session;
  }
stream_not_found:
  {
    GST_ERROR ("client %p: stream '%s' not found", client,
        GST_STR_NULL (control));
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    goto cleanup_session;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    goto cleanup_path;
  }
service_unavailable:
  {
    GST_ERROR ("client %p: can't create session", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    goto cleanup_session;
  }
sessmedia_unavailable:
  {
    GST_ERROR ("client %p: can't create session media", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    goto cleanup_transport;
  }
configure_media_failed_no_reply:
  {
    GST_ERROR ("client %p: configure_media failed", client);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    /* error reply is already sent */
    goto cleanup_session;
  }
unsupported_transports:
  {
    GST_ERROR ("client %p: unsupported transports", client);
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, ctx);
    goto cleanup_transport;
  }
unsupported_client_transport:
  {
    GST_ERROR ("client %p: unsupported client transport", client);
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, ctx);
    goto cleanup_transport;
  }
unsupported_mode:
  {
    GST_ERROR ("client %p: unsupported mode (media play: %d, media record: %d, "
        "mode play: %d, mode record: %d)", client,
        ! !(gst_rtsp_media_get_transport_mode (media) &
            GST_RTSP_TRANSPORT_MODE_PLAY),
        ! !(gst_rtsp_media_get_transport_mode (media) &
            GST_RTSP_TRANSPORT_MODE_RECORD), ct->mode_play, ct->mode_record);
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_TRANSPORT, ctx);
    goto cleanup_transport;
  }
unsupported_range_unit:
  {
    GST_ERROR ("Client %p: does not support any range format we support",
        client);
    send_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, ctx);
    goto cleanup_transport;
  }
keymgmt_error:
  {
    GST_ERROR ("client %p: keymgmt error", client);
    send_generic_response (client, GST_RTSP_STS_KEY_MANAGEMENT_FAILURE, ctx);
    goto cleanup_transport;
  }
  {
  cleanup_transport:
    gst_rtsp_transport_free (ct);
    if (media) {
      gst_rtsp_media_unlock (media);
      g_object_unref (media);
    }
  cleanup_session:
    if (new_session)
      gst_rtsp_session_pool_remove (priv->session_pool, session);
    if (session)
      g_object_unref (session);
  cleanup_path:
    g_free (path);
    return FALSE;
  }
}

static GstSDPMessage *
create_sdp (GstRTSPClient * client, GstRTSPMedia * media)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstSDPMessage *sdp;
  GstSDPInfo info;
  const gchar *proto;
  guint64 session_id_tmp;
  gchar session_id[21];

  gst_sdp_message_new (&sdp);

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");

  if (priv->is_ipv6)
    proto = "IP6";
  else
    proto = "IP4";

  session_id_tmp = (((guint64) g_random_int ()) << 32) | g_random_int ();
  g_snprintf (session_id, sizeof (session_id), "%" G_GUINT64_FORMAT,
      session_id_tmp);

  gst_sdp_message_set_origin (sdp, "-", session_id, "1", "IN", proto,
      priv->server_ip);

  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtsp-server");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");
  gst_sdp_message_add_attribute (sdp, "type", "broadcast");
  gst_sdp_message_add_attribute (sdp, "control", "*");

  info.is_ipv6 = priv->is_ipv6;
  info.server_ip = priv->server_ip;

  /* create an SDP for the media object */
  if (!gst_rtsp_media_setup_sdp (media, sdp, &info))
    goto no_sdp;

  return sdp;

  /* ERRORS */
no_sdp:
  {
    GST_ERROR ("client %p: could not create SDP", client);
    gst_sdp_message_free (sdp);
    return NULL;
  }
}

/* for the describe we must generate an SDP */
static gboolean
handle_describe_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPResult res;
  GstSDPMessage *sdp;
  guint i;
  gchar *path, *str;
  GstRTSPMedia *media;
  GstRTSPClientClass *klass;
  GstRTSPStatusCode sig_result;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);

  if (!ctx->uri)
    goto no_uri;

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_DESCRIBE_REQUEST],
      0, ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  /* check what kind of format is accepted, we don't really do anything with it
   * and always return SDP for now. */
  for (i = 0;; i++) {
    gchar *accept;

    res =
        gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_ACCEPT,
        &accept, i);
    if (res == GST_RTSP_ENOTIMPL)
      break;

    if (g_ascii_strcasecmp (accept, "application/sdp") == 0)
      break;
  }

  if (!priv->mount_points)
    goto no_mount_points;

  if (!(path = gst_rtsp_mount_points_make_path (priv->mount_points, ctx->uri)))
    goto no_path;

  /* find the media object for the uri */
  if (!(media = find_media (client, ctx, path, NULL)))
    goto no_media;

  gst_rtsp_media_lock (media);

  if (!(gst_rtsp_media_get_transport_mode (media) &
          GST_RTSP_TRANSPORT_MODE_PLAY))
    goto unsupported_mode;

  /* create an SDP for the media object on this client */
  if (!(sdp = klass->create_sdp (client, media)))
    goto no_sdp;

  /* we suspend after the describe */
  gst_rtsp_media_suspend (media);

  gst_rtsp_message_init_response (ctx->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), ctx->request);

  gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  /* content base for some clients that might screw up creating the setup uri */
  str = make_base_url (client, ctx->uri, path);
  g_free (path);

  GST_INFO ("adding content-base: %s", str);
  gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_CONTENT_BASE, str);

  /* add SDP to the response body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (ctx->response, (guint8 *) str, strlen (str));
  gst_sdp_message_free (sdp);

  send_message (client, ctx, ctx->response, FALSE);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_DESCRIBE_REQUEST],
      0, ctx);

  gst_rtsp_media_unlock (media);
  g_object_unref (media);

  return TRUE;

  /* ERRORS */
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    return FALSE;
  }
no_uri:
  {
    GST_ERROR ("client %p: no uri", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
no_mount_points:
  {
    GST_ERROR ("client %p: no mount points configured", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_path:
  {
    GST_ERROR ("client %p: can't find path for url", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_media:
  {
    GST_ERROR ("client %p: no media", client);
    g_free (path);
    /* error reply is already sent */
    return FALSE;
  }
unsupported_mode:
  {
    GST_ERROR ("client %p: media does not support DESCRIBE", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_ALLOWED, ctx);
    g_free (path);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
no_sdp:
  {
    GST_ERROR ("client %p: can't create SDP", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    g_free (path);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
}

static gboolean
handle_sdp (GstRTSPClient * client, GstRTSPContext * ctx, GstRTSPMedia * media,
    GstSDPMessage * sdp)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPThread *thread;

  /* create an SDP for the media object */
  if (!gst_rtsp_media_handle_sdp (media, sdp))
    goto unhandled_sdp;

  thread = gst_rtsp_thread_pool_get_thread (priv->thread_pool,
      GST_RTSP_THREAD_TYPE_MEDIA, ctx);
  if (thread == NULL)
    goto no_thread;

  /* prepare the media */
  if (!gst_rtsp_media_prepare (media, thread))
    goto no_prepare;

  return TRUE;

  /* ERRORS */
unhandled_sdp:
  {
    GST_ERROR ("client %p: could not handle SDP", client);
    return FALSE;
  }
no_thread:
  {
    GST_ERROR ("client %p: can't create thread", client);
    return FALSE;
  }
no_prepare:
  {
    GST_ERROR ("client %p: can't prepare media", client);
    return FALSE;
  }
}

static gboolean
handle_announce_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPClientClass *klass;
  GstSDPResult sres;
  GstSDPMessage *sdp;
  GstRTSPMedia *media;
  gchar *path, *cont = NULL;
  guint8 *data;
  guint size;
  GstRTSPStatusCode sig_result;
  guint i, n_streams;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);

  if (!ctx->uri)
    goto no_uri;

  if (!priv->mount_points)
    goto no_mount_points;

  /* check if reply is SDP */
  gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_CONTENT_TYPE, &cont,
      0);
  /* could not be set but since the request returned OK, we assume it
   * was SDP, else check it. */
  if (cont) {
    if (g_ascii_strcasecmp (cont, "application/sdp") != 0)
      goto wrong_content_type;
  }

  /* get message body and parse as SDP */
  gst_rtsp_message_get_body (ctx->request, &data, &size);
  if (data == NULL || size == 0)
    goto no_message;

  GST_DEBUG ("client %p: parse SDP...", client);
  gst_sdp_message_new (&sdp);
  sres = gst_sdp_message_parse_buffer (data, size, sdp);
  if (sres != GST_SDP_OK)
    goto sdp_parse_failed;

  if (!(path = gst_rtsp_mount_points_make_path (priv->mount_points, ctx->uri)))
    goto no_path;

  /* find the media object for the uri */
  if (!(media = find_media (client, ctx, path, NULL)))
    goto no_media;

  ctx->media = media;
  gst_rtsp_media_lock (media);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_ANNOUNCE_REQUEST],
      0, ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  if (!(gst_rtsp_media_get_transport_mode (media) &
          GST_RTSP_TRANSPORT_MODE_RECORD))
    goto unsupported_mode;

  /* Tell client subclass about the media */
  if (!klass->handle_sdp (client, ctx, media, sdp))
    goto unhandled_sdp;

  gst_rtsp_message_init_response (ctx->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), ctx->request);

  n_streams = gst_rtsp_media_n_streams (media);
  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream = gst_rtsp_media_get_stream (media, i);
    gchar *uri, *location, *keymgmt;

    uri = gst_rtsp_url_get_request_uri (ctx->uri);
    location = g_strdup_printf ("%s/stream=%d", uri, i);
    keymgmt = stream_make_keymgmt (client, location, stream);

    if (keymgmt)
      gst_rtsp_message_take_header (ctx->response, GST_RTSP_HDR_KEYMGMT,
          keymgmt);

    g_free (location);
    g_free (uri);
  }

  /* we suspend after the announce */
  gst_rtsp_media_suspend (media);

  send_message (client, ctx, ctx->response, FALSE);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_ANNOUNCE_REQUEST],
      0, ctx);

  gst_sdp_message_free (sdp);
  g_free (path);
  gst_rtsp_media_unlock (media);
  g_object_unref (media);

  return TRUE;

no_uri:
  {
    GST_ERROR ("client %p: no uri", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
no_mount_points:
  {
    GST_ERROR ("client %p: no mount points configured", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_path:
  {
    GST_ERROR ("client %p: can't find path for url", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    gst_sdp_message_free (sdp);
    return FALSE;
  }
wrong_content_type:
  {
    GST_ERROR ("client %p: unknown content type", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
no_message:
  {
    GST_ERROR ("client %p: can't find SDP message", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
sdp_parse_failed:
  {
    GST_ERROR ("client %p: failed to parse SDP message", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    gst_sdp_message_free (sdp);
    return FALSE;
  }
no_media:
  {
    GST_ERROR ("client %p: no media", client);
    g_free (path);
    /* error reply is already sent */
    gst_sdp_message_free (sdp);
    return FALSE;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_sdp_message_free (sdp);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    return FALSE;
  }
unsupported_mode:
  {
    GST_ERROR ("client %p: media does not support ANNOUNCE", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_ALLOWED, ctx);
    g_free (path);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    gst_sdp_message_free (sdp);
    return FALSE;
  }
unhandled_sdp:
  {
    GST_ERROR ("client %p: can't handle SDP", client);
    send_generic_response (client, GST_RTSP_STS_UNSUPPORTED_MEDIA_TYPE, ctx);
    g_free (path);
    gst_rtsp_media_unlock (media);
    g_object_unref (media);
    gst_sdp_message_free (sdp);
    return FALSE;
  }
}

static gboolean
handle_record_request (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPSession *session;
  GstRTSPClientClass *klass;
  GstRTSPSessionMedia *sessmedia;
  GstRTSPMedia *media;
  GstRTSPUrl *uri;
  GstRTSPState rtspstate;
  gchar *path;
  gint matched;
  GstRTSPStatusCode sig_result;
  GPtrArray *transports;

  if (!(session = ctx->session))
    goto no_session;

  if (!(uri = ctx->uri))
    goto no_uri;

  klass = GST_RTSP_CLIENT_GET_CLASS (client);
  path = klass->make_path_from_uri (client, uri);

  /* get a handle to the configuration of the media in the session */
  sessmedia = gst_rtsp_session_get_media (session, path, &matched);
  if (!sessmedia)
    goto not_found;

  if (path[matched] != '\0')
    goto no_aggregate;

  g_free (path);

  ctx->sessmedia = sessmedia;
  ctx->media = media = gst_rtsp_session_media_get_media (sessmedia);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_RECORD_REQUEST], 0,
      ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  if (!(gst_rtsp_media_get_transport_mode (media) &
          GST_RTSP_TRANSPORT_MODE_RECORD))
    goto unsupported_mode;

  /* the session state must be playing or ready */
  rtspstate = gst_rtsp_session_media_get_rtsp_state (sessmedia);
  if (rtspstate != GST_RTSP_STATE_PLAYING && rtspstate != GST_RTSP_STATE_READY)
    goto invalid_state;

  /* update the pipeline */
  transports = gst_rtsp_session_media_get_transports (sessmedia);
  if (!gst_rtsp_media_complete_pipeline (media, transports)) {
    g_ptr_array_unref (transports);
    goto pipeline_error;
  }
  g_ptr_array_unref (transports);

  /* in record we first unsuspend, media could be suspended from SDP or PAUSED */
  if (!gst_rtsp_media_unsuspend (media))
    goto unsuspend_failed;

  gst_rtsp_message_init_response (ctx->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), ctx->request);

  send_message (client, ctx, ctx->response, FALSE);

  /* start playing after sending the response */
  gst_rtsp_session_media_set_state (sessmedia, GST_STATE_PLAYING);

  gst_rtsp_session_media_set_rtsp_state (sessmedia, GST_RTSP_STATE_PLAYING);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_RECORD_REQUEST], 0,
      ctx);

  return TRUE;

  /* ERRORS */
no_session:
  {
    GST_ERROR ("client %p: no session", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    return FALSE;
  }
no_uri:
  {
    GST_ERROR ("client %p: no uri supplied", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    return FALSE;
  }
not_found:
  {
    GST_ERROR ("client %p: media not found", client);
    send_generic_response (client, GST_RTSP_STS_NOT_FOUND, ctx);
    return FALSE;
  }
no_aggregate:
  {
    GST_ERROR ("client %p: no aggregate path %s", client, path);
    send_generic_response (client,
        GST_RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED, ctx);
    g_free (path);
    return FALSE;
  }
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    return FALSE;
  }
unsupported_mode:
  {
    GST_ERROR ("client %p: media does not support RECORD", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_ALLOWED, ctx);
    return FALSE;
  }
invalid_state:
  {
    GST_ERROR ("client %p: not PLAYING or READY", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        ctx);
    return FALSE;
  }
pipeline_error:
  {
    GST_ERROR ("client %p: failed to configure the pipeline", client);
    send_generic_response (client, GST_RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
        ctx);
    return FALSE;
  }
unsuspend_failed:
  {
    GST_ERROR ("client %p: unsuspend failed", client);
    send_generic_response (client, GST_RTSP_STS_SERVICE_UNAVAILABLE, ctx);
    return FALSE;
  }
}

static gboolean
handle_options_request (GstRTSPClient * client, GstRTSPContext * ctx,
    GstRTSPVersion version)
{
  GstRTSPMethod options;
  gchar *str;
  GstRTSPStatusCode sig_result;

  options = GST_RTSP_DESCRIBE |
      GST_RTSP_OPTIONS |
      GST_RTSP_PAUSE |
      GST_RTSP_PLAY |
      GST_RTSP_SETUP |
      GST_RTSP_GET_PARAMETER | GST_RTSP_SET_PARAMETER | GST_RTSP_TEARDOWN;

  if (version < GST_RTSP_VERSION_2_0) {
    options |= GST_RTSP_RECORD;
    options |= GST_RTSP_ANNOUNCE;
  }

  str = gst_rtsp_options_as_text (options);

  gst_rtsp_message_init_response (ctx->response, GST_RTSP_STS_OK,
      gst_rtsp_status_as_text (GST_RTSP_STS_OK), ctx->request);

  gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_PUBLIC, str);
  g_free (str);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_PRE_OPTIONS_REQUEST], 0,
      ctx, &sig_result);
  if (sig_result != GST_RTSP_STS_OK) {
    goto sig_failed;
  }

  send_message (client, ctx, ctx->response, FALSE);

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_OPTIONS_REQUEST],
      0, ctx);

  return TRUE;

/* ERRORS */
sig_failed:
  {
    GST_ERROR ("client %p: pre signal returned error: %s", client,
        gst_rtsp_status_as_text (sig_result));
    send_generic_response (client, sig_result, ctx);
    gst_rtsp_message_free (ctx->response);
    return FALSE;
  }
}

/* remove duplicate and trailing '/' */
static void
sanitize_uri (GstRTSPUrl * uri)
{
  gint i, len;
  gchar *s, *d;
  gboolean have_slash, prev_slash;

  s = d = uri->abspath;
  len = strlen (uri->abspath);

  prev_slash = FALSE;

  for (i = 0; i < len; i++) {
    have_slash = s[i] == '/';
    *d = s[i];
    if (!have_slash || !prev_slash)
      d++;
    prev_slash = have_slash;
  }
  len = d - uri->abspath;
  /* don't remove the first slash if that's the only thing left */
  if (len > 1 && *(d - 1) == '/')
    d--;
  *d = '\0';
}

/* is called when the session is removed from its session pool. */
static void
client_session_removed (GstRTSPSessionPool * pool, GstRTSPSession * session,
    GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  GSource *timer_src;

  GST_INFO ("client %p: session %p removed", client, session);

  g_mutex_lock (&priv->lock);
  client_unwatch_session (client, session, NULL);

  if (!priv->sessions && priv->rtsp_ctrl_timeout == NULL) {
    if (priv->post_session_timeout > 0) {
      GWeakRef *client_weak_ref = g_new (GWeakRef, 1);
      timer_src = g_timeout_source_new_seconds (RTSP_CTRL_CB_INTERVAL);

      g_weak_ref_init (client_weak_ref, client);
      g_source_set_callback (timer_src, rtsp_ctrl_timeout_cb, client_weak_ref,
          rtsp_ctrl_timeout_destroy_notify);
      priv->rtsp_ctrl_timeout_cnt = 0;
      g_source_attach (timer_src, priv->watch_context);
      priv->rtsp_ctrl_timeout = timer_src;
      GST_DEBUG ("rtsp control setting up connection timeout %p.",
          priv->rtsp_ctrl_timeout);
      g_mutex_unlock (&priv->lock);
    } else if (priv->post_session_timeout == 0) {
      g_mutex_unlock (&priv->lock);
      gst_rtsp_client_close (client);
    } else {
      g_mutex_unlock (&priv->lock);
    }
  } else {
    g_mutex_unlock (&priv->lock);
  }
}

/* Check for Require headers. Returns TRUE if there are no Require headers,
 * otherwise lets the application decide which headers are supported.
 * By default all headers are unsupported.
 * If there are unsupported options, FALSE will be returned together with
 * a newly-allocated string of (comma-separated) unsupported options in
 * the unsupported_reqs variable.
 *
 * There may be multiple Require headers, but we must send one single
 * Unsupported header with all the unsupported options as response. If
 * an incoming Require header contained a comma-separated list of options
 * GstRtspConnection will already have split that list up into multiple
 * headers.
 */
static gboolean
check_request_requirements (GstRTSPContext * ctx, gchar ** unsupported_reqs)
{
  GstRTSPResult res;
  GPtrArray *arr = NULL;
  GstRTSPMessage *msg = ctx->request;
  gchar *reqs = NULL;
  gint i;
  gchar *sig_result = NULL;
  gboolean result = TRUE;

  i = 0;
  do {
    res = gst_rtsp_message_get_header (msg, GST_RTSP_HDR_REQUIRE, &reqs, i++);

    if (res == GST_RTSP_ENOTIMPL)
      break;

    if (arr == NULL)
      arr = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);

    g_ptr_array_add (arr, g_strdup (reqs));
  }
  while (TRUE);

  /* if we don't have any Require headers at all, all is fine */
  if (i == 1)
    return TRUE;

  /* otherwise we've now processed at all the Require headers */
  g_ptr_array_add (arr, NULL);

  g_signal_emit (ctx->client,
      gst_rtsp_client_signals[SIGNAL_CHECK_REQUIREMENTS], 0, ctx,
      (gchar **) arr->pdata, &sig_result);

  if (sig_result == NULL) {
    /* no supported options, just report all of the required ones as
     * unsupported */
    *unsupported_reqs = g_strjoinv (", ", (gchar **) arr->pdata);
    result = FALSE;
    goto done;
  }

  if (strlen (sig_result) == 0)
    g_free (sig_result);
  else {
    *unsupported_reqs = sig_result;
    result = FALSE;
  }

done:
  g_ptr_array_unref (arr);
  return result;
}

static void
handle_request (GstRTSPClient * client, GstRTSPMessage * request)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPMethod method;
  const gchar *uristr;
  GstRTSPUrl *uri = NULL;
  GstRTSPVersion version;
  GstRTSPResult res;
  GstRTSPSession *session = NULL;
  GstRTSPContext sctx = { NULL }, *ctx;
  GstRTSPMessage response = { 0 };
  gchar *unsupported_reqs = NULL;
  gchar *sessid = NULL, *pipelined_request_id = NULL;

  if (!(ctx = gst_rtsp_context_get_current ())) {
    ctx = &sctx;
    ctx->auth = priv->auth;
    gst_rtsp_context_push_current (ctx);
  }

  ctx->conn = priv->connection;
  ctx->client = client;
  ctx->request = request;
  ctx->response = &response;

  if (gst_debug_category_get_threshold (rtsp_client_debug) >= GST_LEVEL_LOG) {
    gst_rtsp_message_dump (request);
  }

  gst_rtsp_message_parse_request (request, &method, &uristr, &version);

  GST_INFO ("client %p: received a request %s %s %s", client,
      gst_rtsp_method_as_text (method), uristr,
      gst_rtsp_version_as_text (version));

  /* we can only handle 1.0 requests */
  if (version != GST_RTSP_VERSION_1_0 && version != GST_RTSP_VERSION_2_0)
    goto not_supported;

  ctx->method = method;

  /* we always try to parse the url first */
  if (strcmp (uristr, "*") == 0) {
    /* special case where we have * as uri, keep uri = NULL */
  } else if (gst_rtsp_url_parse (uristr, &uri) != GST_RTSP_OK) {
    /* check if the uristr is an absolute path <=> scheme and host information
     * is missing */
    gchar *scheme;

    scheme = g_uri_parse_scheme (uristr);
    if (scheme == NULL && g_str_has_prefix (uristr, "/")) {
      gchar *absolute_uristr = NULL;

      GST_WARNING_OBJECT (client, "request doesn't contain absolute url");
      if (priv->server_ip == NULL) {
        GST_WARNING_OBJECT (client, "host information missing");
        goto bad_request;
      }

      absolute_uristr =
          g_strdup_printf ("rtsp://%s%s", priv->server_ip, uristr);

      GST_DEBUG_OBJECT (client, "absolute url: %s", absolute_uristr);
      if (gst_rtsp_url_parse (absolute_uristr, &uri) != GST_RTSP_OK) {
        g_free (absolute_uristr);
        goto bad_request;
      }
      g_free (absolute_uristr);
    } else {
      g_free (scheme);
      goto bad_request;
    }
  }

  /* get the session if there is any */
  res = gst_rtsp_message_get_header (request, GST_RTSP_HDR_PIPELINED_REQUESTS,
      &pipelined_request_id, 0);
  if (res == GST_RTSP_OK) {
    sessid = g_hash_table_lookup (client->priv->pipelined_requests,
        pipelined_request_id);

    if (!sessid)
      res = GST_RTSP_ERROR;
  }

  if (res != GST_RTSP_OK)
    res =
        gst_rtsp_message_get_header (request, GST_RTSP_HDR_SESSION, &sessid, 0);

  if (res == GST_RTSP_OK) {
    if (priv->session_pool == NULL)
      goto no_pool;

    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (priv->session_pool, sessid)))
      goto session_not_found;

    /* we add the session to the client list of watched sessions. When a session
     * disappears because it times out, we will be notified. If all sessions are
     * gone, we will close the connection */
    client_watch_session (client, session);
  }

  /* sanitize the uri */
  if (uri)
    sanitize_uri (uri);
  ctx->uri = uri;
  ctx->session = session;

  if (!gst_rtsp_auth_check (GST_RTSP_AUTH_CHECK_URL))
    goto not_authorized;

  /* handle any 'Require' headers */
  if (!check_request_requirements (ctx, &unsupported_reqs))
    goto unsupported_requirement;

  /* now see what is asked and dispatch to a dedicated handler */
  switch (method) {
    case GST_RTSP_OPTIONS:
      priv->version = version;
      handle_options_request (client, ctx, version);
      break;
    case GST_RTSP_DESCRIBE:
      handle_describe_request (client, ctx);
      break;
    case GST_RTSP_SETUP:
      handle_setup_request (client, ctx);
      break;
    case GST_RTSP_PLAY:
      handle_play_request (client, ctx);
      break;
    case GST_RTSP_PAUSE:
      handle_pause_request (client, ctx);
      break;
    case GST_RTSP_TEARDOWN:
      handle_teardown_request (client, ctx);
      break;
    case GST_RTSP_SET_PARAMETER:
      handle_set_param_request (client, ctx);
      break;
    case GST_RTSP_GET_PARAMETER:
      handle_get_param_request (client, ctx);
      break;
    case GST_RTSP_ANNOUNCE:
      if (version >= GST_RTSP_VERSION_2_0)
        goto invalid_command_for_version;
      handle_announce_request (client, ctx);
      break;
    case GST_RTSP_RECORD:
      if (version >= GST_RTSP_VERSION_2_0)
        goto invalid_command_for_version;
      handle_record_request (client, ctx);
      break;
    case GST_RTSP_REDIRECT:
      goto not_implemented;
    case GST_RTSP_INVALID:
    default:
      goto bad_request;
  }

done:
  if (ctx == &sctx)
    gst_rtsp_context_pop_current (ctx);
  if (session)
    g_object_unref (session);
  if (uri)
    gst_rtsp_url_free (uri);
  return;

  /* ERRORS */
not_supported:
  {
    GST_ERROR ("client %p: version %d not supported", client, version);
    send_generic_response (client, GST_RTSP_STS_RTSP_VERSION_NOT_SUPPORTED,
        ctx);
    goto done;
  }
invalid_command_for_version:
  {
    GST_ERROR ("client %p: invalid command for version", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    goto done;
  }
bad_request:
  {
    GST_ERROR ("client %p: bad request", client);
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    goto done;
  }
no_pool:
  {
    GST_ERROR ("client %p: no pool configured", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    goto done;
  }
session_not_found:
  {
    GST_ERROR ("client %p: session not found", client);
    send_generic_response (client, GST_RTSP_STS_SESSION_NOT_FOUND, ctx);
    goto done;
  }
not_authorized:
  {
    GST_ERROR ("client %p: not allowed", client);
    /* error reply is already sent */
    goto done;
  }
unsupported_requirement:
  {
    GST_ERROR ("client %p: Required option is not supported (%s)", client,
        unsupported_reqs);
    send_option_not_supported_response (client, ctx, unsupported_reqs);
    g_free (unsupported_reqs);
    goto done;
  }
not_implemented:
  {
    GST_ERROR ("client %p: method %d not implemented", client, method);
    send_generic_response (client, GST_RTSP_STS_NOT_IMPLEMENTED, ctx);
    goto done;
  }
}


static void
handle_response (GstRTSPClient * client, GstRTSPMessage * response)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPResult res;
  GstRTSPSession *session = NULL;
  GstRTSPContext sctx = { NULL }, *ctx;
  gchar *sessid;

  if (!(ctx = gst_rtsp_context_get_current ())) {
    ctx = &sctx;
    ctx->auth = priv->auth;
    gst_rtsp_context_push_current (ctx);
  }

  ctx->conn = priv->connection;
  ctx->client = client;
  ctx->request = NULL;
  ctx->uri = NULL;
  ctx->method = GST_RTSP_INVALID;
  ctx->response = response;

  if (gst_debug_category_get_threshold (rtsp_client_debug) >= GST_LEVEL_LOG) {
    gst_rtsp_message_dump (response);
  }

  GST_INFO ("client %p: received a response", client);

  /* get the session if there is any */
  res =
      gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION, &sessid, 0);
  if (res == GST_RTSP_OK) {
    if (priv->session_pool == NULL)
      goto no_pool;

    /* we had a session in the request, find it again */
    if (!(session = gst_rtsp_session_pool_find (priv->session_pool, sessid)))
      goto session_not_found;

    /* we add the session to the client list of watched sessions. When a session
     * disappears because it times out, we will be notified. If all sessions are
     * gone, we will close the connection */
    client_watch_session (client, session);
  }

  ctx->session = session;

  g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_HANDLE_RESPONSE],
      0, ctx);

done:
  if (ctx == &sctx)
    gst_rtsp_context_pop_current (ctx);
  if (session)
    g_object_unref (session);
  return;

no_pool:
  {
    GST_ERROR ("client %p: no pool configured", client);
    goto done;
  }
session_not_found:
  {
    GST_ERROR ("client %p: session not found", client);
    goto done;
  }
}

static void
handle_data (GstRTSPClient * client, GstRTSPMessage * message)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPResult res;
  guint8 channel;
  guint8 *data;
  guint size;
  GstBuffer *buffer;
  GstRTSPStreamTransport *trans;

  /* find the stream for this message */
  res = gst_rtsp_message_parse_data (message, &channel);
  if (res != GST_RTSP_OK)
    return;

  gst_rtsp_message_get_body (message, &data, &size);
  if (size < 2)
    goto invalid_length;

  gst_rtsp_message_steal_body (message, &data, &size);

  /* Strip trailing \0 (which GstRTSPConnection adds) */
  --size;

  buffer = gst_buffer_new_wrapped (data, size);

  trans =
      g_hash_table_lookup (priv->transports, GINT_TO_POINTER ((gint) channel));
  if (trans) {
    GSocketAddress *addr;

    /* Only create the socket address once for the transport, we don't really
     * want to do that for every single packet.
     *
     * The netaddress meta is later used by the RTP stack to know where
     * packets came from and allows us to match it again to a stream transport
     *
     * In theory we could use the remote socket address of the RTSP connection
     * here, but this would fail with a custom configure_client_transport()
     * implementation.
     */
    if (!(addr =
            g_object_get_data (G_OBJECT (trans), "rtsp-client.remote-addr"))) {
      const GstRTSPTransport *tr;
      GInetAddress *iaddr;

      tr = gst_rtsp_stream_transport_get_transport (trans);
      iaddr = g_inet_address_new_from_string (tr->destination);
      if (iaddr) {
        addr = g_inet_socket_address_new (iaddr, tr->client_port.min);
        g_object_unref (iaddr);
        g_object_set_data_full (G_OBJECT (trans), "rtsp-client.remote-addr",
            addr, (GDestroyNotify) g_object_unref);
      }
    }

    if (addr) {
      gst_buffer_add_net_address_meta (buffer, addr);
    }

    /* dispatch to the stream based on the channel number */
    GST_LOG_OBJECT (client, "%u bytes of data on channel %u", size, channel);
    gst_rtsp_stream_transport_recv_data (trans, channel, buffer);
  } else {
    GST_DEBUG_OBJECT (client, "received %u bytes of data for "
        "unknown channel %u", size, channel);
    gst_buffer_unref (buffer);
  }

  return;

/* ERRORS */
invalid_length:
  {
    GST_DEBUG ("client %p: Short message received, ignoring", client);
    return;
  }
}

/**
 * gst_rtsp_client_set_session_pool:
 * @client: a #GstRTSPClient
 * @pool: (transfer none) (nullable): a #GstRTSPSessionPool
 *
 * Set @pool as the sessionpool for @client which it will use to find
 * or allocate sessions. the sessionpool is usually inherited from the server
 * that created the client but can be overridden later.
 */
void
gst_rtsp_client_set_session_pool (GstRTSPClient * client,
    GstRTSPSessionPool * pool)
{
  GstRTSPSessionPool *old;
  GstRTSPClientPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  if (pool)
    g_object_ref (pool);

  g_mutex_lock (&priv->lock);
  old = priv->session_pool;
  priv->session_pool = pool;

  if (priv->session_removed_id) {
    g_signal_handler_disconnect (old, priv->session_removed_id);
    priv->session_removed_id = 0;
  }
  g_mutex_unlock (&priv->lock);

  /* FIXME, should remove all sessions from the old pool for this client */
  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_client_get_session_pool:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPSessionPool object that @client uses to manage its sessions.
 *
 * Returns: (transfer full) (nullable): a #GstRTSPSessionPool, unref after usage.
 */
GstRTSPSessionPool *
gst_rtsp_client_get_session_pool (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv;
  GstRTSPSessionPool *result;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  priv = client->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->session_pool))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_client_set_mount_points:
 * @client: a #GstRTSPClient
 * @mounts: (transfer none) (nullable): a #GstRTSPMountPoints
 *
 * Set @mounts as the mount points for @client which it will use to map urls
 * to media streams. These mount points are usually inherited from the server that
 * created the client but can be overriden later.
 */
void
gst_rtsp_client_set_mount_points (GstRTSPClient * client,
    GstRTSPMountPoints * mounts)
{
  GstRTSPClientPrivate *priv;
  GstRTSPMountPoints *old;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  if (mounts)
    g_object_ref (mounts);

  g_mutex_lock (&priv->lock);
  old = priv->mount_points;
  priv->mount_points = mounts;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_client_get_mount_points:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPMountPoints object that @client uses to manage its sessions.
 *
 * Returns: (transfer full) (nullable): a #GstRTSPMountPoints, unref after usage.
 */
GstRTSPMountPoints *
gst_rtsp_client_get_mount_points (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv;
  GstRTSPMountPoints *result;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  priv = client->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->mount_points))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_client_set_content_length_limit:
 * @client: a #GstRTSPClient
 * @limit: Content-Length limit
 *
 * Configure @client to use the specified Content-Length limit.
 *
 * Define an appropriate request size limit and reject requests exceeding the
 * limit with response status 413 Request Entity Too Large
 *
 * Since: 1.18
 */
void
gst_rtsp_client_set_content_length_limit (GstRTSPClient * client, guint limit)
{
  GstRTSPClientPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;
  g_mutex_lock (&priv->lock);
  priv->content_length_limit = limit;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_client_get_content_length_limit:
 * @client: a #GstRTSPClient
 *
 * Get the Content-Length limit of @client.
 *
 * Returns: the Content-Length limit.
 *
 * Since: 1.18
 */
guint
gst_rtsp_client_get_content_length_limit (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv;
  glong content_length_limit;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), -1);
  priv = client->priv;

  g_mutex_lock (&priv->lock);
  content_length_limit = priv->content_length_limit;
  g_mutex_unlock (&priv->lock);

  return content_length_limit;
}

/**
 * gst_rtsp_client_set_auth:
 * @client: a #GstRTSPClient
 * @auth: (transfer none) (nullable): a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @client.
 */
void
gst_rtsp_client_set_auth (GstRTSPClient * client, GstRTSPAuth * auth)
{
  GstRTSPClientPrivate *priv;
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  if (auth)
    g_object_ref (auth);

  g_mutex_lock (&priv->lock);
  old = priv->auth;
  priv->auth = auth;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_client_get_auth:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPAuth used as the authentication manager of @client.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPAuth of @client.
 * g_object_unref() after usage.
 */
GstRTSPAuth *
gst_rtsp_client_get_auth (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv;
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  priv = client->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->auth))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_client_set_thread_pool:
 * @client: a #GstRTSPClient
 * @pool: (transfer none) (nullable): a #GstRTSPThreadPool
 *
 * configure @pool to be used as the thread pool of @client.
 */
void
gst_rtsp_client_set_thread_pool (GstRTSPClient * client,
    GstRTSPThreadPool * pool)
{
  GstRTSPClientPrivate *priv;
  GstRTSPThreadPool *old;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  if (pool)
    g_object_ref (pool);

  g_mutex_lock (&priv->lock);
  old = priv->thread_pool;
  priv->thread_pool = pool;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_client_get_thread_pool:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPThreadPool used as the thread pool of @client.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPThreadPool of @client. g_object_unref() after
 * usage.
 */
GstRTSPThreadPool *
gst_rtsp_client_get_thread_pool (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv;
  GstRTSPThreadPool *result;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  priv = client->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->thread_pool))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_client_set_connection:
 * @client: a #GstRTSPClient
 * @conn: (transfer full): a #GstRTSPConnection
 *
 * Set the #GstRTSPConnection of @client. This function takes ownership of
 * @conn.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_client_set_connection (GstRTSPClient * client,
    GstRTSPConnection * conn)
{
  GstRTSPClientPrivate *priv;
  GSocket *read_socket;
  GSocketAddress *address;
  GstRTSPUrl *url;
  GError *error = NULL;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), FALSE);
  g_return_val_if_fail (conn != NULL, FALSE);

  priv = client->priv;

  gst_rtsp_connection_set_content_length_limit (conn,
      priv->content_length_limit);
  read_socket = gst_rtsp_connection_get_read_socket (conn);

  if (!(address = g_socket_get_local_address (read_socket, &error)))
    goto no_address;

  g_free (priv->server_ip);
  /* keep the original ip that the client connected to */
  if (G_IS_INET_SOCKET_ADDRESS (address)) {
    GInetAddress *iaddr;

    iaddr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address));

    /* socket might be ipv6 but adress still ipv4 */
    priv->is_ipv6 = g_inet_address_get_family (iaddr) == G_SOCKET_FAMILY_IPV6;
    priv->server_ip = g_inet_address_to_string (iaddr);
    g_object_unref (address);
  } else {
    priv->is_ipv6 = g_socket_get_family (read_socket) == G_SOCKET_FAMILY_IPV6;
    priv->server_ip = g_strdup ("unknown");
  }

  GST_INFO ("client %p connected to server ip %s, ipv6 = %d", client,
      priv->server_ip, priv->is_ipv6);

  url = gst_rtsp_connection_get_url (conn);
  GST_INFO ("added new client %p ip %s:%d", client, url->host, url->port);

  priv->connection = conn;

  return TRUE;

  /* ERRORS */
no_address:
  {
    GST_ERROR ("could not get local address %s", error->message);
    g_error_free (error);
    return FALSE;
  }
}

/**
 * gst_rtsp_client_get_connection:
 * @client: a #GstRTSPClient
 *
 * Get the #GstRTSPConnection of @client.
 *
 * Returns: (transfer none) (nullable): the #GstRTSPConnection of @client.
 * The connection object returned remains valid until the client is freed.
 */
GstRTSPConnection *
gst_rtsp_client_get_connection (GstRTSPClient * client)
{
  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  return client->priv->connection;
}

/**
 * gst_rtsp_client_set_send_func:
 * @client: a #GstRTSPClient
 * @func: (scope notified): a #GstRTSPClientSendFunc
 * @user_data: (closure): user data passed to @func
 * @notify: (allow-none): called when @user_data is no longer in use
 *
 * Set @func as the callback that will be called when a new message needs to be
 * sent to the client. @user_data is passed to @func and @notify is called when
 * @user_data is no longer in use.
 *
 * By default, the client will send the messages on the #GstRTSPConnection that
 * was configured with gst_rtsp_client_attach() was called.
 *
 * It is only allowed to set either a `send_func` or a `send_messages_func`
 * but not both at the same time.
 */
void
gst_rtsp_client_set_send_func (GstRTSPClient * client,
    GstRTSPClientSendFunc func, gpointer user_data, GDestroyNotify notify)
{
  GstRTSPClientPrivate *priv;
  GDestroyNotify old_notify;
  gpointer old_data;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  g_mutex_lock (&priv->send_lock);
  g_assert (func == NULL || priv->send_messages_func == NULL);
  priv->send_func = func;
  old_notify = priv->send_notify;
  old_data = priv->send_data;
  priv->send_notify = notify;
  priv->send_data = user_data;
  g_mutex_unlock (&priv->send_lock);

  if (old_notify)
    old_notify (old_data);
}

/**
 * gst_rtsp_client_set_send_messages_func:
 * @client: a #GstRTSPClient
 * @func: (scope notified): a #GstRTSPClientSendMessagesFunc
 * @user_data: (closure): user data passed to @func
 * @notify: (allow-none): called when @user_data is no longer in use
 *
 * Set @func as the callback that will be called when new messages needs to be
 * sent to the client. @user_data is passed to @func and @notify is called when
 * @user_data is no longer in use.
 *
 * By default, the client will send the messages on the #GstRTSPConnection that
 * was configured with gst_rtsp_client_attach() was called.
 *
 * It is only allowed to set either a `send_func` or a `send_messages_func`
 * but not both at the same time.
 *
 * Since: 1.16
 */
void
gst_rtsp_client_set_send_messages_func (GstRTSPClient * client,
    GstRTSPClientSendMessagesFunc func, gpointer user_data,
    GDestroyNotify notify)
{
  GstRTSPClientPrivate *priv;
  GDestroyNotify old_notify;
  gpointer old_data;

  g_return_if_fail (GST_IS_RTSP_CLIENT (client));

  priv = client->priv;

  g_mutex_lock (&priv->send_lock);
  g_assert (func == NULL || priv->send_func == NULL);
  priv->send_messages_func = func;
  old_notify = priv->send_messages_notify;
  old_data = priv->send_messages_data;
  priv->send_messages_notify = notify;
  priv->send_messages_data = user_data;
  g_mutex_unlock (&priv->send_lock);

  if (old_notify)
    old_notify (old_data);
}

/**
 * gst_rtsp_client_handle_message:
 * @client: a #GstRTSPClient
 * @message: (transfer none): an #GstRTSPMessage
 *
 * Let the client handle @message.
 *
 * Returns: a #GstRTSPResult.
 */
GstRTSPResult
gst_rtsp_client_handle_message (GstRTSPClient * client,
    GstRTSPMessage * message)
{
  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  switch (message->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      handle_request (client, message);
      break;
    case GST_RTSP_MESSAGE_RESPONSE:
      handle_response (client, message);
      break;
    case GST_RTSP_MESSAGE_DATA:
      handle_data (client, message);
      break;
    default:
      break;
  }
  return GST_RTSP_OK;
}

/**
 * gst_rtsp_client_send_message:
 * @client: a #GstRTSPClient
 * @session: (allow-none) (transfer none): a #GstRTSPSession to send
 *   the message to or %NULL
 * @message: (transfer none): The #GstRTSPMessage to send
 *
 * Send a message message to the remote end. @message must be a
 * #GST_RTSP_MESSAGE_REQUEST or a #GST_RTSP_MESSAGE_RESPONSE.
 */
GstRTSPResult
gst_rtsp_client_send_message (GstRTSPClient * client, GstRTSPSession * session,
    GstRTSPMessage * message)
{
  GstRTSPContext sctx = { NULL }
  , *ctx;
  GstRTSPClientPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message->type == GST_RTSP_MESSAGE_REQUEST ||
      message->type == GST_RTSP_MESSAGE_RESPONSE, GST_RTSP_EINVAL);

  priv = client->priv;

  if (!(ctx = gst_rtsp_context_get_current ())) {
    ctx = &sctx;
    ctx->auth = priv->auth;
    gst_rtsp_context_push_current (ctx);
  }

  ctx->conn = priv->connection;
  ctx->client = client;
  ctx->session = session;

  send_message (client, ctx, message, FALSE);

  if (ctx == &sctx)
    gst_rtsp_context_pop_current (ctx);

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_client_get_stream_transport:
 *
 * This is useful when providing a send function through
 * gst_rtsp_client_set_send_func() when doing RTSP over TCP:
 * the send function must call gst_rtsp_stream_transport_message_sent ()
 * on the appropriate transport when data has been received for streaming
 * to continue.
 *
 * Returns: (transfer none) (nullable): the #GstRTSPStreamTransport associated with @channel.
 *
 * Since: 1.18
 */
GstRTSPStreamTransport *
gst_rtsp_client_get_stream_transport (GstRTSPClient * self, guint8 channel)
{
  return g_hash_table_lookup (self->priv->transports,
      GINT_TO_POINTER ((gint) channel));
}

static gboolean
do_send_messages (GstRTSPClient * client, GstRTSPMessage * messages,
    guint n_messages, gboolean close, gpointer user_data)
{
  GstRTSPClientPrivate *priv = client->priv;
  guint id = 0;
  GstRTSPResult ret;
  guint i;

  /* send the message */
  if (close)
    GST_INFO ("client %p: sending close message", client);

  ret = gst_rtsp_watch_send_messages (priv->watch, messages, n_messages, &id);
  if (ret != GST_RTSP_OK)
    goto error;

  for (i = 0; i < n_messages; i++) {
    if (gst_rtsp_message_get_type (&messages[i]) == GST_RTSP_MESSAGE_DATA) {
      guint8 channel = 0;
      GstRTSPResult r;

      /* We assume that all data messages in the list are for the
       * same channel */
      r = gst_rtsp_message_parse_data (&messages[i], &channel);
      if (r != GST_RTSP_OK) {
        ret = r;
        goto error;
      }

      /* check if the message has been queued for transmission in watch */
      if (id) {
        /* store the seq number so we can wait until it has been sent */
        GST_DEBUG_OBJECT (client, "wait for message %d, channel %d", id,
            channel);
        set_data_seq (client, channel, id);
      } else {
        GstRTSPStreamTransport *trans;

        trans =
            g_hash_table_lookup (priv->transports,
            GINT_TO_POINTER ((gint) channel));
        if (trans) {
          GST_DEBUG_OBJECT (client, "emit 'message-sent' signal");
          g_mutex_unlock (&priv->send_lock);
          gst_rtsp_stream_transport_message_sent (trans);
          g_mutex_lock (&priv->send_lock);
        }
      }
      break;
    }
  }

  return ret == GST_RTSP_OK;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (client, "got error %d", ret);
    return FALSE;
  }
}

static GstRTSPResult
message_received (GstRTSPWatch * watch, GstRTSPMessage * message,
    gpointer user_data)
{
  return gst_rtsp_client_handle_message (GST_RTSP_CLIENT (user_data), message);
}

static GstRTSPResult
message_sent (GstRTSPWatch * watch, guint cseq, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPStreamTransport *trans = NULL;
  guint8 channel = 0;

  g_mutex_lock (&priv->send_lock);

  if (get_data_channel (client, cseq, &channel)) {
    trans = g_hash_table_lookup (priv->transports, GINT_TO_POINTER (channel));
    set_data_seq (client, channel, 0);
  }
  g_mutex_unlock (&priv->send_lock);

  if (trans) {
    GST_DEBUG_OBJECT (client, "emit 'message-sent' signal");
    gst_rtsp_stream_transport_message_sent (trans);
  }

  return GST_RTSP_OK;
}

static GstRTSPResult
closed (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  GstRTSPClientPrivate *priv = client->priv;
  const gchar *tunnelid;

  GST_INFO ("client %p: connection closed", client);

  if ((tunnelid = gst_rtsp_connection_get_tunnelid (priv->connection))) {
    g_mutex_lock (&tunnels_lock);
    /* remove from tunnelids */
    g_hash_table_remove (tunnels, tunnelid);
    g_mutex_unlock (&tunnels_lock);
  }

  gst_rtsp_watch_set_flushing (watch, TRUE);
  g_mutex_lock (&priv->watch_lock);
  gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
  gst_rtsp_client_set_send_messages_func (client, NULL, NULL, NULL);
  g_mutex_unlock (&priv->watch_lock);

  return GST_RTSP_OK;
}

static GstRTSPResult
error (GstRTSPWatch * watch, GstRTSPResult result, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  gchar *str;

  str = gst_rtsp_strresult (result);
  GST_INFO ("client %p: received an error %s", client, str);
  g_free (str);

  return GST_RTSP_OK;
}

static GstRTSPResult
error_full (GstRTSPWatch * watch, GstRTSPResult result,
    GstRTSPMessage * message, guint id, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  gchar *str;
  GstRTSPContext sctx = { NULL }, *ctx;
  GstRTSPClientPrivate *priv;
  GstRTSPMessage response = { 0 };
  priv = client->priv;

  if (!(ctx = gst_rtsp_context_get_current ())) {
    ctx = &sctx;
    ctx->auth = priv->auth;
    gst_rtsp_context_push_current (ctx);
  }

  ctx->conn = priv->connection;
  ctx->client = client;
  ctx->request = message;
  ctx->method = GST_RTSP_INVALID;
  ctx->response = &response;

  /* only return error response if it is a request */
  if (!message || message->type != GST_RTSP_MESSAGE_REQUEST)
    goto done;

  if (result == GST_RTSP_ENOMEM) {
    send_generic_response (client, GST_RTSP_STS_REQUEST_ENTITY_TOO_LARGE, ctx);
    goto done;
  }
  if (result == GST_RTSP_EPARSE) {
    send_generic_response (client, GST_RTSP_STS_BAD_REQUEST, ctx);
    goto done;
  }

done:
  if (ctx == &sctx)
    gst_rtsp_context_pop_current (ctx);
  str = gst_rtsp_strresult (result);
  GST_INFO
      ("client %p: error when handling message %p with id %d: %s",
      client, message, id, str);
  g_free (str);

  return GST_RTSP_OK;
}

static gboolean
remember_tunnel (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  const gchar *tunnelid;

  /* store client in the pending tunnels */
  tunnelid = gst_rtsp_connection_get_tunnelid (priv->connection);
  if (tunnelid == NULL)
    goto no_tunnelid;

  GST_INFO ("client %p: inserting tunnel session %s", client, tunnelid);

  /* we can't have two clients connecting with the same tunnelid */
  g_mutex_lock (&tunnels_lock);
  if (g_hash_table_lookup (tunnels, tunnelid))
    goto tunnel_existed;

  g_hash_table_insert (tunnels, g_strdup (tunnelid), g_object_ref (client));
  g_mutex_unlock (&tunnels_lock);

  return TRUE;

  /* ERRORS */
no_tunnelid:
  {
    GST_ERROR ("client %p: no tunnelid provided", client);
    return FALSE;
  }
tunnel_existed:
  {
    g_mutex_unlock (&tunnels_lock);
    GST_ERROR ("client %p: tunnel session %s already existed", client,
        tunnelid);
    return FALSE;
  }
}

static GstRTSPResult
tunnel_lost (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  GstRTSPClientPrivate *priv = client->priv;

  GST_WARNING ("client %p: tunnel lost (connection %p)", client,
      priv->connection);

  /* ignore error, it'll only be a problem when the client does a POST again */
  remember_tunnel (client);

  return GST_RTSP_OK;
}

static GstRTSPStatusCode
handle_tunnel (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  GstRTSPClient *oclient;
  GstRTSPClientPrivate *opriv;
  const gchar *tunnelid;

  tunnelid = gst_rtsp_connection_get_tunnelid (priv->connection);
  if (tunnelid == NULL)
    goto no_tunnelid;

  /* check for previous tunnel */
  g_mutex_lock (&tunnels_lock);
  oclient = g_hash_table_lookup (tunnels, tunnelid);

  if (oclient == NULL) {
    /* no previous tunnel, remember tunnel */
    g_hash_table_insert (tunnels, g_strdup (tunnelid), g_object_ref (client));
    g_mutex_unlock (&tunnels_lock);

    GST_INFO ("client %p: no previous tunnel found, remembering tunnel (%p)",
        client, priv->connection);
  } else {
    /* merge both tunnels into the first client */
    /* remove the old client from the table. ref before because removing it will
     * remove the ref to it. */
    g_object_ref (oclient);
    g_hash_table_remove (tunnels, tunnelid);
    g_mutex_unlock (&tunnels_lock);

    opriv = oclient->priv;

    g_mutex_lock (&opriv->watch_lock);
    if (opriv->watch == NULL)
      goto tunnel_closed;
    if (opriv->tstate == priv->tstate)
      goto tunnel_duplicate_id;

    GST_INFO ("client %p: found previous tunnel %p (old %p, new %p)", client,
        oclient, opriv->connection, priv->connection);

    gst_rtsp_connection_do_tunnel (opriv->connection, priv->connection);
    gst_rtsp_watch_reset (priv->watch);
    gst_rtsp_watch_reset (opriv->watch);
    g_mutex_unlock (&opriv->watch_lock);
    g_object_unref (oclient);

    /* the old client owns the tunnel now, the new one will be freed */
    g_source_destroy ((GSource *) priv->watch);
    priv->watch = NULL;
    gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
    gst_rtsp_client_set_send_messages_func (client, NULL, NULL, NULL);
    rtsp_ctrl_timeout_remove (client);
  }

  return GST_RTSP_STS_OK;

  /* ERRORS */
no_tunnelid:
  {
    GST_ERROR ("client %p: no tunnelid provided", client);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
tunnel_closed:
  {
    GST_ERROR ("client %p: tunnel session %s was closed", client, tunnelid);
    g_mutex_unlock (&opriv->watch_lock);
    g_object_unref (oclient);
    return GST_RTSP_STS_SERVICE_UNAVAILABLE;
  }
tunnel_duplicate_id:
  {
    GST_ERROR ("client %p: tunnel session %s was duplicate", client, tunnelid);
    g_mutex_unlock (&opriv->watch_lock);
    g_object_unref (oclient);
    return GST_RTSP_STS_BAD_REQUEST;
  }
}

static GstRTSPStatusCode
tunnel_get (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);

  GST_INFO ("client %p: tunnel get (connection %p)", client,
      client->priv->connection);

  g_mutex_lock (&client->priv->lock);
  client->priv->tstate = TUNNEL_STATE_GET;
  g_mutex_unlock (&client->priv->lock);

  return handle_tunnel (client);
}

static GstRTSPResult
tunnel_post (GstRTSPWatch * watch, gpointer user_data)
{
  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);

  GST_INFO ("client %p: tunnel post (connection %p)", client,
      client->priv->connection);

  g_mutex_lock (&client->priv->lock);
  client->priv->tstate = TUNNEL_STATE_POST;
  g_mutex_unlock (&client->priv->lock);

  if (handle_tunnel (client) != GST_RTSP_STS_OK)
    return GST_RTSP_ERROR;

  return GST_RTSP_OK;
}

static GstRTSPResult
tunnel_http_response (GstRTSPWatch * watch, GstRTSPMessage * request,
    GstRTSPMessage * response, gpointer user_data)
{
  GstRTSPClientClass *klass;

  GstRTSPClient *client = GST_RTSP_CLIENT (user_data);
  klass = GST_RTSP_CLIENT_GET_CLASS (client);

  if (klass->tunnel_http_response) {
    klass->tunnel_http_response (client, request, response);
  }

  return GST_RTSP_OK;
}

static GstRTSPWatchFuncs watch_funcs = {
  message_received,
  message_sent,
  closed,
  error,
  tunnel_get,
  tunnel_post,
  error_full,
  tunnel_lost,
  tunnel_http_response
};

static void
client_watch_notify (GstRTSPClient * client)
{
  GstRTSPClientPrivate *priv = client->priv;
  gboolean closed = TRUE;

  GST_INFO ("client %p: watch destroyed", client);
  priv->watch = NULL;
  /* remove all sessions if the media says so and so drop the extra client ref */
  gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
  gst_rtsp_client_set_send_messages_func (client, NULL, NULL, NULL);
  rtsp_ctrl_timeout_remove (client);
  gst_rtsp_client_session_filter (client, cleanup_session, &closed);

  if (closed)
    g_signal_emit (client, gst_rtsp_client_signals[SIGNAL_CLOSED], 0, NULL);
  g_object_unref (client);
}

/**
 * gst_rtsp_client_attach:
 * @client: a #GstRTSPClient
 * @context: (allow-none): a #GMainContext
 *
 * Attaches @client to @context. When the mainloop for @context is run, the
 * client will be dispatched. When @context is %NULL, the default context will be
 * used).
 *
 * This function should be called when the client properties and urls are fully
 * configured and the client is ready to start.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext.
 */
guint
gst_rtsp_client_attach (GstRTSPClient * client, GMainContext * context)
{
  GstRTSPClientPrivate *priv;
  GSource *timer_src;
  guint res;
  GWeakRef *client_weak_ref = g_new (GWeakRef, 1);

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), 0);
  priv = client->priv;
  g_return_val_if_fail (priv->connection != NULL, 0);
  g_return_val_if_fail (priv->watch == NULL, 0);
  g_return_val_if_fail (priv->watch_context == NULL, 0);

  /* make sure noone will free the context before the watch is destroyed */
  priv->watch_context = g_main_context_ref (context);

  /* create watch for the connection and attach */
  priv->watch = gst_rtsp_watch_new (priv->connection, &watch_funcs,
      g_object_ref (client), (GDestroyNotify) client_watch_notify);
  gst_rtsp_client_set_send_func (client, NULL, NULL, NULL);
  gst_rtsp_client_set_send_messages_func (client, do_send_messages, priv->watch,
      (GDestroyNotify) gst_rtsp_watch_unref);

  gst_rtsp_watch_set_send_backlog (priv->watch, 0, WATCH_BACKLOG_SIZE);

  GST_INFO ("client %p: attaching to context %p", client, context);
  res = gst_rtsp_watch_attach (priv->watch, context);

  /* Setting up a timeout for the RTSP control channel until a session
   * is up where it is handling timeouts. */
  g_mutex_lock (&priv->lock);

  /* remove old timeout if any */
  rtsp_ctrl_timeout_remove_unlocked (client->priv);

  timer_src = g_timeout_source_new_seconds (RTSP_CTRL_CB_INTERVAL);
  g_weak_ref_init (client_weak_ref, client);
  g_source_set_callback (timer_src, rtsp_ctrl_timeout_cb, client_weak_ref,
      rtsp_ctrl_timeout_destroy_notify);
  g_source_attach (timer_src, priv->watch_context);
  priv->rtsp_ctrl_timeout = timer_src;
  GST_DEBUG ("rtsp control setting up session timeout %p.",
      priv->rtsp_ctrl_timeout);

  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_client_session_filter:
 * @client: a #GstRTSPClient
 * @func: (scope call) (allow-none): a callback
 * @user_data: user data passed to @func
 *
 * Call @func for each session managed by @client. The result value of @func
 * determines what happens to the session. @func will be called with @client
 * locked so no further actions on @client can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the session will be removed from
 * @client.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the session will remain in @client.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the session will remain in @client but
 * will also be added with an additional ref to the result #GList of this
 * function..
 *
 * When @func is %NULL, #GST_RTSP_FILTER_REF will be assumed for each session.
 *
 * Returns: (element-type GstRTSPSession) (transfer full): a #GList with all
 * sessions for which @func returned #GST_RTSP_FILTER_REF. After usage, each
 * element in the #GList should be unreffed before the list is freed.
 */
GList *
gst_rtsp_client_session_filter (GstRTSPClient * client,
    GstRTSPClientSessionFilterFunc func, gpointer user_data)
{
  GstRTSPClientPrivate *priv;
  GList *result, *walk, *next;
  GHashTable *visited;
  guint cookie;

  g_return_val_if_fail (GST_IS_RTSP_CLIENT (client), NULL);

  priv = client->priv;

  result = NULL;
  if (func)
    visited = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  g_mutex_lock (&priv->lock);
restart:
  cookie = priv->sessions_cookie;
  for (walk = priv->sessions; walk; walk = next) {
    GstRTSPSession *sess = walk->data;
    GstRTSPFilterResult res;
    gboolean changed;

    next = g_list_next (walk);

    if (func) {
      /* only visit each session once */
      if (g_hash_table_contains (visited, sess))
        continue;

      g_hash_table_add (visited, g_object_ref (sess));
      g_mutex_unlock (&priv->lock);

      res = func (client, sess, user_data);

      g_mutex_lock (&priv->lock);
    } else
      res = GST_RTSP_FILTER_REF;

    changed = (cookie != priv->sessions_cookie);

    switch (res) {
      case GST_RTSP_FILTER_REMOVE:
        /* stop watching the session and pretend it went away, if the list was
         * changed, we can't use the current list position, try to see if we
         * still have the session */
        client_unwatch_session (client, sess, changed ? NULL : walk);
        cookie = priv->sessions_cookie;
        break;
      case GST_RTSP_FILTER_REF:
        result = g_list_prepend (result, g_object_ref (sess));
        break;
      case GST_RTSP_FILTER_KEEP:
      default:
        break;
    }
    if (changed)
      goto restart;
  }
  g_mutex_unlock (&priv->lock);

  if (func)
    g_hash_table_unref (visited);

  return result;
}
