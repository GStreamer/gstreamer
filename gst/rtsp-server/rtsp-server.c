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

#define TCP_BACKLOG             5
#define DEFAULT_PORT            1554

G_DEFINE_TYPE (GstRTSPServer, gst_rtsp_server, G_TYPE_OBJECT);

static void
gst_rtsp_server_class_init (GstRTSPServerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
}

static void
gst_rtsp_server_init (GstRTSPServer * server)
{
  server->server_port = DEFAULT_PORT;
  server->pool = gst_rtsp_session_pool_new ();
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
  server->server_sin.sin_port = htons (server->server_port);        /* on port */
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
      server->server_sock.fd, TCP_BACKLOG);
  if (listen (server->server_sock.fd, TCP_BACKLOG) == -1)
    goto listen_failed;

  GST_DEBUG_OBJECT (server,
      "listened on server socket %d, returning from connection setup",
      server->server_sock.fd);

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

/* called when an event is available on our server socket */
static gboolean
server_dispatch (GIOChannel *source, GIOCondition condition, GstRTSPServer *server)
{
  if (condition & G_IO_IN) {
    GstRTSPClient *client;

    /* a new client connected, create a session to handle the client. */
    client = gst_rtsp_client_new ();

    /* set the session pool that this client should use */
    gst_rtsp_client_set_session_pool (client, server->pool);

    /* accept connections for that client, this function returns after accepting
     * the connection and will run the remainder of the communication with the
     * client asyncronously. */
    if (!gst_rtsp_client_accept (client, source))
      goto accept_failed;

    /* can unref the client now, when the request is finished, it will be
     * unreffed async. */
    gst_object_unref (client);
  }
  else {
    g_print ("received unknown event %08x", condition);
  }
  return TRUE;

  /* ERRORS */
accept_failed:
  {
    g_error ("Could not accept client on server socket %d: %s (%d)",
            server->server_sock.fd, g_strerror (errno), errno);
    return FALSE;
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

  if (!gst_rtsp_server_sink_init_send (server))
    goto init_failed;

  /* create IO channel for the socket */
  server->io_channel = g_io_channel_unix_new (server->server_sock.fd);

  /* create a watch for reads (new connections) and possible errors */
  server->io_watch = g_io_create_watch (server->io_channel, G_IO_IN |
		  G_IO_ERR | G_IO_HUP | G_IO_NVAL);

  /* configure the callback */
  g_source_set_callback (server->io_watch, (GSourceFunc) server_dispatch, server, NULL);

  res = g_source_attach (server->io_watch, context);

  return res;

  /* ERRORS */
init_failed:
  {
    return 0;
  }
}
