/* GStreamer Wayland Library
 *
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2014 Collabora Ltd.
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

#include <gst/wayland/wayland.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_WL_SHM_ALLOCATOR (gst_wl_shm_allocator_get_type ())

GST_WL_API
G_DECLARE_FINAL_TYPE (GstWlShmAllocator, gst_wl_shm_allocator, GST, WL_SHM_ALLOCATOR, GstFdAllocator);

#define GST_ALLOCATOR_WL_SHM "wl_shm"

struct _GstWlShmAllocator
{
  GstFdAllocator parent_instance;
};

GST_WL_API
void gst_wl_shm_allocator_init_once (void);

GST_WL_API
GstAllocator * gst_wl_shm_allocator_get (void);

GST_WL_API
gboolean gst_is_wl_shm_memory (GstMemory * mem);

GST_WL_API
struct wl_buffer * gst_wl_shm_memory_construct_wl_buffer (GstMemory * mem,
    GstWlDisplay * display, const GstVideoInfo * info);

G_END_DECLS
