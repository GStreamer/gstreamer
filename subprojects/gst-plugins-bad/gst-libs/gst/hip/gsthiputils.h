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

#include <gmodule.h>
#include <gst/gst.h>
#include <gst/hip/hip-prelude.h>
#include <gst/hip/gsthip_fwd.h>
#include <gst/hip/gsthip-enums.h>

G_BEGIN_DECLS

GST_HIP_API
gboolean _gst_hip_result (hipError_t result,
                          GstHipVendor vendor,
                          GstDebugCategory * cat,
                          const gchar * file,
                          const gchar * function,
                          gint line);

/**
 * gst_hip_result:
 * @result: HIP device API return code `hipError_t`
 * @vendor: a #GstHipVendor
 *
 * Returns: %TRUE if HIP device API call result is hipSuccess
 *
 * Since: 1.28
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_hip_result(result,vendor) \
_gst_hip_result(result, vendor, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else
#define gst_hip_result(result,vendor) \
_gst_hip_result(result, vendor, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif /* GST_DISABLE_GST_DEBUG */

GST_HIP_API
gboolean     gst_hip_ensure_element_data    (GstElement * element,
                                             GstHipVendor vendor,
                                             gint device_id,
                                             GstHipDevice ** device);

GST_HIP_API
gboolean     gst_hip_handle_set_context     (GstElement * element,
                                             GstContext * context,
                                             GstHipVendor vendor,
                                             gint device_id,
                                             GstHipDevice ** device);

GST_HIP_API
gboolean     gst_hip_handle_context_query   (GstElement * element,
                                             GstQuery * query,
                                             GstHipDevice * device);

GST_HIP_API
GstContext * gst_context_new_hip_device     (GstHipDevice * device);

G_END_DECLS

