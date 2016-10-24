/* GStreamer dmabuf allocator
 * Copyright (C) 2013 Linaro SA
 * Author: Benjamin Gaignard <benjamin.gaignard@linaro.org> for Linaro.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DMABUF_H__
#define __GST_DMABUF_H__

#include <gst/gst.h>
#include <gst/allocators/gstfdmemory.h>

G_BEGIN_DECLS

#define GST_ALLOCATOR_DMABUF "dmabuf"

#define GST_TYPE_DMABUF_ALLOCATOR              (gst_dmabuf_allocator_get_type())
#define GST_IS_DMABUF_ALLOCATOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DMABUF_ALLOCATOR))
#define GST_IS_DMABUF_ALLOCATOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DMABUF_ALLOCATOR))
#define GST_DMABUF_ALLOCATOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DMABUF_ALLOCATOR, GstDmaBufAllocatorClass))
#define GST_DMABUF_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DMABUF_ALLOCATOR, GstDmaBufAllocator))
#define GST_DMABUF_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DMABUF_ALLOCATOR, GstDmaBufAllocatorClass))
#define GST_DMABUF_ALLOCATOR_CAST(obj)         ((GstDmaBufAllocator *)(obj))

typedef struct _GstDmaBufAllocator GstDmaBufAllocator;
typedef struct _GstDmaBufAllocatorClass GstDmaBufAllocatorClass;

/**
 * GstDmaBufAllocator:
 *
 * Base class for allocators with dmabuf-backed memory
 *
 * Since: 1.12
 */
struct _GstDmaBufAllocator
{
  GstFdAllocator parent;
};

struct _GstDmaBufAllocatorClass
{
  GstFdAllocatorClass parent_class;
};


GType gst_dmabuf_allocator_get_type (void);

GstAllocator * gst_dmabuf_allocator_new (void);

GstMemory    * gst_dmabuf_allocator_alloc (GstAllocator * allocator, gint fd, gsize size);

gint           gst_dmabuf_memory_get_fd (GstMemory * mem);

gboolean       gst_is_dmabuf_memory (GstMemory * mem);


#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDmaBufAllocator, gst_object_unref)
#endif

G_END_DECLS
#endif /* __GST_DMABUF_H__ */
