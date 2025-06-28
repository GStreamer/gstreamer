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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwasapi2object.h"
#include "gstwasapi2activator.h"
#include <endpointvolume.h>
#include <mutex>
#include <condition_variable>
#include <wrl.h>
#include <string>
#include <atomic>
#include <string.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_debug);
#define GST_CAT_DEFAULT gst_wasapi2_debug

static GstStaticCaps template_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);

static void gst_wasapi2_object_set_endpoint_muted (GstWasapi2Object * object,
    bool muted);

DEFINE_GUID (IID_Wasapi2EndpointVolumeCallback, 0x21ba991f, 0x4d78,
    0x418c, 0xa1, 0xea, 0x8a, 0xc7, 0xdd, 0xa2, 0xdc, 0x39);
class Wasapi2EndpointVolumeCallback : public IAudioEndpointVolumeCallback
{
public:
  static void CreateInstance (Wasapi2EndpointVolumeCallback ** iface,
      GstWasapi2Object * client)
  {
    auto self = new Wasapi2EndpointVolumeCallback ();
    g_weak_ref_set (&self->client_, client);
    *iface = self;
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
    auto client = (GstWasapi2Object *) g_weak_ref_get (&client_);

    if (client) {
      gst_wasapi2_object_set_endpoint_muted (client, notify->bMuted);
      gst_object_unref (client);
    }

    return S_OK;
  }

private:
  Wasapi2EndpointVolumeCallback ()
  {
    g_weak_ref_init (&client_, nullptr);
  }

  virtual ~Wasapi2EndpointVolumeCallback ()
  {
    g_weak_ref_set (&client_, nullptr);
  }

private:
  ULONG refcount_ = 1;
  GWeakRef client_;
};

struct GstWasapi2ObjectPrivate
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioEndpointVolume> endpoint_volume;
  std::atomic<bool> endpoint_muted = { false };
  Wasapi2EndpointVolumeCallback *volume_callback = nullptr;
  Wasapi2ActivationHandler *activator = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  std::string device_id;
  GstWasapi2EndpointClass device_class;
  guint target_pid;
  gboolean is_default_device = FALSE;

  void ClearCOM ()
  {
    if (volume_callback && endpoint_volume)
      endpoint_volume->UnregisterControlChangeNotify (volume_callback);
    if (activator)
      activator->Release ();
    client = nullptr;
    if (volume_callback)
      volume_callback->Release ();
    endpoint_volume = nullptr;
    device = nullptr;
    enumerator = nullptr;
  }
};
/* *INDENT-ON* */

struct _GstWasapi2Object
{
  GstObject parent;

  GstWasapi2ObjectPrivate *priv;

  GThread *thread;
  GMainContext *context;
  GMainLoop *loop;
  GstCaps *caps;
};

static void gst_wasapi2_object_finalize (GObject * object);

#define gst_wasapi2_object_parent_class parent_class
G_DEFINE_TYPE (GstWasapi2Object, gst_wasapi2_object, GST_TYPE_OBJECT);

static void
gst_wasapi2_object_class_init (GstWasapi2ObjectClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_wasapi2_object_finalize;
}

static void
gst_wasapi2_object_init (GstWasapi2Object * self)
{
  self->priv = new GstWasapi2ObjectPrivate ();
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
}

static void
gst_wasapi2_object_finalize (GObject * object)
{
  auto self = GST_WASAPI2_OBJECT (object);

  g_main_loop_quit (self->loop);
  g_thread_join (self->thread);
  g_main_loop_unref (self->loop);
  g_main_context_unref (self->context);
  gst_clear_caps (&self->caps);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_object_set_endpoint_muted (GstWasapi2Object * object, bool muted)
{
  auto priv = object->priv;
  priv->endpoint_muted.store (muted, std::memory_order_release);
}

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

static gpointer
gst_wasapi2_object_thread_func (GstWasapi2Object * self)
{
  auto priv = self->priv;

  CoInitializeEx (nullptr, COINIT_MULTITHREADED);

  g_main_context_push_thread_default (self->context);

  auto idle_source = g_idle_source_new ();
  /* *INDENT-OFF* */
  g_source_set_callback (idle_source,
      [] (gpointer user_data) -> gboolean {
        auto self = (GstWasapi2Object *) user_data;
        auto priv = self->priv;
        std::lock_guard < std::mutex > lk (priv->lock);
        priv->cond.notify_all ();
        return G_SOURCE_REMOVE;
      },
      self, nullptr);
  /* *INDENT-ON* */
  g_source_attach (idle_source, self->context);
  g_source_unref (idle_source);

  auto hr = CoCreateInstance (__uuidof (MMDeviceEnumerator),
      nullptr, CLSCTX_ALL, IID_PPV_ARGS (&priv->enumerator));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Failed to create IMMDeviceEnumerator instance");
    goto run_loop;
  }

  switch (priv->device_class) {
    case GST_WASAPI2_ENDPOINT_CLASS_CAPTURE:
      if (priv->device_id.empty () ||
          is_equal_device_id (priv->device_id.c_str (),
              gst_wasapi2_get_default_device_id (eCapture))) {
        if (gst_wasapi2_can_automatic_stream_routing ()) {
          Wasapi2ActivationHandler::CreateInstance (&priv->activator,
              gst_wasapi2_get_default_device_id_wide (eCapture), nullptr);
          GST_DEBUG_OBJECT (self, "Creating default capture device");
          priv->is_default_device = TRUE;
        } else {
          GST_DEBUG_OBJECT (self, "Creating default capture MMdevice");
          hr = priv->enumerator->GetDefaultAudioEndpoint (eCapture,
              eConsole, &priv->device);
        }
      } else {
        auto wstr = g_utf8_to_utf16 (priv->device_id.c_str (),
            -1, nullptr, nullptr, nullptr);
        hr = priv->enumerator->GetDevice ((LPCWSTR) wstr, &priv->device);
        g_free (wstr);
      }
      break;
    case GST_WASAPI2_ENDPOINT_CLASS_RENDER:
    case GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE:
      if (priv->device_id.empty () ||
          is_equal_device_id (priv->device_id.c_str (),
              gst_wasapi2_get_default_device_id (eRender))) {
        if (gst_wasapi2_can_automatic_stream_routing ()) {
          Wasapi2ActivationHandler::CreateInstance (&priv->activator,
              gst_wasapi2_get_default_device_id_wide (eRender), nullptr);
          GST_DEBUG_OBJECT (self, "Creating default render device");
          priv->is_default_device = TRUE;
        } else {
          GST_DEBUG_OBJECT (self, "Creating default render MMdevice");
          hr = priv->enumerator->GetDefaultAudioEndpoint (eRender,
              eConsole, &priv->device);
        }
      } else {
        auto wstr = g_utf8_to_utf16 (priv->device_id.c_str (),
            -1, nullptr, nullptr, nullptr);
        hr = priv->enumerator->GetDevice ((LPCWSTR) wstr, &priv->device);
        g_free (wstr);
      }
      break;
    case GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE:
    case GST_WASAPI2_ENDPOINT_CLASS_EXCLUDE_PROCESS_LOOPBACK_CAPTURE:
    {
      AUDIOCLIENT_ACTIVATION_PARAMS params = { };
      params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
      params.ProcessLoopbackParams.TargetProcessId = priv->target_pid;
      if (priv->device_class ==
          GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE) {
        params.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
      } else {
        params.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
      }

      GST_DEBUG_OBJECT (self, "Creating process loopback capture device");

      Wasapi2ActivationHandler::CreateInstance (&priv->activator,
          VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, &params);
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (priv->activator || priv->device) {
    if (priv->activator) {
      hr = priv->activator->ActivateAsync ();
      if (gst_wasapi2_result (hr))
        hr = priv->activator->GetClient (&priv->client, INFINITE);
    } else {
      hr = priv->device->Activate (__uuidof (IAudioClient), CLSCTX_ALL,
          nullptr, &priv->client);
    }

    if (!gst_wasapi2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't activate device");
    } else if (priv->device &&
        priv->device_class == GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE) {
      hr = priv->device->Activate (__uuidof (IAudioEndpointVolume),
          CLSCTX_ALL, nullptr, &priv->endpoint_volume);
      if (gst_wasapi2_result (hr)) {
        Wasapi2EndpointVolumeCallback::CreateInstance (&priv->volume_callback,
            self);
        hr = priv->endpoint_volume->
            RegisterControlChangeNotify (priv->volume_callback);
        if (!gst_wasapi2_result (hr)) {
          priv->volume_callback->Release ();
          priv->volume_callback = nullptr;
        } else {
          BOOL muted = FALSE;
          priv->endpoint_volume->GetMute (&muted);
          if (gst_wasapi2_result (hr))
            gst_wasapi2_object_set_endpoint_muted (self, muted);
        }
      }
    }
  } else {
    GST_WARNING_OBJECT (self, "No device created");
  }

  if (priv->client) {
    WAVEFORMATEX *mix_format = nullptr;
    hr = priv->client->GetMixFormat (&mix_format);
    if (!gst_wasapi2_result (hr)) {
      if (gst_wasapi2_is_process_loopback_class (priv->device_class))
        mix_format = gst_wasapi2_get_default_mix_format ();
    }

    if (mix_format) {
      auto scaps = gst_static_caps_get (&template_caps);
      gst_wasapi2_util_parse_waveformatex (mix_format,
          scaps, &self->caps, nullptr);
      gst_caps_unref (scaps);

      CoTaskMemFree (mix_format);
    }
  }

run_loop:
  GST_INFO_OBJECT (self, "Starting loop");
  g_main_loop_run (self->loop);
  GST_INFO_OBJECT (self, "Stopped loop");

  priv->ClearCOM ();

  g_main_context_pop_thread_default (self->context);

  CoUninitialize ();

  return nullptr;
}

GstWasapi2Object *
gst_wasapi2_object_new (GstWasapi2EndpointClass device_class,
    const gchar * device_id, guint target_pid)
{
  auto self = (GstWasapi2Object *)
      g_object_new (GST_TYPE_WASAPI2_OBJECT, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device_class = device_class;
  if (device_id)
    priv->device_id = device_id;
  priv->target_pid = target_pid;

  if (gst_wasapi2_is_process_loopback_class (device_class) && !target_pid) {
    GST_ERROR_OBJECT (self, "Unspecified target PID");
    gst_object_unref (self);
    return nullptr;
  }

  {
    std::unique_lock < std::mutex > lk (priv->lock);
    self->thread = g_thread_new ("GstWasapi2Object",
        (GThreadFunc) gst_wasapi2_object_thread_func, self);
    while (!g_main_loop_is_running (self->loop))
      priv->cond.wait (lk);
  }

  if (!priv->client) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

GstCaps *
gst_wasapi2_object_get_caps (GstWasapi2Object * object)
{
  if (object->caps)
    return gst_caps_ref (object->caps);

  return nullptr;
}

IAudioClient *
gst_wasapi2_object_get_handle (GstWasapi2Object * object)
{
  return object->priv->client.Get ();
}

gboolean
gst_wasapi2_object_is_endpoint_muted (GstWasapi2Object * object)
{
  return object->priv->endpoint_muted.load (std::memory_order_acquire);
}

gboolean
gst_wasapi2_object_auto_routing_supported (GstWasapi2Object * object)
{
  return object->priv->is_default_device;
}
