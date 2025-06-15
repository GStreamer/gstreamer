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

GstHipVendor
gst_hip_event_get_vendor (GstHipEvent * event)
{
  g_return_val_if_fail (event, GST_HIP_VENDOR_UNKNOWN);

  return event->vendor;
}

guint
gst_hip_event_get_device_id (GstHipEvent * event)
{
  g_return_val_if_fail (event, G_MAXUINT);

  return event->vendor;
}

hipError_t
gst_hip_event_record (GstHipEvent * event, hipStream_t stream)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  return HipEventRecord (event->vendor, event->handle, stream);
}

hipError_t
gst_hip_event_query (GstHipEvent * event)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  return HipEventQuery (event->vendor, event->handle);
}

hipError_t
gst_hip_event_synchronize (GstHipEvent * event)
{
  g_return_val_if_fail (event, hipErrorInvalidValue);

  auto hip_ret = HipSetDevice (event->vendor, event->device_id);
  if (!gst_hip_result (hip_ret, event->vendor))
    return hip_ret;

  return HipEventSynchronize (event->vendor, event->handle);
}

GstHipEvent *
gst_hip_event_ref (GstHipEvent * event)
{
  return (GstHipEvent *) gst_mini_object_ref (event);
}

void
gst_hip_event_unref (GstHipEvent * event)
{
  return gst_mini_object_unref (event);
}

void
gst_clear_hip_event (GstHipEvent ** event)
{
  gst_clear_mini_object (event);
}
