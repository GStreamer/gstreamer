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

#pragma once

#include <gst/gst.h>
#include "gsthip_fwd.h"
#include "gsthip-enums.h"
#include <hip/hip_runtime.h>

G_BEGIN_DECLS

#define GST_TYPE_HIP_DEVICE             (gst_hip_device_get_type())
#define GST_HIP_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HIP_DEVICE,GstHipDevice))
#define GST_HIP_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_HIP_DEVICE,GstHipDeviceClass))
#define GST_HIP_DEVICE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_HIP_DEVICE,GstHipDeviceClass))
#define GST_IS_HIP_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),GST_TYPE_HIP_DEVICE))
#define GST_IS_HIP_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HIP_DEVICE))

#define GST_HIP_DEVICE_CONTEXT_TYPE "gst.hip.device"

struct _GstHipDevice
{
  GstObject object;

  /*< private >*/
  GstHipDevicePrivate *priv;
};

struct _GstHipDeviceClass
{
  GstObjectClass parent_class;
};

GType          gst_hip_device_get_type    (void);

GstHipDevice * gst_hip_device_new         (GstHipVendor vendor,
                                           guint device_id);

gboolean       gst_hip_device_set_current (GstHipDevice * device);

hipError_t     gst_hip_device_get_attribute (GstHipDevice * device,
                                             hipDeviceAttribute_t attr,
                                             gint * value);

gboolean       gst_hip_device_is_equal    (GstHipDevice * device1,
                                           GstHipDevice * device2);

GstHipVendor   gst_hip_device_get_vendor  (GstHipDevice * device);

guint          gst_hip_device_get_device_id  (GstHipDevice * device);

GstHipStream * gst_hip_device_get_stream (GstHipDevice * device);

G_END_DECLS

