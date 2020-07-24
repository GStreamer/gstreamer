/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include "config.h"
#endif

#include "AsyncOperations.h"
#include "gstwasapi2client.h"
#include "gstwasapi2util.h"
#include <initguid.h>
#include <windows.foundation.h>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <string.h>
#include <string>
#include <locale>
#include <codecvt>

using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Media::Devices;
using namespace ABI::Windows::Devices::Enumeration;

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_client_debug);
#define GST_CAT_DEFAULT gst_wasapi2_client_debug

G_END_DECLS

static void
gst_wasapi2_client_on_device_activated (GstWasapi2Client * client,
    IAudioClient3 * audio_client);

class GstWasapiDeviceActivator
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
        IActivateAudioInterfaceCompletionHandler>
{
public:
  GstWasapiDeviceActivator ()
  {
    g_weak_ref_init (&listener_, nullptr);
  }

  ~GstWasapiDeviceActivator ()
  {
    g_weak_ref_set (&listener_, nullptr);
  }

  HRESULT
  RuntimeClassInitialize (GstWasapi2Client * listener, gpointer dispatcher)
  {
    if (!listener)
      return E_INVALIDARG;

    g_weak_ref_set (&listener_, listener);

    if (dispatcher) {
      ComPtr<IInspectable> inspectable =
        reinterpret_cast<IInspectable*> (dispatcher);
      HRESULT hr;

      hr = inspectable.As (&dispatcher_);
      if (gst_wasapi2_result (hr))
        GST_INFO("Main UI dispatcher is available");
    }

    return S_OK;
  }

  STDMETHOD(ActivateCompleted)
  (IActivateAudioInterfaceAsyncOperation *async_op)
  {
    ComPtr<IAudioClient3> audio_client;
    HRESULT hr = S_OK;
    HRESULT hr_async_op = S_OK;
    ComPtr<IUnknown> audio_interface;
    GstWasapi2Client *client;

    client = (GstWasapi2Client *) g_weak_ref_get (&listener_);

    if (!client) {
      this->Release ();
      GST_WARNING ("No listener was configured");
      return S_OK;
    }

    GST_INFO_OBJECT (client, "AsyncOperation done");

    hr = async_op->GetActivateResult(&hr_async_op, &audio_interface);

    if (!gst_wasapi2_result (hr)) {
      GST_WARNING_OBJECT (client, "Failed to get activate result, hr: 0x%x", hr);
      goto done;
    }

    if (!gst_wasapi2_result (hr_async_op)) {
      GST_WARNING_OBJECT (client, "Failed to activate device");
      goto done;
    }

    hr = audio_interface.As (&audio_client);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR_OBJECT (client, "Failed to get IAudioClient3 interface");
      goto done;
    }

  done:
    /* Should call this method anyway, listener will wait this event */
    gst_wasapi2_client_on_device_activated (client, audio_client.Get());
    gst_object_unref (client);
    /* return S_OK anyway, but listener can know it's succeeded or not
     * by passed IAudioClient handle via gst_wasapi2_client_on_device_activated
     */

    this->Release ();

    return S_OK;
  }

  HRESULT
  ActivateDeviceAsync(const std::wstring &device_id)
  {
    ComPtr<IAsyncAction> async_action;
    bool run_async = false;
    HRESULT hr;

    auto work_item = Callback<Implements<RuntimeClassFlags<ClassicCom>,
        IDispatchedHandler, FtmBase>>([this, device_id]{
      ComPtr<IActivateAudioInterfaceAsyncOperation> async_op;
      HRESULT async_hr = S_OK;

      async_hr = ActivateAudioInterfaceAsync (device_id.c_str (),
            __uuidof(IAudioClient3), nullptr, this, &async_op);

      /* for debugging */
      gst_wasapi2_result (async_hr);

      return async_hr;
    });

    if (dispatcher_) {
      boolean can_now;
      hr = dispatcher_->get_HasThreadAccess (&can_now);

      if (!gst_wasapi2_result (hr))
        return hr;

      if (!can_now)
        run_async = true;
    }

    if (run_async && dispatcher_) {
      hr = dispatcher_->RunAsync (CoreDispatcherPriority_Normal,
          work_item.Get (), &async_action);
    } else {
      hr = work_item->Invoke ();
    }

    /* We should hold activator object until activation callback has executed,
     * because OS doesn't hold reference of this callback COM object.
     * otherwise access violation would happen
     * See https://docs.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync
     *
     * This reference count will be decreased by self later on callback,
     * which will be called from device worker thread.
     */
    if (gst_wasapi2_result (hr))
      this->AddRef ();

    return hr;
  }

private:
  GWeakRef listener_;
  ComPtr<ICoreDispatcher> dispatcher_;
};

typedef enum
{
  GST_WASAPI2_CLIENT_ACTIVATE_FAILED = -1,
  GST_WASAPI2_CLIENT_ACTIVATE_INIT = 0,
  GST_WASAPI2_CLIENT_ACTIVATE_WAIT,
  GST_WASAPI2_CLIENT_ACTIVATE_DONE,
} GstWasapi2ClientActivateState;

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_DEVICE_INDEX,
  PROP_DEVICE_CLASS,
  PROP_LOW_LATENCY,
  PROP_DISPATCHER,
};

#define DEFAULT_DEVICE_INDEX  -1
#define DEFAULT_DEVICE_CLASS  GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE
#define DEFAULT_LOW_LATENCY   FALSE

struct _GstWasapi2Client
{
  GstObject parent;

  GstWasapi2ClientDeviceClass device_class;
  gboolean low_latency;
  gchar *device_id;
  gchar *device_name;
  gint device_index;
  gpointer dispatcher;

  IAudioClient3 *audio_client;
  IAudioCaptureClient *audio_capture_client;
  IAudioRenderClient *audio_render_client;
  ISimpleAudioVolume *audio_volume;
  GstWasapiDeviceActivator *activator;

  WAVEFORMATEX *mix_format;
  GstCaps *supported_caps;

  HANDLE event_handle;
  HANDLE cancellable;
  gboolean opened;
  gboolean running;

  guint32 device_period;
  guint32 buffer_frame_count;

  GstAudioChannelPosition *positions;

  /* Used for capture mode */
  GstAdapter *adapter;

  GThread *thread;
  GMutex lock;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;

  /* To wait ActivateCompleted event */
  GMutex init_lock;
  GCond init_cond;
  GstWasapi2ClientActivateState activate_state;
};

GType
gst_wasapi2_client_device_class_get_type (void)
{
  static volatile GType class_type = 0;
  static const GEnumValue types[] = {
    {GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE, "Capture", "capture"},
    {GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER, "Render", "render"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&class_type)) {
    GType gtype = g_enum_register_static ("GstWasapi2ClientDeviceClass", types);
    g_once_init_leave (&class_type, gtype);
  }

  return class_type;
}

static void gst_wasapi2_client_constructed (GObject * object);
static void gst_wasapi2_client_dispose (GObject * object);
static void gst_wasapi2_client_finalize (GObject * object);
static void gst_wasapi2_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_wasapi2_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gpointer gst_wasapi2_client_thread_func (GstWasapi2Client * self);
static gboolean
gst_wasapi2_client_main_loop_running_cb (GstWasapi2Client * self);

#define gst_wasapi2_client_parent_class parent_class
G_DEFINE_TYPE (GstWasapi2Client,
    gst_wasapi2_client, GST_TYPE_OBJECT);

static void
gst_wasapi2_client_class_init (GstWasapi2ClientClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamFlags param_flags =
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_STRINGS);

  gobject_class->constructed = gst_wasapi2_client_constructed;
  gobject_class->dispose = gst_wasapi2_client_dispose;
  gobject_class->finalize = gst_wasapi2_client_finalize;
  gobject_class->get_property = gst_wasapi2_client_get_property;
  gobject_class->set_property = gst_wasapi2_client_set_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "WASAPI playback device as a GUID string", NULL, param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device Name",
          "The human-readable device name", NULL, param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index", -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_CLASS,
      g_param_spec_enum ("device-class", "Device Class",
          "Device class", GST_TYPE_WASAPI2_CLIENT_DEVICE_CLASS,
          DEFAULT_DEVICE_CLASS, param_flags));
  g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency", "Low latency",
          "Optimize all settings for lowest latency. Always safe to enable.",
          DEFAULT_LOW_LATENCY, param_flags));
  g_object_class_install_property (gobject_class, PROP_DISPATCHER,
      g_param_spec_pointer ("dispatcher", "Dispatcher",
          "ICoreDispatcher COM object to use", param_flags));
}

static void
gst_wasapi2_client_init (GstWasapi2Client * self)
{
  self->device_index = DEFAULT_DEVICE_INDEX;
  self->device_class = DEFAULT_DEVICE_CLASS;
  self->low_latency = DEFAULT_LOW_LATENCY;

  self->adapter = gst_adapter_new ();
  self->event_handle = CreateEvent (NULL, FALSE, FALSE, NULL);
  self->cancellable = CreateEvent (NULL, TRUE, FALSE, NULL);

  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  g_mutex_init (&self->init_lock);
  g_cond_init (&self->init_cond);
  self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_INIT;

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
}

static void
gst_wasapi2_client_constructed (GObject * object)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);
  ComPtr<GstWasapiDeviceActivator> activator;

  /* Create a new thread to ensure that COM thread can be MTA thread.
   * We cannot ensure whether CoInitializeEx() was called outside of here for
   * this thread or not. If it was called with non-COINIT_MULTITHREADED option,
   * we cannot update it */
  g_mutex_lock (&self->lock);
  self->thread = g_thread_new ("GstWasapi2ClientWinRT",
      (GThreadFunc) gst_wasapi2_client_thread_func, self);
  while (!self->loop || !g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_wasapi2_client_dispose (GObject * object)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  GST_DEBUG_OBJECT (self, "dispose");

  gst_clear_caps (&self->supported_caps);

  if (self->loop) {
    g_main_loop_quit (self->loop);
    g_thread_join (self->thread);
    g_main_context_unref (self->context);
    g_main_loop_unref (self->loop);

    self->thread = NULL;
    self->context = NULL;
    self->loop = NULL;
  }

  g_clear_object (&self->adapter);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wasapi2_client_finalize (GObject * object)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  g_free (self->device_id);
  g_free (self->device_name);

  g_free (self->positions);

  CoTaskMemFree (self->mix_format);
  CloseHandle (self->event_handle);
  CloseHandle (self->cancellable);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  g_mutex_clear (&self->init_lock);
  g_cond_clear (&self->init_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, self->device_id);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->device_index);
      break;
    case PROP_DEVICE_CLASS:
      g_value_set_enum (value, self->device_class);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, self->low_latency);
      break;
    case PROP_DISPATCHER:
      g_value_set_pointer (value, self->dispatcher);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi2_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (self->device_id);
      self->device_id = g_value_dup_string (value);
      break;
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      break;
    case PROP_DEVICE_INDEX:
      self->device_index = g_value_get_int (value);
      break;
    case PROP_DEVICE_CLASS:
      self->device_class =
          (GstWasapi2ClientDeviceClass) g_value_get_enum (value);
      break;
    case PROP_LOW_LATENCY:
      self->low_latency = g_value_get_boolean (value);
      break;
    case PROP_DISPATCHER:
      self->dispatcher = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_wasapi2_client_main_loop_running_cb (GstWasapi2Client * self)
{
  GST_DEBUG_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

static void
gst_wasapi2_client_on_device_activated (GstWasapi2Client * self,
    IAudioClient3 * audio_client)
{
  GST_INFO_OBJECT (self, "Device activated");

  g_mutex_lock (&self->init_lock);
  if (audio_client) {
    audio_client->AddRef();
    self->audio_client = audio_client;
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_DONE;
  } else {
    GST_WARNING_OBJECT (self, "IAudioClient is unavailable");
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;
  }
  g_cond_broadcast (&self->init_cond);
  g_mutex_unlock (&self->init_lock);
}

static std::string
convert_wstring_to_string (const std::wstring &wstr)
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

  return converter.to_bytes (wstr.c_str());
}

static std::string
convert_hstring_to_string (HString * hstr)
{
  const wchar_t *raw_hstr;

  if (!hstr)
    return std::string();

  raw_hstr = hstr->GetRawBuffer (nullptr);
  if (!raw_hstr)
    return std::string();

  return convert_wstring_to_string (std::wstring (raw_hstr));
}

static std::wstring
gst_wasapi2_client_get_default_device_id (GstWasapi2Client * self)
{
  HRESULT hr;
  PWSTR default_device_id_wstr = nullptr;

  if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE)
    hr = StringFromIID (DEVINTERFACE_AUDIO_CAPTURE, &default_device_id_wstr);
  else
    hr = StringFromIID (DEVINTERFACE_AUDIO_RENDER, &default_device_id_wstr);

  if (!gst_wasapi2_result (hr))
    return std::wstring();

  std::wstring ret = std::wstring (default_device_id_wstr);
  CoTaskMemFree (default_device_id_wstr);

  return ret;
}

static gboolean
gst_wasapi2_client_activate_async (GstWasapi2Client * self,
    GstWasapiDeviceActivator * activator)
{
  HRESULT hr;
  ComPtr<IDeviceInformationStatics> device_info_static;
  ComPtr<IAsyncOperation<DeviceInformationCollection*>> async_op;
  ComPtr<IVectorView<DeviceInformation*>> device_list;
  HStringReference hstr_device_info =
      HStringReference(RuntimeClass_Windows_Devices_Enumeration_DeviceInformation);
  DeviceClass device_class;
  unsigned int count = 0;
  gint device_index = 0;
  std::wstring default_device_id_wstring;
  std::string default_device_id;
  std::wstring target_device_id_wstring;
  std::string target_device_id;
  std::string target_device_name;
  gboolean use_default_device = FALSE;

  GST_INFO_OBJECT (self,
      "requested device info, device-class: %s, device: %s, device-index: %d",
       self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE ? "capture" :
       "render", GST_STR_NULL (self->device_id), self->device_index);

  if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE) {
    device_class = DeviceClass::DeviceClass_AudioCapture;
  } else {
    device_class = DeviceClass::DeviceClass_AudioRender;
  }

  default_device_id_wstring = gst_wasapi2_client_get_default_device_id (self);
  if (default_device_id_wstring.empty ()) {
    GST_WARNING_OBJECT (self, "Couldn't get default device id");
    goto failed;
  }

  default_device_id = convert_wstring_to_string (default_device_id_wstring);
  GST_DEBUG_OBJECT (self, "Default device id: %s", default_device_id.c_str ());

  /* When
   * 1) default device was requested or
   * 2) no explicitly requested device or
   * 3) requested device string id is null but device index is zero
   * will use default device
   *
   * Note that default device is much preferred
   * See https://docs.microsoft.com/en-us/windows/win32/coreaudio/automatic-stream-routing
   */
  if (self->device_id &&
      g_ascii_strcasecmp (self->device_id, default_device_id.c_str()) == 0) {
    GST_DEBUG_OBJECT (self, "Default device was requested");
    use_default_device = TRUE;
  } else if (self->device_index < 0 && !self->device_id) {
    GST_DEBUG_OBJECT (self,
        "No device was explicitly requested, use default device");
    use_default_device = TRUE;
  } else if (!self->device_id && self->device_index == 0) {
    GST_DEBUG_OBJECT (self, "device-index == zero means default device");
    use_default_device = TRUE;
  }

  if (use_default_device) {
    target_device_id_wstring = default_device_id_wstring;
    target_device_id = default_device_id;
    if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE)
      target_device_name = "Default Audio Capture Device";
    else
      target_device_name = "Default Audio Render Device";
    goto activate;
  }

  hr = GetActivationFactory (hstr_device_info.Get(), &device_info_static);
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = device_info_static->FindAllAsyncDeviceClass (device_class, &async_op);
  device_info_static.Reset ();
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = SyncWait<DeviceInformationCollection*>(async_op.Get ());
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = async_op->GetResults (&device_list);
  async_op.Reset ();
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = device_list->get_Size (&count);
  if (!gst_wasapi2_result (hr))
    goto failed;

  if (count == 0) {
    GST_WARNING_OBJECT (self, "No available device");
    goto failed;
  }

  /* device_index 0 will be assigned for default device
   * so the number of available device is count + 1 (for default device) */
  if (self->device_index >= 0 && self->device_index > (gint) count) {
    GST_WARNING_OBJECT (self, "Device index %d is unavailable",
        self->device_index);
    goto failed;
  }

  GST_DEBUG_OBJECT (self, "Available device count: %d", count);

  /* zero is for default device */
  device_index = 1;
  for (unsigned int i = 0; i < count; i++) {
    ComPtr<IDeviceInformation> device_info;
    HString id;
    HString name;
    boolean b_value;
    std::string cur_device_id;
    std::string cur_device_name;

    hr = device_list->GetAt (i, &device_info);
    if (!gst_wasapi2_result (hr))
      continue;

    hr = device_info->get_IsEnabled (&b_value);
    if (!gst_wasapi2_result (hr))
      continue;

    /* select only enabled device */
    if (!b_value) {
      GST_DEBUG_OBJECT (self, "Device index %d is disabled", i);
      continue;
    }

    /* To ensure device id and device name are available,
     * will query this later again once target device is determined */
    hr = device_info->get_Id (id.GetAddressOf());
    if (!gst_wasapi2_result (hr))
      continue;

    if (!id.IsValid()) {
      GST_WARNING_OBJECT (self, "Device index %d has invalid id", i);
      continue;
    }

    hr = device_info->get_Name (name.GetAddressOf());
    if (!gst_wasapi2_result (hr))
      continue;

    if (!name.IsValid ()) {
      GST_WARNING_OBJECT (self, "Device index %d has invalid name", i);
      continue;
    }

    cur_device_id = convert_hstring_to_string (&id);
    if (cur_device_id.empty ()) {
      GST_WARNING_OBJECT (self, "Device index %d has empty id", i);
      continue;
    }

    cur_device_name = convert_hstring_to_string (&name);
    if (cur_device_name.empty ()) {
      GST_WARNING_OBJECT (self, "Device index %d has empty device name", i);
      continue;
    }

    GST_DEBUG_OBJECT (self, "device [%d] id: %s, name: %s",
        device_index, cur_device_id.c_str(), cur_device_name.c_str());

    if (self->device_id &&
        g_ascii_strcasecmp (self->device_id, cur_device_id.c_str ()) == 0) {
      GST_INFO_OBJECT (self,
          "Device index %d has matching device id %s", device_index,
          cur_device_id.c_str ());
      target_device_id_wstring = id.GetRawBuffer (nullptr);
      target_device_id = cur_device_id;
      target_device_name = cur_device_name;
      break;
    }

    if (self->device_index >= 0 && self->device_index == device_index) {
      GST_INFO_OBJECT (self, "Select device index %d, device id %s",
          device_index, cur_device_id.c_str ());
      target_device_id_wstring = id.GetRawBuffer (nullptr);
      target_device_id = cur_device_id;
      target_device_name = cur_device_name;
      break;
    }

    /* count only available devices */
    device_index++;
  }

  if (target_device_id_wstring.empty ()) {
    GST_WARNING_OBJECT (self, "Couldn't find target device");
    goto failed;
  }

activate:
  /* fill device id and name */
  g_free (self->device_id);
  self->device_id = g_strdup (target_device_id.c_str());

  g_free (self->device_name);
  self->device_name = g_strdup (target_device_name.c_str ());

  self->device_index = device_index;

  hr = activator->ActivateDeviceAsync (target_device_id_wstring);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Failed to activate device");
    goto failed;
  }

  g_mutex_lock (&self->lock);
  if (self->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_INIT)
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_WAIT;
  g_mutex_unlock (&self->lock);

  return TRUE;

failed:
  self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;

  return FALSE;
}

static const gchar *
activate_state_to_string (GstWasapi2ClientActivateState state)
{
  switch (state) {
    case GST_WASAPI2_CLIENT_ACTIVATE_FAILED:
      return "FAILED";
    case GST_WASAPI2_CLIENT_ACTIVATE_INIT:
      return "INIT";
    case GST_WASAPI2_CLIENT_ACTIVATE_WAIT:
      return "WAIT";
    case GST_WASAPI2_CLIENT_ACTIVATE_DONE:
      return "DONE";
  }

  g_assert_not_reached ();

  return "Undefined";
}

static gpointer
gst_wasapi2_client_thread_func (GstWasapi2Client * self)
{
  RoInitializeWrapper initialize (RO_INIT_MULTITHREADED);
  GSource *source;
  HRESULT hr;
  ComPtr<GstWasapiDeviceActivator> activator;

  hr = MakeAndInitialize<GstWasapiDeviceActivator> (&activator,
      self, self->dispatcher);
  if (!gst_wasapi2_result (hr)) {
    GST_ERROR_OBJECT (self, "Could not create activator object");
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;
    goto run_loop;
  }

  gst_wasapi2_client_activate_async (self, activator.Get ());

  if (!self->dispatcher) {
    /* In case that dispatcher is unavailable, wait activation synchroniously */
    GST_DEBUG_OBJECT (self, "Wait device activation");
    gst_wasapi2_client_ensure_activation (self);
    GST_DEBUG_OBJECT (self, "Device activation result %s",
        activate_state_to_string (self->activate_state));
  }

run_loop:
  g_main_context_push_thread_default (self->context);

  source = g_idle_source_new ();
  g_source_set_callback (source,
      (GSourceFunc) gst_wasapi2_client_main_loop_running_cb, self, NULL);
  g_source_attach (source, self->context);
  g_source_unref (source);

  GST_DEBUG_OBJECT (self, "Starting main loop");
  g_main_loop_run (self->loop);
  GST_DEBUG_OBJECT (self, "Stopped main loop");

  g_main_context_pop_thread_default (self->context);

  gst_wasapi2_client_stop (self);

  if (self->audio_volume) {
    self->audio_volume->Release ();
    self->audio_volume = NULL;
  }

  if (self->audio_render_client) {
    self->audio_render_client->Release ();
    self->audio_render_client = NULL;
  }

  if (self->audio_capture_client) {
    self->audio_capture_client->Release ();
    self->audio_capture_client = NULL;
  }

  if (self->audio_client) {
    self->audio_client->Release ();
    self->audio_client = NULL;
  }

  /* Reset explicitly to ensure that it happens before
   * RoInitializeWrapper dtor is called */
  activator.Reset ();

  GST_DEBUG_OBJECT (self, "Exit thread function");

  return NULL;
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
    if (IsEqualGUID (format->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
      fmt = gst_audio_format_build_integer (TRUE, G_LITTLE_ENDIAN,
          format->Format.wBitsPerSample, format->Samples.wValidBitsPerSample);
    } else if (IsEqualGUID (format->SubFormat,
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
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

/* Parse WAVEFORMATEX to get the gstreamer channel mask, and the wasapi channel
 * positions so GstAudioRingbuffer can reorder the audio data to match the
 * gstreamer channel order. */
static guint64
gst_wasapi_util_waveformatex_to_channel_mask (WAVEFORMATEXTENSIBLE * format,
    GstAudioChannelPosition ** out_position)
{
  int ii, ch;
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
  for (ii = 0, ch = 0; ii < G_N_ELEMENTS (wasapi_to_gst_pos) && ch < nChannels;
      ii++) {
    if (!(dwChannelMask & wasapi_to_gst_pos[ii].wasapi_pos))
      /* no match, try next */
      continue;
    mask |= G_GUINT64_CONSTANT (1) << wasapi_to_gst_pos[ii].gst_pos;
    pos[ch++] = wasapi_to_gst_pos[ii].gst_pos;
  }

  /* XXX: Warn if some channel masks couldn't be mapped? */

  GST_DEBUG ("Converted WASAPI mask 0x%" G_GINT64_MODIFIER "x -> 0x%"
      G_GINT64_MODIFIER "x", (guint64) dwChannelMask, (guint64) mask);

out:
  if (out_position)
    *out_position = pos;
  return mask;
}

static gboolean
gst_wasapi2_util_parse_waveformatex (WAVEFORMATEXTENSIBLE * format,
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
        "rate", G_TYPE_INT, format->Format.nSamplesPerSec, NULL);

    if (channel_mask) {
      gst_structure_set (s,
          "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
    }
  }

  return TRUE;
}

GstCaps *
gst_wasapi2_client_get_caps (GstWasapi2Client * client)
{
  WAVEFORMATEX *format = NULL;
  static GstStaticCaps static_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);
  GstCaps *scaps;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), NULL);

  if (client->supported_caps)
    return gst_caps_ref (client->supported_caps);

  if (!client->audio_client) {
    GST_WARNING_OBJECT (client, "IAudioClient3 wasn't configured");
    return NULL;
  }

  CoTaskMemFree (client->mix_format);
  client->mix_format = nullptr;

  g_clear_pointer (&client->positions, g_free);

  hr = client->audio_client->GetMixFormat (&format);
  if (!gst_wasapi2_result (hr))
    return NULL;

  scaps = gst_static_caps_get (&static_caps);
  gst_wasapi2_util_parse_waveformatex ((WAVEFORMATEXTENSIBLE *) format,
      scaps, &client->supported_caps, &client->positions);
  gst_caps_unref (scaps);

  client->mix_format = format;

  if (!client->supported_caps) {
    GST_ERROR_OBJECT (client, "No caps from subclass");
    return NULL;
  }

  return gst_caps_ref (client->supported_caps);
}

static gboolean
gst_wasapi2_client_initialize_audio_client3 (GstWasapi2Client * self)
{
  HRESULT hr;
  UINT32 default_period, fundamental_period, min_period, max_period;
  DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  WAVEFORMATEX *format = NULL;
  UINT32 period;
  gboolean ret = FALSE;
  IAudioClient3 *audio_client = self->audio_client;

  hr = audio_client->GetSharedModeEnginePeriod (self->mix_format,
      &default_period, &fundamental_period, &min_period, &max_period);
  if (!gst_wasapi2_result (hr))
    goto done;

  GST_INFO_OBJECT (self, "Using IAudioClient3, default period %d frames, "
      "fundamental period %d frames, minimum period %d frames, maximum period "
      "%d frames", default_period, fundamental_period, min_period, max_period);

  hr = audio_client->InitializeSharedAudioStream (stream_flags, min_period,
      self->mix_format, nullptr);

  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Failed to initialize IAudioClient3");
    goto done;
  }

  /* query period again to be ensured */
  hr = audio_client->GetCurrentSharedModeEnginePeriod (&format, &period);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Failed to get current period");
    goto done;
  }

  self->device_period = period;
  ret = TRUE;

done:
  CoTaskMemFree (format);

  return ret;
}

static void
gst_wasapi2_util_get_best_buffer_sizes (GstAudioRingBufferSpec * spec,
    REFERENCE_TIME default_period, REFERENCE_TIME min_period,
    REFERENCE_TIME * ret_period, REFERENCE_TIME * ret_buffer_duration)
{
  REFERENCE_TIME use_period, use_buffer;

  /* Shared mode always runs at the default period, so if we want a larger
   * period (for lower CPU usage), we do it as a multiple of that */
  use_period = default_period;

  /* Ensure that the period (latency_time) used is an integral multiple of
   * either the default period or the minimum period */
  use_period = use_period * MAX ((spec->latency_time * 10) / use_period, 1);

  /* Ask WASAPI to create a software ringbuffer of at least this size; it may
   * be larger so the actual buffer time may be different, which is why after
   * initialization we read the buffer duration actually in-use and set
   * segsize/segtotal from that. */
  use_buffer = spec->buffer_time * 10;
  /* Has to be at least twice the period */
  if (use_buffer < 2 * use_period)
    use_buffer = 2 * use_period;

  *ret_period = use_period;
  *ret_buffer_duration = use_buffer;
}

static gboolean
gst_wasapi2_client_initialize_audio_client (GstWasapi2Client * self,
    GstAudioRingBufferSpec * spec)
{
  REFERENCE_TIME default_period, min_period;
  REFERENCE_TIME device_period, device_buffer_duration;
  guint rate;
  DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  HRESULT hr;
  IAudioClient3 *audio_client = self->audio_client;

  hr = audio_client->GetDevicePeriod (&default_period, &min_period);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Couldn't get device period info");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "wasapi2 default period: %" G_GINT64_FORMAT
      ", min period: %" G_GINT64_FORMAT, default_period, min_period);

  rate = GST_AUDIO_INFO_RATE (&spec->info);

  if (self->low_latency) {
    device_period = default_period;
    /* this should be same as hnsPeriodicity
     * when AUDCLNT_STREAMFLAGS_EVENTCALLBACK is used
     * And in case of shared mode, hnsPeriodicity should be zero, so
     * this value should be zero as well */
    device_buffer_duration = 0;
  } else {
    /* Clamp values to integral multiples of an appropriate period */
    gst_wasapi2_util_get_best_buffer_sizes (spec,
        default_period, min_period, &device_period, &device_buffer_duration);
  }

  hr = audio_client->Initialize (AUDCLNT_SHAREMODE_SHARED, stream_flags,
      device_buffer_duration,
      /* This must always be 0 in shared mode */
      0,
      self->mix_format, nullptr);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Couldn't initialize audioclient");
    return FALSE;
  }

  /* device_period can be a non-power-of-10 value so round while converting */
  self->device_period =
      gst_util_uint64_scale_round (device_period, rate * 100, GST_SECOND);

  return TRUE;
}

gboolean
gst_wasapi2_client_open (GstWasapi2Client * client, GstAudioRingBufferSpec * spec,
    GstAudioRingBuffer * buf)
{
  HRESULT hr;
  REFERENCE_TIME latency_rt;
  guint bpf, rate;
  IAudioClient3 *audio_client;
  ComPtr<ISimpleAudioVolume> audio_volume;
  gboolean initialized = FALSE;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  /* FIXME: Once IAudioClient3 was initialized, we may need to re-open
   * IAudioClient3 in order to handle audio format change */
  if (client->opened) {
    GST_INFO_OBJECT (client, "IAudioClient3 object is initialized already");
    return TRUE;
  }

  audio_client = client->audio_client;

  if (!audio_client) {
    GST_ERROR_OBJECT (client, "IAudioClient3 object wasn't configured");
    return FALSE;
  }

  if (!client->mix_format) {
    GST_ERROR_OBJECT (client, "Unknown mix format");
    return FALSE;
  }

  /* Only use audioclient3 when low-latency is requested because otherwise
   * very slow machines and VMs with 1 CPU allocated will get glitches:
   * https://bugzilla.gnome.org/show_bug.cgi?id=794497 */
  if (client->low_latency)
    initialized = gst_wasapi2_client_initialize_audio_client3 (client);

  /* Try again if IAudioClinet3 API is unavailable.
   * NOTE: IAudioClinet3:: methods might not be available for default device
   * NOTE: The default device is a special device which is needed for supporting
   * automatic stream routing
   * https://docs.microsoft.com/en-us/windows/win32/coreaudio/automatic-stream-routing
   */
  if (!initialized)
    initialized = gst_wasapi2_client_initialize_audio_client (client, spec);

  if (!initialized) {
    GST_ERROR_OBJECT (client, "Failed to initialize audioclient");
    return FALSE;
  }

  bpf = GST_AUDIO_INFO_BPF (&spec->info);
  rate = GST_AUDIO_INFO_RATE (&spec->info);

  /* Total size in frames of the allocated buffer that we will read from */
  hr = audio_client->GetBufferSize (&client->buffer_frame_count);
  if (!gst_wasapi2_result (hr)) {
    return FALSE;
  }

  GST_INFO_OBJECT (client, "buffer size is %i frames, device period is %i "
      "frames, bpf is %i bytes, rate is %i Hz", client->buffer_frame_count,
      client->device_period, bpf, rate);

  /* Actual latency-time/buffer-time will be different now */
  spec->segsize = client->device_period * bpf;

  /* We need a minimum of 2 segments to ensure glitch-free playback */
  spec->segtotal = MAX (client->buffer_frame_count * bpf / spec->segsize, 2);

  GST_INFO_OBJECT (client, "segsize is %i, segtotal is %i", spec->segsize,
      spec->segtotal);

  /* Get WASAPI latency for logging */
  hr = audio_client->GetStreamLatency (&latency_rt);
  if (!gst_wasapi2_result (hr)) {
    return FALSE;
  }

  GST_INFO_OBJECT (client, "wasapi2 stream latency: %" G_GINT64_FORMAT " (%"
      G_GINT64_FORMAT " ms)", latency_rt, latency_rt / 10000);

  /* Set the event handler which will trigger read/write */
  hr = audio_client->SetEventHandle (client->event_handle);
  if (!gst_wasapi2_result (hr))
    return FALSE;

  if (client->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER) {
    ComPtr<IAudioRenderClient> render_client;

    hr = audio_client->GetService (IID_PPV_ARGS (&render_client));
    if (!gst_wasapi2_result (hr))
      return FALSE;

    client->audio_render_client = render_client.Detach ();
  } else {
    ComPtr<IAudioCaptureClient> capture_client;

    hr = audio_client->GetService (IID_PPV_ARGS (&capture_client));
    if (!gst_wasapi2_result (hr))
      return FALSE;

    client->audio_capture_client = capture_client.Detach ();
  }

  hr = audio_client->GetService (IID_PPV_ARGS (&audio_volume));
  if (!gst_wasapi2_result (hr))
    return FALSE;

  client->audio_volume = audio_volume.Detach ();

  gst_audio_ring_buffer_set_channel_positions (buf, client->positions);

  client->opened = TRUE;

  return TRUE;
}

/* Get the empty space in the buffer that we have to write to */
static gint
gst_wasapi2_client_get_can_frames (GstWasapi2Client * self)
{
  HRESULT hr;
  UINT32 n_frames_padding;
  IAudioClient3 *audio_client = self->audio_client;

  if (!audio_client) {
    GST_WARNING_OBJECT (self, "IAudioClient3 wasn't configured");
    return -1;
  }

  /* Frames the card hasn't rendered yet */
  hr = audio_client->GetCurrentPadding (&n_frames_padding);
  if (!gst_wasapi2_result (hr))
    return -1;

  GST_LOG_OBJECT (self, "%d unread frames (padding)", n_frames_padding);

  /* We can write out these many frames */
  return self->buffer_frame_count - n_frames_padding;
}

gboolean
gst_wasapi2_client_start (GstWasapi2Client * client)
{
  HRESULT hr;
  IAudioClient3 *audio_client;
  WAVEFORMATEX *mix_format;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  audio_client = client->audio_client;
  mix_format = client->mix_format;

  if (!audio_client) {
    GST_ERROR_OBJECT (client, "IAudioClient3 object wasn't configured");
    return FALSE;
  }

  if (!mix_format) {
    GST_ERROR_OBJECT (client, "Unknown MixFormat");
    return FALSE;
  }

  if (client->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE &&
      !client->audio_capture_client) {
    GST_ERROR_OBJECT (client, "IAudioCaptureClient wasn't configured");
    return FALSE;
  }

  if (client->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER &&
      !client->audio_render_client) {
    GST_ERROR_OBJECT (client, "IAudioRenderClient wasn't configured");
    return FALSE;
  }

  ResetEvent (client->cancellable);

  if (client->running) {
    GST_WARNING_OBJECT (client, "IAudioClient3 is running already");
    return TRUE;
  }

  /* To avoid start-up glitches, before starting the streaming, we fill the
   * buffer with silence as recommended by the documentation:
   * https://msdn.microsoft.com/en-us/library/windows/desktop/dd370879%28v=vs.85%29.aspx */
  if (client->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER) {
    IAudioRenderClient *render_client = client->audio_render_client;
    gint n_frames, len;
    BYTE *dst = NULL;

    n_frames = gst_wasapi2_client_get_can_frames (client);
    if (n_frames < 1) {
      GST_ERROR_OBJECT (client,
          "should have more than %i frames to write", n_frames);
      return FALSE;
    }

    len = n_frames * mix_format->nBlockAlign;

    hr = render_client->GetBuffer (n_frames, &dst);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR_OBJECT (client, "Couldn't get buffer");
      return FALSE;
    }

    GST_DEBUG_OBJECT (client, "pre-wrote %i bytes of silence", len);

    hr = render_client->ReleaseBuffer (n_frames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR_OBJECT (client, "Couldn't release buffer");
      return FALSE;
    }
  }

  hr = audio_client->Start ();
  client->running = gst_wasapi2_result (hr);
  gst_adapter_clear (client->adapter);

  return client->running;
}

gboolean
gst_wasapi2_client_stop (GstWasapi2Client * client)
{
  HRESULT hr;
  IAudioClient3 *audio_client;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  audio_client = client->audio_client;

  if (!client->running) {
    GST_DEBUG_OBJECT (client, "We are not running now");
    return TRUE;
  }

  if (!client->audio_client) {
    GST_ERROR_OBJECT (client, "IAudioClient3 object wasn't configured");
    return FALSE;
  }

  client->running = FALSE;
  SetEvent (client->cancellable);

  hr = audio_client->Stop ();
  if (!gst_wasapi2_result (hr))
    return FALSE;

  /* reset state for reuse case */
  hr = audio_client->Reset ();
  return gst_wasapi2_result (hr);
}

gint
gst_wasapi2_client_read (GstWasapi2Client * client, gpointer data, guint length)
{
  IAudioCaptureClient *capture_client;
  WAVEFORMATEX *mix_format;
  HRESULT hr;
  BYTE *from = NULL;
  guint wanted = length;
  guint bpf;
  DWORD flags;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);
  g_return_val_if_fail (client->audio_capture_client != NULL, -1);
  g_return_val_if_fail (client->mix_format != NULL, -1);

  capture_client = client->audio_capture_client;
  mix_format = client->mix_format;

  if (!client->running) {
    GST_ERROR_OBJECT (client, "client is not running now");
    return -1;
  }

  /* If we've accumulated enough data, return it immediately */
  if (gst_adapter_available (client->adapter) >= wanted) {
    memcpy (data, gst_adapter_map (client->adapter, wanted), wanted);
    gst_adapter_flush (client->adapter, wanted);
    GST_DEBUG_OBJECT (client, "Adapter has enough data, returning %i", wanted);
    return wanted;
  }

  bpf = mix_format->nBlockAlign;

  while (wanted > 0) {
    DWORD dwWaitResult;
    guint got_frames, avail_frames, n_frames, want_frames, read_len;
    HANDLE event_handle[2];

    event_handle[0] = client->event_handle;
    event_handle[1] = client->cancellable;

    /* Wait for data to become available */
    dwWaitResult = WaitForMultipleObjects (2, event_handle, FALSE, INFINITE);
    if (dwWaitResult != WAIT_OBJECT_0 && dwWaitResult != WAIT_OBJECT_0 + 1) {
      GST_ERROR_OBJECT (client, "Error waiting for event handle: %x",
          (guint) dwWaitResult);
      return -1;
    }

    if (!client->running) {
      GST_DEBUG_OBJECT (client, "Cancelled");
      return -1;
    }

    hr = capture_client->GetBuffer (&from, &got_frames, &flags, nullptr,
        nullptr);
    if (!gst_wasapi2_result (hr)) {
      if (hr == AUDCLNT_S_BUFFER_EMPTY) {
        GST_INFO_OBJECT (client, "Client buffer is empty, retry");
        return 0;
      }

      GST_ERROR_OBJECT (client, "Couldn't get buffer from capture client");
      return -1;
    }

    if (got_frames == 0) {
      GST_DEBUG_OBJECT (client, "No buffer to read");
      capture_client->ReleaseBuffer (got_frames);
      return 0;
    }

    if (G_UNLIKELY (flags != 0)) {
      /* https://docs.microsoft.com/en-us/windows/win32/api/audioclient/ne-audioclient-_audclnt_bufferflags */
      if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
        GST_DEBUG_OBJECT (client, "WASAPI reported discontinuity (glitch?)");
      if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
        GST_DEBUG_OBJECT (client, "WASAPI reported a timestamp error");
    }

    /* Copy all the frames we got into the adapter, and then extract at most
     * @wanted size of frames from it. This helps when ::GetBuffer returns more
     * data than we can handle right now. */
    {
      GstBuffer *tmp = gst_buffer_new_allocate (NULL, got_frames * bpf, NULL);
      /* If flags has AUDCLNT_BUFFERFLAGS_SILENT, we will ignore the actual
       * data and write out silence, see:
       * https://docs.microsoft.com/en-us/windows/win32/api/audioclient/ne-audioclient-_audclnt_bufferflags */
      if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        memset (from, 0, got_frames * bpf);
      gst_buffer_fill (tmp, 0, from, got_frames * bpf);
      gst_adapter_push (client->adapter, tmp);
    }

    /* Release all captured buffers; we copied them above */
    hr = capture_client->ReleaseBuffer (got_frames);
    from = NULL;
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR_OBJECT (client, "Failed to release buffer");
      return -1;
    }

    want_frames = wanted / bpf;
    avail_frames = gst_adapter_available (client->adapter) / bpf;

    /* Only copy data that will fit into the allocated buffer of size @length */
    n_frames = MIN (avail_frames, want_frames);
    read_len = n_frames * bpf;

    if (read_len == 0) {
      GST_WARNING_OBJECT (client, "No data to read");
      return 0;
    }

    GST_LOG_OBJECT (client, "frames captured: %d (%d bytes), "
        "can read: %d (%d bytes), will read: %d (%d bytes), "
        "adapter has: %d (%d bytes)", got_frames, got_frames * bpf, want_frames,
        wanted, n_frames, read_len, avail_frames, avail_frames * bpf);

    memcpy (data, gst_adapter_map (client->adapter, read_len), read_len);
    gst_adapter_flush (client->adapter, read_len);
    wanted -= read_len;
  }

  return length;
}

gint
gst_wasapi2_client_write (GstWasapi2Client * client, gpointer data,
    guint length)
{
  IAudioRenderClient *render_client;
  WAVEFORMATEX *mix_format;
  HRESULT hr;
  BYTE *dst = nullptr;
  DWORD dwWaitResult;
  guint can_frames, have_frames, n_frames, write_len = 0;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), -1);
  g_return_val_if_fail (client->audio_render_client != NULL, -1);
  g_return_val_if_fail (client->mix_format != NULL, -1);

  if (!client->running) {
    GST_WARNING_OBJECT (client, "client is not running now");
    return -1;
  }

  render_client = client->audio_render_client;
  mix_format = client->mix_format;

  /* We have N frames to be written out */
  have_frames = length / (mix_format->nBlockAlign);

  /* In shared mode we can write parts of the buffer, so only wait
    * in case we can't write anything */
  can_frames = gst_wasapi2_client_get_can_frames (client);
  if (can_frames < 0) {
    GST_ERROR_OBJECT (client, "Error getting frames to write to");
    return -1;
  }

  if (can_frames == 0) {
    HANDLE event_handle[2];

    event_handle[0] = client->event_handle;
    event_handle[1] = client->cancellable;

    dwWaitResult = WaitForMultipleObjects (2, event_handle, FALSE, INFINITE);
    if (dwWaitResult != WAIT_OBJECT_0 && dwWaitResult != WAIT_OBJECT_0 + 1) {
      GST_ERROR_OBJECT (client, "Error waiting for event handle: %x",
          (guint) dwWaitResult);
      return -1;
    }

    if (!client->running) {
      GST_DEBUG_OBJECT (client, "Cancelled");
      return -1;
    }

    can_frames = gst_wasapi2_client_get_can_frames (client);
    if (can_frames < 0) {
      GST_ERROR_OBJECT (client, "Error getting frames to write to");
      return -1;
    }
  }

  /* We will write out these many frames, and this much length */
  n_frames = MIN (can_frames, have_frames);
  write_len = n_frames * mix_format->nBlockAlign;

  GST_LOG_OBJECT (client, "total: %d, have_frames: %d (%d bytes), "
      "can_frames: %d, will write: %d (%d bytes)", client->buffer_frame_count,
      have_frames, length, can_frames, n_frames, write_len);

  hr = render_client->GetBuffer (n_frames, &dst);
  if (!gst_wasapi2_result (hr)) {
    GST_ERROR_OBJECT (client, "Couldn't get buffer from client");
    return -1;
  }

  memcpy (dst, data, write_len);
  hr = render_client->ReleaseBuffer (n_frames, 0);

  return write_len;
}

guint
gst_wasapi2_client_delay (GstWasapi2Client * client)
{
  HRESULT hr;
  UINT32 delay;
  IAudioClient3 *audio_client;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), 0);

  audio_client = client->audio_client;

  if (!audio_client) {
    GST_WARNING_OBJECT (client, "IAudioClient3 wasn't configured");
    return 0;
  }

  hr = audio_client->GetCurrentPadding (&delay);
  if (!gst_wasapi2_result (hr))
    return 0;

  return delay;
}

gboolean
gst_wasapi2_client_set_mute (GstWasapi2Client * client, gboolean mute)
{
  HRESULT hr;
  ISimpleAudioVolume *audio_volume;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  audio_volume = client->audio_volume;

  if (!audio_volume) {
    GST_WARNING_OBJECT (client, "ISimpleAudioVolume object wasn't configured");
    return FALSE;
  }

  hr = audio_volume->SetMute (mute, nullptr);
  GST_DEBUG_OBJECT (client, "Set mute %s, hr: 0x%x",
      mute ? "enabled" : "disabled", (gint) hr);

  return gst_wasapi2_result (hr);
}

gboolean
gst_wasapi2_client_get_mute (GstWasapi2Client * client, gboolean * mute)
{
  HRESULT hr;
  ISimpleAudioVolume *audio_volume;
  BOOL current_mute = FALSE;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);
  g_return_val_if_fail (mute != NULL, FALSE);

  audio_volume = client->audio_volume;

  if (!audio_volume) {
    GST_WARNING_OBJECT (client, "ISimpleAudioVolume object wasn't configured");
    return FALSE;
  }

  hr = audio_volume->GetMute (&current_mute);
  if (!gst_wasapi2_result (hr))
    return FALSE;

  *mute = (gboolean) current_mute;

  return TRUE;
}

gboolean
gst_wasapi2_client_set_volume (GstWasapi2Client * client, gfloat volume)
{
  HRESULT hr;
  ISimpleAudioVolume *audio_volume;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);
  g_return_val_if_fail (volume >= 0 && volume <= 1.0, FALSE);

  audio_volume = client->audio_volume;

  if (!audio_volume) {
    GST_WARNING_OBJECT (client, "ISimpleAudioVolume object wasn't configured");
    return FALSE;
  }

  hr = audio_volume->SetMasterVolume (volume, nullptr);
  GST_DEBUG_OBJECT (client, "Set volume %.2f hr: 0x%x", volume, (gint) hr);

  return gst_wasapi2_result (hr);
}

gboolean
gst_wasapi2_client_get_volume (GstWasapi2Client * client, gfloat * volume)
{
  HRESULT hr;
  ISimpleAudioVolume *audio_volume;
  float current_volume = FALSE;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);
  g_return_val_if_fail (volume != NULL, FALSE);

  audio_volume = client->audio_volume;

  if (!audio_volume) {
    GST_WARNING_OBJECT (client, "ISimpleAudioVolume object wasn't configured");
    return FALSE;
  }

  hr = audio_volume->GetMasterVolume (&current_volume);
  if (!gst_wasapi2_result (hr))
    return FALSE;

  *volume = current_volume;

  return TRUE;
}

gboolean
gst_wasapi2_client_ensure_activation (GstWasapi2Client * client)
{
  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  /* should not happen */
  g_assert (client->activate_state != GST_WASAPI2_CLIENT_ACTIVATE_INIT);

  g_mutex_lock (&client->init_lock);
  while (client->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_WAIT)
    g_cond_wait (&client->init_cond, &client->init_lock);
  g_mutex_unlock (&client->init_lock);

  return client->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_DONE;
}

static HRESULT
find_dispatcher (ICoreDispatcher ** dispatcher)
{
  HStringReference hstr_core_app =
      HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication);
  HRESULT hr;

  ComPtr<ICoreApplication> core_app;
  hr = GetActivationFactory (hstr_core_app.Get(), &core_app);
  if (FAILED (hr))
    return hr;

  ComPtr<ICoreApplicationView> core_app_view;
  hr = core_app->GetCurrentView (&core_app_view);
  if (FAILED (hr))
    return hr;

  ComPtr<ICoreWindow> core_window;
  hr = core_app_view->get_CoreWindow (&core_window);
  if (FAILED (hr))
    return hr;

  return core_window->get_Dispatcher (dispatcher);
}

GstWasapi2Client *
gst_wasapi2_client_new (GstWasapi2ClientDeviceClass device_class,
    gboolean low_latency, gint device_index, const gchar * device_id,
    gpointer dispatcher)
{
  GstWasapi2Client *self;
  ComPtr<ICoreDispatcher> core_dispatcher;
  /* Multiple COM init is allowed */
  RoInitializeWrapper init_wrapper (RO_INIT_MULTITHREADED);

  /* If application didn't pass ICoreDispatcher object,
   * try to get dispatcher object for the current thread */
  if (!dispatcher) {
    HRESULT hr;

    hr = find_dispatcher (&core_dispatcher);
    if (SUCCEEDED (hr)) {
      GST_DEBUG ("UI dispatcher is available");
      dispatcher = core_dispatcher.Get ();
    } else {
      GST_DEBUG ("UI dispatcher is unavailable");
    }
  } else {
    GST_DEBUG ("Use user passed UI dispatcher");
  }

  self = (GstWasapi2Client *) g_object_new (GST_TYPE_WASAPI2_CLIENT,
      "device-class", device_class, "low-latency", low_latency,
      "device-index", device_index, "device", device_id,
      "dispatcher", dispatcher, NULL);

  /* Reset explicitly to ensure that it happens before
   * RoInitializeWrapper dtor is called */
  core_dispatcher.Reset ();

  if (self->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_FAILED) {
    gst_object_unref (self);
    return NULL;
  }

  gst_object_ref_sink (self);

  return self;
}
