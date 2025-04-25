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

#include <gst/gst.h>
#include "gsthip_fwd.h"

G_BEGIN_DECLS

gboolean _gst_hip_result (hipError_t result,
                         GstDebugCategory * cat,
                         const gchar * file,
                         const gchar * function,
                         gint line);

#ifndef GST_DISABLE_GST_DEBUG
#define gst_hip_result(result) \
_gst_hip_result(result, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else
#define gst_hip_result(result) \
_gst_hip_result(result, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif /* GST_DISABLE_GST_DEBUG */

gboolean     gst_hip_ensure_element_data    (GstElement * element,
                                             gint device_id,
                                             GstHipDevice ** device);

gboolean     gst_hip_handle_set_context     (GstElement * element,
                                             GstContext * context,
                                             gint device_id,
                                             GstHipDevice ** device);

gboolean     gst_hip_handle_context_query   (GstElement * element,
                                             GstQuery * query,
                                             GstHipDevice * device);

GstContext * gst_context_new_hip_device     (GstHipDevice * device);

G_END_DECLS

