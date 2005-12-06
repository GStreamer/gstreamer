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
#include "gsttcp.h"
#include "gsttcpserversrc.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>


/* control stuff stolen from fdsrc */
#define CONTROL_STOP            'S'     /* stop the select call */
#define CONTROL_SOCKETS(o)      o->control_fds
#define WRITE_SOCKET(o)         o->control_fds[1]
#define READ_SOCKET(o)          o->control_fds[0]

#define SEND_COMMAND(o, command)          \
G_STMT_START {                              \
  unsigned char c; c = command;             \
  write (WRITE_SOCKET(o), &c, 1);         \
} G_STMT_END

#define READ_COMMAND(o, command, res)        \
G_STMT_START {                                 \
  res = read(READ_SOCKET(o), &command, 1);   \
} G_STMT_END


GST_DEBUG_CATEGORY (tcpserversrc_debug);
#define GST_CAT_DEFAULT tcpserversrc_debug

#define TCP_DEFAULT_LISTEN_HOST         NULL    /* listen on all interfaces */
#define TCP_BACKLOG                     1       /* client connection queue */


static GstElementDetails gst_tcp_server_src_details =
GST_ELEMENT_DETAILS ("TCP Server source",
    "Source/Network",
    "Receive data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_PROTOCOL
};


GST_BOILERPLATE (GstTCPServerSrc, gst_tcp_server_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);


static void gst_tcp_server_src_finalize (GObject * gobject);

static gboolean gst_tcp_server_src_start (GstBaseSrc * bsrc);
static gboolean gst_tcp_server_src_stop (GstBaseSrc * bsrc);
static gboolean gst_tcp_server_src_unlock (GstBaseSrc * bsrc);
static GstFlowReturn gst_tcp_server_src_create (GstPushSrc * psrc,
    GstBuffer ** buf);

static void gst_tcp_server_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcp_server_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static void
gst_tcp_server_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_tcp_server_src_details);
}

static void
gst_tcp_server_src_class_init (GstTCPServerSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_tcp_server_src_set_property;
  gobject_class->get_property = gst_tcp_server_src_get_property;
  gobject_class->finalize = gst_tcp_server_src_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HOST,
      g_param_spec_string ("host", "Host", "The hostname to listen as",
          TCP_DEFAULT_LISTEN_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "Port", "The port to listen to",
          0, TCP_HIGHEST_PORT, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL, GST_TCP_PROTOCOL_NONE, G_PARAM_READWRITE));

  gstbasesrc_class->start = gst_tcp_server_src_start;
  gstbasesrc_class->stop = gst_tcp_server_src_stop;
  gstbasesrc_class->unlock = gst_tcp_server_src_unlock;

  gstpush_src_class->create = gst_tcp_server_src_create;

  GST_DEBUG_CATEGORY_INIT (tcpserversrc_debug, "tcpserversrc", 0,
      "TCP Server Source");
}

static void
gst_tcp_server_src_init (GstTCPServerSrc * src, GstTCPServerSrcClass * g_class)
{
  src->server_port = TCP_DEFAULT_PORT;
  src->host = g_strdup (TCP_DEFAULT_HOST);
  src->server_sock_fd = -1;
  src->client_sock_fd = -1;
  src->protocol = GST_TCP_PROTOCOL_NONE;

  READ_SOCKET (src) = -1;
  WRITE_SOCKET (src) = -1;

  GST_OBJECT_FLAG_UNSET (src, GST_TCP_SERVER_SRC_OPEN);
}

static void
gst_tcp_server_src_finalize (GObject * gobject)
{
  GstTCPServerSrc *src = GST_TCP_SERVER_SRC (gobject);

  g_free (src->host);
}

static GstFlowReturn
gst_tcp_server_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstTCPServerSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  fd_set testfds;
  int maxfdp1;

  src = GST_TCP_SERVER_SRC (psrc);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_TCP_SERVER_SRC_OPEN))
    goto wrong_state;

restart:
  /* do a blocking select on the socket */
  FD_ZERO (&testfds);

  /* always select on cancel socket */
  FD_SET (READ_SOCKET (src), &testfds);

  if (src->client_sock_fd >= 0) {
    /* if we have a client, wait for read */
    FD_SET (src->client_sock_fd, &testfds);
    maxfdp1 = MAX (src->client_sock_fd, READ_SOCKET (src)) + 1;
  } else {
    /* else wait on server socket for connections */
    FD_SET (src->server_sock_fd, &testfds);
    maxfdp1 = MAX (src->server_sock_fd, READ_SOCKET (src)) + 1;
  }

  /* no action (0) is an error too in our case */
  if (select (maxfdp1, &testfds, NULL, NULL, 0) <= 0)
    goto select_error;

  if (FD_ISSET (READ_SOCKET (src), &testfds))
    goto select_cancelled;

  /* if we have no client socket we can accept one now */
  if (src->client_sock_fd < 0) {
    if (FD_ISSET (src->server_sock_fd, &testfds)) {
      if ((src->client_sock_fd =
              accept (src->server_sock_fd, (struct sockaddr *) &src->client_sin,
                  &src->client_sin_len)) == -1)
        goto accept_error;
    }
    /* and restart now to poll the socket. */
    goto restart;
  }

  GST_LOG_OBJECT (src, "asked for a buffer");

  switch (src->protocol) {
    case GST_TCP_PROTOCOL_NONE:
      ret = gst_tcp_read_buffer (GST_ELEMENT (src), src->client_sock_fd,
          READ_SOCKET (src), outbuf);
      break;

    case GST_TCP_PROTOCOL_GDP:
      if (!src->caps_received) {
        GstCaps *caps;
        gchar *string;

        ret = gst_tcp_gdp_read_caps (GST_ELEMENT (src), src->client_sock_fd,
            READ_SOCKET (src), &caps);

        if (ret == GST_FLOW_WRONG_STATE)
          goto gdp_cancelled;

        if (ret != GST_FLOW_OK)
          goto gdp_caps_read_error;

        src->caps_received = TRUE;
        string = gst_caps_to_string (caps);
        GST_DEBUG_OBJECT (src, "Received caps through GDP: %s", string);
        g_free (string);

        gst_pad_set_caps (GST_BASE_SRC_PAD (psrc), caps);
      }

      ret = gst_tcp_gdp_read_buffer (GST_ELEMENT (src), src->client_sock_fd,
          READ_SOCKET (src), outbuf);

      if (ret == GST_FLOW_OK)
        gst_buffer_set_caps (*outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));

      break;

    default:
      /* need to assert as buf == NULL */
      g_assert ("Unhandled protocol type");
      break;
  }

  if (ret == GST_FLOW_OK) {
    GST_LOG_OBJECT (src,
        "Returning buffer from _get of size %d, ts %"
        GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
        ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
        GST_BUFFER_SIZE (*outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (*outbuf)),
        GST_BUFFER_OFFSET (*outbuf), GST_BUFFER_OFFSET_END (*outbuf));
  }

  return ret;

wrong_state:
  {
    GST_DEBUG_OBJECT (src, "connection to closed, cannot read data");
    return GST_FLOW_WRONG_STATE;
  }
select_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Select error: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
select_cancelled:
  {
    GST_DEBUG_OBJECT (src, "select canceled");
    return GST_FLOW_WRONG_STATE;
  }
accept_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
gdp_cancelled:
  {
    GST_DEBUG_OBJECT (src, "reading gdp canceled");
    return GST_FLOW_WRONG_STATE;
  }
gdp_caps_read_error:
  {
    /* if we did not get canceled, report an error */
    if (ret != GST_FLOW_WRONG_STATE) {
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Could not read caps through GDP"));
    }
    return ret;
  }
}

static void
gst_tcp_server_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSrc *tcpserversrc = GST_TCP_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (tcpserversrc->host);
      tcpserversrc->host = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      tcpserversrc->server_port = g_value_get_int (value);
      break;
    case PROP_PROTOCOL:
      tcpserversrc->protocol = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcp_server_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTCPServerSrc *tcpserversrc = GST_TCP_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, tcpserversrc->host);
      break;
    case PROP_PORT:
      g_value_set_int (value, tcpserversrc->server_port);
      break;
    case PROP_PROTOCOL:
      g_value_set_enum (value, tcpserversrc->protocol);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* set up server */
static gboolean
gst_tcp_server_src_start (GstBaseSrc * bsrc)
{
  int ret;
  GstTCPServerSrc *src = GST_TCP_SERVER_SRC (bsrc);

  /* create the control sockets before anything */
  if (socketpair (PF_UNIX, SOCK_STREAM, 0, CONTROL_SOCKETS (src)) < 0)
    goto socket_pair;

  fcntl (READ_SOCKET (src), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (src), F_SETFL, O_NONBLOCK);

  /* reset caps_received flag */
  src->caps_received = FALSE;

  /* create the server listener socket */
  if ((src->server_sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    goto socket_error;

  GST_DEBUG_OBJECT (src, "opened receiving server socket with fd %d",
      src->server_sock_fd);

  /* make address reusable */
  ret = 1;
  if (setsockopt (src->server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &ret,
          sizeof (int)) < 0)
    goto sock_opt;

  /* name the socket */
  memset (&src->server_sin, 0, sizeof (src->server_sin));
  src->server_sin.sin_family = AF_INET; /* network socket */
  src->server_sin.sin_port = htons (src->server_port);  /* on port */
  if (src->host) {
    gchar *host;

    if (!(host = gst_tcp_host_to_ip (GST_ELEMENT (src), src->host)))
      goto host_error;
    src->server_sin.sin_addr.s_addr = inet_addr (host);
    g_free (host);
  } else
    src->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);

  /* bind it */
  GST_DEBUG_OBJECT (src, "binding server socket to address");
  if ((ret = bind (src->server_sock_fd, (struct sockaddr *) &src->server_sin,
              sizeof (src->server_sin))) < 0)
    goto bind_error;

  GST_DEBUG_OBJECT (src, "listening on server socket %d with queue of %d",
      src->server_sock_fd, TCP_BACKLOG);

  if (listen (src->server_sock_fd, TCP_BACKLOG) == -1)
    goto listen_error;

  GST_DEBUG_OBJECT (src, "received client");

  GST_OBJECT_FLAG_SET (src, GST_TCP_SERVER_SRC_OPEN);

  return TRUE;

  /* ERRORS */
socket_pair:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
socket_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
sock_opt:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
host_error:
  {
    gst_tcp_socket_close (&src->server_sock_fd);
    return FALSE;
  }
bind_error:
  {
    gst_tcp_socket_close (&src->server_sock_fd);
    switch (errno) {
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
            ("bind failed: %s", g_strerror (errno)));
        break;
    }
    return FALSE;
  }
listen_error:
  {
    gst_tcp_socket_close (&src->server_sock_fd);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
}

static gboolean
gst_tcp_server_src_stop (GstBaseSrc * bsrc)
{
  GstTCPServerSrc *src = GST_TCP_SERVER_SRC (bsrc);

  if (src->server_sock_fd != -1) {
    close (src->server_sock_fd);
    src->server_sock_fd = -1;
  }
  if (src->client_sock_fd != -1) {
    close (src->client_sock_fd);
    src->client_sock_fd = -1;
  }
  GST_OBJECT_FLAG_UNSET (src, GST_TCP_SERVER_SRC_OPEN);

  close (READ_SOCKET (src));
  close (WRITE_SOCKET (src));
  READ_SOCKET (src) = -1;
  WRITE_SOCKET (src) = -1;

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_tcp_server_src_unlock (GstBaseSrc * bsrc)
{
  GstTCPServerSrc *src = GST_TCP_SERVER_SRC (bsrc);

  SEND_COMMAND (src, CONTROL_STOP);

  return TRUE;
}
