/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

/*
 * This module implements a GstAudioRingBuffer subclass using
 * Windows Audio Session API (WASAPI).
 *
 * Major Components:
 *
 * - RbufCtx: Encapsulates WASAPI objects such as IAudioClient,
 *   IAudioRenderClient/IAudioCaptureClient, volume/mute interfaces, and events.
 *
 * - Wasapi2DeviceManager: Handles IMMDevice activation and RbufCtx creation
 *   in a dedicated COM thread. This avoids blocking the main I/O thread.
 *
 * - CommandData and command queue: All user-triggered operations (open, start,
 *   stop, volume changes, etc.) are serialized through a command queue.
 *
 * - gst_wasapi2_rbuf_loop_thread: The main loop that processes WASAPI I/O events
 *   and executes queued commands.
 *
 * Design Highlights:
 *
 * 1) The Wasapi2DeviceManager and GstWasapi2Rbuf classes are decoupled to manage
 *    device initialization efficiently. Creating and initializing an IAudioClient
 *    can take significant time due to format negotiation or endpoint activation.
 *
 * - During a normal open/start sequence, the main I/O thread (gst_wasapi2_rbuf_loop_thread)
 *   synchronously waits for Wasapi2DeviceManager to finish device activation and
 *   RbufCtx creation before proceeding.
 *
 * - In contrast, when a device is already open and a dynamic device change
 *   is requested, device creation is delegated to Wasapi2DeviceManager
 *   asynchronously in the background. Once initialization succeeds,
 *   newly created RbufCtx is returned back to the I/O thread via the
 *   command queue and swapped in without interrupting ongoing I/O.
 *
 *   This separation allows for seamless device transitions without blocking audio streaming.
 *
 * 2) All user-triggered events (such as open, close, start, stop, volume/mute changes)
 *    are serialized through a command queue and processed exclusively by the main I/O thread.
 *    This ensures thread-safe and ordered execution of state changes, avoiding race conditions.
 */

#include "gstwasapi2rbuf.h"
#include "gstwasapi2activator.h"
#include <endpointvolume.h>
#include <memory>
#include <atomic>
#include <vector>
#include <endpointvolume.h>
#include <mutex>
#include <condition_variable>
#include <wrl.h>
#include <string>
#include <string.h>
#include <queue>
#include <avrt.h>

GST_DEBUG_CATEGORY_STATIC (gst_wasapi2_rbuf_debug);
#define GST_CAT_DEFAULT gst_wasapi2_rbuf_debug

static GstStaticCaps template_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);

/* Defined for _WIN32_WINNT >= _NT_TARGET_VERSION_WIN10_RS4 */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

static gpointer device_manager_com_thread (gpointer manager);

struct RbufCtx
{
  RbufCtx () = delete;
  RbufCtx (const std::string & id) : device_id (id)
  {
    capture_event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    render_event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  ~RbufCtx ()
  {
    Stop ();

    if (volume_callback && endpoint_volume)
      endpoint_volume->UnregisterControlChangeNotify (volume_callback.Get ());

    if (mix_format)
      CoTaskMemFree (mix_format);
    gst_clear_caps (&caps);

    CloseHandle (capture_event);
    CloseHandle (render_event);
  }

  HRESULT Start ()
  {
    if (running)
      return S_OK;

    auto hr = client->Start ();
    if (!gst_wasapi2_result (hr))
      return hr;

    if (dummy_client) {
      hr = dummy_client->Start ();
      if (!gst_wasapi2_result (hr)) {
        client->Stop ();
        client->Reset ();

        return hr;
      }
    }

    running = true;

    return S_OK;
  }

  HRESULT Stop ()
  {
    HRESULT hr = S_OK;
    if (client) {
      hr = client->Stop ();
      if (gst_wasapi2_result (hr))
        client->Reset ();
    }

    if (dummy_client) {
      auto dummy_hr = dummy_client->Stop ();
      if (gst_wasapi2_result (dummy_hr))
        dummy_client->Reset ();
    }

    running = false;

    return hr;
  }

  HRESULT SetVolume (float vol)
  {
    if (!stream_volume)
      return S_OK;

    UINT32 count = 0;
    auto hr = stream_volume->GetChannelCount (&count);
    if (!gst_wasapi2_result (hr) || count == 0)
      return hr;

    volumes.resize (count);

    for (size_t i = 0; i < volumes.size (); i++)
      volumes[i] = vol;

    return stream_volume->SetAllVolumes ((UINT32) volumes.size (),
        volumes.data ());
  }

  BOOL IsEndpointMuted ()
  {
    return endpoint_muted.load (std::memory_order_acquire);
  }

  GstWasapi2EndpointClass endpoint_class;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioClient> dummy_client;
  ComPtr<IAudioCaptureClient> capture_client;
  ComPtr<IAudioRenderClient> render_client;
  ComPtr<IAudioStreamVolume> stream_volume;
  ComPtr<IAudioEndpointVolume> endpoint_volume;
  ComPtr<IAudioEndpointVolumeCallback> volume_callback;
  std::string device_id;
  std::vector<float> volumes;
  std::atomic<bool> endpoint_muted = { false };
  HANDLE capture_event;
  HANDLE render_event;
  GstCaps *caps = nullptr;
  WAVEFORMATEX *mix_format = nullptr;

  UINT32 period = 0;
  UINT32 client_buf_size = 0;
  UINT32 dummy_buf_size = 0;
  bool is_default = false;
  bool running = false;
  bool error_posted = false;
};

typedef std::shared_ptr<RbufCtx> RbufCtxPtr;

enum class CommandType
{
  Shutdown,
  SetDevice,
  UpdateDevice,
  Open,
  Close,
  Acquire,
  Release,
  Start,
  Stop,
  GetCaps,
  UpdateVolume,
};

static inline const gchar *
command_type_to_string (CommandType type)
{
  switch (type) {
    case CommandType::Shutdown:
      return "Shutdown";
    case CommandType::SetDevice:
      return "SetDevice";
    case CommandType::UpdateDevice:
      return "UpdateDevice";
    case CommandType::Open:
      return "Open";
    case CommandType::Close:
      return "Close";
    case CommandType::Acquire:
      return "Acquire";
    case CommandType::Release:
      return "Release";
    case CommandType::Start:
      return "Start";
    case CommandType::Stop:
      return "Stop";
    case CommandType::GetCaps:
      return "GetCaps";
    case CommandType::UpdateVolume:
      return "UpdateVolume";
    default:
      return "Unknown";
  }
}

struct CommandData
{
  CommandData (const CommandData &) = delete;
  CommandData& operator= (const CommandData &) = delete;
  CommandData () = delete;
  CommandData (CommandType ctype) : type (ctype)
  {
    event_handle = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  virtual ~CommandData ()
  {
    CloseHandle (event_handle);
  }

  CommandType type;

  HRESULT hr = S_OK;
  HANDLE event_handle;
};

struct CommandSetDevice : public CommandData
{
  CommandSetDevice () : CommandData (CommandType::SetDevice) {}

  std::string device_id;
  GstWasapi2EndpointClass endpoint_class;
  guint pid = 0;
  gboolean low_latency = FALSE;
};

struct CommandUpdateDevice : public CommandData
{
  CommandUpdateDevice (const std::string & id)
    : CommandData (CommandType::UpdateDevice), device_id (id) {}
  std::shared_ptr<RbufCtx> ctx;
  std::string device_id;
};

struct CommandGetCaps : public CommandData
{
  CommandGetCaps () : CommandData (CommandType::GetCaps) { }

  GstCaps *caps = nullptr;
};

struct CommandAcquire : public CommandData
{
  CommandAcquire (GstAudioRingBufferSpec * s) :
      CommandData (CommandType::Acquire), spec (s) {}

  GstAudioRingBufferSpec *spec = nullptr;
};

static void gst_wasapi2_rbuf_push_command (GstWasapi2Rbuf * self,
    std::shared_ptr<CommandData> cmd);


DEFINE_GUID (IID_Wasapi2EndpointVolumeCallback, 0x21ba991f, 0x4d78,
    0x418c, 0xa1, 0xea, 0x8a, 0xc7, 0xdd, 0xa2, 0xdc, 0x39);
class Wasapi2EndpointVolumeCallback : public IAudioEndpointVolumeCallback
{
public:
  static void CreateInstance (IAudioEndpointVolumeCallback ** iface,
      RbufCtxPtr & ctx)
  {
    auto self = new Wasapi2EndpointVolumeCallback ();
    self->ctx_ = ctx;
    *iface = static_cast<IAudioEndpointVolumeCallback *>(
        static_cast<Wasapi2EndpointVolumeCallback*>(self));
  }

  STDMETHODIMP_ (ULONG)
  AddRef (void)
  {
    return InterlockedIncrement (&refcount_);
  }

  STDMETHODIMP_ (ULONG)
  Release (void)
  {
    ULONG ref_count;

    ref_count = InterlockedDecrement (&refcount_);

    if (ref_count == 0)
      delete this;

    return ref_count;
  }

  STDMETHODIMP
  QueryInterface (REFIID riid, void ** object)
  {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAgileObject)) {
      *object = static_cast<IUnknown *>(
          static_cast<Wasapi2EndpointVolumeCallback*>(this));
    } else if (riid == __uuidof(IAudioEndpointVolumeCallback)) {
      *object = static_cast<IAudioEndpointVolumeCallback *>(
          static_cast<Wasapi2EndpointVolumeCallback*>(this));
    } else if (riid == IID_Wasapi2EndpointVolumeCallback) {
      *object = static_cast<Wasapi2EndpointVolumeCallback *> (this);
    } else {
      *object = nullptr;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  STDMETHODIMP
  OnNotify (AUDIO_VOLUME_NOTIFICATION_DATA * notify)
  {
    auto ctx = ctx_.lock ();
    if (!ctx)
      return S_OK;

    ctx->endpoint_muted.store (notify->bMuted, std::memory_order_release);

    return S_OK;
  }

private:
  Wasapi2EndpointVolumeCallback () {}
  virtual ~Wasapi2EndpointVolumeCallback () {}

private:
  ULONG refcount_ = 1;
  std::weak_ptr<RbufCtx> ctx_;
};

struct RbufCtxDesc
{
  RbufCtxDesc ()
  {
    event_handle = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  ~RbufCtxDesc ()
  {
    CloseHandle (event_handle);
  }

  GstWasapi2Rbuf *rbuf = nullptr;
  GstWasapi2EndpointClass endpoint_class;
  std::string device_id;
  guint pid;
  RbufCtxPtr ctx;
  gint64 buffer_time;
  gint64 latency_time;
  WAVEFORMATEX *mix_format = nullptr;
  gboolean low_latency = FALSE;
  HANDLE event_handle;
};

static gboolean
is_equal_device_id (const gchar * a, const gchar * b)
{
  auto len_a = strlen (a);
  auto len_b = strlen (b);

  if (len_a != len_b)
    return FALSE;

#ifdef _MSC_VER
  return _strnicmp (a, b, len_a) == 0;
#else
  return strncasecmp (a, b, len_a) == 0;
#endif
}

static HRESULT
initialize_audio_client3 (IAudioClient * client_handle,
    WAVEFORMATEX * mix_format, guint * period, DWORD extra_flags)
{
  HRESULT hr = S_OK;
  UINT32 default_period, fundamental_period, min_period, max_period;
  /* AUDCLNT_STREAMFLAGS_NOPERSIST is not allowed for
   * InitializeSharedAudioStream */
  DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  ComPtr < IAudioClient3 > audio_client;

  stream_flags |= extra_flags;

  hr = client_handle->QueryInterface (IID_PPV_ARGS (&audio_client));
  if (!gst_wasapi2_result (hr)) {
    GST_INFO ("IAudioClient3 interface is unavailable");
    return hr;
  }

  hr = audio_client->GetSharedModeEnginePeriod (mix_format,
      &default_period, &fundamental_period, &min_period, &max_period);
  if (!gst_wasapi2_result (hr)) {
    GST_INFO ("Couldn't get period");
    return hr;
  }

  GST_INFO ("Using IAudioClient3, default period %d frames, "
      "fundamental period %d frames, minimum period %d frames, maximum period "
      "%d frames", default_period, fundamental_period, min_period, max_period);

  *period = min_period;

  hr = audio_client->InitializeSharedAudioStream (stream_flags, min_period,
      mix_format, nullptr);

  if (!gst_wasapi2_result (hr)) {
    GST_WARNING ("IAudioClient3::InitializeSharedAudioStream failed 0x%x",
        (guint) hr);
  }

  return hr;
}

static HRESULT
initialize_audio_client (IAudioClient * client_handle,
    WAVEFORMATEX * mix_format, guint * period,
    DWORD extra_flags, GstWasapi2EndpointClass device_class,
    RbufCtxDesc * desc)
{
  REFERENCE_TIME default_period, min_period;
  DWORD stream_flags =
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
  HRESULT hr;
  REFERENCE_TIME buf_dur = 0;

  stream_flags |= extra_flags;

  if (!gst_wasapi2_is_process_loopback_class (device_class)) {
    hr = client_handle->GetDevicePeriod (&default_period, &min_period);
    if (!gst_wasapi2_result (hr)) {
      GST_WARNING ("Couldn't get device period info");
      return hr;
    }

    GST_INFO ("wasapi2 default period: %" G_GINT64_FORMAT
        ", min period: %" G_GINT64_FORMAT, default_period, min_period);

    /* https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
     * For a shared-mode stream that uses event-driven buffering,
     * the caller must set both hnsPeriodicity and hnsBufferDuration to 0
     *
     * The above MS documentation does not seem to correct. By setting
     * zero hnsBufferDuration, we can use audio engine determined buffer size
     * but it seems to cause glitch depending on device. Calculate buffer size
     * like wasapi plugin does. Note that MS example code uses non-zero
     * buffer duration for event-driven shared-mode case as well.
     */
    if (desc && !desc->low_latency && desc->latency_time > 0 &&
        desc->buffer_time > 0) {
      /* Ensure that the period (latency_time) used is an integral multiple of
       * either the default period or the minimum period */
      guint64 factor = (desc->latency_time * 10) / default_period;
      REFERENCE_TIME period = default_period * MAX (factor, 1);

      buf_dur = desc->buffer_time * 10;
      if (buf_dur < 2 * period)
        buf_dur = 2 * period;
    }

    hr = client_handle->Initialize (AUDCLNT_SHAREMODE_SHARED, stream_flags,
        buf_dur,
        /* This must always be 0 in shared mode */
        0, mix_format, nullptr);
  } else {
    /* XXX: virtual device will not report device period.
     * Use hardcoded period 20ms, same as Microsoft sample code
     * https://github.com/microsoft/windows-classic-samples/tree/main/Samples/ApplicationLoopback
     */
    default_period = (20 * GST_MSECOND) / 100;
    hr = client_handle->Initialize (AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        default_period,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, mix_format, nullptr);
  }

  if (!gst_wasapi2_result (hr)) {
    GST_WARNING ("Couldn't initialize audioclient");
    return hr;
  }

  if (period) {
    *period = gst_util_uint64_scale_round (default_period * 100,
        mix_format->nSamplesPerSec, GST_SECOND);
  }

  return S_OK;
}

static WAVEFORMATEX *
copy_wave_format (WAVEFORMATEX * src)
{
  size_t total_size = sizeof (WAVEFORMATEX) + src->cbSize;
  WAVEFORMATEX *dst = (WAVEFORMATEX *) CoTaskMemAlloc (total_size);
  memcpy (dst, src, total_size);

  return dst;
}

static void
gst_wasapi2_device_manager_create_ctx (IMMDeviceEnumerator * enumerator,
    RbufCtxDesc * desc)
{
  HRESULT hr = S_OK;
  Wasapi2ActivationHandler *activator = nullptr;
  Wasapi2ActivationHandler *dummy_activator = nullptr;
  ComPtr<IMMDevice> device;
  bool is_default = false;

  if (!enumerator)
    return;

  auto endpoint_class = desc->endpoint_class;

  switch (endpoint_class) {
    case GST_WASAPI2_ENDPOINT_CLASS_CAPTURE:
      if (desc->device_id.empty () ||
          is_equal_device_id (desc->device_id.c_str (),
              gst_wasapi2_get_default_device_id (eCapture))) {
        if (gst_wasapi2_can_automatic_stream_routing ()) {
          Wasapi2ActivationHandler::CreateInstance (&activator,
              gst_wasapi2_get_default_device_id_wide (eCapture), nullptr);
          GST_LOG ("Creating default capture device");
        } else {
          GST_LOG ("Creating default capture MMdevice");
          ComPtr<IMMDevice> device;
          hr = enumerator->GetDefaultAudioEndpoint (eCapture,
              eConsole, &device);
        }
      } else {
        auto wstr = g_utf8_to_utf16 (desc->device_id.c_str (),
            -1, nullptr, nullptr, nullptr);
        hr = enumerator->GetDevice ((LPCWSTR) wstr, &device);
        g_free (wstr);
      }
      break;
    case GST_WASAPI2_ENDPOINT_CLASS_RENDER:
    case GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE:
      if (desc->device_id.empty () ||
          is_equal_device_id (desc->device_id.c_str (),
              gst_wasapi2_get_default_device_id (eRender))) {
        if (gst_wasapi2_can_automatic_stream_routing ()) {
          Wasapi2ActivationHandler::CreateInstance (&activator,
              gst_wasapi2_get_default_device_id_wide (eRender), nullptr);
          GST_LOG ("Creating default render device");

          if (endpoint_class == GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE) {
            /* Create another client to send dummy audio data to endpoint */
             Wasapi2ActivationHandler::CreateInstance (&dummy_activator,
                gst_wasapi2_get_default_device_id_wide (eRender), nullptr);
          }

          is_default = true;
        } else {
          GST_LOG ("Creating default render MMdevice");
          hr = enumerator->GetDefaultAudioEndpoint (eRender,
              eConsole, &device);
        }
      } else {
        auto wstr = g_utf8_to_utf16 (desc->device_id.c_str (),
            -1, nullptr, nullptr, nullptr);
        hr = enumerator->GetDevice ((LPCWSTR) wstr, &device);
        g_free (wstr);
      }
      break;
    case GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE:
    case GST_WASAPI2_ENDPOINT_CLASS_EXCLUDE_PROCESS_LOOPBACK_CAPTURE:
    {
      AUDIOCLIENT_ACTIVATION_PARAMS params = { };
      params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
      params.ProcessLoopbackParams.TargetProcessId = desc->pid;
      if (desc->endpoint_class ==
          GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE) {
        params.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
      } else {
        params.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
      }

      GST_LOG ("Creating process loopback capture device");

      Wasapi2ActivationHandler::CreateInstance (&activator,
          VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, &params);
      break;
    }
    default:
      g_assert_not_reached ();
      return;
  }

  /* For debug */
  gst_wasapi2_result (hr);

  auto ctx = std::make_shared<RbufCtx> (desc->device_id);
  if (activator) {
    activator->ActivateAsync ();
    activator->GetClient (&ctx->client, INFINITE);
    activator->Release ();
    if (dummy_activator) {
      dummy_activator->ActivateAsync ();
      dummy_activator->GetClient (&ctx->dummy_client, INFINITE);
      dummy_activator->Release ();

      if (!ctx->dummy_client) {
        GST_WARNING ("Couldn't get dummy audio client");
        return;
      }
    }
  } else {
    if (!device) {
      GST_WARNING ("Couldn't get IMMDevice");
      return;
    }

    hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL,
          nullptr, &ctx->client);
    if (!gst_wasapi2_result (hr)) {
      GST_WARNING ("Couldn't get IAudioClient from IMMDevice");
      return;
    }

    if (endpoint_class == GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE) {
      hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL,
          nullptr, &ctx->dummy_client);
      if (!gst_wasapi2_result (hr)) {
        GST_WARNING ("Couldn't get IAudioClient from IMMDevice");
        return;
      }
    }
  }

  DWORD stream_flags = 0;
  if (!desc->mix_format) {
    ctx->client->GetMixFormat (&ctx->mix_format);
    if (!ctx->mix_format && gst_wasapi2_is_process_loopback_class (endpoint_class)) {
      ctx->mix_format = gst_wasapi2_get_default_mix_format ();
    }

    if (!ctx->mix_format) {
      GST_ERROR ("Couldn't get mix format");
      return;
    }
  } else {
    /* Check format support */
    WAVEFORMATEX *closest = nullptr;
    hr = ctx->client->IsFormatSupported (AUDCLNT_SHAREMODE_SHARED,
        desc->mix_format, &closest);
    if (hr == S_OK) {
      ctx->mix_format = copy_wave_format (desc->mix_format);
      /* format supported */
    } else if (hr == S_FALSE) {
      if (!closest) {
        GST_ERROR ("Couldn't get closest format");
        return;
      }

      auto scaps = gst_static_caps_get (&template_caps);
      GstCaps *old_caps = nullptr;
      GstCaps *new_caps = nullptr;

      gst_wasapi2_util_parse_waveformatex (desc->mix_format,
          scaps, &old_caps, nullptr);
      gst_wasapi2_util_parse_waveformatex (closest,
          scaps, &new_caps, nullptr);
      gst_caps_unref (scaps);

      if (!new_caps || !old_caps) {
        GST_ERROR ("Couldn't get caps from format");
        gst_clear_caps (&new_caps);
        gst_clear_caps (&old_caps);
        CoTaskMemFree (closest);
        return;
      }

      if (!gst_caps_is_equal (new_caps, old_caps)) {
        GST_INFO ("Closest caps is different, old: %" GST_PTR_FORMAT
            ", new : %" GST_PTR_FORMAT, old_caps, new_caps);
        /* Hope OS mixer can convert the format */
        gst_caps_unref (new_caps);
        gst_caps_unref (old_caps);
        CoTaskMemFree (closest);
        ctx->mix_format = copy_wave_format (desc->mix_format);
        stream_flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
      } else {
        gst_caps_unref (new_caps);
        gst_caps_unref (old_caps);

        ctx->mix_format = closest;
      }
    } else {
      GST_ERROR ("Format not supported");
      return;
    }
  }

  auto scaps = gst_static_caps_get (&template_caps);
  gst_wasapi2_util_parse_waveformatex (ctx->mix_format,
      scaps, &ctx->caps, nullptr);
  gst_caps_unref (scaps);

  hr = E_FAIL;
  /* Try IAudioClient3 if low-latency is requested */
  if (desc->low_latency && !gst_wasapi2_is_loopback_class (endpoint_class) &&
      !gst_wasapi2_is_process_loopback_class (endpoint_class)) {
    hr = initialize_audio_client3 (ctx->client.Get (), ctx->mix_format,
        &ctx->period, stream_flags);
  }

  if (FAILED (hr)) {
    DWORD extra_flags = stream_flags;
    if (gst_wasapi2_is_loopback_class (endpoint_class))
      extra_flags = AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = initialize_audio_client (ctx->client.Get (), ctx->mix_format,
        &ctx->period, extra_flags, endpoint_class, desc);
  }

  if (FAILED (hr))
    return;

  if (endpoint_class == GST_WASAPI2_ENDPOINT_CLASS_RENDER) {
    hr = ctx->client->SetEventHandle (ctx->render_event);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't set event handle");
      return;
    }

    hr = ctx->client->GetService (IID_PPV_ARGS (&ctx->render_client));
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't get render client handle");
      return;
    }
  } else {
    hr = ctx->client->SetEventHandle (ctx->capture_event);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't set event handle");
      return;
    }

    hr = ctx->client->GetService (IID_PPV_ARGS (&ctx->capture_client));
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't get capture client handle");
      return;
    }
  }

  hr = ctx->client->GetService (IID_PPV_ARGS (&ctx->stream_volume));
  if (!gst_wasapi2_result (hr))
    GST_WARNING ("Couldn't get ISimpleAudioVolume interface");

  hr = ctx->client->GetBufferSize (&ctx->client_buf_size);
  if (!gst_wasapi2_result (hr)) {
    GST_ERROR ("Couldn't get buffer size");
    return;
  }

  /* Activate silence feed client */
  if (ctx->dummy_client) {
    WAVEFORMATEX *mix_format = nullptr;
    hr = ctx->dummy_client->GetMixFormat (&mix_format);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't get mix format");
      return;
    }

    hr = initialize_audio_client (ctx->dummy_client.Get (), mix_format, nullptr,
        0, GST_WASAPI2_ENDPOINT_CLASS_RENDER, nullptr);
    CoTaskMemFree (mix_format);

    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't initialize dummy client");
      return;
    }

    hr = ctx->dummy_client->SetEventHandle (ctx->render_event);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't set event handle");
      return;
    }

    hr = ctx->dummy_client->GetBufferSize (&ctx->dummy_buf_size);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't get buffer size");
      return;
    }

    hr = ctx->dummy_client->GetService (IID_PPV_ARGS (&ctx->render_client));
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR ("Couldn't get render client");
      return;
    }

    if (device) {
      hr = device->Activate (__uuidof (IAudioEndpointVolume),
          CLSCTX_ALL, nullptr, &ctx->endpoint_volume);
      if (gst_wasapi2_result (hr)) {
        Wasapi2EndpointVolumeCallback::CreateInstance (&ctx->volume_callback,
            ctx);

        hr = ctx->endpoint_volume->RegisterControlChangeNotify (
            ctx->volume_callback.Get ());
        if (!gst_wasapi2_result (hr)) {
          ctx->volume_callback = nullptr;
        } else {
          BOOL muted = FALSE;
          hr = ctx->endpoint_volume->GetMute (&muted);
          if (gst_wasapi2_result (hr))
            ctx->endpoint_muted = muted;
        }
      }
    }
  }

  /* Preroll data with silent data */
  if (ctx->render_client && !ctx->dummy_client) {
    UINT32 padding = 0;
    auto hr = ctx->client->GetCurrentPadding (&padding);
    if (SUCCEEDED (hr) && padding < ctx->client_buf_size) {
      auto can_write = ctx->client_buf_size - padding;
      BYTE *data;
      hr = ctx->render_client->GetBuffer (can_write, &data);
      if (SUCCEEDED (hr)) {
        GST_DEBUG ("Prefill %u frames", can_write);
        ctx->render_client->ReleaseBuffer (can_write,
            AUDCLNT_BUFFERFLAGS_SILENT);
      }
    }
  }

  ctx->is_default = is_default;
  ctx->endpoint_class = endpoint_class;

  desc->ctx = ctx;
}

struct Wasapi2DeviceManager
{
  Wasapi2DeviceManager (const Wasapi2DeviceManager &) = delete;
  Wasapi2DeviceManager& operator= (const Wasapi2DeviceManager &) = delete;

  static Wasapi2DeviceManager * GetInstance()
  {
    static Wasapi2DeviceManager *inst = nullptr;
    GST_WASAPI2_CALL_ONCE_BEGIN {
      inst = new Wasapi2DeviceManager ();
    } GST_WASAPI2_CALL_ONCE_END;

    return inst;
  }

  Wasapi2DeviceManager ()
  {
    shutdown_handle = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    interrupt_handle = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    com_thread = g_thread_new ("Wasapi2DeviceManager",
        (GThreadFunc) device_manager_com_thread, this);
  }

  ~Wasapi2DeviceManager ()
  {
    CloseHandle (shutdown_handle);
    CloseHandle (interrupt_handle);
  }

  RbufCtxPtr
  CreateCtx (const std::string & device_id,
      GstWasapi2EndpointClass endpoint_class, guint pid, gint64 buffer_time,
      gint64 latency_time, gboolean low_latency, WAVEFORMATEX * mix_format)
  {
    auto desc = std::make_shared<RbufCtxDesc> ();
    desc->device_id = device_id;
    desc->endpoint_class = endpoint_class;
    desc->pid = pid;
    desc->buffer_time = buffer_time;
    desc->latency_time = latency_time;
    desc->low_latency = low_latency;
    if (mix_format)
      desc->mix_format = copy_wave_format (mix_format);

    {
      std::lock_guard <std::mutex> lk (lock);
      queue.push (desc);
    }
    SetEvent (interrupt_handle);

    WaitForSingleObject (desc->event_handle, INFINITE);

    return desc->ctx;
  }

  void
  CreateCtxAsync (GstWasapi2Rbuf * rbuf, const std::string & device_id,
      GstWasapi2EndpointClass endpoint_class, guint pid, gint64 buffer_time,
      gint64 latency_time, gboolean low_latency, WAVEFORMATEX * mix_format)
  {
    auto desc = std::make_shared<RbufCtxDesc> ();
    desc->rbuf = (GstWasapi2Rbuf *) gst_object_ref (rbuf);
    desc->device_id = device_id;
    desc->endpoint_class = endpoint_class;
    desc->pid = pid;
    desc->buffer_time = buffer_time;
    desc->latency_time = latency_time;
    desc->low_latency = low_latency;
    if (mix_format)
      desc->mix_format = copy_wave_format (mix_format);

    {
      std::lock_guard <std::mutex> lk (lock);
      queue.push (desc);
    }
    SetEvent (interrupt_handle);
  }

  std::mutex lock;
  std::queue<std::shared_ptr<RbufCtxDesc>> queue;
  HANDLE shutdown_handle;
  HANDLE interrupt_handle;
  GThread *com_thread;
};

static gpointer
device_manager_com_thread (gpointer manager)
{
  auto self = (Wasapi2DeviceManager *) manager;
  CoInitializeEx (nullptr, COINIT_MULTITHREADED);

  ComPtr<IMMDeviceEnumerator> enumerator;
  CoCreateInstance (__uuidof (MMDeviceEnumerator),
      nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator));

  HANDLE waitables[] = { self->shutdown_handle, self->interrupt_handle };
  bool running = true;
  while (running) {
    auto wait_ret = WaitForMultipleObjects (G_N_ELEMENTS (waitables),
        waitables, FALSE, INFINITE);

    switch (wait_ret) {
      case WAIT_OBJECT_0:
        running = false;
        break;
      case WAIT_OBJECT_0 + 1:
      {
        std::unique_lock <std::mutex> lk (self->lock);
        while (!self->queue.empty ()) {
          auto desc = self->queue.front ();
          self->queue.pop ();
          lk.unlock ();
          GST_LOG ("Creating new context");
          gst_wasapi2_device_manager_create_ctx (enumerator.Get (), desc.get ());

          if (desc->mix_format)
            CoTaskMemFree (desc->mix_format);

          SetEvent (desc->event_handle);

          if (desc->rbuf) {
            auto cmd = std::make_shared < CommandUpdateDevice > (desc->device_id);
            cmd->ctx = std::move (desc->ctx);

            gst_wasapi2_rbuf_push_command (desc->rbuf, cmd);
            WaitForSingleObject (cmd->event_handle, INFINITE);

            gst_object_unref (desc->rbuf);
          }

          lk.lock ();
        }
        break;
      }
      default:
        GST_ERROR ("Unexpected wait return 0x%x", (guint) wait_ret);
        running = false;
        break;
    }
  }

  enumerator = nullptr;

  CoUninitialize ();

  return nullptr;
}

struct GstWasapi2RbufPrivate
{
  GstWasapi2RbufPrivate ()
  {
    command_handle = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    g_weak_ref_init (&parent, nullptr);

    QueryPerformanceFrequency (&qpc_freq);
  }

  ~GstWasapi2RbufPrivate ()
  {
    CloseHandle (command_handle);
    gst_clear_caps (&caps);
    g_weak_ref_set (&parent, nullptr);
  }

  std::string device_id;
  GstWasapi2EndpointClass endpoint_class;
  guint pid;
  gboolean low_latency = FALSE;

  std::shared_ptr<RbufCtx> ctx;
  std::atomic<bool> monitor_device_mute;
  GThread *thread = nullptr;
  HANDLE command_handle;
  GstCaps *caps = nullptr;

  std::mutex lock;
  std::condition_variable cond;
  WAVEFORMATEX *mix_format = nullptr;
  std::queue<std::shared_ptr<CommandData>> cmd_queue;
  bool opened = false;
  bool running = false;

  std::atomic<float> volume = { 1.0 };
  std::atomic<bool> mute = { false };
  std::atomic<bool> allow_dummy = { false };

  bool is_first = true;
  gint segoffset = 0;
  guint64 write_frame_offset = 0;
  guint64 expected_position = 0;

  HANDLE fallback_timer = nullptr;
  bool fallback_timer_armed = false;
  UINT64 fallback_frames_processed = 0;
  bool configured_allow_dummy = false;

  LARGE_INTEGER qpc_freq;
  LARGE_INTEGER fallback_qpc_base;

  HANDLE monitor_timer = nullptr;
  bool monitor_timer_armed = false;

  GWeakRef parent;
  GstWasapi2RbufCallback invalidated_cb;
};
/* *INDENT-ON* */

struct _GstWasapi2Rbuf
{
  GstAudioRingBuffer parent;

  GstWasapi2RbufPrivate *priv;
};

static void gst_wasapi2_rbuf_finalize (GObject * object);

static gboolean gst_wasapi2_rbuf_open_device (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_close_device (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_acquire (GstAudioRingBuffer * buf,
    GstAudioRingBufferSpec * spec);
static gboolean gst_wasapi2_rbuf_release (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_start (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_resume (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_pause (GstAudioRingBuffer * buf);
static gboolean gst_wasapi2_rbuf_stop (GstAudioRingBuffer * buf);
static guint gst_wasapi2_rbuf_delay (GstAudioRingBuffer * buf);
static gpointer gst_wasapi2_rbuf_loop_thread (GstWasapi2Rbuf * self);

#define gst_wasapi2_rbuf_parent_class parent_class
G_DEFINE_TYPE (GstWasapi2Rbuf, gst_wasapi2_rbuf, GST_TYPE_AUDIO_RING_BUFFER);

static void
gst_wasapi2_rbuf_class_init (GstWasapi2RbufClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioRingBufferClass *ring_buffer_class =
      GST_AUDIO_RING_BUFFER_CLASS (klass);

  gobject_class->finalize = gst_wasapi2_rbuf_finalize;

  ring_buffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_open_device);
  ring_buffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_close_device);
  ring_buffer_class->acquire = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_acquire);
  ring_buffer_class->release = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_release);
  ring_buffer_class->start = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_start);
  ring_buffer_class->resume = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_resume);
  ring_buffer_class->pause = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_pause);
  ring_buffer_class->stop = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_stop);
  ring_buffer_class->delay = GST_DEBUG_FUNCPTR (gst_wasapi2_rbuf_delay);

  GST_DEBUG_CATEGORY_INIT (gst_wasapi2_rbuf_debug,
      "wasapi2ringbuffer", 0, "wasapi2ringbuffer");
}

static void
gst_wasapi2_rbuf_init (GstWasapi2Rbuf * self)
{
  self->priv = new GstWasapi2RbufPrivate ();
}

static void
gst_wasapi2_rbuf_push_command (GstWasapi2Rbuf * self,
    std::shared_ptr < CommandData > cmd)
{
  auto priv = self->priv;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    priv->cmd_queue.push (cmd);
  }
  SetEvent (priv->command_handle);
}

static void
gst_wasapi2_rbuf_finalize (GObject * object)
{
  auto self = GST_WASAPI2_RBUF (object);
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Finalize");

  auto cmd = std::make_shared < CommandData > (CommandType::Shutdown);
  gst_wasapi2_rbuf_push_command (self, cmd);

  g_thread_join (priv->thread);

  delete priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_rbuf_post_open_error (GstWasapi2Rbuf * self,
    const gchar * device_id)
{
  auto priv = self->priv;
  auto parent = g_weak_ref_get (&priv->parent);

  if (!parent)
    return;

  priv->invalidated_cb (parent);

  if (priv->configured_allow_dummy) {
    GST_ELEMENT_WARNING (parent, RESOURCE, OPEN_READ_WRITE,
        (nullptr), ("Failed to open device %s", GST_STR_NULL (device_id)));
  } else {
    GST_ELEMENT_ERROR (parent, RESOURCE, OPEN_READ_WRITE,
        (nullptr), ("Failed to open device %s", GST_STR_NULL (device_id)));
  }

  g_object_unref (parent);
}

static void
gst_wasapi2_rbuf_post_io_error (GstWasapi2Rbuf * self, HRESULT hr,
    gboolean is_write)
{
  auto priv = self->priv;
  auto parent = g_weak_ref_get (&priv->parent);

  auto error_msg = gst_wasapi2_util_get_error_message (hr);
  GST_ERROR_OBJECT (self, "Posting I/O error %s (hr: 0x%x)", error_msg,
      (guint) hr);

  priv->invalidated_cb (parent);

  if (is_write) {
    if (priv->configured_allow_dummy) {
      GST_ELEMENT_WARNING (parent, RESOURCE, WRITE,
          ("Failed to write to device"), ("%s, hr: 0x%x", error_msg,
              (guint) hr));
    } else {
      GST_ELEMENT_ERROR (parent, RESOURCE, WRITE,
          ("Failed to write to device"), ("%s, hr: 0x%x", error_msg,
              (guint) hr));
    }
  } else {
    if (priv->configured_allow_dummy) {
      GST_ELEMENT_WARNING (parent, RESOURCE, READ,
          ("Failed to read from device"), ("%s hr: 0x%x", error_msg,
              (guint) hr));
    } else {
      GST_ELEMENT_ERROR (parent, RESOURCE, READ,
          ("Failed to read from device"), ("%s hr: 0x%x", error_msg,
              (guint) hr));
    }
  }

  g_free (error_msg);
  g_object_unref (parent);
}

static RbufCtxPtr
gst_wasapi2_rbuf_create_ctx (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;
  auto parent = g_weak_ref_get (&priv->parent);

  if (!parent) {
    GST_ERROR_OBJECT (self, "No parent");
    return nullptr;
  }

  gint64 buffer_time = 0;
  gint64 latency_time = 0;
  g_object_get (parent, "buffer-time", &buffer_time, "latency-time",
      &latency_time, nullptr);
  g_object_unref (parent);

  auto inst = Wasapi2DeviceManager::GetInstance ();

  return inst->CreateCtx (priv->device_id, priv->endpoint_class,
      priv->pid, buffer_time, latency_time, priv->low_latency,
      priv->mix_format);
}

static void
gst_wasapi2_rbuf_create_ctx_async (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;
  auto parent = g_weak_ref_get (&priv->parent);

  if (!parent) {
    GST_ERROR_OBJECT (self, "No parent");
    return;
  }

  gint64 buffer_time = 0;
  gint64 latency_time = 0;
  g_object_get (parent, "buffer-time", &buffer_time, "latency-time",
      &latency_time, nullptr);
  g_object_unref (parent);

  auto inst = Wasapi2DeviceManager::GetInstance ();

  inst->CreateCtxAsync (self, priv->device_id, priv->endpoint_class,
      priv->pid, buffer_time, latency_time, priv->low_latency,
      priv->mix_format);
}

static gboolean
gst_wasapi2_rbuf_open_device (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Open");

  auto cmd = std::make_shared < CommandData > (CommandType::Open);
  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return gst_wasapi2_result (cmd->hr);
}

static gboolean
gst_wasapi2_rbuf_close_device (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Close device");

  auto cmd = std::make_shared < CommandData > (CommandType::Close);

  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return TRUE;
}

static gboolean
gst_wasapi2_rbuf_acquire (GstAudioRingBuffer * buf,
    GstAudioRingBufferSpec * spec)
{
  auto self = GST_WASAPI2_RBUF (buf);

  auto cmd = std::make_shared < CommandAcquire > (spec);

  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return gst_wasapi2_result (cmd->hr);
}

static gboolean
gst_wasapi2_rbuf_release (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Release");

  auto cmd = std::make_shared < CommandData > (CommandType::Release);

  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return TRUE;
}

static gboolean
gst_wasapi2_rbuf_start_internal (GstWasapi2Rbuf * self)
{
  auto cmd = std::make_shared < CommandData > (CommandType::Start);
  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return gst_wasapi2_result (cmd->hr);
}

static gboolean
gst_wasapi2_rbuf_start (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Start");

  return gst_wasapi2_rbuf_start_internal (self);
}

static gboolean
gst_wasapi2_rbuf_resume (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Resume");

  return gst_wasapi2_rbuf_start_internal (self);
}

static gboolean
gst_wasapi2_rbuf_stop_internal (GstWasapi2Rbuf * self)
{
  auto cmd = std::make_shared < CommandData > (CommandType::Stop);
  gst_wasapi2_rbuf_push_command (self, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);

  return TRUE;
}

static gboolean
gst_wasapi2_rbuf_stop (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Stop");

  return gst_wasapi2_rbuf_stop_internal (self);
}

static gboolean
gst_wasapi2_rbuf_pause (GstAudioRingBuffer * buf)
{
  auto self = GST_WASAPI2_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Pause");

  return gst_wasapi2_rbuf_stop_internal (self);
}

static HRESULT
gst_wasapi2_rbuf_process_read (GstWasapi2Rbuf * self)
{
  auto rb = GST_AUDIO_RING_BUFFER_CAST (self);
  auto priv = self->priv;
  BYTE *data = nullptr;
  UINT32 to_read = 0;
  guint32 to_read_bytes;
  DWORD flags = 0;
  HRESULT hr;
  guint64 position = 0;
  GstAudioInfo *info = &rb->spec.info;
  guint gap_size = 0;
  guint offset = 0;
  gint segment;
  guint8 *readptr;
  gint len;
  bool is_device_muted;
  UINT64 qpc_pos = 0;
  GstClockTime qpc_time;

  if (!priv->ctx || !priv->ctx->capture_client) {
    GST_ERROR_OBJECT (self, "IAudioCaptureClient is not available");
    return E_FAIL;
  }

  auto client = priv->ctx->capture_client;

  hr = client->GetBuffer (&data, &to_read, &flags, &position, &qpc_pos);
  /* 100 ns unit */
  qpc_time = qpc_pos * 100;

  GST_LOG_OBJECT (self, "Reading %d frames offset at %" G_GUINT64_FORMAT
      ", expected position %" G_GUINT64_FORMAT ", qpc-time %"
      GST_TIME_FORMAT "(%" G_GUINT64_FORMAT "), flags 0x%x", to_read, position,
      priv->expected_position, GST_TIME_ARGS (qpc_time), qpc_pos,
      (guint) flags);

  if (hr == AUDCLNT_S_BUFFER_EMPTY || to_read == 0) {
    GST_LOG_OBJECT (self, "Empty buffer");
    return S_OK;
  }

  is_device_muted =
      priv->monitor_device_mute.load (std::memory_order_acquire) &&
      priv->ctx->IsEndpointMuted ();

  to_read_bytes = to_read * GST_AUDIO_INFO_BPF (info);

  /* XXX: position might not be increased in case of process loopback  */
  if (!gst_wasapi2_is_process_loopback_class (priv->ctx->endpoint_class)) {
    if (priv->is_first) {
      priv->expected_position = position + to_read;
      priv->is_first = false;
    } else {
      if (position > priv->expected_position) {
        guint gap_frames;

        gap_frames = (guint) (position - priv->expected_position);
        GST_WARNING_OBJECT (self, "Found %u frames gap", gap_frames);
        gap_size = gap_frames * GST_AUDIO_INFO_BPF (info);
      }

      priv->expected_position = position + to_read;
    }
  } else if (priv->mute) {
    /* volume clinet might not be available in case of process loopback */
    flags |= AUDCLNT_BUFFERFLAGS_SILENT;
  }

  /* Fill gap data if any */
  while (gap_size > 0) {
    if (!gst_audio_ring_buffer_prepare_read (rb, &segment, &readptr, &len)) {
      GST_INFO_OBJECT (self, "No segment available");
      hr = client->ReleaseBuffer (to_read);
      gst_wasapi2_result (hr);
      return S_OK;
    }

    g_assert (priv->segoffset >= 0);

    len -= priv->segoffset;
    if (len > (gint) gap_size)
      len = gap_size;

    gst_audio_format_info_fill_silence (info->finfo,
        readptr + priv->segoffset, len);

    priv->segoffset += len;
    gap_size -= len;

    if (priv->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_advance (rb, 1);
      priv->segoffset = 0;
    }
  }

  while (to_read_bytes) {
    if (!gst_audio_ring_buffer_prepare_read (rb, &segment, &readptr, &len)) {
      GST_INFO_OBJECT (self, "No segment available");
      hr = client->ReleaseBuffer (to_read);
      gst_wasapi2_result (hr);
      return S_OK;
    }

    len -= priv->segoffset;
    if (len > (gint) to_read_bytes)
      len = to_read_bytes;

    if (((flags & AUDCLNT_BUFFERFLAGS_SILENT) == AUDCLNT_BUFFERFLAGS_SILENT) ||
        is_device_muted) {
      gst_audio_format_info_fill_silence (info->finfo,
          readptr + priv->segoffset, len);
    } else {
      memcpy (readptr + priv->segoffset, data + offset, len);
    }

    priv->segoffset += len;
    offset += len;
    to_read_bytes -= len;

    if (priv->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_advance (rb, 1);
      priv->segoffset = 0;
    }
  }

  hr = client->ReleaseBuffer (to_read);
  gst_wasapi2_result (hr);

  return S_OK;
}

static HRESULT
gst_wasapi2_rbuf_process_write (GstWasapi2Rbuf * self)
{
  auto rb = GST_AUDIO_RING_BUFFER_CAST (self);
  auto priv = self->priv;
  HRESULT hr;
  guint32 padding_frames = 0;
  guint32 can_write;
  guint32 can_write_bytes;
  gint segment;
  guint8 *readptr;
  gint len;
  BYTE *data = nullptr;

  if (!priv->ctx || !priv->ctx->render_client) {
    GST_ERROR_OBJECT (self, "IAudioRenderClient is not available");
    return E_FAIL;
  }

  auto client = priv->ctx->client;
  auto render_client = priv->ctx->render_client;

  hr = client->GetCurrentPadding (&padding_frames);
  if (!gst_wasapi2_result (hr))
    return hr;

  if (padding_frames >= priv->ctx->client_buf_size) {
    GST_INFO_OBJECT (self,
        "Padding size %d is larger than or equal to buffer size %d",
        padding_frames, priv->ctx->client_buf_size);
    return S_OK;
  }

  can_write = priv->ctx->client_buf_size - padding_frames;
  can_write_bytes = can_write * GST_AUDIO_INFO_BPF (&rb->spec.info);

  GST_LOG_OBJECT (self, "Writing %d frames offset at %" G_GUINT64_FORMAT,
      can_write, priv->write_frame_offset);
  priv->write_frame_offset += can_write;

  while (can_write_bytes > 0) {
    if (!gst_audio_ring_buffer_prepare_read (rb, &segment, &readptr, &len)) {
      GST_INFO_OBJECT (self, "No segment available, fill silence");

      /* This would be case where in the middle of PAUSED state change.
       * Just fill silent buffer to avoid immediate I/O callback after
       * we return here */
      hr = render_client->GetBuffer (can_write, &data);
      if (!gst_wasapi2_result (hr))
        return hr;

      hr = render_client->ReleaseBuffer (can_write, AUDCLNT_BUFFERFLAGS_SILENT);
      /* for debugging */
      gst_wasapi2_result (hr);
      return hr;
    }

    len -= priv->segoffset;

    if (len > (gint) can_write_bytes)
      len = can_write_bytes;

    can_write = len / GST_AUDIO_INFO_BPF (&rb->spec.info);
    if (can_write == 0)
      break;

    hr = render_client->GetBuffer (can_write, &data);
    if (!gst_wasapi2_result (hr))
      return hr;

    memcpy (data, readptr + priv->segoffset, len);
    hr = render_client->ReleaseBuffer (can_write, 0);

    priv->segoffset += len;
    can_write_bytes -= len;

    if (priv->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_clear (rb, segment);
      gst_audio_ring_buffer_advance (rb, 1);
      priv->segoffset = 0;
    }

    if (!gst_wasapi2_result (hr)) {
      GST_WARNING_OBJECT (self, "Failed to release buffer");
      break;
    }
  }

  return S_OK;
}

static HRESULT
fill_loopback_silence (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;
  HRESULT hr;
  guint32 padding_frames = 0;
  guint32 can_write;
  BYTE *data = nullptr;

  if (!priv->ctx || !priv->ctx->dummy_client || !priv->ctx->render_client) {
    GST_ERROR_OBJECT (self, "IAudioRenderClient is not available");
    return E_FAIL;
  }

  auto client = priv->ctx->dummy_client;
  auto render_client = priv->ctx->render_client;

  hr = client->GetCurrentPadding (&padding_frames);
  if (!gst_wasapi2_result (hr))
    return hr;

  if (padding_frames >= priv->ctx->dummy_buf_size) {
    GST_INFO_OBJECT (self,
        "Padding size %d is larger than or equal to buffer size %d",
        padding_frames, priv->ctx->dummy_buf_size);
    return S_OK;
  }

  can_write = priv->ctx->dummy_buf_size - padding_frames;

  GST_TRACE_OBJECT (self, "Writing %d silent frames", can_write);

  hr = render_client->GetBuffer (can_write, &data);
  if (!gst_wasapi2_result (hr))
    return hr;

  hr = render_client->ReleaseBuffer (can_write, AUDCLNT_BUFFERFLAGS_SILENT);
  return gst_wasapi2_result (hr);
}

static void
gst_wasapi2_rbuf_process_acquire (GstWasapi2Rbuf * self,
    GstAudioRingBufferSpec * spec)
{
  auto buf = GST_AUDIO_RING_BUFFER (self);
  auto priv = self->priv;

  guint client_buf_size = 0;
  gint period_frames = 480;

  if (priv->ctx) {
    client_buf_size = priv->ctx->client_buf_size;
    period_frames = priv->ctx->period;
  }

  gint bpf = GST_AUDIO_INFO_BPF (&buf->spec.info);
  gint rate = GST_AUDIO_INFO_RATE (&buf->spec.info);
  gint target_frames = rate / 2;        /* 500ms duration */

  gint segtotal = (target_frames + period_frames - 1) / period_frames;
  spec->segsize = period_frames * bpf;
  spec->segtotal = MAX (segtotal, 2);

  /* Since we allocates large buffer (large segtotal) for device switching,
   * update seglatency to reasonable value */
  spec->seglatency = 2;

  GST_INFO_OBJECT (self,
      "Buffer size: %d frames, period: %d frames, segsize: %d bytes, "
      "segtotal: %d", client_buf_size, period_frames,
      spec->segsize, spec->segtotal);

  GstAudioChannelPosition *position = nullptr;
  gst_wasapi2_util_waveformatex_to_channel_mask (priv->mix_format, &position);
  if (position)
    gst_audio_ring_buffer_set_channel_positions (buf, position);
  g_free (position);

  buf->size = spec->segtotal * spec->segsize;
  buf->memory = (guint8 *) g_malloc (buf->size);
  gst_audio_format_info_fill_silence (buf->spec.info.finfo,
      buf->memory, buf->size);
}

static HRESULT
gst_wasapi2_rbuf_process_release (GstWasapi2Rbuf * self)
{
  auto buf = GST_AUDIO_RING_BUFFER (self);

  g_clear_pointer (&buf->memory, g_free);

  return S_OK;
}

static void
gst_wasapi2_rbuf_start_fallback_timer (GstWasapi2Rbuf * self)
{
  auto rb = GST_AUDIO_RING_BUFFER_CAST (self);
  auto priv = self->priv;

  if (priv->fallback_timer_armed || !priv->configured_allow_dummy)
    return;

  GST_DEBUG_OBJECT (self, "Start fallback timer");

  auto period_frames = rb->spec.segsize / GST_AUDIO_INFO_BPF (&rb->spec.info);
  UINT64 period_100ns = (10000000ULL * period_frames) /
      GST_AUDIO_INFO_RATE (&rb->spec.info);

  LARGE_INTEGER due_time;
  due_time.QuadPart = -static_cast < LONGLONG > (period_100ns);

  SetWaitableTimer (priv->fallback_timer,
      &due_time,
      static_cast < LONG > (period_100ns / 10000), nullptr, nullptr, FALSE);

  QueryPerformanceCounter (&priv->fallback_qpc_base);
  priv->fallback_frames_processed = 0;
  priv->fallback_timer_armed = true;
}

static void
gst_wasapi2_rbuf_stop_fallback_timer (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;

  if (!priv->fallback_timer_armed)
    return;

  GST_DEBUG_OBJECT (self, "Stop fallback timer");

  CancelWaitableTimer (priv->fallback_timer);
  priv->fallback_timer_armed = false;
}

static void
gst_wasapi2_rbuf_start_monitor_timer (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;

  if (priv->monitor_timer_armed)
    return;

  GST_DEBUG_OBJECT (self, "Start monitor timer");

  /* Run 15ms timer to monitor device status */
  LARGE_INTEGER due_time;
  due_time.QuadPart = -1500000LL;

  SetWaitableTimer (priv->monitor_timer,
      &due_time, 15, nullptr, nullptr, FALSE);

  priv->monitor_timer_armed = true;
}

static void
gst_wasapi2_rbuf_stop_monitor_timer (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;

  if (!priv->monitor_timer_armed)
    return;

  GST_DEBUG_OBJECT (self, "Stop monitor timer");

  CancelWaitableTimer (priv->monitor_timer);
  priv->monitor_timer_armed = false;
}

static HRESULT
gst_wasapi2_rbuf_process_start (GstWasapi2Rbuf * self, gboolean reset_offset)
{
  auto priv = self->priv;

  if (!priv->ctx && !priv->configured_allow_dummy) {
    GST_WARNING_OBJECT (self, "No context to start");
    return E_FAIL;
  }

  if (priv->running)
    return S_OK;

  priv->is_first = true;
  if (reset_offset)
    priv->segoffset = 0;
  priv->write_frame_offset = 0;
  priv->expected_position = 0;

  if (priv->ctx) {
    auto hr = priv->ctx->Start ();

    if (!gst_wasapi2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't start device");
      gst_wasapi2_rbuf_post_open_error (self, priv->ctx->device_id.c_str ());
      if (!priv->configured_allow_dummy)
        return hr;

      gst_wasapi2_rbuf_start_fallback_timer (self);
    }
  } else {
    gst_wasapi2_rbuf_start_fallback_timer (self);
  }

  gst_wasapi2_rbuf_start_monitor_timer (self);
  priv->running = true;

  return S_OK;
}

static inline void
ClearMixFormat (WAVEFORMATEX ** mix_format)
{
  if (*mix_format) {
    CoTaskMemFree (*mix_format);
    *mix_format = nullptr;
  }
}

static HRESULT
gst_wasapi2_rbuf_process_stop (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;
  HRESULT hr = S_OK;

  if (priv->ctx)
    hr = priv->ctx->Stop ();

  priv->running = false;
  priv->is_first = true;
  priv->segoffset = 0;
  priv->write_frame_offset = 0;
  priv->expected_position = 0;

  gst_wasapi2_rbuf_stop_fallback_timer (self);
  gst_wasapi2_rbuf_stop_monitor_timer (self);

  return hr;
}

static void
gst_wasapi2_rbuf_discard_frames (GstWasapi2Rbuf * self, guint frames)
{
  auto rb = GST_AUDIO_RING_BUFFER_CAST (self);
  auto priv = self->priv;
  guint len = frames * GST_AUDIO_INFO_BPF (&rb->spec.info);

  while (len > 0) {
    gint seg;
    guint8 *ptr;
    gint avail;

    if (!gst_audio_ring_buffer_prepare_read (rb, &seg, &ptr, &avail))
      return;

    avail -= priv->segoffset;
    gint to_consume = MIN ((gint) len, avail);

    priv->segoffset += to_consume;
    len -= to_consume;

    if (priv->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_clear (rb, seg);
      gst_audio_ring_buffer_advance (rb, 1);
      priv->segoffset = 0;
    }
  }
}

static void
gst_wasapi2_rbuf_insert_silence_frames (GstWasapi2Rbuf * self, guint frames)
{
  auto rb = GST_AUDIO_RING_BUFFER_CAST (self);
  auto priv = self->priv;
  guint bpf = GST_AUDIO_INFO_BPF (&rb->spec.info);
  guint len = frames * bpf;

  while (len > 0) {
    gint segment;
    guint8 *writeptr;
    gint avail;

    if (!gst_audio_ring_buffer_prepare_read (rb, &segment, &writeptr, &avail))
      break;

    avail -= priv->segoffset;
    gint to_write = MIN ((gint) len, avail);

    gst_audio_format_info_fill_silence (rb->spec.info.finfo,
        writeptr + priv->segoffset, to_write);

    priv->segoffset += to_write;
    len -= to_write;

    if (priv->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_advance (rb, 1);
      priv->segoffset = 0;
    }
  }
}

static gpointer
gst_wasapi2_rbuf_loop_thread (GstWasapi2Rbuf * self)
{
  auto priv = self->priv;
  DWORD task_idx = 0;
  auto task_handle = AvSetMmThreadCharacteristicsW (L"Pro Audio", &task_idx);

  CoInitializeEx (nullptr, COINIT_MULTITHREADED);

  bool loop_running = true;

  /* Dummy event handles for IO events can have higher priority than user commands */
  auto dummy_render = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  auto dummy_capture = CreateEvent (nullptr, FALSE, FALSE, nullptr);

  priv->fallback_timer = CreateWaitableTimerExW (nullptr,
      nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

  if (!priv->fallback_timer) {
    GST_WARNING_OBJECT (self,
        "High-resolution timer not available, using default");
    priv->fallback_timer = CreateWaitableTimer (nullptr, FALSE, nullptr);
  }

  /* Another timer to detect device-removed state, since I/O event
   * would not be singalled on device-removed state */
  priv->monitor_timer = CreateWaitableTimer (nullptr, FALSE, nullptr);

  HANDLE waitables[] = { dummy_render, dummy_capture,
    priv->fallback_timer, priv->monitor_timer, priv->command_handle
  };

  GST_DEBUG_OBJECT (self, "Entering loop");

  while (loop_running) {
    auto wait_ret = WaitForMultipleObjects (G_N_ELEMENTS (waitables),
        waitables, FALSE, INFINITE);

    switch (wait_ret) {
      case WAIT_OBJECT_0:
        if (priv->running) {
          HRESULT hr = S_OK;
          if (priv->ctx->endpoint_class ==
              GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE) {
            hr = fill_loopback_silence (self);
            if (SUCCEEDED (hr))
              hr = gst_wasapi2_rbuf_process_read (self);
          } else {
            hr = gst_wasapi2_rbuf_process_write (self);
          }

          if (FAILED (hr)) {
            gst_wasapi2_rbuf_post_io_error (self, hr, TRUE);
            gst_wasapi2_rbuf_start_fallback_timer (self);
          }
        }
        break;
      case WAIT_OBJECT_0 + 1:
        if (priv->running) {
          auto hr = gst_wasapi2_rbuf_process_read (self);
          if ((hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED ||
                  hr == AUDCLNT_E_DEVICE_INVALIDATED) && priv->ctx->is_default
              && !gst_wasapi2_is_loopback_class (priv->ctx->endpoint_class)) {
            GST_WARNING_OBJECT (self,
                "Device was unplugged but client can support automatic routing");
            hr = S_OK;
          }

          if (FAILED (hr)) {
            gst_wasapi2_rbuf_post_io_error (self, hr, FALSE);
            gst_wasapi2_rbuf_start_fallback_timer (self);
          }
        }
        break;
      case WAIT_OBJECT_0 + 2:
      {
        if (!priv->running || !priv->fallback_timer_armed)
          break;

        LARGE_INTEGER qpc_now;
        QueryPerformanceCounter (&qpc_now);

        LONGLONG elapsed = qpc_now.QuadPart - priv->fallback_qpc_base.QuadPart;
        UINT64 elapsed_100ns = elapsed * 10000000ULL / priv->qpc_freq.QuadPart;
        UINT32 rate = priv->mix_format->nSamplesPerSec;
        UINT64 expected_frames = (elapsed_100ns * rate) / 10000000ULL;
        UINT64 delta = expected_frames - priv->fallback_frames_processed;

        if (delta > 0) {
          GST_TRACE_OBJECT (self,
              "procssing fallback %u frames", (guint) delta);

          if (priv->endpoint_class == GST_WASAPI2_ENDPOINT_CLASS_RENDER)
            gst_wasapi2_rbuf_discard_frames (self, (guint) delta);
          else
            gst_wasapi2_rbuf_insert_silence_frames (self, (guint) delta);

          priv->fallback_frames_processed += delta;
        }

        break;
      }
      case WAIT_OBJECT_0 + 3:
      {
        if (!priv->running || !priv->ctx || !priv->monitor_timer_armed)
          break;

        UINT32 dummy;
        auto hr = priv->ctx->client->GetCurrentPadding (&dummy);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED && !priv->ctx->error_posted) {
          priv->ctx->error_posted = true;
          gst_wasapi2_rbuf_post_io_error (self, AUDCLNT_E_DEVICE_INVALIDATED,
              priv->endpoint_class == GST_WASAPI2_ENDPOINT_CLASS_RENDER);
          gst_wasapi2_rbuf_start_fallback_timer (self);
        }

        break;
      }
      case WAIT_OBJECT_0 + 4:
        /* Wakeup event for event processing */
        break;
      default:
        GST_WARNING_OBJECT (self,
            "Unexpected wait return 0x%x", (guint) wait_ret);
        loop_running = false;
        break;
    }

    /* Process events */
    {
      std::unique_lock < std::mutex > lk (priv->lock);
      while (!priv->cmd_queue.empty ()) {
        auto cmd = priv->cmd_queue.front ();
        priv->cmd_queue.pop ();
        lk.unlock ();

        auto cmd_name = command_type_to_string (cmd->type);
        GST_DEBUG_OBJECT (self, "Got command %s", cmd_name);
        switch (cmd->type) {
          case CommandType::Shutdown:
            loop_running = false;
            cmd->hr = S_OK;
            SetEvent (cmd->event_handle);
            break;
          case CommandType::SetDevice:
          {
            auto scmd = std::dynamic_pointer_cast < CommandSetDevice > (cmd);
            priv->device_id = scmd->device_id;
            priv->endpoint_class = scmd->endpoint_class;
            priv->pid = scmd->pid;
            priv->low_latency = scmd->low_latency;

            if (priv->opened) {
              GST_DEBUG_OBJECT (self,
                  "Have opened device, creating context asynchronously");
              gst_wasapi2_rbuf_create_ctx_async (self);
            }

            cmd->hr = S_OK;
            SetEvent (cmd->event_handle);
            break;
          }
          case CommandType::UpdateDevice:
          {
            auto ucmd = std::dynamic_pointer_cast < CommandUpdateDevice > (cmd);
            if (priv->opened) {
              GST_DEBUG_OBJECT (self, "Updating device");

              gst_wasapi2_rbuf_stop_fallback_timer (self);

              priv->ctx = ucmd->ctx;

              if (priv->ctx) {
                waitables[0] = priv->ctx->render_event;
                waitables[1] = priv->ctx->capture_event;

                if (priv->mute)
                  priv->ctx->SetVolume (0);
                else
                  priv->ctx->SetVolume (priv->volume);
              } else {
                waitables[0] = dummy_render;
                waitables[1] = dummy_capture;

                gst_wasapi2_rbuf_post_open_error (self,
                    ucmd->device_id.c_str ());
                if (!priv->configured_allow_dummy) {
                  SetEvent (cmd->event_handle);
                  break;
                }
              }

              if (priv->running) {
                priv->running = false;
                gst_wasapi2_rbuf_process_start (self, FALSE);
              }
            }
            SetEvent (cmd->event_handle);
            break;
          }
          case CommandType::Open:
            priv->configured_allow_dummy = priv->allow_dummy;
            ClearMixFormat (&priv->mix_format);
            priv->ctx = gst_wasapi2_rbuf_create_ctx (self);

            if (priv->ctx) {
              waitables[0] = priv->ctx->render_event;
              waitables[1] = priv->ctx->capture_event;
              gst_caps_replace (&priv->caps, priv->ctx->caps);

              if (priv->mute)
                priv->ctx->SetVolume (0);
              else
                priv->ctx->SetVolume (priv->volume);

              priv->opened = true;
              priv->mix_format = copy_wave_format (priv->ctx->mix_format);
              cmd->hr = S_OK;
            } else {
              gst_clear_caps (&priv->caps);
              waitables[0] = dummy_render;
              waitables[1] = dummy_capture;
              gst_wasapi2_rbuf_post_open_error (self, priv->device_id.c_str ());

              if (priv->configured_allow_dummy) {
                priv->mix_format = gst_wasapi2_get_default_mix_format ();

                auto scaps = gst_static_caps_get (&template_caps);
                gst_wasapi2_util_parse_waveformatex (priv->mix_format,
                    scaps, &priv->caps, nullptr);
                gst_caps_unref (scaps);
                priv->opened = true;

                cmd->hr = S_OK;
              } else {
                cmd->hr = E_FAIL;
              }
            }
            SetEvent (cmd->event_handle);
            break;
          case CommandType::Close:
            waitables[0] = dummy_render;
            waitables[1] = dummy_capture;
            priv->ctx = nullptr;
            gst_clear_caps (&priv->caps);
            cmd->hr = S_OK;
            SetEvent (cmd->event_handle);
            priv->opened = false;
            ClearMixFormat (&priv->mix_format);
            gst_wasapi2_rbuf_stop_fallback_timer (self);
            break;
          case CommandType::Acquire:
          {
            auto acquire_cmd =
                std::dynamic_pointer_cast < CommandAcquire > (cmd);

            if (!priv->ctx) {
              priv->ctx = gst_wasapi2_rbuf_create_ctx (self);
              if (!priv->ctx) {
                GST_WARNING_OBJECT (self, "No context configured");
                gst_wasapi2_rbuf_post_open_error (self,
                    priv->device_id.c_str ());
                if (!priv->configured_allow_dummy) {
                  cmd->hr = E_FAIL;
                  SetEvent (cmd->event_handle);
                  break;
                }
              }
            }

            priv->opened = true;
            if (priv->ctx) {
              waitables[0] = priv->ctx->render_event;
              waitables[1] = priv->ctx->capture_event;
              gst_caps_replace (&priv->caps, priv->ctx->caps);

              if (priv->mute)
                priv->ctx->SetVolume (0);
              else
                priv->ctx->SetVolume (priv->volume);

              ClearMixFormat (&priv->mix_format);
              priv->mix_format = copy_wave_format (priv->ctx->mix_format);
            } else {
              waitables[0] = dummy_render;
              waitables[1] = dummy_capture;
            }

            gst_wasapi2_rbuf_process_acquire (self, acquire_cmd->spec);

            cmd->hr = S_OK;
            SetEvent (cmd->event_handle);
            break;
          }
          case CommandType::Release:
            cmd->hr = gst_wasapi2_rbuf_process_release (self);
            gst_wasapi2_rbuf_stop_fallback_timer (self);
            SetEvent (cmd->event_handle);
            break;
          case CommandType::Start:
            cmd->hr = gst_wasapi2_rbuf_process_start (self, TRUE);
            SetEvent (cmd->event_handle);
            break;
          case CommandType::Stop:
            cmd->hr = gst_wasapi2_rbuf_process_stop (self);
            SetEvent (cmd->event_handle);
            break;
          case CommandType::GetCaps:
          {
            auto caps_cmd = std::dynamic_pointer_cast < CommandGetCaps > (cmd);
            if (priv->caps)
              caps_cmd->caps = gst_caps_ref (priv->caps);

            SetEvent (cmd->event_handle);
            break;
          }
          case CommandType::UpdateVolume:
            if (priv->ctx) {
              if (priv->mute)
                priv->ctx->SetVolume (0);
              else
                priv->ctx->SetVolume (priv->volume);
            }
            SetEvent (cmd->event_handle);
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        GST_DEBUG_OBJECT (self, "command %s processed", cmd_name);
        lk.lock ();
      }
    }
  }

  priv->ctx = nullptr;
  priv->cmd_queue = { };
  ClearMixFormat (&priv->mix_format);

  CoUninitialize ();

  if (task_handle)
    AvRevertMmThreadCharacteristics (task_handle);

  GST_DEBUG_OBJECT (self, "Exit loop");

  CloseHandle (dummy_render);
  CloseHandle (dummy_capture);

  CancelWaitableTimer (priv->monitor_timer);
  CloseHandle (priv->monitor_timer);

  CancelWaitableTimer (priv->fallback_timer);
  CloseHandle (priv->fallback_timer);

  return nullptr;
}

static guint
gst_wasapi2_rbuf_delay (GstAudioRingBuffer * buf)
{
  /* NOTE: WASAPI supports GetCurrentPadding() method for querying
   * currently unread buffer size, but it doesn't seem to be quite useful
   * here because:
   *
   * In case of capture client, GetCurrentPadding() will return the number of
   * unread frames which will be identical to pNumFramesToRead value of
   * IAudioCaptureClient::GetBuffer()'s return. Since we are running on
   * event-driven mode and whenever available, WASAPI will notify signal
   * so it's likely zero at this moment. And there is a chance to
   * return incorrect value here because our IO callback happens from
   * other thread.
   *
   * And render client's padding size will return the total size of buffer
   * which is likely larger than twice of our period. Which doesn't represent
   * the amount queued frame size in device correctly
   */
  return 0;
}

GstWasapi2Rbuf *
gst_wasapi2_rbuf_new (gpointer parent, GstWasapi2RbufCallback callback)
{
  auto self = (GstWasapi2Rbuf *) g_object_new (GST_TYPE_WASAPI2_RBUF, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->invalidated_cb = callback;
  g_weak_ref_set (&priv->parent, parent);
  priv->thread = g_thread_new ("GstWasapi2Rbuf",
      (GThreadFunc) gst_wasapi2_rbuf_loop_thread, self);

  return self;
}

void
gst_wasapi2_rbuf_set_device (GstWasapi2Rbuf * rbuf, const gchar * device_id,
    GstWasapi2EndpointClass endpoint_class, guint pid, gboolean low_latency)
{
  auto cmd = std::make_shared < CommandSetDevice > ();

  if (device_id)
    cmd->device_id = device_id;
  cmd->endpoint_class = endpoint_class;
  cmd->pid = pid;
  cmd->low_latency = low_latency;

  gst_wasapi2_rbuf_push_command (rbuf, cmd);

  WaitForSingleObject (cmd->event_handle, INFINITE);
}

GstCaps *
gst_wasapi2_rbuf_get_caps (GstWasapi2Rbuf * rbuf)
{
  auto cmd = std::make_shared < CommandGetCaps > ();

  gst_wasapi2_rbuf_push_command (rbuf, cmd);
  WaitForSingleObject (cmd->event_handle, INFINITE);

  return cmd->caps;
}

void
gst_wasapi2_rbuf_set_mute (GstWasapi2Rbuf * rbuf, gboolean mute)
{
  auto priv = rbuf->priv;

  priv->mute = mute;

  auto cmd = std::make_shared < CommandData > (CommandType::UpdateVolume);

  gst_wasapi2_rbuf_push_command (rbuf, cmd);
}

gboolean
gst_wasapi2_rbuf_get_mute (GstWasapi2Rbuf * rbuf)
{
  auto priv = rbuf->priv;

  return priv->mute.load ();
}

void
gst_wasapi2_rbuf_set_volume (GstWasapi2Rbuf * rbuf, gdouble volume)
{
  auto priv = rbuf->priv;

  priv->volume = (float) volume;

  auto cmd = std::make_shared < CommandData > (CommandType::UpdateVolume);

  gst_wasapi2_rbuf_push_command (rbuf, cmd);
}

gdouble
gst_wasapi2_rbuf_get_volume (GstWasapi2Rbuf * rbuf)
{
  auto priv = rbuf->priv;

  return (gdouble) priv->volume.load ();
}

void
gst_wasapi2_rbuf_set_device_mute_monitoring (GstWasapi2Rbuf * rbuf,
    gboolean value)
{
  auto priv = rbuf->priv;

  priv->monitor_device_mute.store (value, std::memory_order_release);
}

void
gst_wasapi2_rbuf_set_continue_on_error (GstWasapi2Rbuf * rbuf, gboolean value)
{
  auto priv = rbuf->priv;

  priv->allow_dummy = value;
}
