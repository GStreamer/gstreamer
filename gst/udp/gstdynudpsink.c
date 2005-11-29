/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
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
#include "gstudp-marshal.h"
#include "gstdynudpsink.h"
#include <gst/netbuffer/gstnetbuffer.h>

GST_DEBUG_CATEGORY (dynudpsink_debug);
#define GST_CAT_DEFAULT (dynudpsink_debug)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* elementfactory information */
static GstElementDetails gst_dynudpsink_details =
GST_ELEMENT_DETAILS ("UDP packet sender",
    "Sink/Network",
    "Send data over the network via UDP",
    "Philippe Khalaf <burger@speedy.org>");

/* DynUDPSink signals and args */
enum
{
  /* methods */
  SIGNAL_GET_STATS,

  /* signals */

  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  /* FILL ME */
};

static void gst_dynudpsink_base_init (gpointer g_class);
static void gst_dynudpsink_class_init (GstDynUDPSink * klass);
static void gst_dynudpsink_init (GstDynUDPSink * udpsink);
static void gst_dynudpsink_finalize (GObject * object);

static void gst_dynudpsink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_dynudpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static void gst_dynudpsink_close (GstDynUDPSink * sink);
static GstStateChangeReturn gst_dynudpsink_change_state (GstElement * element,
    GstStateChange transition);

static void gst_dynudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dynudpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

static guint gst_dynudpsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_dynudpsink_get_type (void)
{
  static GType dynudpsink_type = 0;

  if (!dynudpsink_type) {
    static const GTypeInfo dynudpsink_info = {
      sizeof (GstDynUDPSinkClass),
      gst_dynudpsink_base_init,
      NULL,
      (GClassInitFunc) gst_dynudpsink_class_init,
      NULL,
      NULL,
      sizeof (GstDynUDPSink),
      0,
      (GInstanceInitFunc) gst_dynudpsink_init,
      NULL
    };

    dynudpsink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstDynUDPSink",
        &dynudpsink_info, 0);
  }
  return dynudpsink_type;
}

static void
gst_dynudpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_dynudpsink_details);
}

static void
gst_dynudpsink_class_init (GstDynUDPSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SINK);

  gobject_class->set_property = gst_dynudpsink_set_property;
  gobject_class->get_property = gst_dynudpsink_get_property;
  gobject_class->finalize = gst_dynudpsink_finalize;

  gst_dynudpsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDynUDPSinkClass, get_stats),
      NULL, NULL, gst_udp_marshal_BOXED__STRING_INT, G_TYPE_VALUE_ARRAY, 2,
      G_TYPE_STRING, G_TYPE_INT);

  gstelement_class->change_state = gst_dynudpsink_change_state;

  gstbasesink_class->get_times = gst_dynudpsink_get_times;
  gstbasesink_class->render = gst_dynudpsink_render;

  GST_DEBUG_CATEGORY_INIT (dynudpsink_debug, "dynudpsink", 0, "UDP sink");
}


static void
gst_dynudpsink_init (GstDynUDPSink * sink)
{
}

static void
gst_dynudpsink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dynudpsink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_BUFFER_TIMESTAMP (buffer);
  *end = *start + GST_BUFFER_DURATION (buffer);
}

static GstFlowReturn
gst_dynudpsink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstDynUDPSink *sink;
  gint ret, size;
  guint8 *data;
  GstNetBuffer *netbuf;
  struct sockaddr_in theiraddr;

  memset (&theiraddr, 0, sizeof (theiraddr));

  if (GST_IS_NETBUFFER (buffer)) {
    netbuf = GST_NETBUFFER (buffer);
  } else {
    GST_DEBUG ("Received buffer is not a GstNetBuffer, skipping");
    return GST_FLOW_OK;
  }

  sink = GST_DYNUDPSINK (bsink);

  size = GST_BUFFER_SIZE (netbuf);
  data = GST_BUFFER_DATA (netbuf);

  GST_DEBUG ("about to send %d bytes", size);
  guint16 destport;
  guint32 destaddr;

  // let's get the address from the netbuffer
  gst_netaddress_get_ip4_address (&netbuf->to, &destaddr, &destport);

  GST_DEBUG ("sending %d bytes to client %d port %d", size, destaddr, destport);

  theiraddr.sin_family = AF_INET;
  theiraddr.sin_addr.s_addr = destaddr;
  theiraddr.sin_port = destport;
  ret = sendto (sink->sock, data, size, 0,
      (struct sockaddr *) &theiraddr, sizeof (theiraddr));

  if (ret < 0) {
    if (errno != EINTR && errno != EAGAIN) {
      goto send_error;
    }
  }

  GST_DEBUG ("sent %d bytes", size);

  return GST_FLOW_OK;

send_error:
  {
    GST_DEBUG ("got send error %s (%d)", g_strerror (errno), errno);
    return GST_FLOW_ERROR;
  }
}

static void
gst_dynudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDynUDPSink *udpsink;

  udpsink = GST_DYNUDPSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dynudpsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDynUDPSink *udpsink;

  udpsink = GST_DYNUDPSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_dynudpsink_init_send (GstDynUDPSink * sink)
{
  guint bc_val;
  gint ret;

  /* create sender socket */
  if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
    goto no_socket;

  bc_val = 1;
  if ((ret =
          setsockopt (sink->sock, SOL_SOCKET, SO_BROADCAST, &bc_val,
              sizeof (bc_val))) < 0)
    goto no_broadcast;

  return TRUE;

  /* ERRORS */
no_socket:
  {
    perror ("socket");
    return FALSE;
  }
no_broadcast:
  {
    perror ("setsockopt");
    return FALSE;
  }
}

GValueArray *
gst_dynudpsink_get_stats (GstDynUDPSink * sink, const gchar * host, gint port)
{
  return NULL;
}

static void
gst_dynudpsink_close (GstDynUDPSink * sink)
{
  close (sink->sock);
}

static GstStateChangeReturn
gst_dynudpsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDynUDPSink *sink;

  sink = GST_DYNUDPSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_dynudpsink_init_send (sink))
        goto no_init;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_dynudpsink_close (sink);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
no_init:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}
