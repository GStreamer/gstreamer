/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * SECTION:element-giostreamsink
 *
 * This plugin writes incoming data to a custom GIO #GOutputStream.
 *
 * It can, for example, be used to write a stream to memory with a
 * #GMemoryOuputStream or to write to a file with a #GFileOuputStream.
 *
 * <refsect2>
 * <title>Example code</title>
 * <para>
 * The following example writes the received data to a #GMemoryOutputStream.
 * |[

#include &lt;gst/gst.h&gt;
#include &lt;gio/gio.h&gt;

...

GstElement *sink;
GMemoryOuputStream *stream;
// out_data will contain the received data
guint8 *out_data;

...

stream = G_MEMORY_OUTPUT_STREAM (g_memory_output_stream_new (NULL, 0,
          (GReallocFunc) g_realloc, (GDestroyNotify) g_free));
sink = gst_element_factory_make ("giostreamsink", "sink");
g_object_set (G_OBJECT (sink), "stream", stream, NULL);

...

// after processing get the written data
out_data = g_memory_ouput_stream_get_data (G_MEMORY_OUTPUT_STREAM (stream));

...

 * ]|
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiostreamsink.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_stream_sink_debug);
#define GST_CAT_DEFAULT gst_gio_stream_sink_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_STREAM
};

GST_BOILERPLATE (GstGioStreamSink, gst_gio_stream_sink, GstGioBaseSink,
    GST_TYPE_GIO_BASE_SINK);

static void gst_gio_stream_sink_finalize (GObject * object);
static void gst_gio_stream_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_stream_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_gio_stream_sink_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO stream sink",
    "Sink",
    "Write to any GIO stream",
    "Sebastian Dröge <slomo@circular-chaos.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_stream_sink_debug, "gio_stream_sink", 0,
      "GIO stream sink");

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_stream_sink_class_init (GstGioStreamSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = gst_gio_stream_sink_finalize;
  gobject_class->set_property = gst_gio_stream_sink_set_property;
  gobject_class->get_property = gst_gio_stream_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_STREAM,
      g_param_spec_object ("stream", "Stream", "Stream to write to",
          G_TYPE_OUTPUT_STREAM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gio_stream_sink_init (GstGioStreamSink * sink,
    GstGioStreamSinkClass * gclass)
{
}

static void
gst_gio_stream_sink_finalize (GObject * object)
{
  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_stream_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioStreamSink *sink = GST_GIO_STREAM_SINK (object);

  switch (prop_id) {
    case ARG_STREAM:{
      GObject *stream;

      if (GST_STATE (sink) == GST_STATE_PLAYING ||
          GST_STATE (sink) == GST_STATE_PAUSED)
        break;

      stream = g_value_dup_object (value);
      if (G_IS_OUTPUT_STREAM (stream))
        gst_gio_base_sink_set_stream (GST_GIO_BASE_SINK (sink),
            G_OUTPUT_STREAM (stream));

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_stream_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioStreamSink *sink = GST_GIO_STREAM_SINK (object);

  switch (prop_id) {
    case ARG_STREAM:
      g_value_set_object (value, GST_GIO_BASE_SINK (sink)->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
