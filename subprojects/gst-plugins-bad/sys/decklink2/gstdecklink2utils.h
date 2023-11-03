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
#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#ifndef INITGUID
#include <initguid.h>
#endif /* INITGUID */
#include <DeckLinkAPI_i.c>
#else
#include <DeckLinkAPI_v10_11.h>
#include <DeckLinkAPI_v11_5.h>
#include <DeckLinkAPI_v11_5_1.h>
#include <DeckLinkAPIConfiguration_v10_11.h>
#include <DeckLinkAPIVideoInput_v10_11.h>
#include <DeckLinkAPIVideoInput_v11_4.h>
#include <DeckLinkAPIVideoInput_v11_5_1.h>
#include <DeckLinkAPIVideoOutput_v10_11.h>
#include <DeckLinkAPIVideoOutput_v11_4.h>
#endif /* G_OS_WIN32 */

#include <DeckLinkAPI.h>

#if defined(__APPLE__)
#define IID_IUnknown CFUUIDGetUUIDBytes (IUnknownUUID)
#endif

#if defined(G_OS_WIN32)
#define dlbool_t    BOOL
#define dlstring_t  BSTR
#elif defined(__APPLE__)
#define dlbool_t    bool
#define dlstring_t  CFStringRef
#else
#define dlbool_t    bool
#define dlstring_t  const char*
#endif

G_BEGIN_DECLS

typedef struct _GstDeckLink2AudioMeta GstDeckLink2AudioMeta;
typedef struct _GstDeckLink2DisplayMode GstDeckLink2DisplayMode;

/* DeckLink interfaces have changed across SDK versions, including interface
 * IIDs and method signatures. Select the matching interface based on the
 * installed runtime version.
 *
 * LATEST uses the current interfaces from the bundled DeckLink SDK 12.4.2.
 * Newer runtimes are expected to keep these interfaces compatible. */
typedef enum
{
  GST_DECKLINK2_API_LEVEL_UNKNOWN,
  GST_DECKLINK2_API_LEVEL_10_11,
  GST_DECKLINK2_API_LEVEL_11_4,
  GST_DECKLINK2_API_LEVEL_11_5_1,
  GST_DECKLINK2_API_LEVEL_LATEST,
} GstDeckLink2APILevel;

/* defines custom display mode for wide screen */
#define bmdModeNTSC_W     ((BMDDisplayMode) 0x4E545343) /* 'NTSC' */
#define bmdModeNTSC2398_W ((BMDDisplayMode) 0x4E543233) /* 'NT23' */
#define bmdModePAL_W      ((BMDDisplayMode) 0x50414C20) /* 'PAL ' */
#define bmdModeNTSCp_W    ((BMDDisplayMode) 0x4E545350) /* 'NTSP' */
#define bmdModePALp_W     ((BMDDisplayMode) 0x50414C50) /* 'PALP' */

#define GST_TYPE_DECKLINK2_MODE (gst_decklink2_mode_get_type ())
GType gst_decklink2_mode_get_type (void);

#define GST_TYPE_DECKLINK2_VIDEO_FORMAT (gst_decklink2_video_format_get_type ())
GType gst_decklink2_video_format_get_type (void);

#define bmdProfileDefault ((BMDProfileID) 0)
#define GST_TYPE_DECKLINK2_PROFILE_ID (gst_decklink2_profile_id_get_type ())
GType gst_decklink2_profile_id_get_type (void);

typedef enum
{
  GST_DECKLINK2_KEYER_MODE_OFF,
  GST_DECKLINK2_KEYER_MODE_INTERNAL,
  GST_DECKLINK2_KEYER_MODE_EXTERNAL
} GstDeckLink2KeyerMode;
#define GST_TYPE_DECKLINK2_KEYER_MODE (gst_decklink2_keyer_mode_get_type ())
GType gst_decklink2_keyer_mode_get_type (void);

typedef enum
{
  GST_DECKLINK2_MAPPING_FORMAT_DEFAULT,
  GST_DECKLINK2_MAPPING_FORMAT_LEVEL_A, /* bmdDeckLinkConfigSMPTELevelAOutput = true */
  GST_DECKLINK2_MAPPING_FORMAT_LEVEL_B, /* bmdDeckLinkConfigSMPTELevelAOutput = false */
} GstDeckLink2MappingFormat;
#define GST_TYPE_DECKLINK2_MAPPING_FORMAT (gst_decklink2_mapping_format_get_type ())
GType gst_decklink2_mapping_format_get_type (void);

#define GST_TYPE_DECKLINK2_TIMECODE_FORMAT (gst_decklink2_timecode_format_get_type ())
GType gst_decklink2_timecode_format_get_type (void);

#define GST_TYPE_DECKLINK2_VIDEO_CONNECTION (gst_decklink2_video_connection_get_type ())
GType gst_decklink2_video_connection_get_type (void);

#define bmdAudioConnectionUnspecified ((BMDAudioConnection) 0)
#define GST_TYPE_DECKLINK2_AUDIO_CONNECTION (gst_decklink2_audio_connection_get_type ())
GType gst_decklink2_audio_connection_get_type (void);

typedef enum
{
  GST_DECKLINK2_AUDIO_CHANNELS_DISABLED = -1,
  GST_DECKLINK2_AUDIO_CHANNELS_MAX = 0,
  GST_DECKLINK2_AUDIO_CHANNELS_2 = 2,
  GST_DECKLINK2_AUDIO_CHANNELS_8 = 8,
  GST_DECKLINK2_AUDIO_CHANNELS_16 = 16,
} GstDeckLink2AudioChannels;
#define GST_TYPE_DECKLINK2_AUDIO_CHANNELS (gst_decklink2_audio_channels_get_type ())
GType gst_decklink2_audio_channels_get_type (void);

struct _GstDeckLink2DisplayMode
{
  BMDDisplayMode mode;
  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  gboolean interlaced;
  gint par_n;
  gint par_d;
  gboolean tff;
  BMDDisplayModeFlags flags;
};

struct GstDeckLink2ColorCapabilities
{
  gboolean supports_hdr;
  gboolean supports_colorspace;
  BMDDynamicRange dynamic_range;
};

struct GstDeckLink2HDRMetadata
{
  gboolean have_mastering_info;
  GstVideoMasteringDisplayInfo mastering_info;
  gboolean have_light_level;
  GstVideoContentLightLevel light_level;
};

void gst_decklink2_get_color_capabilities (IDeckLink * device,
                                           GstDeckLink2ColorCapabilities * caps);

BMDDisplayModeFlags gst_decklink2_get_colorspace_flags (const GstDeckLink2DisplayMode * mode,
                                                        const GstDeckLink2ColorCapabilities * caps);

gboolean  gst_decklink2_init_once (void);

void      gst_decklink2_deinit (void);

gboolean  gst_decklink2_get_api_version (guint * major,
                                         guint * minor,
                                         guint * sub,
                                         guint * extra);

GstDeckLink2APILevel gst_decklink2_get_api_level (void);

const gchar * gst_decklink2_api_level_to_string (GstDeckLink2APILevel level);

GstCaps * gst_decklink2_get_default_template_caps (void);

BMDDisplayMode gst_decklink2_get_real_display_mode (BMDDisplayMode mode);

typedef gboolean (*GstDeckLink2DoesSupportVideoMode) (GstObject * object,
                                                      BMDDisplayMode mode,
                                                      BMDPixelFormat format);

GstCaps * gst_decklink2_build_caps (GstObject * io_object,
                                    IDeckLinkDisplayModeIterator * iter,
                                    BMDDisplayMode requested_mode,
                                    BMDPixelFormat format,
                                    GstDeckLink2DoesSupportVideoMode func,
                                    const GstDeckLink2ColorCapabilities * color_caps);

GstCaps * gst_decklink2_build_template_caps (GstObject * io_object,
                                             IDeckLinkDisplayModeIterator * iter,
                                             GstDeckLink2DoesSupportVideoMode func,
                                             GArray * format_table,
                                             const GstDeckLink2ColorCapabilities * color_caps);

GstCaps * gst_decklink2_get_caps_from_mode (const GstDeckLink2DisplayMode * mode);


GstVideoFormat gst_decklink2_video_format_from_pixel_format (BMDPixelFormat format);

BMDPixelFormat gst_decklink2_pixel_format_from_video_format (GstVideoFormat format);

struct _GstDeckLink2AudioMeta
{
  GstMeta meta;

  GstSample *sample;
};

GType gst_decklink2_audio_meta_api_get_type (void);
#define GST_DECKLINK2_AUDIO_META_API_TYPE (gst_decklink2_audio_meta_api_get_type())

const GstMetaInfo *gst_decklink2_audio_meta_get_info (void);
#define GST_DECKLINK2_AUDIO_META_INFO (gst_decklink2_audio_meta_get_info())

#define gst_buffer_get_decklink2_audio_meta(b) \
    ((GstDeckLink2AudioMeta*)gst_buffer_get_meta((b),GST_DECKLINK2_AUDIO_META_API_TYPE))

GstDeckLink2AudioMeta * gst_buffer_add_decklink2_audio_meta (GstBuffer * buffer,
                                                             GstSample * audio_sample);

GstQuery * gst_query_new_decklink2_audio_caps (void);

gboolean gst_query_is_decklink2_audio_caps (GstQuery * query);

void gst_query_parse_decklink2_audio_caps (GstQuery * query,
                                           GstCaps ** caps);

void gst_query_set_decklink2_audio_caps (GstQuery * query,
                                         GstCaps * caps);

#ifndef GST_DISABLE_GST_DEBUG
static inline gboolean
_gst_decklink2_result (HRESULT hr, GstDebugCategory * cat, const gchar * file,
    const gchar * function, gint line)
{
  if (hr == S_OK)
    return TRUE;

#ifdef G_OS_WIN32
  {
    gchar *error_text = g_win32_error_message ((guint) hr);
    gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line, NULL,
        "DeckLink call failed: 0x%x (%s)", (guint) hr,
        GST_STR_NULL (error_text));
    g_free (error_text);
  }
#else
  gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line, NULL,
      "DeckLink call failed: 0x%x", (guint) hr);
#endif /* G_OS_WIN32 */

  return FALSE;
}

#define gst_decklink2_result(hr) \
  _gst_decklink2_result (hr, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else /* GST_DISABLE_GST_DEBUG */
static inline gboolean
gst_decklink2_result (HRESULT hr)
{
  if (hr == S_OK)
    return TRUE;

  return FALSE;
}
#endif /* GST_DISABLE_GST_DEBUG */

#define GST_DECKLINK2_PRINT_CHAR(c) \
  g_ascii_isprint(c) ? (c) : '.'

#define GST_DECKLINK2_FOURCC_ARGS(fourcc) \
  GST_DECKLINK2_PRINT_CHAR(((fourcc) >> 24) & 0xff), \
  GST_DECKLINK2_PRINT_CHAR(((fourcc) >> 16) & 0xff), \
  GST_DECKLINK2_PRINT_CHAR(((fourcc) >> 8) & 0xff), \
  GST_DECKLINK2_PRINT_CHAR((fourcc) & 0xff)

G_END_DECLS

#ifdef __cplusplus
#include <string>
#include <functional>
#include <stdint.h>
#include <mutex>

#define GST_DECKLINK2_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_DECKLINK2_CALL_ONCE_END )

#define GST_DECKLINK2_CLEAR_COM(obj) G_STMT_START { \
    if (obj) { \
      (obj)->Release (); \
      (obj) = NULL; \
    } \
  } G_STMT_END

#ifndef G_OS_WIN32
#include <string.h>
inline bool operator==(REFIID a, REFIID b)
{
  if (memcmp (&a, &b, sizeof (REFIID)) != 0)
    return false;

  return true;
}
#endif

#if defined(G_OS_WIN32)
const std::function<void(dlstring_t)> DeleteString = SysFreeString;

const std::function<std::string(dlstring_t)> DlToStdString = [](dlstring_t dl_str) -> std::string
{
  int wlen = ::SysStringLen(dl_str);
  int mblen = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      (wchar_t*)dl_str, wlen, NULL, 0, NULL, NULL);
  if (mblen == 0)
    return std::string ();

  std::string ret_str(mblen, '\0');
  if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
          (wchar_t*)dl_str, wlen, &ret_str[0], mblen, NULL, NULL) != mblen) {
    return std::string ();
  }

  return ret_str;
};

const std::function<dlstring_t(std::string)> StdToDlString = [](std::string std_str) -> dlstring_t
{
  if (std_str.empty ())
    return ::SysAllocStringLen(NULL, 0);

  int wlen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      std_str.data(), (int)std_str.length(), NULL, 0);
  if (wlen == 0)
    return NULL;

  dlstring_t ret_str = ::SysAllocStringLen(NULL, wlen);
  if (!ret_str)
    return NULL;
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
          std_str.data(), (int)std_str.length(), ret_str, wlen) != wlen) {
    ::SysFreeString (ret_str);
    return NULL;
  }

  return ret_str;
};
#elif defined(__APPLE__)
const auto DeleteString = CFRelease;

const auto DlToStdString = [](dlstring_t dl_str) -> std::string
{
  CFIndex len;
  CFIndex char_count = CFStringGetLength(dl_str);
  if (CFStringGetBytes(dl_str, CFRangeMake(0, char_count),
          kCFStringEncodingUTF8, 0, FALSE, NULL, 0, &len) != char_count) {
    return std::string ();
  }

  std::string ret_str (len + 1, '\0');
  if (!CFStringGetCString (dl_str, &ret_str[0], len + 1,
          kCFStringEncodingUTF8)) {
    return std::string ();
  }

  ret_str.resize (len);

  return ret_str;
};

const auto StdToDlString = [](std::string std_str) -> dlstring_t
{
  return CFStringCreateWithCString(kCFAllocatorMalloc, std_str.c_str(), kCFStringEncodingUTF8);
};
#else
#include <string.h>

const std::function<void(dlstring_t)> DeleteString = [](dlstring_t dl_str)
{
  free((void*)dl_str);
};

const std::function<std::string(dlstring_t)> DlToStdString = [](dlstring_t dl_str) -> std::string
{
  return dl_str;
};

const std::function<dlstring_t(std::string)> StdToDlString = [](std::string std_str) -> dlstring_t
{
  return strcpy((char*)malloc(std_str.length()+1), std_str.c_str());
};
#endif
#endif /* __cplusplus */
