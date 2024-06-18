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

#include "gstd3d12fencedatapool.h"
#include <gst/base/gstqueuearray.h>
#include <mutex>
#include <queue>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_fence_data_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_fence_data_pool_debug

struct NotifyData
{
  gpointer user_data = nullptr;
  GDestroyNotify notify = nullptr;
};

static void
notify_data_clear_func (NotifyData * data)
{
  if (data->notify)
    data->notify (data->user_data);

  data->user_data = nullptr;
  data->notify = nullptr;
}

/* *INDENT-OFF* */
struct _GstD3D12FenceData : public GstMiniObject
{
  _GstD3D12FenceData ()
  {
    queue = gst_vec_deque_new_for_struct (sizeof (NotifyData), 4);
    gst_vec_deque_set_clear_func (queue,
        (GDestroyNotify) notify_data_clear_func);
  }

  ~_GstD3D12FenceData ()
  {
    gst_vec_deque_free (queue);
  }

  GstD3D12FenceDataPool *pool = nullptr;
  GstVecDeque *queue;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12FenceData, gst_d3d12_fence_data);

struct _GstD3D12FenceDataPoolPrivate
{
  ~_GstD3D12FenceDataPoolPrivate ()
  {
    while (!data_pool.empty ()) {
      auto data = data_pool.front ();
      data_pool.pop ();
      gst_mini_object_unref (data);
    }
  }

  std::mutex lock;
  std::queue<GstD3D12FenceData *>data_pool;
};
/* *INDENT-ON* */

static void gst_d3d12_fence_data_pool_finalize (GObject * object);

#define gst_d3d12_fence_data_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12FenceDataPool,
    gst_d3d12_fence_data_pool, GST_TYPE_OBJECT);

static void
gst_d3d12_fence_data_pool_class_init (GstD3D12FenceDataPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_fence_data_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_fence_data_pool_debug,
      "d3d12fencedatapool", 0, "d3d12fencedatapool");
}

static void
gst_d3d12_fence_data_pool_init (GstD3D12FenceDataPool * self)
{
  self->priv = new GstD3D12FenceDataPoolPrivate ();
}

static void
gst_d3d12_fence_data_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_FENCE_DATA_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_d3d12_fence_data_pool_new:
 *
 * Creates #GstD3D12FenceDataPool instance
 *
 * Returns: (transfer full): a #GstD3D12FenceDataPool instance
 *
 * Since: 1.26
 */
GstD3D12FenceDataPool *
gst_d3d12_fence_data_pool_new (void)
{
  auto self = (GstD3D12FenceDataPool *)
      g_object_new (GST_TYPE_D3D12_FENCE_DATA_POOL, nullptr);
  gst_object_ref_sink (self);

  return self;
}

static void
gst_d3d12_fence_data_pool_release (GstD3D12FenceDataPool * pool,
    GstD3D12FenceData * data)
{
  auto priv = pool->priv;
  gst_vec_deque_clear (data->queue);

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    data->dispose = nullptr;
    data->pool = nullptr;
    priv->data_pool.push (data);
  }

  gst_object_unref (pool);
}

static void
gst_d3d12_fence_data_free (GstD3D12FenceData * data)
{
  delete data;
}

static gboolean
gst_d3d12_fence_data_dispose (GstD3D12FenceData * data)
{
  if (!data->pool)
    return TRUE;

  gst_mini_object_ref (data);
  gst_d3d12_fence_data_pool_release (data->pool, data);
  return FALSE;
}

static GstD3D12FenceData *
gst_d3d12_fence_data_new (void)
{
  auto data = new GstD3D12FenceData ();

  gst_mini_object_init (data, 0, gst_d3d12_fence_data_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_fence_data_free);

  return data;
}

/**
 * gst_d3d12_fence_data_pool_acquire:
 * @pool: a #GstD3D12FenceDataPool
 * @data: (out) (transfer full): a pointer to #GstD3D12FenceData
 *
 * Acquire #GstD3D12CommandAllocator object
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_fence_data_pool_acquire (GstD3D12FenceDataPool * pool,
    GstD3D12FenceData ** data)
{
  g_return_val_if_fail (GST_IS_D3D12_FENCE_DATA_POOL (pool), FALSE);
  g_return_val_if_fail (data, FALSE);

  auto priv = pool->priv;
  GstD3D12FenceData *new_data = nullptr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->data_pool.empty ()) {
      new_data = priv->data_pool.front ();
      priv->data_pool.pop ();
    }
  }

  if (!new_data)
    new_data = gst_d3d12_fence_data_new ();

  new_data->pool = (GstD3D12FenceDataPool *) gst_object_ref (pool);
  new_data->dispose =
      (GstMiniObjectDisposeFunction) gst_d3d12_fence_data_dispose;

  *data = new_data;

  return TRUE;
}

/**
 * gst_d3d12_fence_data_push:
 * @data: a #GstD3D12FenceData
 * @user_data: private data
 * @notify: a #GDestroyNotify
 *
 * Sets notify callback
 *
 * Since: 1.26
 */
void
gst_d3d12_fence_data_push (GstD3D12FenceData * data, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (data);

  NotifyData notify_data;

  notify_data.user_data = user_data;
  notify_data.notify = notify;

  gst_vec_deque_push_tail_struct (data->queue, &notify_data);
}

/**
 * gst_d3d12_fence_data_ref:
 * @data: a #GstD3D12FenceData
 *
 * Increments the refcount of @data
 *
 * Returns: (transfer full): a #GstD3D12FenceData
 *
 * Since: 1.26
 */
GstD3D12FenceData *
gst_d3d12_fence_data_ref (GstD3D12FenceData * data)
{
  return (GstD3D12FenceData *) gst_mini_object_ref (data);
}

/**
 * gst_d3d12_fence_data_unref:
 * @cmd: a #GstD3D12FenceData
 *
 * Decrements the refcount of @data
 *
 * Since: 1.26
 */
void
gst_d3d12_fence_data_unref (GstD3D12FenceData * data)
{
  gst_mini_object_unref (data);
}

/**
 * gst_clear_d3d12_fence_data:
 * @data: a pointer to #GstD3D12FenceData
 *
 * Clears a reference to a #GstD3D12FenceData
 *
 * Since: 1.26
 */
void
gst_clear_d3d12_fence_data (GstD3D12FenceData ** data)
{
  gst_clear_mini_object (data);
}
