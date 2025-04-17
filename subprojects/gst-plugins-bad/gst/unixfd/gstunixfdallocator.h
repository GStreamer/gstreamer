/* GStreamer unix file-descriptor source/sink
 *
 * Copyright (C) 2025 Netflix Inc.
 *  Author: Xavier Claessens <xclaessens@netflix.com>
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

#include <gst/allocators/allocators.h>

#define GST_TYPE_UNIX_FD_ALLOCATOR gst_unix_fd_allocator_get_type()
G_DECLARE_FINAL_TYPE (GstUnixFdAllocator, gst_unix_fd_allocator,
    GST, UNIX_FD_ALLOCATOR, GstShmAllocator);

GstUnixFdAllocator *gst_unix_fd_allocator_new (void);
void gst_unix_fd_allocator_flush (GstUnixFdAllocator * self);
