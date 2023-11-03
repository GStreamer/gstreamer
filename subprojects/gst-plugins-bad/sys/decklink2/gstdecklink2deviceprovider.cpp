/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstdecklink2deviceprovider.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_decklink2_debug);
#define GST_CAT_DEFAULT gst_decklink2_debug

struct _GstDeckLink2ProviderDevice
{
  GstDevice parent;

  gboolean is_src;
  guint device_number;
  gint64 persistent_id;
};

static GstElement *gst_decklink2_provider_device_create_element (GstDevice *
    device, const gchar * name);

G_DEFINE_TYPE (GstDeckLink2ProviderDevice, gst_decklink2_provider_device,
    GST_TYPE_DEVICE);

static void
gst_decklink2_provider_device_class_init (GstDeckLink2ProviderDeviceClass *
    klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);

  dev_class->create_element =
      GST_DEBUG_FUNCPTR (gst_decklink2_provider_device_create_element);
}

static void
gst_decklink2_provider_device_init (GstDeckLink2ProviderDevice * self)
{
}

static GstElement *
gst_decklink2_provider_device_create_element (GstDevice * device,
    const gchar * name)
{
  GstDeckLink2ProviderDevice *self = GST_DECKLINK2_PROVIDER_DEVICE (device);
  GstElement *elem;

  if (self->is_src) {
    elem = gst_element_factory_make ("decklink2src", name);
  } else {
    elem = gst_element_factory_make ("decklink2sink", name);
  }

  g_object_set (elem, "persistent-id", self->persistent_id, NULL);

  return elem;
}

GstDevice *
gst_decklink2_provider_device_new (gboolean is_src, const gchar * model_name,
    const gchar * display_name, const gchar * serial_number, GstCaps * caps,
    gint64 persistent_id, guint device_number, guint max_audio_channels,
    const gchar * driver_ver, const gchar * api_ver,
    const GstDeckLink2ColorCapabilities * color_caps)
{
  GstDevice *device;
  GstDeckLink2ProviderDevice *self;
  const gchar *device_class;
  GstStructure *props;

  if (is_src)
    device_class = "Video/Audio/Source/Hardware";
  else
    device_class = "Video/Audio/Sink/Hardware";

  props = gst_structure_new ("properties",
      "model-name", G_TYPE_STRING, model_name,
      "driver-version", G_TYPE_STRING, driver_ver,
      "api-version", G_TYPE_STRING, api_ver,
      "supports-hdr", G_TYPE_BOOLEAN, color_caps->supports_hdr,
      "supports-colorspace", G_TYPE_BOOLEAN, color_caps->supports_colorspace,
      "device-number", G_TYPE_UINT, device_number,
      "persistent-id", G_TYPE_INT64, persistent_id, NULL);

  if (max_audio_channels > 0) {
    gst_structure_set (props,
        "max-channels", G_TYPE_UINT, max_audio_channels, NULL);
  }

  if (serial_number && serial_number[0] != '\0') {
    gst_structure_set (props,
        "serial-number", G_TYPE_STRING, serial_number, NULL);
  }

  device = (GstDevice *) g_object_new (GST_TYPE_DECKLINK2_PROVIDER_DEVICE,
      "display-name", display_name, "device-class", device_class,
      "caps", caps, "properties", props, NULL);
  gst_structure_free (props);

  self = GST_DECKLINK2_PROVIDER_DEVICE (device);
  self->device_number = device_number;
  self->persistent_id = persistent_id;
  self->is_src = is_src;

  return device;
}

struct _GstDeckLink2DeviceProvider
{
  GstDeviceProvider parent;
};

static GList *gst_decklink2_device_provider_probe (GstDeviceProvider *
    provider);

G_DEFINE_TYPE (GstDeckLink2DeviceProvider, gst_decklink2_device_provider,
    GST_TYPE_DEVICE_PROVIDER);
GST_DEVICE_PROVIDER_REGISTER_DEFINE (decklink2deviceprovider,
    "decklink2deviceprovider", GST_RANK_SECONDARY,
    GST_TYPE_DECKLINK2_DEVICE_PROVIDER);

static void
gst_decklink2_device_provider_class_init (GstDeckLink2DeviceProviderClass *
    klass)
{
  GstDeviceProviderClass *provider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  provider_class->probe =
      GST_DEBUG_FUNCPTR (gst_decklink2_device_provider_probe);

  gst_device_provider_class_set_static_metadata (provider_class,
      "Decklink Device Provider", "Hardware/Source/Sink/Audio/Video",
      "Lists and provides Decklink devices",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_decklink2_device_provider_init (GstDeckLink2DeviceProvider * self)
{
}

static GList *
gst_decklink2_device_provider_probe (GstDeviceProvider * provider)
{
  return gst_decklink2_get_devices ();
}
