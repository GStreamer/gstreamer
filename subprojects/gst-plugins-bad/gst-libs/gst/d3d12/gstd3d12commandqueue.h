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

#define GST_TYPE_D3D12_COMMAND_QUEUE            (gst_d3d12_command_queue_get_type ())
#define GST_D3D12_COMMAND_QUEUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_COMMAND_QUEUE, GstD3D12CommandQueue))
#define GST_D3D12_COMMAND_QUEUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_COMMAND_QUEUE, GstD3D12CommandQueueClass))
#define GST_IS_D3D12_COMMAND_QUEUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_COMMAND_QUEUE))
#define GST_IS_D3D12_COMMAND_QUEUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_COMMAND_QUEUE))
#define GST_D3D12_COMMAND_QUEUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_COMMAND_QUEUE, GstD3D12CommandQueueClass))
#define GST_D3D12_COMMAND_QUEUE_CAST(obj)       ((GstD3D12CommandQueue*)(obj))

/**
 * GstD3D12CommandQueue:
 *
 * Opaque GstD3D12CommandQueue struct
 *
 * Since: 1.26
 */
struct _GstD3D12CommandQueue
{
  GstObject parent;

  /*< private >*/
  GstD3D12CommandQueuePrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12CommandQueueClass:
 *
 * Opaque GstD3D12CommandQueueClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12CommandQueueClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType                  gst_d3d12_command_queue_get_type (void);

GST_D3D12_API
GstD3D12CommandQueue * gst_d3d12_command_queue_new (ID3D12Device * device,
                                                    const D3D12_COMMAND_QUEUE_DESC * desc,
                                                    D3D12_FENCE_FLAGS fence_flags,
                                                    guint queue_size);

GST_D3D12_API
ID3D12CommandQueue *   gst_d3d12_command_queue_get_handle (GstD3D12CommandQueue * queue);

GST_D3D12_API
ID3D12Fence *          gst_d3d12_command_queue_get_fence_handle (GstD3D12CommandQueue * queue);

GST_D3D12_API
HRESULT                gst_d3d12_command_queue_execute_command_lists (GstD3D12CommandQueue * queue,
                                                                      guint num_command_lists,
                                                                      ID3D12CommandList ** command_lists,
                                                                      guint64 * fence_value);

GST_D3D12_API
HRESULT                gst_d3d12_command_queue_execute_command_lists_full (GstD3D12CommandQueue * queue,
                                                                           guint num_fences_to_wait,
                                                                           ID3D12Fence ** fences_to_wait,
                                                                           const guint64 * fence_values_to_wait,
                                                                           guint num_command_lists,
                                                                           ID3D12CommandList ** command_lists,
                                                                           guint64 * fence_value);

GST_D3D12_API
HRESULT                gst_d3d12_command_queue_execute_wait (GstD3D12CommandQueue * queue,
                                                             ID3D12Fence * fence,
                                                             guint64 fence_value);

GST_D3D12_API
guint64                gst_d3d12_command_queue_get_completed_value (GstD3D12CommandQueue * queue);

GST_D3D12_API
HRESULT                gst_d3d12_command_queue_fence_wait   (GstD3D12CommandQueue * queue,
                                                             guint64 fence_value,
                                                             HANDLE event_handle);

GST_D3D12_API
void                   gst_d3d12_command_queue_set_notify (GstD3D12CommandQueue * queue,
                                                           guint64 fence_value,
                                                           gpointer fence_data,
                                                           GDestroyNotify notify);

GST_D3D12_API
HRESULT                gst_d3d12_command_queue_drain      (GstD3D12CommandQueue * queue);

G_END_DECLS
