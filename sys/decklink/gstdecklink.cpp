/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Florian Langlois <florian.langlois@fr.thalesgroup.com>
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
#include "gstdecklinkaudiosink.h"
#include "gstdecklinkvideosink.h"
#include "gstdecklinkaudiosrc.h"
#include "gstdecklinkvideosrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_decklink_debug);
#define GST_CAT_DEFAULT gst_decklink_debug

GType
gst_decklink_mode_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue modes[] = {
    {GST_DECKLINK_MODE_AUTO, "Automatic detection", "auto"},

    {GST_DECKLINK_MODE_NTSC, "NTSC SD 60i", "ntsc"},
    {GST_DECKLINK_MODE_NTSC2398, "NTSC SD 60i (24 fps)", "ntsc2398"},
    {GST_DECKLINK_MODE_PAL, "PAL SD 50i", "pal"},
    {GST_DECKLINK_MODE_NTSC_P, "NTSC SD 60p", "ntsc-p"},
    {GST_DECKLINK_MODE_PAL_P, "PAL SD 50p", "pal-p"},

    {GST_DECKLINK_MODE_1080p2398, "HD1080 23.98p", "1080p2398"},
    {GST_DECKLINK_MODE_1080p24, "HD1080 24p", "1080p24"},
    {GST_DECKLINK_MODE_1080p25, "HD1080 25p", "1080p25"},
    {GST_DECKLINK_MODE_1080p2997, "HD1080 29.97p", "1080p2997"},
    {GST_DECKLINK_MODE_1080p30, "HD1080 30p", "1080p30"},

    {GST_DECKLINK_MODE_1080i50, "HD1080 50i", "1080i50"},
    {GST_DECKLINK_MODE_1080i5994, "HD1080 59.94i", "1080i5994"},
    {GST_DECKLINK_MODE_1080i60, "HD1080 60i", "1080i60"},

    {GST_DECKLINK_MODE_1080p50, "HD1080 50p", "1080p50"},
    {GST_DECKLINK_MODE_1080p5994, "HD1080 59.94p", "1080p5994"},
    {GST_DECKLINK_MODE_1080p60, "HD1080 60p", "1080p60"},

    {GST_DECKLINK_MODE_720p50, "HD720 50p", "720p50"},
    {GST_DECKLINK_MODE_720p5994, "HD720 59.94p", "720p5994"},
    {GST_DECKLINK_MODE_720p60, "HD720 60p", "720p60"},

    {GST_DECKLINK_MODE_1556p2398, "2k 23.98p", "1556p2398"},
    {GST_DECKLINK_MODE_1556p24, "2k 24p", "1556p24"},
    {GST_DECKLINK_MODE_1556p25, "2k 25p", "1556p25"},

    {GST_DECKLINK_MODE_2160p2398, "4k 23.98p", "2160p2398"},
    {GST_DECKLINK_MODE_2160p24, "4k 24p", "2160p24"},
    {GST_DECKLINK_MODE_2160p25, "4k 25p", "2160p25"},
    {GST_DECKLINK_MODE_2160p2997, "4k 29.97p", "2160p2997"},
    {GST_DECKLINK_MODE_2160p30, "4k 30p", "2160p30"},
    {GST_DECKLINK_MODE_2160p50, "4k 50p", "2160p50"},
    {GST_DECKLINK_MODE_2160p5994, "4k 59.94p", "2160p5994"},
    {GST_DECKLINK_MODE_2160p60, "4k 60p", "2160p60"},

    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstDecklinkModes", modes);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_decklink_connection_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue connections[] = {
    {GST_DECKLINK_CONNECTION_AUTO, "Auto", "auto"},
    {GST_DECKLINK_CONNECTION_SDI, "SDI", "sdi"},
    {GST_DECKLINK_CONNECTION_HDMI, "HDMI", "hdmi"},
    {GST_DECKLINK_CONNECTION_OPTICAL_SDI, "Optical SDI", "optical-sdi"},
    {GST_DECKLINK_CONNECTION_COMPONENT, "Component", "component"},
    {GST_DECKLINK_CONNECTION_COMPOSITE, "Composite", "composite"},
    {GST_DECKLINK_CONNECTION_SVIDEO, "S-Video", "svideo"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstDecklinkConnection", connections);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_decklink_video_format_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue types[] = {
    {GST_DECKLINK_VIDEO_FORMAT_AUTO, "Auto", "auto"},
    {GST_DECKLINK_VIDEO_FORMAT_8BIT_YUV, "bmdFormat8BitYUV", "8bit-yuv"},
    {GST_DECKLINK_VIDEO_FORMAT_10BIT_YUV, "bmdFormat10BitYUV", "10bit-yuv"},
    {GST_DECKLINK_VIDEO_FORMAT_8BIT_ARGB, "bmdFormat8BitARGB", "8bit-argb"},
    {GST_DECKLINK_VIDEO_FORMAT_8BIT_BGRA, "bmdFormat8BitBGRA", "8bit-bgra"},
    /* Not yet supported:
       {GST_DECKLINK_VIDEO_FORMAT_10BIT_RGB, "bmdFormat10BitRGB", "10bit-rgb"},
       {GST_DECKLINK_VIDEO_FORMAT_12BIT_RGB, "bmdFormat12BitRGB", "12bit-rgb"},
       {GST_DECKLINK_VIDEO_FORMAT_12BIT_RGBLE, "bmdFormat12BitRGBLE", "12bit-rgble"},
       {GST_DECKLINK_VIDEO_FORMAT_10BIT_RGBXLE, "bmdFormat10BitRGBXLE", "10bit-rgbxle"},
       {GST_DECKLINK_VIDEO_FORMAT_10BIT_RGBX, "bmdFormat10BitRGBX", "10bit-rgbx"},
     */
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstDecklinkVideoFormat", types);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_decklink_timecode_format_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue timecodeformats[] = {
    {GST_DECKLINK_TIMECODE_FORMAT_RP188VITC1, "bmdTimecodeRP188VITC1",
        "rp188vitc1"},
    {GST_DECKLINK_TIMECODE_FORMAT_RP188VITC2, "bmdTimecodeRP188VITC2",
        "rp188vitc2"},
    {GST_DECKLINK_TIMECODE_FORMAT_RP188LTC, "bmdTimecodeRP188LTC", "rp188ltc"},
    {GST_DECKLINK_TIMECODE_FORMAT_RP188ANY, "bmdTimecodeRP188Any", "rp188any"},
    {GST_DECKLINK_TIMECODE_FORMAT_VITC, "bmdTimecodeVITC", "vitc"},
    {GST_DECKLINK_TIMECODE_FORMAT_VITCFIELD2, "bmdTimecodeVITCField2",
        "vitcfield2"},
    {GST_DECKLINK_TIMECODE_FORMAT_SERIAL, "bmdTimecodeSerial", "serial"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp =
        g_enum_register_static ("GstDecklinkTimecodeFormat", timecodeformats);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_decklink_audio_connection_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue connections[] = {
    {GST_DECKLINK_AUDIO_CONNECTION_AUTO, "Automatic", "auto"},
    {GST_DECKLINK_AUDIO_CONNECTION_EMBEDDED, "SDI/HDMI embedded audio",
        "embedded"},
    {GST_DECKLINK_AUDIO_CONNECTION_AES_EBU, "AES/EBU input", "aes"},
    {GST_DECKLINK_AUDIO_CONNECTION_ANALOG, "Analog input", "analog"},
    {GST_DECKLINK_AUDIO_CONNECTION_ANALOG_XLR, "Analog input (XLR)",
        "analog-xlr"},
    {GST_DECKLINK_AUDIO_CONNECTION_ANALOG_RCA, "Analog input (RCA)",
        "analog-rca"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp =
        g_enum_register_static ("GstDecklinkAudioConnection", connections);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_decklink_audio_channels_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue connections[] = {
    {GST_DECKLINK_AUDIO_CHANNELS_2, "2 Channels", "2"},
    {GST_DECKLINK_AUDIO_CHANNELS_8, "8 Channels", "8"},
    {GST_DECKLINK_AUDIO_CHANNELS_16, "16 Channels", "16"},
    {GST_DECKLINK_AUDIO_CHANNELS_MAX, "Maximum channels supported", "max"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp =
        g_enum_register_static ("GstDecklinkAudioChannels", connections);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

#define NTSC 10, 11, false, "bt601"
#define PAL 12, 11, true, "bt601"
#define HD 1, 1, true, "bt709"
#define UHD 1, 1, true, "bt2020"

static const GstDecklinkMode modes[] = {
  {bmdModeNTSC, 720, 486, 30000, 1001, true, NTSC},     // default is ntsc

  {bmdModeNTSC, 720, 486, 30000, 1001, true, NTSC},
  {bmdModeNTSC2398, 720, 486, 24000, 1001, true, NTSC},
  {bmdModePAL, 720, 576, 25, 1, true, PAL},
  {bmdModeNTSCp, 720, 486, 30000, 1001, false, NTSC},
  {bmdModePALp, 720, 576, 25, 1, false, PAL},

  {bmdModeHD1080p2398, 1920, 1080, 24000, 1001, false, HD},
  {bmdModeHD1080p24, 1920, 1080, 24, 1, false, HD},
  {bmdModeHD1080p25, 1920, 1080, 25, 1, false, HD},
  {bmdModeHD1080p2997, 1920, 1080, 30000, 1001, false, HD},
  {bmdModeHD1080p30, 1920, 1080, 30, 1, false, HD},

  {bmdModeHD1080i50, 1920, 1080, 25, 1, true, HD},
  {bmdModeHD1080i5994, 1920, 1080, 30000, 1001, true, HD},
  {bmdModeHD1080i6000, 1920, 1080, 30, 1, true, HD},

  {bmdModeHD1080p50, 1920, 1080, 50, 1, false, HD},
  {bmdModeHD1080p5994, 1920, 1080, 60000, 1001, false, HD},
  {bmdModeHD1080p6000, 1920, 1080, 60, 1, false, HD},

  {bmdModeHD720p50, 1280, 720, 50, 1, false, HD},
  {bmdModeHD720p5994, 1280, 720, 60000, 1001, false, HD},
  {bmdModeHD720p60, 1280, 720, 60, 1, false, HD},

  {bmdMode2k2398, 2048, 1556, 24000, 1001, false, HD},
  {bmdMode2k24, 2048, 1556, 24, 1, false, HD},
  {bmdMode2k25, 2048, 1556, 25, 1, false, HD},

  {bmdMode4K2160p2398, 3840, 2160, 24000, 1001, false, UHD},
  {bmdMode4K2160p24, 3840, 2160, 24, 1, false, UHD},
  {bmdMode4K2160p25, 3840, 2160, 25, 1, false, UHD},
  {bmdMode4K2160p2997, 3840, 2160, 30000, 1001, false, UHD},
  {bmdMode4K2160p30, 3840, 2160, 30, 1, false, UHD},
  {bmdMode4K2160p50, 3840, 2160, 50, 1, false, UHD},
  {bmdMode4K2160p5994, 3840, 2160, 60000, 1001, false, UHD},
  {bmdMode4K2160p60, 3840, 2160, 60, 1, false, UHD}
};

static const struct
{
  BMDPixelFormat format;
  gint bpp;
  GstVideoFormat vformat;
} formats[] = {
  /* *INDENT-OFF* */
  {bmdFormat8BitYUV, 2, GST_VIDEO_FORMAT_UYVY},  /* auto */
  {bmdFormat8BitYUV, 2, GST_VIDEO_FORMAT_UYVY},
  {bmdFormat10BitYUV, 4, GST_VIDEO_FORMAT_v210},
  {bmdFormat8BitARGB, 4, GST_VIDEO_FORMAT_ARGB},
  {bmdFormat8BitBGRA, 4, GST_VIDEO_FORMAT_BGRA},
/* Not yet supported
  {bmdFormat10BitRGB, FIXME, FIXME},
  {bmdFormat12BitRGB, FIXME, FIXME},
  {bmdFormat12BitRGBLE, FIXME, FIXME},
  {bmdFormat10BitRGBXLE, FIXME, FIXME},
  {bmdFormat10BitRGBX, FIXME, FIXME} */
  /* *INDENT-ON* */
};

static const struct
{
  BMDTimecodeFormat format;
  GstDecklinkTimecodeFormat gstformat;
} tcformats[] = {
  /* *INDENT-OFF* */
  {bmdTimecodeRP188VITC1, GST_DECKLINK_TIMECODE_FORMAT_RP188VITC1},
  {bmdTimecodeRP188VITC2, GST_DECKLINK_TIMECODE_FORMAT_RP188VITC2},
  {bmdTimecodeRP188LTC, GST_DECKLINK_TIMECODE_FORMAT_RP188LTC},
  {bmdTimecodeRP188Any, GST_DECKLINK_TIMECODE_FORMAT_RP188ANY},
  {bmdTimecodeVITC, GST_DECKLINK_TIMECODE_FORMAT_VITC},
  {bmdTimecodeVITCField2, GST_DECKLINK_TIMECODE_FORMAT_VITCFIELD2},
  {bmdTimecodeSerial, GST_DECKLINK_TIMECODE_FORMAT_SERIAL}
  /* *INDENT-ON* */
};

const GstDecklinkMode *
gst_decklink_get_mode (GstDecklinkModeEnum e)
{
  if (e < GST_DECKLINK_MODE_AUTO || e > GST_DECKLINK_MODE_2160p60)
    return NULL;
  return &modes[e];
}

const GstDecklinkModeEnum
gst_decklink_get_mode_enum_from_bmd (BMDDisplayMode mode)
{
  GstDecklinkModeEnum displayMode = GST_DECKLINK_MODE_NTSC;
  switch (mode) {
    case bmdModeNTSC:
      displayMode = GST_DECKLINK_MODE_NTSC;
      break;
    case bmdModeNTSC2398:
      displayMode = GST_DECKLINK_MODE_NTSC2398;
      break;
    case bmdModePAL:
      displayMode = GST_DECKLINK_MODE_PAL;
      break;
    case bmdModeNTSCp:
      displayMode = GST_DECKLINK_MODE_NTSC_P;
      break;
    case bmdModePALp:
      displayMode = GST_DECKLINK_MODE_PAL_P;
      break;
    case bmdModeHD1080p2398:
      displayMode = GST_DECKLINK_MODE_1080p2398;
      break;
    case bmdModeHD1080p24:
      displayMode = GST_DECKLINK_MODE_1080p24;
      break;
    case bmdModeHD1080p25:
      displayMode = GST_DECKLINK_MODE_1080p25;
      break;
    case bmdModeHD1080p2997:
      displayMode = GST_DECKLINK_MODE_1080p2997;
      break;
    case bmdModeHD1080p30:
      displayMode = GST_DECKLINK_MODE_1080p30;
      break;
    case bmdModeHD1080i50:
      displayMode = GST_DECKLINK_MODE_1080i50;
      break;
    case bmdModeHD1080i5994:
      displayMode = GST_DECKLINK_MODE_1080i5994;
      break;
    case bmdModeHD1080i6000:
      displayMode = GST_DECKLINK_MODE_1080i60;
      break;
    case bmdModeHD1080p50:
      displayMode = GST_DECKLINK_MODE_1080p50;
      break;
    case bmdModeHD1080p5994:
      displayMode = GST_DECKLINK_MODE_1080p5994;
      break;
    case bmdModeHD1080p6000:
      displayMode = GST_DECKLINK_MODE_1080p60;
      break;
    case bmdModeHD720p50:
      displayMode = GST_DECKLINK_MODE_720p50;
      break;
    case bmdModeHD720p5994:
      displayMode = GST_DECKLINK_MODE_720p5994;
      break;
    case bmdModeHD720p60:
      displayMode = GST_DECKLINK_MODE_720p60;
      break;
    case bmdMode2k2398:
      displayMode = GST_DECKLINK_MODE_1556p2398;
      break;
    case bmdMode2k24:
      displayMode = GST_DECKLINK_MODE_1556p24;
      break;
    case bmdMode2k25:
      displayMode = GST_DECKLINK_MODE_1556p25;
      break;
    case bmdMode4K2160p2398:
      displayMode = GST_DECKLINK_MODE_2160p2398;
      break;
    case bmdMode4K2160p24:
      displayMode = GST_DECKLINK_MODE_2160p24;
      break;
    case bmdMode4K2160p25:
      displayMode = GST_DECKLINK_MODE_2160p25;
      break;
    case bmdMode4K2160p2997:
      displayMode = GST_DECKLINK_MODE_2160p2997;
      break;
    case bmdMode4K2160p30:
      displayMode = GST_DECKLINK_MODE_2160p30;
      break;
    case bmdMode4K2160p50:
      displayMode = GST_DECKLINK_MODE_2160p50;
      break;
    case bmdMode4K2160p5994:
      displayMode = GST_DECKLINK_MODE_2160p5994;
      break;
    case bmdMode4K2160p60:
      displayMode = GST_DECKLINK_MODE_2160p60;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return displayMode;
}

const BMDPixelFormat
gst_decklink_pixel_format_from_type (GstDecklinkVideoFormat t)
{
  return formats[t].format;
}

const gint
gst_decklink_bpp_from_type (GstDecklinkVideoFormat t)
{
  return formats[t].bpp;
}

const GstDecklinkVideoFormat
gst_decklink_type_from_video_format (GstVideoFormat f)
{
  guint i;

  for (i = 1; i < G_N_ELEMENTS (formats); i++) {
    if (formats[i].vformat == f)
      return (GstDecklinkVideoFormat) i;
  }
  g_assert_not_reached ();
  return GST_DECKLINK_VIDEO_FORMAT_AUTO;
}

const BMDTimecodeFormat
gst_decklink_timecode_format_from_enum (GstDecklinkTimecodeFormat f)
{
  return tcformats[f].format;
}

const GstDecklinkTimecodeFormat
gst_decklink_timecode_format_to_enum (BMDTimecodeFormat f)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (tcformats); i++) {
    if (tcformats[i].format == f)
      return (GstDecklinkTimecodeFormat) i;
  }
  g_assert_not_reached ();
  return GST_DECKLINK_TIMECODE_FORMAT_RP188ANY;
}

static const BMDVideoConnection connections[] = {
  (BMDVideoConnection) 0,       /* auto */
  bmdVideoConnectionSDI,
  bmdVideoConnectionHDMI,
  bmdVideoConnectionOpticalSDI,
  bmdVideoConnectionComponent,
  bmdVideoConnectionComposite,
  bmdVideoConnectionSVideo
};

const BMDVideoConnection
gst_decklink_get_connection (GstDecklinkConnectionEnum e)
{
  g_return_val_if_fail (e != GST_DECKLINK_CONNECTION_AUTO,
      bmdVideoConnectionSDI);

  if (e <= GST_DECKLINK_CONNECTION_AUTO || e > GST_DECKLINK_CONNECTION_SVIDEO)
    e = GST_DECKLINK_CONNECTION_SDI;

  return connections[e];
}

static gboolean
gst_decklink_caps_get_pixel_format (GstCaps * caps, BMDPixelFormat * format)
{
  GstVideoInfo vinfo;
  GstVideoFormat f;

  if (gst_video_info_from_caps (&vinfo, caps) == FALSE) {
    GST_ERROR ("Could not get video info from caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  f = vinfo.finfo->format;
  return gst_decklink_type_from_video_format (f);
}

static GstStructure *
gst_decklink_mode_get_structure (GstDecklinkModeEnum e, BMDPixelFormat f,
    gboolean input)
{
  const GstDecklinkMode *mode = &modes[e];
  GstStructure *s = gst_structure_new ("video/x-raw",
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, mode->par_n, mode->par_d,
      "interlace-mode", G_TYPE_STRING,
      mode->interlaced ? "interleaved" : "progressive",
      "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d, NULL);

  if (input && mode->interlaced) {
    if (mode->tff)
      gst_structure_set (s, "field-order", G_TYPE_STRING, "top-field-first",
          NULL);
    else
      gst_structure_set (s, "field-order", G_TYPE_STRING, "bottom-field-first",
          NULL);
  }

  switch (f) {
    case bmdFormat8BitYUV:     /* '2vuy' */
      gst_structure_set (s, "format", G_TYPE_STRING, "UYVY",
          "colorimetry", G_TYPE_STRING, mode->colorimetry,
          "chroma-site", G_TYPE_STRING, "mpeg2", NULL);
      break;
    case bmdFormat10BitYUV:    /* 'v210' */
      gst_structure_set (s, "format", G_TYPE_STRING, "v210", NULL);
      break;
    case bmdFormat8BitARGB:    /* 'ARGB' */
      gst_structure_set (s, "format", G_TYPE_STRING, "ARGB", NULL);
      break;
    case bmdFormat8BitBGRA:    /* 'BGRA' */
      gst_structure_set (s, "format", G_TYPE_STRING, "BGRA", NULL);
      break;
    case bmdFormat10BitRGB:    /* 'r210' Big-endian RGB 10-bit per component with SMPTE video levels (64-960). Packed as 2:10:10:10 */
    case bmdFormat12BitRGB:    /* 'R12B' Big-endian RGB 12-bit per component with full range (0-4095). Packed as 12-bit per component */
    case bmdFormat12BitRGBLE:  /* 'R12L' Little-endian RGB 12-bit per component with full range (0-4095). Packed as 12-bit per component */
    case bmdFormat10BitRGBXLE: /* 'R10l' Little-endian 10-bit RGB with SMPTE video levels (64-940) */
    case bmdFormat10BitRGBX:   /* 'R10b' Big-endian 10-bit RGB with SMPTE video levels (64-940) */
    default:
      GST_WARNING ("format not supported %d", f);
      gst_structure_free (s);
      s = NULL;
      break;
  }

  return s;
}

GstCaps *
gst_decklink_mode_get_caps (GstDecklinkModeEnum e, BMDPixelFormat f,
    gboolean input)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();
  caps =
      gst_caps_merge_structure (caps, gst_decklink_mode_get_structure (e, f,
          input));

  return caps;
}

GstCaps *
gst_decklink_mode_get_caps_all_formats (GstDecklinkModeEnum e, gboolean input)
{
  GstCaps *caps;
  guint i;

  caps = gst_caps_new_empty ();
  for (i = 1; i < G_N_ELEMENTS (formats); i++)
    caps =
        gst_caps_merge_structure (caps, gst_decklink_mode_get_structure (e,
            formats[i].format, input));

  return caps;
}

GstCaps *
gst_decklink_pixel_format_get_caps (BMDPixelFormat f, gboolean input)
{
  int i;
  GstCaps *caps;
  GstStructure *s;

  caps = gst_caps_new_empty ();
  for (i = 1; i < (int) G_N_ELEMENTS (modes); i++) {
    s = gst_decklink_mode_get_structure ((GstDecklinkModeEnum) i, f, input);
    caps = gst_caps_merge_structure (caps, s);
  }

  return caps;
}

GstCaps *
gst_decklink_mode_get_template_caps (gboolean input)
{
  int i;
  GstCaps *caps;

  caps = gst_caps_new_empty ();
  for (i = 1; i < (int) G_N_ELEMENTS (modes); i++)
    caps =
        gst_caps_merge (caps,
        gst_decklink_mode_get_caps_all_formats ((GstDecklinkModeEnum) i,
            input));

  return caps;
}

const GstDecklinkMode *
gst_decklink_find_mode_and_format_for_caps (GstCaps * caps,
    BMDPixelFormat * format)
{
  int i;
  GstCaps *mode_caps;

  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);
  if (!gst_decklink_caps_get_pixel_format (caps, format))
    return NULL;

  for (i = 1; i < (int) G_N_ELEMENTS (modes); i++) {
    mode_caps =
        gst_decklink_mode_get_caps ((GstDecklinkModeEnum) i, *format, FALSE);
    if (gst_caps_can_intersect (caps, mode_caps)) {
      gst_caps_unref (mode_caps);
      return gst_decklink_get_mode ((GstDecklinkModeEnum) i);
    }
    gst_caps_unref (mode_caps);
  }

  return NULL;
}

const GstDecklinkMode *
gst_decklink_find_mode_for_caps (GstCaps * caps)
{
  BMDPixelFormat format;

  return gst_decklink_find_mode_and_format_for_caps (caps, &format);
}

#define GST_TYPE_DECKLINK_CLOCK \
  (gst_decklink_clock_get_type())
#define GST_DECKLINK_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECKLINK_CLOCK,GstDecklinkClock))
#define GST_DECKLINK_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECKLINK_CLOCK,GstDecklinkClockClass))
#define GST_IS_Decklink_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECKLINK_CLOCK))
#define GST_IS_Decklink_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECKLINK_CLOCK))
#define GST_DECKLINK_CLOCK_CAST(obj) \
  ((GstDecklinkClock*)(obj))

typedef struct _GstDecklinkClock GstDecklinkClock;
typedef struct _GstDecklinkClockClass GstDecklinkClockClass;

struct _GstDecklinkClock
{
  GstSystemClock clock;

  GstDecklinkOutput *output;
};

struct _GstDecklinkClockClass
{
  GstSystemClockClass parent_class;
};

GType gst_decklink_clock_get_type (void);
static GstClock *gst_decklink_clock_new (const gchar * name);

typedef struct _Device Device;
struct _Device
{
  GstDecklinkOutput output;
  GstDecklinkInput input;
};

class GStreamerDecklinkInputCallback:public IDeckLinkInputCallback
{
private:
  GstDecklinkInput * m_input;
  GMutex m_mutex;
  gint m_refcount;
public:
    GStreamerDecklinkInputCallback (GstDecklinkInput * input)
  : IDeckLinkInputCallback (), m_refcount (1)
  {
    m_input = input;
    g_mutex_init (&m_mutex);
  }

  virtual ~ GStreamerDecklinkInputCallback ()
  {
    g_mutex_clear (&m_mutex);
  }

  virtual HRESULT STDMETHODCALLTYPE QueryInterface (REFIID, LPVOID *)
  {
    return E_NOINTERFACE;
  }

  virtual ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount++;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);

    return ret;
  }

  virtual ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount--;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);


    if (ret == 0) {
      delete this;
    }

    return ret;
  }

  virtual HRESULT STDMETHODCALLTYPE
      VideoInputFormatChanged (BMDVideoInputFormatChangedEvents,
      IDeckLinkDisplayMode * mode, BMDDetectedVideoInputFormatFlags formatFlags)
  {
    BMDPixelFormat pixelFormat = bmdFormat8BitYUV;

    GST_INFO ("Video input format changed");

    if (formatFlags & bmdDetectedVideoInputRGB444)
      pixelFormat = bmdFormat8BitARGB;

    g_mutex_lock (&m_input->lock);
    m_input->input->PauseStreams ();
    m_input->input->EnableVideoInput (mode->GetDisplayMode (),
        pixelFormat, bmdVideoInputEnableFormatDetection);
    m_input->input->FlushStreams ();
    m_input->input->StartStreams ();
    m_input->mode =
        gst_decklink_get_mode (gst_decklink_get_mode_enum_from_bmd
        (mode->GetDisplayMode ()));
    m_input->format = pixelFormat;
    g_mutex_unlock (&m_input->lock);

    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE
      VideoInputFrameArrived (IDeckLinkVideoInputFrame * video_frame,
      IDeckLinkAudioInputPacket * audio_packet)
  {
    GstElement *videosrc = NULL, *audiosrc = NULL;
    void (*got_video_frame) (GstElement * videosrc,
        IDeckLinkVideoInputFrame * frame, GstDecklinkModeEnum mode,
        GstClockTime capture_time, GstClockTime stream_time,
        GstClockTime stream_duration, IDeckLinkTimecode * dtc, gboolean
        no_signal) = NULL;
    void (*got_audio_packet) (GstElement * videosrc,
        IDeckLinkAudioInputPacket * packet, GstClockTime capture_time,
        GstClockTime packet_time, gboolean no_signal) = NULL;
    GstDecklinkModeEnum mode;
    GstClockTime capture_time = GST_CLOCK_TIME_NONE;
    GstClockTime base_time = 0;
    gboolean no_signal = FALSE;
    GstClock *clock = NULL;
    HRESULT res;
    BMDTimeValue stream_time = GST_CLOCK_TIME_NONE;

    g_mutex_lock (&m_input->lock);
    if (m_input->videosrc) {
      videosrc = GST_ELEMENT_CAST (gst_object_ref (m_input->videosrc));
      clock = gst_element_get_clock (videosrc);
      base_time = gst_element_get_base_time (videosrc);
      got_video_frame = m_input->got_video_frame;
    }
    mode = gst_decklink_get_mode_enum_from_bmd (m_input->mode->mode);

    if (m_input->audiosrc) {
      audiosrc = GST_ELEMENT_CAST (gst_object_ref (m_input->audiosrc));
      if (!clock) {
        clock = gst_element_get_clock (GST_ELEMENT_CAST (audiosrc));
        base_time = gst_element_get_base_time (audiosrc);
      }
      got_audio_packet = m_input->got_audio_packet;
    }
    g_mutex_unlock (&m_input->lock);

    if (clock) {
      capture_time = gst_clock_get_time (clock);
      if (capture_time > base_time)
        capture_time -= base_time;
      else
        capture_time = 0;
    }

    if (video_frame) {
      BMDFrameFlags flags;

      flags = video_frame->GetFlags ();
      if (flags & bmdFrameHasNoInputSource) {
        no_signal = TRUE;
      }
    }

    if (got_video_frame && videosrc && video_frame) {
      BMDTimeValue stream_duration = GST_CLOCK_TIME_NONE;
      IDeckLinkTimecode *dtc = 0;

      res =
          video_frame->GetStreamTime (&stream_time, &stream_duration,
          GST_SECOND);
      if (res != S_OK) {
        GST_ERROR ("Failed to get stream time: 0x%08x", res);
        stream_time = GST_CLOCK_TIME_NONE;
        stream_duration = GST_CLOCK_TIME_NONE;
      }

      if (m_input->videosrc) {
        /* FIXME: Avoid circularity between gstdecklink.cpp and
         * gstdecklinkvideosrc.cpp */
        res =
            video_frame->
            GetTimecode (GST_DECKLINK_VIDEO_SRC (videosrc)->timecode_format,
            &dtc);

        if (res != S_OK) {
          GST_DEBUG_OBJECT (videosrc, "Failed to get timecode: 0x%08x", res);
          dtc = NULL;
        }
      }

      /* passing dtc reference */
      got_video_frame (videosrc, video_frame, mode, capture_time,
          stream_time, stream_duration, dtc, no_signal);
    }

    if (got_audio_packet && audiosrc && audio_packet) {
      m_input->got_audio_packet (audiosrc, audio_packet, capture_time,
          stream_time, no_signal);
    } else {
      if (!audio_packet)
        GST_DEBUG ("Received no audio packet at %" GST_TIME_FORMAT,
            GST_TIME_ARGS (capture_time));
    }

    gst_object_replace ((GstObject **) & videosrc, NULL);
    gst_object_replace ((GstObject **) & audiosrc, NULL);
    gst_object_replace ((GstObject **) & clock, NULL);

    return S_OK;
  }
};

#ifdef _MSC_VER
/* FIXME: We currently never deinit this */

static GMutex com_init_lock;
static GMutex com_deinit_lock;
static GCond com_init_cond;
static GCond com_deinit_cond;
static GCond com_deinited_cond;
static gboolean com_initialized = FALSE;

/* COM initialization/uninitialization thread */
static gpointer
gst_decklink_com_thread (gpointer data)
{
  HRESULT res;

  g_mutex_lock (&com_init_lock);

  /* Initialize COM with a MTA for this process. This thread will
   * be the first one to enter the apartement and the last one to leave
   * it, unitializing COM properly */

  res = CoInitializeEx (0, COINIT_MULTITHREADED);
  if (res == S_FALSE)
    GST_WARNING ("COM has been already initialized in the same process");
  else if (res == RPC_E_CHANGED_MODE)
    GST_WARNING ("The concurrency model of COM has changed.");
  else
    GST_INFO ("COM intialized succesfully");

  com_initialized = TRUE;

  /* Signal other threads waiting on this condition that COM was initialized */
  g_cond_signal (&com_init_cond);

  g_mutex_unlock (&com_init_lock);

  /* Wait until the unitialize condition is met to leave the COM apartement */
  g_mutex_lock (&com_deinit_lock);
  g_cond_wait (&com_deinit_cond, &com_deinit_lock);

  CoUninitialize ();
  GST_INFO ("COM unintialized succesfully");
  com_initialized = FALSE;
  g_cond_signal (&com_deinited_cond);
  g_mutex_unlock (&com_deinit_lock);

  return NULL;
}
#endif /* _MSC_VER */

static GOnce devices_once = G_ONCE_INIT;
static int n_devices;
static Device devices[10];

static gpointer
init_devices (gpointer data)
{
  IDeckLinkIterator *iterator;
  IDeckLink *decklink = NULL;
  HRESULT ret;
  int i;

#ifdef _MSC_VER
  // Start COM thread for Windows

  g_mutex_lock (&com_init_lock);

  /* create the COM initialization thread */
  g_thread_create ((GThreadFunc) gst_decklink_com_thread, NULL, FALSE, NULL);

  /* wait until the COM thread signals that COM has been initialized */
  g_cond_wait (&com_init_cond, &com_init_lock);
  g_mutex_unlock (&com_init_lock);
#endif /* _MSC_VER */

  iterator = CreateDeckLinkIteratorInstance ();
  if (iterator == NULL) {
    GST_ERROR ("no driver");
    return NULL;
  }

  i = 0;
  ret = iterator->Next (&decklink);
  while (ret == S_OK) {
    ret = decklink->QueryInterface (IID_IDeckLinkInput,
        (void **) &devices[i].input.input);
    if (ret != S_OK) {
      GST_WARNING ("selected device does not have input interface: 0x%08x",
          ret);
    } else {
      IDeckLinkDisplayModeIterator *mode_iter;

      devices[i].input.device = decklink;
      devices[i].input.
          input->SetCallback (new GStreamerDecklinkInputCallback (&devices[i].
              input));

      if ((ret =
              devices[i].input.input->GetDisplayModeIterator (&mode_iter)) ==
          S_OK) {
        IDeckLinkDisplayMode *mode;

        GST_DEBUG ("Input %d supports:", i);
        while ((ret = mode_iter->Next (&mode)) == S_OK) {
          char *name;

          mode->GetName ((COMSTR_T *) & name);
          CONVERT_COM_STRING (name);
          GST_DEBUG ("    %s mode: 0x%08x width: %ld height: %ld"
              " fields: 0x%08x flags: 0x%08x", name,
              (int) mode->GetDisplayMode (), mode->GetWidth (),
              mode->GetHeight (), (int) mode->GetFieldDominance (),
              (int) mode->GetFlags ());
          FREE_COM_STRING (name);
          mode->Release ();
        }
        mode_iter->Release ();
      }
      ret = S_OK;
    }

    ret = decklink->QueryInterface (IID_IDeckLinkOutput,
        (void **) &devices[i].output.output);
    if (ret != S_OK) {
      GST_WARNING ("selected device does not have output interface: 0x%08x",
          ret);
    } else {
      IDeckLinkDisplayModeIterator *mode_iter;

      devices[i].output.device = decklink;
      devices[i].output.clock =
          gst_decklink_clock_new ("GstDecklinkOutputClock");
      GST_DECKLINK_CLOCK_CAST (devices[i].output.clock)->output =
          &devices[i].output;

      if ((ret =
              devices[i].output.output->GetDisplayModeIterator (&mode_iter)) ==
          S_OK) {
        IDeckLinkDisplayMode *mode;

        GST_DEBUG ("Output %d supports:", i);
        while ((ret = mode_iter->Next (&mode)) == S_OK) {
          char *name;

          mode->GetName ((COMSTR_T *) & name);
          CONVERT_COM_STRING (name);
          GST_DEBUG ("    %s mode: 0x%08x width: %ld height: %ld"
              " fields: 0x%08x flags: 0x%08x", name,
              (int) mode->GetDisplayMode (), mode->GetWidth (),
              mode->GetHeight (), (int) mode->GetFieldDominance (),
              (int) mode->GetFlags ());
          FREE_COM_STRING (name);
          mode->Release ();
        }
        mode_iter->Release ();
      }
      ret = S_OK;
    }

    ret = decklink->QueryInterface (IID_IDeckLinkConfiguration,
        (void **) &devices[i].input.config);
    if (ret != S_OK) {
      GST_WARNING ("selected device does not have config interface: 0x%08x",
          ret);
    }

    ret = decklink->QueryInterface (IID_IDeckLinkAttributes,
        (void **) &devices[i].input.attributes);
    devices[i].output.attributes = devices[i].input.attributes;
    if (ret != S_OK) {
      GST_WARNING ("selected device does not have attributes interface: 0x%08x",
          ret);
    }

    ret = iterator->Next (&decklink);
    i++;

    if (i == 10) {
      GST_WARNING ("this hardware has more then 10 devices");
      break;
    }
  }

  n_devices = i;

  iterator->Release ();

  return NULL;
}

GstDecklinkOutput *
gst_decklink_acquire_nth_output (gint n, GstElement * sink, gboolean is_audio)
{
  GstDecklinkOutput *output;

  g_once (&devices_once, init_devices, NULL);

  if (n >= n_devices)
    return NULL;

  output = &devices[n].output;
  if (!output->output) {
    GST_ERROR ("Device %d has no output", n);
    return NULL;
  }

  g_mutex_lock (&output->lock);
  if (is_audio && !output->audiosink) {
    output->audiosink = GST_ELEMENT_CAST (gst_object_ref (sink));
    g_mutex_unlock (&output->lock);
    return output;
  } else if (!output->videosink) {
    output->videosink = GST_ELEMENT_CAST (gst_object_ref (sink));
    g_mutex_unlock (&output->lock);
    return output;
  }
  g_mutex_unlock (&output->lock);

  GST_ERROR ("Output device %d (audio: %d) in use already", n, is_audio);
  return NULL;
}

void
gst_decklink_release_nth_output (gint n, GstElement * sink, gboolean is_audio)
{
  GstDecklinkOutput *output;

  if (n >= n_devices)
    return;

  output = &devices[n].output;
  g_assert (output->output);

  g_mutex_lock (&output->lock);
  if (is_audio) {
    g_assert (output->audiosink == sink);
    gst_object_unref (sink);
    output->audiosink = NULL;
  } else {
    g_assert (output->videosink == sink);
    gst_object_unref (sink);
    output->videosink = NULL;
  }
  g_mutex_unlock (&output->lock);
}

void
gst_decklink_output_set_audio_clock (GstDecklinkOutput * output,
    GstClock * clock)
{
  g_mutex_lock (&output->lock);
  if (output->audio_clock)
    gst_object_unref (output->audio_clock);
  output->audio_clock = clock;
  if (clock)
    gst_object_ref (clock);
  g_mutex_unlock (&output->lock);
}


GstClock *
gst_decklink_output_get_audio_clock (GstDecklinkOutput * output)
{
  GstClock *ret = NULL;

  g_mutex_lock (&output->lock);
  if (output->audio_clock)
    ret = GST_CLOCK_CAST (gst_object_ref (output->audio_clock));
  g_mutex_unlock (&output->lock);

  return ret;
}

GstDecklinkInput *
gst_decklink_acquire_nth_input (gint n, GstElement * src, gboolean is_audio)
{
  GstDecklinkInput *input;

  g_once (&devices_once, init_devices, NULL);

  if (n >= n_devices)
    return NULL;

  input = &devices[n].input;
  if (!input->input) {
    GST_ERROR ("Device %d has no input", n);
    return NULL;
  }

  g_mutex_lock (&input->lock);
  if (is_audio && !input->audiosrc) {
    input->audiosrc = GST_ELEMENT_CAST (gst_object_ref (src));
    g_mutex_unlock (&input->lock);
    return input;
  } else if (!input->videosrc) {
    input->videosrc = GST_ELEMENT_CAST (gst_object_ref (src));
    g_mutex_unlock (&input->lock);
    return input;
  }
  g_mutex_unlock (&input->lock);

  GST_ERROR ("Input device %d (audio: %d) in use already", n, is_audio);
  return NULL;
}

void
gst_decklink_release_nth_input (gint n, GstElement * src, gboolean is_audio)
{
  GstDecklinkInput *input;

  if (n >= n_devices)
    return;

  input = &devices[n].input;
  g_assert (input->input);

  g_mutex_lock (&input->lock);
  if (is_audio) {
    g_assert (input->audiosrc == src);
    gst_object_unref (src);
    input->audiosrc = NULL;
  } else {
    g_assert (input->videosrc == src);
    gst_object_unref (src);
    input->videosrc = NULL;
  }
  g_mutex_unlock (&input->lock);
}

G_DEFINE_TYPE (GstDecklinkClock, gst_decklink_clock, GST_TYPE_SYSTEM_CLOCK);

static GstClockTime gst_decklink_clock_get_internal_time (GstClock * clock);

static void
gst_decklink_clock_class_init (GstDecklinkClockClass * klass)
{
  GstClockClass *clock_class = (GstClockClass *) klass;

  clock_class->get_internal_time = gst_decklink_clock_get_internal_time;
}

static void
gst_decklink_clock_init (GstDecklinkClock * clock)
{
  GST_OBJECT_FLAG_SET (clock, GST_CLOCK_FLAG_CAN_SET_MASTER);
}

static GstClock *
gst_decklink_clock_new (const gchar * name)
{
  GstDecklinkClock *self =
      GST_DECKLINK_CLOCK (g_object_new (GST_TYPE_DECKLINK_CLOCK, "name", name,
          "clock-type", GST_CLOCK_TYPE_OTHER, NULL));

  return GST_CLOCK_CAST (self);
}

static GstClockTime
gst_decklink_clock_get_internal_time (GstClock * clock)
{
  GstDecklinkClock *self = GST_DECKLINK_CLOCK (clock);
  GstClockTime result, start_time, last_time;
  GstClockTimeDiff offset;
  BMDTimeValue time;
  HRESULT ret;

  g_mutex_lock (&self->output->lock);
  start_time = self->output->clock_start_time;
  offset = self->output->clock_offset;
  last_time = self->output->clock_last_time;
  time = -1;
  if (!self->output->started) {
    result = last_time;
    ret = -1;
  } else {
    ret =
        self->output->output->GetHardwareReferenceClock (GST_SECOND, &time,
        NULL, NULL);
    if (ret == S_OK && time >= 0) {
      result = time;

      if (start_time == GST_CLOCK_TIME_NONE)
        start_time = self->output->clock_start_time = result;

      if (result > start_time)
        result -= start_time;
      else
        result = 0;

      if (self->output->clock_restart) {
        self->output->clock_offset = result - last_time;
        offset = self->output->clock_offset;
        self->output->clock_restart = FALSE;
      }
      result = MAX (last_time, result);
      result -= offset;
      result = MAX (last_time, result);
    } else {
      result = last_time;
    }

    self->output->clock_last_time = result;
  }
  result += self->output->clock_epoch;
  g_mutex_unlock (&self->output->lock);

  GST_LOG_OBJECT (clock,
      "result %" GST_TIME_FORMAT " time %" GST_TIME_FORMAT " last time %"
      GST_TIME_FORMAT " offset %" GST_TIME_FORMAT " start time %"
      GST_TIME_FORMAT " (ret: 0x%08x)", GST_TIME_ARGS (result),
      GST_TIME_ARGS (time), GST_TIME_ARGS (last_time), GST_TIME_ARGS (offset),
      GST_TIME_ARGS (start_time), ret);

  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decklink_debug, "decklink", 0,
      "debug category for decklink plugin");

  gst_element_register (plugin, "decklinkaudiosink", GST_RANK_NONE,
      GST_TYPE_DECKLINK_AUDIO_SINK);
  gst_element_register (plugin, "decklinkvideosink", GST_RANK_NONE,
      GST_TYPE_DECKLINK_VIDEO_SINK);
  gst_element_register (plugin, "decklinkaudiosrc", GST_RANK_NONE,
      GST_TYPE_DECKLINK_AUDIO_SRC);
  gst_element_register (plugin, "decklinkvideosrc", GST_RANK_NONE,
      GST_TYPE_DECKLINK_VIDEO_SRC);
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    decklink,
    "Blackmagic Decklink plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
