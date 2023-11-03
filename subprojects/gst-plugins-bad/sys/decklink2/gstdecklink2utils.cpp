/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklink2utils.h"
#include "gstdecklink2device.h"
#include <mutex>
#include <string>
#include <vector>

GST_DEBUG_CATEGORY_EXTERN (gst_decklink2_debug);
#define GST_CAT_DEFAULT gst_decklink2_debug

static guint api_version_major = 0;
static guint api_version_minor = 0;
static guint api_version_sub = 0;
static guint api_version_extra = 0;

#ifdef G_OS_WIN32
#include <condition_variable>

static GThread *win32_com_thread = NULL;
/* *INDENT-OFF* */
static std::mutex com_init_lock;
static std::mutex com_deinit_lock;
static std::condition_variable com_init_cond;
static std::condition_variable com_deinit_cond;
/* *INDENT-ON* */
static bool com_thread_running = false;
static bool exit_com_thread = false;

static gpointer
gst_decklink2_win32_com_thread (gpointer user_data)
{
  std::unique_lock < std::mutex > init_lk (com_init_lock);

  CoInitializeEx (0, COINIT_MULTITHREADED);
  com_thread_running = true;
  com_init_cond.notify_all ();
  init_lk.unlock ();

  std::unique_lock < std::mutex > deinit_lk (com_deinit_lock);
  while (!exit_com_thread) {
    com_deinit_cond.wait (deinit_lk);
  }
  CoUninitialize ();

  return NULL;
}

static IDeckLinkAPIInformation *
CreateDeckLinkAPIInformationInstance (void)
{
  IDeckLinkAPIInformation *info = NULL;

  CoCreateInstance (CLSID_CDeckLinkAPIInformation, NULL, CLSCTX_ALL,
      IID_IDeckLinkAPIInformation, (void **) &info);

  return info;
}
#endif

gboolean
gst_decklink2_init_once (void)
{
  static gboolean ret = FALSE;
  GST_DECKLINK2_CALL_ONCE_BEGIN {
    IDeckLinkAPIInformation *info = NULL;
    gint64 version = 0;
    HRESULT hr;

#ifdef G_OS_WIN32
    std::unique_lock < std::mutex > lk (com_init_lock);
    win32_com_thread = g_thread_new ("GstDeckLink2Win32",
        gst_decklink2_win32_com_thread, NULL);
    while (!com_thread_running)
      com_init_cond.wait (lk);
#endif

    info = CreateDeckLinkAPIInformationInstance ();
    if (!info)
      return;

    hr = info->GetInt (BMDDeckLinkAPIVersion, &version);
    if (gst_decklink2_result (hr)) {
      api_version_major = (version & 0xff000000) >> 24;
      api_version_minor = (version & 0xff0000) >> 16;
      api_version_sub = (version & 0xff00) >> 8;
      api_version_extra = version & 0xff;
      ret = TRUE;
    }

    info->Release ();
  }
  GST_DECKLINK2_CALL_ONCE_END;

  return ret;
}

void
gst_decklink2_deinit (void)
{
  gst_decklink2_device_deinit ();
#ifdef G_OS_WIN32
  std::unique_lock < std::mutex > deinit_lk (com_deinit_lock);
  if (win32_com_thread) {
    exit_com_thread = true;
    com_deinit_cond.notify_all ();
    deinit_lk.unlock ();
    g_thread_join (win32_com_thread);
    win32_com_thread = NULL;
  }
#endif
}

gboolean
gst_decklink2_get_api_version (guint * major, guint * minor, guint * sub,
    guint * extra)
{
  if (!gst_decklink2_init_once ())
    return FALSE;

  if (major)
    *major = api_version_major;
  if (minor)
    *minor = api_version_minor;
  if (sub)
    *sub = api_version_sub;
  if (extra)
    *extra = api_version_extra;

  return TRUE;
}

GstDeckLink2APILevel
gst_decklink2_get_api_level (void)
{
  static GstDeckLink2APILevel level = GST_DECKLINK2_API_LEVEL_UNKNOWN;

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    guint major, minor, sub;
    if (!gst_decklink2_get_api_version (&major, &minor, &sub, NULL))
      return;

    if (major >= 12) {
      level = GST_DECKLINK2_API_LEVEL_LATEST;
      return;
    } else if (major == 11) {
      if (minor > 5 || (minor == 5 && sub >= 1)) {
        level = GST_DECKLINK2_API_LEVEL_11_5_1;
      } else if (minor == 4) {
        level = GST_DECKLINK2_API_LEVEL_11_4;
      } else {
        level = GST_DECKLINK2_API_LEVEL_10_11;
      }
    } else if (major == 10 && minor >= 11) {
      level = GST_DECKLINK2_API_LEVEL_10_11;
    }
  }
  GST_DECKLINK2_CALL_ONCE_END;

  return level;
}

const gchar *
gst_decklink2_api_level_to_string (GstDeckLink2APILevel level)
{
  switch (level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      return "10.11";
    case GST_DECKLINK2_API_LEVEL_11_4:
      return "11.4";
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      return "11.5.1";
    case GST_DECKLINK2_API_LEVEL_LATEST:
      return "latest";
    default:
      break;
  }

  return "unknown";
}

GType
gst_decklink2_mode_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue modes[] = {
    {bmdModeUnknown, "Automatic detection", "auto"},

    {bmdModeNTSC, "NTSC SD 60i", "ntsc"},
    {bmdModeNTSC2398, "NTSC SD 60i (24 fps)", "ntsc2398"},
    {bmdModePAL, "PAL SD 50i", "pal"},
    {bmdModeNTSCp, "NTSC SD 60p", "ntsc-p"},
    {bmdModePALp, "PAL SD 50p", "pal-p"},

    {bmdModeNTSC_W, "NTSC SD 60i Widescreen", "ntsc-widescreen"},
    {bmdModeNTSC2398_W, "NTSC SD 60i Widescreen (24 fps)",
        "ntsc2398-widescreen"},
    {bmdModePAL_W, "PAL SD 50i Widescreen", "pal-widescreen"},
    {bmdModeNTSCp_W, "NTSC SD 60p Widescreen", "ntsc-p-widescreen"},
    {bmdModePALp_W, "PAL SD 50p Widescreen", "pal-p-widescreen"},

    {bmdModeHD1080p2398, "HD1080 23.98p", "1080p2398"},
    {bmdModeHD1080p24, "HD1080 24p", "1080p24"},
    {bmdModeHD1080p25, "HD1080 25p", "1080p25"},
    {bmdModeHD1080p2997, "HD1080 29.97p", "1080p2997"},
    {bmdModeHD1080p30, "HD1080 30p", "1080p30"},
    {bmdModeHD1080p50, "HD1080 50p", "1080p50"},
    {bmdModeHD1080p5994, "HD1080 59.94p", "1080p5994"},
    {bmdModeHD1080p6000, "HD1080 60p", "1080p60"},
    {bmdModeHD1080i50, "HD1080 50i", "1080i50"},
    {bmdModeHD1080i5994, "HD1080 59.94i", "1080i5994"},
    {bmdModeHD1080i6000, "HD1080 60i", "1080i60"},

    {bmdModeHD720p50, "HD720 50p", "720p50"},
    {bmdModeHD720p5994, "HD720 59.94p", "720p5994"},
    {bmdModeHD720p60, "HD720 60p", "720p60"},

    {bmdMode2k2398, "2k 23.98p", "1556p2398"},
    {bmdMode2k24, "2k 24p", "1556p24"},
    {bmdMode2k25, "2k 25p", "1556p25"},

    {bmdMode2kDCI2398, "2k dci 23.98p", "2kdcip2398"},
    {bmdMode2kDCI24, "2k dci 24p", "2kdcip24"},
    {bmdMode2kDCI25, "2k dci 25p", "2kdcip25"},
    {bmdMode2kDCI2997, "2k dci 29.97p", "2kdcip2997"},
    {bmdMode2kDCI30, "2k dci 30p", "2kdcip30"},
    {bmdMode2kDCI50, "2k dci 50p", "2kdcip50"},
    {bmdMode2kDCI5994, "2k dci 59.94p", "2kdcip5994"},
    {bmdMode2kDCI60, "2k dci 60p", "2kdcip60"},

    {bmdMode4K2160p2398, "4k 23.98p", "2160p2398"},
    {bmdMode4K2160p24, "4k 24p", "2160p24"},
    {bmdMode4K2160p25, "4k 25p", "2160p25"},
    {bmdMode4K2160p2997, "4k 29.97p", "2160p2997"},
    {bmdMode4K2160p30, "4k 30p", "2160p30"},
    {bmdMode4K2160p50, "4k 50p", "2160p50"},
    {bmdMode4K2160p5994, "4k 59.94p", "2160p5994"},
    {bmdMode4K2160p60, "4k 60p", "2160p60"},

    {bmdMode4kDCI2398, "4k dci 23.98p", "4kdcip2398"},
    {bmdMode4kDCI24, "4k dci 24p", "4kdcip24"},
    {bmdMode4kDCI25, "4k dci 25p", "4kdcip25"},
    {bmdMode4kDCI2997, "4k dci 29.97p", "4kdcip2997"},
    {bmdMode4kDCI30, "4k dci 30p", "4kdcip30"},
    {bmdMode4kDCI50, "4k dci 50p", "4kdcip50"},
    {bmdMode4kDCI5994, "4k dci 59.94p", "4kdcip5994"},
    {bmdMode4kDCI60, "4k dci 60p", "4kdcip60"},

    {bmdMode8K4320p2398, "8k 23.98p", "8kp2398"},
    {bmdMode8K4320p24, "8k 24p", "8kp24"},
    {bmdMode8K4320p25, "8k 25p", "8kp25"},
    {bmdMode8K4320p2997, "8k 29.97p", "8kp2997"},
    {bmdMode8K4320p30, "8k 30p", "8kp30"},
    {bmdMode8K4320p50, "8k 50p", "8kp50"},
    {bmdMode8K4320p5994, "8k 59.94p", "8kp5994"},
    {bmdMode8K4320p60, "8k 60p", "8kp60"},

    {bmdMode8kDCI2398, "8k dci 23.98p", "8kdcip2398"},
    {bmdMode8kDCI24, "8k dci 24p", "8kdcip24"},
    {bmdMode8kDCI25, "8k dci 25p", "8kdcip25"},
    {bmdMode8kDCI2997, "8k dci 29.97p", "8kdcip2997"},
    {bmdMode8kDCI30, "8k dci 30p", "8kdcip30"},
    {bmdMode8kDCI50, "8k dci 50p", "8kdcip50"},
    {bmdMode8kDCI5994, "8k dci 59.94p", "8kdcip5994"},
    {bmdMode8kDCI60, "8k dci 60p", "8kdcip60"},

    {bmdMode640x480p60, "640x480 60p", "640x480p60"},
    {bmdMode800x600p60, "800x600 60p", "800x600p60"},
    {bmdMode1440x900p50, "1440x900 50p", "1440x900p50"},
    {bmdMode1440x900p60, "1440x900 60p", "1440x900p60"},
    {bmdMode1440x1080p50, "1440x1080 50p", "1440x1080p50"},
    {bmdMode1440x1080p60, "1440x1080 60p", "1440x1080p60"},
    {bmdMode1600x1200p50, "1600x1200 50p", "1600x1200p50"},
    {bmdMode1600x1200p60, "1600x1200 60p", "1600x1200p60"},
    {bmdMode1920x1200p50, "1920x1200 50p", "1920x1200p50"},
    {bmdMode1920x1200p60, "1920x1200 60p", "1920x1200p60"},
    {bmdMode1920x1440p50, "1920x1440 50p", "1920x1440p50"},
    {bmdMode1920x1440p60, "1920x1440 60p", "1920x1440p60"},
    {bmdMode2560x1440p50, "2560x1440 50p", "2560x1440p50"},
    {bmdMode2560x1440p60, "2560x1440 60p", "2560x1440p60"},
    {bmdMode2560x1600p50, "2560x1600 50p", "2560x1600p50"},
    {bmdMode2560x1600p60, "2560x1600 60p", "2560x1600p60"},

    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2Mode", modes);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_video_format_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue formats[] = {
    {bmdFormatUnspecified, "Auto", "auto"},
    {bmdFormat8BitYUV, "bmdFormat8BitYUV", "8bit-yuv"},
    {bmdFormat10BitYUV, "bmdFormat10BitYUV", "10bit-yuv"},
    {bmdFormat8BitARGB, "bmdFormat8BitARGB", "8bit-argb"},
    {bmdFormat8BitBGRA, "bmdFormat8BitBGRA", "8bit-bgra"},
    {bmdFormat10BitRGB, "bmdFormat10BitRGB", "10bit-rgb"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2VideoFormat", formats);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_profile_id_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue profiles[] = {
    {bmdProfileDefault, "Default, don't change profile",
        "default"},
    {bmdProfileOneSubDeviceFullDuplex,
        "One sub-device, Full-Duplex", "one-sub-device-full"},
    {bmdProfileOneSubDeviceHalfDuplex,
        "One sub-device, Half-Duplex", "one-sub-device-half"},
    {bmdProfileTwoSubDevicesFullDuplex,
        "Two sub-devices, Full-Duplex", "two-sub-devices-full"},
    {bmdProfileTwoSubDevicesHalfDuplex,
        "Two sub-devices, Half-Duplex", "two-sub-devices-half"},
    {bmdProfileFourSubDevicesHalfDuplex,
        "Four sub-devices, Half-Duplex", "four-sub-devices-half"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2ProfileId", profiles);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_keyer_mode_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue modes[] = {
    {GST_DECKLINK2_KEYER_MODE_OFF, "Off", "off"},
    {GST_DECKLINK2_KEYER_MODE_INTERNAL, "Internal", "internal"},
    {GST_DECKLINK2_KEYER_MODE_EXTERNAL, "External", "external"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2KeyerMode", modes);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_mapping_format_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue formats[] = {
    {GST_DECKLINK2_MAPPING_FORMAT_DEFAULT,
          "Default, don't change mapping format",
        "default"},
    {GST_DECKLINK2_MAPPING_FORMAT_LEVEL_A, "Level A", "level-a"},
    {GST_DECKLINK2_MAPPING_FORMAT_LEVEL_B, "Level B", "level-b"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2MappingFormat", formats);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_timecode_format_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue formats[] = {
    {bmdTimecodeRP188VITC1, "bmdTimecodeRP188VITC1", "rp188vitc1"},
    {bmdTimecodeRP188VITC2, "bmdTimecodeRP188VITC2", "rp188vitc2"},
    {bmdTimecodeRP188LTC, "bmdTimecodeRP188LTC", "rp188ltc"},
    {bmdTimecodeRP188Any, "bmdTimecodeRP188Any", "rp188any"},
    {bmdTimecodeVITC, "bmdTimecodeVITC", "vitc"},
    {bmdTimecodeVITCField2, "bmdTimecodeVITCField2", "vitcfield2"},
    {bmdTimecodeSerial, "bmdTimecodeSerial", "serial"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2TimecodeFormat", formats);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_video_connection_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue connections[] = {
    {bmdVideoConnectionUnspecified, "Auto", "auto"},
    {bmdVideoConnectionSDI, "SDI", "sdi"},
    {bmdVideoConnectionHDMI, "HDMI", "hdmi"},
    {bmdVideoConnectionOpticalSDI, "Optical SDI", "optical-sdi"},
    {bmdVideoConnectionComponent, "Component", "component"},
    {bmdVideoConnectionComposite, "Composite", "composite"},
    {bmdVideoConnectionSVideo, "S-Video", "svideo"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2VideoConnection", connections);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_audio_connection_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue connections[] = {
    {bmdAudioConnectionUnspecified, "Auto", "auto"},
    {bmdAudioConnectionEmbedded, "SDI/HDMI embedded audio", "embedded"},
    {bmdAudioConnectionAESEBU, "AES/EBU", "aes"},
    {bmdAudioConnectionAnalog, "Analog", "analog"},
    {bmdAudioConnectionAnalogXLR, "Analog (XLR)", "analog-xlr"},
    {bmdAudioConnectionAnalogRCA, "Analog (RCA)", "analog-rca"},
#if 0
    {bmdAudioConnectionMicrophone, "Microphone", "microphone"},
    {bmdAudioConnectionHeadphones, "Headphones", "headphones"},
#endif
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2AudioConnection", connections);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

GType
gst_decklink2_audio_channels_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const GEnumValue channels[] = {
    {GST_DECKLINK2_AUDIO_CHANNELS_DISABLED, "Disabled", "disabled"},
    {GST_DECKLINK2_AUDIO_CHANNELS_2, "2 Channels", "2"},
    {GST_DECKLINK2_AUDIO_CHANNELS_8, "8 Channels", "8"},
    {GST_DECKLINK2_AUDIO_CHANNELS_16, "16 Channels", "16"},
    {GST_DECKLINK2_AUDIO_CHANNELS_MAX, "Maximum channels supported", "max"},
    {0, NULL, NULL}
  };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstDeckLink2AudioChannels", channels);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

#define NTSC 10, 11, FALSE
#define PAL 12, 11, TRUE
#define NTSC_WS 40, 33, FALSE
#define PAL_WS 16, 11, TRUE
#define HD 1, 1, TRUE
#define UHD 1, 1, TRUE

static const GstDeckLink2DisplayMode all_modes[] = {
  {bmdModeUnknown, 0, 0, 0, 0, FALSE, 0, 0, 0},
  /* SD Modes */
  {bmdModeNTSC, 720, 486, 30000, 1001, TRUE, NTSC},
  {bmdModeNTSC2398, 720, 486, 24000, 1001, TRUE, NTSC},
  {bmdModePAL, 720, 576, 25, 1, TRUE, PAL},
  {bmdModeNTSCp, 720, 486, 30000, 1001, FALSE, NTSC},
  {bmdModePALp, 720, 576, 25, 1, FALSE, PAL},

  /* Custom wide modes */
  {bmdModeNTSC_W, 720, 486, 30000, 1001, TRUE, NTSC_WS},
  {bmdModeNTSC2398_W, 720, 486, 24000, 1001, TRUE, NTSC_WS},
  {bmdModePAL_W, 720, 576, 25, 1, TRUE, PAL_WS},
  {bmdModeNTSCp_W, 720, 486, 30000, 1001, FALSE, NTSC_WS},
  {bmdModePALp_W, 720, 576, 25, 1, FALSE, PAL_WS},

  /* HD 1080 Modes */
  {bmdModeHD1080p2398, 1920, 1080, 24000, 1001, FALSE, HD},
  {bmdModeHD1080p24, 1920, 1080, 24, 1, FALSE, HD},
  {bmdModeHD1080p25, 1920, 1080, 25, 1, FALSE, HD},
  {bmdModeHD1080p2997, 1920, 1080, 30000, 1001, FALSE, HD},
  {bmdModeHD1080p30, 1920, 1080, 30, 1, FALSE, HD},
  /* FIXME:
   * bmdModeHD1080p4795
   * bmdModeHD1080p48
   */
  {bmdModeHD1080p50, 1920, 1080, 50, 1, FALSE, HD},
  {bmdModeHD1080p5994, 1920, 1080, 60000, 1001, FALSE, HD},
  {bmdModeHD1080p6000, 1920, 1080, 60, 1, FALSE, HD},
  /* FIXME:
   * bmdModeHD1080p9590
   * bmdModeHD1080p96
   * bmdModeHD1080p100
   * bmdModeHD1080p11988
   * bmdModeHD1080p120
   */
  {bmdModeHD1080i50, 1920, 1080, 25, 1, TRUE, HD},
  {bmdModeHD1080i5994, 1920, 1080, 30000, 1001, TRUE, HD},
  {bmdModeHD1080i6000, 1920, 1080, 30, 1, TRUE, HD},

  /* HD 720 Modes */
  {bmdModeHD720p50, 1280, 720, 50, 1, FALSE, HD},
  {bmdModeHD720p5994, 1280, 720, 60000, 1001, FALSE, HD},
  {bmdModeHD720p60, 1280, 720, 60, 1, FALSE, HD},

  /* 2K Modes */
  {bmdMode2k2398, 2048, 1556, 24000, 1001, FALSE, HD},
  {bmdMode2k24, 2048, 1556, 24, 1, FALSE, HD},
  {bmdMode2k25, 2048, 1556, 25, 1, FALSE, HD},

  /* 2K DCI Modes */
  {bmdMode2kDCI2398, 2048, 1080, 24000, 1001, FALSE, HD},
  {bmdMode2kDCI24, 2048, 1080, 24, 1, FALSE, HD},
  {bmdMode2kDCI25, 2048, 1080, 25, 1, FALSE, HD},
  {bmdMode2kDCI2997, 2048, 1080, 30000, 1001, FALSE, HD},
  {bmdMode2kDCI30, 2048, 1080, 30, 1, FALSE, HD},
  /* FIXME:
   * bmdMode2kDCI4795
   * bmdMode2kDCI48
   */
  {bmdMode2kDCI50, 2048, 1080, 50, 1, FALSE, HD},
  {bmdMode2kDCI5994, 2048, 1080, 60000, 1001, FALSE, HD},
  {bmdMode2kDCI60, 2048, 1080, 60, 1, FALSE, HD},
  /* FIXME:
   * bmdMode2kDCI9590
   * bmdMode2kDCI96
   * bmdMode2kDCI100
   * bmdMode2kDCI11988
   * bmdMode2kDCI120
   */

  /* 4K UHD Modes */
  {bmdMode4K2160p2398, 3840, 2160, 24000, 1001, FALSE, UHD},
  {bmdMode4K2160p24, 3840, 2160, 24, 1, FALSE, UHD},
  {bmdMode4K2160p25, 3840, 2160, 25, 1, FALSE, UHD},
  {bmdMode4K2160p2997, 3840, 2160, 30000, 1001, FALSE, UHD},
  {bmdMode4K2160p30, 3840, 2160, 30, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode4K2160p4795
   * bmdMode4K2160p48
   */
  {bmdMode4K2160p50, 3840, 2160, 50, 1, FALSE, UHD},
  {bmdMode4K2160p5994, 3840, 2160, 60000, 1001, FALSE, UHD},
  {bmdMode4K2160p60, 3840, 2160, 60, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode4K2160p9590
   * bmdMode4K2160p96
   * bmdMode4K2160p100
   * bmdMode4K2160p11988
   * bmdMode4K2160p120
   */

  /* 4K DCI Modes */
  {bmdMode4kDCI2398, 4096, 2160, 24000, 1001, FALSE, UHD},
  {bmdMode4kDCI24, 4096, 2160, 24, 1, FALSE, UHD},
  {bmdMode4kDCI25, 4096, 2160, 25, 1, FALSE, UHD},
  {bmdMode4kDCI2997, 4096, 2160, 30000, 1001, FALSE, UHD},
  {bmdMode4kDCI30, 4096, 2160, 30, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode4kDCI4795
   * bmdMode4kDCI48
   */
  {bmdMode4kDCI50, 4096, 2160, 50, 1, FALSE, UHD},
  {bmdMode4kDCI5994, 4096, 2160, 60000, 1001, FALSE, UHD},
  {bmdMode4kDCI60, 4096, 2160, 60, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode4kDCI9590
   * bmdMode4kDCI96
   * bmdMode4kDCI100
   * bmdMode4kDCI11988
   * bmdMode4kDCI120
   */

  /* 8K UHD Modes */
  {bmdMode8K4320p2398, 7680, 4320, 24000, 1001, FALSE, UHD},
  {bmdMode8K4320p24, 7680, 4320, 24, 1, FALSE, UHD},
  {bmdMode8K4320p25, 7680, 4320, 25, 1, FALSE, UHD},
  {bmdMode8K4320p2997, 7680, 4320, 30000, 1001, FALSE, UHD},
  {bmdMode8K4320p30, 7680, 4320, 30, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode8K4320p4795
   * bmdMode8K4320p48
   */
  {bmdMode8K4320p50, 7680, 4320, 50, 1, FALSE, UHD},
  {bmdMode8K4320p5994, 7680, 4320, 60000, 1001, FALSE, UHD},
  {bmdMode8K4320p60, 7680, 4320, 60, 1, FALSE, UHD},

  /* 8K DCI Modes */
  {bmdMode8kDCI2398, 8192, 4320, 24000, 1001, FALSE, UHD},
  {bmdMode8kDCI24, 8192, 4320, 24, 1, FALSE, UHD},
  {bmdMode8kDCI25, 8192, 4320, 25, 1, FALSE, UHD},
  {bmdMode8kDCI2997, 8192, 4320, 30000, 1001, FALSE, UHD},
  {bmdMode8kDCI30, 8192, 4320, 30, 1, FALSE, UHD},
  /* FIXME:
   * bmdMode8kDCI4795
   * bmdMode8kDCI48
   */
  {bmdMode8kDCI50, 8192, 4320, 50, 1, FALSE, UHD},
  {bmdMode8kDCI5994, 8192, 4320, 60000, 1001, FALSE, UHD},
  {bmdMode8kDCI60, 8192, 4320, 60, 1, FALSE, UHD},

  {bmdMode640x480p60, 640, 480, 60, 1, FALSE, HD},
  {bmdMode800x600p60, 800, 600, 60, 1, FALSE, HD},
  {bmdMode1440x900p50, 1440, 900, 50, 1, FALSE, HD},
  {bmdMode1440x900p60, 1440, 900, 60, 1, FALSE, HD},
  {bmdMode1440x1080p50, 1440, 1080, 50, 1, FALSE, HD},
  {bmdMode1440x1080p60, 1440, 1080, 60, 1, FALSE, HD},
  {bmdMode1600x1200p50, 1600, 1200, 50, 1, FALSE, HD},
  {bmdMode1600x1200p60, 1600, 1200, 60, 1, FALSE, HD},
  {bmdMode1920x1200p50, 1920, 1200, 50, 1, FALSE, HD},
  {bmdMode1920x1200p60, 1920, 1200, 60, 1, FALSE, HD},
  {bmdMode1920x1440p50, 1920, 1440, 50, 1, FALSE, HD},
  {bmdMode1920x1440p60, 1920, 1440, 60, 1, FALSE, HD},
  {bmdMode2560x1440p50, 2560, 1440, 50, 1, FALSE, HD},
  {bmdMode2560x1440p60, 2560, 1440, 60, 1, FALSE, HD},
  {bmdMode2560x1600p50, 2560, 1600, 50, 1, FALSE, HD},
  {bmdMode2560x1600p60, 2560, 1600, 60, 1, FALSE, HD},
};

static const struct
{
  BMDPixelFormat format;
  gint bpp;
  GstVideoFormat vformat;
} formats[] = {
  {bmdFormatUnspecified, 0, GST_VIDEO_FORMAT_UNKNOWN},
  {bmdFormat8BitYUV, 2, GST_VIDEO_FORMAT_UYVY},
  {bmdFormat10BitYUV, 4, GST_VIDEO_FORMAT_v210},
  {bmdFormat8BitARGB, 4, GST_VIDEO_FORMAT_ARGB},
  {bmdFormat8BitBGRA, 4, GST_VIDEO_FORMAT_BGRA},
  {bmdFormat10BitRGB, 4, GST_VIDEO_FORMAT_r210},
};

GstVideoFormat
gst_decklink2_video_format_from_pixel_format (BMDPixelFormat format)
{
  switch (format) {
    case bmdFormat8BitYUV:
      return GST_VIDEO_FORMAT_UYVY;
    case bmdFormat10BitYUV:
      return GST_VIDEO_FORMAT_v210;
    case bmdFormat8BitARGB:
      return GST_VIDEO_FORMAT_ARGB;
    case bmdFormat8BitBGRA:
      return GST_VIDEO_FORMAT_BGRA;
    case bmdFormat10BitRGB:
      return GST_VIDEO_FORMAT_r210;
    default:
      break;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

BMDPixelFormat
gst_decklink2_pixel_format_from_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_UYVY:
      return bmdFormat8BitYUV;
    case GST_VIDEO_FORMAT_v210:
      return bmdFormat10BitYUV;
    case GST_VIDEO_FORMAT_ARGB:
      return bmdFormat8BitARGB;
    case GST_VIDEO_FORMAT_BGRA:
      return bmdFormat8BitBGRA;
    case GST_VIDEO_FORMAT_r210:
      return bmdFormat10BitRGB;
    default:
      break;
  }

  return bmdFormatUnspecified;
}


static const gchar *
gst_decklink2_pixel_format_to_string (BMDPixelFormat format)
{
  switch (format) {
    case bmdFormat8BitYUV:
      return "UYVY";
    case bmdFormat10BitYUV:
      return "v210";
    case bmdFormat8BitARGB:
      return "ARGB";
    case bmdFormat8BitBGRA:
      return "BGRA";
    case bmdFormat10BitRGB:
      return "r210";
    default:
      g_assert_not_reached ();
      break;
  }

  return "";
}

static GstStructure *
gst_decklink2_mode_get_structure (const GstDeckLink2DisplayMode * mode,
    BMDPixelFormat f)
{
  GstStructure *s = gst_structure_new ("video/x-raw",
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, mode->par_n, mode->par_d,
      "interlace-mode", G_TYPE_STRING,
      mode->interlaced ? "interleaved" : "progressive",
      "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d, NULL);

  switch (f) {
    case bmdFormatUnspecified:
    {
      GValue format_values = G_VALUE_INIT;

      g_value_init (&format_values, GST_TYPE_LIST);
      for (guint i = 1; i < G_N_ELEMENTS (formats); i++) {
        GValue f = G_VALUE_INIT;

        g_value_init (&f, G_TYPE_STRING);
        g_value_set_static_string (&f,
            gst_decklink2_pixel_format_to_string (formats[i].format));
        gst_value_list_append_and_take_value (&format_values, &f);
      }

      gst_structure_set_value (s, "format", &format_values);
      g_value_unset (&format_values);
      break;
    }
    case bmdFormat8BitYUV:
      gst_structure_set (s, "format", G_TYPE_STRING, "UYVY", NULL);
      break;
    case bmdFormat10BitYUV:
      gst_structure_set (s, "format", G_TYPE_STRING, "v210", NULL);
      break;
    case bmdFormat8BitARGB:
      gst_structure_set (s, "format", G_TYPE_STRING, "ARGB", NULL);
      break;
    case bmdFormat8BitBGRA:
      gst_structure_set (s, "format", G_TYPE_STRING, "BGRA", NULL);
      break;
    case bmdFormat10BitRGB:
      gst_structure_set (s, "format", G_TYPE_STRING, "r210", NULL);
      break;
    default:
      GST_WARNING ("format not supported %d", f);
      gst_structure_free (s);
      s = NULL;
      break;
  }

  return s;
}

/* *INDENT-OFF* */
template<typename T>
static void
get_color_capabilities (T * attr, GstDeckLink2ColorCapabilities * caps)
{
  dlbool_t supported = false;
  int64_t range;

  if (attr->GetFlag (BMDDeckLinkSupportsColorspaceMetadata, &supported) == S_OK)
    caps->supports_colorspace = supported;

  supported = false;
  if (attr->GetFlag (BMDDeckLinkSupportsHDRMetadata, &supported) == S_OK)
    caps->supports_hdr = supported;

  if (caps->supports_hdr &&
      attr->GetInt (BMDDeckLinkSupportedDynamicRange, &range) == S_OK) {
    caps->dynamic_range = (BMDDynamicRange) range;
  }
}
/* *INDENT-ON* */

void
gst_decklink2_get_color_capabilities (IDeckLink * device,
    GstDeckLink2ColorCapabilities * caps)
{
  IDeckLinkProfileAttributes *attr = nullptr;
  IDeckLinkAttributes_v10_11 *attr_10_11 = nullptr;

  *caps = { };
  caps->dynamic_range = bmdDynamicRangeSDR;

  if (device->QueryInterface (IID_IDeckLinkProfileAttributes,
          (void **) &attr) == S_OK) {
    get_color_capabilities (attr, caps);
    attr->Release ();
  } else if (device->QueryInterface (IID_IDeckLinkAttributes_v10_11,
          (void **) &attr_10_11) == S_OK) {
    get_color_capabilities (attr_10_11, caps);
    attr_10_11->Release ();
  }
}

BMDDisplayModeFlags
gst_decklink2_get_colorspace_flags (const GstDeckLink2DisplayMode * mode,
    const GstDeckLink2ColorCapabilities * caps)
{
  BMDDisplayModeFlags flags;

  switch (gst_decklink2_get_real_display_mode (mode->mode)) {
    case bmdModeNTSC:
    case bmdModeNTSC2398:
    case bmdModeNTSCp:
    case bmdModePAL:
    case bmdModePALp:
      flags = bmdDisplayModeColorspaceRec601;
      break;
    default:
      flags = (BMDDisplayModeFlags) (bmdDisplayModeColorspaceRec709 |
          bmdDisplayModeColorspaceRec2020);
      break;
  }
  if (!caps->supports_colorspace)
    flags = (BMDDisplayModeFlags) (flags & mode->flags);
  return flags;
}

static void
append_color_caps (GstCaps * caps, GstStructure * s,
    const GstDeckLink2DisplayMode * mode,
    const GstDeckLink2ColorCapabilities * color_caps)
{
  auto flags = gst_decklink2_get_colorspace_flags (mode, color_caps);
  const auto formats = gst_structure_get_value (s, "format");
  guint count = GST_VALUE_HOLDS_LIST (formats) ?
      gst_value_list_get_size (formats) : 1;

  for (guint i = 0; i < count; i++) {
    const auto value = GST_VALUE_HOLDS_LIST (formats) ?
        gst_value_list_get_value (formats, i) : formats;
    const auto format = g_value_get_string (value);
    auto base = gst_structure_copy (s);
    gst_structure_set (base, "format", G_TYPE_STRING, format, nullptr);

    if (g_str_equal (format, "UYVY") || g_str_equal (format, "v210")) {
      auto append =[&](const gchar * color) {
        auto copy = gst_structure_copy (base);
        gst_structure_set (copy, "colorimetry", G_TYPE_STRING, color, nullptr);
        gst_caps_append_structure (caps, copy);
      };
      if (flags & bmdDisplayModeColorspaceRec601)
        append (GST_VIDEO_COLORIMETRY_BT601);
      if (flags & bmdDisplayModeColorspaceRec709)
        append (GST_VIDEO_COLORIMETRY_BT709);
      if (flags & bmdDisplayModeColorspaceRec2020) {
        append (GST_VIDEO_COLORIMETRY_BT2020);
        if (color_caps->dynamic_range & bmdDynamicRangeHDRStaticPQ)
          append (GST_VIDEO_COLORIMETRY_BT2100_PQ);
        if (color_caps->dynamic_range & bmdDynamicRangeHDRStaticHLG)
          append (GST_VIDEO_COLORIMETRY_BT2100_HLG);
      }
      gst_structure_free (base);
    } else {
      gst_caps_append_structure (caps, base);
    }
  }
  gst_structure_free (s);
}

static GstCaps *
gst_decklink2_mode_get_all_format_caps (const GstDeckLink2DisplayMode * mode)
{
  GstCaps *caps;
  GstStructure *s;

  if (!mode)
    return NULL;

  caps = gst_caps_new_empty ();
  s = gst_decklink2_mode_get_structure (mode, bmdFormatUnspecified);

  GstDeckLink2ColorCapabilities color_caps;
  color_caps.supports_hdr = TRUE;
  color_caps.supports_colorspace = TRUE;
  color_caps.dynamic_range = (BMDDynamicRange) (bmdDynamicRangeSDR |
      bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG);

  append_color_caps (caps, s, mode, &color_caps);
  return caps;
}

static gboolean
gst_decklink2_get_display_mode (BMDDisplayMode mode,
    GstDeckLink2DisplayMode * display_mode)
{
  for (guint i = 0; i < G_N_ELEMENTS (all_modes); i++) {
    if (mode == all_modes[i].mode) {
      *display_mode = all_modes[i];
      return TRUE;
    }
  }

  return FALSE;
}

BMDDisplayMode
gst_decklink2_get_real_display_mode (BMDDisplayMode mode)
{
  switch (mode) {
    case bmdModeNTSC_W:
      return bmdModeNTSC;
    case bmdModeNTSC2398_W:
      return bmdModeNTSC2398;
    case bmdModePAL_W:
      return bmdModePAL;
    case bmdModeNTSCp_W:
      return bmdModeNTSCp;
    case bmdModePALp_W:
      return bmdModePALp;
    default:
      break;
  }

  return mode;
}

GstCaps *
gst_decklink2_build_caps (GstObject * io_object,
    IDeckLinkDisplayModeIterator * iter, BMDDisplayMode requested_mode,
    BMDPixelFormat format, GstDeckLink2DoesSupportVideoMode func,
    const GstDeckLink2ColorCapabilities * color_caps)
{
  HRESULT hr = S_OK;
  GstCaps *caps = NULL;
  BMDDisplayMode real_mode =
      gst_decklink2_get_real_display_mode (requested_mode);

  GST_LOG_OBJECT (io_object, "Building caps, mode: %" GST_FOURCC_FORMAT
      ", format: %" GST_FOURCC_FORMAT,
      GST_DECKLINK2_FOURCC_ARGS (requested_mode),
      GST_DECKLINK2_FOURCC_ARGS (format));

  do {
    IDeckLinkDisplayMode *mode = NULL;
    BMDDisplayMode bdm_mode;
    GstDeckLink2DisplayMode gst_mode;
    std::vector < std::string > formats;
    GstStructure *s;
    GstStructure *s_wide = NULL;
    const gchar *format_str;

    hr = iter->Next (&mode);
    if (hr != S_OK)
      break;

    bdm_mode = mode->GetDisplayMode ();
    if (requested_mode != bmdModeUnknown) {
      if (real_mode != bdm_mode)
        goto next;

      if (!gst_decklink2_get_display_mode (requested_mode, &gst_mode)) {
        GST_WARNING_OBJECT (io_object, "Couldn't get mode");
        goto next;
      }
    } else if (!gst_decklink2_get_display_mode (bdm_mode, &gst_mode)) {
      goto next;
    }

    if (format == bmdFormatUnspecified) {
      if (func (io_object, bdm_mode, bmdFormat8BitYUV)) {
        format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitYUV);
        formats.push_back (format_str);
      }

      if (func (io_object, bdm_mode, bmdFormat10BitYUV)) {
        format_str = gst_decklink2_pixel_format_to_string (bmdFormat10BitYUV);
        formats.push_back (format_str);
      }

      if (func (io_object, bdm_mode, bmdFormat8BitARGB)) {
        format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitARGB);
        formats.push_back (format_str);
      }

      if (func (io_object, bdm_mode, bmdFormat8BitBGRA)) {
        format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitBGRA);
        formats.push_back (format_str);
      }

      if (func (io_object, bdm_mode, bmdFormat10BitRGB)) {
        format_str = gst_decklink2_pixel_format_to_string (bmdFormat10BitRGB);
        formats.push_back (format_str);
      }
    } else if (func (io_object, bdm_mode, format)) {
      format_str = gst_decklink2_pixel_format_to_string (format);
      formats.push_back (format_str);
    }

    if (formats.empty ())
      goto next;

    if (!caps)
      caps = gst_caps_new_empty ();

    gst_mode.flags = mode->GetFlags ();
    gst_mode.width = (gint) mode->GetWidth ();
    gst_mode.height = (gint) mode->GetHeight ();

    s = gst_structure_new ("video/x-raw",
        "width", G_TYPE_INT, gst_mode.width,
        "height", G_TYPE_INT, gst_mode.height,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, gst_mode.par_n, gst_mode.par_d,
        "interlace-mode", G_TYPE_STRING,
        gst_mode.interlaced ? "interleaved" : "progressive",
        "framerate", GST_TYPE_FRACTION, gst_mode.fps_n, gst_mode.fps_d, NULL);

    if (gst_mode.interlaced) {
      BMDFieldDominance field;

      field = mode->GetFieldDominance ();
      if (field == bmdLowerFieldFirst)
        gst_mode.tff = FALSE;
      else if (field == bmdUpperFieldFirst)
        gst_mode.tff = TRUE;

      if (gst_mode.tff) {
        gst_structure_set (s,
            "field-order", G_TYPE_STRING, "top-field-first", NULL);
      } else {
        gst_structure_set (s,
            "field-order", G_TYPE_STRING, "bottom-field-first", NULL);
      }
    }

    if (formats.size () == 1) {
      gst_structure_set (s, "format", G_TYPE_STRING, formats[0].c_str (), NULL);
    } else {
      GValue format_values = G_VALUE_INIT;

      g_value_init (&format_values, GST_TYPE_LIST);

      /* *INDENT-OFF* */
      for (const auto & iter: formats) {
        GValue f = G_VALUE_INIT;

        g_value_init (&f, G_TYPE_STRING);
        g_value_set_string (&f, iter.c_str ());
        gst_value_list_append_and_take_value (&format_values, &f);
      }
      /* *INDENT-ON* */

      gst_structure_set_value (s, "format", &format_values);
      g_value_unset (&format_values);
    }

    /* Add custom wide mode */
    if (requested_mode == bmdModeUnknown) {
      switch (bdm_mode) {
        case bmdModeNTSC:
        case bmdModeNTSC2398:
        case bmdModeNTSCp:
          s_wide = gst_structure_copy (s);
          gst_structure_set (s_wide,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, 40, 33, NULL);
          break;
        case bmdModePAL:
        case bmdModePALp:
          s_wide = gst_structure_copy (s);
          gst_structure_set (s_wide,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, 16, 11, NULL);
          break;
        default:
          break;
      }
    }

    append_color_caps (caps, s, &gst_mode, color_caps);
    if (s_wide)
      append_color_caps (caps, s_wide, &gst_mode, color_caps);

  next:
    mode->Release ();
  } while (gst_decklink2_result (hr));

  return caps;
}

GstCaps *
gst_decklink2_build_template_caps (GstObject * io_object,
    IDeckLinkDisplayModeIterator * iter, GstDeckLink2DoesSupportVideoMode func,
    GArray * format_table, const GstDeckLink2ColorCapabilities * color_caps)
{
  HRESULT hr = S_OK;
  GstCaps *caps = NULL;

  do {
    IDeckLinkDisplayMode *mode = NULL;
    BMDDisplayMode bdm_mode;
    GstDeckLink2DisplayMode gst_mode;
    GstDeckLink2DisplayMode gst_mode_wide;
    std::vector < std::string > formats;
    GstStructure *s;
    GstStructure *s_wide = NULL;
    const gchar *format_str;

    hr = iter->Next (&mode);
    if (hr != S_OK)
      break;

    bdm_mode = mode->GetDisplayMode ();
    if (!gst_decklink2_get_display_mode (bdm_mode, &gst_mode))
      goto next;

    if (func (io_object, bdm_mode, bmdFormat8BitYUV)) {
      format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitYUV);
      formats.push_back (format_str);
    }

    if (func (io_object, bdm_mode, bmdFormat10BitYUV)) {
      format_str = gst_decklink2_pixel_format_to_string (bmdFormat10BitYUV);
      formats.push_back (format_str);
    }

    if (func (io_object, bdm_mode, bmdFormat8BitARGB)) {
      format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitARGB);
      formats.push_back (format_str);
    }

    if (func (io_object, bdm_mode, bmdFormat8BitBGRA)) {
      format_str = gst_decklink2_pixel_format_to_string (bmdFormat8BitBGRA);
      formats.push_back (format_str);
    }

    if (func (io_object, bdm_mode, bmdFormat10BitRGB)) {
      format_str = gst_decklink2_pixel_format_to_string (bmdFormat10BitRGB);
      formats.push_back (format_str);
    }

    if (formats.empty ())
      goto next;

    if (!caps)
      caps = gst_caps_new_empty ();

    gst_mode.flags = mode->GetFlags ();
    gst_mode.width = (gint) mode->GetWidth ();
    gst_mode.height = (gint) mode->GetHeight ();
    if (gst_mode.interlaced) {
      BMDFieldDominance field;

      field = mode->GetFieldDominance ();
      if (field == bmdLowerFieldFirst)
        gst_mode.tff = FALSE;
      else if (field == bmdUpperFieldFirst)
        gst_mode.tff = TRUE;
    }

    s = gst_structure_new ("video/x-raw",
        "width", G_TYPE_INT, gst_mode.width,
        "height", G_TYPE_INT, gst_mode.height,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, gst_mode.par_n, gst_mode.par_d,
        "interlace-mode", G_TYPE_STRING,
        gst_mode.interlaced ? "interleaved" : "progressive",
        "framerate", GST_TYPE_FRACTION, gst_mode.fps_n, gst_mode.fps_d, NULL);

    if (gst_mode.interlaced) {
      if (gst_mode.tff) {
        gst_structure_set (s,
            "field-order", G_TYPE_STRING, "top-field-first", NULL);
      } else {
        gst_structure_set (s,
            "field-order", G_TYPE_STRING, "bottom-field-first", NULL);
      }
    }

    if (formats.size () == 1) {
      gst_structure_set (s, "format", G_TYPE_STRING, formats[0].c_str (), NULL);
    } else {
      GValue format_values = G_VALUE_INIT;

      g_value_init (&format_values, GST_TYPE_LIST);

      /* *INDENT-OFF* */
      for (const auto & iter: formats) {
        GValue f = G_VALUE_INIT;

        g_value_init (&f, G_TYPE_STRING);
        g_value_set_string (&f, iter.c_str ());
        gst_value_list_append_and_take_value (&format_values, &f);
      }
      /* *INDENT-ON* */

      gst_structure_set_value (s, "format", &format_values);
      g_value_unset (&format_values);
    }

    /* Add custom wide mode */
    switch (bdm_mode) {
      case bmdModeNTSC:
        gst_mode_wide = gst_mode;
        gst_mode_wide.mode = bmdModeNTSC_W;
        gst_mode_wide.par_n = 40;
        gst_mode_wide.par_d = 33;
        s_wide = gst_structure_copy (s);
        gst_structure_set (s_wide,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 40, 33, NULL);
        break;
      case bmdModeNTSC2398:
        gst_mode_wide = gst_mode;
        gst_mode_wide.mode = bmdModeNTSC2398_W;
        gst_mode_wide.par_n = 40;
        gst_mode_wide.par_d = 33;
        s_wide = gst_structure_copy (s);
        gst_structure_set (s_wide,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 40, 33, NULL);
        break;
      case bmdModeNTSCp:
        gst_mode_wide = gst_mode;
        gst_mode_wide.mode = bmdModeNTSCp_W;
        gst_mode_wide.par_n = 40;
        gst_mode_wide.par_d = 33;
        s_wide = gst_structure_copy (s);
        gst_structure_set (s_wide,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 40, 33, NULL);
        break;
      case bmdModePAL:
        gst_mode_wide = gst_mode;
        gst_mode_wide.mode = bmdModePAL_W;
        gst_mode_wide.par_n = 16;
        gst_mode_wide.par_d = 11;
        s_wide = gst_structure_copy (s);
        gst_structure_set (s_wide,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 16, 11, NULL);
        break;
      case bmdModePALp:
        gst_mode_wide = gst_mode;
        gst_mode_wide.mode = bmdModePALp_W;
        gst_mode_wide.par_n = 16;
        gst_mode_wide.par_d = 11;
        s_wide = gst_structure_copy (s);
        gst_structure_set (s_wide,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 16, 11, NULL);
        break;
      default:
        break;
    }

    append_color_caps (caps, s, &gst_mode, color_caps);
    g_array_append_val (format_table, gst_mode);

    if (s_wide) {
      append_color_caps (caps, s_wide, &gst_mode, color_caps);
      g_array_append_val (format_table, gst_mode_wide);
    }

  next:
    mode->Release ();
  } while (gst_decklink2_result (hr));

  return caps;
}

GstCaps *
gst_decklink2_get_default_template_caps (void)
{
  static GstCaps *template_caps = NULL;

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    template_caps = gst_caps_new_empty ();

    for (guint i = 1; i < G_N_ELEMENTS (all_modes); i++) {
      GstCaps *other;

      other = gst_decklink2_mode_get_all_format_caps (&all_modes[i]);
      template_caps = gst_caps_merge (template_caps, other);
    }

    GST_MINI_OBJECT_FLAG_SET (template_caps,
        GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  }
  GST_DECKLINK2_CALL_ONCE_END;

  return gst_caps_ref (template_caps);
}

GstCaps *
gst_decklink2_get_caps_from_mode (const GstDeckLink2DisplayMode * mode)
{
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, mode->par_n, mode->par_d,
      "interlace-mode", G_TYPE_STRING,
      mode->interlaced ? "interleaved" : "progressive",
      "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d,
      NULL);

  if (mode->interlaced) {
    if (mode->tff) {
      gst_caps_set_simple (caps,
          "field-order", G_TYPE_STRING, "top-field-first", NULL);
    } else {
      gst_caps_set_simple (caps,
          "field-order", G_TYPE_STRING, "bottom-field-first", NULL);
    }
  }

  return caps;
}

GType
gst_decklink2_audio_meta_api_get_type (void)
{
  static GType type = G_TYPE_INVALID;
  static const gchar *tags[] = { NULL };

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    type = gst_meta_api_type_register ("GstDeckLink2AudioMetaAPI", tags);
  } GST_DECKLINK2_CALL_ONCE_END;

  return type;
}

static gboolean
gst_decklink2_audio_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstDeckLink2AudioMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = (GstDeckLink2AudioMeta *) meta;

    dmeta = gst_buffer_add_decklink2_audio_meta (dest, smeta->sample);
    if (!dmeta)
      return FALSE;
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_decklink2_audio_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstDeckLink2AudioMeta *dmeta = (GstDeckLink2AudioMeta *) meta;

  dmeta->sample = NULL;

  return TRUE;
}

static void
gst_decklink2_audio_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstDeckLink2AudioMeta *dmeta = (GstDeckLink2AudioMeta *) meta;

  if (dmeta->sample)
    gst_sample_unref (dmeta->sample);
}

const GstMetaInfo *
gst_decklink2_audio_meta_get_info (void)
{
  static GstMetaInfo *meta_info = NULL;

  GST_DECKLINK2_CALL_ONCE_BEGIN {
    const GstMetaInfo *m = gst_meta_register (GST_DECKLINK2_AUDIO_META_API_TYPE,
        "GstDeckLink2AudioMeta",
        sizeof (GstDeckLink2AudioMeta),
        gst_decklink2_audio_meta_init,
        gst_decklink2_audio_meta_free,
        gst_decklink2_audio_meta_transform);
    meta_info = (GstMetaInfo *) m;
  }
  GST_DECKLINK2_CALL_ONCE_END;

  return meta_info;
}

GstDeckLink2AudioMeta *
gst_buffer_add_decklink2_audio_meta (GstBuffer * buffer,
    GstSample * audio_sample)
{
  GstDeckLink2AudioMeta *meta;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (audio_sample != NULL, NULL);

  meta = (GstDeckLink2AudioMeta *) gst_buffer_add_meta (buffer,
      GST_DECKLINK2_AUDIO_META_INFO, NULL);

  meta->sample = gst_sample_ref (audio_sample);

  return meta;
}

GstQuery *
gst_query_new_decklink2_audio_caps (void)
{
  auto s = gst_structure_new_static_str ("GstQueryDeckLink2AudioCaps",
      "caps", GST_TYPE_CAPS, nullptr, nullptr);
  return gst_query_new_custom (GST_QUERY_CUSTOM, s);
}

gboolean
gst_query_is_decklink2_audio_caps (GstQuery * query)
{
  g_return_val_if_fail (query, FALSE);

  return GST_QUERY_TYPE (query) == GST_QUERY_CUSTOM &&
      gst_structure_has_name (gst_query_get_structure (query),
      "GstQueryDeckLink2AudioCaps");
}

void
gst_query_parse_decklink2_audio_caps (GstQuery * query, GstCaps ** caps)
{
  g_return_if_fail (query);
  g_return_if_fail (caps);

  if (!gst_query_is_decklink2_audio_caps (query)) {
    GST_WARNING ("Query is not a decklink2 audio caps query");
    return;
  }

  auto s = gst_query_get_structure (query);
  *caps = (GstCaps *) g_value_get_boxed (gst_structure_get_value (s, "caps"));
}

void
gst_query_set_decklink2_audio_caps (GstQuery * query, GstCaps * caps)
{
  g_return_if_fail (query);
  g_return_if_fail (caps);

  if (!gst_query_is_decklink2_audio_caps (query)) {
    GST_WARNING ("Query is not a decklink2 audio caps query");
    return;
  }

  auto s = gst_query_writable_structure (query);
  gst_structure_set (s, "caps", GST_TYPE_CAPS, caps, nullptr);
}
