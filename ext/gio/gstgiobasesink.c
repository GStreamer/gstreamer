/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <config.h>
#endif

#include "gstgiobasesink.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_base_sink_debug);
#define GST_CAT_DEFAULT gst_gio_base_sink_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstGioBaseSink, gst_gio_base_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void gst_gio_base_sink_finalize (GObject * object);
static gboolean gst_gio_base_sink_start (GstBaseSink * base_sink);
static gboolean gst_gio_base_sink_stop (GstBaseSink * base_sink);
static gboolean gst_gio_base_sink_unlock (GstBaseSink * base_sink);
static gboolean gst_gio_base_sink_unlock_stop (GstBaseSink * base_sink);
static gboolean gst_gio_base_sink_event (GstBaseSink * base_sink,
    GstEvent * event);
static GstFlowReturn gst_gio_base_sink_render (GstBaseSink * base_sink,
    GstBuffer * buffer);
static gboolean gst_gio_base_sink_query (GstPad * pad, GstQuery * query);

static void
gst_gio_base_sink_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_base_sink_debug, "gio_base_sink", 0,
      "GIO base sink");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_gio_base_sink_class_init (GstGioBaseSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = gst_gio_base_sink_finalize;

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_gio_base_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_gio_base_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_gio_base_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_gio_base_sink_unlock_stop);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_gio_base_sink_event);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_gio_base_sink_render);
}

static void
gst_gio_base_sink_init (GstGioBaseSink * sink, GstGioBaseSinkClass * gclass)
{
  gst_pad_set_query_function (GST_BASE_SINK_PAD (sink),
      GST_DEBUG_FUNCPTR (gst_gio_base_sink_query));

  gst_base_sink_set_sync (GST_BASE_SINK (sink), FALSE);

  sink->cancel = g_cancellable_new ();
}

static void
gst_gio_base_sink_finalize (GObject * object)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (object);

  if (sink->cancel) {
    g_object_unref (sink->cancel);
    sink->cancel = NULL;
  }

  if (sink->stream) {
    g_object_unref (sink->stream);
    sink->stream = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static gboolean
gst_gio_base_sink_start (GstBaseSink * base_sink)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);
  GstGioBaseSinkClass *gbsink_class = GST_GIO_BASE_SINK_GET_CLASS (sink);

  sink->position = 0;

  /* FIXME: This will likely block */
  sink->stream = gbsink_class->get_stream (sink);
  if (G_UNLIKELY (!G_IS_OUTPUT_STREAM (sink->stream))) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("No output stream provided by subclass"));
    return FALSE;
  } else if (G_UNLIKELY (g_output_stream_is_closed (sink->stream))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, FAILED, (NULL),
        ("Output stream is already closed"));
    return FALSE;
  }

  GST_DEBUG_OBJECT (sink, "started sink");

  return TRUE;
}

static gboolean
gst_gio_base_sink_stop (GstBaseSink * base_sink)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);
  GstGioBaseSinkClass *klass = GST_GIO_BASE_SINK_GET_CLASS (sink);
  gboolean success;
  GError *err = NULL;

  if (klass->close_on_stop && G_IS_OUTPUT_STREAM (sink->stream)) {
    GST_DEBUG_OBJECT (sink, "closing stream");

    /* FIXME: can block but unfortunately we can't use async operations
     * here because they require a running main loop */
    success = g_output_stream_close (sink->stream, sink->cancel, &err);

    if (!success && !gst_gio_error (sink, "g_output_stream_close", &err, NULL)) {
      GST_ELEMENT_WARNING (sink, RESOURCE, CLOSE, (NULL),
          ("gio_output_stream_close failed: %s", err->message));
      g_clear_error (&err);
    } else if (!success) {
      GST_ELEMENT_WARNING (sink, RESOURCE, CLOSE, (NULL),
          ("g_output_stream_close failed"));
    } else {
      GST_DEBUG_OBJECT (sink, "g_outut_stream_close succeeded");
    }

    g_object_unref (sink->stream);
    sink->stream = NULL;
  } else {
    success = g_output_stream_flush (sink->stream, sink->cancel, &err);

    if (!success && !gst_gio_error (sink, "g_output_stream_flush", &err, NULL)) {
      GST_ELEMENT_WARNING (sink, RESOURCE, CLOSE, (NULL),
          ("gio_output_stream_flush failed: %s", err->message));
      g_clear_error (&err);
    } else if (!success) {
      GST_ELEMENT_WARNING (sink, RESOURCE, CLOSE, (NULL),
          ("g_output_stream_flush failed"));
    } else {
      GST_DEBUG_OBJECT (sink, "g_outut_stream_flush succeeded");
    }

    g_object_unref (sink->stream);
    sink->stream = NULL;
  }

  return TRUE;
}

static gboolean
gst_gio_base_sink_unlock (GstBaseSink * base_sink)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);

  GST_LOG_OBJECT (sink, "triggering cancellation");

  g_cancellable_cancel (sink->cancel);

  return TRUE;
}

static gboolean
gst_gio_base_sink_unlock_stop (GstBaseSink * base_sink)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);

  GST_LOG_OBJECT (sink, "resetting cancellable");

  g_cancellable_reset (sink->cancel);

  return TRUE;
}

static gboolean
gst_gio_base_sink_event (GstBaseSink * base_sink, GstEvent * event)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);
  GstFlowReturn ret = GST_FLOW_OK;

  if (sink->stream == NULL)
    return TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      if (G_IS_OUTPUT_STREAM (sink->stream)) {
        GstFormat format;
        gint64 offset;

        gst_event_parse_new_segment (event, NULL, NULL, &format, &offset, NULL,
            NULL);

        if (format != GST_FORMAT_BYTES) {
          GST_WARNING_OBJECT (sink, "ignored NEWSEGMENT event in %s format",
              gst_format_get_name (format));
          break;
        }

        if (GST_GIO_STREAM_IS_SEEKABLE (sink->stream)) {
          ret = gst_gio_seek (sink, G_SEEKABLE (sink->stream), offset,
              sink->cancel);
          if (ret == GST_FLOW_OK)
            sink->position = offset;
        } else {
          ret = GST_FLOW_NOT_SUPPORTED;
        }
      }
      break;

    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_START:
      if (G_IS_OUTPUT_STREAM (sink->stream)) {
        gboolean success;
        GError *err = NULL;

        success = g_output_stream_flush (sink->stream, sink->cancel, &err);

        if (!success && !gst_gio_error (sink, "g_output_stream_flush", &err,
                &ret)) {
          GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
              ("flush failed: %s", err->message));
          g_clear_error (&err);
        }
      }
      break;

    default:
      break;
  }

  return (ret == GST_FLOW_OK);
}

static GstFlowReturn
gst_gio_base_sink_render (GstBaseSink * base_sink, GstBuffer * buffer)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (base_sink);
  gssize written;
  gboolean success;
  GError *err = NULL;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (sink->stream), GST_FLOW_ERROR);

  GST_LOG_OBJECT (sink, "writing %u bytes to offset %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buffer), sink->position);

  written = g_output_stream_write (sink->stream,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), sink->cancel, &err);

  success = (written >= 0);

  if (G_UNLIKELY (success && written < GST_BUFFER_SIZE (buffer))) {
    /* FIXME: Can this happen?  Should we handle it gracefully?  gnomevfssink
     * doesn't... */
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
        ("Could not write to stream: (short write, only %"
            G_GSSIZE_FORMAT " bytes of %d bytes written)",
            written, GST_BUFFER_SIZE (buffer)));
    return GST_FLOW_ERROR;
  }

  if (success) {
    sink->position += written;
    return GST_FLOW_OK;

  } else {
    GstFlowReturn ret;

    if (!gst_gio_error (sink, "g_output_stream_write", &err, &ret)) {
      if (GST_GIO_ERROR_MATCHES (err, NO_SPACE)) {
        GST_ELEMENT_ERROR (sink, RESOURCE, NO_SPACE_LEFT, (NULL),
            ("Could not write to stream: %s", err->message));
      } else {
        GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
            ("Could not write to stream: %s", err->message));
      }
      g_clear_error (&err);
    }

    return ret;
  }
}

static gboolean
gst_gio_base_sink_query (GstPad * pad, GstQuery * query)
{
  GstGioBaseSink *sink = GST_GIO_BASE_SINK (GST_PAD_PARENT (pad));
  GstFormat format;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, GST_FORMAT_BYTES, sink->position);
          return TRUE;
        default:
          return FALSE;
      }
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      return TRUE;
    case GST_QUERY_URI:
      if (GST_IS_URI_HANDLER (sink)) {
        const gchar *uri;

        uri = gst_uri_handler_get_uri (GST_URI_HANDLER (sink));
        gst_query_set_uri (query, uri);
        return TRUE;
      }
      return FALSE;
    default:
      return gst_pad_query_default (pad, query);
  }
}
