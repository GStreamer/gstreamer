/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you cln redistribute it and/or
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

#include "gstd3d12.h"
#include <wrl.h>
#include <queue>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_desc_heap_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_desc_heap_pool_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct _GstD3D12DescHeap : public GstMiniObject
{
  GstD3D12DescHeapPool *pool = nullptr;
  ComPtr < ID3D12DescriptorHeap > heap;
};

struct _GstD3D12DescHeapPoolPrivate
{
  ~_GstD3D12DescHeapPoolPrivate ()
  {
    while (!heap_pool.empty ()) {
      auto heap = heap_pool.front ();
      heap_pool.pop ();
      gst_mini_object_unref (heap);
    }
  }

  ComPtr<ID3D12Device> device;

  std::mutex lock;
  std::queue<GstD3D12DescHeap *>heap_pool;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DescHeap, gst_d3d12_desc_heap);

static void gst_d3d12_desc_heap_pool_finalize (GObject * object);

#define gst_d3d12_desc_heap_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12DescHeapPool, gst_d3d12_desc_heap_pool, GST_TYPE_OBJECT);

static void
gst_d3d12_desc_heap_pool_class_init (GstD3D12DescHeapPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_desc_heap_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_desc_heap_pool_debug,
      "d3d12descheappool", 0, "d3d12descheappool");
}

static void
gst_d3d12_desc_heap_pool_init (GstD3D12DescHeapPool * self)
{
  self->priv = new GstD3D12DescHeapPoolPrivate ();
}

static void
gst_d3d12_desc_heap_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_DESC_HEAP_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_d3d12_desc_heap_pool_new:
 * @device: a #GstD3D12Device
 * @type: D3D12_DESCRIPTOR_HEAP_DESC
 *
 * Returns: (transfer full): a new #GstD3D12DescHeapPool instance
 *
 * Since: 1.26
 */
GstD3D12DescHeapPool *
gst_d3d12_desc_heap_pool_new (ID3D12Device * device,
    const D3D12_DESCRIPTOR_HEAP_DESC * desc)
{
  g_return_val_if_fail (device, nullptr);
  g_return_val_if_fail (desc, nullptr);

  auto self = (GstD3D12DescHeapPool *)
      g_object_new (GST_TYPE_D3D12_DESC_HEAP_POOL, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = device;
  priv->heap_desc = *desc;

  return self;
}

static void
gst_d3d12_desc_heap_pool_release (GstD3D12DescHeapPool * pool,
    GstD3D12DescHeap * heap)
{
  auto priv = pool->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    heap->dispose = nullptr;
    heap->pool = nullptr;
    priv->heap_pool.push (heap);
  }

  gst_object_unref (pool);
}

static gboolean
gst_d3d12_desc_heap_dispose (GstD3D12DescHeap * heap)
{
  if (!heap->pool)
    return TRUE;

  gst_mini_object_ref (heap);
  gst_d3d12_desc_heap_pool_release (heap->pool, heap);

  return FALSE;
}

static void
gst_d3d12_desc_heap_free (GstD3D12DescHeap * heap)
{
  delete heap;
}

static GstD3D12DescHeap *
gst_d3d12_desc_heap_new (ID3D12DescriptorHeap * handle)
{
  auto heap = new GstD3D12DescHeap ();
  heap->heap = handle;

  gst_mini_object_init (heap, 0, gst_d3d12_desc_heap_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_desc_heap_free);

  return heap;
}

/**
 * gst_d3d12_desc_heap_pool_acquire:
 * @pool: a #GstD3D12DescHeapPool
 * @heap: (out) (transfer full): a pointer to GstD3D12DescHeap
 *
 * Acquire #GstD3D12DescHeap object
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_desc_heap_pool_acquire (GstD3D12DescHeapPool * pool,
    GstD3D12DescHeap ** heap)
{
  g_return_val_if_fail (GST_IS_D3D12_DESC_HEAP_POOL (pool), FALSE);
  g_return_val_if_fail (heap, FALSE);

  *heap = nullptr;

  auto priv = pool->priv;
  GstD3D12DescHeap *new_heap = nullptr;
  HRESULT hr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->heap_pool.empty ()) {
      new_heap = priv->heap_pool.front ();
      priv->heap_pool.pop ();
    }
  }

  if (!new_heap) {
    ComPtr < ID3D12DescriptorHeap > heap;
    hr = priv->device->CreateDescriptorHeap (&priv->heap_desc,
        IID_PPV_ARGS (&heap));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't create descriptor heap, hr: 0x%x",
          (guint) hr);
      return FALSE;
    }

    new_heap = gst_d3d12_desc_heap_new (heap.Get ());
  }

  new_heap->pool = (GstD3D12DescHeapPool *) gst_object_ref (pool);
  new_heap->dispose =
      (GstMiniObjectDisposeFunction) gst_d3d12_desc_heap_dispose;

  *heap = new_heap;

  return TRUE;
}

/**
 * gst_d3d12_desc_heap_ref:
 * @heap: a #GstD3D12DescHeap
 *
 * Increments the refcount of @desc
 *
 * Returns: (transfer full): a #GstD3D12DescHeap
 *
 * Since: 1.26
 */
GstD3D12DescHeap *
gst_d3d12_desc_heap_ref (GstD3D12DescHeap * heap)
{
  return (GstD3D12DescHeap *) gst_mini_object_ref (heap);
}

/**
 * gst_d3d12_desc_heap_unref:
 * @heap: a #GstD3D12DescHeap
 *
 * Decrements the refcount of @heap
 *
 * Since: 1.26
 */
void
gst_d3d12_desc_heap_unref (GstD3D12DescHeap * heap)
{
  gst_mini_object_unref (heap);
}

/**
 * gst_clear_d3d12_desc_heap:
 * @heap: a pointer to #GstD3D12DescHeap
 *
 * Clears a reference to a #GstD3D12DescHeap
 *
 * Since: 1.26
 */
void
gst_clear_d3d12_desc_heap (GstD3D12DescHeap ** heap)
{
  gst_clear_mini_object (heap);
}

/**
 * gst_d3d12_desc_heap_get_handle:
 * @desc: a #GstD3D12DescHeap
 *
 * Gets ID3D12DescriptorHeap handle.
 *
 * Returns: (transfer none): ID3D12DescriptorHeap handle
 *
 * Since: 1.26
 */
ID3D12DescriptorHeap *
gst_d3d12_desc_heap_get_handle (GstD3D12DescHeap * heap)
{
  g_return_val_if_fail (heap, nullptr);

  return heap->heap.Get ();
}
