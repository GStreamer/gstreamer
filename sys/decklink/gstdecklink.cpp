/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstdecklink.h"
#include "gstdecklinksrc.h"
#include "gstdecklinksink.h"

GType
gst_decklink_mode_get_type (void)
{
  static GType type;

  if (!type) {
    static const GEnumValue modes[] = {
      {GST_DECKLINK_MODE_NTSC, "ntsc", "NTSC SD 60i"},
      {GST_DECKLINK_MODE_NTSC2398, "ntsc2398", "NTSC SD 60i (24 fps)"},
      {GST_DECKLINK_MODE_PAL, "pal", "PAL SD 50i"},
      {GST_DECKLINK_MODE_NTSC_P, "ntsc-p", "NTSC SD 60p"},
      {GST_DECKLINK_MODE_PAL_P, "pal-p", "PAL SD 50p"},

      {GST_DECKLINK_MODE_1080p2398, "1080p2398", "HD1080 23.98p"},
      {GST_DECKLINK_MODE_1080p24, "1080p24", "HD1080 24p"},
      {GST_DECKLINK_MODE_1080p25, "1080p25", "HD1080 25p"},
      {GST_DECKLINK_MODE_1080p2997, "1080p2997", "HD1080 29.97p"},
      {GST_DECKLINK_MODE_1080p30, "1080p30", "HD1080 30p"},

      {GST_DECKLINK_MODE_1080i50, "1080i50", "HD1080 50i"},
      {GST_DECKLINK_MODE_1080i5994, "1080i5994", "HD1080 59.94i"},
      {GST_DECKLINK_MODE_1080i60, "1080i60", "HD1080 60i"},

      {GST_DECKLINK_MODE_1080p50, "1080p50", "HD1080 50p"},
      {GST_DECKLINK_MODE_1080p5994, "1080p5994", "HD1080 59.94p"},
      {GST_DECKLINK_MODE_1080p60, "1080p60", "HD1080 60p"},

      {GST_DECKLINK_MODE_720p50, "720p50", "HD720 50p"},
      {GST_DECKLINK_MODE_720p5994, "720p5994", "HD720 59.94p"},
      {GST_DECKLINK_MODE_720p60, "720p60", "HD720 60p"},

      {0, NULL, NULL}
    };

    type = g_enum_register_static ("GstDecklinkModes", modes);
  }
  return type;
}

GType
gst_decklink_connection_get_type (void)
{
  static GType type;

  if (!type) {
    static const GEnumValue connections[] = {
      {GST_DECKLINK_CONNECTION_SDI, "sdi", "SDI"},
      {GST_DECKLINK_CONNECTION_HDMI, "hdmi", "HDMI"},
      {GST_DECKLINK_CONNECTION_OPTICAL_SDI, "optical-sdi", "Optical SDI"},
      {GST_DECKLINK_CONNECTION_COMPONENT, "component", "Component"},
      {GST_DECKLINK_CONNECTION_COMPOSITE, "composite", "Composite"},
      {GST_DECKLINK_CONNECTION_SVIDEO, "svideo", "S-Video"},
      {0, NULL, NULL}
    };

    type = g_enum_register_static ("GstDecklinkConnection", connections);
  }
  return type;
}

GType
gst_decklink_audio_connection_get_type (void)
{
  static GType type;

  if (!type) {
    static const GEnumValue connections[] = {
      {GST_DECKLINK_AUDIO_CONNECTION_AUTO, "auto", "Automatic"},
      {GST_DECKLINK_AUDIO_CONNECTION_EMBEDDED, "embedded", "SDI/HDMI embedded audio"},
      {GST_DECKLINK_AUDIO_CONNECTION_AES_EBU, "aes", "AES/EBU input"},
      {GST_DECKLINK_AUDIO_CONNECTION_ANALOG, "analog", "Analog input"},
      {0, NULL, NULL}
    };

    type = g_enum_register_static ("GstDecklinkAudioConnection", connections);
  }
  return type;
}

#define NTSC 10, 11, false, false
#define PAL 12, 11, true, false
#define HD 1, 1, false, true

static const GstDecklinkMode modes[] = {
  {bmdModeNTSC, 720, 486, 30000, 1001, true, NTSC },
  {bmdModeNTSC2398, 720, 486, 24000, 1001, true, NTSC },
  {bmdModePAL, 720, 576, 25, 1, true, PAL },
  {bmdModeNTSCp, 720, 486, 30000, 1001, false, NTSC },
  {bmdModePALp, 720, 576, 25, 1, false, PAL },

  {bmdModeHD1080p2398, 1920, 1080, 24000, 1001, false, HD },
  {bmdModeHD1080p24, 1920, 1080, 24, 1, false, HD },
  {bmdModeHD1080p25, 1920, 1080, 25, 1, false, HD },
  {bmdModeHD1080p2997, 1920, 1080, 30000, 1001, false, HD },
  {bmdModeHD1080p30, 1920, 1080, 30, 1, false, HD },

  {bmdModeHD1080i50, 1920, 1080, 25, 1, true, HD },
  {bmdModeHD1080i5994, 1920, 1080, 30000, 1001, true, HD },
  {bmdModeHD1080i6000, 1920, 1080, 30, 1, true, HD },

  {bmdModeHD1080p50, 1920, 1080, 50, 1, false, HD },
  {bmdModeHD1080p5994, 1920, 1080, 30000, 1001, false, HD },
  {bmdModeHD1080p6000, 1920, 1080, 60, 1, false, HD },

  {bmdModeHD720p50, 1280, 720, 50, 1, false, HD },
  {bmdModeHD720p5994, 1280, 720, 60000, 1001, false, HD },
  {bmdModeHD720p60, 1280, 720, 60, 1, false, HD }

};

const GstDecklinkMode *
gst_decklink_get_mode (GstDecklinkModeEnum e)
{
  return &modes[e];
}

static GstStructure *
gst_decklink_mode_get_structure (GstDecklinkModeEnum e)
{
  const GstDecklinkMode *mode = &modes[e];

  return gst_structure_new ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'),
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d,
      "interlaced", G_TYPE_BOOLEAN, mode->interlaced,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, mode->par_n, mode->par_d,
      "color-matrix", G_TYPE_STRING, mode->is_hdtv ? "hdtv" : "sdtv",
      "chroma-site", G_TYPE_STRING, "mpeg2",
      NULL);
}

GstCaps *
gst_decklink_mode_get_caps (GstDecklinkModeEnum e)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();
  gst_caps_append_structure (caps, gst_decklink_mode_get_structure (e));

  return caps;
}

GstCaps *
gst_decklink_mode_get_template_caps (void)
{
  int i;
  GstCaps *caps;
  GstStructure *s;

  caps = gst_caps_new_empty ();
  for(i=0;i<(int)G_N_ELEMENTS(modes);i++) {
    s = gst_decklink_mode_get_structure ((GstDecklinkModeEnum)i);
    gst_caps_append_structure (caps, s);
  }

  return caps;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "decklinksrc", GST_RANK_NONE,
      gst_decklink_src_get_type ());
  gst_element_register (plugin, "decklinksink", GST_RANK_NONE,
      gst_decklink_sink_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "decklink",
    "Blackmagic Decklink plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
