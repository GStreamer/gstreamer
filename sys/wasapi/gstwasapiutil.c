/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
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
#  include <config.h>
#endif

#include "gstwasapiutil.h"

#include <mmdeviceapi.h>

#ifdef __uuidof
const CLSID CLSID_MMDeviceEnumerator = __uuidof (MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof (IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof (IAudioClient);
const IID IID_IAudioRenderClient = __uuidof (IAudioRenderClient);
const IID IID_IAudioCaptureClient = __uuidof (IAudioCaptureClient);
const IID IID_IAudioClock = __uuidof (IAudioClock);
#else
/* __uuidof is not implemented in our Cerbero's ancient MinGW toolchain so we
 * hard-code the GUID values for all these. This is ok because these are ABI. */
const CLSID CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c,
  {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}
};

const IID IID_IMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35,
  {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}
};

const IID IID_IAudioClient = { 0x1cb9ad4c, 0xdbfa, 0x4c32,
  {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}
};

const IID IID_IAudioClock = { 0xcd63314f, 0x3fba, 0x4a1b,
  {0x81, 0x2c, 0xef, 0x96, 0x35, 0x87, 0x28, 0xe7}
};

const IID IID_IAudioCaptureClient = { 0xc8adbd64, 0xe71e, 0x48a0,
  {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}
};

const IID IID_IAudioRenderClient = { 0xf294acfc, 0x3146, 0x4483,
  {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}
};
#endif

static struct {
  guint64 wasapi_pos;
  GstAudioChannelPosition gst_pos;
} wasapi_to_gst_pos[] = {
  {SPEAKER_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT},
  {SPEAKER_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {SPEAKER_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER},
  {SPEAKER_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1},
  {SPEAKER_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT},
  {SPEAKER_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {SPEAKER_FRONT_LEFT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER},
  {SPEAKER_FRONT_RIGHT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER},
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
  {SPEAKER_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT},
};

GType
gst_wasapi_device_role_get_type (void)
{
  static const GEnumValue values[] = {
    {GST_WASAPI_DEVICE_ROLE_CONSOLE,
        "Games, system notifications, voice commands", "console"},
    {GST_WASAPI_DEVICE_ROLE_MULTIMEDIA, "Music, movies, recorded media",
        "multimedia"},
    {GST_WASAPI_DEVICE_ROLE_COMMS, "Voice communications", "comms"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstWasapiDeviceRole", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

gint
gst_wasapi_device_role_to_erole (gint role)
{
  switch (role) {
    case GST_WASAPI_DEVICE_ROLE_CONSOLE:
      return eConsole;
    case GST_WASAPI_DEVICE_ROLE_MULTIMEDIA:
      return eMultimedia;
    case GST_WASAPI_DEVICE_ROLE_COMMS:
      return eCommunications;
    default:
      g_assert_not_reached ();
  }
}

gint
gst_wasapi_erole_to_device_role (gint erole)
{
  switch (erole) {
    case eConsole:
      return GST_WASAPI_DEVICE_ROLE_CONSOLE;
    case eMultimedia:
      return GST_WASAPI_DEVICE_ROLE_MULTIMEDIA;
    case eCommunications:
      return GST_WASAPI_DEVICE_ROLE_COMMS;
    default:
      g_assert_not_reached ();
  }
}

const gchar *
gst_wasapi_util_hresult_to_string (HRESULT hr)
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
    case E_INVALIDARG:
      s = "E_INVALIDARG";
      break;
  }

  return s;
}

gboolean
gst_wasapi_util_get_device_client (GstElement * element,
    gboolean capture, gint role, const wchar_t * device_strid,
    IAudioClient ** ret_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;
  IMMDevice *device = NULL;
  IAudioClient *client = NULL;

  hr = CoCreateInstance (&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
      &IID_IMMDeviceEnumerator, (void **) &enumerator);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "CoCreateInstance (MMDeviceEnumerator) failed"
        ": %s", gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

  if (!device_strid) {
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint (enumerator,
        capture ? eCapture : eRender, role, &device);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (element,
          "IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: %s",
          gst_wasapi_util_hresult_to_string (hr));
      goto beach;
    }
  } else {
    hr = IMMDeviceEnumerator_GetDevice (enumerator, device_strid, &device);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (element, "IMMDeviceEnumerator::GetDevice (%S) failed"
          ": %s", device_strid, gst_wasapi_util_hresult_to_string (hr));
      goto beach;
    }
  }

  hr = IMMDevice_Activate (device, &IID_IAudioClient, CLSCTX_ALL, NULL,
      (void **) &client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "IMMDevice::Activate (IID_IAudioClient) failed"
        ": %s", gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

  IUnknown_AddRef (client);
  *ret_client = client;

  res = TRUE;

beach:
  if (client != NULL)
    IUnknown_Release (client);

  if (device != NULL)
    IUnknown_Release (device);

  if (enumerator != NULL)
    IUnknown_Release (enumerator);

  return res;
}

gboolean
gst_wasapi_util_get_render_client (GstElement * element, IAudioClient * client,
    IAudioRenderClient ** ret_render_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioRenderClient *render_client = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioRenderClient,
      (void **) &render_client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element,
        "IAudioClient::GetService (IID_IAudioRenderClient) failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

  *ret_render_client = render_client;
  res = TRUE;

beach:
  return res;
}

gboolean
gst_wasapi_util_get_capture_client (GstElement * element, IAudioClient * client,
    IAudioCaptureClient ** ret_capture_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioCaptureClient *capture_client = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioCaptureClient,
      (void **) &capture_client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element,
        "IAudioClient::GetService (IID_IAudioCaptureClient) failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

  *ret_capture_client = capture_client;
  res = TRUE;

beach:
  return res;
}

gboolean
gst_wasapi_util_get_clock (GstElement * element, IAudioClient * client,
    IAudioClock ** ret_clock)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioClock *clock = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioClock, (void **) &clock);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element,
        "IAudioClient::GetService (IID_IAudioClock) failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

  *ret_clock = clock;
  res = TRUE;

beach:
  return res;
}

static const gchar *
gst_waveformatex_to_audio_format (WAVEFORMATEXTENSIBLE * format)
{
  const gchar *fmt_str = NULL;
  GstAudioFormat fmt = GST_AUDIO_FORMAT_UNKNOWN;

  if (format->Format.wFormatTag == WAVE_FORMAT_PCM) {
    fmt = gst_audio_format_build_integer (TRUE, G_LITTLE_ENDIAN,
        format->Format.wBitsPerSample, format->Format.wBitsPerSample);
  } else if (format->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    if (format->Format.wBitsPerSample == 32)
      fmt = GST_AUDIO_FORMAT_F32LE;
    else if (format->Format.wBitsPerSample == 64)
      fmt = GST_AUDIO_FORMAT_F64LE;
  } else if (format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    if (IsEqualGUID (&format->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
      fmt = gst_audio_format_build_integer (TRUE, G_LITTLE_ENDIAN,
          format->Format.wBitsPerSample, format->Samples.wValidBitsPerSample);
    } else if (IsEqualGUID (&format->SubFormat,
            &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
      if (format->Format.wBitsPerSample == 32
          && format->Samples.wValidBitsPerSample == 32)
        fmt = GST_AUDIO_FORMAT_F32LE;
      else if (format->Format.wBitsPerSample == 64 &&
          format->Samples.wValidBitsPerSample == 64)
        fmt = GST_AUDIO_FORMAT_F64LE;
    }
  }

  if (fmt != GST_AUDIO_FORMAT_UNKNOWN)
    fmt_str = gst_audio_format_to_string (fmt);

  return fmt_str;
}

static void
gst_wasapi_util_channel_position_all_none (guint channels,
    GstAudioChannelPosition * position)
{
  int ii;
  for (ii = 0; ii < channels; ii++)
    position[ii] = GST_AUDIO_CHANNEL_POSITION_NONE;
}

/* Parse WAVEFORMATEX to get the gstreamer channel mask, and the wasapi channel
 * positions so GstAudioRingbuffer can reorder the audio data to match the
 * gstreamer channel order. */
static guint64
gst_wasapi_util_waveformatex_to_channel_mask (WAVEFORMATEXTENSIBLE * format,
    GstAudioChannelPosition ** out_position)
{
  int ii;
  guint64 mask = 0;
  WORD nChannels = format->Format.nChannels;
  DWORD dwChannelMask = format->dwChannelMask;
  GstAudioChannelPosition *pos = NULL;

  pos = g_new (GstAudioChannelPosition, nChannels);
  gst_wasapi_util_channel_position_all_none (nChannels, pos);

  /* Too many channels, have to assume that they are all non-positional */
  if (nChannels > G_N_ELEMENTS (wasapi_to_gst_pos)) {
    GST_INFO ("wasapi: got too many (%i) channels, assuming non-positional",
        nChannels);
    goto out;
  }

  /* Too many bits in the channel mask, and the bits don't match nChannels */
  if (dwChannelMask >> (G_N_ELEMENTS (wasapi_to_gst_pos) + 1) != 0) {
    GST_WARNING ("wasapi: too many bits in channel mask (%lu), assuming "
        "non-positional", dwChannelMask);
    goto out;
  }

  /* Map WASAPI's channel mask to Gstreamer's channel mask and positions.
   * If the no. of bits in the mask > nChannels, we will ignore the extra. */
  for (ii = 0; ii < nChannels; ii++) {
    if (!(dwChannelMask & wasapi_to_gst_pos[ii].wasapi_pos))
      /* Non-positional or unknown position, warn? */
      continue;
    mask |= G_GUINT64_CONSTANT(1) << wasapi_to_gst_pos[ii].gst_pos;
    pos[ii] = wasapi_to_gst_pos[ii].gst_pos;
  }

out:
  if (out_position)
    *out_position = pos;
  return mask;
}

gboolean
gst_wasapi_util_parse_waveformatex (WAVEFORMATEXTENSIBLE * format,
    GstCaps * template_caps, GstCaps ** out_caps,
    GstAudioChannelPosition ** out_positions)
{
  int ii;
  const gchar *afmt;
  guint64 channel_mask;

  *out_caps = NULL;

  /* TODO: handle SPDIF and other encoded formats */

  /* 1 or 2 channels <= 16 bits sample size OR
   * 1 or 2 channels > 16 bits sample size or >2 channels */
  if (format->Format.wFormatTag != WAVE_FORMAT_PCM &&
      format->Format.wFormatTag != WAVE_FORMAT_IEEE_FLOAT &&
      format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    /* Unhandled format tag */
    return FALSE;

  /* WASAPI can only tell us one canonical mix format that it will accept. The
   * alternative is calling IsFormatSupported on all combinations of formats.
   * Instead, it's simpler and faster to require conversion inside gstreamer */
  afmt = gst_waveformatex_to_audio_format (format);
  if (afmt == NULL)
    return FALSE;

  *out_caps = gst_caps_copy (template_caps);

  /* This will always return something that might be usable */
  channel_mask =
      gst_wasapi_util_waveformatex_to_channel_mask (format, out_positions);

  for (ii = 0; ii < gst_caps_get_size (*out_caps); ii++) {
    GstStructure *s = gst_caps_get_structure (*out_caps, ii);

    gst_structure_set (s,
        "format", G_TYPE_STRING, afmt,
        "channels", G_TYPE_INT, format->Format.nChannels,
        "rate", G_TYPE_INT, format->Format.nSamplesPerSec,
        "channel-mask", GST_TYPE_BITMASK, channel_mask,
        NULL);
  }

  return TRUE;
}
