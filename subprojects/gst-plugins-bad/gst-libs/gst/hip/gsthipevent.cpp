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

#include "gsthip-config.h"
#include "gsthip.h"
#include "gsthip-private.h"
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hipevent", 0, "hipevent");
      });

  return cat;
}
#endif

/* *INDENT-OFF* */
struct _GstHipEvent : public GstMiniObject
{
  ~_GstHipEvent ()
  {
    if (handle) {
      auto hip_ret = HipSetDevice (vendor, device_id);
      if (gst_hip_result (hip_ret, vendor)) {
        HipEventSynchronize (vendor, handle);
        HipEventDestroy (vendor, handle);
      }
    }
  }

  GstHipEventPool *pool = nullptr;
  hipEvent_t handle = nullptr;
  gboolean recorded = FALSE;
  GstHipVendor vendor;
  guint device_id;
};

struct _GstHipEventPoolPrivate
{
  ~_GstHipEventPoolPrivate ()
  {
    while (!event_pool.empty ()) {
      auto event = event_pool.front ();
      event_pool.pop ();
      gst_mini_object_unref (event);
    }
  }

  GstHipVendor vendor;
  guint device_id;
  std::mutex lock;
  std::queue<GstHipEvent *>event_pool;
};

struct HipNotifyData
{
  GstHipEvent *event = nullptr;
  gpointer user_data = nullptr;
  GDestroyNotify notify = nullptr;
};


struct HipCompletionWorker
{
  HipCompletionWorker ()
  {
    thread_ = g_thread_new ("GstHipGc", Run, this);
  }

  ~HipCompletionWorker ()
  {
    {
      std::lock_guard<std::mutex> lk (lock_);
      shutdown_ = true;
      cond_.notify_one ();
    }
    g_thread_join (thread_);
  }

  void
  Push (GstHipEvent *event, gpointer user_data, GDestroyNotify notify)
  {
    std::lock_guard<std::mutex> lk (lock_);
    pending_.push ({ gst_hip_event_ref (event), user_data, notify });
    cond_.notify_one ();
  }

private:
  static gpointer
  Run (gpointer data)
  {
    auto self = static_cast<HipCompletionWorker *> (data);
    std::unique_lock<std::mutex> lk (self->lock_);
    while (true) {
      while (!self->shutdown_ && self->pending_.empty ())
        self->cond_.wait (lk);

      /* Keep processing pending notifications even if we are shutting down */
      if (self->pending_.empty ())
        break;

      auto item = self->pending_.front ();
      self->pending_.pop ();
      lk.unlock ();

      auto ret = gst_hip_event_query (item.event);
      if (ret == hipErrorNotReady)
        ret = gst_hip_event_synchronize (item.event);
      gst_hip_result (ret, gst_hip_event_get_vendor (item.event));

      if (item.notify)
        item.notify (item.user_data);
      gst_hip_event_unref (item.event);
      lk.lock ();
    }

    return nullptr;
  }

  std::mutex lock_;
  std::condition_variable cond_;
  std::queue<HipNotifyData> pending_;
  bool shutdown_ = false;
  GThread *thread_ = nullptr;
};

struct HipCompletionManager
{
  static HipCompletionManager * GetInstance ()
  {
    static auto manager = new HipCompletionManager ();
    return manager;
  }

  void Push (GstHipEvent *event, gpointer user_data, GDestroyNotify notify)
  {
    std::lock_guard<std::mutex> lk (lock_);
    auto key = std::make_pair (event->vendor, event->device_id);
    auto &worker = workers_[key];
    if (!worker)
      worker = std::make_unique<HipCompletionWorker> ();

    worker->Push (event, user_data, notify);
  }

  void Flush ()
  {
    std::lock_guard<std::mutex> flush_lk (flush_lock_);
    WorkerMap workers;
    {
      std::lock_guard<std::mutex> lk (lock_);
      workers.swap (workers_);
    }

    /* Joining also waits for callbacks, without holding the registry lock */
    workers.clear ();
  }

private:
  using WorkerMap = std::map<std::pair<GstHipVendor, guint>,
      std::unique_ptr<HipCompletionWorker>>;
  std::mutex lock_;
  std::mutex flush_lock_;
  WorkerMap workers_;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstHipEvent, gst_hip_event);

static void gst_hip_event_pool_finalize (GObject * object);

#define gst_hip_event_pool_parent_class parent_class
G_DEFINE_TYPE (GstHipEventPool, gst_hip_event_pool, GST_TYPE_OBJECT);

static void
gst_hip_event_pool_class_init (GstHipEventPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_hip_event_pool_finalize;
}

static void
gst_hip_event_pool_init (GstHipEventPool * self)
{
  self->priv = new GstHipEventPoolPrivate ();
}

static void
gst_hip_event_pool_finalize (GObject * object)
{
  auto self = GST_HIP_EVENT_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_hip_event_pool_new:
 * @vendor: a #GstHipVendor
 * @device_id: device identifier
 *
 * Creates a new event pool instance with @vendor and @device_id.
 *
 * Returns: (transfer full) (nullable): a #GstHipEventPool if succeeded,
 * otherwise %NULL
 *
 * Since: 1.28
 */
GstHipEventPool *
gst_hip_event_pool_new (GstHipVendor vendor, guint device_id)
{
  g_return_val_if_fail (vendor != GST_HIP_VENDOR_UNKNOWN, nullptr);

  auto self = (GstHipEventPool *)
      g_object_new (GST_TYPE_HIP_EVENT_POOL, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->vendor = vendor;
  priv->device_id = device_id;

  return self;
}

static void
gst_hip_event_pool_release (GstHipEventPool * pool, GstHipEvent * event)
{
  auto priv = pool->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    event->dispose = nullptr;
    event->pool = nullptr;
    event->recorded = FALSE;
    priv->event_pool.push (event);
  }

  gst_object_unref (pool);
}

static gboolean
gst_hip_event_dispose (GstHipEvent * event)
{
  if (!event->pool)
    return TRUE;

  gst_mini_object_ref (event);
  gst_hip_event_pool_release (event->pool, event);

  return FALSE;
}

static void
gst_hip_event_free (GstHipEvent * event)
{
  delete event;
}

/**
 * gst_hip_event_pool_acquire:
 * @pool: a #GstHipEventPool
 * @event: (out) (transfer full) (nullable): a location to store #GstHipEvent
 *
 * Acquires #GstHipEvent from @pool
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.28
 */
gboolean
gst_hip_event_pool_acquire (GstHipEventPool * pool, GstHipEvent ** event)
{
  g_return_val_if_fail (GST_IS_HIP_EVENT_POOL (pool), FALSE);
  g_return_val_if_fail (event, FALSE);

  *event = nullptr;

  auto priv = pool->priv;
  GstHipEvent *new_event = nullptr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->event_pool.empty ()) {
      new_event = priv->event_pool.front ();
      priv->event_pool.pop ();
    }
  }

  if (!new_event) {
    auto hip_ret = HipSetDevice (priv->vendor, priv->device_id);
    if (!gst_hip_result (hip_ret, priv->vendor)) {
      GST_ERROR_OBJECT (pool, "Couldn't set device");
      return FALSE;
    }

    hipEvent_t handle;
    hip_ret = HipEventCreateWithFlags (priv->vendor, &handle,
        hipEventDisableTiming);

    if (!gst_hip_result (hip_ret, priv->vendor)) {
      GST_ERROR_OBJECT (pool, "Couldn't create event");
      return FALSE;
    }

    new_event = new GstHipEvent ();
    new_event->handle = handle;
    new_event->vendor = priv->vendor;
    new_event->device_id = priv->device_id;

    gst_mini_object_init (new_event, 0, gst_hip_event_get_type (),
        nullptr, nullptr, (GstMiniObjectFreeFunction) gst_hip_event_free);
  }

  new_event->pool = (GstHipEventPool *) gst_object_ref (pool);
  new_event->dispose = (GstMiniObjectDisposeFunction) gst_hip_event_dispose;

  *event = new_event;

  return TRUE;
}

/**
 * gst_hip_event_get_vendor:
 * @event: a #GstHipEvent
 *
 * Gets device vendor of @event object
 *
 * Returns: #GstHipVendor
 *
 * Since: 1.28
 */
GstHipVendor
gst_hip_event_get_vendor (GstHipEvent * event)
{
  g_return_val_if_fail (event, GST_HIP_VENDOR_UNKNOWN);

  return event->vendor;
}

/**
 * gst_hip_event_get_device_id:
 * @event: a #GstHipEvent
 *
 * Gets numeric device identifier of @event object
 *
 * Returns: device identifier
 *
 * Since: 1.28
 */
guint
gst_hip_event_get_device_id (GstHipEvent * event)
{
  g_return_val_if_fail (event, G_MAXUINT);

  return event->device_id;
}

/**
 * gst_hip_event_record:
 * @event: a #GstHipEvent
 * @stream: (type gpointer): a hipStream_t handle
 *
 * Records operations currently scheduled by @stream to @event
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_event_record (GstHipEvent * event, hipStream_t stream)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  event->recorded = FALSE;

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  hip_ret = HipEventRecord (event->vendor, event->handle, stream);
  if (hip_ret == hipSuccess)
    event->recorded = TRUE;

  return hip_ret;
}

/**
 * gst_hip_event_query:
 * @event: a #GstHipEvent
 *
 * Queries event status via hipEventQuery()
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_event_query (GstHipEvent * event)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  return HipEventQuery (event->vendor, event->handle);
}

/**
 * gst_hip_event_synchronize:
 * @event: a #GstHipEvent
 *
 * Waits for recorded operations via hipEventSynchronize()
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_event_synchronize (GstHipEvent * event)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  return HipEventSynchronize (event->vendor, event->handle);
}

/**
 * gst_hip_event_ref:
 * @event: a #GstHipEvent
 *
 * Increments the reference count on @event
 *
 * Returns: (transfer full): a pointer to @event
 *
 * Since: 1.28
 */
GstHipEvent *
gst_hip_event_ref (GstHipEvent * event)
{
  return (GstHipEvent *) gst_mini_object_ref (event);
}

/**
 * gst_hip_event_unref:
 * @event: a #GstHipEvent
 *
 * Decrements the reference count on @event
 *
 * Since: 1.28
 */
void
gst_hip_event_unref (GstHipEvent * event)
{
  return gst_mini_object_unref (event);
}

/**
 * gst_clear_hip_event: (skip)
 * @event: a pointer to a #GstHipEvent
 *
 * Clears a reference to the @event
 *
 * Since: 1.28
 */
void
gst_clear_hip_event (GstHipEvent ** event)
{
  gst_clear_mini_object (event);
}

/**
 * gst_hip_event_set_notify:
 * @event: a #GstHipEvent
 * @user_data: user data
 * @notify: a #GDestroyNotify
 *
 * Schedules oneshot @notify callback.
 *
 * @event must have been successfully recorded using gst_hip_event_record() or
 * gst_hip_stream_record_event() before calling this function.
 *
 * This method can be used to keep resources associated with HIP operations
 * alive until @event is completed. @notify will be called with @user_data
 * from an internal worker thread once the event is completed.
 *
 * Since: 1.30
 */
void
gst_hip_event_set_notify (GstHipEvent * event, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (event);
  g_return_if_fail (event->recorded);

  HipCompletionManager::GetInstance ()->Push (event, user_data, notify);
}

void
gst_hip_flush_all_devices (void)
{
  HipCompletionManager::GetInstance ()->Flush ();
}
