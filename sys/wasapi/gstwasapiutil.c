/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#include "gstwasapiutil.h"

#include <mmdeviceapi.h>

/* These seem to be missing in the Windows SDK... */
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

const gchar *
gst_wasapi_util_hresult_to_string (HRESULT hr)
{
  const gchar *s = "AUDCLNT_E_UNKNOWN";

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
    case AUDCLNT_E_INVALID_SIZE:
      s = "AUDCLNT_E_INVALID_SIZE";
      break;
    case AUDCLNT_E_DEVICE_IN_USE:
      s = "AUDCLNT_E_DEVICE_IN_USE";
      break;
    case AUDCLNT_E_BUFFER_OPERATION_PENDING:
      s = "AUDCLNT_E_BUFFER_OPERATION_PENDING";
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
    case AUDCLNT_E_BUFFER_SIZE_ERROR:
      s = "AUDCLNT_E_BUFFER_SIZE_ERROR";
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
  }

  return s;
}

gboolean
gst_wasapi_util_get_default_device_client (GstElement * element,
    gboolean capture,
    guint rate,
    GstClockTime buffer_time,
    GstClockTime period_time,
    DWORD flags, IAudioClient ** ret_client, GstClockTime * ret_latency)
{
  gboolean res = FALSE;
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;
  IMMDevice *device = NULL;
  IAudioClient *client = NULL;
  REFERENCE_TIME latency_rt, def_period, min_period;
  WAVEFORMATEXTENSIBLE format;

  hr = CoCreateInstance (&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
      &IID_IMMDeviceEnumerator, &enumerator);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "CoCreateInstance (MMDeviceEnumerator) failed");
    goto beach;
  }

  hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint (enumerator,
      (capture) ? eCapture : eRender, eCommunications, &device);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element,
        "IMMDeviceEnumerator::GetDefaultAudioEndpoint () failed");
    goto beach;
  }

  hr = IMMDevice_Activate (device, &IID_IAudioClient, CLSCTX_ALL, NULL,
      &client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "IMMDevice::Activate (IID_IAudioClient) failed");
    goto beach;
  }

  hr = IAudioClient_GetDevicePeriod (client, &def_period, &min_period);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "IAudioClient::GetDevicePeriod () failed");
    goto beach;
  }

  ZeroMemory (&format, sizeof (format));
  format.Format.cbSize = sizeof (format) - sizeof (format.Format);
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = 2;
  format.Format.nSamplesPerSec = rate;
  format.Format.wBitsPerSample = 16;
  format.Format.nBlockAlign = format.Format.nChannels
      * (format.Format.wBitsPerSample / 8);
  format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec
      * format.Format.nBlockAlign;
  format.Samples.wValidBitsPerSample = 16;
  format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

  hr = IAudioClient_Initialize (client, AUDCLNT_SHAREMODE_EXCLUSIVE,    /* or AUDCLNT_SHAREMODE_SHARED */
      flags, buffer_time / 100, /* buffer duration in 100s of ns */
      period_time / 100,        /* periodicity in 100s of ns */
      (WAVEFORMATEX *) & format, NULL);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL),
        ("IAudioClient::Initialize () failed: %s",
            gst_wasapi_util_hresult_to_string (hr)));
    goto beach;
  }

  hr = IAudioClient_GetStreamLatency (client, &latency_rt);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (element, "IAudioClient::GetStreamLatency () failed");
    goto beach;
  }

  GST_INFO_OBJECT (element, "default period: %d (%d ms), "
      "minimum period: %d (%d ms), "
      "latency: %d (%d ms)",
      (guint32) def_period, (guint32) def_period / 10000,
      (guint32) min_period, (guint32) min_period / 10000,
      (guint32) latency_rt, (guint32) latency_rt / 10000);

  IUnknown_AddRef (client);
  *ret_client = client;

  *ret_latency = latency_rt * 100;

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

#if 0
static WAVEFORMATEXTENSIBLE *
gst_wasapi_src_probe_device_format (GstWasapiSrc * self, IMMDevice * device)
{
  HRESULT hr;
  IPropertyStore *props = NULL;
  PROPVARIANT format_prop;
  WAVEFORMATEXTENSIBLE *format = NULL;

  hr = IMMDevice_OpenPropertyStore (device, STGM_READ, &props);
  if (hr != S_OK)
    goto beach;

  PropVariantInit (&format_prop);
  hr = IPropertyStore_GetValue (props, &PKEY_AudioEngine_DeviceFormat,
      &format_prop);
  if (hr != S_OK)
    goto beach;

  format = (WAVEFORMATEXTENSIBLE *) format_prop.blob.pBlobData;

  /* hmm: HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\{64adb8b7-9716-4c02-8929-96e53f5642da}\Properties */

beach:
  if (props != NULL)
    IUnknown_Release (props);

  return format;
}
#endif
