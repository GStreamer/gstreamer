/*
 * Copyright (C) 2014, Fluendo, S.A.
 * Copyright (C) 2014, Metrological Media Innovations B.V.
 *   Author: Josep Torra <josep@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxanalogaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_analog_audio_sink_debug_category);
#define GST_CAT_DEFAULT gst_omx_analog_audio_sink_debug_category

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_analog_audio_sink_debug_category, \
      "omxanalogaudiosink", 0, "debug category for gst-omx analog audio sink");

G_DEFINE_TYPE_WITH_CODE (GstOMXAnalogAudioSink, gst_omx_analog_audio_sink,
    GST_TYPE_OMX_AUDIO_SINK, DEBUG_INIT);

static void
gst_omx_analog_audio_sink_class_init (GstOMXAnalogAudioSinkClass * klass)
{
  GstOMXAudioSinkClass *audiosink_class = GST_OMX_AUDIO_SINK_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  audiosink_class->cdata.default_sink_template_caps = "audio/x-raw, "
      "format = (string) " GST_AUDIO_FORMATS_ALL ", "
      "layout = (string) interleaved, "
      "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ] ";
  audiosink_class->destination = "local";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX Analog Audio Sink",
      "Sink/Audio", "Output analog audio", "Josep Torra <josep@fluendo.com>");

  gst_omx_set_default_role (&audiosink_class->cdata, "audio_render.local");
}

static void
gst_omx_analog_audio_sink_init (GstOMXAnalogAudioSink * self)
{
}
