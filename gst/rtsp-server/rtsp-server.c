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

#include <sys/ioctl.h>

#include "rtsp-server.h"
#include "rtsp-client.h"

#define DEFAULT_BACKLOG         5
#define DEFAULT_PORT            8554

enum
{
  PROP_0,
  PROP_BACKLOG,
  PROP_PORT,
  PROP_SESSION_POOL,
  PROP_MEDIA_MAPPING,
  PROP_LAST
};

G_DEFINE_TYPE (GstRTSPServer, gst_rtsp_server, G_TYPE_OBJECT);

static void gst_rtsp_server_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_server_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_server_finalize (GObject *object);

static GstRTSPClient * default_accept_client (GstRTSPServer *server,
    GIOChannel *channel);

static void
gst_rtsp_server_class_init (GstRTSPServerClass * klass)
{ 
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  
  gobject_class->get_property = gst_rtsp_server_get_property;
  gobject_class->set_property = gst_rtsp_server_set_property;
  gobject_class->finalize = gst_rtsp_server_finalize;
  
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
      g_param_spec_int ("backlog", "Backlog", "The maximum length to which the queue "
	      "of pending connections may grow",
          0, G_MAXINT, DEFAULT_BACKLOG, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::port
   *
   * The session port of the server. This is the port where the server will
   * listen on.
   */
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "Port", "The port the server uses to listen on",
          1, 65535, DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
          GST_TYPE_RTSP_SESSION_POOL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::media-mapping
   *
   * The media mapping to use for this server. By default the server has no
   * media mapping and thus cannot map urls to media streams.
   */
  g_object_class_install_property (gobject_class, PROP_MEDIA_MAPPING,
      g_param_spec_object ("media-mapping", "Media Mapping",
	  "The media mapping to use for client session",
          GST_TYPE_RTSP_MEDIA_MAPPING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->accept_client = default_accept_client;
}

static void
gst_rtsp_server_init (GstRTSPServer * server)
{
  server->port = DEFAULT_PORT;
  server->backlog = DEFAULT_BACKLOG;
  server->session_pool = gst_rtsp_session_pool_new ();
  server->media_mapping = gst_rtsp_media_mapping_new ();
}

static void
gst_rtsp_server_finalize (GObject *object)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  g_object_unref (server->session_pool);
  g_object_unref (server->media_mapping);
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
 * gst_rtsp_server_set_port:
 * @server: a #GstRTSPServer
 * @port: the port
 *
 * Configure @server to accept connections on the given port.
 * @port should be a port number between 1 and 65535.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_port (GstRTSPServer *server, gint port)
{
  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (port >= 1 && port <= 65535);

  server->port = port;
}

/**
 * gst_rtsp_server_get_port:
 * @server: a #GstRTSPServer
 *
 * Get the port number on which the server will accept connections.
 *
 * Returns: the server port.
 */
gint
gst_rtsp_server_get_port (GstRTSPServer *server)
{
  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  return server->port;
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
gst_rtsp_server_set_backlog (GstRTSPServer *server, gint backlog)
{
  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  server->backlog = backlog;
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
gst_rtsp_server_get_backlog (GstRTSPServer *server)
{
  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  return server->backlog;
}

/**
 * gst_rtsp_server_set_session_pool:
 * @server: a #GstRTSPServer
 * @pool: a #GstRTSPSessionPool
 *
 * configure @pool to be used as the session pool of @server.
 */
void
gst_rtsp_server_set_session_pool (GstRTSPServer *server, GstRTSPSessionPool *pool)
{
  GstRTSPSessionPool *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  old = server->session_pool;

  if (old != pool) {
    if (pool)
      g_object_ref (pool);
    server->session_pool = pool;
    if (old)
      g_object_unref (old);
  }
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
gst_rtsp_server_get_session_pool (GstRTSPServer *server)
{
  GstRTSPSessionPool *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  if ((result = server->session_pool))
    g_object_ref (result);

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
gst_rtsp_server_set_media_mapping (GstRTSPServer *server, GstRTSPMediaMapping *mapping)
{
  GstRTSPMediaMapping *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  old = server->media_mapping;

  if (old != mapping) {
    if (mapping)
      g_object_ref (mapping);
    server->media_mapping = mapping;
    if (old)
      g_object_unref (old);
  }
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
gst_rtsp_server_get_media_mapping (GstRTSPServer *server)
{
  GstRTSPMediaMapping *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  if ((result = server->media_mapping))
    g_object_ref (result);

  return result;
}

static void
gst_rtsp_server_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_PORT:
      g_value_set_int (value, gst_rtsp_server_get_port (server));
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
gst_rtsp_server_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_PORT:
      gst_rtsp_server_set_port (server, g_value_get_int (value));
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

/* Prepare a server socket for @server and make it listen on the configured port */
static gboolean
gst_rtsp_server_sink_init_send (GstRTSPServer * server)
{
  int ret;

  /* create server socket */
  if ((server->server_sock.fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    goto no_socket;

  GST_DEBUG_OBJECT (server, "opened sending server socket with fd %d",
      server->server_sock.fd);

  /* make address reusable */
  ret = 1;
  if (setsockopt (server->server_sock.fd, SOL_SOCKET, SO_REUSEADDR,
          (void *) &ret, sizeof (ret)) < 0)
    goto reuse_failed;

  /* keep connection alive; avoids SIGPIPE during write */
  ret = 1;
  if (setsockopt (server->server_sock.fd, SOL_SOCKET, SO_KEEPALIVE,
          (void *) &ret, sizeof (ret)) < 0)
    goto keepalive_failed;

  /* name the socket */
  memset (&server->server_sin, 0, sizeof (server->server_sin));
  server->server_sin.sin_family = AF_INET;        /* network socket */
  server->server_sin.sin_port = htons (server->port);        /* on port */
  server->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);        /* for hosts */

  /* bind it */
  GST_DEBUG_OBJECT (server, "binding server socket to address");
  ret = bind (server->server_sock.fd, (struct sockaddr *) &server->server_sin,
      sizeof (server->server_sin));
  if (ret)
    goto bind_failed;

  /* set the server socket to nonblocking */
  fcntl (server->server_sock.fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (server, "listening on server socket %d with queue of %d",
      server->server_sock.fd, server->backlog);
  if (listen (server->server_sock.fd, server->backlog) == -1)
    goto listen_failed;

  GST_DEBUG_OBJECT (server,
      "listened on server socket %d, returning from connection setup",
      server->server_sock.fd);

  g_message ("listening on port %d", server->port);

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket: %s", g_strerror (errno));
    return FALSE;
  }
reuse_failed:
  {
    if (server->server_sock.fd >= 0) {
      close (server->server_sock.fd);
      server->server_sock.fd = -1;
    }
    GST_ERROR_OBJECT (server, "failed to reuse socket: %s", g_strerror (errno));
    return FALSE;
  }
keepalive_failed:
  {
    if (server->server_sock.fd >= 0) {
      close (server->server_sock.fd);
      server->server_sock.fd = -1;
    }
    GST_ERROR_OBJECT (server, "failed to configure keepalive socket: %s", g_strerror (errno));
    return FALSE;
  }
listen_failed:
  {
    if (server->server_sock.fd >= 0) {
      close (server->server_sock.fd);
      server->server_sock.fd = -1;
    }
    GST_ERROR_OBJECT (server, "failed to listen on socket: %s", g_strerror (errno));
    return FALSE;
  }
bind_failed:
  {
    if (server->server_sock.fd >= 0) {
      close (server->server_sock.fd);
      server->server_sock.fd = -1;
    }
    GST_ERROR_OBJECT (server, "failed to bind on socket: %s", g_strerror (errno));
    return FALSE;
  }
}

/* default method for creating a new client object in the server to accept and
 * handle a client connection on this server */
static GstRTSPClient *
default_accept_client (GstRTSPServer *server, GIOChannel *channel)
{
  GstRTSPClient *client;

  /* a new client connected, create a session to handle the client. */
  client = gst_rtsp_client_new ();

  /* set the session pool that this client should use */
  gst_rtsp_client_set_session_pool (client, server->session_pool);

  /* set the session pool that this client should use */
  gst_rtsp_client_set_media_mapping (client, server->media_mapping);

  /* accept connections for that client, this function returns after accepting
   * the connection and will run the remainder of the communication with the
   * client asyncronously. */
  if (!gst_rtsp_client_accept (client, channel))
    goto accept_failed;

  return client;

  /* ERRORS */
accept_failed:
  {
    g_error ("Could not accept client on server socket %d: %s (%d)",
            server->server_sock.fd, g_strerror (errno), errno);
    gst_object_unref (client);
    return NULL;
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
gst_rtsp_server_io_func (GIOChannel *channel, GIOCondition condition, GstRTSPServer *server)
{
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;

  if (condition & G_IO_IN) {
    klass = GST_RTSP_SERVER_GET_CLASS (server);

    /* a new client connected, create a client object to handle the client. */
    if (klass->accept_client)
      client = klass->accept_client (server, channel);
    if (client == NULL)
      goto client_failed;

    /* can unref the client now, when the request is finished, it will be
     * unreffed async. */
    gst_object_unref (client);
  }
  else {
    g_print ("received unknown event %08x", condition);
  }
  return TRUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    return FALSE;
  }
}

/**
 * gst_rtsp_server_get_io_channel:
 * @server: a #GstRTSPServer
 *
 * Create a #GIOChannel for @server.
 *
 * Returns: the GIOChannel for @server or NULL when an error occured.
 */
GIOChannel *
gst_rtsp_server_get_io_channel (GstRTSPServer *server)
{
  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  if (server->io_channel == NULL) {
    if (!gst_rtsp_server_sink_init_send (server))
      goto init_failed;

    /* create IO channel for the socket */
    server->io_channel = g_io_channel_unix_new (server->server_sock.fd);
  }
  return server->io_channel;

init_failed:
  {
    return NULL;
  }
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
gst_rtsp_server_create_watch (GstRTSPServer *server)
{
  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  if (server->io_watch == NULL) {
    GIOChannel *channel;

    channel = gst_rtsp_server_get_io_channel (server);
    if (channel == NULL)
      goto no_channel;
     
    /* create a watch for reads (new connections) and possible errors */
    server->io_watch = g_io_create_watch (channel, G_IO_IN |
		  G_IO_ERR | G_IO_HUP | G_IO_NVAL);

    /* configure the callback */
    g_source_set_callback (server->io_watch, (GSourceFunc) gst_rtsp_server_io_func, server, NULL);
  }
  return server->io_watch;

no_channel:
  {
    return NULL;
  }
}

/**
 * gst_rtsp_server_attach:
 * @server: a #GstRTSPServer
 * @context: a #GMainContext
 *
 * Attaches @server to @context. When the mainloop for @context is run, the
 * server will be dispatched.
 *
 * This function should be called when the server properties and urls are fully
 * configured and the server is ready to start.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext. 
 */
guint
gst_rtsp_server_attach (GstRTSPServer *server, GMainContext *context)
{
  guint res;
  GSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), 0);

  source = gst_rtsp_server_create_watch (server);
  if (source == NULL)
    goto no_source;

  res = g_source_attach (source, context);

  return res;

  /* ERRORS */
no_source:
  {
    return 0;
  }
}
