/* GStreamer
 * Copyright (C) 2018 Nirbheek Chauhan <nirbheek@centricular.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwasapidevice.h"

G_DEFINE_TYPE (GstWasapiDeviceProvider, gst_wasapi_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static void gst_wasapi_device_provider_finalize (GObject * object);
static GList *gst_wasapi_device_provider_probe (GstDeviceProvider * provider);

static void
gst_wasapi_device_provider_class_init (GstWasapiDeviceProviderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);

  gobject_class->finalize = gst_wasapi_device_provider_finalize;

  dm_class->probe = gst_wasapi_device_provider_probe;

  gst_device_provider_class_set_static_metadata (dm_class,
      "WASAPI (Windows Audio Session API) Device Provider",
      "Source/Sink/Audio", "List WASAPI source and sink devices",
      "Nirbheek Chauhan <nirbheek@centricular.com>");
}

static void
gst_wasapi_device_provider_init (GstWasapiDeviceProvider * provider)
{
  CoInitialize (NULL);
}

static void
gst_wasapi_device_provider_finalize (GObject * object)
{
  CoUninitialize ();
}

static GList *
gst_wasapi_device_provider_probe (GstDeviceProvider * provider)
{
  GstWasapiDeviceProvider *self = GST_WASAPI_DEVICE_PROVIDER (provider);
  GList *devices = NULL;

  if (!gst_wasapi_util_get_devices (GST_ELEMENT (self), TRUE, &devices))
    GST_ERROR_OBJECT (self, "Failed to enumerate devices");

  return devices;
}

/* GstWasapiDevice begins */

enum
{
  PROP_DEVICE_STRID = 1,
};

G_DEFINE_TYPE (GstWasapiDevice, gst_wasapi_device, GST_TYPE_DEVICE);

static void gst_wasapi_device_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wasapi_device_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wasapi_device_finalize (GObject * object);
static GstElement *gst_wasapi_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_wasapi_device_class_init (GstWasapiDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_wasapi_device_create_element;

  object_class->get_property = gst_wasapi_device_get_property;
  object_class->set_property = gst_wasapi_device_set_property;
  object_class->finalize = gst_wasapi_device_finalize;

  g_object_class_install_property (object_class, PROP_DEVICE_STRID,
      g_param_spec_string ("device", "Device string ID",
          "Device strId", NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_wasapi_device_init (GstWasapiDevice * device)
{
}

static void
gst_wasapi_device_finalize (GObject * object)
{
  GstWasapiDevice *device = GST_WASAPI_DEVICE (object);

  g_free (device->strid);

  G_OBJECT_CLASS (gst_wasapi_device_parent_class)->finalize (object);
}

static GstElement *
gst_wasapi_device_create_element (GstDevice * device, const gchar * name)
{
  GstWasapiDevice *wasapi_dev = GST_WASAPI_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (wasapi_dev->element, name);

  g_object_set (elem, "device", wasapi_dev->strid, NULL);

  return elem;
}

static void
gst_wasapi_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapiDevice *device = GST_WASAPI_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_STRID:
      g_value_set_string (value, device->strid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapiDevice *device = GST_WASAPI_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_STRID:
      device->strid = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
