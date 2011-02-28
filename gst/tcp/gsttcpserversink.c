/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

/**
 * SECTION:element-tcpserversink
 * @see_also: #multifdsink
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * # server:
 * gst-launch fdsrc fd=1 ! tcpserversink protocol=none port=3000
 * # client:
 * gst-launch tcpclientsrc protocol=none port=3000 ! fdsink fd=2
 * ]| 
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst-i18n-plugin.h>
#include <string.h>             /* memset */

#include <sys/ioctl.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gsttcp.h"
#include "gsttcpserversink.h"
#include "gsttcp-marshal.h"

#define TCP_BACKLOG             5

GST_DEBUG_CATEGORY_STATIC (tcpserversink_debug);
#define GST_CAT_DEFAULT (tcpserversink_debug)

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
};

static void gst_tcp_server_sink_finalize (GObject * gobject);

static gboolean gst_tcp_server_sink_handle_wait (GstMultiFdSink * sink,
    GstPoll * set);
static gboolean gst_tcp_server_sink_init_send (GstMultiFdSink * this);
static gboolean gst_tcp_server_sink_close (GstMultiFdSink * this);
static void gst_tcp_server_sink_removed (GstMultiFdSink * sink, int fd);

static void gst_tcp_server_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcp_server_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


GST_BOILERPLATE (GstTCPServerSink, gst_tcp_server_sink, GstMultiFdSink,
    GST_TYPE_MULTI_FD_SINK);


static void
gst_tcp_server_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "TCP server sink", "Sink/Network",
      "Send data as a server over the network via TCP",
      "Thomas Vander Stichele <thomas at apestaart dot org>");
}

static void
gst_tcp_server_sink_class_init (GstTCPServerSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstMultiFdSinkClass *gstmultifdsink_class;

  gobject_class = (GObjectClass *) klass;
  gstmultifdsink_class = (GstMultiFdSinkClass *) klass;

  gobject_class->set_property = gst_tcp_server_sink_set_property;
  gobject_class->get_property = gst_tcp_server_sink_get_property;
  gobject_class->finalize = gst_tcp_server_sink_finalize;

  g_object_class_install_property (gobject_class, ARG_HOST,
      g_param_spec_string ("host", "host", "The host/IP to send the packets to",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, TCP_HIGHEST_PORT, TCP_DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstmultifdsink_class->init = gst_tcp_server_sink_init_send;
  gstmultifdsink_class->wait = gst_tcp_server_sink_handle_wait;
  gstmultifdsink_class->close = gst_tcp_server_sink_close;
  gstmultifdsink_class->removed = gst_tcp_server_sink_removed;

  GST_DEBUG_CATEGORY_INIT (tcpserversink_debug, "tcpserversink", 0, "TCP sink");
}

static void
gst_tcp_server_sink_init (GstTCPServerSink * this,
    GstTCPServerSinkClass * klass)
{
  this->server_port = TCP_DEFAULT_PORT;
  /* should support as minimum 576 for IPV4 and 1500 for IPV6 */
  /* this->mtu = 1500; */
  this->host = g_strdup (TCP_DEFAULT_HOST);

  this->server_sock.fd = -1;
}

static void
gst_tcp_server_sink_finalize (GObject * gobject)
{
  GstTCPServerSink *this = GST_TCP_SERVER_SINK (gobject);

  g_free (this->host);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

/* handle a read request on the server,
 * which indicates a new client connection */
static gboolean
gst_tcp_server_sink_handle_server_read (GstTCPServerSink * sink)
{
  /* new client */
  int client_sock_fd;
  struct sockaddr_in client_address;
  socklen_t client_address_len;

  /* For some stupid reason, client_address and client_address_len has to be
   * zeroed */
  memset (&client_address, 0, sizeof (client_address));
  client_address_len = 0;

  client_sock_fd =
      accept (sink->server_sock.fd, (struct sockaddr *) &client_address,
      &client_address_len);
  if (client_sock_fd == -1)
    goto accept_failed;

  gst_multi_fd_sink_add (GST_MULTI_FD_SINK (sink), client_sock_fd);

  GST_DEBUG_OBJECT (sink, "added new client ip %s with fd %d",
      inet_ntoa (client_address.sin_addr), client_sock_fd);

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket %d: %s (%d)",
            sink->server_sock.fd, g_strerror (errno), errno));
    return FALSE;
  }
}

static void
gst_tcp_server_sink_removed (GstMultiFdSink * sink, int fd)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstTCPServerSink *this = GST_TCP_SERVER_SINK (sink);
#endif

  GST_LOG_OBJECT (this, "closing fd %d", fd);
  if (close (fd) < 0) {
    GST_WARNING_OBJECT (this, "error closing fd %d: %s", fd,
        g_strerror (errno));
  }
}

static gboolean
gst_tcp_server_sink_handle_wait (GstMultiFdSink * sink, GstPoll * set)
{
  GstTCPServerSink *this = GST_TCP_SERVER_SINK (sink);

  if (gst_poll_fd_can_read (set, &this->server_sock)) {
    /* handle new client connection on server socket */
    if (!gst_tcp_server_sink_handle_server_read (this))
      goto connection_failed;
  }
  return TRUE;

  /* ERRORS */
connection_failed:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("client connection failed: %s", g_strerror (errno)));
    return FALSE;
  }
}

static void
gst_tcp_server_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSink *sink;

  g_return_if_fail (GST_IS_TCP_SERVER_SINK (object));
  sink = GST_TCP_SERVER_SINK (object);

  switch (prop_id) {
    case ARG_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (sink->host);
      sink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      sink->server_port = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcp_server_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTCPServerSink *sink;

  g_return_if_fail (GST_IS_TCP_SERVER_SINK (object));
  sink = GST_TCP_SERVER_SINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, sink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, sink->server_port);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_tcp_server_sink_init_send (GstMultiFdSink * parent)
{
  int ret;
  GstTCPServerSink *this = GST_TCP_SERVER_SINK (parent);

  /* create sending server socket */
  if ((this->server_sock.fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    goto no_socket;

  GST_DEBUG_OBJECT (this, "opened sending server socket with fd %d",
      this->server_sock.fd);

  /* make address reusable */
  ret = 1;
  if (setsockopt (this->server_sock.fd, SOL_SOCKET, SO_REUSEADDR,
          (void *) &ret, sizeof (ret)) < 0)
    goto reuse_failed;

  /* keep connection alive; avoids SIGPIPE during write */
  ret = 1;
  if (setsockopt (this->server_sock.fd, SOL_SOCKET, SO_KEEPALIVE,
          (void *) &ret, sizeof (ret)) < 0)
    goto keepalive_failed;

  /* name the socket */
  memset (&this->server_sin, 0, sizeof (this->server_sin));
  this->server_sin.sin_family = AF_INET;        /* network socket */
  this->server_sin.sin_port = htons (this->server_port);        /* on port */
  this->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);        /* for hosts */

  /* bind it */
  GST_DEBUG_OBJECT (this, "binding server socket to address");
  ret = bind (this->server_sock.fd, (struct sockaddr *) &this->server_sin,
      sizeof (this->server_sin));
  if (ret)
    goto bind_failed;

  /* set the server socket to nonblocking */
  fcntl (this->server_sock.fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (this, "listening on server socket %d with queue of %d",
      this->server_sock.fd, TCP_BACKLOG);
  if (listen (this->server_sock.fd, TCP_BACKLOG) == -1)
    goto listen_failed;

  GST_DEBUG_OBJECT (this,
      "listened on server socket %d, returning from connection setup",
      this->server_sock.fd);

  gst_poll_add_fd (parent->fdset, &this->server_sock);
  gst_poll_fd_ctl_read (parent->fdset, &this->server_sock, TRUE);

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
reuse_failed:
  {
    gst_tcp_socket_close (&this->server_sock);
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
keepalive_failed:
  {
    gst_tcp_socket_close (&this->server_sock);
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
listen_failed:
  {
    gst_tcp_socket_close (&this->server_sock);
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
bind_failed:
  {
    gst_tcp_socket_close (&this->server_sock);
    switch (errno) {
      default:
        GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
            ("bind on port %d failed: %s", this->server_port,
                g_strerror (errno)));
        break;
    }
    return FALSE;
  }
}

static gboolean
gst_tcp_server_sink_close (GstMultiFdSink * parent)
{
  GstTCPServerSink *this = GST_TCP_SERVER_SINK (parent);

  if (this->server_sock.fd != -1) {
    gst_poll_remove_fd (parent->fdset, &this->server_sock);

    close (this->server_sock.fd);
    this->server_sock.fd = -1;
  }
  return TRUE;
}
