/*
 * GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 *
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "openslessink.h"

GST_DEBUG_CATEGORY_STATIC (opensles_sink_debug);
#define GST_CAT_DEFAULT opensles_sink_debug

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_LAST
};

#define DEFAULT_VOLUME 1.0

#define RATES "8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000, 192000"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { " RATES "}, "
        "channels = (int) [1, 2];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) { " RATES "}, " "channels = (int) [1, 2]")
    );

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (opensles_sink_debug, "opensles_sink", 0,
      "OpenSL ES Sink");
}

GST_BOILERPLATE_FULL (GstOpenSLESSink, gst_opensles_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, _do_init);

static void
gst_opensles_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "OpenSL ES Sink",
      "Sink/Audio",
      "Output sound using the OpenSL ES APIs",
      "Josep Torra <support@fluendo.com>");
}

static GstRingBuffer *
gst_opensles_sink_create_ringbuffer (GstBaseAudioSink * base)
{
  GstRingBuffer *rb;

  rb = gst_opensles_ringbuffer_new (RB_MODE_SINK_PCM);

  return rb;
}

static void
gst_opensles_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenSLESSink *sink = GST_OPENSLES_SINK (object);

  switch (prop_id) {
    case PROP_VOLUME:
      sink->volume = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opensles_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenSLESSink *sink = GST_OPENSLES_SINK (object);
  switch (prop_id) {
    case PROP_VOLUME:
      g_value_set_double (value, sink->volume);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opensles_sink_class_init (GstOpenSLESSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_opensles_sink_set_property;
  gobject_class->get_property = gst_opensles_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_opensles_sink_create_ringbuffer);
}

static void
gst_opensles_sink_init (GstOpenSLESSink * sink, GstOpenSLESSinkClass * gclass)
{
  sink->volume = DEFAULT_VOLUME;
}
