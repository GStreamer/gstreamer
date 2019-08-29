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
/**
 * SECTION:rtsp-server
 * @short_description: The main server object
 * @see_also: #GstRTSPClient, #GstRTSPThreadPool
 *
 * The server object is the object listening for connections on a port and
 * creating #GstRTSPClient objects to handle those connections.
 *
 * The server will listen on the address set with gst_rtsp_server_set_address()
 * and the port or service configured with gst_rtsp_server_set_service().
 * Use gst_rtsp_server_set_backlog() to configure the amount of pending requests
 * that the server will keep. By default the server listens on the current
 * network (0.0.0.0) and port 8554.
 *
 * The server will require an SSL connection when a TLS certificate has been
 * set in the auth object with gst_rtsp_auth_set_tls_certificate().
 *
 * To start the server, use gst_rtsp_server_attach() to attach it to a
 * #GMainContext. For more control, gst_rtsp_server_create_source() and
 * gst_rtsp_server_create_socket() can be used to get a #GSource and #GSocket
 * respectively.
 *
 * gst_rtsp_server_transfer_connection() can be used to transfer an existing
 * socket to the RTSP server, for example from an HTTP server.
 *
 * Once the server socket is attached to a mainloop, it will start accepting
 * connections. When a new connection is received, a new #GstRTSPClient object
 * is created to handle the connection. The new client will be configured with
 * the server #GstRTSPAuth, #GstRTSPMountPoints, #GstRTSPSessionPool and
 * #GstRTSPThreadPool.
 *
 * The server uses the configured #GstRTSPThreadPool object to handle the
 * remainder of the communication with this client.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "rtsp-context.h"
#include "rtsp-server-object.h"
#include "rtsp-client.h"

#define GST_RTSP_SERVER_GET_LOCK(server)  (&(GST_RTSP_SERVER_CAST(server)->priv->lock))
#define GST_RTSP_SERVER_LOCK(server)      (g_mutex_lock(GST_RTSP_SERVER_GET_LOCK(server)))
#define GST_RTSP_SERVER_UNLOCK(server)    (g_mutex_unlock(GST_RTSP_SERVER_GET_LOCK(server)))

struct _GstRTSPServerPrivate
{
  GMutex lock;                  /* protects everything in this struct */

  /* server information */
  gchar *address;
  gchar *service;
  gint backlog;

  GSocket *socket;

  /* sessions on this server */
  GstRTSPSessionPool *session_pool;

  /* mount points for this server */
  GstRTSPMountPoints *mount_points;

  /* request size limit */
  guint content_length_limit;

  /* authentication manager */
  GstRTSPAuth *auth;

  /* resource manager */
  GstRTSPThreadPool *thread_pool;

  /* the clients that are connected */
  GList *clients;
  guint clients_cookie;
};

#define DEFAULT_ADDRESS         "0.0.0.0"
#define DEFAULT_BOUND_PORT      -1
/* #define DEFAULT_ADDRESS         "::0" */
#define DEFAULT_SERVICE         "8554"
#define DEFAULT_BACKLOG         5

/* Define to use the SO_LINGER option so that the server sockets can be resused
 * sooner. Disabled for now because it is not very well implemented by various
 * OSes and it causes clients to fail to read the TEARDOWN response. */
#undef USE_SOLINGER

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_SERVICE,
  PROP_BOUND_PORT,
  PROP_BACKLOG,

  PROP_SESSION_POOL,
  PROP_MOUNT_POINTS,
  PROP_CONTENT_LENGTH_LIMIT,
  PROP_LAST
};

enum
{
  SIGNAL_CLIENT_CONNECTED,
  SIGNAL_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPServer, gst_rtsp_server, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (rtsp_server_debug);
#define GST_CAT_DEFAULT rtsp_server_debug

typedef struct _ClientContext ClientContext;

static guint gst_rtsp_server_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_finalize (GObject * object);

static GstRTSPClient *default_create_client (GstRTSPServer * server);

static void
gst_rtsp_server_class_init (GstRTSPServerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_server_get_property;
  gobject_class->set_property = gst_rtsp_server_set_property;
  gobject_class->finalize = gst_rtsp_server_finalize;

  /**
   * GstRTSPServer::address:
   *
   * The address of the server. This is the address where the server will
   * listen on.
   */
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "The address the server uses to listen on", DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::service:
   *
   * The service of the server. This is either a string with the service name or
   * a port number (as a string) the server will listen on.
   */
  g_object_class_install_property (gobject_class, PROP_SERVICE,
      g_param_spec_string ("service", "Service",
          "The service or port number the server uses to listen on",
          DEFAULT_SERVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::bound-port:
   *
   * The actual port the server is listening on. Can be used to retrieve the
   * port number when the server is started on port 0, which means bind to a
   * random port. Set to -1 if the server has not been bound yet.
   */
  g_object_class_install_property (gobject_class, PROP_BOUND_PORT,
      g_param_spec_int ("bound-port", "Bound port",
          "The port number the server is listening on",
          -1, G_MAXUINT16, DEFAULT_BOUND_PORT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::backlog:
   *
   * The backlog argument defines the maximum length to which the queue of
   * pending connections for the server may grow. If a connection request arrives
   * when the queue is full, the client may receive an error with an indication of
   * ECONNREFUSED or, if the underlying protocol supports retransmission, the
   * request may be ignored so that a later reattempt at connection succeeds.
   */
  g_object_class_install_property (gobject_class, PROP_BACKLOG,
      g_param_spec_int ("backlog", "Backlog",
          "The maximum length to which the queue "
          "of pending connections may grow", 0, G_MAXINT, DEFAULT_BACKLOG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::session-pool:
   *
   * The session pool of the server. By default each server has a separate
   * session pool but sessions can be shared between servers by setting the same
   * session pool on multiple servers.
   */
  g_object_class_install_property (gobject_class, PROP_SESSION_POOL,
      g_param_spec_object ("session-pool", "Session Pool",
          "The session pool to use for client session",
          GST_TYPE_RTSP_SESSION_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::mount-points:
   *
   * The mount points to use for this server. By default the server has no
   * mount points and thus cannot map urls to media streams.
   */
  g_object_class_install_property (gobject_class, PROP_MOUNT_POINTS,
      g_param_spec_object ("mount-points", "Mount Points",
          "The mount points to use for client session",
          GST_TYPE_RTSP_MOUNT_POINTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * RTSPServer::content-length-limit:
   *
   * Define an appropriate request size limit and reject requests exceeding the
   * limit.
   *
   * Since: 1.18
   */
  g_object_class_install_property (gobject_class, PROP_CONTENT_LENGTH_LIMIT,
      g_param_spec_uint ("content-length-limit", "Limitation of Content-Length",
          "Limitation of Content-Length",
          0, G_MAXUINT, G_MAXUINT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED] =
      g_signal_new ("client-connected", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPServerClass, client_connected),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_CLIENT);

  klass->create_client = default_create_client;

  GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "rtspserver", 0, "GstRTSPServer");
}

static void
gst_rtsp_server_init (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = gst_rtsp_server_get_instance_private (server);

  server->priv = priv;

  g_mutex_init (&priv->lock);
  priv->address = g_strdup (DEFAULT_ADDRESS);
  priv->service = g_strdup (DEFAULT_SERVICE);
  priv->socket = NULL;
  priv->backlog = DEFAULT_BACKLOG;
  priv->session_pool = gst_rtsp_session_pool_new ();
  priv->mount_points = gst_rtsp_mount_points_new ();
  priv->content_length_limit = G_MAXUINT;
  priv->thread_pool = gst_rtsp_thread_pool_new ();
}

static void
gst_rtsp_server_finalize (GObject * object)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "finalize server");

  g_free (priv->address);
  g_free (priv->service);

  if (priv->socket)
    g_object_unref (priv->socket);

  if (priv->session_pool)
    g_object_unref (priv->session_pool);
  if (priv->mount_points)
    g_object_unref (priv->mount_points);
  if (priv->thread_pool)
    g_object_unref (priv->thread_pool);

  if (priv->auth)
    g_object_unref (priv->auth);

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_server_parent_class)->finalize (object);
}

/**
 * gst_rtsp_server_new:
 *
 * Create a new #GstRTSPServer instance.
 *
 * Returns: (transfer full): a new #GstRTSPServer
 */
GstRTSPServer *
gst_rtsp_server_new (void)
{
  GstRTSPServer *result;

  result = g_object_new (GST_TYPE_RTSP_SERVER, NULL);

  return result;
}

/**
 * gst_rtsp_server_set_address:
 * @server: a #GstRTSPServer
 * @address: the address
 *
 * Configure @server to accept connections on the given address.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_address (GstRTSPServer * server, const gchar * address)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (address != NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  g_free (priv->address);
  priv->address = g_strdup (address);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_address:
 * @server: a #GstRTSPServer
 *
 * Get the address on which the server will accept connections.
 *
 * Returns: (transfer full) (nullable): the server address. g_free() after usage.
 */
gchar *
gst_rtsp_server_get_address (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (priv->address);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_get_bound_port:
 * @server: a #GstRTSPServer
 *
 * Get the port number where the server was bound to.
 *
 * Returns: the port number
 */
int
gst_rtsp_server_get_bound_port (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GSocketAddress *address;
  int result = -1;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), result);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if (priv->socket == NULL)
    goto out;

  address = g_socket_get_local_address (priv->socket, NULL);
  result = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (address));
  g_object_unref (address);

out:
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_service:
 * @server: a #GstRTSPServer
 * @service: the service
 *
 * Configure @server to accept connections on the given service.
 * @service should be a string containing the service name (see services(5)) or
 * a string containing a port number between 1 and 65535.
 *
 * When @service is set to "0", the server will listen on a random free
 * port. The actual used port can be retrieved with
 * gst_rtsp_server_get_bound_port().
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_service (GstRTSPServer * server, const gchar * service)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (service != NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  g_free (priv->service);
  priv->service = g_strdup (service);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_service:
 * @server: a #GstRTSPServer
 *
 * Get the service on which the server will accept connections.
 *
 * Returns: (transfer full) (nullable): the service. use g_free() after usage.
 */
gchar *
gst_rtsp_server_get_service (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (priv->service);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_backlog:
 * @server: a #GstRTSPServer
 * @backlog: the backlog
 *
 * configure the maximum amount of requests that may be queued for the
 * server.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_backlog (GstRTSPServer * server, gint backlog)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  priv->backlog = backlog;
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_backlog:
 * @server: a #GstRTSPServer
 *
 * The maximum amount of queued requests for the server.
 *
 * Returns: the server backlog.
 */
gint
gst_rtsp_server_get_backlog (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gint result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = priv->backlog;
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_session_pool:
 * @server: a #GstRTSPServer
 * @pool: (transfer none) (nullable): a #GstRTSPSessionPool
 *
 * configure @pool to be used as the session pool of @server.
 */
void
gst_rtsp_server_set_session_pool (GstRTSPServer * server,
    GstRTSPSessionPool * pool)
{
  GstRTSPServerPrivate *priv;
  GstRTSPSessionPool *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (pool)
    g_object_ref (pool);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->session_pool;
  priv->session_pool = pool;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_server_get_session_pool:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPSessionPool used as the session pool of @server.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPSessionPool used for sessions. g_object_unref() after
 * usage.
 */
GstRTSPSessionPool *
gst_rtsp_server_get_session_pool (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPSessionPool *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->session_pool))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_mount_points:
 * @server: a #GstRTSPServer
 * @mounts: (transfer none) (nullable): a #GstRTSPMountPoints
 *
 * configure @mounts to be used as the mount points of @server.
 */
void
gst_rtsp_server_set_mount_points (GstRTSPServer * server,
    GstRTSPMountPoints * mounts)
{
  GstRTSPServerPrivate *priv;
  GstRTSPMountPoints *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (mounts)
    g_object_ref (mounts);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->mount_points;
  priv->mount_points = mounts;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_server_get_mount_points:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPMountPoints used as the mount points of @server.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPMountPoints of @server. g_object_unref() after
 * usage.
 */
GstRTSPMountPoints *
gst_rtsp_server_get_mount_points (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPMountPoints *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->mount_points))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_content_length_limit
 * @server: a #GstRTSPServer
 * Configure @server to use the specified Content-Length limit.
 *
 * Define an appropriate request size limit and reject requests exceeding the
 * limit.
 *
 * Since: 1.18
 */
void
gst_rtsp_server_set_content_length_limit (GstRTSPServer * server, guint limit)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  priv->content_length_limit = limit;
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_content_length_limit:
 * @server: a #GstRTSPServer
 *
 * Get the Content-Length limit of @server.
 *
 * Returns: the Content-Length limit.
 *
 * Since: 1.18
 */
guint
gst_rtsp_server_get_content_length_limit (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), G_MAXUINT);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = priv->content_length_limit;
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_auth:
 * @server: a #GstRTSPServer
 * @auth: (transfer none) (nullable): a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @server.
 */
void
gst_rtsp_server_set_auth (GstRTSPServer * server, GstRTSPAuth * auth)
{
  GstRTSPServerPrivate *priv;
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (auth)
    g_object_ref (auth);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->auth;
  priv->auth = auth;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_server_get_auth:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPAuth used as the authentication manager of @server.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPAuth of @server. g_object_unref() after
 * usage.
 */
GstRTSPAuth *
gst_rtsp_server_get_auth (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->auth))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_thread_pool:
 * @server: a #GstRTSPServer
 * @pool: (transfer none) (nullable): a #GstRTSPThreadPool
 *
 * configure @pool to be used as the thread pool of @server.
 */
void
gst_rtsp_server_set_thread_pool (GstRTSPServer * server,
    GstRTSPThreadPool * pool)
{
  GstRTSPServerPrivate *priv;
  GstRTSPThreadPool *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (pool)
    g_object_ref (pool);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->thread_pool;
  priv->thread_pool = pool;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_server_get_thread_pool:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPThreadPool used as the thread pool of @server.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPThreadPool of @server. g_object_unref() after
 * usage.
 */
GstRTSPThreadPool *
gst_rtsp_server_get_thread_pool (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPThreadPool *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->thread_pool))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

static void
gst_rtsp_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      g_value_take_string (value, gst_rtsp_server_get_address (server));
      break;
    case PROP_SERVICE:
      g_value_take_string (value, gst_rtsp_server_get_service (server));
      break;
    case PROP_BOUND_PORT:
      g_value_set_int (value, gst_rtsp_server_get_bound_port (server));
      break;
    case PROP_BACKLOG:
      g_value_set_int (value, gst_rtsp_server_get_backlog (server));
      break;
    case PROP_SESSION_POOL:
      g_value_take_object (value, gst_rtsp_server_get_session_pool (server));
      break;
    case PROP_MOUNT_POINTS:
      g_value_take_object (value, gst_rtsp_server_get_mount_points (server));
      break;
    case PROP_CONTENT_LENGTH_LIMIT:
      g_value_set_uint (value,
          gst_rtsp_server_get_content_length_limit (server));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      gst_rtsp_server_set_address (server, g_value_get_string (value));
      break;
    case PROP_SERVICE:
      gst_rtsp_server_set_service (server, g_value_get_string (value));
      break;
    case PROP_BACKLOG:
      gst_rtsp_server_set_backlog (server, g_value_get_int (value));
      break;
    case PROP_SESSION_POOL:
      gst_rtsp_server_set_session_pool (server, g_value_get_object (value));
      break;
    case PROP_MOUNT_POINTS:
      gst_rtsp_server_set_mount_points (server, g_value_get_object (value));
      break;
    case PROP_CONTENT_LENGTH_LIMIT:
      gst_rtsp_server_set_content_length_limit (server,
          g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_server_create_socket:
 * @server: a #GstRTSPServer
 * @cancellable: (allow-none): a #GCancellable
 * @error: (out): a #GError
 *
 * Create a #GSocket for @server. The socket will listen on the
 * configured service.
 *
 * Returns: (transfer full): the #GSocket for @server or %NULL when an error
 * occurred.
 */
GSocket *
gst_rtsp_server_create_socket (GstRTSPServer * server,
    GCancellable * cancellable, GError ** error)
{
  GstRTSPServerPrivate *priv;
  GSocketConnectable *conn;
  GSocketAddressEnumerator *enumerator;
  GSocket *socket = NULL;
#ifdef USE_SOLINGER
  struct linger linger;
#endif
  GError *sock_error = NULL;
  GError *bind_error = NULL;
  guint16 port;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  GST_DEBUG_OBJECT (server, "getting address info of %s/%s", priv->address,
      priv->service);

  /* resolve the server IP address */
  port = atoi (priv->service);
  if (port != 0 || !strcmp (priv->service, "0"))
    conn = g_network_address_new (priv->address, port);
  else
    conn = g_network_service_new (priv->service, "tcp", priv->address);

  enumerator = g_socket_connectable_enumerate (conn);
  g_object_unref (conn);

  /* create server socket, we loop through all the addresses until we manage to
   * create a socket and bind. */
  while (TRUE) {
    GSocketAddress *sockaddr;

    sockaddr =
        g_socket_address_enumerator_next (enumerator, cancellable, error);
    if (!sockaddr) {
      if (!*error)
        GST_DEBUG_OBJECT (server, "no more addresses %s",
            *error ? (*error)->message : "");
      else
        GST_DEBUG_OBJECT (server, "failed to retrieve next address %s",
            (*error)->message);
      break;
    }

    /* only keep the first error */
    socket = g_socket_new (g_socket_address_get_family (sockaddr),
        G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
        sock_error ? NULL : &sock_error);

    if (socket == NULL) {
      GST_DEBUG_OBJECT (server, "failed to make socket (%s), try next",
          sock_error->message);
      g_object_unref (sockaddr);
      continue;
    }

    if (g_socket_bind (socket, sockaddr, TRUE, bind_error ? NULL : &bind_error)) {
      /* ask what port the socket has been bound to */
      if (port == 0 || !strcmp (priv->service, "0")) {
        GError *addr_error = NULL;

        g_object_unref (sockaddr);
        sockaddr = g_socket_get_local_address (socket, &addr_error);

        if (addr_error != NULL) {
          GST_DEBUG_OBJECT (server,
              "failed to get the local address of a bound socket %s",
              addr_error->message);
          g_clear_error (&addr_error);
          break;
        }
        port =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (sockaddr));

        if (port != 0) {
          g_free (priv->service);
          priv->service = g_strdup_printf ("%d", port);
        } else {
          GST_DEBUG_OBJECT (server, "failed to get the port of a bound socket");
        }
      }
      g_object_unref (sockaddr);
      break;
    }

    GST_DEBUG_OBJECT (server, "failed to bind socket (%s), try next",
        bind_error->message);
    g_object_unref (sockaddr);
    g_object_unref (socket);
    socket = NULL;
  }
  g_object_unref (enumerator);

  if (socket == NULL)
    goto no_socket;

  g_clear_error (&sock_error);
  g_clear_error (&bind_error);

  GST_DEBUG_OBJECT (server, "opened sending server socket");

  /* keep connection alive; avoids SIGPIPE during write */
  g_socket_set_keepalive (socket, TRUE);

#if 0
#ifdef USE_SOLINGER
  /* make sure socket is reset 5 seconds after close. This ensure that we can
   * reuse the socket quickly while still having a chance to send data to the
   * client. */
  linger.l_onoff = 1;
  linger.l_linger = 5;
  if (setsockopt (sockfd, SOL_SOCKET, SO_LINGER,
          (void *) &linger, sizeof (linger)) < 0)
    goto linger_failed;
#endif
#endif

  /* set the server socket to nonblocking */
  g_socket_set_blocking (socket, FALSE);

  /* set listen backlog */
  g_socket_set_listen_backlog (socket, priv->backlog);

  if (!g_socket_listen (socket, error))
    goto listen_failed;

  GST_DEBUG_OBJECT (server, "listening on server socket %p with queue of %d",
      socket, priv->backlog);

  GST_RTSP_SERVER_UNLOCK (server);

  return socket;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket");
    goto close_error;
  }
#if 0
#ifdef USE_SOLINGER
linger_failed:
  {
    GST_ERROR_OBJECT (server, "failed to no linger socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#endif
#endif
listen_failed:
  {
    GST_ERROR_OBJECT (server, "failed to listen on socket: %s",
        (*error)->message);
    goto close_error;
  }
close_error:
  {
    if (socket)
      g_object_unref (socket);

    if (sock_error) {
      if (error == NULL)
        g_propagate_error (error, sock_error);
      else
        g_error_free (sock_error);
    }
    if (bind_error) {
      if ((error == NULL) || (*error == NULL))
        g_propagate_error (error, bind_error);
      else
        g_error_free (bind_error);
    }
    GST_RTSP_SERVER_UNLOCK (server);
    return NULL;
  }
}

struct _ClientContext
{
  GstRTSPServer *server;
  GstRTSPThread *thread;
  GstRTSPClient *client;
};

static gboolean
free_client_context (ClientContext * ctx)
{
  GST_DEBUG ("free context %p", ctx);

  GST_RTSP_SERVER_LOCK (ctx->server);
  if (ctx->thread)
    gst_rtsp_thread_stop (ctx->thread);
  GST_RTSP_SERVER_UNLOCK (ctx->server);

  g_object_unref (ctx->client);
  g_object_unref (ctx->server);
  g_slice_free (ClientContext, ctx);

  return G_SOURCE_REMOVE;
}

static void
unmanage_client (GstRTSPClient * client, ClientContext * ctx)
{
  GstRTSPServer *server = ctx->server;
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "unmanage client %p", client);

  GST_RTSP_SERVER_LOCK (server);
  priv->clients = g_list_remove (priv->clients, ctx);
  priv->clients_cookie++;
  GST_RTSP_SERVER_UNLOCK (server);

  if (ctx->thread) {
    GSource *src;

    src = g_idle_source_new ();
    g_source_set_callback (src, (GSourceFunc) free_client_context, ctx, NULL);
    g_source_attach (src, ctx->thread->context);
    g_source_unref (src);
  } else {
    free_client_context (ctx);
  }
}

/* add the client context to the active list of clients, takes ownership
 * of client */
static void
manage_client (GstRTSPServer * server, GstRTSPClient * client)
{
  ClientContext *cctx;
  GstRTSPServerPrivate *priv = server->priv;
  GMainContext *mainctx = NULL;
  GstRTSPContext ctx = { NULL };

  GST_DEBUG_OBJECT (server, "manage client %p", client);

  g_signal_emit (server, gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED], 0,
      client);

  cctx = g_slice_new0 (ClientContext);
  cctx->server = g_object_ref (server);
  cctx->client = client;

  GST_RTSP_SERVER_LOCK (server);

  ctx.server = server;
  ctx.client = client;

  cctx->thread = gst_rtsp_thread_pool_get_thread (priv->thread_pool,
      GST_RTSP_THREAD_TYPE_CLIENT, &ctx);
  if (cctx->thread)
    mainctx = cctx->thread->context;
  else {
    GSource *source;
    /* find the context to add the watch */
    if ((source = g_main_current_source ()))
      mainctx = g_source_get_context (source);
  }

  g_signal_connect (client, "closed", (GCallback) unmanage_client, cctx);
  priv->clients = g_list_prepend (priv->clients, cctx);
  priv->clients_cookie++;

  gst_rtsp_client_attach (client, mainctx);

  GST_RTSP_SERVER_UNLOCK (server);
}

static GstRTSPClient *
default_create_client (GstRTSPServer * server)
{
  GstRTSPClient *client;
  GstRTSPServerPrivate *priv = server->priv;

  /* a new client connected, create a session to handle the client. */
  client = gst_rtsp_client_new ();

  /* set the session pool that this client should use */
  GST_RTSP_SERVER_LOCK (server);
  gst_rtsp_client_set_session_pool (client, priv->session_pool);
  /* set the mount points that this client should use */
  gst_rtsp_client_set_mount_points (client, priv->mount_points);
  /* Set content-length limit */
  gst_rtsp_client_set_content_length_limit (GST_RTSP_CLIENT (client),
      priv->content_length_limit);
  /* set authentication manager */
  gst_rtsp_client_set_auth (client, priv->auth);
  /* set threadpool */
  gst_rtsp_client_set_thread_pool (client, priv->thread_pool);
  GST_RTSP_SERVER_UNLOCK (server);

  return client;
}

/**
 * gst_rtsp_server_transfer_connection:
 * @server: a #GstRTSPServer
 * @socket: (transfer full): a network socket
 * @ip: the IP address of the remote client
 * @port: the port used by the other end
 * @initial_buffer: (nullable): any initial data that was already read from the socket
 *
 * Take an existing network socket and use it for an RTSP connection. This
 * is used when transferring a socket from an HTTP server which should be used
 * as an RTSP over HTTP tunnel. The @initial_buffer contains any remaining data
 * that the HTTP server read from the socket while parsing the HTTP header.
 *
 * Returns: TRUE if all was ok, FALSE if an error occurred.
 */
gboolean
gst_rtsp_server_transfer_connection (GstRTSPServer * server, GSocket * socket,
    const gchar * ip, gint port, const gchar * initial_buffer)
{
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;
  GstRTSPConnection *conn;
  GstRTSPResult res;

  klass = GST_RTSP_SERVER_GET_CLASS (server);

  if (klass->create_client)
    client = klass->create_client (server);
  if (client == NULL)
    goto client_failed;

  GST_RTSP_CHECK (gst_rtsp_connection_create_from_socket (socket, ip, port,
          initial_buffer, &conn), no_connection);
  g_object_unref (socket);

  /* set connection on the client now */
  gst_rtsp_client_set_connection (client, conn);

  /* manage the client connection */
  manage_client (server, client);

  return TRUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    g_object_unref (socket);
    return FALSE;
  }
no_connection:
  {
    gchar *str = gst_rtsp_strresult (res);
    GST_ERROR ("could not create connection from socket %p: %s", socket, str);
    g_free (str);
    g_object_unref (socket);
    return FALSE;
  }
}

/**
 * gst_rtsp_server_io_func:
 * @socket: a #GSocket
 * @condition: the condition on @source
 * @server: (transfer none): a #GstRTSPServer
 *
 * A default #GSocketSourceFunc that creates a new #GstRTSPClient to accept and handle a
 * new connection on @socket or @server.
 *
 * Returns: TRUE if the source could be connected, FALSE if an error occurred.
 */
gboolean
gst_rtsp_server_io_func (GSocket * socket, GIOCondition condition,
    GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = server->priv;
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;
  GstRTSPResult res;
  GstRTSPConnection *conn = NULL;
  GstRTSPContext ctx = { NULL };

  if (condition & G_IO_IN) {
    /* a new client connected. */
    GST_RTSP_CHECK (gst_rtsp_connection_accept (socket, &conn, NULL),
        accept_failed);

    ctx.server = server;
    ctx.conn = conn;
    ctx.auth = priv->auth;
    gst_rtsp_context_push_current (&ctx);

    if (!gst_rtsp_auth_check (GST_RTSP_AUTH_CHECK_CONNECT))
      goto connection_refused;

    klass = GST_RTSP_SERVER_GET_CLASS (server);
    /* a new client connected, create a client object to handle the client. */
    if (klass->create_client)
      client = klass->create_client (server);
    if (client == NULL)
      goto client_failed;

    /* set connection on the client now */
    gst_rtsp_client_set_connection (client, conn);

    /* manage the client connection */
    manage_client (server, client);
  } else {
    GST_WARNING_OBJECT (server, "received unknown event %08x", condition);
    goto exit_no_ctx;
  }
exit:
  gst_rtsp_context_pop_current (&ctx);
exit_no_ctx:

  return G_SOURCE_CONTINUE;

  /* ERRORS */
accept_failed:
  {
    gchar *str = gst_rtsp_strresult (res);
    GST_ERROR_OBJECT (server, "Could not accept client on socket %p: %s",
        socket, str);
    g_free (str);
    /* We haven't pushed the context yet, so just return */
    goto exit_no_ctx;
  }
connection_refused:
  {
    GST_ERROR_OBJECT (server, "connection refused");
    gst_rtsp_connection_free (conn);
    goto exit;
  }
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    gst_rtsp_connection_free (conn);
    goto exit;
  }
}

static void
watch_destroyed (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "source destroyed");

  g_object_unref (priv->socket);
  priv->socket = NULL;
  g_object_unref (server);
}

/**
 * gst_rtsp_server_create_source:
 * @server: a #GstRTSPServer
 * @cancellable: (allow-none): a #GCancellable or %NULL.
 * @error: (out): a #GError
 *
 * Create a #GSource for @server. The new source will have a default
 * #GSocketSourceFunc of gst_rtsp_server_io_func().
 *
 * @cancellable if not %NULL can be used to cancel the source, which will cause
 * the source to trigger, reporting the current condition (which is likely 0
 * unless cancellation happened at the same time as a condition change). You can
 * check for this in the callback using g_cancellable_is_cancelled().
 *
 * This takes a reference on @server until @source is destroyed.
 *
 * Returns: (transfer full): the #GSource for @server or %NULL when an error
 * occurred. Free with g_source_unref ()
 */
GSource *
gst_rtsp_server_create_source (GstRTSPServer * server,
    GCancellable * cancellable, GError ** error)
{
  GstRTSPServerPrivate *priv;
  GSocket *socket, *old;
  GSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  socket = gst_rtsp_server_create_socket (server, NULL, error);
  if (socket == NULL)
    goto no_socket;

  GST_RTSP_SERVER_LOCK (server);
  old = priv->socket;
  priv->socket = g_object_ref (socket);
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);

  /* create a watch for reads (new connections) and possible errors */
  source = g_socket_create_source (socket, G_IO_IN |
      G_IO_ERR | G_IO_HUP | G_IO_NVAL, cancellable);
  g_object_unref (socket);

  /* configure the callback */
  g_source_set_callback (source,
      (GSourceFunc) gst_rtsp_server_io_func, g_object_ref (server),
      (GDestroyNotify) watch_destroyed);

  return source;

no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket");
    return NULL;
  }
}

/**
 * gst_rtsp_server_attach:
 * @server: a #GstRTSPServer
 * @context: (allow-none): a #GMainContext
 *
 * Attaches @server to @context. When the mainloop for @context is run, the
 * server will be dispatched. When @context is %NULL, the default context will be
 * used).
 *
 * This function should be called when the server properties and urls are fully
 * configured and the server is ready to start.
 *
 * This takes a reference on @server until the source is destroyed. Note that
 * if @context is not the default main context as returned by
 * g_main_context_default() (or %NULL), g_source_remove() cannot be used to
 * destroy the source. In that case it is recommended to use
 * gst_rtsp_server_create_source() and attach it to @context manually.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext.
 */
guint
gst_rtsp_server_attach (GstRTSPServer * server, GMainContext * context)
{
  guint res;
  GSource *source;
  GError *error = NULL;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), 0);

  source = gst_rtsp_server_create_source (server, NULL, &error);
  if (source == NULL)
    goto no_source;

  res = g_source_attach (source, context);
  g_source_unref (source);

  return res;

  /* ERRORS */
no_source:
  {
    GST_ERROR_OBJECT (server, "failed to create watch: %s", error->message);
    g_error_free (error);
    return 0;
  }
}

/**
 * gst_rtsp_server_client_filter:
 * @server: a #GstRTSPServer
 * @func: (scope call) (allow-none): a callback
 * @user_data: user data passed to @func
 *
 * Call @func for each client managed by @server. The result value of @func
 * determines what happens to the client. @func will be called with @server
 * locked so no further actions on @server can be performed from @func.
 *
 * If @func returns #GST_RTSP_FILTER_REMOVE, the client will be removed from
 * @server.
 *
 * If @func returns #GST_RTSP_FILTER_KEEP, the client will remain in @server.
 *
 * If @func returns #GST_RTSP_FILTER_REF, the client will remain in @server but
 * will also be added with an additional ref to the result #GList of this
 * function..
 *
 * When @func is %NULL, #GST_RTSP_FILTER_REF will be assumed for each client.
 *
 * Returns: (element-type GstRTSPClient) (transfer full): a #GList with all
 * clients for which @func returned #GST_RTSP_FILTER_REF. After usage, each
 * element in the #GList should be unreffed before the list is freed.
 */
GList *
gst_rtsp_server_client_filter (GstRTSPServer * server,
    GstRTSPServerClientFilterFunc func, gpointer user_data)
{
  GstRTSPServerPrivate *priv;
  GList *result, *walk, *next;
  GHashTable *visited;
  guint cookie;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  result = NULL;
  if (func)
    visited = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  GST_RTSP_SERVER_LOCK (server);
restart:
  cookie = priv->clients_cookie;
  for (walk = priv->clients; walk; walk = next) {
    ClientContext *cctx = walk->data;
    GstRTSPClient *client = cctx->client;
    GstRTSPFilterResult res;
    gboolean changed;

    next = g_list_next (walk);

    if (func) {
      /* only visit each media once */
      if (g_hash_table_contains (visited, client))
        continue;

      g_hash_table_add (visited, g_object_ref (client));
      GST_RTSP_SERVER_UNLOCK (server);

      res = func (server, client, user_data);

      GST_RTSP_SERVER_LOCK (server);
    } else
      res = GST_RTSP_FILTER_REF;

    changed = (cookie != priv->clients_cookie);

    switch (res) {
      case GST_RTSP_FILTER_REMOVE:
        GST_RTSP_SERVER_UNLOCK (server);

        gst_rtsp_client_close (client);

        GST_RTSP_SERVER_LOCK (server);
        changed |= (cookie != priv->clients_cookie);
        break;
      case GST_RTSP_FILTER_REF:
        result = g_list_prepend (result, g_object_ref (client));
        break;
      case GST_RTSP_FILTER_KEEP:
      default:
        break;
    }
    if (changed)
      goto restart;
  }
  GST_RTSP_SERVER_UNLOCK (server);

  if (func)
    g_hash_table_unref (visited);

  return result;
}
