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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstglbasebuffer.h"
#include "gstglutils.h"

/**
 * SECTION:gstglbuffer
 * @short_description: memory subclass for GL buffers
 * @see_also: #GstMemory, #GstAllocator
 *
 * GstGLBaseBuffer is a #GstMemory subclass providing support for the mapping of
 * GL buffers.  
 *
 * Data is uploaded or downloaded from the GPU as is necessary.
 */

/* Implementation notes:
 *
 * Currently does not take into account GLES2 differences (no mapbuffer)
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

/* compatibility definitions... */
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_BASE_BUFFER);
#define GST_CAT_DEFUALT GST_CAT_GL_BASE_BUFFER

static GstAllocator *_gl_base_buffer_allocator;

GQuark
gst_gl_base_buffer_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-base-buffer-error-quark");
}

static gboolean
_default_create (GstGLBaseBuffer * mem, GError ** error)
{
  g_set_error (error, GST_GL_BASE_BUFFER_ERROR, GST_GL_BASE_BUFFER_ERROR_FAILED,
      "subclass should define create() vfunc");

  g_critical ("subclass should override "
      "GstGLBaseBufferAllocatorClass::create() function");

  return FALSE;
}

struct create_data
{
  GstGLBaseBuffer *mem;
  gboolean result;
};

static void
_mem_create_gl (GstGLContext * context, struct create_data *transfer)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  GError *error = NULL;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_GET_CLASS (transfer->mem->mem.allocator);

  g_return_if_fail (alloc_class->create != NULL);

  if ((transfer->result = alloc_class->create (transfer->mem, &error)))
    return;

  g_assert (error != NULL);

  GST_CAT_ERROR (GST_CAT_GL_BASE_BUFFER, "Failed to create GL buffer: %s",
      error->message);
}

void
gst_gl_base_buffer_init (GstGLBaseBuffer * mem, GstAllocator * allocator,
    GstMemory * parent, GstGLContext * context, GstAllocationParams * params,
    gsize size)
{
  gsize align = gst_memory_alignment, offset = 0, maxsize = size;
  GstMemoryFlags flags = 0;
  struct create_data data;

  if (params) {
    flags = params->flags;
    align |= params->align;
    offset = params->prefix;
    maxsize += params->prefix + params->padding + align;
  }

  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, parent, maxsize,
      align, offset, size);

  mem->context = gst_object_ref (context);

  g_mutex_init (&mem->lock);

  data.mem = mem;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _mem_create_gl, &data);
  if (!data.result) {
    GST_CAT_ERROR (GST_CAT_GL_BASE_BUFFER,
        "Could not create GL buffer with context:%p", context);
  }

  GST_CAT_DEBUG (GST_CAT_GL_BASE_BUFFER, "new GL buffer memory:%p size:%"
      G_GSIZE_FORMAT, mem, maxsize);
}

static gpointer
_align_data (gpointer data, gsize align, gsize * maxsize)
{
  guint8 *ret = data;
  gsize aoffset;

  /* do alignment */
  if ((aoffset = ((guintptr) ret & align))) {
    aoffset = (align + 1) - aoffset;
    ret += aoffset;
    *maxsize -= aoffset;
  }

  return ret;
}

/* subclass usage only */
GstGLBaseBuffer *
gst_gl_base_buffer_alloc_data (GstGLBaseBuffer * gl_mem)
{
  GstMemory *mem = (GstMemory *) gl_mem;

  if (gl_mem->data)
    return gl_mem;

  gl_mem->alloc_data = g_try_malloc (mem->maxsize);

  if (gl_mem->alloc_data == NULL) {
    gst_memory_unref (mem);
    return NULL;
  }

  gl_mem->data = _align_data (gl_mem->alloc_data, mem->align, &mem->maxsize);

  return gl_mem;
}

/* XXX: add as API? */
static gpointer
gst_gl_base_buffer_cpu_access (GstGLBaseBuffer * mem,
    GstMapInfo * info, gsize size)
{
  const GstGLFuncs *gl = mem->context->gl_vtable;
  gpointer data, ret;

  gst_gl_base_buffer_alloc_data (mem);
  ret = mem->data;

  GST_CAT_LOG (GST_CAT_GL_BASE_BUFFER, "mapping id %d size %" G_GSIZE_FORMAT,
      mem->id, size);

  /* The extra data pointer indirection/memcpy is needed for coherent across
   * concurrent map()'s in both GL and CPU */
  if (GST_MEMORY_FLAG_IS_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD)
      && (info->flags & GST_MAP_GL) == 0 && (info->flags & GST_MAP_READ) != 0) {
    gl->BindBuffer (mem->target, mem->id);

    if (gl->MapBufferRange) {
      /* FIXME: optionally remove this with a flag and return the
       * glMapBufferRange pointer (requires
       * GL_ARB_buffer_storage/GL4/GL_COHERENT_BIT) */
      guint gl_map_flags = GL_MAP_READ_BIT;

      data = gl->MapBufferRange (mem->target, 0, size, gl_map_flags);

      if (data)
        memcpy (mem->data, data, size);

      gl->UnmapBuffer (mem->target);
      ret = mem->data;
    } else if (gl->GetBufferSubData) {
      gl->GetBufferSubData (mem->target, 0, size, mem->data);
      ret = mem->data;
    } else {
      ret = NULL;
    }
    gl->BindBuffer (mem->target, 0);
  }

  return ret;
}

/* XXX: add as API? */
static void
gst_gl_base_buffer_upload_cpu_write (GstGLBaseBuffer * mem, GstMapInfo * info,
    gsize size)
{
  const GstGLFuncs *gl = mem->context->gl_vtable;
  gpointer data;

  if (!mem->data)
    /* no data pointer has been written */
    return;

  /* The extra data pointer indirection/memcpy is needed for coherent across
   * concurrent map()'s in both GL and CPU */
  /* FIXME: uploading potentially half-written data for libav pushing READWRITE
   * mapped buffers */
  if (GST_MEMORY_FLAG_IS_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD)
      || (mem->map_flags & GST_MAP_WRITE) != 0) {
    gl->BindBuffer (mem->target, mem->id);

    if (gl->MapBufferRange) {
      /* FIXME: optionally remove this with a flag and return the
       * glMapBufferRange pointer (requires
       * GL_ARB_buffer_storage/GL4/GL_COHERENT_BIT) */
      guint gl_map_flags = GL_MAP_WRITE_BIT;

      data = gl->MapBufferRange (mem->target, 0, size, gl_map_flags);

      if (data)
        memcpy (data, mem->data, size);

      gl->UnmapBuffer (mem->target);
    } else if (gl->BufferSubData) {
      gl->BufferSubData (mem->target, 0, size, mem->data);
    }
    gl->BindBuffer (mem->target, 0);
  }
}

static gpointer
_default_map_buffer (GstGLBaseBuffer * mem, GstMapInfo * info, gsize size)
{
  if ((info->flags & GST_MAP_GL) != 0) {
    if (info->flags & GST_MAP_READ) {
      gst_gl_base_buffer_upload_cpu_write (mem, info, size);
    }
    return &mem->id;
  } else {
    return gst_gl_base_buffer_cpu_access (mem, info, size);
  }

  return NULL;
}

struct map_data
{
  GstGLBaseBuffer *mem;
  GstMapInfo *info;
  gsize size;
  gpointer data;
};

static void
_map_data_gl (GstGLContext * context, struct map_data *transfer)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  GstGLBaseBuffer *mem = transfer->mem;
  GstMapInfo *info = transfer->info;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_GET_CLASS (transfer->mem->mem.allocator);

  g_return_if_fail (alloc_class->map_buffer != NULL);

  g_mutex_lock (&mem->lock);

  GST_CAT_LOG (GST_CAT_GL_BASE_BUFFER, "mapping mem %p id %d flags %04x", mem,
      mem->id, info->flags);

  /* FIXME: validate map flags based on the memory domain */
  if (mem->map_count++ == 0)
    mem->map_flags = info->flags;
  else {
    /* assert that the flags are a subset of the first map flags */
    g_assert ((((GST_MAP_GL - 1) & info->flags) & mem->map_flags) != 0);
    GST_CAT_LOG (GST_CAT_GL_BASE_BUFFER, "multiple map no %d flags %04x "
        "all flags %04x", mem->map_count, info->flags, mem->map_flags);
  }

  if ((info->flags & GST_MAP_GL) != (mem->map_flags & GST_MAP_GL))
    mem->map_flags |= GST_MAP_GL;

  if (info->flags & GST_MAP_GL)
    mem->gl_map_count++;

  transfer->data = alloc_class->map_buffer (transfer->mem, transfer->info,
      transfer->size);

  if (transfer->data) {
    if (info->flags & GST_MAP_GL) {
      if (info->flags & GST_MAP_WRITE)
        GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD);
      GST_MEMORY_FLAG_UNSET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD);
    } else {
      if (info->flags & GST_MAP_WRITE)
        GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD);
      GST_MEMORY_FLAG_UNSET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD);
    }
  }

  g_mutex_unlock (&mem->lock);
}

static gpointer
_mem_map_full (GstGLBaseBuffer * mem, GstMapInfo * info, gsize size)
{
  struct map_data transfer;

  transfer.mem = mem;
  transfer.info = info;
  transfer.size = size;
  transfer.data = NULL;

  gst_gl_context_thread_add (mem->context,
      (GstGLContextThreadFunc) _map_data_gl, &transfer);

  return transfer.data;
}

static void
_default_unmap_buffer (GstGLBaseBuffer * mem, GstMapInfo * info)
{
  /* XXX: optimistically transfer data */
}

struct unmap_data
{
  GstGLBaseBuffer *mem;
  GstMapInfo *info;
};

static void
_unmap_data_gl (GstGLContext * context, struct unmap_data *transfer)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  GstGLBaseBuffer *mem = transfer->mem;
  GstMapInfo *info = transfer->info;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_GET_CLASS (transfer->mem->mem.allocator);

  g_return_if_fail (alloc_class->unmap_buffer != NULL);

  g_mutex_lock (&mem->lock);

  GST_CAT_LOG (GST_CAT_GL_BASE_BUFFER, "unmapping mem %p id %d flags %04x", mem,
      mem->id, info->flags);

  alloc_class->unmap_buffer (transfer->mem, transfer->info);

  if (info->flags & GST_MAP_GL && --mem->gl_map_count)
    /* unset the gl flag */
    mem->map_flags &= ~GST_MAP_GL;

  if (--mem->map_count <= 0) {
    mem->map_flags = 0;
  }

  if (info->flags & GST_MAP_GL) {
    if (info->flags & GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD);
  } else {
    if (info->flags & GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD);
  }

  g_mutex_unlock (&mem->lock);
}

static void
_mem_unmap_full (GstGLBaseBuffer * mem, GstMapInfo * info)
{
  struct unmap_data transfer;

  transfer.mem = mem;
  transfer.info = info;

  gst_gl_context_thread_add (mem->context,
      (GstGLContextThreadFunc) _unmap_data_gl, &transfer);
}

gboolean
gst_gl_base_buffer_copy_buffer_sub_data (GstGLBaseBuffer * src,
    GstGLBaseBuffer * dest, gssize offset, gssize size)
{
  const GstGLFuncs *gl = src->context->gl_vtable;
  GstMapInfo sinfo, dinfo;

  if (!gl->CopyBufferSubData)
    /* This is GL(ES) 3.0+ only */
    return FALSE;

  if (!gst_memory_map ((GstMemory *) src, &sinfo, GST_MAP_READ | GST_MAP_GL)) {
    GST_CAT_WARNING (GST_CAT_GL_BASE_BUFFER,
        "failed to read map source memory %p", src);
    return FALSE;
  }

  if (!gst_memory_map ((GstMemory *) dest, &dinfo, GST_MAP_WRITE | GST_MAP_GL)) {
    GST_CAT_WARNING (GST_CAT_GL_BASE_BUFFER,
        "failed to write map destination memory %p", dest);
    gst_memory_unmap ((GstMemory *) src, &sinfo);
    return FALSE;
  }

  gl->BindBuffer (GL_COPY_READ_BUFFER, src->id);
  gl->BindBuffer (GL_COPY_WRITE_BUFFER, dest->id);
  gl->CopyBufferSubData (GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
      offset, 0, size);

  gst_memory_unmap ((GstMemory *) src, &sinfo);
  gst_memory_unmap ((GstMemory *) dest, &dinfo);

  return TRUE;
}

gboolean
gst_gl_base_buffer_memcpy (GstGLBaseBuffer * src, GstGLBaseBuffer * dest,
    gssize offset, gssize size)
{
  GstMapInfo sinfo, dinfo;

  if (!gst_memory_map ((GstMemory *) src, &sinfo, GST_MAP_READ)) {
    GST_CAT_WARNING (GST_CAT_GL_BASE_BUFFER,
        "could not read map source memory %p", src);
    return FALSE;
  }

  if (!gst_memory_map ((GstMemory *) dest, &dinfo, GST_MAP_WRITE)) {
    GST_CAT_WARNING (GST_CAT_GL_BASE_BUFFER,
        "could not write map dest memory %p", dest);
    gst_memory_unmap ((GstMemory *) src, &sinfo);
    return FALSE;
  }

  GST_CAT_DEBUG (GST_CAT_GL_BASE_BUFFER,
      "memcpy %" G_GSSIZE_FORMAT " memory %p -> %p", size, src, dest);
  memcpy (dinfo.data, sinfo.data + offset, size);
  gst_memory_unmap ((GstMemory *) dest, &dinfo);
  gst_memory_unmap ((GstMemory *) src, &sinfo);

  return TRUE;
}

static GstGLBaseBuffer *
_default_copy (GstGLBaseBuffer * src, gssize offset, gssize size)
{
  return NULL;
}

struct copy_params
{
  GstGLBaseBuffer *src;
  GstGLBaseBuffer *dest;
  gssize offset;
  gssize size;
  gboolean result;
};

static void
_mem_copy_gl (GstGLContext * context, struct copy_params *transfer)
{
  GstGLBaseBufferAllocatorClass *alloc_class;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_GET_CLASS (transfer->src->mem.allocator);

  g_return_if_fail (alloc_class->copy != NULL);

  transfer->dest =
      alloc_class->copy (transfer->src, transfer->offset, transfer->size);
}

static GstMemory *
_mem_copy (GstGLBaseBuffer * src, gssize offset, gssize size)
{
  struct copy_params transfer;

  transfer.dest = NULL;
  transfer.src = src;
  transfer.offset = offset;
  transfer.size = size;
  if (size == -1 || size > 0)
    gst_gl_context_thread_add (src->context,
        (GstGLContextThreadFunc) _mem_copy_gl, &transfer);

  return (GstMemory *) transfer.dest;
}

static GstMemory *
_mem_share (GstGLBaseBuffer * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
_mem_is_span (GstGLBaseBuffer * mem1, GstGLBaseBuffer * mem2, gsize * offset)
{
  return FALSE;
}

static GstMemory *
_mem_alloc (GstAllocator * allocator, gsize size, GstAllocationParams * params)
{
  g_critical ("Subclass should override GstAllocatorClass::alloc() function");

  return NULL;
}

static void
_default_destroy (GstGLBaseBuffer * mem)
{
}

static void
_destroy_gl_objects (GstGLContext * context, GstGLBaseBuffer * mem)
{
  GstGLBaseBufferAllocatorClass *alloc_class;

  alloc_class = GST_GL_BASE_BUFFER_ALLOCATOR_GET_CLASS (mem->mem.allocator);

  g_return_if_fail (alloc_class->destroy != NULL);

  alloc_class->destroy (mem);
}

static void
_mem_free (GstAllocator * allocator, GstMemory * memory)
{
  GstGLBaseBuffer *mem = (GstGLBaseBuffer *) memory;

  GST_CAT_TRACE (GST_CAT_GL_BASE_BUFFER, "freeing buffer memory:%p id:%u", mem,
      mem->id);

  gst_gl_context_thread_add (mem->context,
      (GstGLContextThreadFunc) _destroy_gl_objects, mem);

  g_mutex_clear (&mem->lock);

  if (mem->alloc_data) {
    g_free (mem->alloc_data);
    mem->alloc_data = NULL;
  }
  mem->data = NULL;

  gst_object_unref (mem->context);
}

G_DEFINE_TYPE (GstGLBaseBufferAllocator, gst_gl_base_buffer_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_gl_base_buffer_allocator_class_init (GstGLBaseBufferAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _mem_alloc;
  allocator_class->free = _mem_free;

  klass->create = _default_create;
  klass->map_buffer = _default_map_buffer;
  klass->unmap_buffer = _default_unmap_buffer;
  klass->copy = _default_copy;
  klass->destroy = _default_destroy;
}

static void
gst_gl_base_buffer_allocator_init (GstGLBaseBufferAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_GL_MEMORY_ALLOCATOR;
  alloc->mem_map_full = (GstMemoryMapFullFunction) _mem_map_full;
  alloc->mem_unmap_full = (GstMemoryUnmapFullFunction) _mem_unmap_full;
  alloc->mem_copy = (GstMemoryCopyFunction) _mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _mem_is_span;
}

/**
 * gst_gl_base_buffer_init_once:
 *
 * Initializes the GL Buffer allocator. It is safe to call this function
 * multiple times.  This must be called before any other GstGLBaseBuffer operation.
 */
void
gst_gl_base_buffer_init_once (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_BASE_BUFFER, "glbuffer", 0,
        "OpenGL Buffer");

    _gl_base_buffer_allocator =
        g_object_new (gst_gl_base_buffer_allocator_get_type (), NULL);

    gst_allocator_register (GST_GL_BASE_BUFFER_ALLOCATOR_NAME,
        gst_object_ref (_gl_base_buffer_allocator));
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_is_gl_base_buffer:
 * @mem:a #GstMemory
 * 
 * Returns: whether the memory at @mem is a #GstGLBaseBuffer
 */
gboolean
gst_is_gl_base_buffer (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      g_type_is_a (G_OBJECT_TYPE (mem->allocator),
      GST_TYPE_GL_BASE_BUFFER_ALLOCATOR);
}
