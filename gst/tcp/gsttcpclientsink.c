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
 * SECTION:element-tcpclientsink
 * @see_also: #tcpclientsrc
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * # server:
 * nc -l -p 3000
 * # client:
 * gst-launch fdsrc fd=1 ! tcpclientsink protocol=none port=3000
 * ]| everything you type in the client is shown on the server
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst-i18n-plugin.h>
#include <gst/dataprotocol/dataprotocol.h>
#include "gsttcp.h"
#include "gsttcpclientsink.h"
#include <string.h>             /* memset */

/* TCPClientSink signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

GST_DEBUG_CATEGORY_STATIC (tcpclientsink_debug);
#define GST_CAT_DEFAULT (tcpclientsink_debug)

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_PROTOCOL
      /* FILL ME */
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_tcp_client_sink_base_init (gpointer g_class);
static void gst_tcp_client_sink_class_init (GstTCPClientSink * klass);
static void gst_tcp_client_sink_init (GstTCPClientSink * tcpclientsink);
static void gst_tcp_client_sink_finalize (GObject * gobject);

static gboolean gst_tcp_client_sink_setcaps (GstBaseSink * bsink,
    GstCaps * caps);
static GstFlowReturn gst_tcp_client_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static GstStateChangeReturn gst_tcp_client_sink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_tcp_client_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcp_client_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstElementClass *parent_class = NULL;

/*static guint gst_tcp_client_sink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_tcp_client_sink_get_type (void)
{
  static GType tcpclientsink_type = 0;


  if (!tcpclientsink_type) {
    static const GTypeInfo tcpclientsink_info = {
      sizeof (GstTCPClientSinkClass),
      gst_tcp_client_sink_base_init,
      NULL,
      (GClassInitFunc) gst_tcp_client_sink_class_init,
      NULL,
      NULL,
      sizeof (GstTCPClientSink),
      0,
      (GInstanceInitFunc) gst_tcp_client_sink_init,
      NULL
    };

    tcpclientsink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstTCPClientSink",
        &tcpclientsink_info, 0);
  }
  return tcpclientsink_type;
}

static void
gst_tcp_client_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class,
      "TCP client sink", "Sink/Network",
      "Send data as a client over the network via TCP",
      "Thomas Vander Stichele <thomas at apestaart dot org>");
}

static void
gst_tcp_client_sink_class_init (GstTCPClientSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_tcp_client_sink_set_property;
  gobject_class->get_property = gst_tcp_client_sink_get_property;
  gobject_class->finalize = gst_tcp_client_sink_finalize;

  g_object_class_install_property (gobject_class, ARG_HOST,
      g_param_spec_string ("host", "Host", "The host/IP to send the packets to",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PORT,
      g_param_spec_int ("port", "Port", "The port to send the packets to",
          0, TCP_HIGHEST_PORT, TCP_DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL, GST_TCP_PROTOCOL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_tcp_client_sink_change_state;

  gstbasesink_class->set_caps = gst_tcp_client_sink_setcaps;
  gstbasesink_class->render = gst_tcp_client_sink_render;

  GST_DEBUG_CATEGORY_INIT (tcpclientsink_debug, "tcpclientsink", 0, "TCP sink");
}

static void
gst_tcp_client_sink_init (GstTCPClientSink * this)
{
  this->host = g_strdup (TCP_DEFAULT_HOST);
  this->port = TCP_DEFAULT_PORT;

  this->sock_fd.fd = -1;
  this->protocol = GST_TCP_PROTOCOL_NONE;
  GST_OBJECT_FLAG_UNSET (this, GST_TCP_CLIENT_SINK_OPEN);
}

static void
gst_tcp_client_sink_finalize (GObject * gobject)
{
  GstTCPClientSink *this = GST_TCP_CLIENT_SINK (gobject);

  g_free (this->host);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gboolean
gst_tcp_client_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstTCPClientSink *sink;

  sink = GST_TCP_CLIENT_SINK (bsink);

  /* write the buffer header if we have one */
  switch (sink->protocol) {
    case GST_TCP_PROTOCOL_NONE:
      break;

    case GST_TCP_PROTOCOL_GDP:
      /* if we haven't send caps yet, send them first */
      if (!sink->caps_sent) {
        const GstCaps *caps;
        gchar *string;

        caps = GST_PAD_CAPS (GST_PAD_PEER (GST_BASE_SINK_PAD (bsink)));
        string = gst_caps_to_string (caps);
        GST_DEBUG_OBJECT (sink, "Sending caps %s through GDP", string);
        g_free (string);

        if (!gst_tcp_gdp_write_caps (GST_ELEMENT (sink), sink->sock_fd.fd,
                caps, TRUE, sink->host, sink->port))
          goto gdp_write_error;

        sink->caps_sent = TRUE;
      }
      break;
    default:
      g_warning ("Unhandled protocol type");
      break;
  }

  return TRUE;

  /* ERRORS */
gdp_write_error:
  {
    return FALSE;
  }
}

static GstFlowReturn
gst_tcp_client_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  size_t wrote = 0;
  GstTCPClientSink *sink;
  gint size;

  sink = GST_TCP_CLIENT_SINK (bsink);

  g_return_val_if_fail (GST_OBJECT_FLAG_IS_SET (sink, GST_TCP_CLIENT_SINK_OPEN),
      GST_FLOW_WRONG_STATE);

  size = GST_BUFFER_SIZE (buf);

  GST_LOG_OBJECT (sink, "writing %d bytes for buffer data", size);

  /* write the buffer header if we have one */
  switch (sink->protocol) {
    case GST_TCP_PROTOCOL_NONE:
      break;
    case GST_TCP_PROTOCOL_GDP:
      GST_LOG_OBJECT (sink, "Sending buffer header through GDP");
      if (!gst_tcp_gdp_write_buffer (GST_ELEMENT (sink), sink->sock_fd.fd, buf,
              TRUE, sink->host, sink->port))
        goto gdp_write_error;
      break;
    default:
      break;
  }

  /* write buffer data */
  wrote = gst_tcp_socket_write (sink->sock_fd.fd, GST_BUFFER_DATA (buf), size);

  if (wrote < size)
    goto write_error;

  sink->data_written += wrote;

  return GST_FLOW_OK;

  /* ERRORS */
gdp_write_error:
  {
    return FALSE;
  }
write_error:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
        (_("Error while sending data to \"%s:%d\"."), sink->host, sink->port),
        ("Only %" G_GSIZE_FORMAT " of %u bytes written: %s",
            wrote, GST_BUFFER_SIZE (buf), g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
}

static void
gst_tcp_client_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPClientSink *tcpclientsink;

  g_return_if_fail (GST_IS_TCP_CLIENT_SINK (object));
  tcpclientsink = GST_TCP_CLIENT_SINK (object);

  switch (prop_id) {
    case ARG_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (tcpclientsink->host);
      tcpclientsink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      tcpclientsink->port = g_value_get_int (value);
      break;
    case ARG_PROTOCOL:
      tcpclientsink->protocol = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcp_client_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTCPClientSink *tcpclientsink;

  g_return_if_fail (GST_IS_TCP_CLIENT_SINK (object));
  tcpclientsink = GST_TCP_CLIENT_SINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, tcpclientsink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, tcpclientsink->port);
      break;
    case ARG_PROTOCOL:
      g_value_set_enum (value, tcpclientsink->protocol);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_tcp_client_sink_start (GstTCPClientSink * this)
{
  int ret;
  gchar *ip;

  if (GST_OBJECT_FLAG_IS_SET (this, GST_TCP_CLIENT_SINK_OPEN))
    return TRUE;

  /* reset caps_sent flag */
  this->caps_sent = FALSE;

  /* create sending client socket */
  GST_DEBUG_OBJECT (this, "opening sending client socket to %s:%d", this->host,
      this->port);
  if ((this->sock_fd.fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "opened sending client socket with fd %d",
      this->sock_fd.fd);

  /* look up name if we need to */
  ip = gst_tcp_host_to_ip (GST_ELEMENT (this), this->host);
  if (!ip) {
    gst_tcp_socket_close (&this->sock_fd);
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "IP address for host %s is %s", this->host, ip);

  /* connect to server */
  memset (&this->server_sin, 0, sizeof (this->server_sin));
  this->server_sin.sin_family = AF_INET;        /* network socket */
  this->server_sin.sin_port = htons (this->port);       /* on port */
  this->server_sin.sin_addr.s_addr = inet_addr (ip);    /* on host ip */
  g_free (ip);

  GST_DEBUG_OBJECT (this, "connecting to server");
  ret = connect (this->sock_fd.fd, (struct sockaddr *) &this->server_sin,
      sizeof (this->server_sin));

  if (ret) {
    gst_tcp_socket_close (&this->sock_fd);
    switch (errno) {
      case ECONNREFUSED:
        GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE,
            (_("Connection to %s:%d refused."), this->host, this->port),
            (NULL));
        return FALSE;
        break;
      default:
        GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
            ("connect to %s:%d failed: %s", this->host, this->port,
                g_strerror (errno)));
        return FALSE;
        break;
    }
  }

  GST_OBJECT_FLAG_SET (this, GST_TCP_CLIENT_SINK_OPEN);

  this->data_written = 0;

  return TRUE;
}

static gboolean
gst_tcp_client_sink_stop (GstTCPClientSink * this)
{
  if (!GST_OBJECT_FLAG_IS_SET (this, GST_TCP_CLIENT_SINK_OPEN))
    return TRUE;

  gst_tcp_socket_close (&this->sock_fd);

  GST_OBJECT_FLAG_UNSET (this, GST_TCP_CLIENT_SINK_OPEN);

  return TRUE;
}

static GstStateChangeReturn
gst_tcp_client_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstTCPClientSink *sink;
  GstStateChangeReturn res;

  sink = GST_TCP_CLIENT_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_tcp_client_sink_start (sink))
        goto start_failure;
      break;
    default:
      break;
  }
  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tcp_client_sink_stop (sink);
    default:
      break;
  }
  return res;

start_failure:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}
