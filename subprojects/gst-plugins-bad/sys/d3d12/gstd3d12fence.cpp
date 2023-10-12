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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12fence.h"
#include "gstd3d12device.h"
#include "gstd3d12utils.h"
#include <wrl.h>
#include <mutex>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_fence_debug);
#define GST_CAT_DEFAULT gst_d3d12_fence_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12Fence, gst_d3d12_fence);

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct _GstD3D12FencePrivate
{
  _GstD3D12FencePrivate()
  {
    event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~_GstD3D12FencePrivate()
  {
    CloseHandle (event_handle);
  }

  ComPtr<ID3D12Fence> fence;
  HANDLE event_handle;
  std::mutex lock;
  guint64 value = 0;
};
/* *INDENT-ON* */

static void
gst_d3d12_fence_free (GstD3D12Fence * self)
{
  if (!self)
    return;

  GST_TRACE ("Freeing fence %p", self);

  gst_clear_object (&self->device);
  delete self->priv;

  g_free (self);
}

GstD3D12Fence *
gst_d3d12_fence_new (GstD3D12Device * device)
{
  GstD3D12Fence *self;
  GstD3D12FencePrivate *priv;
  ID3D12Device *device_handle;
  HRESULT hr;
  ComPtr < ID3D12Fence > fence;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  device_handle = gst_d3d12_device_get_device_handle (device);
  hr = device_handle->CreateFence (0,
      D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&fence));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Failed to create fence, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  priv = new GstD3D12FencePrivate ();
  priv->fence = fence;

  self = g_new0 (GstD3D12Fence, 1);
  GST_TRACE_OBJECT (device, "Creating fence %p", self);

  self->device = (GstD3D12Device *) gst_object_ref (device);
  self->priv = priv;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (self), 0, GST_TYPE_D3D12_FENCE,
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_fence_free);

  return self;
}

gboolean
gst_d3d12_fence_set_event_on_completion_value (GstD3D12Fence * fence,
    guint64 value)
{
  GstD3D12FencePrivate *priv;

  g_return_val_if_fail (fence != nullptr, FALSE);

  priv = fence->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  auto current = priv->fence->GetCompletedValue ();
  if (value > current)
    priv->value = value;

  return TRUE;
}

ID3D12Fence *
gst_d3d12_fence_get_handle (GstD3D12Fence * fence)
{
  g_return_val_if_fail (fence != nullptr, nullptr);

  return fence->priv->fence.Get ();
}

void
gst_d3d12_fence_wait_for (GstD3D12Fence * fence, guint timeout_ms)
{
  g_return_if_fail (fence != nullptr);

  GstD3D12FencePrivate *priv = fence->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->value)
    return;

  auto current = priv->fence->GetCompletedValue ();
  if (current < priv->value) {
    HRESULT hr;
    GST_TRACE ("Waiting for fence to be signalled with value %" G_GUINT64_FORMAT
        ", current: %" G_GUINT64_FORMAT, priv->value, current);

    hr = priv->fence->SetEventOnCompletion (priv->value, priv->event_handle);
    if (!gst_d3d12_result (hr, fence->device)) {
      GST_ERROR_OBJECT (fence->device, "Failed to set completion event");
      return;
    }

    WaitForSingleObjectEx (priv->event_handle, timeout_ms, FALSE);
    GST_TRACE ("Signalled with value %" G_GUINT64_FORMAT, priv->value);
  } else {
    GST_TRACE ("target %" G_GUINT64_FORMAT " <= target: %" G_GUINT64_FORMAT,
        priv->value, current);
  }
}

void
gst_d3d12_fence_wait (GstD3D12Fence * fence)
{
  gst_d3d12_fence_wait_for (fence, INFINITE);
}

GstD3D12Fence *
gst_d3d12_fence_ref (GstD3D12Fence * fence)
{
  return (GstD3D12Fence *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (fence));
}

void
gst_d3d12_fence_unref (GstD3D12Fence * fence)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (fence));
}
