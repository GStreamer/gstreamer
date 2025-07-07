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

#include "gsthip.h"
#include "gsthip-private.h"
#include <mutex>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hipdevice", 0, "hipdevice");
      });

  return cat;
}
#endif

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_VENDOR,
  PROP_TEXTURE2D_SUPPORT,
};

struct _GstHipDevicePrivate
{
  ~_GstHipDevicePrivate ()
  {
    gst_clear_hip_stream (&stream);
  }
  guint device_id;
  GstHipVendor vendor;
  gboolean texture_support;
  GstHipStream *stream = nullptr;
};

#define gst_hip_device_parent_class parent_class
G_DEFINE_TYPE (GstHipDevice, gst_hip_device, GST_TYPE_OBJECT);

static void gst_hip_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_hip_device_finalize (GObject * object);

static void
gst_hip_device_class_init (GstHipDeviceClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_hip_device_get_property;
  object_class->finalize = gst_hip_device_finalize;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device ID", "Device ID",
          0, G_MAXUINT, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_VENDOR,
      g_param_spec_enum ("vendor", "Vendor", "Vendor",
          GST_TYPE_HIP_VENDOR, GST_HIP_VENDOR_UNKNOWN,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_TEXTURE2D_SUPPORT,
      g_param_spec_boolean ("texture2d-support", "Texture2D support",
          "Texture2D support", FALSE,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  gst_hip_memory_init_once ();
}

static void
gst_hip_device_init (GstHipDevice * self)
{
  self->priv = new GstHipDevicePrivate ();
}

static void
gst_hip_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_DEVICE (object);
  auto priv = self->priv;

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_uint (value, priv->device_id);
      break;
    case PROP_VENDOR:
      g_value_set_enum (value, priv->vendor);
      break;
    case PROP_TEXTURE2D_SUPPORT:
      g_value_set_boolean (value, priv->texture_support);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_device_finalize (GObject * object)
{
  auto self = GST_HIP_DEVICE (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_hip_device_new:
 * @vendor: a #GstHipVendor
 * @device_id: device identifier
 *
 * Creates a new device instance with @vendor and @device_id.
 *
 * Returns: (transfer full) (nullable): a #GstHipDevice if succeeded,
 * otherwise %NULL
 *
 * Since: 1.28
 */
GstHipDevice *
gst_hip_device_new (GstHipVendor vendor, guint device_id)
{
  if (vendor == GST_HIP_VENDOR_UNKNOWN) {
    if (gst_hip_load_library (GST_HIP_VENDOR_AMD))
      vendor = GST_HIP_VENDOR_AMD;
    else if (gst_hip_load_library (GST_HIP_VENDOR_NVIDIA))
      vendor = GST_HIP_VENDOR_NVIDIA;
    else
      return nullptr;
  }

  if (!gst_hip_load_library (vendor)) {
    GST_INFO ("Couldn't load HIP library");
    return nullptr;
  }

  int num_dev = 0;
  auto hip_ret = HipGetDeviceCount (vendor, &num_dev);
  if (hip_ret != hipSuccess || num_dev <= 0) {
    GST_DEBUG ("No supported HIP device, error: %d", hip_ret);
    return nullptr;
  }

  if ((guint) num_dev <= device_id) {
    GST_DEBUG ("Num device %d <= requested device id %d", num_dev, device_id);
    return nullptr;
  }

  gboolean texture_support = FALSE;
  int val = 0;
  hip_ret = HipDeviceGetAttribute (vendor, &val,
      hipDeviceAttributeMaxTexture2DWidth, device_id);
  if (hip_ret == hipSuccess && val > 0) {
    hip_ret = HipDeviceGetAttribute (vendor, &val,
        hipDeviceAttributeMaxTexture2DHeight, device_id);
    if (hip_ret == hipSuccess && val > 0) {
      hip_ret = HipDeviceGetAttribute (vendor, &val,
          hipDeviceAttributeTextureAlignment, device_id);
      if (hip_ret == hipSuccess && val > 0) {
        texture_support = TRUE;
      }
    }
  }

  auto stream = gst_hip_stream_new (vendor, device_id);
  if (!stream) {
    GST_ERROR ("Couldn't create stream");
    return nullptr;
  }

  auto self = (GstHipDevice *) g_object_new (GST_TYPE_HIP_DEVICE, nullptr);
  gst_object_ref_sink (self);
  self->priv->device_id = device_id;
  self->priv->vendor = vendor;
  self->priv->texture_support = texture_support;
  self->priv->stream = stream;

  return self;
}

/**
 * gst_hip_device_set_current:
 * @device: a #GstHipDevice
 *
 * Sets @device to current stack via hipSetDevice
 *
 * Returns: %TRUE if hipSetDevice call succeeded
 *
 * Since: 1.28
 */
gboolean
gst_hip_device_set_current (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), FALSE);

  auto priv = device->priv;
  auto hip_ret = HipSetDevice (priv->vendor, priv->device_id);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (device, "hipSetDevice result %d", hip_ret);
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_hip_device_get_attribute:
 * @device: a #GstHipDevice
 * @attr: a hipDeviceAttribute_t value
 * @value: (out): an attribute value
 *
 * Gets a device attribute via hipDeviceGetAttribute
 *
 * Returns: hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_device_get_attribute (GstHipDevice * device, hipDeviceAttribute_t attr,
    gint * value)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), hipErrorInvalidDevice);

  auto priv = device->priv;

  return HipDeviceGetAttribute (priv->vendor, value, attr, priv->device_id);
}

/**
 * gst_hip_device_is_equal:
 * @device1: a #GstHipDevice
 * @device2: a #GstHipDevice
 *
 * Checks equality of @device1 and @device2
 *
 * Returns: %TRUE if both devices are associated with the same hardware device
 *
 * Since: 1.28
 */
gboolean
gst_hip_device_is_equal (GstHipDevice * device1, GstHipDevice * device2)
{
  if (!device1 || !device2)
    return FALSE;

  g_return_val_if_fail (GST_IS_HIP_DEVICE (device1), FALSE);
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device2), FALSE);

  if (device1 == device2)
    return TRUE;

  if (device1->priv->device_id == device2->priv->device_id &&
      device1->priv->vendor == device2->priv->vendor) {
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_hip_device_get_vendor:
 * @device: a #GstHipDevice
 *
 * Gets vendor of @device
 *
 * Returns: #GstHipVendor
 *
 * Since: 1.28
 */
GstHipVendor
gst_hip_device_get_vendor (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), GST_HIP_VENDOR_UNKNOWN);

  return device->priv->vendor;
}

/**
 * gst_hip_device_get_device_id:
 * @device: a #GstHipDevice
 *
 * Gets numeric device identifier of @device
 *
 * Returns: the device identifier
 *
 * Since: 1.28
 */
guint
gst_hip_device_get_device_id (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), (guint) - 1);

  return device->priv->device_id;
}

/**
 * gst_hip_device_get_stream:
 * @device: a #GstHipDevice
 *
 * Gets per #GstHipDevice default #GstHipStream owned by @device
 *
 * Returns: a #GstHipStream
 *
 * Since: 1.28
 */
GstHipStream *
gst_hip_device_get_stream (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);

  return device->priv->stream;
}
