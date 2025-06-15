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
  }

  hipStream_t handle = nullptr;
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

  gst_mini_object_init (stream, 0, gst_hip_stream_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_hip_stream_free);

  return stream;
}

GstHipVendor
gst_hip_stream_get_vendor (GstHipStream * stream)
{
  g_return_val_if_fail (stream, GST_HIP_VENDOR_UNKNOWN);

  return stream->vendor;
}

guint
gst_hip_stream_get_device_id (GstHipStream * stream)
{
  g_return_val_if_fail (stream, G_MAXUINT);

  return stream->vendor;
}

hipStream_t
gst_hip_stream_get_handle (GstHipStream * stream)
{
  if (!stream)
    return nullptr;

  return stream->handle;
}

GstHipStream *
gst_hip_stream_ref (GstHipStream * stream)
{
  return (GstHipStream *) gst_mini_object_ref (stream);
}

void
gst_hip_stream_unref (GstHipStream * stream)
{
  return gst_mini_object_unref (stream);
}

void
gst_clear_hip_stream (GstHipStream ** stream)
{
  gst_clear_mini_object (stream);
}
