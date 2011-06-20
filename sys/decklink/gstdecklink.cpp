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
      {GST_DECKLINK_MODE_PAL, "pal", "PAL SD 50i"},
      {GST_DECKLINK_MODE_1080i50, "1080i50", "HD1080 50i"},
      {GST_DECKLINK_MODE_1080i60, "1080i60", "HD1080 60i"},
      {GST_DECKLINK_MODE_720p50, "720p50", "HD720 50p"},
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

static const GstDecklinkMode modes[] = {
  {bmdModeNTSC, 720, 486, 30000, 1001, true},
  {bmdModePAL, 720, 576, 25, 1, true},
  {bmdModeHD1080i50, 1920, 1080, 25, 1, true},
  {bmdModeHD1080i5994, 1920, 1080, 30000, 1001, true},
  {bmdModeHD720p50, 1280, 720, 50, 1, true},
  {bmdModeHD720p5994, 1280, 720, 60000, 1001, true}
};

#if 0
  //{ bmdModeNTSC2398, 720,486,24000,1001,true },
  //{ bmdModeHD1080p2398, 1920,1080,24000,1001,false },
  //{ bmdModeHD1080p24, 1920,1080,24,1,false },
  //{ bmdModeHD1080p25, 1920,1080,25,1,false },
  //{ bmdModeHD1080p2997, 1920,1080,30000,1001,false },
  //{ bmdModeHD1080p30, 1920,1080,30,1,false },
  //{ bmdModeHD1080i6000, 1920,1080,30,1,true },
  //{ bmdModeHD720p60, 1280,720,60,1,true }
#endif

const GstDecklinkMode *
gst_decklink_get_mode (GstDecklinkModeEnum e)
{
  return &modes[e];
}

GstCaps *
gst_decklink_mode_get_caps (GstDecklinkModeEnum e)
{
  const GstDecklinkMode *mode = &modes[e];

  return gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'),
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "framerate", GST_TYPE_FRACTION,
      mode->fps_n, mode->fps_d,
      "interlaced", G_TYPE_BOOLEAN, mode->interlaced, NULL);
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
