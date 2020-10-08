/* GStreamer
 * Copyright (C) 2018, Collabora Ltd.
 * Copyright (C) 2018, SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
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
 * SECTION:element-srtsink
 * @title: srtsink
 *
 * srtsink is a network sink that sends [SRT](http://www.srtalliance.org/)
 * packets to the network.
 *
 * ## Examples</title>
 *
 * |[
 * gst-launch-1.0 -v audiotestsrc ! srtsink uri=srt://host
 * ]| This pipeline shows how to serve SRT packets through the default port.
 *
 * |[
 * gst-launch-1.0 -v audiotestsrc ! srtsink uri=srt://:port
 * ]| This pipeline shows how to wait SRT callers.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstsrtsink.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_CAT_DEFAULT gst_debug_srt_sink
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

enum
{
  SIG_CALLER_ADDED,
  SIG_CALLER_REMOVED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gst_srt_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static gchar *gst_srt_sink_uri_get_uri (GstURIHandler * handler);
static gboolean gst_srt_sink_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);

#define gst_srt_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSRTSink, gst_srt_sink,
    GST_TYPE_BASE_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_srt_sink_uri_handler_init)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtsink", 0, "SRT Sink"));

static void
gst_srt_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTSink *self = GST_SRT_SINK (object);

  if (!gst_srt_object_set_property_helper (self->srtobject, prop_id, value,
          pspec)) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_srt_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTSink *self = GST_SRT_SINK (object);

  if (!gst_srt_object_get_property_helper (self->srtobject, prop_id, value,
          pspec)) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_srt_sink_finalize (GObject * object)
{
  GstSRTSink *self = GST_SRT_SINK (object);

  g_clear_object (&self->cancellable);
  gst_srt_object_destroy (self->srtobject);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_srt_sink_init (GstSRTSink * self)
{
  self->srtobject = gst_srt_object_new (GST_ELEMENT (self));
  self->cancellable = g_cancellable_new ();

  gst_srt_object_set_uri (self->srtobject, GST_SRT_DEFAULT_URI, NULL);
}

static gboolean
gst_srt_sink_start (GstBaseSink * bsink)
{
  GstSRTSink *self = GST_SRT_SINK (bsink);
  GstSRTConnectionMode connection_mode = GST_SRT_CONNECTION_MODE_NONE;

  GError *error = NULL;
  gboolean ret = FALSE;

  gst_structure_get_enum (self->srtobject->parameters, "mode",
      GST_TYPE_SRT_CONNECTION_MODE, (gint *) & connection_mode);

  ret = gst_srt_object_open (self->srtobject, self->cancellable, &error);

  if (!ret) {
    /* ensure error is posted since state change will fail */
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE, (NULL),
        ("Failed to open SRT: %s", error->message));
    g_clear_error (&error);
  }

  return ret;
}

static gboolean
gst_srt_sink_stop (GstBaseSink * bsink)
{
  GstSRTSink *self = GST_SRT_SINK (bsink);

  gst_srt_object_close (self->srtobject);

  return TRUE;
}

static GstFlowReturn
gst_srt_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstSRTSink *self = GST_SRT_SINK (sink);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo info;
  GError *error = NULL;

  if (g_cancellable_is_cancelled (self->cancellable)) {
    ret = GST_FLOW_FLUSHING;
  }

  if (self->headers && GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_HEADER)) {
    GST_DEBUG_OBJECT (self, "Have streamheaders,"
        " ignoring header %" GST_PTR_FORMAT, buffer);
    return GST_FLOW_OK;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ,
        ("Could not map the input stream"), (NULL));
    return GST_FLOW_ERROR;
  }

  if (gst_srt_object_write (self->srtobject, self->headers, &info,
          self->cancellable, &error) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to write to SRT socket: %s",
            error ? error->message : "Unknown error"), (NULL));
    g_clear_error (&error);
    ret = GST_FLOW_ERROR;
  }

  gst_buffer_unmap (buffer, &info);

  GST_TRACE_OBJECT (self, "sending buffer %p, offset %"
      G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT
      ", timestamp %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT,
      buffer, GST_BUFFER_OFFSET (buffer),
      GST_BUFFER_OFFSET_END (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      gst_buffer_get_size (buffer));

  return ret;
}

static gboolean
gst_srt_sink_unlock (GstBaseSink * bsink)
{
  GstSRTSink *self = GST_SRT_SINK (bsink);

  gst_srt_object_wakeup (self->srtobject, self->cancellable);

  return TRUE;
}

static gboolean
gst_srt_sink_unlock_stop (GstBaseSink * bsink)
{
  GstSRTSink *self = GST_SRT_SINK (bsink);

  g_cancellable_reset (self->cancellable);

  return TRUE;
}

static gboolean
gst_srt_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstSRTSink *self = GST_SRT_SINK (bsink);
  GstStructure *s;
  const GValue *streamheader;

  GST_DEBUG_OBJECT (self, "setcaps %" GST_PTR_FORMAT, caps);

  g_clear_pointer (&self->headers, gst_buffer_list_unref);

  s = gst_caps_get_structure (caps, 0);
  streamheader = gst_structure_get_value (s, "streamheader");

  if (!streamheader) {
    GST_DEBUG_OBJECT (self, "'streamheader' field not present");
  } else if (GST_VALUE_HOLDS_BUFFER (streamheader)) {
    GST_DEBUG_OBJECT (self, "'streamheader' field holds buffer");
    self->headers = gst_buffer_list_new_sized (1);
    gst_buffer_list_add (self->headers, g_value_dup_boxed (streamheader));
  } else if (GST_VALUE_HOLDS_ARRAY (streamheader)) {
    guint i, size;

    GST_DEBUG_OBJECT (self, "'streamheader' field holds array");

    size = gst_value_array_get_size (streamheader);
    self->headers = gst_buffer_list_new_sized (size);

    for (i = 0; i < size; i++) {
      const GValue *v = gst_value_array_get_value (streamheader, i);
      if (!GST_VALUE_HOLDS_BUFFER (v)) {
        GST_ERROR_OBJECT (self, "'streamheader' item of unexpected type '%s'",
            G_VALUE_TYPE_NAME (v));
        return FALSE;
      }

      gst_buffer_list_add (self->headers, g_value_dup_boxed (v));
    }
  } else {
    GST_ERROR_OBJECT (self, "'streamheader' field has unexpected type '%s'",
        G_VALUE_TYPE_NAME (streamheader));
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Collected streamheaders: %u buffers",
      self->headers ? gst_buffer_list_length (self->headers) : 0);

  return TRUE;
}

static void
gst_srt_sink_class_init (GstSRTSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_srt_sink_set_property;
  gobject_class->get_property = gst_srt_sink_get_property;
  gobject_class->finalize = gst_srt_sink_finalize;

  /**
   * GstSRTSink::caller-added:
   * @gstsrtsink: the srtsink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtsink
   * @addr: the #GSocketAddress that describes the @sock
   *
   * The given socket descriptor was added to srtsink.
   */
  signals[SIG_CALLER_ADDED] =
      g_signal_new ("caller-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTSinkClass, caller_added),
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  /**
   * GstSRTSink::caller-removed:
   * @gstsrtsink: the srtsink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtsink
   * @addr: the #GSocketAddress that describes the @sock
   *
   * The given socket descriptor was removed from srtsink.
   */
  signals[SIG_CALLER_REMOVED] =
      g_signal_new ("caller-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTSinkClass,
          caller_added), NULL, NULL, NULL, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  gst_srt_object_install_properties_helper (gobject_class);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_set_metadata (gstelement_class,
      "SRT sink", "Sink/Network",
      "Send data over the network via SRT",
      "Justin Kim <justin.joy.9to5@gmail.com>");

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_srt_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_srt_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_srt_sink_render);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_srt_sink_unlock);
  gstbasesink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_srt_sink_unlock_stop);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_srt_sink_set_caps);

}

static GstURIType
gst_srt_sink_uri_get_type (GType type)
{
  return GST_URI_SINK;
}

static const gchar *const *
gst_srt_sink_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { GST_SRT_DEFAULT_URI_SCHEME, NULL };

  return protocols;
}

static gchar *
gst_srt_sink_uri_get_uri (GstURIHandler * handler)
{
  gchar *uri_str;
  GstSRTSink *self = GST_SRT_SINK (handler);

  GST_OBJECT_LOCK (self);
  uri_str = gst_uri_to_string (self->srtobject->uri);
  GST_OBJECT_UNLOCK (self);

  return uri_str;
}

static gboolean
gst_srt_sink_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error)
{
  GstSRTSink *self = GST_SRT_SINK (handler);
  gboolean ret;

  GST_OBJECT_LOCK (self);
  ret = gst_srt_object_set_uri (self->srtobject, uri, error);
  GST_OBJECT_UNLOCK (self);

  return ret;
}

static void
gst_srt_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_srt_sink_uri_get_type;
  iface->get_protocols = gst_srt_sink_uri_get_protocols;
  iface->get_uri = gst_srt_sink_uri_get_uri;
  iface->set_uri = gst_srt_sink_uri_set_uri;
}
