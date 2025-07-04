/* GStreamer
 * Copyright (C) 2018 Nirbheek Chauhan <nirbheek@centricular.com>
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

#include "gstwasapi2device.h"
#include "gstwasapi2util.h"
#include "gstwasapi2enumerator.h"

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_debug);
#define GST_CAT_DEFAULT gst_wasapi2_debug

enum
{
  PROP_0,
  PROP_DEVICE,
};

struct _GstWasapi2Device
{
  GstDevice parent;

  gchar *device_id;
  const gchar *factory_name;
  GstWasapi2EndpointClass device_class;
  gboolean is_default;
};

G_DEFINE_TYPE (GstWasapi2Device, gst_wasapi2_device, GST_TYPE_DEVICE);

static void gst_wasapi2_device_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wasapi2_device_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wasapi2_device_finalize (GObject * object);
static GstElement *gst_wasapi2_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_wasapi2_device_class_init (GstWasapi2DeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);

  dev_class->create_element = gst_wasapi2_device_create_element;

  gobject_class->get_property = gst_wasapi2_device_get_property;
  gobject_class->set_property = gst_wasapi2_device_set_property;
  gobject_class->finalize = gst_wasapi2_device_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Audio device ID as provided by "
          "Windows.Devices.Enumeration.DeviceInformation.Id", nullptr,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
              G_PARAM_STATIC_STRINGS)));
}

static void
gst_wasapi2_device_init (GstWasapi2Device * self)
{
}

static void
gst_wasapi2_device_finalize (GObject * object)
{
  GstWasapi2Device *self = GST_WASAPI2_DEVICE (object);

  g_free (self->device_id);

  G_OBJECT_CLASS (gst_wasapi2_device_parent_class)->finalize (object);
}

static GstElement *
gst_wasapi2_device_create_element (GstDevice * device, const gchar * name)
{
  GstWasapi2Device *self = GST_WASAPI2_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (self->factory_name, name);

  g_object_set (elem, "device", self->device_id, nullptr);

  if (self->device_class == GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE)
    g_object_set (elem, "loopback", TRUE, nullptr);

  return elem;
}

static void
gst_wasapi2_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapi2Device *self = GST_WASAPI2_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, self->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi2_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapi2Device *self = GST_WASAPI2_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      /* G_PARAM_CONSTRUCT_ONLY */
      self->device_id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

struct _GstWasapi2DeviceProvider
{
  GstDeviceProvider parent;

  GstWasapi2Enumerator *enumerator;
  GPtrArray *device_list;
};

G_DEFINE_TYPE (GstWasapi2DeviceProvider, gst_wasapi2_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static void gst_wasapi2_device_provider_dispose (GObject * object);

static GList *gst_wasapi2_device_provider_probe (GstDeviceProvider * provider);
static gboolean
gst_wasapi2_device_provider_start (GstDeviceProvider * provider);
static void gst_wasapi2_device_provider_stop (GstDeviceProvider * provider);
static void gst_wasapi2_device_provider_on_updated (GstWasapi2Enumerator *
    object, GstWasapi2DeviceProvider * self);

static void
gst_wasapi2_device_provider_class_init (GstWasapi2DeviceProviderClass * klass)
{
  auto gobject_class = G_OBJECT_CLASS (klass);
  auto provider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  gobject_class->dispose = gst_wasapi2_device_provider_dispose;

  provider_class->probe = GST_DEBUG_FUNCPTR (gst_wasapi2_device_provider_probe);
  provider_class->start = GST_DEBUG_FUNCPTR (gst_wasapi2_device_provider_start);
  provider_class->stop = GST_DEBUG_FUNCPTR (gst_wasapi2_device_provider_stop);

  gst_device_provider_class_set_static_metadata (provider_class,
      "WASAPI (Windows Audio Session API) Device Provider",
      "Source/Sink/Audio", "List WASAPI source devices",
      "Nirbheek Chauhan <nirbheek@centricular.com>, "
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_wasapi2_device_provider_init (GstWasapi2DeviceProvider * self)
{
  self->enumerator = gst_wasapi2_enumerator_new ();
  g_signal_connect (self->enumerator, "updated",
      G_CALLBACK (gst_wasapi2_device_provider_on_updated), self);

  self->device_list = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_wasapi2_enumerator_entry_free);
}

static void
gst_wasapi2_device_provider_dispose (GObject * object)
{
  auto self = GST_WASAPI2_DEVICE_PROVIDER (object);

  gst_clear_object (&self->enumerator);

  g_clear_pointer (&self->device_list, g_ptr_array_unref);

  G_OBJECT_CLASS (gst_wasapi2_device_provider_parent_class)->dispose (object);
}

static GList *
gst_wasapi2_device_provider_probe (GstDeviceProvider * provider)
{
  auto self = GST_WASAPI2_DEVICE_PROVIDER (provider);
  GList *devices = nullptr;

  g_ptr_array_set_size (self->device_list, 0);
  gst_wasapi2_enumerator_enumerate_devices (self->enumerator,
      self->device_list);

  for (guint i = 0; i < self->device_list->len; i++) {
    auto entry = (GstWasapi2EnumeratorEntry *)
        g_ptr_array_index (self->device_list, i);

    auto props = gst_structure_new ("wasapi2-proplist",
        "device.api", G_TYPE_STRING, "wasapi2",
        "device.id", G_TYPE_STRING, entry->device_id.c_str (),
        "device.default", G_TYPE_BOOLEAN, entry->is_default,
        "wasapi2.device.description", G_TYPE_STRING,
        entry->device_name.c_str (),
        "device.form-factor", G_TYPE_INT,
        (gint) entry->device_props.form_factor,
        "device.form-factor-name", G_TYPE_STRING,
        gst_wasapi2_form_factor_to_string (entry->device_props.form_factor),
        "device.enumerator-name", G_TYPE_STRING,
        entry->device_props.enumerator_name.c_str (), nullptr);

    if (entry->is_default) {
      if (!entry->actual_device_id.empty ()) {
        gst_structure_set (props, "device.actual-id", G_TYPE_STRING,
            entry->actual_device_id.c_str (), nullptr);
      }

      if (!entry->actual_device_name.empty ()) {
        gst_structure_set (props, "device.actual-name", G_TYPE_STRING,
            entry->actual_device_name.c_str (), nullptr);
      }
    }

    if (entry->flow == eCapture) {
      gst_structure_set (props,
          "wasapi2.device.loopback", G_TYPE_BOOLEAN, FALSE, nullptr);

      auto device = (GstDevice *) g_object_new (GST_TYPE_WASAPI2_DEVICE,
          "device", entry->device_id.c_str (),
          "display-name", entry->device_name.c_str (), "caps", entry->caps,
          "device-class", "Audio/Source", "properties", props, nullptr);
      gst_structure_free (props);

      auto wasapi2_dev = GST_WASAPI2_DEVICE (device);
      wasapi2_dev->factory_name = "wasapi2src";
      wasapi2_dev->device_class = GST_WASAPI2_ENDPOINT_CLASS_CAPTURE;
      wasapi2_dev->is_default = entry->is_default;

      devices = g_list_append (devices, device);
    } else {
      auto prop_copy = gst_structure_copy (props);
      gst_structure_set (prop_copy,
          "wasapi2.device.loopback", G_TYPE_BOOLEAN, TRUE, nullptr);

      auto device = (GstDevice *) g_object_new (GST_TYPE_WASAPI2_DEVICE,
          "device", entry->device_id.c_str (),
          "display-name", entry->device_name.c_str (), "caps", entry->caps,
          "device-class", "Audio/Sink", "properties", props, nullptr);
      gst_structure_free (props);

      auto wasapi2_dev = GST_WASAPI2_DEVICE (device);
      wasapi2_dev->factory_name = "wasapi2sink";
      wasapi2_dev->device_class = GST_WASAPI2_ENDPOINT_CLASS_RENDER;
      wasapi2_dev->is_default = entry->is_default;

      devices = g_list_append (devices, device);

      device = (GstDevice *) g_object_new (GST_TYPE_WASAPI2_DEVICE,
          "device", entry->device_id.c_str (),
          "display-name", entry->device_name.c_str (), "caps", entry->caps,
          "device-class", "Audio/Source", "properties", prop_copy, nullptr);
      gst_structure_free (prop_copy);

      wasapi2_dev = GST_WASAPI2_DEVICE (device);
      wasapi2_dev->factory_name = "wasapi2src";
      wasapi2_dev->device_class = GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE;
      wasapi2_dev->is_default = entry->is_default;

      devices = g_list_append (devices, device);
    }
  }

  g_ptr_array_set_size (self->device_list, 0);

  return devices;
}

static gboolean
gst_wasapi2_device_provider_start (GstDeviceProvider * provider)
{
  auto self = GST_WASAPI2_DEVICE_PROVIDER (provider);
  GList *devices = nullptr;
  GList *iter;

  if (!self->enumerator) {
    GST_ERROR_OBJECT (self, "Enumerator object wasn't configured");
    return FALSE;
  }

  devices = gst_wasapi2_device_provider_probe (provider);
  if (devices) {
    for (iter = devices; iter; iter = g_list_next (iter))
      gst_device_provider_device_add (provider, GST_DEVICE (iter->data));

    g_list_free (devices);
  }

  gst_wasapi2_enumerator_activate_notification (self->enumerator, TRUE);

  return TRUE;
}

static void
gst_wasapi2_device_provider_stop (GstDeviceProvider * provider)
{
  auto self = GST_WASAPI2_DEVICE_PROVIDER (provider);

  if (self->enumerator)
    gst_wasapi2_enumerator_activate_notification (self->enumerator, FALSE);
}

static gboolean
gst_wasapi2_device_is_in_list (GList * list, GstDevice * device)
{
  GList *iter;
  GstStructure *s;
  gboolean found = FALSE;

  s = gst_device_get_properties (device);
  g_assert (s);

  for (iter = list; iter; iter = g_list_next (iter)) {
    GstStructure *other_s;

    other_s = gst_device_get_properties (GST_DEVICE (iter->data));
    g_assert (other_s);

    found = gst_structure_is_equal (s, other_s);

    gst_structure_free (other_s);
    if (found)
      break;
  }

  gst_structure_free (s);

  return found;
}

static void
gst_wasapi2_device_provider_update_devices (GstWasapi2DeviceProvider * self)
{
  GstDeviceProvider *provider = GST_DEVICE_PROVIDER_CAST (self);
  GList *prev_devices = nullptr;
  GList *new_devices = nullptr;
  GList *to_add = nullptr;
  GList *to_remove = nullptr;
  GList *iter, *walk;

  GST_OBJECT_LOCK (self);
  prev_devices = g_list_copy_deep (provider->devices,
      (GCopyFunc) gst_object_ref, nullptr);
  GST_OBJECT_UNLOCK (self);

  new_devices = gst_wasapi2_device_provider_probe (provider);

  /* Ownership of GstDevice for gst_device_provider_device_add()
   * and gst_device_provider_device_remove() is a bit complicated.
   * Remove floating reference here for things to be clear */
  for (iter = new_devices; iter; iter = g_list_next (iter))
    gst_object_ref_sink (iter->data);

  /* Check newly added devices */
  for (iter = new_devices; iter; iter = g_list_next (iter)) {
    if (!gst_wasapi2_device_is_in_list (prev_devices, GST_DEVICE (iter->data))) {
      to_add = g_list_prepend (to_add, gst_object_ref (iter->data));
    }
  }

  /* Check removed device */
  for (iter = prev_devices; iter; iter = g_list_next (iter)) {
    if (!gst_wasapi2_device_is_in_list (new_devices, GST_DEVICE (iter->data))) {
      to_remove = g_list_prepend (to_remove, gst_object_ref (iter->data));
    }
  }

  iter = to_remove;
  while (iter) {
    auto prev_dev = GST_WASAPI2_DEVICE (iter->data);

    if (!prev_dev->is_default) {
      iter = g_list_next (iter);
      continue;
    }

    walk = to_add;
    bool found = false;
    while (walk) {
      auto new_dev = GST_WASAPI2_DEVICE (walk->data);

      if (!new_dev->is_default ||
          prev_dev->device_class != new_dev->device_class) {
        walk = g_list_next (walk);
        continue;
      }

      gst_device_provider_device_changed (provider, GST_DEVICE (new_dev),
          GST_DEVICE (prev_dev));
      gst_object_unref (new_dev);
      to_add = g_list_delete_link (to_add, walk);
      found = true;
      break;
    }

    if (found) {
      gst_object_unref (prev_dev);
      auto next = iter->next;
      to_remove = g_list_delete_link (to_remove, iter);
      iter = next;
    } else {
      iter = g_list_next (iter);
    }
  }

  for (iter = to_remove; iter; iter = g_list_next (iter))
    gst_device_provider_device_remove (provider, GST_DEVICE (iter->data));

  for (iter = to_add; iter; iter = g_list_next (iter))
    gst_device_provider_device_add (provider, GST_DEVICE (iter->data));

  if (prev_devices)
    g_list_free_full (prev_devices, (GDestroyNotify) gst_object_unref);

  if (to_add)
    g_list_free_full (to_add, (GDestroyNotify) gst_object_unref);

  if (to_remove)
    g_list_free_full (to_remove, (GDestroyNotify) gst_object_unref);
}

static void
gst_wasapi2_device_provider_on_updated (GstWasapi2Enumerator * object,
    GstWasapi2DeviceProvider * self)
{
  gst_wasapi2_device_provider_update_devices (self);
}
