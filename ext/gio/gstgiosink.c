/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
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
 * SECTION:element-giosink
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc num-buffers=100 ! flacenc ! giosink location=file:///home/foo/bar.flac
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_sink_debug);
#define GST_CAT_DEFAULT gst_gio_sink_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

GST_BOILERPLATE_FULL (GstGioSink, gst_gio_sink, GstBaseSink, GST_TYPE_BASE_SINK,
    gst_gio_uri_handler_do_init);

static void gst_gio_sink_finalize (GObject * object);
static void gst_gio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_gio_sink_start (GstBaseSink * base_sink);
static gboolean gst_gio_sink_stop (GstBaseSink * base_sink);
static gboolean gst_gio_sink_unlock (GstBaseSink * base_sink);
static gboolean gst_gio_sink_unlock_stop (GstBaseSink * base_sink);
static gboolean gst_gio_sink_event (GstBaseSink * base_sink, GstEvent * event);
static GstFlowReturn gst_gio_sink_render (GstBaseSink * base_sink,
    GstBuffer * buffer);
static gboolean gst_gio_sink_query (GstPad * pad, GstQuery * query);

static void
gst_gio_sink_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO sink",
    "Sink/File",
    "Write to any GVFS-supported location",
    "Ren\xc3\xa9 Stadler <mail@renestadler.de>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_sink_debug, "giosink", 0, "GIO source");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_sink_class_init (GstGioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = gst_gio_sink_finalize;
  gobject_class->set_property = gst_gio_sink_set_property;
  gobject_class->get_property = gst_gio_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to write to",
          NULL, G_PARAM_READWRITE));

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_gio_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_gio_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_gio_sink_unlock);
  gstbasesink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_gio_sink_unlock_stop);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_gio_sink_event);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_gio_sink_render);
}

static void
gst_gio_sink_init (GstGioSink * sink, GstGioSinkClass * gclass)
{
  gst_pad_set_query_function (GST_BASE_SINK_PAD (sink),
      GST_DEBUG_FUNCPTR (gst_gio_sink_query));

  GST_BASE_SINK (sink)->sync = FALSE;

  sink->cancel = g_cancellable_new ();
}

static void
gst_gio_sink_finalize (GObject * object)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  g_object_unref (sink->cancel);

  if (sink->file)
    g_object_unref (sink->file);

  g_free (sink->location);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_free (sink->location);
      sink->location = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_sink_start (GstBaseSink * base_sink)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);
  gboolean success;
  GError *err = NULL;

  if (sink->location == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("No location given"));
    return FALSE;
  }

  sink->file = g_file_new_for_uri (sink->location);

  if (sink->file == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Malformed URI or protocol not supported (%s)", sink->location));
    return FALSE;
  }

  sink->stream = g_file_create (sink->file, sink->cancel, &err);
  success = (sink->stream != NULL);

  if (!success && !gst_gio_error (sink, "g_file_create", &err, NULL)) {

    /*if (GST_GIO_ERROR_MATCHES (err, EXISTS)) */
    /* FIXME: Retry with replace if overwrite == TRUE! */

    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND))
      GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for writing: %s",
              sink->location, err->message));
    else
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for writing: %s",
              sink->location, err->message));

    g_clear_error (&err);
  }

  if (!success) {
    g_object_unref (sink->file);
    sink->file = NULL;

    return FALSE;
  }

  sink->position = 0;

  GST_DEBUG_OBJECT (sink, "opened location %s", sink->location);

  return TRUE;
}

static gboolean
gst_gio_sink_stop (GstBaseSink * base_sink)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);
  gboolean success = TRUE;
  GError *err = NULL;

  if (sink->file != NULL) {
    g_object_unref (sink->file);
    sink->file = NULL;
  }

  if (sink->stream != NULL) {
    /* FIXME: In case that the call below would block, there is no one to
     * trigger the cancellation! */

    success = g_output_stream_close (G_OUTPUT_STREAM (sink->stream),
        sink->cancel, &err);

    if (success) {
      GST_DEBUG_OBJECT (sink, "closed location %s", sink->location);
    } else if (!gst_gio_error (sink, "g_output_stream_close", &err, NULL)) {
      GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE, (NULL),
          ("g_output_stream_close failed: %s", err->message));
      g_clear_error (&err);
    }

    g_object_unref (sink->stream);
    sink->stream = NULL;
  }

  return success;
}

static gboolean
gst_gio_sink_unlock (GstBaseSink * base_sink)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);

  GST_LOG_OBJECT (sink, "triggering cancellation");

  g_cancellable_cancel (sink->cancel);

  return TRUE;
}

static gboolean
gst_gio_sink_unlock_stop (GstBaseSink * base_sink)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);

  GST_LOG_OBJECT (sink, "restoring cancellable");

  g_object_unref (sink->cancel);
  sink->cancel = g_cancellable_new ();

  return TRUE;
}

static gboolean
gst_gio_sink_event (GstBaseSink * base_sink, GstEvent * event)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);
  GstFlowReturn ret = GST_FLOW_OK;

  if (sink->stream == NULL)
    return TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gint64 offset;

      gst_event_parse_new_segment (event, NULL, NULL, &format, &offset, NULL,
          NULL);

      if (format != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (sink, "ignored NEWSEGMENT event in %s format",
            gst_format_get_name (format));
        break;
      }

      ret = gst_gio_seek (sink, G_SEEKABLE (sink->stream), offset,
          sink->cancel);

      if (ret == GST_FLOW_OK)
        sink->position = offset;
    }
      break;

    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_START:
    {
      gboolean success;
      GError *err = NULL;

      success = g_output_stream_flush (G_OUTPUT_STREAM (sink->stream),
          sink->cancel, &err);

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
gst_gio_sink_render (GstBaseSink * base_sink, GstBuffer * buffer)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);
  gssize written;
  gboolean success;
  GError *err = NULL;

  GST_LOG_OBJECT (sink, "writing %u bytes to offset %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buffer), sink->position);

  written = g_output_stream_write (G_OUTPUT_STREAM (sink->stream),
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), sink->cancel, &err);

  success = (written >= 0);

  if (G_UNLIKELY (success && written < GST_BUFFER_SIZE (buffer))) {
    /* FIXME: Can this happen?  Should we handle it gracefully?  gnomevfssink
     * doesn't... */
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
        ("Could not write to location %s: (short write)", sink->location));
    return GST_FLOW_ERROR;
  }

  if (success) {
    sink->position += written;
    return GST_FLOW_OK;

  } else {
    GstFlowReturn ret;

    if (!gst_gio_error (sink, "g_output_stream_write", &err, &ret)) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not write to location %s: %s", sink->location, err->message));
      g_clear_error (&err);
    }

    return ret;
  }
}

static gboolean
gst_gio_sink_query (GstPad * pad, GstQuery * query)
{
  GstGioSink *sink = GST_GIO_SINK (GST_PAD_PARENT (pad));
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
    default:
      return gst_pad_query_default (pad, query);
  }
}
