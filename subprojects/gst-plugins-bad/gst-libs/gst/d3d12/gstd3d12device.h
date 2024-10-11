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
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_DEVICE             (gst_d3d12_device_get_type())
#define GST_D3D12_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D12_DEVICE,GstD3D12Device))
#define GST_D3D12_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D12_DEVICE,GstD3D12DeviceClass))
#define GST_D3D12_DEVICE_GET_CLASS(obj)   (GST_D3D12_DEVICE_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D12_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D12_DEVICE))
#define GST_IS_D3D12_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D12_DEVICE))
#define GST_D3D12_DEVICE_CAST(obj)        ((GstD3D12Device*)(obj))

#define GST_D3D12_DEVICE_HANDLE_CONTEXT_TYPE "gst.d3d12.device.handle"

/**
 * GstD3D12Device:
 *
 * Opaque GstD3D12Device struct
 *
 * Since: 1.26
 */
struct _GstD3D12Device
{
  GstObject parent;

  /*< private >*/
  GstD3D12DevicePrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12DeviceClass:
 *
 * Opaque GstD3D12DeviceClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12DeviceClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType                   gst_d3d12_device_get_type                 (void);

GST_D3D12_API
GstD3D12Device *        gst_d3d12_device_new                      (guint adapter_index);

GST_D3D12_API
GstD3D12Device *        gst_d3d12_device_new_for_adapter_luid     (gint64 adapter_luid);

GST_D3D12_API
ID3D12Device *          gst_d3d12_device_get_device_handle        (GstD3D12Device * device);

GST_D3D12_API
IDXGIAdapter1 *         gst_d3d12_device_get_adapter_handle       (GstD3D12Device * device);

GST_D3D12_API
IDXGIFactory2 *         gst_d3d12_device_get_factory_handle       (GstD3D12Device * device);

GST_D3D12_API
ID3D12Fence *           gst_d3d12_device_get_fence_handle         (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type);

GST_D3D12_API
gboolean                gst_d3d12_device_get_format               (GstD3D12Device * device,
                                                                   GstVideoFormat format,
                                                                   GstD3D12Format * device_format);

GST_D3D12_API
GstD3D12CmdQueue *      gst_d3d12_device_get_cmd_queue            (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type);

GST_D3D12_API
HRESULT                 gst_d3d12_device_execute_command_lists    (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type,
                                                                   guint num_command_lists,
                                                                   ID3D12CommandList ** command_lists,
                                                                   guint64 * fence_value);

GST_D3D12_API
guint64                 gst_d3d12_device_get_completed_value      (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type);

GST_D3D12_API
gboolean                gst_d3d12_device_set_fence_notify         (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type,
                                                                   guint64 fence_value,
                                                                   gpointer fence_data,
                                                                   GDestroyNotify notify);

GST_D3D12_API
HRESULT                 gst_d3d12_device_fence_wait               (GstD3D12Device * device,
                                                                   D3D12_COMMAND_LIST_TYPE queue_type,
                                                                   guint64 fence_value);

GST_D3D12_API
gboolean                gst_d3d12_device_is_equal                 (GstD3D12Device * device1,
                                                                   GstD3D12Device * device2);

G_END_DECLS

