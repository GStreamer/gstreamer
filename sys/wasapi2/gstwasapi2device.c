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
#include "gstwasapi2client.h"
#include "gstwasapi2util.h"

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
          "WASAPI playback device as a GUID string", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
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

  g_object_set (elem, "device", self->device_id, NULL);

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
};

G_DEFINE_TYPE (GstWasapi2DeviceProvider, gst_wasapi2_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static GList *gst_wasapi2_device_provider_probe (GstDeviceProvider * provider);

static void
gst_wasapi2_device_provider_class_init (GstWasapi2DeviceProviderClass * klass)
{
  GstDeviceProviderClass *provider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  provider_class->probe = GST_DEBUG_FUNCPTR (gst_wasapi2_device_provider_probe);

  gst_device_provider_class_set_static_metadata (provider_class,
      "WASAPI (Windows Audio Session API) Device Provider",
      "Source/Sink/Audio", "List WASAPI source and sink devices",
      "Nirbheek Chauhan <nirbheek@centricular.com>, "
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_wasapi2_device_provider_init (GstWasapi2DeviceProvider * provider)
{
}

static void
gst_wasapi2_device_provider_probe_internal (GstWasapi2DeviceProvider * self,
    GstWasapi2ClientDeviceClass client_class, GList ** devices)
{
  gint i;
  const gchar *device_class, *factory_name;

  if (client_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER) {
    device_class = "Audio/Sink";
    factory_name = "wasapi2sink";
  } else {
    device_class = "Audio/Source";
    factory_name = "wasapi2src";
  }

  for (i = 0;; i++) {
    GstWasapi2Client *client = NULL;
    GstDevice *device;
    GstStructure *props = NULL;
    GstCaps *caps = NULL;
    gchar *device_id = NULL;
    gchar *device_name = NULL;

    client = gst_wasapi2_client_new (client_class, FALSE, i, NULL, NULL);

    if (!client)
      return;

    caps = gst_wasapi2_client_get_caps (client);
    if (!caps) {
      GST_WARNING_OBJECT (self, "Couldn't get caps from client %d", i);
      /* this might be a case where device activation is not finished yet */
      caps = gst_caps_from_string (GST_WASAPI2_STATIC_CAPS);
    }

    g_object_get (client,
        "device", &device_id, "device-name", &device_name, NULL);
    if (!device_id) {
      GST_WARNING_OBJECT (self, "Couldn't get device name from client %d", i);
      goto next;
    }

    if (!device_name) {
      GST_WARNING_OBJECT (self, "Couldn't get device name from client %d", i);
      goto next;
    }

    props = gst_structure_new ("wasapi2-proplist",
        "device.api", G_TYPE_STRING, "wasapi",
        "device.id", G_TYPE_STRING, device_id,
        "device.default", G_TYPE_BOOLEAN, i == 0,
        "wasapi.device.description", G_TYPE_STRING, device_name, NULL);

    device = g_object_new (GST_TYPE_WASAPI2_DEVICE, "device", device_id,
        "display-name", device_name, "caps", caps,
        "device-class", device_class, "properties", props, NULL);
    GST_WASAPI2_DEVICE (device)->factory_name = factory_name;

    *devices = g_list_append (*devices, device);

  next:
    gst_clear_object (&client);
    gst_clear_caps (&caps);
    g_free (device_id);
    g_free (device_name);
    if (props)
      gst_structure_free (props);
  }

  return;
}

static GList *
gst_wasapi2_device_provider_probe (GstDeviceProvider * provider)
{
  GstWasapi2DeviceProvider *self = GST_WASAPI2_DEVICE_PROVIDER (provider);
  GList *devices = NULL;

  gst_wasapi2_device_provider_probe_internal (self,
      GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE, &devices);
  gst_wasapi2_device_provider_probe_internal (self,
      GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER, &devices);

  return devices;
}
