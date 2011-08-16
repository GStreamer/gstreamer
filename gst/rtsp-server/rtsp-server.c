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
#include <sys/ioctl.h>

#include "rtsp-server.h"
#include "rtsp-client.h"

#define DEFAULT_ADDRESS         "0.0.0.0"
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
  PROP_BACKLOG,

  PROP_SESSION_POOL,
  PROP_MEDIA_MAPPING,
  PROP_LAST
};

enum
{
  SIGNAL_CLIENT_CONNECTED,
  SIGNAL_LAST
};

G_DEFINE_TYPE (GstRTSPServer, gst_rtsp_server, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (rtsp_server_debug);
#define GST_CAT_DEFAULT rtsp_server_debug

static guint gst_rtsp_server_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_finalize (GObject * object);

static GstRTSPClient *default_create_client (GstRTSPServer * server);
static gboolean default_accept_client (GstRTSPServer * server,
    GstRTSPClient * client, GIOChannel * channel);

static void
gst_rtsp_server_class_init (GstRTSPServerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_server_get_property;
  gobject_class->set_property = gst_rtsp_server_set_property;
  gobject_class->finalize = gst_rtsp_server_finalize;

  /**
   * GstRTSPServer::address
   *
   * The address of the server. This is the address where the server will
   * listen on.
   */
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "The address the server uses to listen on", DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::service
   *
   * The service of the server. This is either a string with the service name or
   * a port number (as a string) the server will listen on.
   */
  g_object_class_install_property (gobject_class, PROP_SERVICE,
      g_param_spec_string ("service", "Service",
          "The service or port number the server uses to listen on",
          DEFAULT_SERVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::backlog
   *
   * The backlog argument defines the maximum length to which the queue of
   * pending connections for the server may grow. If a connection request arrives
   * when the queue is full, the client may receive an error with an indication of
   * ECONNREFUSED or, if the underlying protocol supports retransmission, the
   * request may be ignored so that a later reattempt at  connection succeeds.
   */
  g_object_class_install_property (gobject_class, PROP_BACKLOG,
      g_param_spec_int ("backlog", "Backlog",
          "The maximum length to which the queue "
          "of pending connections may grow", 0, G_MAXINT, DEFAULT_BACKLOG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::session-pool
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
   * GstRTSPServer::media-mapping
   *
   * The media mapping to use for this server. By default the server has no
   * media mapping and thus cannot map urls to media streams.
   */
  g_object_class_install_property (gobject_class, PROP_MEDIA_MAPPING,
      g_param_spec_object ("media-mapping", "Media Mapping",
          "The media mapping to use for client session",
          GST_TYPE_RTSP_MEDIA_MAPPING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED] =
      g_signal_new ("client-connected", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPServerClass, client_connected),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      gst_rtsp_client_get_type ());

  klass->create_client = default_create_client;
  klass->accept_client = default_accept_client;

  GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "rtspserver", 0, "GstRTSPServer");
}

static void
gst_rtsp_server_init (GstRTSPServer * server)
{
  server->lock = g_mutex_new ();
  server->address = g_strdup (DEFAULT_ADDRESS);
  server->service = g_strdup (DEFAULT_SERVICE);
  server->backlog = DEFAULT_BACKLOG;
  server->session_pool = gst_rtsp_session_pool_new ();
  server->media_mapping = gst_rtsp_media_mapping_new ();
}

static void
gst_rtsp_server_finalize (GObject * object)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  GST_DEBUG_OBJECT (server, "finalize server");

  g_free (server->address);
  g_free (server->service);

  g_object_unref (server->session_pool);
  g_object_unref (server->media_mapping);

  if (server->auth)
    g_object_unref (server->auth);

  g_mutex_free (server->lock);

  G_OBJECT_CLASS (gst_rtsp_server_parent_class)->finalize (object);
}

/**
 * gst_rtsp_server_new:
 *
 * Create a new #GstRTSPServer instance.
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
  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (address != NULL);

  GST_RTSP_SERVER_LOCK (server);
  g_free (server->address);
  server->address = g_strdup (address);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_address:
 * @server: a #GstRTSPServer
 *
 * Get the address on which the server will accept connections.
 *
 * Returns: the server address. g_free() after usage.
 */
gchar *
gst_rtsp_server_get_address (GstRTSPServer * server)
{
  gchar *result;
  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (server->address);
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
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_service (GstRTSPServer * server, const gchar * service)
{
  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (service != NULL);

  GST_RTSP_SERVER_LOCK (server);
  g_free (server->service);
  server->service = g_strdup (service);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_service:
 * @server: a #GstRTSPServer
 *
 * Get the service on which the server will accept connections.
 *
 * Returns: the service. use g_free() after usage.
 */
gchar *
gst_rtsp_server_get_service (GstRTSPServer * server)
{
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (server->service);
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
  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  GST_RTSP_SERVER_LOCK (server);
  server->backlog = backlog;
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
  gint result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  GST_RTSP_SERVER_LOCK (server);
  result = server->backlog;
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_session_pool:
 * @server: a #GstRTSPServer
 * @pool: a #GstRTSPSessionPool
 *
 * configure @pool to be used as the session pool of @server.
 */
void
gst_rtsp_server_set_session_pool (GstRTSPServer * server,
    GstRTSPSessionPool * pool)
{
  GstRTSPSessionPool *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  if (pool)
    g_object_ref (pool);

  GST_RTSP_SERVER_LOCK (server);
  old = server->session_pool;
  server->session_pool = pool;
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
 * Returns: the #GstRTSPSessionPool used for sessions. g_object_unref() after
 * usage.
 */
GstRTSPSessionPool *
gst_rtsp_server_get_session_pool (GstRTSPServer * server)
{
  GstRTSPSessionPool *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  GST_RTSP_SERVER_LOCK (server);
  if ((result = server->session_pool))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_media_mapping:
 * @server: a #GstRTSPServer
 * @mapping: a #GstRTSPMediaMapping
 *
 * configure @mapping to be used as the media mapping of @server.
 */
void
gst_rtsp_server_set_media_mapping (GstRTSPServer * server,
    GstRTSPMediaMapping * mapping)
{
  GstRTSPMediaMapping *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  if (mapping)
    g_object_ref (mapping);

  GST_RTSP_SERVER_LOCK (server);
  old = server->media_mapping;
  server->media_mapping = mapping;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_server_get_media_mapping:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPMediaMapping used as the media mapping of @server.
 *
 * Returns: the #GstRTSPMediaMapping of @server. g_object_unref() after
 * usage.
 */
GstRTSPMediaMapping *
gst_rtsp_server_get_media_mapping (GstRTSPServer * server)
{
  GstRTSPMediaMapping *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  GST_RTSP_SERVER_LOCK (server);
  if ((result = server->media_mapping))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_auth:
 * @server: a #GstRTSPServer
 * @auth: a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @server.
 */
void
gst_rtsp_server_set_auth (GstRTSPServer * server, GstRTSPAuth * auth)
{
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  if (auth)
    g_object_ref (auth);

  GST_RTSP_SERVER_LOCK (server);
  old = server->auth;
  server->auth = auth;
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
 * Returns: the #GstRTSPAuth of @server. g_object_unref() after
 * usage.
 */
GstRTSPAuth *
gst_rtsp_server_get_auth (GstRTSPServer * server)
{
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  GST_RTSP_SERVER_LOCK (server);
  if ((result = server->auth))
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
    case PROP_BACKLOG:
      g_value_set_int (value, gst_rtsp_server_get_backlog (server));
      break;
    case PROP_SESSION_POOL:
      g_value_take_object (value, gst_rtsp_server_get_session_pool (server));
      break;
    case PROP_MEDIA_MAPPING:
      g_value_take_object (value, gst_rtsp_server_get_media_mapping (server));
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
    case PROP_MEDIA_MAPPING:
      gst_rtsp_server_set_media_mapping (server, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_server_get_io_channel:
 * @server: a #GstRTSPServer
 *
 * Create a #GIOChannel for @server. The io channel will listen on the
 * configured service.
 *
 * Returns: the GIOChannel for @server or NULL when an error occured.
 */
GIOChannel *
gst_rtsp_server_get_io_channel (GstRTSPServer * server)
{
  GIOChannel *channel;
  int ret, sockfd = -1;
  struct addrinfo hints;
  struct addrinfo *result, *rp;
#ifdef USE_SOLINGER
  struct linger linger;
#endif

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;      /* stream socket */
  hints.ai_flags = AI_PASSIVE | AI_CANONNAME;   /* For wildcard IP address */
  hints.ai_protocol = 0;        /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  GST_DEBUG_OBJECT (server, "getting address info of %s/%s", server->address,
      server->service);

  GST_RTSP_SERVER_LOCK (server);
  /* resolve the server IP address */
  if ((ret =
          getaddrinfo (server->address, server->service, &hints, &result)) != 0)
    goto no_address;

  /* create server socket, we loop through all the addresses until we manage to
   * create a socket and bind. */
  for (rp = result; rp; rp = rp->ai_next) {
    sockfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sockfd == -1) {
      GST_DEBUG_OBJECT (server, "failed to make socket (%s), try next",
          g_strerror (errno));
      continue;
    }

    /* make address reusable */
    ret = 1;
    if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR,
            (void *) &ret, sizeof (ret)) < 0) {
      /* warn but try to bind anyway */
      GST_WARNING_OBJECT (server, "failed to reuse socker (%s)",
          g_strerror (errno));
    }

    if (bind (sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
      GST_DEBUG_OBJECT (server, "bind on %s", rp->ai_canonname);
      break;
    }

    GST_DEBUG_OBJECT (server, "failed to bind socket (%s), try next",
        g_strerror (errno));
    close (sockfd);
    sockfd = -1;
  }
  freeaddrinfo (result);

  if (sockfd == -1)
    goto no_socket;

  GST_DEBUG_OBJECT (server, "opened sending server socket with fd %d", sockfd);

  /* keep connection alive; avoids SIGPIPE during write */
  ret = 1;
  if (setsockopt (sockfd, SOL_SOCKET, SO_KEEPALIVE,
          (void *) &ret, sizeof (ret)) < 0)
    goto keepalive_failed;

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

  /* set the server socket to nonblocking */
  fcntl (sockfd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (server, "listening on server socket %d with queue of %d",
      sockfd, server->backlog);
  if (listen (sockfd, server->backlog) == -1)
    goto listen_failed;

  GST_DEBUG_OBJECT (server,
      "listened on server socket %d, returning from connection setup", sockfd);

  /* create IO channel for the socket */
  channel = g_io_channel_unix_new (sockfd);
  g_io_channel_set_close_on_unref (channel, TRUE);

  GST_INFO_OBJECT (server, "listening on service %s", server->service);
  GST_RTSP_SERVER_UNLOCK (server);

  return channel;

  /* ERRORS */
no_address:
  {
    GST_ERROR_OBJECT (server, "failed to resolve address: %s",
        gai_strerror (ret));
    goto close_error;
  }
no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket: %s",
        g_strerror (errno));
    goto close_error;
  }
keepalive_failed:
  {
    GST_ERROR_OBJECT (server, "failed to configure keepalive socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#ifdef USE_SOLINGER
linger_failed:
  {
    GST_ERROR_OBJECT (server, "failed to no linger socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#endif
listen_failed:
  {
    GST_ERROR_OBJECT (server, "failed to listen on socket: %s",
        g_strerror (errno));
    goto close_error;
  }
close_error:
  {
    if (sockfd >= 0) {
      close (sockfd);
    }
    GST_RTSP_SERVER_UNLOCK (server);
    return NULL;
  }
}

static void
unmanage_client (GstRTSPClient * client, GstRTSPServer * server)
{
  GST_DEBUG_OBJECT (server, "unmanage client %p", client);

  g_object_ref (server);
  gst_rtsp_client_set_server (client, NULL);

  GST_RTSP_SERVER_LOCK (server);
  server->clients = g_list_remove (server->clients, client);
  GST_RTSP_SERVER_UNLOCK (server);
  g_object_unref (server);

  g_object_unref (client);
}

/* add the client to the active list of clients, takes ownership of
 * the client */
static void
manage_client (GstRTSPServer * server, GstRTSPClient * client)
{
  GST_DEBUG_OBJECT (server, "manage client %p", client);
  gst_rtsp_client_set_server (client, server);

  GST_RTSP_SERVER_LOCK (server);
  g_signal_connect (client, "closed", (GCallback) unmanage_client, server);
  server->clients = g_list_prepend (server->clients, client);
  GST_RTSP_SERVER_UNLOCK (server);
}

static GstRTSPClient *
default_create_client (GstRTSPServer * server)
{
  GstRTSPClient *client;

  /* a new client connected, create a session to handle the client. */
  client = gst_rtsp_client_new ();

  /* set the session pool that this client should use */
  GST_RTSP_SERVER_LOCK (server);
  gst_rtsp_client_set_session_pool (client, server->session_pool);
  /* set the media mapping that this client should use */
  gst_rtsp_client_set_media_mapping (client, server->media_mapping);
  /* set authentication manager */
  gst_rtsp_client_set_auth (client, server->auth);
  GST_RTSP_SERVER_UNLOCK (server);

  return client;
}

/* default method for creating a new client object in the server to accept and
 * handle a client connection on this server */
static gboolean
default_accept_client (GstRTSPServer * server, GstRTSPClient * client,
    GIOChannel * channel)
{
  /* accept connections for that client, this function returns after accepting
   * the connection and will run the remainder of the communication with the
   * client asyncronously. */
  if (!gst_rtsp_client_accept (client, channel))
    goto accept_failed;

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    GST_ERROR_OBJECT (server,
        "Could not accept client on server : %s (%d)", g_strerror (errno),
        errno);
    return FALSE;
  }
}

/**
 * gst_rtsp_server_io_func:
 * @channel: a #GIOChannel
 * @condition: the condition on @source
 *
 * A default #GIOFunc that creates a new #GstRTSPClient to accept and handle a
 * new connection on @channel or @server.
 *
 * Returns: TRUE if the source could be connected, FALSE if an error occured.
 */
gboolean
gst_rtsp_server_io_func (GIOChannel * channel, GIOCondition condition,
    GstRTSPServer * server)
{
  gboolean result;
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;

  if (condition & G_IO_IN) {
    klass = GST_RTSP_SERVER_GET_CLASS (server);

    if (klass->create_client)
      client = klass->create_client (server);
    if (client == NULL)
      goto client_failed;

    /* a new client connected, create a client object to handle the client. */
    if (klass->accept_client)
      result = klass->accept_client (server, client, channel);
    if (!result)
      goto accept_failed;

    /* manage the client connection */
    manage_client (server, client);

    g_signal_emit (server, gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED], 0,
        client);
  } else {
    GST_WARNING_OBJECT (server, "received unknown event %08x", condition);
  }
  return TRUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    return FALSE;
  }
accept_failed:
  {
    GST_ERROR_OBJECT (server, "failed to accept client");
    gst_object_unref (client);
    return FALSE;
  }
}

static void
watch_destroyed (GstRTSPServer * server)
{
  GST_DEBUG_OBJECT (server, "source destroyed");
  g_object_unref (server);
}

/**
 * gst_rtsp_server_create_watch:
 * @server: a #GstRTSPServer
 *
 * Create a #GSource for @server. The new source will have a default
 * #GIOFunc of gst_rtsp_server_io_func().
 *
 * Returns: the #GSource for @server or NULL when an error occured.
 */
GSource *
gst_rtsp_server_create_watch (GstRTSPServer * server)
{
  GIOChannel *channel;
  GSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  channel = gst_rtsp_server_get_io_channel (server);
  if (channel == NULL)
    goto no_channel;

  /* create a watch for reads (new connections) and possible errors */
  source = g_io_create_watch (channel, G_IO_IN |
      G_IO_ERR | G_IO_HUP | G_IO_NVAL);
  g_io_channel_unref (channel);

  /* configure the callback */
  g_source_set_callback (source,
      (GSourceFunc) gst_rtsp_server_io_func, g_object_ref (server),
      (GDestroyNotify) watch_destroyed);

  return source;

no_channel:
  {
    GST_ERROR_OBJECT (server, "failed to create IO channel");
    return NULL;
  }
}

/**
 * gst_rtsp_server_attach:
 * @server: a #GstRTSPServer
 * @context: a #GMainContext
 *
 * Attaches @server to @context. When the mainloop for @context is run, the
 * server will be dispatched. When @context is NULL, the default context will be
 * used).
 *
 * This function should be called when the server properties and urls are fully
 * configured and the server is ready to start.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext.
 */
guint
gst_rtsp_server_attach (GstRTSPServer * server, GMainContext * context)
{
  guint res;
  GSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), 0);

  source = gst_rtsp_server_create_watch (server);
  if (source == NULL)
    goto no_source;

  res = g_source_attach (source, context);
  g_source_unref (source);

  return res;

  /* ERRORS */
no_source:
  {
    GST_ERROR_OBJECT (server, "failed to create watch");
    return 0;
  }
}
