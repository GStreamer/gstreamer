/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gsthipfencedatapool.h"
#include <gst/gstvecdeque.h>
#include <mutex>
#include <queue>

GST_DEBUG_CATEGORY_STATIC (gst_hip_fence_data_pool_debug);
#define GST_CAT_DEFAULT gst_hip_fence_data_pool_debug

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
struct _GstHipFenceData : public GstMiniObject
{
  _GstHipFenceData ()
  {
    queue = gst_vec_deque_new_for_struct (sizeof (NotifyData), 4);
    gst_vec_deque_set_clear_func (queue,
        (GDestroyNotify) notify_data_clear_func);
  }

  ~_GstHipFenceData ()
  {
    gst_vec_deque_free (queue);
  }

  GstHipFenceDataPool *pool = nullptr;
  GstVecDeque *queue;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstHipFenceData, gst_hip_fence_data);

struct _GstHipFenceDataPoolPrivate
{
  ~_GstHipFenceDataPoolPrivate ()
  {
    while (!data_pool.empty ()) {
      auto data = data_pool.front ();
      data_pool.pop ();
      gst_mini_object_unref (data);
    }
  }

  std::mutex lock;
  std::queue<GstHipFenceData *>data_pool;
};
/* *INDENT-ON* */

static void gst_hip_fence_data_pool_finalize (GObject * object);

#define gst_hip_fence_data_pool_parent_class parent_class
G_DEFINE_TYPE (GstHipFenceDataPool, gst_hip_fence_data_pool, GST_TYPE_OBJECT);

static void
gst_hip_fence_data_pool_class_init (GstHipFenceDataPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_hip_fence_data_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_hip_fence_data_pool_debug,
      "hipfencedatapool", 0, "hipfencedatapool");
}

static void
gst_hip_fence_data_pool_init (GstHipFenceDataPool * self)
{
  self->priv = new GstHipFenceDataPoolPrivate ();
}

static void
gst_hip_fence_data_pool_finalize (GObject * object)
{
  auto self = GST_HIP_FENCE_DATA_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_hip_fence_data_pool_new:
 *
 * Creates #GstHipFenceDataPool instance
 *
 * Returns: (transfer full): a #GstHipFenceDataPool instance
 *
 * Since: 1.30
 */
GstHipFenceDataPool *
gst_hip_fence_data_pool_new (void)
{
  auto self = (GstHipFenceDataPool *)
      g_object_new (GST_TYPE_HIP_FENCE_DATA_POOL, nullptr);
  gst_object_ref_sink (self);

  return self;
}

static void
gst_hip_fence_data_pool_release (GstHipFenceDataPool * pool,
    GstHipFenceData * data)
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
gst_hip_fence_data_free (GstHipFenceData * data)
{
  delete data;
}

static gboolean
gst_hip_fence_data_dispose (GstHipFenceData * data)
{
  if (!data->pool)
    return TRUE;

  gst_mini_object_ref (data);
  gst_hip_fence_data_pool_release (data->pool, data);
  return FALSE;
}

static GstHipFenceData *
gst_hip_fence_data_new (void)
{
  auto data = new GstHipFenceData ();

  gst_mini_object_init (data, 0, gst_hip_fence_data_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_hip_fence_data_free);

  return data;
}

/**
 * gst_hip_fence_data_pool_acquire:
 * @pool: a #GstHipFenceDataPool
 * @data: (out) (transfer full): a pointer to #GstHipFenceData
 *
 * Acquire #GstHipFenceData object
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.30
 */
gboolean
gst_hip_fence_data_pool_acquire (GstHipFenceDataPool * pool,
    GstHipFenceData ** data)
{
  g_return_val_if_fail (GST_IS_HIP_FENCE_DATA_POOL (pool), FALSE);
  g_return_val_if_fail (data, FALSE);

  auto priv = pool->priv;
  GstHipFenceData *new_data = nullptr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->data_pool.empty ()) {
      new_data = priv->data_pool.front ();
      priv->data_pool.pop ();
    }
  }

  if (!new_data)
    new_data = gst_hip_fence_data_new ();

  new_data->pool = (GstHipFenceDataPool *) gst_object_ref (pool);
  new_data->dispose = (GstMiniObjectDisposeFunction) gst_hip_fence_data_dispose;

  *data = new_data;

  return TRUE;
}

/**
 * gst_hip_fence_data_push:
 * @data: a #GstHipFenceData
 * @user_data: private data
 * @notify: a #GDestroyNotify
 *
 * Adds a notify callback
 *
 * Since: 1.30
 */
void
gst_hip_fence_data_push (GstHipFenceData * data, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (data);

  NotifyData notify_data;

  notify_data.user_data = user_data;
  notify_data.notify = notify;

  gst_vec_deque_push_tail_struct (data->queue, &notify_data);
}

/**
 * gst_hip_fence_data_ref:
 * @data: a #GstHipFenceData
 *
 * Increments the refcount of @data
 *
 * Returns: (transfer full): a #GstHipFenceData
 *
 * Since: 1.30
 */
GstHipFenceData *
gst_hip_fence_data_ref (GstHipFenceData * data)
{
  return (GstHipFenceData *) gst_mini_object_ref (data);
}

/**
 * gst_hip_fence_data_unref:
 * @data: a #GstHipFenceData
 *
 * Decrements the refcount of @data
 *
 * Since: 1.30
 */
void
gst_hip_fence_data_unref (GstHipFenceData * data)
{
  gst_mini_object_unref (data);
}

/**
 * gst_clear_hip_fence_data:
 * @data: a pointer to #GstHipFenceData
 *
 * Clears a reference to a #GstHipFenceData
 *
 * Since: 1.30
 */
void
gst_clear_hip_fence_data (GstHipFenceData ** data)
{
  gst_clear_mini_object (data);
}
