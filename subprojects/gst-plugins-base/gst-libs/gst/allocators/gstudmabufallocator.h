/* GStreamer udmabuf allocator
 *
 * Copyright (C) 2025 Collabora Ltd.
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

#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

/**
 * GST_ALLOCATOR_UDMABUF:
 *
 * Name of this allocator, to be used for example with gst_allocator_find() and
 * gst_memory_is_type().
 *
 * Since: 1.28
 */
#define GST_ALLOCATOR_UDMABUF "udmabuf"

/**
 * GstUdmabufAllocator:
 *
 * Private instance object for #GstUdmabufAllocator.
 *
 * Since: 1.28
 */

/**
 * GstUdmabufAllocatorClass.parent_class:
 *
 * Parent Class.
 *
 * Since: 1.28
 */

/**
 * GST_TYPE_UDMABUF_ALLOCATOR:
 *
 * Macro that returns the #GstUdmabufAllocator type.
 *
 * Since: 1.28
 */
#define GST_TYPE_UDMABUF_ALLOCATOR gst_udmabuf_allocator_get_type ()
GST_ALLOCATORS_API
G_DECLARE_FINAL_TYPE (GstUdmabufAllocator, gst_udmabuf_allocator, GST, UDMABUF_ALLOCATOR, GstDmaBufAllocator)

GST_ALLOCATORS_API
void gst_udmabuf_allocator_init_once (void);

GST_ALLOCATORS_API
GstAllocator* gst_udmabuf_allocator_get (void);

G_END_DECLS
