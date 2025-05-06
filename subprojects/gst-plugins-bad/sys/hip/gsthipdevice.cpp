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
#include "gsthiploader.h"
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
  PROP_TEXTURE2D_SUPPORT,
};

struct _GstHipDevicePrivate
{
  guint device_id;
  gboolean texture_support;
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

static hipError_t
gst_hip_init_once (void)
{
  static hipError_t ret = hipErrorInitializationError;
  static std::once_flag once;

  std::call_once (once,[&] {
        ret = HipInit (0);
      });

  return ret;
}

GstHipDevice *
gst_hip_device_new (guint device_id)
{
  if (!gst_hip_load_library ()) {
    GST_INFO ("Couldn't load HIP library");
    return nullptr;
  }

  auto hip_ret = gst_hip_init_once ();
  if (hip_ret != hipSuccess) {
    GST_DEBUG ("Couldn't initialize HIP, error: %d", hip_ret);
    return nullptr;
  }

  int num_dev = 0;
  hip_ret = HipGetDeviceCount (&num_dev);
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
  hip_ret = HipDeviceGetAttribute (&val,
      hipDeviceAttributeMaxTexture2DWidth, device_id);
  if (hip_ret == hipSuccess && val > 0) {
    hip_ret = HipDeviceGetAttribute (&val,
        hipDeviceAttributeMaxTexture2DHeight, device_id);
    if (hip_ret == hipSuccess && val > 0) {
      hip_ret = HipDeviceGetAttribute (&val,
          hipDeviceAttributeTextureAlignment, device_id);
      if (hip_ret == hipSuccess && val > 0) {
        texture_support = TRUE;
      }
    }
  }

  auto self = (GstHipDevice *) g_object_new (GST_TYPE_HIP_DEVICE, nullptr);
  gst_object_ref_sink (self);
  self->priv->device_id = device_id;
  self->priv->texture_support = texture_support;

  return self;
}

gboolean
gst_hip_device_set_current (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), FALSE);

  auto hip_ret = HipSetDevice (device->priv->device_id);
  if (!gst_hip_result (hip_ret)) {
    GST_ERROR_OBJECT (device, "hipSetDevice result %d", hip_ret);
    return FALSE;
  }

  return TRUE;
}

hipError_t
gst_hip_device_get_attribute (GstHipDevice * device, hipDeviceAttribute_t attr,
    gint * value)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), hipErrorInvalidDevice);

  return HipDeviceGetAttribute (value, attr, device->priv->device_id);
}

gboolean
gst_hip_device_is_equal (GstHipDevice * device1, GstHipDevice * device2)
{
  if (!device1 || !device2)
    return FALSE;

  g_return_val_if_fail (GST_IS_HIP_DEVICE (device1), FALSE);
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device2), FALSE);

  if (device1 == device2)
    return TRUE;

  if (device1->priv->device_id == device2->priv->device_id)
    return TRUE;

  return FALSE;
}
