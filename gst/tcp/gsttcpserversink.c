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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst-i18n-plugin.h>

#include <sys/ioctl.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gsttcpserversink.h"
#include "gsttcp-marshal.h"

#define TCP_DEFAULT_HOST	"127.0.0.1"
#define TCP_DEFAULT_PORT	4953
#define TCP_BACKLOG		5

/* elementfactory information */
static GstElementDetails gst_tcpserversink_details =
GST_ELEMENT_DETAILS ("TCP Server sink",
    "Sink/Network",
    "Send data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

GST_DEBUG_CATEGORY (tcpserversink_debug);
#define GST_CAT_DEFAULT (tcpserversink_debug)

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
};

static void gst_tcpserversink_base_init (gpointer g_class);
static void gst_tcpserversink_class_init (GstTCPServerSink * klass);
static void gst_tcpserversink_init (GstTCPServerSink * tcpserversink);

static gboolean gst_tcpserversink_handle_select (GstMultiFdSink * sink,
    fd_set * readfds, fd_set * writefds);
static gboolean gst_tcpserversink_init_send (GstMultiFdSink * this);
static gboolean gst_tcpserversink_close (GstMultiFdSink * this);

static void gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpserversink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstMultiFdSinkClass *parent_class = NULL;

//static guint gst_tcpserversink_signals[LAST_SIGNAL] = { 0 };

GType
gst_tcpserversink_get_type (void)
{
  static GType tcpserversink_type = 0;


  if (!tcpserversink_type) {
    static const GTypeInfo tcpserversink_info = {
      sizeof (GstTCPServerSinkClass),
      gst_tcpserversink_base_init,
      NULL,
      (GClassInitFunc) gst_tcpserversink_class_init,
      NULL,
      NULL,
      sizeof (GstTCPServerSink),
      0,
      (GInstanceInitFunc) gst_tcpserversink_init,
      NULL
    };

    tcpserversink_type =
        g_type_register_static (GST_TYPE_MULTIFDSINK, "GstTCPServerSink",
        &tcpserversink_info, 0);
  }
  return tcpserversink_type;
}

static void
gst_tcpserversink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpserversink_details);
}

static void
gst_tcpserversink_class_init (GstTCPServerSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstMultiFdSinkClass *gstmultifdsink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstmultifdsink_class = (GstMultiFdSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_MULTIFDSINK);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "host", "The host/IP to send the packets to",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, 32768, TCP_DEFAULT_PORT, G_PARAM_READWRITE));

  gobject_class->set_property = gst_tcpserversink_set_property;
  gobject_class->get_property = gst_tcpserversink_get_property;

  gstmultifdsink_class->init = gst_tcpserversink_init_send;
  gstmultifdsink_class->select = gst_tcpserversink_handle_select;
  gstmultifdsink_class->close = gst_tcpserversink_close;

  GST_DEBUG_CATEGORY_INIT (tcpserversink_debug, "tcpserversink", 0, "TCP sink");
}

static void
gst_tcpserversink_init (GstTCPServerSink * this)
{
  this->server_port = TCP_DEFAULT_PORT;
  /* should support as minimum 576 for IPV4 and 1500 for IPV6 */
  /* this->mtu = 1500; */

  this->server_sock_fd = -1;
}

/* handle a read request on the server,
 * which indicates a new client connection */
static gboolean
gst_tcpserversink_handle_server_read (GstTCPServerSink * sink)
{
  /* new client */
  int client_sock_fd;
  struct sockaddr_in client_address;
  int client_address_len;
  GstTCPClient *client;
  GstMultiFdSink *parent = GST_MULTIFDSINK (sink);

  client_sock_fd =
      accept (sink->server_sock_fd, (struct sockaddr *) &client_address,
      &client_address_len);
  if (client_sock_fd == -1) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return FALSE;
  }

  /* create client datastructure */
  client = g_new0 (GstTCPClient, 1);
  client->fd = client_sock_fd;
  client->bufpos = -1;
  client->bufoffset = 0;
  client->sending = NULL;

  g_mutex_lock (parent->clientslock);
  parent->clients = g_list_prepend (parent->clients, client);
  g_mutex_unlock (parent->clientslock);

  /* we always read from a client */
  FD_SET (client_sock_fd, &parent->readfds);

  /* set the socket to non blocking */
  fcntl (client_sock_fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (sink, "added new client ip %s with fd %d",
      inet_ntoa (client_address.sin_addr), client_sock_fd);

  return TRUE;
}

static gboolean
gst_tcpserversink_handle_select (GstMultiFdSink * sink,
    fd_set * readfds, fd_set * writefds)
{
  GstTCPServerSink *this = GST_TCPSERVERSINK (sink);

  if (FD_ISSET (this->server_sock_fd, readfds)) {
    /* handle new client connection on server socket */
    if (!gst_tcpserversink_handle_server_read (this)) {
      GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
          ("client connection failed: %s", g_strerror (errno)));
      return FALSE;
    }
  }
  return TRUE;
}

static void
gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSink *sink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  sink = GST_TCPSERVERSINK (object);

  switch (prop_id) {
    case ARG_HOST:
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
gst_tcpserversink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPServerSink *sink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  sink = GST_TCPSERVERSINK (object);

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
gst_tcpserversink_init_send (GstMultiFdSink * parent)
{
  int ret;
  GstTCPServerSink *this = GST_TCPSERVERSINK (parent);

  /* create sending server socket */
  if ((this->server_sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "opened sending server socket with fd %d",
      this->server_sock_fd);

  /* make address reusable */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
  /* keep connection alive; avoids SIGPIPE during write */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }

  /* name the socket */
  memset (&this->server_sin, 0, sizeof (this->server_sin));
  this->server_sin.sin_family = AF_INET;        /* network socket */
  this->server_sin.sin_port = htons (this->server_port);        /* on port */
  this->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);        /* for hosts */

  /* bind it */
  GST_DEBUG_OBJECT (this, "binding server socket to address");
  ret = bind (this->server_sock_fd, (struct sockaddr *) &this->server_sin,
      sizeof (this->server_sin));

  if (ret) {
    switch (errno) {
      default:
        GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
            ("bind failed: %s", g_strerror (errno)));
        return FALSE;
        break;
    }
  }

  /* set the server socket to nonblocking */
  fcntl (this->server_sock_fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (this, "listening on server socket %d with queue of %d",
      this->server_sock_fd, TCP_BACKLOG);
  if (listen (this->server_sock_fd, TCP_BACKLOG) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
  GST_DEBUG_OBJECT (this,
      "listened on server socket %d, returning from connection setup",
      this->server_sock_fd);

  FD_SET (this->server_sock_fd, &parent->readfds);

  return TRUE;
}

static gboolean
gst_tcpserversink_close (GstMultiFdSink * parent)
{
  GstTCPServerSink *this = GST_TCPSERVERSINK (parent);

  if (this->server_sock_fd != -1) {
    close (this->server_sock_fd);
    this->server_sock_fd = -1;
  }
  return TRUE;
}
