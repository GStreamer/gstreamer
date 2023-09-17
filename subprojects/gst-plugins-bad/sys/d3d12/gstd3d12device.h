/* GStreamer
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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstd3d12_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_DEVICE             (gst_d3d12_device_get_type())
#define GST_D3D12_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D12_DEVICE,GstD3D12Device))
#define GST_D3D12_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D12_DEVICE,GstD3D12DeviceClass))
#define GST_D3D12_DEVICE_GET_CLASS(obj)   (GST_D3D12_DEVICE_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D12_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D12_DEVICE))
#define GST_IS_D3D12_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D12_DEVICE))
#define GST_D3D12_DEVICE_CAST(obj)        ((GstD3D12Device*)(obj))

#define GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE "gst.d3d12.device.handle"

struct _GstD3D12Device
{
  GstObject parent;

  /*< private >*/
  GstD3D12DevicePrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstD3D12DeviceClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType                   gst_d3d12_device_get_type                 (void);

GstD3D12Device *        gst_d3d12_device_new                      (guint adapter_index);

GstD3D12Device *        gst_d3d12_device_new_for_adapter_luid     (gint64 adapter_luid);

ID3D12Device *          gst_d3d12_device_get_device_handle        (GstD3D12Device * device);

IDXGIAdapter1 *         gst_d3d12_device_get_adapter_handle       (GstD3D12Device * device);

IDXGIFactory2 *         gst_d3d12_device_get_factory_handle       (GstD3D12Device * device);

gboolean                gst_d3d12_device_get_format               (GstD3D12Device * device,
                                                                   GstVideoFormat format,
                                                                   GstD3D12Format * device_format);

guint64                 gst_d3d12_device_get_fence_value          (GstD3D12Device * device);

ID3D12CommandQueue *    gst_d3d12_device_get_copy_queue           (GstD3D12Device * device);

void                    gst_d3d12_device_d3d12_debug              (GstD3D12Device * device,
                                                                   const gchar * file,
                                                                   const gchar * function,
                                                                   gint line);

G_END_DECLS

