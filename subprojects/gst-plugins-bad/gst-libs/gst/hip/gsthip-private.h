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

#pragma once

#include <gst/hip/gsthip.h>
#include <gst/hip/gsthipformat-private.h>

G_BEGIN_DECLS

GST_HIP_API
void  gst_hip_memory_init_once (void);

GST_HIP_API
void  gst_hip_flush_all_devices (void);

GST_HIP_API
hipEvent_t gst_hip_event_get_handle (GstHipEvent * event);

GST_HIP_API
hipStream_t gst_hip_event_get_recorded_stream (GstHipEvent * event);

GST_HIP_API
gpointer gst_hip_memory_get_pointer (GstHipMemory * mem);

GST_HIP_API
gchar * gst_hip_rtc_compile_full (GstHipDevice * device,
                                  const gchar * source,
                                  const gchar ** options,
                                  guint num_options,
                                  GError ** error);

GST_HIP_API
void gst_hip_kernel_args_add_scalar (GstHipKernelArgs * args,
                                     gconstpointer value,
                                     gsize size);

G_END_DECLS

#ifdef __cplusplus
#include <mutex>
#include <type_traits>

#define GST_HIP_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_HIP_CALL_ONCE_END )

template <typename T>
static inline void
gst_hip_kernel_args_add (GstHipKernelArgs * args, const T & value)
{
  if (std::is_scalar<T>::value)
    gst_hip_kernel_args_add_scalar (args, &value, sizeof (value));
  else
    gst_hip_kernel_args_add_value (args, &value, sizeof (value), alignof (T));
}
#endif
