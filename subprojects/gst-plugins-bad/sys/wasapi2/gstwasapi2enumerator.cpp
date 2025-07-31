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
#include <atomic>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_WASAPI2_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("wasapi2enumerator", 0, "wasapi2enumerator");
  } GST_WASAPI2_CALL_ONCE_END;

  return cat;
}
#endif

static GstStaticCaps template_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);

static void gst_wasapi2_on_device_updated (GstWasapi2Enumerator * object);

static std::string
device_state_to_string (DWORD state)
{
  std::string ret;
  bool is_first = true;
  if ((state & DEVICE_STATE_ACTIVE) == DEVICE_STATE_ACTIVE) {
    if (!is_first)
      ret += "|";
    ret += "ACTIVE";
    is_first = false;
  }

  if ((state & DEVICE_STATE_DISABLED) == DEVICE_STATE_DISABLED) {
    if (!is_first)
      ret += "|";
    ret += "DISABLED";
    is_first = false;
  }

  if ((state & DEVICE_STATE_NOTPRESENT) == DEVICE_STATE_NOTPRESENT) {
    if (!is_first)
      ret += "|";
    ret += "NOTPRESENT";
    is_first = false;
  }

  if ((state & DEVICE_STATE_UNPLUGGED) == DEVICE_STATE_UNPLUGGED) {
    if (!is_first)
      ret += "|";
    ret += "UNPLUGGED";
    is_first = false;
  }

  return ret;
}

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

    auto id = g_utf16_to_utf8 ((gunichar2 *) device_id,
          -1, nullptr, nullptr, nullptr);
    auto state = device_state_to_string (new_state);
    GST_LOG ("%s, %s (0x%x)", id, state.c_str (), (guint) new_state);
    g_free (id);

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

    auto id = g_utf16_to_utf8 ((gunichar2 *) device_id,
          -1, nullptr, nullptr, nullptr);
    GST_LOG ("%s", id);
    g_free (id);

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

    auto id = g_utf16_to_utf8 ((gunichar2 *) device_id,
          -1, nullptr, nullptr, nullptr);
    GST_LOG ("%s", id);
    g_free (id);

    gst_wasapi2_on_device_updated (object);
    gst_object_unref (object);

    return S_OK;
  }

  STDMETHODIMP
  OnDefaultDeviceChanged (EDataFlow flow, ERole role, LPCWSTR device_id)
  {
    auto object = (GstWasapi2Enumerator *) g_weak_ref_get (&obj_);
    if (!object)
      return S_OK;

    auto id = g_utf16_to_utf8 ((gunichar2 *) device_id,
          -1, nullptr, nullptr, nullptr);
    GST_LOG ("%s, flow: %s, role: %s", id,
        gst_wasapi2_data_flow_to_string (flow),
        gst_wasapi2_role_to_string (role));
    g_free (id);

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
  GstWasapi2EnumeratorPrivate ()
  {
    device_list = g_ptr_array_new_with_free_func ((GDestroyNotify)
        gst_wasapi2_enumerator_entry_free);
    scaps = gst_static_caps_get (&template_caps);
  }

  ~GstWasapi2EnumeratorPrivate ()
  {
    g_ptr_array_unref (device_list);
    gst_caps_unref (scaps);
  }

  ComPtr<IMMDeviceEnumerator> handle;
  std::mutex lock;
  std::condition_variable cond;

  ComPtr<IMMNotificationClient> client;
  Wasapi2ActivationHandler *capture_activator = nullptr;
  Wasapi2ActivationHandler *render_activator = nullptr;
  std::atomic<int> notify_count = { 0 };
  GPtrArray *device_list;
  GstCaps *scaps;

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
  auto priv = object->priv;

  auto count = priv->notify_count.fetch_add (1);
  GST_LOG ("notify count before scheduling %d", count);

  auto source = g_timeout_source_new (100);
  g_source_set_callback (source,
      [] (gpointer obj) -> gboolean {
        auto self = GST_WASAPI2_ENUMERATOR (obj);
        auto priv = self->priv;
        auto count = priv->notify_count.fetch_sub (1);
        GST_LOG ("scheduled notify count %d", count);
        if (count == 1)
          g_signal_emit (obj, wasapi2_device_signals[SIGNAL_UPDATED], 0);
        return G_SOURCE_REMOVE;
      },
      gst_object_ref (object), (GDestroyNotify) gst_object_unref);

  g_source_attach (source, object->context);
  g_source_unref (source);
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
  delete entry;
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
    gchar * device_id, gchar * device_name,
    gchar * actual_device_id, gchar * actual_device_name,
    GstWasapi2DeviceProps * device_props, GPtrArray * device_list)
{
  WAVEFORMATEX *mix_format = nullptr;
  GstCaps *supported_caps = nullptr;

  client->GetMixFormat (&mix_format);
  if (!mix_format) {
    g_free (device_id);
    g_free (device_name);
    g_free (actual_device_id);
    g_free (actual_device_name);
    return;
  }

  gst_wasapi2_util_parse_waveformatex (mix_format,
      static_caps, &supported_caps, nullptr);
  CoTaskMemFree (mix_format);

  if (!supported_caps) {
    g_free (device_id);
    g_free (device_name);
    g_free (actual_device_id);
    g_free (actual_device_name);
    return;
  }

  auto entry = new GstWasapi2EnumeratorEntry ();

  entry->device_id = device_id;
  entry->device_name = device_name;
  entry->caps = supported_caps;
  entry->flow = flow;
  entry->is_default = is_default;
  if (actual_device_id)
    entry->actual_device_id = actual_device_id;
  if (actual_device_name)
    entry->actual_device_name = actual_device_name;

  if (device_props) {
    entry->device_props.form_factor = device_props->form_factor;
    entry->device_props.enumerator_name = device_props->enumerator_name;
  }

  GST_LOG_OBJECT (self, "Adding entry %s (%s), flow %d, caps %" GST_PTR_FORMAT,
      device_id, device_name, flow, supported_caps);
  g_free (device_id);
  g_free (device_name);
  g_free (actual_device_id);
  g_free (actual_device_name);

  g_ptr_array_add (device_list, entry);
}

static void
gst_wasapi2_enumerator_probe_props (IPropertyStore * store,
    GstWasapi2DeviceProps * props)
{
  PROPVARIANT var;
  PropVariantInit (&var);

  auto hr = store->GetValue (PKEY_AudioEndpoint_FormFactor, &var);
  if (SUCCEEDED (hr) && var.vt == VT_UI4)
    props->form_factor = (EndpointFormFactor) var.ulVal;

  PropVariantClear (&var);

  hr = store->GetValue (PKEY_Device_EnumeratorName, &var);
  if (SUCCEEDED (hr) && var.vt == VT_LPWSTR) {
    auto name = g_utf16_to_utf8 ((gunichar2 *) var.pwszVal,
        -1, nullptr, nullptr, nullptr);
    props->enumerator_name = name;
    g_free (name);
  }

  PropVariantClear (&var);
}

static void
probe_default_device_props (GstWasapi2Enumerator * self, EDataFlow flow,
    GstWasapi2DeviceProps * props, gchar ** actual_device_id,
    gchar ** actual_device_name)
{
  auto priv = self->priv;
  ComPtr < IMMDevice > device;
  ComPtr < IPropertyStore > prop;

  *actual_device_id = nullptr;
  *actual_device_name = nullptr;

  auto hr = priv->handle->GetDefaultAudioEndpoint (flow,
      eConsole, &device);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Couldn't get default endpoint for %s",
        gst_wasapi2_data_flow_to_string (flow));
    return;
  }

  LPWSTR wid = nullptr;
  hr = device->GetId (&wid);
  if (gst_wasapi2_result (hr)) {
    *actual_device_id = g_utf16_to_utf8 ((gunichar2 *) wid,
        -1, nullptr, nullptr, nullptr);
    CoTaskMemFree (wid);
  }

  hr = device->OpenPropertyStore (STGM_READ, &prop);
  if (!gst_wasapi2_result (hr))
    return;

  PROPVARIANT var;
  PropVariantInit (&var);
  hr = prop->GetValue (PKEY_Device_FriendlyName, &var);
  if (gst_wasapi2_result (hr)) {
    *actual_device_name = g_utf16_to_utf8 ((gunichar2 *) var.pwszVal,
        -1, nullptr, nullptr, nullptr);
    PropVariantClear (&var);
  }

  gst_wasapi2_enumerator_probe_props (prop.Get (), props);
}

static gboolean
gst_wasapi2_enumerator_execute (GstWasapi2Enumerator * self,
    IMMDeviceCollection * collection, gboolean ignore_error)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start enumerate");

  UINT count = 0;
  auto hr = collection->GetCount (&count);
  if (!gst_wasapi2_result (hr) || count == 0)
    return TRUE;

  ComPtr < IAudioClient > default_capture_client;
  ComPtr < IAudioClient > default_render_client;
  if (priv->capture_activator)
    priv->capture_activator->GetClient (&default_capture_client, 10000);
  if (priv->render_activator)
    priv->render_activator->GetClient (&default_render_client, 10000);

  if (default_capture_client) {
    GstWasapi2DeviceProps props;
    props.form_factor = UnknownFormFactor;
    props.enumerator_name = "UNKNOWN";

    gchar *actual_device_id = nullptr;
    gchar *actual_device_name = nullptr;
    probe_default_device_props (self, eCapture, &props, &actual_device_id,
        &actual_device_name);

    gst_wasapi2_enumerator_add_entry (self, default_capture_client.Get (),
        priv->scaps, eCapture, TRUE,
        g_strdup (gst_wasapi2_get_default_device_id (eCapture)),
        g_strdup ("Default Audio Capture Device"), actual_device_id,
        actual_device_name, &props, priv->device_list);
  }

  if (default_render_client) {
    GstWasapi2DeviceProps props;
    props.form_factor = UnknownFormFactor;
    props.enumerator_name = "UNKNOWN";

    gchar *actual_device_id = nullptr;
    gchar *actual_device_name = nullptr;
    probe_default_device_props (self, eRender, &props, &actual_device_id,
        &actual_device_name);

    gst_wasapi2_enumerator_add_entry (self, default_render_client.Get (),
        priv->scaps, eRender, TRUE,
        g_strdup (gst_wasapi2_get_default_device_id (eRender)),
        g_strdup ("Default Audio Render Device"), actual_device_id,
        actual_device_name, &props, priv->device_list);
  }

  for (UINT i = 0; i < count; i++) {
    ComPtr < IMMDevice > device;
    ComPtr < IMMEndpoint > endpoint;
    EDataFlow flow;

    GstWasapi2DeviceProps props;
    props.form_factor = UnknownFormFactor;
    props.enumerator_name = "UNKNOWN";

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
      /* Requested active devices via DEVICE_STATE_ACTIVE but activate fail here.
       * That means devices were changed while we were enumerating.
       * Need retry here */
      GST_DEBUG_OBJECT (self, "Couldn't activate device %s (%s)",
          device_id, desc);
      g_free (device_id);
      g_free (desc);

      if (!ignore_error && hr == AUDCLNT_E_DEVICE_INVALIDATED)
        return FALSE;
    }

    gst_wasapi2_enumerator_probe_props (prop.Get (), &props);

    gst_wasapi2_enumerator_add_entry (self, client.Get (), priv->scaps, flow,
        FALSE, device_id, desc, nullptr, nullptr, &props, priv->device_list);
  }

  return TRUE;
}

static gboolean
gst_wasapi2_enumerator_enumerate_internal (EnumerateData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  /* Upto 3 times retry */
  const guint num_retry = 5;

  for (guint i = 0; i < num_retry; i++) {
    ComPtr < IMMDeviceCollection > collection;
    gboolean is_last = FALSE;

    if (i + 1 == num_retry)
      is_last = TRUE;

    g_ptr_array_set_size (priv->device_list, 0);

    auto hr = priv->handle->EnumAudioEndpoints (eAll, DEVICE_STATE_ACTIVE,
        &collection);
    if (!gst_wasapi2_result (hr)) {
      SetEvent (data->event);
      return G_SOURCE_REMOVE;
    }

    if (gst_wasapi2_enumerator_execute (self, collection.Get (), is_last))
      break;

    if (!is_last) {
      GST_DEBUG_OBJECT (self, "Sleep for retrying");
      Sleep (50);
    }
  }

  while (priv->device_list->len > 0) {
    g_ptr_array_add (data->device_list,
        g_ptr_array_steal_index (priv->device_list, 0));
  }

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

const gchar *
gst_wasapi2_form_factor_to_string (EndpointFormFactor form_factor)
{
  switch (form_factor) {
    case RemoteNetworkDevice:
      return "RemoteNetworkDevice";
    case Speakers:
      return "Speakers";
    case LineLevel:
      return "LineLevel";
    case Microphone:
      return "Microphone";
    case Headset:
      return "Headset";
    case Handset:
      return "Handset";
    case UnknownDigitalPassthrough:
      return "UnknownDigitalPassthrough";
    case SPDIF:
      return "SPDIF";
    case DigitalAudioDisplayDevice:
      return "DigitalAudioDisplayDevice";
    case UnknownFormFactor:
    default:
      return "UnknownFormFactor";
  }
}
