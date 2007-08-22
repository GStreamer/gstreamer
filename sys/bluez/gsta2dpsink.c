/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ipc.h"
#include "sbc.h"

#include "gsta2dpsink.h"

GST_DEBUG_CATEGORY_EXTERN (bluetooth_debug);
#define GST_CAT_DEFAULT bluetooth_debug

GST_BOILERPLATE (GstA2dpSink, gst_a2dpsink, GstAudioSink, GST_TYPE_AUDIO_SINK);

static const GstElementDetails a2dpsink_details =
GST_ELEMENT_DETAILS ("Bluetooth A2DP sink",
    "Sink/Audio",
    "Plays audio to an A2DP device",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

static void
gst_a2dpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG ("");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &a2dpsink_details);
}

static void
gst_a2dpsink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_a2dpsink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_a2dpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a2dpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_a2dpsink_getcaps (GstBaseSink * basesink)
{
  GST_DEBUG_OBJECT (basesink, "");

  return NULL;
}

static gboolean
gst_a2dpsink_open (GstAudioSink * audiosink)
{
  GST_DEBUG_OBJECT (audiosink, "");

  return TRUE;
}

static gboolean
gst_a2dpsink_prepare (GstAudioSink * audiosink, GstRingBufferSpec * spec)
{
  GST_DEBUG_OBJECT (audiosink, "spec %p", spec);

  return TRUE;
}

static gboolean
gst_a2dpsink_unprepare (GstAudioSink * audiosink)
{
  GST_DEBUG_OBJECT (audiosink, "");

  return TRUE;
}

static gboolean
gst_a2dpsink_close (GstAudioSink * audiosink)
{
  GST_DEBUG_OBJECT (audiosink, "");

  return TRUE;
}

static guint
gst_a2dpsink_write (GstAudioSink * audiosink, gpointer data, guint length)
{
  GST_DEBUG_OBJECT (audiosink, "data %p length %d", data, length);

  return length;
}

static guint
gst_a2dpsink_delay (GstAudioSink * audiosink)
{
  GST_DEBUG_OBJECT (audiosink, "");

  return 0;
}

static void
gst_a2dpsink_reset (GstAudioSink * audiosink)
{
}

static void
gst_a2dpsink_class_init (GstA2dpSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *gstaudiosink_class = GST_AUDIO_SINK_CLASS (klass);

  GST_DEBUG ("");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_a2dpsink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_a2dpsink_finalize);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_a2dpsink_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_a2dpsink_set_property);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_a2dpsink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_a2dpsink_open);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_a2dpsink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_a2dpsink_unprepare);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_a2dpsink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_a2dpsink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_a2dpsink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_a2dpsink_reset);
}

static void
gst_a2dpsink_init (GstA2dpSink * a2dpsink, GstA2dpSinkClass * klass)
{
  GST_DEBUG_OBJECT (a2dpsink, "");
}
