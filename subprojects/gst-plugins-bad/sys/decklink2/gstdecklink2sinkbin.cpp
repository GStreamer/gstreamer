/*
 * GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-decklink2sinkbin
 * @short_description: Outputs Video and Audio to a BlackMagic DeckLink Device
 * @see_also: decklink2sink, decklink2combiner
 *
 * Convenience bin combining decklink2combiner and decklink2sink and exposing
 * separate video and audio sink pads.
 *
 * ## Sample pipeline
 * |[
 * gst-launch-1.0 decklink2sinkbin name=sink \
 *   videotestsrc ! sink.video \
 *   audiotestsrc ! sink.audio
 * ]|
 * Outputs test video and audio to a DeckLink device.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklink2sinkbin.h"
#include "gstdecklink2sink.h"
#include "gstdecklink2utils.h"
#include "gstdecklink2sinkprops.h"

GST_DEBUG_CATEGORY_STATIC (gst_decklink2_sink_bin_debug);
#define GST_CAT_DEFAULT gst_decklink2_sink_bin_debug

static GstStaticPadTemplate audio_template = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format = (string) { S16LE, S32LE }, "
        "rate = (int) 48000, channels = (int) { 2, 8, 16 }, "
        "layout = (string) interleaved"));

enum
{
  /* actions */
  SIGNAL_RESTART,

  SIGNAL_LAST,
};

static guint gst_decklink2_sink_bin_signals[SIGNAL_LAST] = { 0, };

struct _GstDeckLink2SinkBin
{
  GstBin parent;

  GstElement *sink;
};

static void gst_decklink2_sink_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink2_sink_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void on_hw_serial_number (GObject * object, GParamSpec * pspec,
    GstElement * self);
static void on_restart (GstDeckLink2SinkBin * self);

#define gst_decklink2_sink_bin_parent_class parent_class
G_DEFINE_TYPE (GstDeckLink2SinkBin, gst_decklink2_sink_bin, GST_TYPE_BIN);
GST_ELEMENT_REGISTER_DEFINE (decklink2sinkbin, "decklink2sinkbin",
    GST_RANK_NONE, GST_TYPE_DECKLINK2_SINK_BIN);

static void
gst_decklink2_sink_bin_class_init (GstDeckLink2SinkBinClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_decklink2_sink_bin_set_property;
  object_class->get_property = gst_decklink2_sink_bin_get_property;

  gst_decklink2_sink_install_properties (object_class);

  gst_decklink2_sink_bin_signals[SIGNAL_RESTART] =
      g_signal_new_class_handler ("restart", G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (on_restart), nullptr, nullptr, nullptr, G_TYPE_NONE, 0);

  auto templ_caps = gst_decklink2_get_default_template_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("video", GST_PAD_SINK, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref (templ_caps);

  gst_element_class_add_static_pad_template (element_class, &audio_template);

  gst_element_class_set_static_metadata (element_class,
      "Decklink2 Sink Bin", "Video/Audio/Sink/Hardware",
      "Decklink2 Sink Bin", "Seungha Yang <seungha@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink2_sink_bin_debug, "decklink2sinkbin",
      0, "decklink2sinkbin");
}

static void
gst_decklink2_sink_bin_init (GstDeckLink2SinkBin * self)
{
  auto combiner = gst_element_factory_make ("decklink2combiner", nullptr);
  auto queue = gst_element_factory_make ("queue", nullptr);
  self->sink = gst_element_factory_make ("decklink2sink", nullptr);

  g_object_set (queue, "max-size-buffers", 3, "max-size-bytes", 0,
      "max-size-time", (guint64) 0, nullptr);

  gst_bin_add_many (GST_BIN (self), combiner, queue, self->sink, nullptr);
  gst_element_link_many (combiner, queue, self->sink, nullptr);

  auto pad = gst_element_get_static_pad (combiner, "video");
  auto gpad = gst_ghost_pad_new ("video", pad);
  gst_object_unref (pad);
  gst_element_add_pad (GST_ELEMENT (self), gpad);

  pad = gst_element_get_static_pad (combiner, "audio");
  gpad = gst_ghost_pad_new ("audio", pad);
  gst_object_unref (pad);
  gst_element_add_pad (GST_ELEMENT (self), gpad);

  g_signal_connect (self->sink,
      "notify::hw-serial-number", G_CALLBACK (on_hw_serial_number), self);

  gst_bin_set_suppressed_flags (GST_BIN (self),
      (GstElementFlags) (GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK));
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_SINK);
}

static void
gst_decklink2_sink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_DECKLINK2_SINK_BIN (object);

  g_object_set_property (G_OBJECT (self->sink), pspec->name, value);
}

static void
gst_decklink2_sink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_DECKLINK2_SINK_BIN (object);

  g_object_get_property (G_OBJECT (self->sink), pspec->name, value);
}

static void
on_hw_serial_number (GObject * object, GParamSpec * pspec, GstElement * self)
{
  g_object_notify (G_OBJECT (self), "hw-serial-number");
}

static void
on_restart (GstDeckLink2SinkBin * self)
{
  gst_decklink2_sink_restart (GST_DECKLINK2_SINK (self->sink));
}
