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
#include <hip/hip_runtime.h>

G_BEGIN_DECLS

GType gst_hip_graphics_resource_get_type (void);

hipError_t gst_hip_graphics_resource_map (GstHipGraphicsResource * resource,
                                          hipStream_t stream);

hipError_t gst_hip_graphics_resource_unmap (GstHipGraphicsResource * resource,
                                            hipStream_t stream);

hipError_t gst_hip_graphics_resource_get_mapped_pointer (GstHipGraphicsResource * resource,
                                                         void ** dev_ptr,
                                                         size_t * size);

GstHipGraphicsResource * gst_hip_graphics_resource_ref (GstHipGraphicsResource * resource);

void gst_hip_graphics_resource_unref (GstHipGraphicsResource * resource);

void gst_clear_hip_graphics_resource (GstHipGraphicsResource ** resource);

G_END_DECLS

