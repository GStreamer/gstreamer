/* GStreamer shared memory allocator
 *
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2023 Netflix Inc.
 *  Author: Xavier Claessens <xavier.claessens@collabora.com>
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

#include <gst/allocators/gstfdmemory.h>

G_BEGIN_DECLS

#define GST_ALLOCATOR_SHM "shm"

#define GST_TYPE_SHM_ALLOCATOR gst_shm_allocator_get_type ()
GST_ALLOCATORS_API
G_DECLARE_FINAL_TYPE (GstShmAllocator, gst_shm_allocator, GST, SHM_ALLOCATOR, GstFdAllocator)

GST_ALLOCATORS_API void gst_shm_allocator_init_once (void);
GST_ALLOCATORS_API GstAllocator* gst_shm_allocator_get (void);

G_END_DECLS
