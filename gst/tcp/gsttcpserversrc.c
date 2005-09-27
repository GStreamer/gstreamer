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

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

GST_DEBUG_CATEGORY (tcpserversrc_debug);
#define GST_CAT_DEFAULT tcpserversrc_debug

#define TCP_DEFAULT_LISTEN_HOST		NULL    /* listen on all interfaces */
#define TCP_BACKLOG			1       /* client connection queue */

/* elementfactory information */
static GstElementDetails gst_tcpserversrc_details =
GST_ELEMENT_DETAILS ("TCP Server source",
    "Source/Network",
    "Receive data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


/* TCPServerSrc signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_PROTOCOL
};

static void gst_tcpserversrc_base_init (gpointer g_class);
static void gst_tcpserversrc_class_init (GstTCPServerSrc * klass);
static void gst_tcpserversrc_init (GstTCPServerSrc * tcpserversrc);
static void gst_tcpserversrc_finalize (GObject * gobject);

static gboolean gst_tcpserversrc_start (GstBaseSrc * bsrc);
static gboolean gst_tcpserversrc_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_tcpserversrc_create (GstPushSrc * psrc,
    GstBuffer ** buf);

static void gst_tcpserversrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpserversrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_tcpserversrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_tcpserversrc_get_type (void)
{
  static GType tcpserversrc_type = 0;


  if (!tcpserversrc_type) {
    static const GTypeInfo tcpserversrc_info = {
      sizeof (GstTCPServerSrcClass),
      gst_tcpserversrc_base_init,
      NULL,
      (GClassInitFunc) gst_tcpserversrc_class_init,
      NULL,
      NULL,
      sizeof (GstTCPServerSrc),
      0,
      (GInstanceInitFunc) gst_tcpserversrc_init,
      NULL
    };

    tcpserversrc_type =
        g_type_register_static (GST_TYPE_PUSH_SRC, "GstTCPServerSrc",
        &tcpserversrc_info, 0);
  }
  return tcpserversrc_type;
}

static void
gst_tcpserversrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_tcpserversrc_details);
}

static void
gst_tcpserversrc_class_init (GstTCPServerSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

  gobject_class->set_property = gst_tcpserversrc_set_property;
  gobject_class->get_property = gst_tcpserversrc_get_property;
  gobject_class->finalize = gst_tcpserversrc_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "Host", "The hostname to listen as",
          TCP_DEFAULT_LISTEN_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "Port", "The port to listen to",
          0, TCP_HIGHEST_PORT, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL, GST_TCP_PROTOCOL_NONE, G_PARAM_READWRITE));

  gstbasesrc_class->start = gst_tcpserversrc_start;
  gstbasesrc_class->stop = gst_tcpserversrc_stop;

  gstpush_src_class->create = gst_tcpserversrc_create;

  GST_DEBUG_CATEGORY_INIT (tcpserversrc_debug, "tcpserversrc", 0,
      "TCP Server Source");
}

static void
gst_tcpserversrc_init (GstTCPServerSrc * src)
{
  src->server_port = TCP_DEFAULT_PORT;
  src->host = g_strdup (TCP_DEFAULT_HOST);
  src->server_sock_fd = -1;
  src->client_sock_fd = -1;
  src->curoffset = 0;
  src->protocol = GST_TCP_PROTOCOL_NONE;

  GST_FLAG_UNSET (src, GST_TCPSERVERSRC_OPEN);
}

static void
gst_tcpserversrc_finalize (GObject * gobject)
{
  GstTCPServerSrc *src = GST_TCPSERVERSRC (gobject);

  g_free (src->host);
}

static GstFlowReturn
gst_tcpserversrc_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstTCPServerSrc *src;
  GstFlowReturn ret;

  src = GST_TCPSERVERSRC (psrc);

  if (!GST_FLAG_IS_SET (src, GST_TCPSERVERSRC_OPEN))
    goto wrong_state;

  GST_LOG_OBJECT (src, "asked for a buffer");

  switch (src->protocol) {
    case GST_TCP_PROTOCOL_NONE:
      ret = gst_tcp_read_buffer (GST_ELEMENT (src), src->client_sock_fd, -1,
          outbuf);
      break;

    case GST_TCP_PROTOCOL_GDP:
      if (!src->caps_received) {
        GstCaps *caps;
        gchar *string;

        ret = gst_tcp_gdp_read_caps (GST_ELEMENT (src), src->client_sock_fd, -1,
            &caps);

        if (ret != GST_FLOW_OK)
          goto gdp_caps_read_error;

        src->caps_received = TRUE;
        string = gst_caps_to_string (caps);
        GST_DEBUG_OBJECT (src, "Received caps through GDP: %s", string);
        g_free (string);

        gst_pad_set_caps (GST_BASE_SRC_PAD (psrc), caps);
      }

      ret = gst_tcp_gdp_read_buffer (GST_ELEMENT (src), src->client_sock_fd, -1,
          outbuf);

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
gdp_caps_read_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not read caps through GDP"));
    return ret;
  }
}

static void
gst_tcpserversrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSrc *tcpserversrc;

  g_return_if_fail (GST_IS_TCPSERVERSRC (object));
  tcpserversrc = GST_TCPSERVERSRC (object);

  switch (prop_id) {
    case ARG_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (tcpserversrc->host);
      tcpserversrc->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      tcpserversrc->server_port = g_value_get_int (value);
      break;
    case ARG_PROTOCOL:
      tcpserversrc->protocol = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcpserversrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPServerSrc *tcpserversrc;

  g_return_if_fail (GST_IS_TCPSERVERSRC (object));
  tcpserversrc = GST_TCPSERVERSRC (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, tcpserversrc->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, tcpserversrc->server_port);
      break;
    case ARG_PROTOCOL:
      g_value_set_enum (value, tcpserversrc->protocol);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* set up server */
static gboolean
gst_tcpserversrc_start (GstBaseSrc * bsrc)
{
  int ret;
  GstTCPServerSrc *src = GST_TCPSERVERSRC (bsrc);

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

  /* FIXME: maybe we should think about moving actual client accepting
     somewhere else */
  GST_DEBUG_OBJECT (src, "waiting for client");
  if ((src->client_sock_fd =
          accept (src->server_sock_fd, (struct sockaddr *) &src->client_sin,
              &src->client_sin_len)) == -1)
    goto accept_error;

  GST_DEBUG_OBJECT (src, "received client");

  GST_FLAG_SET (src, GST_TCPSERVERSRC_OPEN);

  return TRUE;

  /* ERRORS */
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
accept_error:
  {
    gst_tcp_socket_close (&src->server_sock_fd);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
}

static gboolean
gst_tcpserversrc_stop (GstBaseSrc * bsrc)
{
  GstTCPServerSrc *src = GST_TCPSERVERSRC (bsrc);

  if (src->server_sock_fd != -1) {
    close (src->server_sock_fd);
    src->server_sock_fd = -1;
  }
  if (src->client_sock_fd != -1) {
    close (src->client_sock_fd);
    src->client_sock_fd = -1;
  }
  GST_FLAG_UNSET (src, GST_TCPSERVERSRC_OPEN);

  return TRUE;
}
