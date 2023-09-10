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
#include "gstd3d12_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_FENCE    (gst_d3d12_fence_get_type())
#define GST_D3D12_FENCE_CAST(f) ((GstD3D12Fence *)f)

struct _GstD3D12Fence
{
  GstMiniObject parent;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12FencePrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

GType           gst_d3d12_fence_get_type (void);

GstD3D12Fence * gst_d3d12_fence_new (GstD3D12Device * device);

ID3D12Fence *   gst_d3d12_fence_get_handle (GstD3D12Fence * fence);

gboolean        gst_d3d12_fence_set_event_on_completion_value (GstD3D12Fence * fence,
                                                               guint64 value);

void            gst_d3d12_fence_wait_for (GstD3D12Fence * fence,
                                          guint timeout_ms);

void            gst_d3d12_fence_wait (GstD3D12Fence * fence);

GstD3D12Fence * gst_d3d12_fence_ref (GstD3D12Fence * fence);

void            gst_d3d12_fence_unref (GstD3D12Fence * fence);

G_END_DECLS

