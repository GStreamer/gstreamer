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

#define TCP_DEFAULT_PORT		4953
#define TCP_DEFAULT_HOST		NULL    /* listen on all interfaces */
#define TCP_BACKLOG			1       /* client connection queue */

/* elementfactory information */
static GstElementDetails gst_tcpserversrc_details =
GST_ELEMENT_DETAILS ("TCP Server source",
    "Source/Network",
    "Receive data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

/* TCPServerSrc signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PORT,
  ARG_HOST,
  ARG_PROTOCOL
};

static void gst_tcpserversrc_base_init (gpointer g_class);
static void gst_tcpserversrc_class_init (GstTCPServerSrc * klass);
static void gst_tcpserversrc_init (GstTCPServerSrc * tcpserversrc);

static GstData *gst_tcpserversrc_get (GstPad * pad);
static GstElementStateReturn gst_tcpserversrc_change_state (GstElement *
    element);

static void gst_tcpserversrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpserversrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_tcpserversrc_set_clock (GstElement * element, GstClock * clock);

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
        g_type_register_static (GST_TYPE_ELEMENT, "GstTCPServerSrc",
        &tcpserversrc_info, 0);
  }
  return tcpserversrc_type;
}

static void
gst_tcpserversrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpserversrc_details);
}

static void
gst_tcpserversrc_class_init (GstTCPServerSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "Port", "The port to listen to",
          0, 32768, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL_TYPE, GST_TCP_PROTOCOL_TYPE_GDP,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "Host", "The hostname to listen",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE));

  gobject_class->set_property = gst_tcpserversrc_set_property;
  gobject_class->get_property = gst_tcpserversrc_get_property;

  gstelement_class->change_state = gst_tcpserversrc_change_state;
  gstelement_class->set_clock = gst_tcpserversrc_set_clock;

  GST_DEBUG_CATEGORY_INIT (tcpserversrc_debug, "tcpserversrc", 0,
      "TCP Server Source");
}

static void
gst_tcpserversrc_set_clock (GstElement * element, GstClock * clock)
{
  GstTCPServerSrc *tcpserversrc;

  tcpserversrc = GST_TCPSERVERSRC (element);

  tcpserversrc->clock = clock;
}

static void
gst_tcpserversrc_init (GstTCPServerSrc * this)
{
  /* create the src pad */
  this->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (this), this->srcpad);
  gst_pad_set_get_function (this->srcpad, gst_tcpserversrc_get);

  this->server_port = TCP_DEFAULT_PORT;
  this->host = TCP_DEFAULT_HOST;
  this->clock = NULL;
  this->server_sock_fd = -1;
  this->client_sock_fd = -1;
  this->curoffset = 0;
  this->protocol = GST_TCP_PROTOCOL_TYPE_GDP;

  GST_FLAG_UNSET (this, GST_TCPSERVERSRC_OPEN);
}

/* read the gdp caps packet from the socket */
static GstCaps *
gst_tcpserversrc_gdp_read_caps (GstTCPServerSrc * this)
{
  size_t header_length = GST_DP_HEADER_LENGTH;
  size_t readsize;
  guint8 *header = NULL;
  guint8 *payload = NULL;
  size_t ret;
  GstCaps *caps;
  gchar *string;

  header = g_malloc (header_length);

  readsize = header_length;
  GST_LOG_OBJECT (this, "Reading %d bytes for caps packet header", readsize);
  ret = read (this->client_sock_fd, header, readsize);
  if (ret < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return NULL;
  }
  g_assert (ret == readsize);

  if (!gst_dp_validate_header (header_length, header)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet header does not validate"));
    g_free (header);
    return NULL;
  }

  readsize = gst_dp_header_payload_length (header);
  payload = g_malloc (readsize);
  GST_LOG_OBJECT (this, "Reading %d bytes for caps packet payload", readsize);
  ret = read (this->client_sock_fd, payload, readsize);
  if (ret < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    g_free (header);
    return NULL;
  }
  g_assert (ret == readsize);

  if (!gst_dp_validate_payload (readsize, header, payload)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet payload does not validate"));
    g_free (header);
    g_free (payload);
    return NULL;
  }

  caps = gst_dp_caps_from_packet (header_length, header, payload);
  string = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (this, "retrieved GDP caps from packet payload: %s", string);

  g_free (header);
  g_free (payload);
  g_free (string);
  return caps;
}

/* read the gdp buffer header from the socket
 * returns a GstData,
 * representing the new GstBuffer to read data into, or an EOS event
 */
static GstData *
gst_tcpserversrc_gdp_read_header (GstTCPServerSrc * this)
{
  size_t header_length = GST_DP_HEADER_LENGTH;
  size_t readsize;
  guint8 *header = NULL;
  size_t ret;
  GstBuffer *buffer;

  header = g_malloc (header_length);

  readsize = header_length;
  GST_LOG_OBJECT (this, "Reading %d bytes for buffer packet header", readsize);
  ret = read (this->client_sock_fd, header, readsize);
  /* if we read 0 bytes, and we're blocking, we hit eos */
  if (ret == 0) {
    GST_DEBUG ("blocking read returns 0, EOS");
    gst_element_set_eos (GST_ELEMENT (this));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  if (ret < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return NULL;
  }
  if (ret != readsize) {
    g_warning ("Wanted %d bytes, got %d bytes", readsize, ret);
  }
  g_assert (ret == readsize);

  if (!gst_dp_validate_header (header_length, header)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP buffer packet header does not validate"));
    g_free (header);
    return NULL;
  }
  GST_LOG_OBJECT (this, "validated buffer packet header");

  buffer = gst_dp_buffer_from_header (header_length, header);

  GST_LOG_OBJECT (this, "created new buffer %p from packet header", buffer);
  return GST_DATA (buffer);
}

static GstData *
gst_tcpserversrc_get (GstPad * pad)
{
  GstTCPServerSrc *src;
  size_t readsize;
  int ret;

  GstData *data = NULL;
  GstBuffer *buf = NULL;
  GstCaps *caps;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  src = GST_TCPSERVERSRC (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_TCPSERVERSRC_OPEN), NULL);

  /* read the buffer header if we're using a protocol */
  switch (src->protocol) {
      fd_set testfds;

    case GST_TCP_PROTOCOL_TYPE_NONE:
      /* do a blocking select on the socket */
      FD_ZERO (&testfds);
      FD_SET (src->client_sock_fd, &testfds);
      ret =
          select (src->client_sock_fd + 1, &testfds, (fd_set *) 0, (fd_set *) 0,
          0);
      /* no action (0) is an error too in our case */
      if (ret <= 0) {
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("select failed: %s", g_strerror (errno)));
        return NULL;
      }
      /* ask how much is available for reading on the socket */
      ret = ioctl (src->client_sock_fd, FIONREAD, &readsize);
      if (ret < 0) {
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("ioctl failed: %s", g_strerror (errno)));
        return NULL;
      }

      buf = gst_buffer_new_and_alloc (readsize);
      break;
    case GST_TCP_PROTOCOL_TYPE_GDP:
      /* if we haven't received caps yet, we should get them first */
      if (!src->caps_received) {
        gchar *string;

        if (!(caps = gst_tcpserversrc_gdp_read_caps (src))) {
          GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
              ("Could not read caps through GDP"));
          return NULL;
        }
        src->caps_received = TRUE;
        string = gst_caps_to_string (caps);
        GST_DEBUG_OBJECT (src, "Received caps through GDP: %s", string);
        g_free (string);

        if (!gst_pad_try_set_caps (pad, caps)) {
          g_warning ("Could not set caps");
          return NULL;
        }
      }

      /* now receive the buffer header */
      if (!(data = gst_tcpserversrc_gdp_read_header (src))) {
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("Could not read data header through GDP"));
        return NULL;
      }
      if (GST_IS_EVENT (data))
        return data;
      buf = GST_BUFFER (data);

      GST_LOG_OBJECT (src, "Going to read data from socket into buffer %p",
          buf);
      /* use this new buffer to read data into */
      readsize = GST_BUFFER_SIZE (buf);
      break;
    default:
      g_warning ("Unhandled protocol type");
      break;
  }

  GST_LOG_OBJECT (src, "Reading %d bytes", readsize);
  ret =
      gst_tcp_socket_read (src->client_sock_fd, GST_BUFFER_DATA (buf),
      readsize);
  if (ret < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return NULL;
  }

  /* if we read 0 bytes, and we're blocking, we hit eos */
  if (ret == 0) {
    GST_DEBUG ("blocking read returns 0, EOS");
    gst_buffer_unref (buf);
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  readsize = ret;
  GST_LOG_OBJECT (src, "Read %d bytes", readsize);
  GST_BUFFER_SIZE (buf) = readsize;
  GST_BUFFER_MAXSIZE (buf) = readsize;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_OFFSET_END (buf) = src->curoffset + readsize;
  src->curoffset += readsize;
  return GST_DATA (buf);
}


static void
gst_tcpserversrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSrc *tcpserversrc;

  g_return_if_fail (GST_IS_TCPSERVERSRC (object));
  tcpserversrc = GST_TCPSERVERSRC (object);

  switch (prop_id) {
    case ARG_PORT:
      tcpserversrc->server_port = g_value_get_int (value);
      break;
    case ARG_PROTOCOL:
      tcpserversrc->protocol = g_value_get_enum (value);
      break;
    case ARG_HOST:
      if (tcpserversrc->host)
        g_free (tcpserversrc->host);
      tcpserversrc->host = g_strdup (g_value_get_string (value));
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
    case ARG_PORT:
      g_value_set_int (value, tcpserversrc->server_port);
      break;
    case ARG_PROTOCOL:
      g_value_set_enum (value, tcpserversrc->protocol);
      break;
    case ARG_HOST:
      g_value_set_string (value, tcpserversrc->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* set up server */
static gboolean
gst_tcpserversrc_init_receive (GstTCPServerSrc * this)
{
  int ret;

  /* reset caps_received flag */
  this->caps_received = FALSE;

  /* create the server listener socket */
  if ((this->server_sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "opened receiving server socket with fd %d",
      this->server_sock_fd);

  /* make address reusable */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }

  /* name the socket */
  memset (&this->server_sin, 0, sizeof (this->server_sin));
  this->server_sin.sin_family = AF_INET;        /* network socket */
  this->server_sin.sin_port = htons (this->server_port);        /* on port */
  if (this->host) {
    gchar *host = gst_tcp_host_to_ip (GST_ELEMENT (this), this->host);

    if (!host)
      return FALSE;

    this->server_sin.sin_addr.s_addr = inet_addr (host);
    g_free (host);
  } else
    this->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);

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

  GST_DEBUG_OBJECT (this, "listening on server socket %d with queue of %d",
      this->server_sock_fd, TCP_BACKLOG);
  if (listen (this->server_sock_fd, TCP_BACKLOG) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }

  /* FIXME: maybe we should think about moving actual client accepting
     somewhere else */
  GST_DEBUG_OBJECT (this, "waiting for client");
  this->client_sock_fd =
      accept (this->server_sock_fd, (struct sockaddr *) &this->client_sin,
      &this->client_sin_len);
  if (this->client_sock_fd == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "received client");

  GST_FLAG_SET (this, GST_TCPSERVERSRC_OPEN);
  return TRUE;
}

static void
gst_tcpserversrc_close (GstTCPServerSrc * this)
{
  if (this->server_sock_fd != -1) {
    close (this->server_sock_fd);
    this->server_sock_fd = -1;
  }
  if (this->client_sock_fd != -1) {
    close (this->client_sock_fd);
    this->client_sock_fd = -1;
  }
  GST_FLAG_UNSET (this, GST_TCPSERVERSRC_OPEN);
}

static GstElementStateReturn
gst_tcpserversrc_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_TCPSERVERSRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_TCPSERVERSRC_OPEN))
      gst_tcpserversrc_close (GST_TCPSERVERSRC (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_TCPSERVERSRC_OPEN)) {
      if (!gst_tcpserversrc_init_receive (GST_TCPSERVERSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
