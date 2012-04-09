/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
 * Copyright (C) <2006> Joni Valtanen <joni.valtanen@movial.fi>
 * Copyright (C) <2012> Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstudp-marshal.h"
#include "gstdynudpsink.h"

#include <gst/net/gstnetaddressmeta.h>

GST_DEBUG_CATEGORY_STATIC (dynudpsink_debug);
#define GST_CAT_DEFAULT (dynudpsink_debug)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* DynUDPSink signals and args */
enum
{
  /* methods */
  SIGNAL_GET_STATS,

  /* signals */

  /* FILL ME */
  LAST_SIGNAL
};

#define UDP_DEFAULT_SOCKET		NULL
#define UDP_DEFAULT_CLOSE_SOCKET	TRUE

enum
{
  PROP_0,
  PROP_SOCKET,
  PROP_CLOSE_SOCKET
};

static void gst_dynudpsink_finalize (GObject * object);

static GstFlowReturn gst_dynudpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_dynudpsink_stop (GstBaseSink * bsink);
static gboolean gst_dynudpsink_start (GstBaseSink * bsink);
static gboolean gst_dynudpsink_unlock (GstBaseSink * bsink);
static gboolean gst_dynudpsink_unlock_stop (GstBaseSink * bsink);

static void gst_dynudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dynudpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStructure *gst_dynudpsink_get_stats (GstDynUDPSink * sink,
    const gchar * host, gint port);

static guint gst_dynudpsink_signals[LAST_SIGNAL] = { 0 };

#define gst_dynudpsink_parent_class parent_class
G_DEFINE_TYPE (GstDynUDPSink, gst_dynudpsink, GST_TYPE_BASE_SINK);

static void
gst_dynudpsink_class_init (GstDynUDPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_dynudpsink_set_property;
  gobject_class->get_property = gst_dynudpsink_get_property;
  gobject_class->finalize = gst_dynudpsink_finalize;

  gst_dynudpsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstDynUDPSinkClass, get_stats),
      NULL, NULL, gst_udp_marshal_BOXED__STRING_INT, GST_TYPE_STRUCTURE, 2,
      G_TYPE_STRING, G_TYPE_INT);

  g_object_class_install_property (gobject_class, PROP_SOCKET,
      g_param_spec_object ("socket", "Socket",
          "Socket to use for UDP sending. (NULL == allocate)",
          G_TYPE_SOCKET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOSE_SOCKET,
      g_param_spec_boolean ("close-socket", "Close socket",
          "Close socket if passed as property on state change",
          UDP_DEFAULT_CLOSE_SOCKET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "UDP packet sender",
      "Sink/Network",
      "Send data over the network via UDP",
      "Philippe Khalaf <burger@speedy.org>");

  gstbasesink_class->render = gst_dynudpsink_render;
  gstbasesink_class->start = gst_dynudpsink_start;
  gstbasesink_class->stop = gst_dynudpsink_stop;
  gstbasesink_class->unlock = gst_dynudpsink_unlock;
  gstbasesink_class->unlock_stop = gst_dynudpsink_unlock_stop;

  klass->get_stats = gst_dynudpsink_get_stats;

  GST_DEBUG_CATEGORY_INIT (dynudpsink_debug, "dynudpsink", 0, "UDP sink");
}

static void
gst_dynudpsink_init (GstDynUDPSink * sink)
{
  sink->socket = UDP_DEFAULT_SOCKET;
  sink->close_socket = UDP_DEFAULT_CLOSE_SOCKET;
  sink->external_socket = FALSE;

  sink->used_socket = NULL;
  sink->cancellable = g_cancellable_new ();
  sink->family = G_SOCKET_FAMILY_IPV6;
}

static void
gst_dynudpsink_finalize (GObject * object)
{
  GstDynUDPSink *sink;

  sink = GST_DYNUDPSINK (object);

  if (sink->cancellable)
    g_object_unref (sink->cancellable);
  sink->cancellable = NULL;

  if (sink->socket)
    g_object_unref (sink->socket);
  sink->socket = NULL;

  if (sink->used_socket)
    g_object_unref (sink->used_socket);
  sink->used_socket = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_dynudpsink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstDynUDPSink *sink;
  gssize ret;
  GstMapInfo map;
  GstNetAddressMeta *meta;
  GSocketAddress *addr;
  GError *err = NULL;
  GSocketFamily family;

  meta = gst_buffer_get_net_address_meta (buffer);

  if (meta == NULL) {
    GST_DEBUG ("Received buffer is not a GstNetBuffer, skipping");
    return GST_FLOW_OK;
  }

  sink = GST_DYNUDPSINK (bsink);

  /* let's get the address from the metadata */
  addr = meta->addr;

  family = g_socket_address_get_family (addr);
  if (sink->family != family && family != G_SOCKET_FAMILY_IPV4)
    goto invalid_family;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  GST_DEBUG ("about to send %" G_GSIZE_FORMAT " bytes", map.size);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *host;

    host =
        g_inet_address_to_string (g_inet_socket_address_get_address
        (G_INET_SOCKET_ADDRESS (addr)));
    GST_DEBUG ("sending %" G_GSIZE_FORMAT " bytes to client %s port %d",
        map.size, host,
        g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr)));
    g_free (host);
  }
#endif

  ret =
      g_socket_send_to (sink->used_socket, addr, (gchar *) map.data, map.size,
      sink->cancellable, &err);
  gst_buffer_unmap (buffer, &map);

  if (ret < 0)
    goto send_error;

  GST_DEBUG ("sent %" G_GSSIZE_FORMAT " bytes", ret);

  return GST_FLOW_OK;

send_error:
  {
    GST_DEBUG ("got send error %s", err->message);
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }
invalid_family:
  {
    GST_DEBUG ("invalid family (got %d, expected %d)",
        g_socket_address_get_family (addr), sink->family);
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
    case PROP_SOCKET:
      if (udpsink->socket != NULL && udpsink->socket != udpsink->used_socket &&
          udpsink->close_socket) {
        GError *err = NULL;

        if (!g_socket_close (udpsink->socket, &err)) {
          GST_ERROR ("failed to close socket %p: %s", udpsink->socket,
              err->message);
          g_clear_error (&err);
        }
      }
      if (udpsink->socket)
        g_object_unref (udpsink->socket);
      udpsink->socket = g_value_dup_object (value);
      GST_DEBUG ("setting socket to %p", udpsink->socket);
      break;
    case PROP_CLOSE_SOCKET:
      udpsink->close_socket = g_value_get_boolean (value);
      break;
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
    case PROP_SOCKET:
      g_value_set_object (value, udpsink->socket);
      break;
    case PROP_CLOSE_SOCKET:
      g_value_set_boolean (value, udpsink->close_socket);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_dynudpsink_start (GstBaseSink * bsink)
{
  GstDynUDPSink *udpsink;
  GError *err = NULL;

  udpsink = GST_DYNUDPSINK (bsink);

  if (udpsink->socket == NULL) {
    /* create sender socket if none available, first try IPv6, then
     * fall-back to IPv4 */
    udpsink->family = G_SOCKET_FAMILY_IPV6;
    if ((udpsink->used_socket =
            g_socket_new (G_SOCKET_FAMILY_IPV6,
                G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err)) == NULL) {
      udpsink->family = G_SOCKET_FAMILY_IPV4;
      if ((udpsink->used_socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
                  G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err)) == NULL)
        goto no_socket;
    }

    udpsink->external_socket = FALSE;
  } else {
    udpsink->used_socket = G_SOCKET (g_object_ref (udpsink->socket));
    udpsink->external_socket = TRUE;
    udpsink->family = g_socket_get_family (udpsink->used_socket);
  }

  g_socket_set_broadcast (udpsink->used_socket, TRUE);

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (udpsink, "Failed to create socket: %s", err->message);
    g_clear_error (&err);
    return FALSE;
  }
}

static GstStructure *
gst_dynudpsink_get_stats (GstDynUDPSink * sink, const gchar * host, gint port)
{
  return NULL;
}

static gboolean
gst_dynudpsink_stop (GstBaseSink * bsink)
{
  GstDynUDPSink *udpsink;

  udpsink = GST_DYNUDPSINK (bsink);

  if (udpsink->used_socket) {
    if (udpsink->close_socket || !udpsink->external_socket) {
      GError *err = NULL;

      if (!g_socket_close (udpsink->used_socket, &err)) {
        GST_ERROR_OBJECT (udpsink, "Failed to close socket: %s", err->message);
        g_clear_error (&err);
      }
    }

    g_object_unref (udpsink->used_socket);
    udpsink->used_socket = NULL;
  }

  return TRUE;
}

static gboolean
gst_dynudpsink_unlock (GstBaseSink * bsink)
{
  GstDynUDPSink *udpsink;

  udpsink = GST_DYNUDPSINK (bsink);

  g_cancellable_cancel (udpsink->cancellable);

  return TRUE;
}

static gboolean
gst_dynudpsink_unlock_stop (GstBaseSink * bsink)
{
  GstDynUDPSink *udpsink;

  udpsink = GST_DYNUDPSINK (bsink);

  g_cancellable_reset (udpsink->cancellable);

  return TRUE;
}
