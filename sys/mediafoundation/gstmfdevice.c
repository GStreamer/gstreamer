/* GStreamer
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

#include "gstmfconfig.h"

#include "gstmfvideosrc.h"
#include "gstmfutils.h"
#include "gstmfsourceobject.h"

#include "gstmfdevice.h"

GST_DEBUG_CATEGORY_EXTERN (gst_mf_debug);
#define GST_CAT_DEFAULT gst_mf_debug

enum
{
  PROP_0,
  PROP_DEVICE_PATH,
};

struct _GstMFDevice
{
  GstDevice parent;

  gchar *device_path;
};

G_DEFINE_TYPE (GstMFDevice, gst_mf_device, GST_TYPE_DEVICE);

static void gst_mf_device_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mf_device_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mf_device_finalize (GObject * object);
static GstElement *gst_mf_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_mf_device_class_init (GstMFDeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);

  dev_class->create_element = gst_mf_device_create_element;

  gobject_class->get_property = gst_mf_device_get_property;
  gobject_class->set_property = gst_mf_device_set_property;
  gobject_class->finalize = gst_mf_device_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE_PATH,
      g_param_spec_string ("device-path", "Device Path",
          "The device path", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_mf_device_init (GstMFDevice * self)
{
}

static void
gst_mf_device_finalize (GObject * object)
{
  GstMFDevice *self = GST_MF_DEVICE (object);

  g_free (self->device_path);

  G_OBJECT_CLASS (gst_mf_device_parent_class)->finalize (object);
}

static GstElement *
gst_mf_device_create_element (GstDevice * device, const gchar * name)
{
  GstMFDevice *self = GST_MF_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make ("mfvideosrc", name);

  g_object_set (elem, "device-path", self->device_path, NULL);

  return elem;
}

static void
gst_mf_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFDevice *self = GST_MF_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_value_set_string (value, self->device_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFDevice *self = GST_MF_DEVICE (object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      self->device_path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

struct _GstMFDeviceProvider
{
  GstDeviceProvider parent;
};

G_DEFINE_TYPE (GstMFDeviceProvider, gst_mf_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static GList *gst_mf_device_provider_probe (GstDeviceProvider * provider);

static void
gst_mf_device_provider_class_init (GstMFDeviceProviderClass * klass)
{
  GstDeviceProviderClass *provider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  provider_class->probe = GST_DEBUG_FUNCPTR (gst_mf_device_provider_probe);

  gst_device_provider_class_set_static_metadata (provider_class,
      "Media Foundation Device Provider",
      "Source/Video", "List Media Foundation source devices",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_mf_device_provider_init (GstMFDeviceProvider * provider)
{
}

static GList *
gst_mf_device_provider_probe (GstDeviceProvider * provider)
{
  GstMFDeviceProvider *self = GST_MF_DEVICE_PROVIDER (provider);
  GList *list = NULL;
  gint i;

  for (i = 0;; i++) {
    GstMFSourceObject *obj = NULL;
    GstDevice *device;
    GstStructure *props = NULL;
    GstCaps *caps = NULL;
    gchar *device_name = NULL;
    gchar *device_path = NULL;

    obj = gst_mf_source_object_new (GST_MF_SOURCE_TYPE_VIDEO,
        i, NULL, NULL, NULL);
    if (!obj)
      break;

    caps = gst_mf_source_object_get_caps (obj);
    if (!caps) {
      GST_WARNING_OBJECT (self, "Empty caps for device index %d", i);
      goto next;
    }

    g_object_get (obj,
        "device-path", &device_path, "device-name", &device_name, NULL);

    if (!device_path) {
      GST_WARNING_OBJECT (self, "Device path is unavailable");
      goto next;
    }

    if (!device_name) {
      GST_WARNING_OBJECT (self, "Device name is unavailable");
      goto next;
    }

    props = gst_structure_new ("mf-proplist",
        "device.api", G_TYPE_STRING, "mediafoundation",
        "device.path", G_TYPE_STRING, device_path,
        "device.name", G_TYPE_STRING, device_name, NULL);

    device = g_object_new (GST_TYPE_MF_DEVICE, "device-path", device_path,
        "display-name", device_name, "caps", caps,
        "device-class", "Source/Video", "properties", props, NULL);

    list = g_list_append (list, device);

  next:
    if (caps)
      gst_caps_unref (caps);
    if (props)
      gst_structure_free (props);
    g_free (device_path);
    g_free (device_name);
    gst_object_unref (obj);
  }

  return list;
}
