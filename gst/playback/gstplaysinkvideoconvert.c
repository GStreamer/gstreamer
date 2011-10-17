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

#include "gstplaysinkvideoconvert.h"

#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_play_sink_video_convert_debug);
#define GST_CAT_DEFAULT gst_play_sink_video_convert_debug

#define parent_class gst_play_sink_video_convert_parent_class

G_DEFINE_TYPE (GstPlaySinkVideoConvert, gst_play_sink_video_convert,
    GST_TYPE_PLAY_SINK_CONVERT_BIN);

static gboolean
gst_play_sink_video_convert_add_conversion_elements (GstPlaySinkVideoConvert *
    self)
{
  GstPlaySinkConvertBin *cbin = GST_PLAY_SINK_CONVERT_BIN (self);
  GstElement *el, *prev = NULL;

  el = gst_play_sink_convert_bin_add_conversion_element_factory (cbin,
      COLORSPACE, "conv");
  if (el)
    prev = el;

  el = gst_play_sink_convert_bin_add_conversion_element_factory (cbin,
      "videoscale", "scale");
  if (el) {
    /* Add black borders if necessary to keep the DAR */
    g_object_set (el, "add-borders", TRUE, NULL);
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
gst_play_sink_video_convert_class_init (GstPlaySinkVideoConvertClass * klass)
{
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (gst_play_sink_video_convert_debug,
      "playsinkvideoconvert", 0, "play bin");

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_details_simple (gstelement_class,
      "Player Sink Video Converter", "Video/Bin/Converter",
      "Convenience bin for video conversion",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_play_sink_video_convert_init (GstPlaySinkVideoConvert * self)
{
  GstPlaySinkConvertBin *cbin = GST_PLAY_SINK_CONVERT_BIN (self);
  cbin->audio = FALSE;

  gst_play_sink_video_convert_add_conversion_elements (self);
  gst_play_sink_convert_bin_cache_converter_caps (cbin);
}
