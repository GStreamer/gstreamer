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
 * SECTION:element-giostreamsrc
 * @short_description: Reads data from a GIO GInputStream
 *
 * <refsect2>
 * <para>
 * This plugin reads data from a custom GIO #GInputStream.
 * </para>
 * <para>
 * It can, for example, be used to read data from memory with a
 * #GMemoryInputStream or to read from a file with a
 * #GFileInputStream.
 * </para>
 * <title>Example code</title>
 * <para>
 * The following example reads data from a #GMemoryOutputStream.
 * <programlisting>

#include &lt;gst/gst.h&gt;
#include &lt;gio/gio.h&gt;

...

GstElement *src;
GMemoryInputStream *stream;
// in_data will contain the data to send
guint8 *in_data;
gint i;

...
in_data = g_new (guint8, 512);
for (i = 0; i < 512; i++)
  in_data[i] = i % 256;

stream = G_MEMORY_INPUT_STREAM (g_memory_input_stream_new_from_data (in_data, 512,
          (GDestroyNotify) g_free));
src = gst_element_factory_make ("giostreamsrc", "src");
g_object_set (G_OBJECT (src), "stream", stream, NULL);

...

 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiostreamsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_stream_src_debug);
#define GST_CAT_DEFAULT gst_gio_stream_src_debug

enum
{
  ARG_0,
  ARG_STREAM
};

GST_BOILERPLATE (GstGioStreamSrc, gst_gio_stream_src, GstGioBaseSrc,
    GST_TYPE_GIO_BASE_SRC);

static void gst_gio_stream_src_finalize (GObject * object);
static void gst_gio_stream_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_stream_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_gio_stream_src_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO stream source",
    "Source",
    "Read from any GIO stream",
    "Sebastian Dröge <slomo@circular-chaos.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_stream_src_debug, "gio_stream_src", 0,
      "GIO source");

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_stream_src_class_init (GstGioStreamSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_gio_stream_src_finalize;
  gobject_class->set_property = gst_gio_stream_src_set_property;
  gobject_class->get_property = gst_gio_stream_src_get_property;

  g_object_class_install_property (gobject_class, ARG_STREAM,
      g_param_spec_object ("stream", "Stream", "Stream to read from",
          G_TYPE_INPUT_STREAM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gio_stream_src_init (GstGioStreamSrc * src, GstGioStreamSrcClass * gclass)
{
}

static void
gst_gio_stream_src_finalize (GObject * object)
{
  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_stream_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioStreamSrc *src = GST_GIO_STREAM_SRC (object);

  switch (prop_id) {
    case ARG_STREAM:{
      GObject *stream;

      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      stream = g_value_dup_object (value);
      if (G_IS_INPUT_STREAM (stream))
        gst_gio_base_src_set_stream (GST_GIO_BASE_SRC (src),
            G_INPUT_STREAM (stream));

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_stream_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioStreamSrc *src = GST_GIO_STREAM_SRC (object);

  switch (prop_id) {
    case ARG_STREAM:
      g_value_set_object (value, GST_GIO_BASE_SRC (src)->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
