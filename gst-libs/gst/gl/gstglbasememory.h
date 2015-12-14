/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef _GST_GL_BASE_MEMORY_H_
#define _GST_GL_BASE_MEMORY_H_

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/gstmemory.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_BASE_MEMORY_ALLOCATOR (gst_gl_base_memory_allocator_get_type())
GType gst_gl_base_memory_allocator_get_type(void);

#define GST_IS_GL_BASE_MEMORY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_ALLOCATOR))
#define GST_IS_GL_BASE_MEMORY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GL_BASE_MEMORY_ALLOCATOR))
#define GST_GL_BASE_MEMORY_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GL_BASE_MEMORY_ALLOCATOR, GstGLBaseMemoryAllocatorClass))
#define GST_GL_BASE_MEMORY_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_BASE_MEMORY_ALLOCATOR, GstGLBaseMemoryAllocator))
#define GST_GL_BASE_MEMORY_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GL_BASE_MEMORY_ALLOCATOR, GstGLBaseMemoryAllocatorClass))
#define GST_GL_BASE_MEMORY_ALLOCATOR_CAST(obj)            ((GstGLBaseMemoryAllocator *)(obj))

#define GST_GL_BASE_MEMORY_CAST(mem) ((GstGLBaseMemory *)mem)

GQuark gst_gl_base_memory_error_quark (void);
#define GST_GL_BASE_MEMORY_ERROR (gst_gl_base_memory_error_quark ())

typedef enum
{
  GST_GL_BASE_MEMORY_ERROR_FAILED,
  GST_GL_BASE_MEMORY_ERROR_OLD_LIBS,
  GST_GL_BASE_MEMORY_ERROR_RESOURCE_UNAVAILABLE,
} GstGLBaseMemoryError;

typedef enum
{
  GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD   = (GST_MEMORY_FLAG_LAST << 0),
  GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD     = (GST_MEMORY_FLAG_LAST << 1)
} GstGLBaseMemoryTransfer;

/**
 * GST_MAP_GL:
 *
 * Flag indicating that we should map the GL object instead of to system memory.
 *
 * Combining #GST_MAP_GL with #GST_MAP_WRITE has the same semantics as though
 * you are writing to OpenGL. Conversely, combining #GST_MAP_GL with
 * #GST_MAP_READ has the same semantics as though you are reading from OpenGL.
 */
#define GST_MAP_GL (GST_MAP_FLAG_LAST << 1)

/**
 * GstGLBaseMemory:
 * @mem: the parent object
 * @context: the #GstGLContext to use for GL operations
 *
 * Represents information about a GL memory object
 */
struct _GstGLBaseMemory
{
  GstMemory             mem;

  GstGLContext         *context;

  /* <protected> */
  GMutex                lock;

  GstMapFlags           map_flags;       /* cumulative map flags */
  gint                  map_count;
  gint                  gl_map_count;

  gpointer              data;

  /* <private> */
  gpointer              alloc_data;

  GDestroyNotify        notify;
  gpointer              user_data;
};

typedef gboolean          (*GstGLBaseMemoryAllocatorCreateFunction)     (GstGLBaseMemory * mem, GError ** error);
typedef gpointer          (*GstGLBaseMemoryAllocatorMapFunction)        (GstGLBaseMemory * mem, GstMapInfo * info, gsize maxsize);
typedef void              (*GstGLBaseMemoryAllocatorUnmapFunction)      (GstGLBaseMemory * mem, GstMapInfo * info);
typedef GstGLBaseMemory * (*GstGLBaseMemoryAllocatorCopyFunction)       (GstGLBaseMemory * mem, gssize offset, gssize size);
typedef void              (*GstGLBaseMemoryAllocatorDestroyFunction)    (GstGLBaseMemory * mem);

/**
 * GstGLBaseMemoryAllocator
 *
 * Opaque #GstGLAllocator struct
 */
struct _GstGLBaseMemoryAllocator
{
  GstAllocator parent;
  GstMemoryCopyFunction fallback_mem_copy;
};

/**
 * GstGLBaseMemoryAllocatorClass:
 *
 * The #GstGLBaseMemoryAllocatorClass only contains private data
 */
struct _GstGLBaseMemoryAllocatorClass
{
  GstAllocatorClass parent_class;

  GstGLBaseMemoryAllocatorCreateFunction        create;
  GstGLBaseMemoryAllocatorMapFunction           map;
#if 0
  GstGLBaseMemoryAllocatorFlushFunction         flush;        /* make CPU writes visible to the GPU */
  GstGLBaseMemoryAllocatorInvalidateFunction    invalidate;   /* make GPU writes visible to the CPU */
#endif
  GstGLBaseMemoryAllocatorUnmapFunction         unmap;
  GstGLBaseMemoryAllocatorCopyFunction          copy;
  GstGLBaseMemoryAllocatorDestroyFunction       destroy;
};

#include <gst/gl/gl.h>

/**
 * GST_GL_BASE_MEMORY_ALLOCATOR_NAME:
 *
 * The name of the GL buffer allocator
 */
#define GST_GL_BASE_MEMORY_ALLOCATOR_NAME   "GLBaseMemory"

void          gst_gl_base_memory_init_once (void);
gboolean      gst_is_gl_base_memory        (GstMemory * mem);

void          gst_gl_base_memory_init      (GstGLBaseMemory * mem,
                                            GstAllocator * allocator,
                                            GstMemory * parent,
                                            GstGLContext * context,
                                            GstAllocationParams * params,
                                            gsize maxsize,
                                            GDestroyNotify notify,
                                            gpointer user_data);

gboolean      gst_gl_base_memory_alloc_data (GstGLBaseMemory * gl_mem);
gboolean      gst_gl_base_memory_memcpy     (GstGLBaseMemory * src,
                                             GstGLBaseMemory * dest,
                                             gssize offset,
                                             gssize size);

G_END_DECLS

#endif /* _GST_GL_BUFFER_H_ */
