/*
 * GStreamer
 * Copyright (C) 2017 Collabora Inc.
 * Copyright (C) 2021 Igalia S.L.
 *   Author: Philippe Normand <philn@igalia.com>
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
 * SECTION:element-fakeaudiosink
 * @title: fakeaudiosink
 *
 * This element is the same as fakesink but will pretend to act as an audio sink
 * supporting the `GstStreamVolume` interface. This is useful for throughput
 * testing while creating a new pipeline or for CI purposes on machines not
 * running a real audio daemon.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 audiotestsrc ! fakeaudiosink
 * ]|
 *
 * Since: 1.20
 */

#include "gstdebugutilsbadelements.h"
#include "gstfakeaudiosink.h"
#include "gstfakesinkutils.h"

#include <gst/audio/audio.h>

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_LAST
};


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS_ALL)));

G_DEFINE_TYPE_WITH_CODE (GstFakeAudioSink, gst_fake_audio_sink, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_STREAM_VOLUME, NULL););
GST_ELEMENT_REGISTER_DEFINE (fakeaudiosink, "fakeaudiosink",
    GST_RANK_NONE, gst_fake_audio_sink_get_type ());

static void
gst_fake_audio_sink_proxy_properties (GstFakeAudioSink * self,
    GstElement * child)
{
  static gsize initialized = 0;

  if (g_once_init_enter (&initialized)) {
    gst_fake_sink_proxy_properties (GST_ELEMENT_CAST (self), child, PROP_LAST);
    g_once_init_leave (&initialized, 1);
  }
}

static void
gst_fake_audio_sink_init (GstFakeAudioSink * self)
{
  GstElement *child;
  GstPadTemplate *template = gst_static_pad_template_get (&sink_factory);

  self->volume = 1.0;
  self->mute = FALSE;

  child = gst_element_factory_make ("fakesink", "sink");

  if (child) {
    GstPad *sink_pad = gst_element_get_static_pad (child, "sink");
    GstPad *ghost_pad;

    /* mimic GstAudioSink base class */
    g_object_set (child, "qos", TRUE, "sync", TRUE, NULL);

    gst_bin_add (GST_BIN_CAST (self), child);

    ghost_pad = gst_ghost_pad_new_from_template ("sink", sink_pad, template);
    gst_object_unref (template);
    gst_element_add_pad (GST_ELEMENT_CAST (self), ghost_pad);
    gst_object_unref (sink_pad);

    self->child = child;

    gst_fake_audio_sink_proxy_properties (self, child);
  } else {
    g_warning ("Check your GStreamer installation, "
        "core element 'fakesink' is missing.");
  }
}

static void
gst_fake_audio_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstFakeAudioSink *self = GST_FAKE_AUDIO_SINK (object);

  switch (property_id) {
    case PROP_VOLUME:
      g_value_set_double (value, self->volume);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, self->mute);
      break;
    default:
      g_object_get_property (G_OBJECT (self->child), pspec->name, value);
      break;
  }
}

static void
gst_fake_audio_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFakeAudioSink *self = GST_FAKE_AUDIO_SINK (object);

  switch (property_id) {
    case PROP_VOLUME:
      self->volume = g_value_get_double (value);
      break;
    case PROP_MUTE:
      self->mute = g_value_get_boolean (value);
      break;
    default:
      g_object_set_property (G_OBJECT (self->child), pspec->name, value);
      break;
  }
}

static void
gst_fake_audio_sink_class_init (GstFakeAudioSinkClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_fake_audio_sink_get_property;
  object_class->set_property = gst_fake_audio_sink_set_property;


  /**
   * GstFakeAudioSink:volume
   *
   * Control the audio volume
   *
   * Since: 1.20
   */
  g_object_class_install_property (object_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "The audio volume, 1.0=100%",
          0, 10, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstFakeAudioSink:mute
   *
   * Control the mute state
   *
   * Since: 1.20
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "Mute the audio channel without changing the volume", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class, "Fake Audio Sink",
      "Audio/Sink", "Fake audio renderer",
      "Philippe Normand <philn@igalia.com>");
}
