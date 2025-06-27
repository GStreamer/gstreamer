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

#include "gstwasapi2enumerator.h"
#include "gstwasapi2activator.h"
#include <mutex>
#include <condition_variable>
#include <wrl.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_debug);
#define GST_CAT_DEFAULT gst_wasapi2_debug

static GstStaticCaps template_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);

static void gst_wasapi2_on_device_updated (GstWasapi2Enumerator * object);

/* IMMNotificationClient implementation */
class IWasapi2NotificationClient : public IMMNotificationClient
{
public:
  static void
  CreateInstance (GstWasapi2Enumerator * object, IMMNotificationClient ** client)
  {
    auto self = new IWasapi2NotificationClient ();

    g_weak_ref_set (&self->obj_, object);

    *client = (IMMNotificationClient *) self;
  }

  /* IUnknown */
  STDMETHODIMP
  QueryInterface (REFIID riid, void ** object)
  {
    if (!object)
      return E_POINTER;

    if (riid == IID_IUnknown) {
      *object = static_cast<IUnknown *> (this);
    } else if (riid == __uuidof(IMMNotificationClient)) {
      *object = static_cast<IMMNotificationClient *> (this);
    } else {
      *object = nullptr;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  STDMETHODIMP_ (ULONG)
  AddRef (void)
  {
    GST_TRACE ("%p, %d", this, (guint) ref_count_);
    return InterlockedIncrement (&ref_count_);
  }

  STDMETHODIMP_ (ULONG)
  Release (void)
  {
    ULONG ref_count;

    GST_TRACE ("%p, %d", this, (guint) ref_count_);
    ref_count = InterlockedDecrement (&ref_count_);

    if (ref_count == 0) {
      GST_TRACE ("Delete instance %p", this);
      delete this;
    }

    return ref_count;
  }

  /* IMMNotificationClient */
  STDMETHODIMP
  OnDeviceStateChanged (LPCWSTR device_id, DWORD new_state)
  {
    auto object = (GstWasapi2Enumerator *) g_weak_ref_get (&obj_);
    if (!object)
      return S_OK;

    gst_wasapi2_on_device_updated (object);
    gst_object_unref (object);

    return S_OK;
  }

  STDMETHODIMP
  OnDeviceAdded (LPCWSTR device_id)
  {
    auto object = (GstWasapi2Enumerator *) g_weak_ref_get (&obj_);
    if (!object)
      return S_OK;

    gst_wasapi2_on_device_updated (object);
    gst_object_unref (object);

    return S_OK;
  }

  STDMETHODIMP
  OnDeviceRemoved (LPCWSTR device_id)
  {
    auto object = (GstWasapi2Enumerator *) g_weak_ref_get (&obj_);
    if (!object)
      return S_OK;

    gst_wasapi2_on_device_updated (object);
    gst_object_unref (object);

    return S_OK;
  }

  STDMETHODIMP
  OnDefaultDeviceChanged (EDataFlow flow, ERole role, LPCWSTR default_device_id)
  {
    auto object = (GstWasapi2Enumerator *) g_weak_ref_get (&obj_);
    if (!object)
      return S_OK;

    gst_wasapi2_on_device_updated (object);
    gst_object_unref (object);

    return S_OK;
  }

  STDMETHODIMP
  OnPropertyValueChanged (LPCWSTR device_id, const PROPERTYKEY key)
  {
    return S_OK;
  }

private:
  IWasapi2NotificationClient ()
  {
    g_weak_ref_init (&obj_, nullptr);
  }

  virtual ~IWasapi2NotificationClient ()
  {
    g_weak_ref_clear (&obj_);
  }

private:
  ULONG ref_count_ = 1;
  GWeakRef obj_;
};

enum
{
  PROP_0,
  PROP_ENUMERATOR,
};

enum
{
  SIGNAL_UPDATED,
  SIGNAL_LAST,
};

static guint wasapi2_device_signals[SIGNAL_LAST] = { };

struct GstWasapi2EnumeratorPrivate
{
  ComPtr<IMMDeviceEnumerator> handle;
  std::mutex lock;
  std::condition_variable cond;

  ComPtr<IMMNotificationClient> client;
  Wasapi2ActivationHandler *capture_activator = nullptr;
  Wasapi2ActivationHandler *render_activator = nullptr;

  void ClearCOM ()
  {
    if (capture_activator) {
      capture_activator->GetClient (nullptr, INFINITE);
      capture_activator->Release ();
    }

    if (render_activator) {
      render_activator->GetClient (nullptr, INFINITE);
      render_activator->Release ();
    }

    if (client && handle)
      handle->UnregisterEndpointNotificationCallback (client.Get ());

    client = nullptr;
    handle = nullptr;
  }
};
/* *INDENT-ON* */

struct _GstWasapi2Enumerator
{
  GstObject parent;

  GstWasapi2EnumeratorPrivate *priv;

  GThread *thread;
  GMainContext *context;
  GMainLoop *loop;
};

static void gst_wasapi2_enumerator_finalize (GObject * object);

#define gst_wasapi2_enumerator_parent_class parent_class
G_DEFINE_TYPE (GstWasapi2Enumerator, gst_wasapi2_enumerator, GST_TYPE_OBJECT);

static void
gst_wasapi2_enumerator_class_init (GstWasapi2EnumeratorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_wasapi2_enumerator_finalize;

  wasapi2_device_signals[SIGNAL_UPDATED] =
      g_signal_new_class_handler ("updated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, nullptr, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

static void
gst_wasapi2_enumerator_init (GstWasapi2Enumerator * self)
{
  self->priv = new GstWasapi2EnumeratorPrivate ();
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
}

static void
gst_wasapi2_enumerator_finalize (GObject * object)
{
  auto self = GST_WASAPI2_ENUMERATOR (object);

  g_main_loop_quit (self->loop);
  g_thread_join (self->thread);
  g_main_loop_unref (self->loop);
  g_main_context_unref (self->context);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_on_device_updated (GstWasapi2Enumerator * object)
{
  /* *INDENT-OFF* */
  g_main_context_invoke_full (object->context, G_PRIORITY_DEFAULT,
      [] (gpointer obj) -> gboolean {
        g_signal_emit (obj, wasapi2_device_signals[SIGNAL_UPDATED], 0);
        return G_SOURCE_REMOVE;
      },
      gst_object_ref (object), (GDestroyNotify) gst_object_unref);
  /* *INDENT-ON* */
}

static gpointer
gst_wasapi2_enumerator_thread_func (GstWasapi2Enumerator * self)
{
  auto priv = self->priv;

  CoInitializeEx (nullptr, COINIT_MULTITHREADED);

  g_main_context_push_thread_default (self->context);

  auto idle_source = g_idle_source_new ();
  /* *INDENT-OFF* */
  g_source_set_callback (idle_source,
      [] (gpointer user_data) -> gboolean {
        auto self = (GstWasapi2Enumerator *) user_data;
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
      nullptr, CLSCTX_ALL, IID_PPV_ARGS (&priv->handle));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Failed to create IMMDeviceEnumerator instance");
    goto run_loop;
  }

  if (gst_wasapi2_can_automatic_stream_routing ()) {
    Wasapi2ActivationHandler::CreateInstance (&priv->capture_activator,
        gst_wasapi2_get_default_device_id_wide (eCapture), nullptr);
    priv->capture_activator->ActivateAsync ();

    Wasapi2ActivationHandler::CreateInstance (&priv->render_activator,
        gst_wasapi2_get_default_device_id_wide (eRender), nullptr);
    priv->render_activator->ActivateAsync ();
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

GstWasapi2Enumerator *
gst_wasapi2_enumerator_new (void)
{
  auto self = (GstWasapi2Enumerator *)
      g_object_new (GST_TYPE_WASAPI2_ENUMERATOR, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;

  {
    std::unique_lock < std::mutex > lk (priv->lock);
    self->thread = g_thread_new ("GstWasapi2Enumerator",
        (GThreadFunc) gst_wasapi2_enumerator_thread_func, self);
    while (!g_main_loop_is_running (self->loop))
      priv->cond.wait (lk);
  }

  if (!priv->handle) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

/* *INDENT-OFF* */
struct ActivateNotificationData
{
  ActivateNotificationData ()
  {
    event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  ~ActivateNotificationData ()
  {
    CloseHandle (event);
  }

  GstWasapi2Enumerator *self;
  gboolean active;
  HANDLE event;
};
/* *INDENT-ON* */

static gboolean
set_notification_callback (ActivateNotificationData * data)
{
  auto self = data->self;
  auto priv = self->priv;

  if (data->active) {
    if (!priv->client) {
      ComPtr < IMMNotificationClient > client;
      IWasapi2NotificationClient::CreateInstance (self, &client);

      auto hr =
          priv->handle->RegisterEndpointNotificationCallback (client.Get ());
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't register callback");
      } else {
        GST_LOG_OBJECT (self, "Registered notification");
        priv->client = client;
      }
    }
  } else if (priv->client) {
    priv->handle->UnregisterEndpointNotificationCallback (priv->client.Get ());
    priv->client = nullptr;
    GST_LOG_OBJECT (self, "Unregistered notification");
  }

  SetEvent (data->event);

  return G_SOURCE_REMOVE;
}

void
gst_wasapi2_enumerator_activate_notification (GstWasapi2Enumerator * object,
    gboolean active)
{
  auto priv = object->priv;

  if (!priv->handle)
    return;

  ActivateNotificationData data;
  data.self = object;
  data.active = active;

  g_main_context_invoke (object->context,
      (GSourceFunc) set_notification_callback, &data);

  WaitForSingleObject (data.event, INFINITE);
}

void
gst_wasapi2_enumerator_entry_free (GstWasapi2EnumeratorEntry * entry)
{
  g_free (entry->device_id);
  g_free (entry->device_name);
  gst_clear_caps (&entry->caps);
  g_free (entry);
}

/* *INDENT-OFF* */
struct EnumerateData
{
  EnumerateData ()
  {
    event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  ~EnumerateData ()
  {
    CloseHandle (event);
  }

  GstWasapi2Enumerator *self;
  GPtrArray *device_list;
  HANDLE event;
};
/* *INDENT-ON* */

static void
gst_wasapi2_enumerator_add_entry (GstWasapi2Enumerator * self,
    IAudioClient * client,
    GstCaps * static_caps, EDataFlow flow, gboolean is_default,
    gchar * device_id, gchar * device_name, GPtrArray * device_list)
{
  WAVEFORMATEX *mix_format = nullptr;
  GstCaps *supported_caps = nullptr;

  client->GetMixFormat (&mix_format);
  if (!mix_format) {
    g_free (device_id);
    g_free (device_name);
    return;
  }

  gst_wasapi2_util_parse_waveformatex (mix_format,
      static_caps, &supported_caps, nullptr);
  CoTaskMemFree (mix_format);

  if (!supported_caps) {
    g_free (device_id);
    g_free (device_name);
    return;
  }

  auto entry = g_new0 (GstWasapi2EnumeratorEntry, 1);

  entry->device_id = device_id;
  entry->device_name = device_name;
  entry->caps = supported_caps;
  entry->flow = flow;
  entry->is_default = is_default;

  GST_LOG_OBJECT (self, "Adding entry %s (%s), flow %d, caps %" GST_PTR_FORMAT,
      device_id, device_name, flow, supported_caps);

  g_ptr_array_add (device_list, entry);
}

static gboolean
gst_wasapi2_enumerator_enumerate_internal (EnumerateData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  ComPtr < IMMDeviceCollection > collection;

  auto hr = priv->handle->EnumAudioEndpoints (eAll, DEVICE_STATE_ACTIVE,
      &collection);
  if (!gst_wasapi2_result (hr)) {
    SetEvent (data->event);
    return G_SOURCE_REMOVE;
  }

  UINT count = 0;
  hr = collection->GetCount (&count);
  if (!gst_wasapi2_result (hr) || count == 0) {
    SetEvent (data->event);
    return G_SOURCE_REMOVE;
  }

  auto scaps = gst_static_caps_get (&template_caps);

  ComPtr < IAudioClient > default_capture_client;
  ComPtr < IAudioClient > default_render_client;
  if (priv->capture_activator)
    priv->capture_activator->GetClient (&default_capture_client, 10000);
  if (priv->render_activator)
    priv->render_activator->GetClient (&default_render_client, 10000);

  if (default_capture_client) {
    gst_wasapi2_enumerator_add_entry (self, default_capture_client.Get (),
        scaps, eCapture, TRUE,
        g_strdup (gst_wasapi2_get_default_device_id (eCapture)),
        g_strdup ("Default Audio Capture Device"), data->device_list);
  }

  if (default_render_client) {
    gst_wasapi2_enumerator_add_entry (self, default_render_client.Get (),
        scaps, eRender, TRUE,
        g_strdup (gst_wasapi2_get_default_device_id (eRender)),
        g_strdup ("Default Audio Render Device"), data->device_list);
  }

  for (UINT i = 0; i < count; i++) {
    ComPtr < IMMDevice > device;
    ComPtr < IMMEndpoint > endpoint;
    EDataFlow flow;

    hr = collection->Item (i, &device);
    if (!gst_wasapi2_result (hr))
      continue;

    hr = device.As (&endpoint);
    if (!gst_wasapi2_result (hr))
      continue;

    hr = endpoint->GetDataFlow (&flow);
    if (!gst_wasapi2_result (hr))
      continue;

    ComPtr < IPropertyStore > prop;
    hr = device->OpenPropertyStore (STGM_READ, &prop);
    if (!gst_wasapi2_result (hr))
      continue;

    PROPVARIANT var;
    PropVariantInit (&var);
    hr = prop->GetValue (PKEY_Device_FriendlyName, &var);
    if (!gst_wasapi2_result (hr))
      continue;

    auto desc = g_utf16_to_utf8 ((gunichar2 *) var.pwszVal,
        -1, nullptr, nullptr, nullptr);
    PropVariantClear (&var);

    LPWSTR wid = nullptr;
    hr = device->GetId (&wid);
    if (!gst_wasapi2_result (hr)) {
      g_free (desc);
      continue;
    }

    auto device_id = g_utf16_to_utf8 ((gunichar2 *) wid,
        -1, nullptr, nullptr, nullptr);
    CoTaskMemFree (wid);

    ComPtr < IAudioClient > client;
    hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
        &client);
    if (!gst_wasapi2_result (hr)) {
      g_free (device_id);
      g_free (desc);
      continue;
    }

    gst_wasapi2_enumerator_add_entry (self, client.Get (), scaps, flow, FALSE,
        device_id, desc, data->device_list);
  }

  gst_caps_unref (scaps);

  SetEvent (data->event);
  return G_SOURCE_REMOVE;
}

void
gst_wasapi2_enumerator_enumerate_devices (GstWasapi2Enumerator * object,
    GPtrArray * device_list)
{
  EnumerateData data;

  data.self = object;
  data.device_list = device_list;

  g_main_context_invoke (object->context,
      (GSourceFunc) gst_wasapi2_enumerator_enumerate_internal, &data);

  WaitForSingleObject (data.event, INFINITE);
}
