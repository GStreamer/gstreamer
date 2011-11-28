/*
 * GStreamer
 * Copyright (C) 2010 Jan Schmidt <thaytan@noraisin.net>
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
 * SECTION:element-rtmpsink
 *
 * This element delivers data to a streaming server via RTMP. It uses
 * librtmp, and supports any protocols/urls that librtmp supports.
 * The URL/location can contain extra connection or session parameters
 * for librtmp, such as 'flashver=version'. See the librtmp documentation
 * for more detail
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! ffenc_flv ! flvmux ! rtmpsink location='rtmp://localhost/path/to/stream live=1'
 * ]| Encode a test video stream to FLV video format and stream it via RTMP.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstrtmpsink.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_rtmp_sink_debug);
#define GST_CAT_DEFAULT gst_rtmp_sink_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LOCATION
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv")
    );

static void gst_rtmp_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_rtmp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtmp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtmp_sink_finalize (GObject * object);
static gboolean gst_rtmp_sink_stop (GstBaseSink * sink);
static gboolean gst_rtmp_sink_start (GstBaseSink * sink);
static GstFlowReturn gst_rtmp_sink_render (GstBaseSink * sink, GstBuffer * buf);

static void
_do_init (GType gtype)
{
  static const GInterfaceInfo urihandler_info = {
    gst_rtmp_sink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (gtype, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_rtmp_sink_debug, "rtmpsink", 0,
      "RTMP server element");
}

GST_BOILERPLATE_FULL (GstRTMPSink, gst_rtmp_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);


static void
gst_rtmp_sink_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "RTMP output sink",
      "Sink/Network", "Sends FLV content to a server via RTMP",
      "Jan Schmidt <thaytan@noraisin.net>");

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}

/* initialize the plugin's class */
static void
gst_rtmp_sink_class_init (GstRTMPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = gst_rtmp_sink_finalize;
  gobject_class->set_property = gst_rtmp_sink_set_property;
  gobject_class->get_property = gst_rtmp_sink_get_property;

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_rtmp_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_rtmp_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_rtmp_sink_render);

  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", PROP_LOCATION, G_PARAM_READWRITE, NULL);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_rtmp_sink_init (GstRTMPSink * sink, GstRTMPSinkClass * klass)
{
#ifdef G_OS_WIN32
  WSADATA wsa_data;

  if (WSAStartup (MAKEWORD (2, 2), &wsa_data) != 0) {
    GST_ERROR_OBJECT (sink, "WSAStartup failed: 0x%08x", WSAGetLastError ());
  }
#endif
}

static void
gst_rtmp_sink_finalize (GObject * object)
{
#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_rtmp_sink_start (GstBaseSink * basesink)
{
  GstRTMPSink *sink = GST_RTMP_SINK (basesink);

  if (!sink->uri) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Please set URI for RTMP output"), ("No URI set before starting"));
    return FALSE;
  }

  sink->rtmp_uri = g_strdup (sink->uri);
  sink->rtmp = RTMP_Alloc ();
  RTMP_Init (sink->rtmp);
  if (!RTMP_SetupURL (sink->rtmp, sink->rtmp_uri)) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Failed to setup URL '%s'", sink->uri));
    RTMP_Free (sink->rtmp);
    sink->rtmp = NULL;
    g_free (sink->rtmp_uri);
    sink->rtmp_uri = NULL;
    return FALSE;
  }

  GST_DEBUG_OBJECT (sink, "Created RTMP object");

  /* Mark this as an output connection */
  RTMP_EnableWrite (sink->rtmp);

  sink->first = TRUE;

  return TRUE;
}

static gboolean
gst_rtmp_sink_stop (GstBaseSink * basesink)
{
  GstRTMPSink *sink = GST_RTMP_SINK (basesink);

  gst_buffer_replace (&sink->cache, NULL);

  if (sink->rtmp) {
    RTMP_Close (sink->rtmp);
    RTMP_Free (sink->rtmp);
    sink->rtmp = NULL;
  }
  if (sink->rtmp_uri) {
    g_free (sink->rtmp_uri);
    sink->rtmp_uri = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_rtmp_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstRTMPSink *sink = GST_RTMP_SINK (bsink);
  GstBuffer *reffed_buf = NULL;

  if (sink->first) {
    /* open the connection */
    if (!RTMP_IsConnected (sink->rtmp)) {
      if (!RTMP_Connect (sink->rtmp, NULL)
          || !RTMP_ConnectStream (sink->rtmp, 0)) {
        GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
            ("Could not connect to RTMP stream \"%s\" for writing", sink->uri));
        RTMP_Free (sink->rtmp);
        sink->rtmp = NULL;
        g_free (sink->rtmp_uri);
        sink->rtmp_uri = NULL;
        return GST_FLOW_ERROR;
      }
      GST_DEBUG_OBJECT (sink, "Opened connection to %s", sink->rtmp_uri);
    }

    /* FIXME: Parse the first buffer and see if it contains a header plus a packet instead
     * of just assuming it's only the header */
    GST_LOG_OBJECT (sink, "Caching first buffer of size %d for concatenation",
        GST_BUFFER_SIZE (buf));
    gst_buffer_replace (&sink->cache, buf);
    sink->first = FALSE;
    return GST_FLOW_OK;
  }

  if (sink->cache) {
    GST_LOG_OBJECT (sink, "Joining 2nd buffer of size %d to cached buf",
        GST_BUFFER_SIZE (buf));
    gst_buffer_ref (buf);
    reffed_buf = buf = gst_buffer_join (sink->cache, buf);
    sink->cache = NULL;
  }

  GST_LOG_OBJECT (sink, "Sending %d bytes to RTMP server",
      GST_BUFFER_SIZE (buf));

  if (!RTMP_Write (sink->rtmp,
          (char *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf))) {
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL), ("Failed to write data"));
    if (reffed_buf)
      gst_buffer_unref (reffed_buf);
    return GST_FLOW_ERROR;
  }

  if (reffed_buf)
    gst_buffer_unref (reffed_buf);

  return GST_FLOW_OK;
}

/*
 * URI interface support.
 */
static GstURIType
gst_rtmp_sink_uri_get_type (void)
{
  return GST_URI_SINK;
}

static gchar **
gst_rtmp_sink_uri_get_protocols (void)
{
  static gchar *protocols[] =
      { (char *) "rtmp", (char *) "rtmpt", (char *) "rtmps", (char *) "rtmpe",
    (char *) "rtmfp", (char *) "rtmpte", (char *) "rtmpts", NULL
  };
  return protocols;
}

static const gchar *
gst_rtmp_sink_uri_get_uri (GstURIHandler * handler)
{
  GstRTMPSink *sink = GST_RTMP_SINK (handler);

  return sink->uri;
}

static gboolean
gst_rtmp_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTMPSink *sink = GST_RTMP_SINK (handler);

  if (GST_STATE (sink) >= GST_STATE_PAUSED)
    return FALSE;

  g_free (sink->uri);
  sink->uri = NULL;

  if (uri != NULL) {
    int protocol;
    AVal host;
    unsigned int port;
    AVal playpath, app;

    if (!RTMP_ParseURL (uri, &protocol, &host, &port, &playpath, &app) ||
        !host.av_len || !playpath.av_len) {
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
          ("Failed to parse URI %s", uri), (NULL));
      return FALSE;
    }
    sink->uri = g_strdup (uri);
  }

  GST_DEBUG_OBJECT (sink, "Changed URI to %s", GST_STR_NULL (uri));

  return TRUE;
}

static void
gst_rtmp_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtmp_sink_uri_get_type;
  iface->get_protocols = gst_rtmp_sink_uri_get_protocols;
  iface->get_uri = gst_rtmp_sink_uri_get_uri;
  iface->set_uri = gst_rtmp_sink_uri_set_uri;
}

static void
gst_rtmp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTMPSink *sink = GST_RTMP_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_rtmp_sink_uri_set_uri (GST_URI_HANDLER (sink),
          g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTMPSink *sink = GST_RTMP_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
