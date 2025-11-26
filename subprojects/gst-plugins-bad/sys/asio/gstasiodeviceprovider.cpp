/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CM_NOTIFY
#define _WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbiodef.h>
#endif

#include "gstasiodeviceprovider.h"
#include "gstasioutils.h"
#include "gstasioobject.h"

enum
{
  PROP_0,
  PROP_DEVICE_CLSID,
};

struct _GstAsioDevice
{
  GstDevice parent;

  gchar *device_clsid;
  const gchar *factory_name;
};

G_DEFINE_TYPE (GstAsioDevice, gst_asio_device, GST_TYPE_DEVICE);

static void gst_asio_device_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_asio_device_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_asio_device_finalize (GObject * object);
static GstElement *gst_asio_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_asio_device_class_init (GstAsioDeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);

  dev_class->create_element = gst_asio_device_create_element;

  gobject_class->get_property = gst_asio_device_get_property;
  gobject_class->set_property = gst_asio_device_set_property;
  gobject_class->finalize = gst_asio_device_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE_CLSID,
      g_param_spec_string ("device-clsid", "Device CLSID",
          "ASIO device CLSID as string including curly brackets", NULL,
          (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));
}

static void
gst_asio_device_init (GstAsioDevice * self)
{
}

static void
gst_asio_device_finalize (GObject * object)
{
  GstAsioDevice *self = GST_ASIO_DEVICE (object);

  g_free (self->device_clsid);

  G_OBJECT_CLASS (gst_asio_device_parent_class)->finalize (object);
}

static GstElement *
gst_asio_device_create_element (GstDevice * device, const gchar * name)
{
  GstAsioDevice *self = GST_ASIO_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (self->factory_name, name);

  g_object_set (elem, "device-clsid", self->device_clsid, NULL);

  return elem;
}

static void
gst_asio_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAsioDevice *self = GST_ASIO_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE_CLSID:
      g_value_set_string (value, self->device_clsid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_asio_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAsioDevice *self = GST_ASIO_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE_CLSID:
      g_free (self->device_clsid);
      self->device_clsid = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

struct _GstAsioDeviceProvider
{
  GstDeviceProvider parent;

#ifdef HAVE_CM_NOTIFY
  GThread *thread;
  GMainLoop *loop;
  GMainContext *context;
  guint device_update_id;
#endif /* HAVE_CM_NOTIFY */
};

GST_DEBUG_CATEGORY_STATIC (gst_asio_dp_debug);
#define GST_CAT_DEFAULT gst_asio_dp_debug

G_DEFINE_TYPE (GstAsioDeviceProvider, gst_asio_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

#ifdef HAVE_CM_NOTIFY
static void gst_asio_device_provider_finalize (GObject * obj);
static gboolean gst_asio_device_provider_start (GstDeviceProvider * provider);
static void gst_asio_device_provider_stop (GstDeviceProvider * provider);
#endif /* HAVE_CM_NOTIFY */
static GList *gst_asio_device_provider_probe (GstDeviceProvider * provider);

static void
gst_asio_device_provider_class_init (GstAsioDeviceProviderClass * klass)
{
  GstDeviceProviderClass *provider_class = GST_DEVICE_PROVIDER_CLASS (klass);
#ifdef HAVE_CM_NOTIFY
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_asio_device_provider_finalize;

  provider_class->start = GST_DEBUG_FUNCPTR (gst_asio_device_provider_start);
  provider_class->stop = GST_DEBUG_FUNCPTR (gst_asio_device_provider_stop);
#endif /* HAVE_CM_NOTIFY */
  provider_class->probe = GST_DEBUG_FUNCPTR (gst_asio_device_provider_probe);

  gst_device_provider_class_set_static_metadata (provider_class,
      "ASIO Device Provider",
      "Source/Sink/Audio", "List ASIO source and sink devices",
      "Seungha Yang <seungha@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_asio_dp_debug, "asio-deviceprovider",
      0, "ASIO device provider");
}

static void
gst_asio_device_provider_init (GstAsioDeviceProvider * self)
{
#ifdef HAVE_CM_NOTIFY
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  self->device_update_id = 0;
#endif
}

#ifdef HAVE_CM_NOTIFY
static void
gst_asio_device_provider_finalize (GObject * obj)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (obj);

  g_main_loop_quit (self->loop);
  g_clear_pointer (&self->thread, g_thread_join);
  g_main_loop_unref (self->loop);
  g_main_context_unref (self->context);

  G_OBJECT_CLASS (gst_asio_device_provider_parent_class)->finalize (obj);
}

static gboolean
gst_asio_device_provider_update_devices (GstDeviceProvider * provider)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (provider);

  GST_LOG_OBJECT (self, "Updating devices");

  GList *prev_devices = g_list_copy_deep (provider->devices,
      (GCopyFunc) gst_object_ref, NULL);
  GList *new_devices = gst_asio_device_provider_probe (provider);

  // Emit device removal messages
  for (GList *p = prev_devices; p; p = p->next) {
    GstAsioDevice *pdev = GST_ASIO_DEVICE (p->data);
    bool found = false;
    for (GList *n = new_devices; n; n = n->next) {
      GstAsioDevice *ndev = GST_ASIO_DEVICE (n->data);
      if (g_strcmp0 (ndev->device_clsid, pdev->device_clsid) == 0 &&
          g_strcmp0 (ndev->factory_name, pdev->factory_name) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      gst_device_provider_device_remove (provider, GST_DEVICE (gst_object_ref(pdev)));
    }
  }

  // Emit device added messages
  for (GList *n = new_devices; n; n = n->next) {
    GstAsioDevice *ndev = GST_ASIO_DEVICE (n->data);
    bool found = false;
    for (GList *p = prev_devices; p; p = p->next) {
      GstAsioDevice *pdev = GST_ASIO_DEVICE (p->data);
      if (g_strcmp0 (ndev->device_clsid, pdev->device_clsid) == 0 &&
          g_strcmp0 (ndev->factory_name, pdev->factory_name) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      gst_device_provider_device_add (provider, GST_DEVICE (gst_object_ref (ndev)));
    }
  }

  g_list_free_full (new_devices, (GDestroyNotify) gst_object_unref);
  g_list_free_full (prev_devices, (GDestroyNotify) gst_object_unref);
  self->device_update_id = 0;

  return G_SOURCE_REMOVE;
}

static DWORD
device_event_cb (HCMNOTIFICATION, PVOID data, CM_NOTIFY_ACTION action,
    PCM_NOTIFY_EVENT_DATA, DWORD)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (data);

  GST_LOG_OBJECT (self, "USB device event: %d", action);

  if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL ||
      action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL ||
      action == CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED) {
    GSource *source;
    // We need to wait a bit before probing for ASIO devices, because device
    // initialization can take some time. However, we must also not probe too
    // frequently, so if there's already a device update scheduled when we get
    // a WM_DEVICECHANGE event, then cancel the existing update and schedule
    // a new one in the near future.
    if (self->device_update_id > 0) {
      source = g_main_context_find_source_by_id (self->context, self->device_update_id);
      self->device_update_id = 0;
      g_source_destroy (source);
    }
    source = g_timeout_source_new (500);
    g_source_set_callback (source,
        (GSourceFunc) gst_asio_device_provider_update_devices, self, NULL);
    self->device_update_id = g_source_attach (source, self->context);
  }

  return ERROR_SUCCESS;
}

static gpointer
gst_asio_device_provider_thread_func (GstDeviceProvider * provider)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (provider);

  g_main_context_push_thread_default (self->context);

  CONFIGRET cr;
  CM_NOTIFY_FILTER filter = { 0 };

  // Filter for USB device interface events
  filter.cbSize = sizeof (CM_NOTIFY_FILTER);
  filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
  filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_USB_DEVICE;

  HCMNOTIFICATION notify = NULL;
  // Register for USB device interface events
  cr = CM_Register_Notification (&filter, (gpointer) self,
      (PCM_NOTIFY_CALLBACK) device_event_cb, &notify);
  if (cr != CR_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to register for USB notifications:"
        " 0x%lX\n", cr);
    return NULL;
  }

  GST_LOG_OBJECT (self, "Started device provider loop");
  g_main_loop_run (self->loop);
  GST_LOG_OBJECT (self, "Stopped device provider loop");

  // Cleanup
  if (notify) {
    CM_Unregister_Notification (notify);
  }

  g_main_context_pop_thread_default (self->context);

  return NULL;
}

static gboolean
gst_asio_device_provider_start (GstDeviceProvider * provider)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (provider);

  GList *devices = gst_asio_device_provider_probe (provider);
  if (devices) {
    for (GList *iter = devices; iter; iter = g_list_next (iter)) {
      gst_device_provider_device_add (provider, GST_DEVICE (iter->data));
    }

    g_list_free (devices);
  }

  self->thread = g_thread_new ("GstAsioDeviceProvider",
      (GThreadFunc) gst_asio_device_provider_thread_func, self);

  GST_LOG_OBJECT (self, "Started ASIO device provider");

  return TRUE;
}

static void
gst_asio_device_provider_stop (GstDeviceProvider * provider)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (provider);

  if (self->thread) {
    g_main_loop_quit (self->loop);
    g_clear_pointer (&self->thread, g_thread_join);
  }

  if (self->device_update_id > 0) {
    GSource *source = g_main_context_find_source_by_id (self->context, self->device_update_id);
    self->device_update_id = 0;
    g_source_destroy (source);
  }
}
#endif /* HAVE_CM_NOTIFY */

static void
gst_asio_device_provider_probe_internal (GstAsioDeviceProvider * self,
    gboolean is_src, GList * asio_device_list, GList ** devices)
{
  const gchar *device_class, *factory_name;
  GList *iter;

  if (is_src) {
    device_class = "Audio/Source";
    factory_name = "asiosrc";
  } else {
    device_class = "Audio/Sink";
    factory_name = "asiosink";
  }

  for (iter = asio_device_list; iter; iter = g_list_next (iter)) {
    GstDevice *device;
    GstAsioDeviceInfo *info = (GstAsioDeviceInfo *) iter->data;
    GstAsioObject *obj;
    GstCaps *caps = nullptr;
    GstStructure *props = nullptr;
    long max_in_ch = 0;
    long max_out_ch = 0;
    HRESULT hr;
    LPOLESTR clsid_str = nullptr;
    glong min_buf_size = 0;
    glong max_buf_size = 0;
    glong preferred_buf_size = 0;
    glong buf_size_granularity = 0;
    gchar *clsid_str_utf8;

    obj = gst_asio_object_new (info, FALSE);
    if (!obj)
      continue;

    if (!gst_asio_object_get_max_num_channels (obj, &max_in_ch, &max_out_ch))
      goto done;

    if (is_src && max_in_ch <= 0)
      goto done;
    else if (!is_src && max_out_ch <= 0)
      goto done;

    if (is_src) {
      caps = gst_asio_object_get_caps (obj,
          GST_ASIO_DEVICE_CLASS_CAPTURE, 1, max_in_ch);
    } else {
      caps = gst_asio_object_get_caps (obj,
          GST_ASIO_DEVICE_CLASS_RENDER, 1, max_out_ch);
    }
    if (!caps)
      goto done;

    hr = StringFromIID (info->clsid, &clsid_str);
    if (FAILED (hr))
      goto done;

    if (!gst_asio_object_get_buffer_size (obj, &min_buf_size, &max_buf_size,
            &preferred_buf_size, &buf_size_granularity))
      goto done;

    clsid_str_utf8 = g_utf16_to_utf8 ((const gunichar2 *) clsid_str, -1,
        nullptr, nullptr, nullptr);

    props = gst_structure_new ("asio-proplist",
        "device.api", G_TYPE_STRING, "asio",
        "device.clsid", G_TYPE_STRING, clsid_str_utf8,
        "asio.device.description", G_TYPE_STRING, info->driver_desc,
        "asio.device.min-buf-size", G_TYPE_LONG, min_buf_size,
        "asio.device.max-buf-size", G_TYPE_LONG, max_buf_size,
        "asio.device.preferred-buf-size", G_TYPE_LONG, preferred_buf_size,
        "asio.device.buf-size-granularity", G_TYPE_LONG, buf_size_granularity,
        nullptr);

    device = (GstDevice *) g_object_new (GST_TYPE_ASIO_DEVICE,
        "device-clsid", clsid_str_utf8,
        "display-name", info->driver_desc, "caps", caps,
        "device-class", device_class, "properties", props, nullptr);
    GST_ASIO_DEVICE (device)->factory_name = factory_name;

    g_free (clsid_str_utf8);

    *devices = g_list_append (*devices, device);

  done:
    if (clsid_str)
      CoTaskMemFree (clsid_str);
    gst_clear_caps (&caps);
    gst_clear_object (&obj);
    if (props)
      gst_structure_free (props);
  }

  return;
}

static GList *
gst_asio_device_provider_probe (GstDeviceProvider * provider)
{
  GstAsioDeviceProvider *self = GST_ASIO_DEVICE_PROVIDER (provider);
  GList *devices = nullptr;
  guint num_device;
  GList *asio_device_list = nullptr;

  num_device = gst_asio_enum (&asio_device_list);

  if (num_device == 0)
    return nullptr;

  gst_asio_device_provider_probe_internal (self,
      TRUE, asio_device_list, &devices);
  gst_asio_device_provider_probe_internal (self,
      FALSE, asio_device_list, &devices);

  g_list_free_full (asio_device_list,
      (GDestroyNotify) gst_asio_device_info_free);

  return devices;
}
