/* GStreamer
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include "config.h"
#endif

#include "gstplaysinkaudioconvert.h"

#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_play_sink_audio_convert_debug);
#define GST_CAT_DEFAULT gst_play_sink_audio_convert_debug

#define parent_class gst_play_sink_audio_convert_parent_class

G_DEFINE_TYPE (GstPlaySinkAudioConvert, gst_play_sink_audio_convert,
    GST_TYPE_PLAY_SINK_CONVERT_BIN);

static gboolean
gst_play_sink_audio_convert_add_conversion_elements (GstPlaySinkConvertBin *
    cbin)
{
  GstPlaySinkAudioConvert *self = GST_PLAY_SINK_AUDIO_CONVERT (cbin);
  GstElement *el, *prev = NULL;

  g_assert (cbin->conversion_elements == NULL);

  if (self->use_converters) {
    el = gst_play_sink_convert_bin_add_conversion_element_factory (cbin,
        "audioconvert", "conv");
    if (el) {
      prev = el;
    }

    el = gst_play_sink_convert_bin_add_conversion_element_factory (cbin,
        "audioresample", "resample");
    if (el) {

      if (prev) {
        if (!gst_element_link_pads_full (prev, "src", el, "sink",
                GST_PAD_LINK_CHECK_TEMPLATE_CAPS))
          goto link_failed;
      }
      prev = el;
    }
  }

  if (self->use_volume && self->volume) {
    el = self->volume;
    gst_play_sink_convert_bin_add_conversion_element (cbin, el);
    if (prev) {
      if (!gst_element_link_pads_full (prev, "src", el, "sink",
              GST_PAD_LINK_CHECK_TEMPLATE_CAPS))
        goto link_failed;
    }
    prev = el;
  }
  return TRUE;

link_failed:
  return FALSE;
}

static void
gst_play_sink_audio_convert_finalize (GObject * object)
{
  GstPlaySinkAudioConvert *self = GST_PLAY_SINK_AUDIO_CONVERT_CAST (object);

  if (self->volume)
    gst_object_unref (self->volume);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_play_sink_audio_convert_class_init (GstPlaySinkAudioConvertClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (gst_play_sink_audio_convert_debug,
      "playsinkaudioconvert", 0, "play bin");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_play_sink_audio_convert_finalize;

  gst_element_class_set_details_simple (gstelement_class,
      "Player Sink Audio Converter", "Audio/Bin/Converter",
      "Convenience bin for audio conversion",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_play_sink_audio_convert_init (GstPlaySinkAudioConvert * self)
{
  GstPlaySinkConvertBin *cbin = GST_PLAY_SINK_CONVERT_BIN (self);

  cbin->audio = TRUE;
  cbin->add_conversion_elements =
      gst_play_sink_audio_convert_add_conversion_elements;

  /* FIXME: Only create this on demand but for now we need
   * it to always exist because of playsink's volume proxying
   * logic.
   */
  self->volume = gst_element_factory_make ("volume", "volume");
  if (self->volume)
    gst_object_ref_sink (self->volume);
}
