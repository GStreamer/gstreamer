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
#include "gstwasapidevice.h"

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi_debug);
#define GST_CAT_DEFAULT gst_wasapi_debug

/* This was only added to MinGW in ~2015 and our Cerbero toolchain is too old */
#if defined(_MSC_VER)
#include <functiondiscoverykeys_devpkey.h>
#elif !defined(PKEY_Device_FriendlyName)
#include <initguid.h>
#include <propkey.h>
DEFINE_PROPERTYKEY (PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,
    0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
DEFINE_PROPERTYKEY (PKEY_AudioEngine_DeviceFormat, 0xf19f064d, 0x82c, 0x4e27,
    0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c, 0);
#endif

/* __uuidof is only available in C++, so we hard-code the GUID values for all
 * these. This is ok because these are ABI. */
const CLSID CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c,
  {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}
};

const IID IID_IMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35,
  {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}
};

const IID IID_IMMEndpoint = { 0x1be09788, 0x6894, 0x4089,
  {0x85, 0x86, 0x9a, 0x2a, 0x6c, 0x26, 0x5a, 0xc5}
};

const IID IID_IAudioClient = { 0x1cb9ad4c, 0xdbfa, 0x4c32,
  {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}
};

const IID IID_IAudioClient3 = { 0x7ed4ee07, 0x8e67, 0x4cd4,
  {0x8c, 0x1a, 0x2b, 0x7a, 0x59, 0x87, 0xad, 0x42}
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
/* *INDENT-ON* */

static int windows_major_version = 0;

gboolean
gst_wasapi_util_have_audioclient3 (void)
{
  if (windows_major_version > 0)
    return windows_major_version == 10;

  if (g_getenv ("GST_WASAPI_DISABLE_AUDIOCLIENT3") != NULL) {
    windows_major_version = 6;
    return FALSE;
  }

  /* https://msdn.microsoft.com/en-us/library/windows/desktop/ms724834(v=vs.85).aspx */
  windows_major_version = 6;
  if (g_win32_check_windows_version (10, 0, 0, G_WIN32_OS_ANY))
    windows_major_version = 10;

  return windows_major_version == 10;
}

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
gst_wasapi_util_hresult_to_string (HRESULT hr)
{
  DWORD flags;
  gchar *ret_text;
  LPTSTR error_text = NULL;

  flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER
      | FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessage (flags, NULL, hr, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR) & error_text, 0, NULL);

  /* If we couldn't get the error msg, try the fallback switch statement */
  if (error_text == NULL)
    return g_strdup (hresult_to_string_fallback (hr));

#ifdef UNICODE
  /* If UNICODE is defined, LPTSTR is LPWSTR which is UTF-16 */
  ret_text = g_utf16_to_utf8 (error_text, 0, NULL, NULL, NULL);
#else
  ret_text = g_strdup (error_text);
#endif

  LocalFree (error_text);
  return ret_text;
}

static IMMDeviceEnumerator *
gst_wasapi_util_get_device_enumerator (GstElement * self)
{
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;

  hr = CoCreateInstance (&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
      &IID_IMMDeviceEnumerator, (void **) &enumerator);
  HR_FAILED_RET (hr, CoCreateInstance (MMDeviceEnumerator), NULL);

  return enumerator;
}

gboolean
gst_wasapi_util_get_devices (GstElement * self, gboolean active,
    GList ** devices)
{
  gboolean res = FALSE;
  static GstStaticCaps scaps = GST_STATIC_CAPS (GST_WASAPI_STATIC_CAPS);
  DWORD dwStateMask = active ? DEVICE_STATE_ACTIVE : DEVICE_STATEMASK_ALL;
  IMMDeviceCollection *device_collection = NULL;
  IMMDeviceEnumerator *enumerator = NULL;
  const gchar *device_class, *element_name;
  guint ii, count;
  HRESULT hr;

  *devices = NULL;

  enumerator = gst_wasapi_util_get_device_enumerator (self);
  if (!enumerator)
    return FALSE;

  hr = IMMDeviceEnumerator_EnumAudioEndpoints (enumerator, eAll, dwStateMask,
      &device_collection);
  HR_FAILED_GOTO (hr, IMMDeviceEnumerator::EnumAudioEndpoints, err);

  hr = IMMDeviceCollection_GetCount (device_collection, &count);
  HR_FAILED_GOTO (hr, IMMDeviceCollection::GetCount, err);

  /* Create a GList of GstDevices* to return */
  for (ii = 0; ii < count; ii++) {
    IMMDevice *item = NULL;
    IMMEndpoint *endpoint = NULL;
    IAudioClient *client = NULL;
    IPropertyStore *prop_store = NULL;
    WAVEFORMATEX *format = NULL;
    gchar *description = NULL;
    gchar *strid = NULL;
    EDataFlow dataflow;
    PROPVARIANT var;
    wchar_t *wstrid;
    GstDevice *device;
    GstStructure *props;
    GstCaps *caps;

    hr = IMMDeviceCollection_Item (device_collection, ii, &item);
    if (hr != S_OK)
      continue;

    hr = IMMDevice_QueryInterface (item, &IID_IMMEndpoint, (void **) &endpoint);
    if (hr != S_OK)
      goto next;

    hr = IMMEndpoint_GetDataFlow (endpoint, &dataflow);
    if (hr != S_OK)
      goto next;

    if (dataflow == eRender) {
      device_class = "Audio/Sink";
      element_name = "wasapisink";
    } else {
      device_class = "Audio/Source";
      element_name = "wasapisrc";
    }

    PropVariantInit (&var);

    hr = IMMDevice_GetId (item, &wstrid);
    if (hr != S_OK)
      goto next;
    strid = g_utf16_to_utf8 (wstrid, -1, NULL, NULL, NULL);
    CoTaskMemFree (wstrid);

    hr = IMMDevice_OpenPropertyStore (item, STGM_READ, &prop_store);
    if (hr != S_OK)
      goto next;

    /* NOTE: More properties can be added as needed from here:
     * https://msdn.microsoft.com/en-us/library/windows/desktop/dd370794(v=vs.85).aspx */
    hr = IPropertyStore_GetValue (prop_store, &PKEY_Device_FriendlyName, &var);
    if (hr != S_OK)
      goto next;
    description = g_utf16_to_utf8 (var.pwszVal, -1, NULL, NULL, NULL);
    PropVariantClear (&var);

    /* Get the audio client so we can fetch the mix format for shared mode
     * to get the device format for exclusive mode (or something close to that)
     * fetch PKEY_AudioEngine_DeviceFormat from the property store. */
    hr = IMMDevice_Activate (item, &IID_IAudioClient, CLSCTX_ALL, NULL,
        (void **) &client);
    if (hr != S_OK) {
      gchar *msg = gst_wasapi_util_hresult_to_string (hr);
      GST_ERROR_OBJECT (self, "IMMDevice::Activate (IID_IAudioClient) failed"
          "on %s: %s", strid, msg);
      g_free (msg);
      goto next;
    }

    hr = IAudioClient_GetMixFormat (client, &format);
    if (hr != S_OK || format == NULL) {
      gchar *msg = gst_wasapi_util_hresult_to_string (hr);
      GST_ERROR_OBJECT (self, "GetMixFormat failed on %s: %s", strid, msg);
      g_free (msg);
      goto next;
    }

    if (!gst_wasapi_util_parse_waveformatex ((WAVEFORMATEXTENSIBLE *) format,
            gst_static_caps_get (&scaps), &caps, NULL))
      goto next;

    /* Set some useful properties */
    props = gst_structure_new ("wasapi-proplist",
        "device.api", G_TYPE_STRING, "wasapi",
        "device.strid", G_TYPE_STRING, GST_STR_NULL (strid),
        "wasapi.device.description", G_TYPE_STRING, description, NULL);

    device = g_object_new (GST_TYPE_WASAPI_DEVICE, "device", strid,
        "display-name", description, "caps", caps,
        "device-class", device_class, "properties", props, NULL);
    GST_WASAPI_DEVICE (device)->element = element_name;

    gst_structure_free (props);
    gst_caps_unref (caps);
    *devices = g_list_prepend (*devices, device);

  next:
    PropVariantClear (&var);
    if (prop_store)
      IUnknown_Release (prop_store);
    if (endpoint)
      IUnknown_Release (endpoint);
    if (client)
      IUnknown_Release (client);
    if (item)
      IUnknown_Release (item);
    if (description)
      g_free (description);
    if (strid)
      g_free (strid);
  }

  res = TRUE;

err:
  if (enumerator)
    IUnknown_Release (enumerator);
  if (device_collection)
    IUnknown_Release (device_collection);
  return res;
}

gboolean
gst_wasapi_util_get_device_format (GstElement * self,
    gint device_mode, IMMDevice * device, IAudioClient * client,
    WAVEFORMATEX ** ret_format)
{
  WAVEFORMATEX *format;
  HRESULT hr;

  *ret_format = NULL;

  hr = IAudioClient_GetMixFormat (client, &format);
  HR_FAILED_RET (hr, IAudioClient::GetMixFormat, FALSE);

  /* WASAPI always accepts the format returned by GetMixFormat in shared mode */
  if (device_mode == AUDCLNT_SHAREMODE_SHARED)
    goto out;

  /* WASAPI may or may not support this format in exclusive mode */
  hr = IAudioClient_IsFormatSupported (client, AUDCLNT_SHAREMODE_EXCLUSIVE,
      format, NULL);
  if (hr == S_OK)
    goto out;

  CoTaskMemFree (format);

  /* Open the device property store, and get the format that WASAPI has been
   * using for sending data to the device */
  {
    PROPVARIANT var;
    IPropertyStore *prop_store = NULL;

    hr = IMMDevice_OpenPropertyStore (device, STGM_READ, &prop_store);
    HR_FAILED_RET (hr, IMMDevice::OpenPropertyStore, FALSE);

    hr = IPropertyStore_GetValue (prop_store, &PKEY_AudioEngine_DeviceFormat,
        &var);
    if (hr != S_OK) {
      gchar *msg = gst_wasapi_util_hresult_to_string (hr);
      GST_ERROR_OBJECT (self, "GetValue failed: %s", msg);
      g_free (msg);
      IUnknown_Release (prop_store);
      return FALSE;
    }

    format = malloc (var.blob.cbSize);
    memcpy (format, var.blob.pBlobData, var.blob.cbSize);

    PropVariantClear (&var);
    IUnknown_Release (prop_store);
  }

  /* WASAPI may or may not support this format in exclusive mode */
  hr = IAudioClient_IsFormatSupported (client, AUDCLNT_SHAREMODE_EXCLUSIVE,
      format, NULL);
  if (hr == S_OK)
    goto out;

  GST_ERROR_OBJECT (self, "AudioEngine DeviceFormat not supported");
  free (format);
  return FALSE;

out:
  *ret_format = format;
  return TRUE;
}

gboolean
gst_wasapi_util_get_device_client (GstElement * self,
    gboolean capture, gint role, const wchar_t * device_strid,
    IMMDevice ** ret_device, IAudioClient ** ret_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;
  IMMDevice *device = NULL;
  IAudioClient *client = NULL;

  if (!(enumerator = gst_wasapi_util_get_device_enumerator (self)))
    goto beach;

  if (!device_strid) {
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint (enumerator,
        capture ? eCapture : eRender, role, &device);
    HR_FAILED_GOTO (hr, IMMDeviceEnumerator::GetDefaultAudioEndpoint, beach);
  } else {
    hr = IMMDeviceEnumerator_GetDevice (enumerator, device_strid, &device);
    if (hr != S_OK) {
      gchar *msg = gst_wasapi_util_hresult_to_string (hr);
      GST_ERROR_OBJECT (self, "IMMDeviceEnumerator::GetDevice (%S) failed"
          ": %s", device_strid, msg);
      g_free (msg);
      goto beach;
    }
  }

  if (gst_wasapi_util_have_audioclient3 ())
    hr = IMMDevice_Activate (device, &IID_IAudioClient3, CLSCTX_ALL, NULL,
        (void **) &client);
  else
    hr = IMMDevice_Activate (device, &IID_IAudioClient, CLSCTX_ALL, NULL,
        (void **) &client);
  HR_FAILED_GOTO (hr, IMMDevice::Activate (IID_IAudioClient), beach);

  IUnknown_AddRef (client);
  IUnknown_AddRef (device);
  *ret_client = client;
  *ret_device = device;

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
gst_wasapi_util_get_render_client (GstElement * self, IAudioClient * client,
    IAudioRenderClient ** ret_render_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioRenderClient *render_client = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioRenderClient,
      (void **) &render_client);
  HR_FAILED_GOTO (hr, IAudioClient::GetService, beach);

  *ret_render_client = render_client;
  res = TRUE;

beach:
  return res;
}

gboolean
gst_wasapi_util_get_capture_client (GstElement * self, IAudioClient * client,
    IAudioCaptureClient ** ret_capture_client)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioCaptureClient *capture_client = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioCaptureClient,
      (void **) &capture_client);
  HR_FAILED_GOTO (hr, IAudioClient::GetService, beach);

  *ret_capture_client = capture_client;
  res = TRUE;

beach:
  return res;
}

gboolean
gst_wasapi_util_get_clock (GstElement * self, IAudioClient * client,
    IAudioClock ** ret_clock)
{
  gboolean res = FALSE;
  HRESULT hr;
  IAudioClock *clock = NULL;

  hr = IAudioClient_GetService (client, &IID_IAudioClock, (void **) &clock);
  HR_FAILED_GOTO (hr, IAudioClient::GetService, beach);

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
    GST_INFO ("Got too many (%i) channels, assuming non-positional", nChannels);
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
  for (ii = 0; ii < nChannels; ii++) {
    if (!(dwChannelMask & wasapi_to_gst_pos[ii].wasapi_pos))
      /* Non-positional or unknown position, warn? */
      continue;
    mask |= G_GUINT64_CONSTANT (1) << wasapi_to_gst_pos[ii].gst_pos;
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
        "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
  }

  return TRUE;
}

void
gst_wasapi_util_get_best_buffer_sizes (GstAudioRingBufferSpec * spec,
    gboolean exclusive, REFERENCE_TIME default_period,
    REFERENCE_TIME min_period, REFERENCE_TIME * ret_period,
    REFERENCE_TIME * ret_buffer_duration)
{
  REFERENCE_TIME use_period, use_buffer;

  /* Figure out what integral device period to use as the base */
  if (exclusive) {
    /* Exclusive mode can run at multiples of either the minimum period or the
     * default period; these are on the hardware ringbuffer */
    if (spec->latency_time * 10 > default_period)
      use_period = default_period;
    else
      use_period = min_period;
  } else {
    /* Shared mode always runs at the default period, so if we want a larger
     * period (for lower CPU usage), we do it as a multiple of that */
    use_period = default_period;
  }

  /* Ensure that the period (latency_time) used is an integral multiple of
   * either the default period or the minimum period */
  use_period = use_period * MAX ((spec->latency_time * 10) / use_period, 1);

  if (exclusive) {
    /* Buffer duration is the same as the period in exclusive mode. The
     * hardware is always writing out one buffer (of size *ret_period), and
     * we're writing to the other one. */
    use_buffer = use_period;
  } else {
    /* Ask WASAPI to create a software ringbuffer of at least this size; it may
     * be larger so the actual buffer time may be different, which is why after
     * initialization we read the buffer duration actually in-use and set
     * segsize/segtotal from that. */
    use_buffer = spec->buffer_time * 10;
    /* Has to be at least twice the period */
    if (use_buffer < 2 * use_period)
      use_buffer = 2 * use_period;
  }

  *ret_period = use_period;
  *ret_buffer_duration = use_buffer;
}

gboolean
gst_wasapi_util_initialize_audioclient (GstElement * self,
    GstAudioRingBufferSpec * spec, IAudioClient * client,
    WAVEFORMATEX * format, guint sharemode, gboolean low_latency,
    guint * ret_devicep_frames)
{
  REFERENCE_TIME default_period, min_period;
  REFERENCE_TIME device_period, device_buffer_duration;
  guint rate;
  guint32 n_frames;
  HRESULT hr;

  hr = IAudioClient_GetDevicePeriod (client, &default_period, &min_period);
  HR_FAILED_RET (hr, IAudioClient::GetDevicePeriod, FALSE);

  GST_INFO_OBJECT (self, "wasapi default period: %" G_GINT64_FORMAT
      ", min period: %" G_GINT64_FORMAT, default_period, min_period);

  rate = GST_AUDIO_INFO_RATE (&spec->info);

  if (low_latency) {
    if (sharemode == AUDCLNT_SHAREMODE_SHARED) {
      device_period = default_period;
      device_buffer_duration = 0;
    } else {
      device_period = min_period;
      device_buffer_duration = min_period;
    }
  } else {
    /* Clamp values to integral multiples of an appropriate period */
    gst_wasapi_util_get_best_buffer_sizes (spec,
        sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE, default_period,
        min_period, &device_period, &device_buffer_duration);
  }

  hr = IAudioClient_Initialize (client, sharemode,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK, device_buffer_duration,
      /* This must always be 0 in shared mode */
      sharemode == AUDCLNT_SHAREMODE_SHARED ? 0 : device_period, format, NULL);

  if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED &&
      sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
    GST_WARNING_OBJECT (self, "initialize failed due to unaligned period %i",
        (int) device_period);

    /* Calculate a new aligned period. First get the aligned buffer size. */
    hr = IAudioClient_GetBufferSize (client, &n_frames);
    HR_FAILED_RET (hr, IAudioClient::GetBufferSize, FALSE);

    device_period = (GST_SECOND / 100) * n_frames / rate;

    GST_WARNING_OBJECT (self, "trying to re-initialize with period %i "
        "(%i frames, %i rate)", (int) device_period, n_frames, rate);

    hr = IAudioClient_Initialize (client, sharemode,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, device_period,
        device_period, format, NULL);
  }
  HR_FAILED_RET (hr, IAudioClient::Initialize, FALSE);

  if (sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
    /* We use the device period for the segment size and that needs to match
     * the buffer size exactly when we write into it */
    hr = IAudioClient_GetBufferSize (client, &n_frames);
    HR_FAILED_RET (hr, IAudioClient::GetBufferSize, FALSE);

    *ret_devicep_frames = n_frames;
  } else {
    *ret_devicep_frames = (rate * device_period * 100) / GST_SECOND;
  }

  return TRUE;
}

gboolean
gst_wasapi_util_initialize_audioclient3 (GstElement * self,
    GstAudioRingBufferSpec * spec, IAudioClient3 * client,
    WAVEFORMATEX * format, gboolean low_latency, guint * ret_devicep_frames)
{
  HRESULT hr;
  guint devicep_frames;
  guint defaultp_frames, fundp_frames, minp_frames, maxp_frames;
  WAVEFORMATEX *tmpf;

  hr = IAudioClient3_GetSharedModeEnginePeriod (client, format,
      &defaultp_frames, &fundp_frames, &minp_frames, &maxp_frames);
  HR_FAILED_RET (hr, IAudioClient3::GetSharedModeEnginePeriod, FALSE);

  GST_INFO_OBJECT (self, "Using IAudioClient3, default period %i frames, "
      "fundamental period %i frames, minimum period %i frames, maximum period "
      "%i frames", defaultp_frames, fundp_frames, minp_frames, maxp_frames);

  if (low_latency)
    devicep_frames = minp_frames;
  else
    /* Just pick the max period, because lower values can cause glitches
     * https://bugzilla.gnome.org/show_bug.cgi?id=794497 */
    devicep_frames = maxp_frames;

  hr = IAudioClient3_InitializeSharedAudioStream (client,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK, devicep_frames, format, NULL);
  HR_FAILED_RET (hr, IAudioClient3::InitializeSharedAudioStream, FALSE);

  hr = IAudioClient3_GetCurrentSharedModeEnginePeriod (client, &tmpf,
      &devicep_frames);
  CoTaskMemFree (tmpf);
  HR_FAILED_RET (hr, IAudioClient3::GetCurrentSharedModeEnginePeriod, FALSE);

  *ret_devicep_frames = devicep_frames;
  return TRUE;
}
