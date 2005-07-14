/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include "gstudpsrc.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <gst/net/gstnetbuffer.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

GST_DEBUG_CATEGORY (udpsrc_debug);
#define GST_CAT_DEFAULT (udpsrc_debug)

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unblock or restart the select call */
#define CONTROL_RESTART        'R'      /* restart the select call */
#define CONTROL_STOP           'S'      /* stop the select call */
#define CONTROL_SOCKETS(src)   src->control_sock
#define WRITE_SOCKET(src)      src->control_sock[1]
#define READ_SOCKET(src)       src->control_sock[0]

#define SEND_COMMAND(src, command)             	\
G_STMT_START {                                 	\
  unsigned char c; c = command;                	\
  write (WRITE_SOCKET(src), &c, 1);         	\
} G_STMT_END

#define READ_COMMAND(src, command, res)        	\
G_STMT_START {                                	\
  res = read(READ_SOCKET(src), &command, 1);    \
} G_STMT_END

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* elementfactory information */
static GstElementDetails gst_udpsrc_details =
GST_ELEMENT_DETAILS ("UDP packet receiver",
    "Source/Network",
    "Receive data over the network via UDP",
    "Wim Taymans <wim@fluendo.com>");

/* UDPSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define UDP_DEFAULT_PORT		4951
#define UDP_DEFAULT_MULTICAST_GROUP	"0.0.0.0"
#define UDP_DEFAULT_URI			"udp://0.0.0.0:4951"

enum
{
  PROP_0,
  PROP_PORT,
  PROP_MULTICAST_GROUP,
  PROP_URI,
  /* FILL ME */
};

static void gst_udpsrc_base_init (gpointer g_class);
static void gst_udpsrc_class_init (GstUDPSrc * klass);
static void gst_udpsrc_init (GstUDPSrc * udpsrc);

static void gst_udpsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstFlowReturn gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf);
static gboolean gst_udpsrc_start (GstBaseSrc * bsrc);
static gboolean gst_udpsrc_stop (GstBaseSrc * bsrc);
static gboolean gst_udpsrc_unlock (GstBaseSrc * bsrc);

static void gst_udpsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_udpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsrc_get_type (void)
{
  static GType udpsrc_type = 0;

  if (!udpsrc_type) {
    static const GTypeInfo udpsrc_info = {
      sizeof (GstUDPSrcClass),
      gst_udpsrc_base_init,
      NULL,
      (GClassInitFunc) gst_udpsrc_class_init,
      NULL,
      NULL,
      sizeof (GstUDPSrc),
      0,
      (GInstanceInitFunc) gst_udpsrc_init,
      NULL
    };
    static const GInterfaceInfo urihandler_info = {
      gst_udpsrc_uri_handler_init,
      NULL,
      NULL
    };

    udpsrc_type =
        g_type_register_static (GST_TYPE_PUSH_SRC, "GstUDPSrc", &udpsrc_info,
        0);

    g_type_add_interface_static (udpsrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return udpsrc_type;
}

static void
gst_udpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_udpsrc_details);
}

static void
gst_udpsrc_class_init (GstUDPSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port to receive the packets from, 0=allocate", 0, 32768,
          UDP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MULTICAST_GROUP,
      g_param_spec_string ("multicast_group", "multicast_group",
          "The Address of multicast group to join", UDP_DEFAULT_MULTICAST_GROUP,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI in the form of udp://hostname:port", UDP_DEFAULT_URI,
          G_PARAM_READWRITE));

  gstbasesrc_class->start = gst_udpsrc_start;
  gstbasesrc_class->stop = gst_udpsrc_stop;
  gstbasesrc_class->unlock = gst_udpsrc_unlock;

  gstpushsrc_class->create = gst_udpsrc_create;

  GST_DEBUG_CATEGORY_INIT (udpsrc_debug, "udpsrc", 0, "UDP src");
}

static void
gst_udpsrc_init (GstUDPSrc * udpsrc)
{
  gst_base_src_set_live (GST_BASE_SRC (udpsrc), TRUE);
  udpsrc->port = UDP_DEFAULT_PORT;
  udpsrc->sock = -1;
  udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
  udpsrc->uri = g_strdup (UDP_DEFAULT_URI);
}

static GstFlowReturn
gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstUDPSrc *udpsrc;
  GstNetBuffer *outbuf;
  struct sockaddr_in tmpaddr;
  socklen_t len;
  fd_set read_fds;
  guint max_sock;
  gchar *pktdata;
  gint pktsize;
  gint readsize;
  gint ret;
  gboolean try_again;

  udpsrc = GST_UDPSRC (psrc);

  FD_ZERO (&read_fds);
  FD_SET (udpsrc->sock, &read_fds);
  FD_SET (READ_SOCKET (udpsrc), &read_fds);
  max_sock = MAX (udpsrc->sock, READ_SOCKET (udpsrc));

  do {
    gboolean stop;

    try_again = FALSE;
    stop = FALSE;

    GST_LOG_OBJECT (udpsrc, "doing select");
    ret = select (max_sock + 1, &read_fds, NULL, NULL, NULL);
    GST_LOG_OBJECT (udpsrc, "select returned %d", ret);
    if (ret <= 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto select_error;
      else
        try_again = TRUE;
    } else {
      /* got control message */
      if (FD_ISSET (READ_SOCKET (udpsrc), &read_fds)) {
        while (TRUE) {
          gchar command;
          int res;

          READ_COMMAND (udpsrc, command, res);
          if (res < 0) {
            GST_LOG_OBJECT (udpsrc, "no more commands");
            /* no more commands */
            break;
          }

          switch (command) {
            case CONTROL_STOP:
              /* break out of the select loop */
              GST_LOG_OBJECT (udpsrc, "stop");
              /* stop this function */
              stop = TRUE;
              break;
            default:
              GST_WARNING_OBJECT (udpsrc, "unkown");
              g_warning ("multiudpsink: unknown control message received");
              break;
          }
        }
      }
    }
    if (stop)
      goto stopped;
  } while (try_again);

  /* ask how much is available for reading on the socket */
  if ((ret = ioctl (udpsrc->sock, FIONREAD, &readsize)) < 0)
    goto ioctl_failed;

  GST_LOG_OBJECT (udpsrc, "ioctl says %d bytes available", readsize);

  pktdata = g_malloc (readsize);
  pktsize = readsize;

  len = sizeof (struct sockaddr);
  while (TRUE) {
    ret = recvfrom (udpsrc->sock, pktdata, pktsize,
        0, (struct sockaddr *) &tmpaddr, &len);
    if (ret < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto receive_error;
    } else
      break;
  }

  outbuf = gst_netbuffer_new ();
  GST_BUFFER_DATA (outbuf) = (guint8 *) pktdata;
  GST_BUFFER_MALLOCDATA (outbuf) = (guint8 *) pktdata;
  GST_BUFFER_SIZE (outbuf) = ret;

  gst_netaddress_set_ip4_address (&outbuf->from, tmpaddr.sin_addr.s_addr,
      tmpaddr.sin_port);

  GST_LOG_OBJECT (udpsrc, "read %d bytes", readsize);

  *buf = GST_BUFFER (outbuf);

  return GST_FLOW_OK;

select_error:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("select error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG ("stop called");
    return GST_FLOW_WRONG_STATE;
  }
ioctl_failed:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("ioctl failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
receive_error:
  {
    g_free (pktdata);
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("receive error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_udpsrc_set_uri (GstUDPSrc * src, const gchar * uri)
{
  gchar *protocol;
  gchar *location;
  gchar *colptr;

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "udp") != 0)
    goto wrong_protocol;
  g_free (protocol);

  location = gst_uri_get_location (uri);
  colptr = strstr (location, ":");
  if (colptr != NULL) {
    src->port = atoi (colptr + 1);
  }
  g_free (location);
  g_free (src->uri);

  src->uri = g_strdup (uri);

  return TRUE;

wrong_protocol:
  {
    g_free (protocol);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("error parsing uri %s: wrong protocol", uri));
    return FALSE;
  }
}

static void
gst_udpsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case PROP_PORT:
      udpsrc->port = g_value_get_int (value);
      break;
    case PROP_MULTICAST_GROUP:
      g_free (udpsrc->multi_group);

      if (g_value_get_string (value) == NULL)
        udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
      else
        udpsrc->multi_group = g_strdup (g_value_get_string (value));

      break;
    case PROP_URI:
      gst_udpsrc_set_uri (udpsrc, g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_udpsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_int (value, udpsrc->port);
      break;
    case PROP_MULTICAST_GROUP:
      g_value_set_string (value, udpsrc->multi_group);
      break;
    case PROP_URI:
      g_value_set_string (value, udpsrc->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_udpsrc_start (GstBaseSrc * bsrc)
{
  guint bc_val;
  gint reuse;
  struct sockaddr_in my_addr;
  guint len;
  int port;
  GstUDPSrc *src;
  gint ret;

  src = GST_UDPSRC (bsrc);

  if ((ret = socketpair (PF_UNIX, SOCK_STREAM, 0, CONTROL_SOCKETS (src))) < 0)
    goto no_socket_pair;

  fcntl (READ_SOCKET (src), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (src), F_SETFL, O_NONBLOCK);

  if ((ret = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    goto no_socket;

  src->sock = ret;

  reuse = 1;
  if ((ret =
          setsockopt (src->sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
              sizeof (reuse))) < 0)
    goto setsockopt_error;

  memset (&src->myaddr, 0, sizeof (src->myaddr));
  src->myaddr.sin_family = AF_INET;     /* host byte order */
  src->myaddr.sin_port = htons (src->port);     /* short, network byte order */
  src->myaddr.sin_addr.s_addr = INADDR_ANY;

  if ((ret =
          bind (src->sock, (struct sockaddr *) &src->myaddr,
              sizeof (src->myaddr))) < 0)
    goto bind_error;

  if (inet_aton (src->multi_group, &(src->multi_addr.imr_multiaddr))) {
    if (src->multi_addr.imr_multiaddr.s_addr) {
      src->multi_addr.imr_interface.s_addr = INADDR_ANY;
      if ((ret =
              setsockopt (src->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                  &src->multi_addr, sizeof (src->multi_addr))) < 0)
        goto membership;
    }
  }

  len = sizeof (my_addr);
  if ((ret = getsockname (src->sock, (struct sockaddr *) &my_addr, &len)) < 0)
    goto getsockname_error;

  port = ntohs (my_addr.sin_port);
  if (port != src->port) {
    src->port = port;
    g_object_notify (G_OBJECT (src), "port");
  }

  bc_val = 1;
  if ((ret =
          setsockopt (src->sock, SOL_SOCKET, SO_BROADCAST, &bc_val,
              sizeof (bc_val))) < 0)
    goto no_broadcast;

  src->myaddr.sin_port = htons (src->port + 1);

  return TRUE;

  /* ERRORS */
no_socket_pair:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("no socket pair %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
no_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("no socket error %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
setsockopt_error:
  {
    close (src->sock);
    src->sock = -1;
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("setsockopt failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
bind_error:
  {
    close (src->sock);
    src->sock = -1;
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("bind failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
membership:
  {
    close (src->sock);
    src->sock = -1;
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("could add membership %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
getsockname_error:
  {
    close (src->sock);
    src->sock = -1;
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("getsockname failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
no_broadcast:
  {
    close (src->sock);
    src->sock = -1;
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("could not configure socket for broadcast %d: %s (%d)", ret,
            g_strerror (errno), errno));
    return FALSE;
  }
}

static gboolean
gst_udpsrc_unlock (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_DEBUG ("sending stop command");
  SEND_COMMAND (src, CONTROL_STOP);

  return TRUE;
}

static gboolean
gst_udpsrc_stop (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  if (src->sock != -1) {
    close (src->sock);
    src->sock = -1;
  }
  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_udpsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_udpsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "udp", NULL };

  return protocols;
}

static const gchar *
gst_udpsrc_uri_get_uri (GstURIHandler * handler)
{
  GstUDPSrc *src = GST_UDPSRC (handler);

  return g_strdup (src->uri);
}

static gboolean
gst_udpsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gboolean ret;
  GstUDPSrc *src = GST_UDPSRC (handler);

  ret = gst_udpsrc_set_uri (src, uri);

  return ret;
}

static void
gst_udpsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_udpsrc_uri_get_type;
  iface->get_protocols = gst_udpsrc_uri_get_protocols;
  iface->get_uri = gst_udpsrc_uri_get_uri;
  iface->set_uri = gst_udpsrc_uri_set_uri;
}
