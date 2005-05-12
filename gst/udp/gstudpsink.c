/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "gstudpsink.h"

#define UDP_DEFAULT_HOST	"localhost"
#define UDP_DEFAULT_PORT	4951

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* elementfactory information */
static GstElementDetails gst_udpsink_details =
GST_ELEMENT_DETAILS ("UDP packet sender",
    "Sink/Network",
    "Send data over the network via UDP",
    "Wim Taymans <wim.taymans@chello.be>");

/* UDPSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  /* FILL ME */
};

static void gst_udpsink_base_init (gpointer g_class);
static void gst_udpsink_class_init (GstUDPSink * klass);
static void gst_udpsink_init (GstUDPSink * udpsink);

static void gst_udpsink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_udpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstElementStateReturn gst_udpsink_change_state (GstElement * element);

static void gst_udpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_udpsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsink_get_type (void)
{
  static GType udpsink_type = 0;

  if (!udpsink_type) {
    static const GTypeInfo udpsink_info = {
      sizeof (GstUDPSinkClass),
      gst_udpsink_base_init,
      NULL,
      (GClassInitFunc) gst_udpsink_class_init,
      NULL,
      NULL,
      sizeof (GstUDPSink),
      0,
      (GInstanceInitFunc) gst_udpsink_init,
      NULL
    };

    udpsink_type =
        g_type_register_static (GST_TYPE_BASESINK, "GstUDPSink", &udpsink_info,
        0);
  }
  return udpsink_type;
}

static void
gst_udpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_udpsink_details);
}

static void
gst_udpsink_class_init (GstUDPSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASESINK);

  gobject_class->set_property = gst_udpsink_set_property;
  gobject_class->get_property = gst_udpsink_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "host",
          "The host/IP/Multicast group to send the packets to",
          UDP_DEFAULT_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, 32768, UDP_DEFAULT_PORT, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_udpsink_change_state;

  gstbasesink_class->get_times = gst_udpsink_get_times;
  gstbasesink_class->render = gst_udpsink_render;
}


static void
gst_udpsink_init (GstUDPSink * udpsink)
{
  udpsink->host = g_strdup (UDP_DEFAULT_HOST);
  udpsink->port = UDP_DEFAULT_PORT;
}

static void
gst_udpsink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_BUFFER_TIMESTAMP (buffer);
  *end = *start + GST_BUFFER_DURATION (buffer);
}

static GstFlowReturn
gst_udpsink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstUDPSink *udpsink;
  gint ret, size;
  guint8 *data;

  udpsink = GST_UDPSINK (sink);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  while (TRUE) {
    ret = sendto (udpsink->sock, data, size, 0,
        (struct sockaddr *) &udpsink->theiraddr, sizeof (udpsink->theiraddr));

    if (ret < 0) {
      if (errno != EINTR && errno != EAGAIN)
        goto send_error;
    } else
      break;
  }
  return GST_FLOW_OK;

send_error:
  {
    GST_DEBUG ("got send error");
    return GST_FLOW_ERROR;
  }
}

static void
gst_udpsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstUDPSink *udpsink;

  udpsink = GST_UDPSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      if (udpsink->host != NULL)
        g_free (udpsink->host);
      if (g_value_get_string (value) == NULL)
        udpsink->host = NULL;
      else
        udpsink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      udpsink->port = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_udpsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstUDPSink *udpsink;

  udpsink = GST_UDPSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, udpsink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, udpsink->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_udpsink_init_send (GstUDPSink * sink)
{
  struct hostent *he;
  struct in_addr addr;
  guint bc_val;

  memset (&sink->theiraddr, 0, sizeof (sink->theiraddr));
  sink->theiraddr.sin_family = AF_INET; /* host byte order */
  sink->theiraddr.sin_port = htons (sink->port);        /* short, network byte order */

  /* if its an IP address */
  if (inet_aton (sink->host, &addr)) {
    /* check if its a multicast address */
    if ((ntohl (addr.s_addr) & 0xe0000000) == 0xe0000000) {
      sink->multi_addr.imr_multiaddr.s_addr = addr.s_addr;
      sink->multi_addr.imr_interface.s_addr = INADDR_ANY;

      sink->theiraddr.sin_addr = sink->multi_addr.imr_multiaddr;

      /* Joining the multicast group */
      setsockopt (sink->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &sink->multi_addr,
          sizeof (sink->multi_addr));
    }

    else {
      sink->theiraddr.sin_addr = *((struct in_addr *) &addr);
    }
  }

  /* we dont need to lookup for localhost */
  else if (strcmp (sink->host, UDP_DEFAULT_HOST) == 0 &&
      inet_aton ("127.0.0.1", &addr)) {
    sink->theiraddr.sin_addr = *((struct in_addr *) &addr);
  }

  /* if its a hostname */
  else if ((he = gethostbyname (sink->host))) {
    sink->theiraddr.sin_addr = *((struct in_addr *) he->h_addr);
  }

  else {
    perror ("hostname lookup error?");
    return FALSE;
  }

  if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror ("socket");
    return FALSE;
  }

  bc_val = 1;
  setsockopt (sink->sock, SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));

  return TRUE;
}

static void
gst_udpsink_close (GstUDPSink * sink)
{
  close (sink->sock);
}

static GstElementStateReturn
gst_udpsink_change_state (GstElement * element)
{
  GstElementStateReturn ret;
  GstUDPSink *sink;
  gint transition;

  sink = GST_UDPSINK (element);
  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_READY_TO_PAUSED:
      if (!gst_udpsink_init_send (sink))
        goto no_init;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      gst_udpsink_close (sink);
      break;
    default:
      break;
  }

  return ret;

no_init:
  {
    return GST_STATE_FAILURE;
  }
}
