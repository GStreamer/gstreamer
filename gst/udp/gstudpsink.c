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
/**
 * SECTION:element-udpsink
 * @see_also: udpsrc, multifdsink
 *
 * udpsink is a network sink that sends UDP packets to the network.
 * It can be combined with RTP payloaders to implement RTP streaming.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v audiotestsrc ! udpsink
 * ]|
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstudpsink.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>

#define UDP_DEFAULT_HOST        "localhost"
#define UDP_DEFAULT_PORT        4951

/* UDPSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_URI,
  /* FILL ME */
};

static void gst_udpsink_finalize (GstUDPSink * udpsink);

static void gst_udpsink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_udpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/*static guint gst_udpsink_signals[LAST_SIGNAL] = { 0 }; */
#define gst_udpsink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstUDPSink, gst_udpsink, GST_TYPE_MULTIUDPSINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_udpsink_uri_handler_init));

static void
gst_udpsink_class_init (GstUDPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_udpsink_set_property;
  gobject_class->get_property = gst_udpsink_get_property;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_udpsink_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HOST,
      g_param_spec_string ("host", "host",
          "The host/IP/Multicast group to send the packets to",
          UDP_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, 65535, UDP_DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class, "UDP packet sender",
      "Sink/Network",
      "Send data over the network via UDP", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_udpsink_init (GstUDPSink * udpsink)
{
  gst_udp_uri_init (&udpsink->uri, UDP_DEFAULT_HOST, UDP_DEFAULT_PORT);

  gst_multiudpsink_add (GST_MULTIUDPSINK (udpsink), udpsink->uri.host,
      udpsink->uri.port);
}

static void
gst_udpsink_finalize (GstUDPSink * udpsink)
{
  gst_udp_uri_free (&udpsink->uri);
  g_free (udpsink->uristr);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) udpsink);
}

static gboolean
gst_udpsink_set_uri (GstUDPSink * sink, const gchar * uri)
{
  gst_multiudpsink_remove (GST_MULTIUDPSINK (sink), sink->uri.host,
      sink->uri.port);

  if (gst_udp_parse_uri (uri, &sink->uri) < 0)
    goto wrong_uri;

  gst_multiudpsink_add (GST_MULTIUDPSINK (sink), sink->uri.host,
      sink->uri.port);

  return TRUE;

  /* ERRORS */
wrong_uri:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("error parsing uri %s", uri));
    return FALSE;
  }
}

static void
gst_udpsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstUDPSink *udpsink;

  udpsink = GST_UDPSINK (object);

  /* remove old host */
  gst_multiudpsink_remove (GST_MULTIUDPSINK (udpsink),
      udpsink->uri.host, udpsink->uri.port);

  switch (prop_id) {
    case PROP_HOST:
    {
      const gchar *host;

      host = g_value_get_string (value);

      if (host)
        gst_udp_uri_update (&udpsink->uri, host, -1);
      else
        gst_udp_uri_update (&udpsink->uri, UDP_DEFAULT_HOST, -1);
      break;
    }
    case PROP_PORT:
      gst_udp_uri_update (&udpsink->uri, NULL, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  /* add new host */
  gst_multiudpsink_add (GST_MULTIUDPSINK (udpsink),
      udpsink->uri.host, udpsink->uri.port);
}

static void
gst_udpsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstUDPSink *udpsink;

  udpsink = GST_UDPSINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, udpsink->uri.host);
      break;
    case PROP_PORT:
      g_value_set_int (value, udpsink->uri.port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_udpsink_uri_get_type (void)
{
  return GST_URI_SINK;
}

static gchar **
gst_udpsink_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "udp", NULL };

  return protocols;
}

static const gchar *
gst_udpsink_uri_get_uri (GstURIHandler * handler)
{
  GstUDPSink *sink = GST_UDPSINK (handler);

  g_free (sink->uristr);
  sink->uristr = gst_udp_uri_string (&sink->uri);

  return sink->uristr;
}

static gboolean
gst_udpsink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gboolean ret;
  GstUDPSink *sink = GST_UDPSINK (handler);

  ret = gst_udpsink_set_uri (sink, uri);

  return ret;
}

static void
gst_udpsink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_udpsink_uri_get_type;
  iface->get_protocols = gst_udpsink_uri_get_protocols;
  iface->get_uri = gst_udpsink_uri_get_uri;
  iface->set_uri = gst_udpsink_uri_set_uri;
}
