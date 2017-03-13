/*
 * GStreamer
 * Copyright (C) 2015 Alessandro Decina <twi@centricular.com>
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

#ifndef _GST_IO_SURFACE_MEMORY_H_
#define _GST_IO_SURFACE_MEMORY_H_

#include <IOSurface/IOSurface.h>
#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR (gst_io_surface_memory_allocator_get_type())
GType gst_io_surface_memory_allocator_get_type(void);

#define GST_IS_IO_SURFACE_MEMORY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR))
#define GST_IS_IO_SURFACE_MEMORY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR))
#define GST_IO_SURFACE_MEMORY_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR, GstIOSurfaceMemoryAllocatorClass))
#define GST_IO_SURFACE_MEMORY_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR, GstIOSurfaceMemoryAllocator))
#define GST_IO_SURFACE_MEMORY_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_IO_SURFACE_MEMORY_ALLOCATOR, GstIOSurfaceMemoryAllocatorClass))
#define GST_IO_SURFACE_MEMORY_ALLOCATOR_CAST(obj)            ((GstIOSurfaceMemoryAllocator *)(obj))

typedef struct _GstIOSurfaceMemory
{
  GstGLMemory gl_mem;
  IOSurfaceRef surface;
} GstIOSurfaceMemory;

#define GST_IO_SURFACE_MEMORY_ALLOCATOR_NAME   "IOSurfaceMemory"

void gst_ios_surface_memory_init (void);

GstIOSurfaceMemory *
gst_io_surface_memory_wrapped (GstGLContext * context,
    IOSurfaceRef surface,
    GstGLTextureTarget target,
    GstGLFormat tex_format,
    GstVideoInfo * info,
    guint plane,
    GstVideoAlignment *valign,
    gpointer user_data,
    GDestroyNotify notify);

void gst_io_surface_memory_set_surface (GstIOSurfaceMemory *memory, IOSurfaceRef surface);

gboolean gst_is_io_surface_memory (GstMemory * mem);

typedef struct _GstIOSurfaceMemoryAllocator
{
  GstGLMemoryAllocator allocator;
} GstIOSurfaceMemoryAllocator;

typedef struct _GstIOSurfaceMemoryAllocatorClass
{
  GstGLMemoryAllocatorClass parent_class;
} GstIOSurfaceMemoryAllocatorClass;

G_END_DECLS

#endif /* _GST_IO_SURFACE_MEMORY_H_ */
