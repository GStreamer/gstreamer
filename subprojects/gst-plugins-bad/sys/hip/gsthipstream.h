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
#include "gsthip-enums.h"
#include <hip/hip_runtime.h>

G_BEGIN_DECLS

GType gst_hip_stream_get_type (void);

GstHipStream * gst_hip_stream_new (GstHipVendor vendor,
                                   guint device_id);

GstHipVendor   gst_hip_stream_get_vendor (GstHipStream * stream);

guint          gst_hip_stream_get_device_id (GstHipStream * stream);

hipStream_t    gst_hip_stream_get_handle (GstHipStream * stream);

GstHipStream * gst_hip_stream_ref (GstHipStream * stream);

void           gst_hip_stream_unref (GstHipStream * stream);

void           gst_clear_hip_stream (GstHipStream ** stream);

G_END_DECLS

