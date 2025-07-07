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

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hipstream", 0, "hipstream");
      });

  return cat;
}
#endif

/* *INDENT-OFF* */
struct _GstHipStream : public GstMiniObject
{
  ~_GstHipStream ()
  {
    if (handle) {
      auto hip_ret = HipSetDevice (vendor, device_id);
      if (gst_hip_result (hip_ret, vendor))
        HipStreamDestroy (vendor, handle);
    }

    gst_clear_object (&event_pool);
  }

  hipStream_t handle = nullptr;
  GstHipEventPool *event_pool = nullptr;
  GstHipVendor vendor;
  guint device_id;
};
/* *INDENT-ON* */

static void
gst_hip_stream_free (GstHipStream * stream)
{
  delete stream;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstHipStream, gst_hip_stream);

/**
 * gst_hip_stream_new:
 * @vendor: a #GstHipVendor
 * @device_id: device identifier
 *
 * Creates a new #GstHipStream object
 *
 * Returns: (transfer full) (nullable): a #GstHipStream object or %NULL if failed
 *
 * Since: 1.28
 */
GstHipStream *
gst_hip_stream_new (GstHipVendor vendor, guint device_id)
{
  g_return_val_if_fail (vendor != GST_HIP_VENDOR_UNKNOWN, nullptr);

  auto hip_ret = HipSetDevice (vendor, device_id);
  if (!gst_hip_result (hip_ret, vendor)) {
    GST_ERROR ("Couldn't set device");
    return nullptr;
  }

  hipStream_t handle;
  hip_ret = HipStreamCreate (vendor, &handle);
  if (!gst_hip_result (hip_ret, vendor)) {
    GST_ERROR ("Couldn't create stream");
    return nullptr;
  }

  auto stream = new GstHipStream ();
  stream->handle = handle;
  stream->vendor = vendor;
  stream->device_id = device_id;
  stream->event_pool = gst_hip_event_pool_new (vendor, device_id);

  gst_mini_object_init (stream, 0, gst_hip_stream_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_hip_stream_free);

  return stream;
}

/**
 * gst_hip_stream_get_vendor:
 * @stream: a #GstHipStream
 *
 * Gets device vendor of @stream object
 *
 * Returns: #GstHipVendor
 *
 * Since: 1.28
 */
GstHipVendor
gst_hip_stream_get_vendor (GstHipStream * stream)
{
  g_return_val_if_fail (stream, GST_HIP_VENDOR_UNKNOWN);

  return stream->vendor;
}

/**
 * gst_hip_stream_get_device_id:
 * @stream: a #GstHipStream
 *
 * Gets numeric device identifier of @stream object
 *
 * Returns: device identifier
 *
 * Since: 1.28
 */
guint
gst_hip_stream_get_device_id (GstHipStream * stream)
{
  g_return_val_if_fail (stream, G_MAXUINT);

  return stream->vendor;
}

/**
 * gst_hip_stream_get_handle:
 * @stream: (allow-none): a #GstHipStream
 *
 * Gets hipStream_t handle owned by @stream
 *
 * Returns: hipStream_t handle
 *
 * Since: 1.28
 */
hipStream_t
gst_hip_stream_get_handle (GstHipStream * stream)
{
  if (!stream)
    return nullptr;

  return stream->handle;
}

/**
 * gst_hip_stream_record_event:
 * @stream: a #GstHipStream
 * @event: (out) (transfer full) (nullable): a location to store #GstHipEvent
 *
 * Records currently scheduled operations in @stream to #GstHipEvent
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.28
 */
gboolean
gst_hip_stream_record_event (GstHipStream * stream, GstHipEvent ** event)
{
  g_return_val_if_fail (stream, FALSE);
  g_return_val_if_fail (event, FALSE);

  auto hip_ret = HipSetDevice (stream->vendor, stream->device_id);
  if (!gst_hip_result (hip_ret, stream->vendor)) {
    GST_ERROR ("Couldn't set device");
    return FALSE;
  }

  GstHipEvent *new_event;
  if (!gst_hip_event_pool_acquire (stream->event_pool, &new_event)) {
    GST_ERROR ("Couldn't acquire event");
    return FALSE;
  }

  hip_ret = gst_hip_event_record (new_event, stream->handle);
  if (!gst_hip_result (hip_ret, stream->vendor)) {
    GST_ERROR ("Couldn't record event");
    gst_hip_event_unref (new_event);
    return FALSE;
  }

  *event = new_event;

  return TRUE;
}

/**
 * gst_hip_stream_ref:
 * @stream: a #GstHipStream
 *
 * Increments the reference count on @stream
 *
 * Returns: (transfer full): a pointer to @stream
 *
 * Since: 1.28
 */
GstHipStream *
gst_hip_stream_ref (GstHipStream * stream)
{
  return (GstHipStream *) gst_mini_object_ref (stream);
}

/**
 * gst_hip_stream_unref:
 * @stream: a #GstHipStream
 *
 * Decrements the reference count on @stream
 *
 * Since: 1.28
 */
void
gst_hip_stream_unref (GstHipStream * stream)
{
  return gst_mini_object_unref (stream);
}

/**
 * gst_clear_hip_stream: (skip)
 * @stream: a pointer to a #GstHipStream
 *
 * Clears a reference to the @stream
 *
 * Since: 1.28
 */
void
gst_clear_hip_stream (GstHipStream ** stream)
{
  gst_clear_mini_object (stream);
}
