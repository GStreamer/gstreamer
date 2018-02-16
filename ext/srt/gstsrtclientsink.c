/* GStreamer SRT plugin based on libsrt
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-srtserversink
 * @title: srtserversink
 *
 * srtserversink is a network sink that sends <ulink url="http://www.srtalliance.org/">SRT</ulink>
 * packets to the network. Although SRT is an UDP-based protocol, srtserversink works like
 * a server socket of connection-oriented protocol.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc ! srtserversink
 * ]| This pipeline shows how to serve SRT packets through the default port.

 * |[
 * gst-launch-1.0 -v audiotestsrc ! srtserversink uri=srt://192.168.1.10:8888/ rendez-vous=1
 * ]| This pipeline shows how to serve SRT packets to 192.168.1.10 port 8888 using the rendez-vous mode.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsrtclientsink.h"
#include "gstsrt.h"
#include <srt/srt.h>
#include <gio/gio.h>

#define SRT_DEFAULT_POLL_TIMEOUT -1

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_CAT_DEFAULT gst_debug_srt_client_sink
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstSRTClientSinkPrivate
{
  SRTSOCKET sock;
  GSocketAddress *sockaddr;
  gint poll_id;
  gint poll_timeout;

  gboolean rendez_vous;
  gchar *bind_address;
  guint16 bind_port;

  gboolean sent_headers;
};

#define GST_SRT_CLIENT_SINK_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_SRT_CLIENT_SINK, GstSRTClientSinkPrivate))

enum
{
  PROP_POLL_TIMEOUT = 1,
  PROP_BIND_ADDRESS,
  PROP_BIND_PORT,
  PROP_RENDEZ_VOUS,
  PROP_STATS,
  /*< private > */
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

#define gst_srt_client_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSRTClientSink, gst_srt_client_sink,
    GST_TYPE_SRT_BASE_SINK, G_ADD_PRIVATE (GstSRTClientSink)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtclientsink", 0,
        "SRT Client Sink"));

static void
gst_srt_client_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTClientSink *self = GST_SRT_CLIENT_SINK (object);
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      g_value_set_int (value, priv->poll_timeout);
      break;
    case PROP_BIND_PORT:
      g_value_set_int (value, priv->rendez_vous);
      break;
    case PROP_BIND_ADDRESS:
      g_value_set_string (value, priv->bind_address);
      break;
    case PROP_RENDEZ_VOUS:
      g_value_set_boolean (value, priv->bind_port);
      break;
    case PROP_STATS:
      g_value_take_boxed (value, gst_srt_base_sink_get_stats (priv->sockaddr,
              priv->sock));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_client_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTClientSink *self = GST_SRT_CLIENT_SINK (object);
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      priv->poll_timeout = g_value_get_int (value);
      break;
    case PROP_BIND_ADDRESS:
      g_free (priv->bind_address);
      priv->bind_address = g_value_dup_string (value);
      break;
    case PROP_BIND_PORT:
      priv->bind_port = g_value_get_int (value);
      break;
    case PROP_RENDEZ_VOUS:
      priv->rendez_vous = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_srt_client_sink_start (GstBaseSink * sink)
{
  GstSRTClientSink *self = GST_SRT_CLIENT_SINK (sink);
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);
  GstSRTBaseSink *base = GST_SRT_BASE_SINK (sink);
  GstUri *uri = gst_uri_ref (GST_SRT_BASE_SINK (self)->uri);

  priv->sock = gst_srt_client_connect_full (GST_ELEMENT (sink), FALSE,
      gst_uri_get_host (uri), gst_uri_get_port (uri), priv->rendez_vous,
      priv->bind_address, priv->bind_port, base->latency,
      &priv->sockaddr, &priv->poll_id, base->passphrase, base->key_length);

  g_clear_pointer (&uri, gst_uri_unref);

  return (priv->sock != SRT_INVALID_SOCK);
}

static gboolean
send_buffer_internal (GstSRTBaseSink * sink,
    const GstMapInfo * mapinfo, gpointer user_data)
{
  SRTSOCKET sock = GPOINTER_TO_INT (user_data);

  if (srt_sendmsg2 (sock, (char *) mapinfo->data, mapinfo->size,
          0) == SRT_ERROR) {
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, NULL,
        ("%s", srt_getlasterror_str ()));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_srt_client_sink_send_buffer (GstSRTBaseSink * sink,
    const GstMapInfo * mapinfo)
{
  GstSRTClientSink *self = GST_SRT_CLIENT_SINK (sink);
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);

  if (!priv->sent_headers) {
    if (!gst_srt_base_sink_send_headers (sink, send_buffer_internal,
            GINT_TO_POINTER (priv->sock)))
      return FALSE;

    priv->sent_headers = TRUE;
  }

  return send_buffer_internal (sink, mapinfo, GINT_TO_POINTER (priv->sock));
}

static gboolean
gst_srt_client_sink_stop (GstBaseSink * sink)
{
  GstSRTClientSink *self = GST_SRT_CLIENT_SINK (sink);
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);

  GST_DEBUG_OBJECT (self, "closing SRT connection");

  if (priv->poll_id != SRT_ERROR) {
    srt_epoll_remove_usock (priv->poll_id, priv->sock);
    srt_epoll_release (priv->poll_id);
    priv->poll_id = SRT_ERROR;
  }

  if (priv->sock != SRT_INVALID_SOCK) {
    srt_close (priv->sock);
    priv->sock = SRT_INVALID_SOCK;
  }

  g_clear_object (&priv->sockaddr);

  priv->sent_headers = FALSE;

  return GST_BASE_SINK_CLASS (parent_class)->stop (sink);
}

static void
gst_srt_client_sink_class_init (GstSRTClientSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstSRTBaseSinkClass *gstsrtbasesink_class = GST_SRT_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_srt_client_sink_set_property;
  gobject_class->get_property = gst_srt_client_sink_get_property;

  properties[PROP_POLL_TIMEOUT] =
      g_param_spec_int ("poll-timeout", "Poll Timeout",
      "Return poll wait after timeout miliseconds (-1 = infinite)", -1,
      G_MAXINT32, SRT_DEFAULT_POLL_TIMEOUT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_BIND_ADDRESS] =
      g_param_spec_string ("bind-address", "Bind Address",
      "Address to bind socket to (required for rendez-vous mode) ", NULL,
      G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS);

  properties[PROP_BIND_PORT] =
      g_param_spec_int ("bind-port", "Bind Port",
      "Port to bind socket to (Ignored in rendez-vous mode)", 0,
      G_MAXUINT16, 0,
      G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS);

  properties[PROP_RENDEZ_VOUS] =
      g_param_spec_boolean ("rendez-vous", "Rendez Vous",
      "Work in Rendez-Vous mode instead of client/caller mode", FALSE,
      G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY | G_PARAM_STATIC_STRINGS);

  properties[PROP_STATS] = g_param_spec_boxed ("stats", "Statistics",
      "SRT Statistics", GST_TYPE_STRUCTURE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_set_metadata (gstelement_class,
      "SRT client sink", "Sink/Network",
      "Send data over the network via SRT",
      "Justin Kim <justin.kim@collabora.com>");

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_srt_client_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_srt_client_sink_stop);

  gstsrtbasesink_class->send_buffer =
      GST_DEBUG_FUNCPTR (gst_srt_client_sink_send_buffer);
}

static void
gst_srt_client_sink_init (GstSRTClientSink * self)
{
  GstSRTClientSinkPrivate *priv = GST_SRT_CLIENT_SINK_GET_PRIVATE (self);
  priv->poll_timeout = SRT_DEFAULT_POLL_TIMEOUT;
}
