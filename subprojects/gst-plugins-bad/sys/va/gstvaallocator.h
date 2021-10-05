/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include <gst/allocators/allocators.h>
#include <gst/va/gstvadisplay.h>
#include <gst/video/video.h>
#include <stdint.h>
#include <va/va.h>

G_BEGIN_DECLS

#define GST_TYPE_VA_DMABUF_ALLOCATOR (gst_va_dmabuf_allocator_get_type())
G_DECLARE_FINAL_TYPE (GstVaDmabufAllocator, gst_va_dmabuf_allocator, GST,
    VA_DMABUF_ALLOCATOR, GstDmaBufAllocator);

GstAllocator *        gst_va_dmabuf_allocator_new         (GstVaDisplay * display);
gboolean              gst_va_dmabuf_allocator_setup_buffer (GstAllocator * allocator,
                                                            GstBuffer * buffer);
gboolean              gst_va_dmabuf_allocator_prepare_buffer (GstAllocator * allocator,
                                                              GstBuffer * buffer);
void                  gst_va_dmabuf_allocator_flush       (GstAllocator * allocator);
gboolean              gst_va_dmabuf_allocator_set_format  (GstAllocator * allocator,
                                                           GstVideoInfo * info,
                                                           guint usage_hint);
gboolean              gst_va_dmabuf_allocator_get_format  (GstAllocator * allocator,
                                                           GstVideoInfo * info,
                                                           guint * usage_hint);

gboolean              gst_va_dmabuf_memories_setup        (GstVaDisplay * display,
                                                           GstVideoInfo * info,
                                                           guint n_planes,
                                                           GstMemory * mem[GST_VIDEO_MAX_PLANES],
                                                           uintptr_t * fds,
                                                           gsize offset[GST_VIDEO_MAX_PLANES],
                                                           guint usage_hint);

#define GST_TYPE_VA_ALLOCATOR (gst_va_allocator_get_type())
G_DECLARE_FINAL_TYPE (GstVaAllocator, gst_va_allocator, GST, VA_ALLOCATOR, GstAllocator);

#define GST_ALLOCATOR_VASURFACE   "VAMemory"

#define GST_MAP_VA (GST_MAP_FLAG_LAST << 1)

GstAllocator *        gst_va_allocator_new                (GstVaDisplay * display,
                                                           GArray * surface_formats);
GstMemory *           gst_va_allocator_alloc              (GstAllocator * allocator);
gboolean              gst_va_allocator_setup_buffer       (GstAllocator * allocator,
                                                           GstBuffer * buffer);
gboolean              gst_va_allocator_prepare_buffer     (GstAllocator * allocator,
                                                           GstBuffer * buffer);
void                  gst_va_allocator_flush              (GstAllocator * allocator);
gboolean              gst_va_allocator_set_format         (GstAllocator * allocator,
                                                           GstVideoInfo * info,
                                                           guint usage_hint);
gboolean              gst_va_allocator_get_format         (GstAllocator * allocator,
                                                           GstVideoInfo * info,
                                                           guint * usage_hint);

VASurfaceID           gst_va_memory_get_surface           (GstMemory * mem);
VASurfaceID           gst_va_buffer_get_surface           (GstBuffer * buffer);

gboolean              gst_va_buffer_create_aux_surface    (GstBuffer * buffer);
VASurfaceID           gst_va_buffer_get_aux_surface       (GstBuffer * buffer);

G_END_DECLS
