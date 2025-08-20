/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
#include <config.h>
#endif

#include "gstwasapi2util.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <winternl.h>
#include <mutex>
#include <string.h>
#include <wrl.h>
#include <vector>
#include <math.h>

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_debug);
#define GST_CAT_DEFAULT gst_wasapi2_debug

static GstStaticCaps template_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

/* Define GUIDs instead of linking ksuser.lib */
DEFINE_GUID (GST_KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

DEFINE_GUID (GST_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

/* Desktop only defines */
#ifndef KSAUDIO_SPEAKER_MONO
#define KSAUDIO_SPEAKER_MONO            (SPEAKER_FRONT_CENTER)
#endif
#ifndef KSAUDIO_SPEAKER_1POINT1
#define KSAUDIO_SPEAKER_1POINT1         (SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY)
#endif
#ifndef KSAUDIO_SPEAKER_STEREO
#define KSAUDIO_SPEAKER_STEREO          (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
#endif
#ifndef KSAUDIO_SPEAKER_2POINT1
#define KSAUDIO_SPEAKER_2POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY)
#endif
#ifndef KSAUDIO_SPEAKER_3POINT0
#define KSAUDIO_SPEAKER_3POINT0         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER)
#endif
#ifndef KSAUDIO_SPEAKER_3POINT1
#define KSAUDIO_SPEAKER_3POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY)
#endif
#ifndef KSAUDIO_SPEAKER_QUAD
#define KSAUDIO_SPEAKER_QUAD            (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_BACK_LEFT  | SPEAKER_BACK_RIGHT)
#endif
#define KSAUDIO_SPEAKER_SURROUND        (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_BACK_CENTER)
#ifndef KSAUDIO_SPEAKER_5POINT0
#define KSAUDIO_SPEAKER_5POINT0         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | \
                                         SPEAKER_SIDE_LEFT  | SPEAKER_SIDE_RIGHT)
#endif
#define KSAUDIO_SPEAKER_5POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | \
                                         SPEAKER_BACK_LEFT  | SPEAKER_BACK_RIGHT)
#ifndef KSAUDIO_SPEAKER_7POINT0
#define KSAUDIO_SPEAKER_7POINT0         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | \
                                         SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | \
                                         SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT)
#endif
#ifndef KSAUDIO_SPEAKER_7POINT1
#define KSAUDIO_SPEAKER_7POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | \
                                         SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | \
                                         SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER)
#endif

/* *INDENT-OFF* */
static struct
{
  guint64 wasapi_pos;
  GstAudioChannelPosition gst_pos;
} wasapi_to_gst_pos[] = {
  {SPEAKER_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT},
  {SPEAKER_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {SPEAKER_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER},
  {SPEAKER_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1},
  {SPEAKER_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT},
  {SPEAKER_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {SPEAKER_FRONT_LEFT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER},
  {SPEAKER_FRONT_RIGHT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER},
  {SPEAKER_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  /* Enum values diverge from this point onwards */
  {SPEAKER_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT},
  {SPEAKER_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT},
  {SPEAKER_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_CENTER},
  {SPEAKER_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT},
  {SPEAKER_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER},
  {SPEAKER_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT},
  {SPEAKER_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT},
  {SPEAKER_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER},
  {SPEAKER_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT}
};

static DWORD default_ch_masks[] = {
  0,
  KSAUDIO_SPEAKER_MONO,
  /* 2ch */
  KSAUDIO_SPEAKER_STEREO,
  /* 2.1ch */
  /* KSAUDIO_SPEAKER_3POINT0 ? */
  KSAUDIO_SPEAKER_2POINT1,
  /* 4ch */
  /* KSAUDIO_SPEAKER_3POINT1 or KSAUDIO_SPEAKER_SURROUND ? */
  KSAUDIO_SPEAKER_QUAD,
  /* 5ch */
  KSAUDIO_SPEAKER_5POINT0,
  /* 5.1ch */
  KSAUDIO_SPEAKER_5POINT1,
  /* 7ch */
  KSAUDIO_SPEAKER_7POINT0,
  /* 7.1ch */
  KSAUDIO_SPEAKER_7POINT1,
};
/* *INDENT-ON* */

static const gchar *
hresult_to_string_fallback (HRESULT hr)
{
  const gchar *s = "unknown error";

  switch (hr) {
    case AUDCLNT_E_NOT_INITIALIZED:
      s = "AUDCLNT_E_NOT_INITIALIZED";
      break;
    case AUDCLNT_E_ALREADY_INITIALIZED:
      s = "AUDCLNT_E_ALREADY_INITIALIZED";
      break;
    case AUDCLNT_E_WRONG_ENDPOINT_TYPE:
      s = "AUDCLNT_E_WRONG_ENDPOINT_TYPE";
      break;
    case AUDCLNT_E_DEVICE_INVALIDATED:
      s = "AUDCLNT_E_DEVICE_INVALIDATED";
      break;
    case AUDCLNT_E_NOT_STOPPED:
      s = "AUDCLNT_E_NOT_STOPPED";
      break;
    case AUDCLNT_E_BUFFER_TOO_LARGE:
      s = "AUDCLNT_E_BUFFER_TOO_LARGE";
      break;
    case AUDCLNT_E_OUT_OF_ORDER:
      s = "AUDCLNT_E_OUT_OF_ORDER";
      break;
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
      s = "AUDCLNT_E_UNSUPPORTED_FORMAT";
      break;
    case AUDCLNT_E_INVALID_DEVICE_PERIOD:
      s = "AUDCLNT_E_INVALID_DEVICE_PERIOD";
      break;
    case AUDCLNT_E_INVALID_SIZE:
      s = "AUDCLNT_E_INVALID_SIZE";
      break;
    case AUDCLNT_E_DEVICE_IN_USE:
      s = "AUDCLNT_E_DEVICE_IN_USE";
      break;
    case AUDCLNT_E_BUFFER_OPERATION_PENDING:
      s = "AUDCLNT_E_BUFFER_OPERATION_PENDING";
      break;
    case AUDCLNT_E_BUFFER_SIZE_ERROR:
      s = "AUDCLNT_E_BUFFER_SIZE_ERROR";
      break;
    case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED:
      s = "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED";
      break;
    case AUDCLNT_E_THREAD_NOT_REGISTERED:
      s = "AUDCLNT_E_THREAD_NOT_REGISTERED";
      break;
    case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:
      s = "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED";
      break;
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
      s = "AUDCLNT_E_ENDPOINT_CREATE_FAILED";
      break;
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
      s = "AUDCLNT_E_SERVICE_NOT_RUNNING";
      break;
    case AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED:
      s = "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED";
      break;
    case AUDCLNT_E_EXCLUSIVE_MODE_ONLY:
      s = "AUDCLNT_E_EXCLUSIVE_MODE_ONLY";
      break;
    case AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL:
      s = "AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL";
      break;
    case AUDCLNT_E_EVENTHANDLE_NOT_SET:
      s = "AUDCLNT_E_EVENTHANDLE_NOT_SET";
      break;
    case AUDCLNT_E_INCORRECT_BUFFER_SIZE:
      s = "AUDCLNT_E_INCORRECT_BUFFER_SIZE";
      break;
    case AUDCLNT_E_CPUUSAGE_EXCEEDED:
      s = "AUDCLNT_E_CPUUSAGE_EXCEEDED";
      break;
    case AUDCLNT_S_BUFFER_EMPTY:
      s = "AUDCLNT_S_BUFFER_EMPTY";
      break;
    case AUDCLNT_S_THREAD_ALREADY_REGISTERED:
      s = "AUDCLNT_S_THREAD_ALREADY_REGISTERED";
      break;
    case AUDCLNT_S_POSITION_STALLED:
      s = "AUDCLNT_S_POSITION_STALLED";
      break;
    case E_POINTER:
      s = "E_POINTER";
      break;
    case E_INVALIDARG:
      s = "E_INVALIDARG";
      break;
  }

  return s;
}

gchar *
gst_wasapi2_util_get_error_message (HRESULT hr)
{
  gchar *error_text = NULL;

  error_text = g_win32_error_message ((gint) hr);
  if (!error_text || strlen (error_text) == 0) {
    g_free (error_text);
    error_text = g_strdup (hresult_to_string_fallback (hr));
  }

  return error_text;
}

gboolean
_gst_wasapi2_result (HRESULT hr, GstDebugCategory * cat, const gchar * file,
    const gchar * function, gint line)
{
#ifndef GST_DISABLE_GST_DEBUG
  gboolean ret = TRUE;

  if (FAILED (hr)) {
    gchar *error_text = NULL;
    gboolean free_string = TRUE;

    error_text = g_win32_error_message ((gint) hr);
    /* g_win32_error_message() seems to be returning empty string for
     * AUDCLNT_* cases */
    if (!error_text || strlen (error_text) == 0) {
      g_free (error_text);
      error_text = (gchar *) hresult_to_string_fallback (hr);

      free_string = FALSE;
    }

    gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line,
        NULL, "WASAPI call failed: 0x%x, %s", (guint) hr, error_text);

    if (free_string)
      g_free (error_text);

    ret = FALSE;
  }

  return ret;
#else
  return SUCCEEDED (hr);
#endif
}

static void
gst_wasapi_util_channel_position_all_none (guint channels,
    GstAudioChannelPosition * position)
{
  guint i;

  for (i = 0; i < channels; i++)
    position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
}

guint64
gst_wasapi2_util_waveformatex_to_channel_mask (WAVEFORMATEX * format,
    GstAudioChannelPosition ** out_position)
{
  guint i, ch;
  guint64 mask = 0;
  GstAudioChannelPosition *pos = NULL;
  WORD nChannels = 0;
  DWORD dwChannelMask = 0;

  nChannels = format->nChannels;
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    WAVEFORMATEXTENSIBLE *extensible = (WAVEFORMATEXTENSIBLE *) format;
    dwChannelMask = extensible->dwChannelMask;
  }

  if (out_position)
    *out_position = NULL;

  if (nChannels > 2 && !dwChannelMask) {
    GST_WARNING ("Unknown channel mask value for %d channel stream", nChannels);

    if (nChannels >= G_N_ELEMENTS (default_ch_masks)) {
      GST_ERROR ("To may channels %d", nChannels);
      return 0;
    }

    dwChannelMask = default_ch_masks[nChannels];
  }

  pos = g_new (GstAudioChannelPosition, nChannels);
  gst_wasapi_util_channel_position_all_none (nChannels, pos);

  /* Too many channels, have to assume that they are all non-positional */
  if (nChannels > G_N_ELEMENTS (wasapi_to_gst_pos)) {
    GST_LOG ("Got too many (%i) channels, assuming non-positional", nChannels);
    goto out;
  }

  /* Too many bits in the channel mask, and the bits don't match nChannels */
  if (dwChannelMask >> (G_N_ELEMENTS (wasapi_to_gst_pos) + 1) != 0) {
    GST_WARNING ("Too many bits in channel mask (%lu), assuming "
        "non-positional", dwChannelMask);
    goto out;
  }

  /* Map WASAPI's channel mask to Gstreamer's channel mask and positions.
   * If the no. of bits in the mask > nChannels, we will ignore the extra. */
  for (i = 0, ch = 0; i < G_N_ELEMENTS (wasapi_to_gst_pos) && ch < nChannels;
      i++) {
    if (!(dwChannelMask & wasapi_to_gst_pos[i].wasapi_pos))
      /* no match, try next */
      continue;
    mask |= G_GUINT64_CONSTANT (1) << wasapi_to_gst_pos[i].gst_pos;
    pos[ch++] = wasapi_to_gst_pos[i].gst_pos;
  }

  /* XXX: Warn if some channel masks couldn't be mapped? */

  GST_TRACE ("Converted WASAPI mask 0x%" G_GINT64_MODIFIER "x -> 0x%"
      G_GINT64_MODIFIER "x", (guint64) dwChannelMask, (guint64) mask);

out:
  if (out_position) {
    *out_position = pos;
  } else {
    g_free (pos);
  }

  return mask;
}

const gchar *
gst_wasapi2_util_waveformatex_to_audio_format (WAVEFORMATEX * format)
{
  const gchar *fmt_str = NULL;
  GstAudioFormat fmt = GST_AUDIO_FORMAT_UNKNOWN;

  switch (format->wFormatTag) {
    case WAVE_FORMAT_PCM:
      fmt = gst_audio_format_build_integer (TRUE, G_LITTLE_ENDIAN,
          format->wBitsPerSample, format->wBitsPerSample);
      break;
    case WAVE_FORMAT_IEEE_FLOAT:
      if (format->wBitsPerSample == 32)
        fmt = GST_AUDIO_FORMAT_F32LE;
      else if (format->wBitsPerSample == 64)
        fmt = GST_AUDIO_FORMAT_F64LE;
      break;
    case WAVE_FORMAT_EXTENSIBLE:
    {
      WAVEFORMATEXTENSIBLE *ex = (WAVEFORMATEXTENSIBLE *) format;
      if (IsEqualGUID (ex->SubFormat, GST_KSDATAFORMAT_SUBTYPE_PCM)) {
        fmt = gst_audio_format_build_integer (TRUE, G_LITTLE_ENDIAN,
            format->wBitsPerSample, ex->Samples.wValidBitsPerSample);
      } else if (IsEqualGUID (ex->SubFormat,
              GST_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
        if (format->wBitsPerSample == 32
            && ex->Samples.wValidBitsPerSample == 32)
          fmt = GST_AUDIO_FORMAT_F32LE;
        else if (format->wBitsPerSample == 64 &&
            ex->Samples.wValidBitsPerSample == 64)
          fmt = GST_AUDIO_FORMAT_F64LE;
      }
      break;
    }
    default:
      break;
  }

  if (fmt != GST_AUDIO_FORMAT_UNKNOWN)
    fmt_str = gst_audio_format_to_string (fmt);

  return fmt_str;
}

gboolean
gst_wasapi2_util_parse_waveformatex (WAVEFORMATEX * format,
    GstCaps ** out_caps, GstAudioChannelPosition ** out_positions)
{
  const gchar *afmt;
  guint64 channel_mask;

  *out_caps = NULL;

  /* TODO: handle SPDIF and other encoded formats */

  /* 1 or 2 channels <= 16 bits sample size OR
   * 1 or 2 channels > 16 bits sample size or >2 channels */
  if (format->wFormatTag != WAVE_FORMAT_PCM &&
      format->wFormatTag != WAVE_FORMAT_IEEE_FLOAT &&
      format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    /* Unhandled format tag */
    return FALSE;

  /* WASAPI can only tell us one canonical mix format that it will accept. The
   * alternative is calling IsFormatSupported on all combinations of formats.
   * Instead, it's simpler and faster to require conversion inside gstreamer */
  afmt = gst_wasapi2_util_waveformatex_to_audio_format (format);
  if (afmt == NULL)
    return FALSE;

  auto caps = gst_static_caps_get (&template_caps);
  caps = gst_caps_make_writable (caps);

  channel_mask = gst_wasapi2_util_waveformatex_to_channel_mask (format,
      out_positions);

  gst_caps_set_simple (caps,
      "format", G_TYPE_STRING, afmt,
      "channels", G_TYPE_INT, format->nChannels,
      "rate", G_TYPE_INT, format->nSamplesPerSec, NULL);

  if (channel_mask) {
    gst_caps_set_simple (caps,
        "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
  }

  *out_caps = caps;

  return TRUE;
}

gboolean
gst_wasapi2_can_automatic_stream_routing (void)
{
  static gboolean ret = FALSE;

  GST_WASAPI2_CALL_ONCE_BEGIN {
    OSVERSIONINFOEXW osverinfo;
    typedef NTSTATUS (WINAPI fRtlGetVersion) (PRTL_OSVERSIONINFOEXW);
    fRtlGetVersion *RtlGetVersion = NULL;
    HMODULE hmodule = NULL;

    memset (&osverinfo, 0, sizeof (OSVERSIONINFOEXW));
    osverinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

    hmodule = LoadLibraryW (L"ntdll.dll");
    if (hmodule)
      RtlGetVersion =
          (fRtlGetVersion *) GetProcAddress (hmodule, "RtlGetVersion");

    if (RtlGetVersion) {
      RtlGetVersion (&osverinfo);

      /* automatic stream routing requires Windows 10
       * Anniversary Update (version 1607, build number 14393.0) */
      if (osverinfo.dwMajorVersion > 10 ||
          (osverinfo.dwMajorVersion == 10 && osverinfo.dwBuildNumber >= 14393))
        ret = TRUE;
    }

    if (hmodule)
      FreeLibrary (hmodule);
  }
  GST_WASAPI2_CALL_ONCE_END;

  GST_TRACE ("Automatic stream routing support: %d", ret);

  return ret;
}

gboolean
gst_wasapi2_can_process_loopback (void)
{
  static gboolean ret = FALSE;

  GST_WASAPI2_CALL_ONCE_BEGIN {
    OSVERSIONINFOEXW osverinfo;
    typedef NTSTATUS (WINAPI fRtlGetVersion) (PRTL_OSVERSIONINFOEXW);
    fRtlGetVersion *RtlGetVersion = NULL;
    HMODULE hmodule = NULL;

    memset (&osverinfo, 0, sizeof (OSVERSIONINFOEXW));
    osverinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

    hmodule = LoadLibraryW (L"ntdll.dll");
    if (hmodule)
      RtlGetVersion =
          (fRtlGetVersion *) GetProcAddress (hmodule, "RtlGetVersion");

    if (RtlGetVersion) {
      RtlGetVersion (&osverinfo);

      /* Process loopback requires Windows 10 build 20348
       * https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params
       *
       * Note: "Windows 10 build 20348" would mean "Windows server 2022" or
       * "Windows 11", since build number of "Windows 10 version 21H2" is
       * still 19044.XXX
       */

      /* But other software enables this for build number 19041 or higher... */
      if (osverinfo.dwMajorVersion > 10 ||
          (osverinfo.dwMajorVersion == 10 && osverinfo.dwBuildNumber >= 19041))
        ret = TRUE;
    }

    if (hmodule)
      FreeLibrary (hmodule);
  }
  GST_WASAPI2_CALL_ONCE_END;

  GST_INFO ("Process loopback support: %d", ret);

  return ret;
}

WAVEFORMATEX *
gst_wasapi2_get_default_mix_format (void)
{
  WAVEFORMATEX *format;

  /* virtual loopback device might not provide mix format. Create our default
   * mix format */
  format = (WAVEFORMATEX *) CoTaskMemAlloc (sizeof (WAVEFORMATEX));
  format->wFormatTag = WAVE_FORMAT_PCM;
  format->nChannels = 2;
  format->nSamplesPerSec = 48000;
  format->wBitsPerSample = 16;
  format->nBlockAlign = format->nChannels * format->wBitsPerSample / 8;
  format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
  format->cbSize = 0;

  return format;
}

const wchar_t *
gst_wasapi2_get_default_device_id_wide (EDataFlow flow)
{
  static wchar_t *capture = nullptr;
  static wchar_t *render = nullptr;

  GST_WASAPI2_CALL_ONCE_BEGIN {
    StringFromIID (DEVINTERFACE_AUDIO_CAPTURE, &capture);
    StringFromIID (DEVINTERFACE_AUDIO_RENDER, &render);
  } GST_WASAPI2_CALL_ONCE_END;

  if (flow == eCapture)
    return (const wchar_t *) capture;

  return (const wchar_t *) render;
}

const char *
gst_wasapi2_get_default_device_id (EDataFlow flow)
{
  static char *capture = nullptr;
  static char *render = nullptr;

  GST_WASAPI2_CALL_ONCE_BEGIN {
    auto wstr = gst_wasapi2_get_default_device_id_wide (eCapture);
    if (wstr) {
      capture = g_utf16_to_utf8 ((const gunichar2 *) wstr,
          -1, nullptr, nullptr, nullptr);
    }

    wstr = gst_wasapi2_get_default_device_id_wide (eRender);
    if (wstr) {
      render = g_utf16_to_utf8 ((const gunichar2 *) wstr,
          -1, nullptr, nullptr, nullptr);
    }
  } GST_WASAPI2_CALL_ONCE_END;

  if (flow == eCapture)
    return (const char *) capture;

  return (const char *) render;
}

const gchar *
gst_wasapi2_data_flow_to_string (EDataFlow flow)
{
  switch (flow) {
    case eRender:
      return "eRender";
    case eCapture:
      return "eCapture";
    case eAll:
      return "eAll";
    default:
      break;
  }

  return "Unknown";
}

const gchar *
gst_wasapi2_role_to_string (ERole role)
{
  switch (role) {
    case eConsole:
      return "eConsole";
    case eMultimedia:
      return "eMultimedia";
    case eCommunications:
      return "eCommunications";
    default:
      break;
  }

  return "Unknown";
}

void
gst_wasapi2_free_wfx (WAVEFORMATEX * wfx)
{
  if (wfx)
    CoTaskMemFree (wfx);
}

void
gst_wasapi2_clear_wfx (WAVEFORMATEX ** wfx)
{
  if (*wfx) {
    CoTaskMemFree (*wfx);
    *wfx = nullptr;
  }
}

WAVEFORMATEX *
gst_wasapi2_copy_wfx (WAVEFORMATEX * src)
{
  guint total_size = sizeof (WAVEFORMATEX) + src->cbSize;
  auto dst = (WAVEFORMATEX *) CoTaskMemAlloc (total_size);
  memcpy (dst, src, total_size);

  return dst;
}

static DWORD
make_channel_mask (WORD nChannels)
{
  switch (nChannels) {
    case 1:
      return KSAUDIO_SPEAKER_MONO;
    case 2:
      return KSAUDIO_SPEAKER_STEREO;
    case 4:
      return KSAUDIO_SPEAKER_3POINT1;
    case 6:
      return KSAUDIO_SPEAKER_5POINT1;
    case 8:
      return KSAUDIO_SPEAKER_7POINT1;
    default:
      return 0;
  }
}

static WAVEFORMATEXTENSIBLE
make_wfx_ext (DWORD nSamplesPerSec, WORD nChannels, WORD wBitsPerSample,
    WORD wValidBitsPerSample, bool is_float)
{
  WAVEFORMATEXTENSIBLE w = { };
  w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  w.Format.nChannels = nChannels;
  w.Format.nSamplesPerSec = nSamplesPerSec;

  w.Format.wBitsPerSample = wBitsPerSample;
  w.Samples.wValidBitsPerSample = wValidBitsPerSample;

  w.dwChannelMask = make_channel_mask (nChannels);
  w.SubFormat = is_float ? GST_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
      : GST_KSDATAFORMAT_SUBTYPE_PCM;

  w.Format.nBlockAlign = (wBitsPerSample / 8) * nChannels;
  w.Format.nAvgBytesPerSec = w.Format.nSamplesPerSec * w.Format.nBlockAlign;
  w.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);

  return w;
}

/* *INDENT-OFF* */
gboolean
gst_wasapi2_get_exclusive_formats (IAudioClient * client,
    IPropertyStore * props, GPtrArray * list)
{
  PROPVARIANT var;
  PropVariantInit (&var);
  WAVEFORMATEX *device_format = nullptr;
  WAVEFORMATEX *closest = nullptr;
  WAVEFORMATEX *basis = nullptr;

  /* Prefer device format if supported */
  auto hr = props->GetValue (PKEY_AudioEngine_DeviceFormat, &var);
  if (gst_wasapi2_result (hr)) {
    if (var.vt == VT_BLOB && var.blob.cbSize >= sizeof (WAVEFORMATEX)
        && var.blob.pBlobData) {
      device_format = (WAVEFORMATEX *) CoTaskMemAlloc (var.blob.cbSize);

      memcpy (device_format, var.blob.pBlobData, var.blob.cbSize);
    }
    PropVariantClear (&var);
  }

  if (device_format) {
    hr = client->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, device_format,
        &closest);

    if (hr == S_OK) {
      basis = gst_wasapi2_copy_wfx (device_format);
      g_ptr_array_add (list, device_format);
      device_format = nullptr;
    } else if (hr == S_FALSE && closest) {
      basis = gst_wasapi2_copy_wfx (closest);
      g_ptr_array_add (list, closest);
      closest = nullptr;
    }
  }

  gst_wasapi2_clear_wfx (&device_format);

  /* Checks using pre-defined format list */
  struct DepthPair
  {
    WORD wBitsPerSample;
    WORD wValidBitsPerSample;
    bool is_float;
  };

  const DepthPair depth_pairs[] = {
    {32, 32, true},  /* 32-float */
    {32, 32, false}, /* 32-int */
    {24, 24, false}, /* 24-packed */
    {16, 16, false}, /* 16-int */
    {32, 24, false}, /* 24-in-32 */
  };

  const DWORD rates[] = { 192000, 176400, 96000, 88200, 48000, 44100 };
  const WORD chs[] = { 8, 6, 2, 1 };

  for (auto r : rates) {
    for (auto c : chs) {
      for (auto d : depth_pairs) {
        auto wfx = make_wfx_ext (r, c, d.wBitsPerSample, d.wValidBitsPerSample,
            d.is_float);
        hr = client->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE,
            (WAVEFORMATEX *) &wfx, &closest);
        if (hr == S_OK) {
          g_ptr_array_add (list, gst_wasapi2_copy_wfx ((WAVEFORMATEX *) &wfx));
        } else if (hr == S_FALSE && closest) {
          g_ptr_array_add (list, closest);
          closest = nullptr;
        }
      }
    }
  }

  if (!basis) {
    if (list && list->len > 0) {
      auto first = (WAVEFORMATEX *) g_ptr_array_index (list, 0);
      basis = gst_wasapi2_copy_wfx (first);
    } else {
      basis = gst_wasapi2_get_default_mix_format ();
    }
  }

  gst_wasapi2_sort_wfx (list, basis);
  gst_wasapi2_free_wfx (basis);

  return TRUE;
}

GstCaps *
gst_wasapi2_wfx_list_to_caps (GPtrArray * list)
{
  if (!list || list->len == 0)
    return nullptr;

  std::vector <GstCaps *> caps_list;

  for (guint i = 0; i < list->len; i++) {
    auto wfx = (WAVEFORMATEX *) g_ptr_array_index (list, i);
    GstCaps *tmp;

    if (gst_wasapi2_util_parse_waveformatex (wfx, &tmp, nullptr)) {
      bool unique = true;
      for (auto it : caps_list) {
        if (gst_caps_is_equal (it, tmp)) {
          unique = false;
          break;
        }
      }

      if (unique)
        caps_list.push_back (tmp);
      else
        gst_caps_unref (tmp);
    }
  }

  if (caps_list.empty ())
    return nullptr;

  auto caps = gst_caps_new_empty ();
  for (auto it : caps_list)
    gst_caps_append (caps, it);

  return caps;
}
/* *INDENT-ON* */

struct FormatView
{
  WORD channels;
  DWORD sample_rate;
  GUID subformat;
  WORD bits_per_sample;
  WORD valid_bits_per_sample;
  WORD raw_valid_bits_per_sample;
  DWORD channel_mask;
  WORD format_tag;
};

static inline gboolean
is_extensible_format (const WAVEFORMATEX * wfx)
{
  return wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      wfx->cbSize >= (sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX));
}

static inline gboolean
is_float_subformat (const FormatView * v)
{
  return IsEqualGUID (v->subformat, GST_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

static inline gboolean
is_pcm_subformat (const FormatView * v)
{
  return IsEqualGUID (v->subformat, GST_KSDATAFORMAT_SUBTYPE_PCM);
}

static inline gint
effective_bits (const FormatView * v)
{
  if (is_float_subformat (v))
    return 32;

  return v->valid_bits_per_sample ? v->
      valid_bits_per_sample : v->bits_per_sample;
}

static inline gboolean
is_s24_in_32 (const FormatView * v)
{
  return is_pcm_subformat (v) &&
      v->bits_per_sample == 32 &&
      (v->raw_valid_bits_per_sample == 24 || v->valid_bits_per_sample == 24);
}

static FormatView
make_view (const WAVEFORMATEX * wfx)
{
  FormatView view = { };

  view.channels = wfx->nChannels;
  view.sample_rate = wfx->nSamplesPerSec;
  view.bits_per_sample = wfx->wBitsPerSample;
  view.format_tag = wfx->wFormatTag;

  if (is_extensible_format (wfx)) {
    auto wfe = (const WAVEFORMATEXTENSIBLE *) wfx;
    view.subformat = wfe->SubFormat;
    view.raw_valid_bits_per_sample = wfe->Samples.wValidBitsPerSample;
    view.valid_bits_per_sample = view.raw_valid_bits_per_sample ?
        view.raw_valid_bits_per_sample : view.bits_per_sample;
    view.channel_mask = wfe->dwChannelMask;
  } else {
    if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
      view.subformat = GST_KSDATAFORMAT_SUBTYPE_PCM;
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
      view.subformat = GST_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    view.raw_valid_bits_per_sample = view.bits_per_sample;
    view.valid_bits_per_sample = view.bits_per_sample;
    view.channel_mask = 0;
  }

  return view;
}

static gint
compare_format_similarity (const FormatView * a, const FormatView * b,
    const FormatView * basis)
{
  gboolean a_sub_eq = IsEqualGUID (a->subformat, basis->subformat);
  gboolean b_sub_eq = IsEqualGUID (b->subformat, basis->subformat);

  /* Check subformat (e.g., PCM vs FLOAT) */
  if (a_sub_eq != b_sub_eq)
    return a_sub_eq ? -1 : 1;

  /* BPS diff */
  gint da_bits =
      abs ((gint) a->bits_per_sample - (gint) basis->bits_per_sample);
  gint db_bits =
      abs ((gint) b->bits_per_sample - (gint) basis->bits_per_sample);
  if (da_bits != db_bits)
    return (da_bits < db_bits) ? -1 : 1;

  gint a_valid = a->valid_bits_per_sample ?
      a->valid_bits_per_sample : a->bits_per_sample;
  gint b_valid = b->valid_bits_per_sample ?
      b->valid_bits_per_sample : b->bits_per_sample;
  gint basis_valid = basis->valid_bits_per_sample ?
      basis->valid_bits_per_sample : basis->bits_per_sample;

  gint da_valid = abs (a_valid - basis_valid);
  gint db_valid = abs (b_valid - basis_valid);
  if (da_valid != db_valid)
    return (da_valid < db_valid) ? -1 : 1;

  /* Checks sample mask */
  gboolean a_mask_eq = (a->channel_mask != 0 && basis->channel_mask != 0 &&
      a->channel_mask == basis->channel_mask);
  gboolean b_mask_eq = (b->channel_mask != 0 && basis->channel_mask != 0 &&
      b->channel_mask == basis->channel_mask);
  if (a_mask_eq != b_mask_eq)
    return a_mask_eq ? -1 : 1;

  /* Check format tag */
  gint dtag_a = abs ((gint) a->format_tag - (gint) basis->format_tag);
  gint dtag_b = abs ((gint) b->format_tag - (gint) basis->format_tag);
  if (dtag_a != dtag_b)
    return (dtag_a < dtag_b) ? -1 : 1;

  return 0;
}

static gint
compare_wfx_func (gconstpointer pa, gconstpointer pb, gpointer user_data)
{
  const WAVEFORMATEX *A = (const WAVEFORMATEX *) pa;
  const WAVEFORMATEX *B = (const WAVEFORMATEX *) pb;
  const WAVEFORMATEX *basis_wfx = (const WAVEFORMATEX *) user_data;

  FormatView a = make_view (A);
  FormatView b = make_view (B);
  FormatView basis = make_view (basis_wfx);

  /* S24_32LE is the lowest */
  gboolean a_s2432 = is_s24_in_32 (&a);
  gboolean b_s2432 = is_s24_in_32 (&b);
  if (a_s2432 != b_s2432)
    return a_s2432 ? 1 : -1;

  /* Prefer same channel */
  gint dch_a = abs ((gint) a.channels - (gint) basis.channels);
  gint dch_b = abs ((gint) b.channels - (gint) basis.channels);
  if (dch_a != dch_b)
    return (dch_a < dch_b) ? -1 : 1;

  /* Then sample rate */
  gint64 dra = (gint64) a.sample_rate - (gint64) basis.sample_rate;
  gint64 drb = (gint64) b.sample_rate - (gint64) basis.sample_rate;
  dra = dra >= 0 ? dra : -dra;
  drb = drb >= 0 ? drb : -drb;
  if (dra != drb)
    return (dra < drb) ? -1 : 1;

  /* Prefere higher sample rate */
  if (a.sample_rate != b.sample_rate)
    return (a.sample_rate > b.sample_rate) ? -1 : +1;

  /* High bit first */
  gint a_bits = effective_bits (&a);
  gint b_bits = effective_bits (&b);
  if (a_bits != b_bits)
    return (a_bits > b_bits) ? -1 : +1;

  /* format compare */
  gint fcmp = compare_format_similarity (&a, &b, &basis);
  if (fcmp != 0)
    return fcmp;

  return 0;
}

/* *INDENT-OFF* */
static void
demote_s24_32le (GPtrArray *list)
{
  if (!list || list->len == 0)
    return;

  std::vector<gpointer> head;
  std::vector<gpointer> tail;

  head.reserve (list->len);
  tail.reserve (list->len);

  for (guint i = 0; i < list->len; i++) {
    auto wfx = (WAVEFORMATEX *) g_ptr_array_index (list, i);
    FormatView v = make_view (wfx);
    if (is_s24_in_32 (&v))
      tail.push_back ((gpointer) wfx);
    else
      head.push_back ((gpointer) wfx);
  }

  guint idx = 0;
  for (gpointer p : head)
    list->pdata[idx++] = p;

  for (gpointer p : tail)
    list->pdata[idx++] = p;
}
/* *INDENT-ON* */

void
gst_wasapi2_sort_wfx (GPtrArray * list, WAVEFORMATEX * wfx)
{
  if (!list || list->len == 0 || !wfx)
    return;

  g_ptr_array_sort_with_data (list, compare_wfx_func, wfx);
  demote_s24_32le (list);
}
