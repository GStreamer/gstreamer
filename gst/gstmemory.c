/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.be>
 *
 * gstmemory.c: memory block handling
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

/**
 * SECTION:gstmemory
 * @short_description: refcounted wrapper for memory blocks
 * @see_also: #GstBuffer
 *
 * GstMemory is a lightweight refcounted object that wraps a region of memory.
 * They are typically used to manage the data of a #GstBuffer.
 *
 * Memory is usually created by allocators with a gst_memory_allocator_alloc()
 * method call. When NULL is used as the allocator, the default allocator will
 * be used.
 *
 * New allocators can be registered with gst_memory_allocator_register().
 * Allocators are identified by name and can be retrieved with
 * gst_memory_allocator_find().
 *
 * New memory can be created with gst_memory_new_wrapped() that wraps the memory
 * allocated elsewhere.
 *
 * Refcounting of the memory block is performed with gst_memory_ref() and
 * gst_memory_unref().
 *
 * The size of the memory can be retrieved and changed with
 * gst_memory_get_sizes() and gst_memory_resize() respectively.
 *
 * Getting access to the data of the memory is performed with gst_memory_map().
 * After the memory access is completed, gst_memory_unmap() should be called.
 *
 * Memory can be copied with gst_memory_copy(), which will returnn a writable
 * copy. gst_memory_share() will create a new memory block that shares the
 * memory with an existing memory block at a custom offset and with a custom
 * size.
 *
 * Memory can be efficiently merged when gst_memory_is_span() returns TRUE and
 * with the function gst_memory_span().
 *
 * Last reviewed on 2011-06-08 (0.11.0)
 */

#include "config.h"
#include "gst_private.h"
#include "gstmemory.h"


/* buffer alignment in bytes - 1
 * an alignment of 7 would be the same as malloc() guarantees
 */
#ifdef HAVE_POSIX_MEMALIGN
#if defined(MEMORY_ALIGNMENT_MALLOC)
size_t gst_memory_alignment = 7;
#elif defined(MEMORY_ALIGNMENT_PAGESIZE)
size_t gst_memory_alignment = 0;
#elif defined(MEMORY_ALIGNMENT)
size_t gst_memory_alignment = MEMORY_ALIGNMENT - 1;
#else
#error "No memory alignment configured"
size_t gst_memory_alignment = 0;
#endif
#endif /* HAVE_POSIX_MEMALIGN */

struct _GstMemoryAllocator
{
  GQuark name;

  GstMemoryInfo info;
};

/* default memory implementation */
typedef struct
{
  GstMemory mem;
  gsize slice_size;
  guint8 *data;
  GFreeFunc free_func;
  gsize maxsize;
  gsize offset;
  gsize size;
} GstMemoryDefault;

/* the default allocator */
static const GstMemoryAllocator *_default_allocator;

/* our predefined allocators */
static const GstMemoryAllocator *_default_mem_impl;

/* initialize the fields */
static void
_default_mem_init (GstMemoryDefault * mem, GstMemoryFlags flags,
    GstMemory * parent, gsize slice_size, gpointer data,
    GFreeFunc free_func, gsize maxsize, gsize offset, gsize size)
{
  mem->mem.allocator = _default_mem_impl;
  mem->mem.flags = flags;
  mem->mem.refcount = 1;
  mem->mem.parent = parent ? gst_memory_ref (parent) : NULL;
  mem->slice_size = slice_size;
  mem->data = data;
  mem->free_func = free_func;
  mem->maxsize = maxsize;
  mem->offset = offset;
  mem->size = size;
}

/* create a new memory block that manages the given memory */
static GstMemoryDefault *
_default_mem_new (GstMemoryFlags flags, GstMemory * parent, gpointer data,
    GFreeFunc free_func, gsize maxsize, gsize offset, gsize size)
{
  GstMemoryDefault *mem;
  gsize slice_size;

  slice_size = sizeof (GstMemoryDefault);

  mem = g_slice_alloc (slice_size);
  _default_mem_init (mem, flags, parent, slice_size,
      data, free_func, maxsize, offset, size);

  return mem;
}

/* allocate the memory and structure in one block */
static GstMemoryDefault *
_default_mem_new_block (gsize maxsize, gsize align, gsize offset, gsize size)
{
  GstMemoryDefault *mem;
  gsize aoffset, slice_size;
  guint8 *data;

  /* ensure configured alignment */
  align |= gst_memory_alignment;
  /* allocate more to compensate for alignment */
  maxsize += align;
  /* alloc header and data in one block */
  slice_size = sizeof (GstMemoryDefault) + maxsize;

  mem = g_slice_alloc (slice_size);
  if (mem == NULL)
    return NULL;

  data = (guint8 *) mem + sizeof (GstMemoryDefault);

  if ((aoffset = ((guintptr) data & align)))
    aoffset = (align + 1) - aoffset;

  _default_mem_init (mem, 0, NULL, slice_size, data, NULL, maxsize,
      aoffset + offset, size);

  return mem;
}

static GstMemory *
_default_mem_alloc (const GstMemoryAllocator * allocator, gsize maxsize,
    gsize align)
{
  return (GstMemory *) _default_mem_new_block (maxsize, align, 0, maxsize);
}

static gsize
_default_mem_get_sizes (GstMemoryDefault * mem, gsize * maxsize)
{
  if (maxsize)
    *maxsize = mem->maxsize;

  return mem->size;
}

static void
_default_mem_resize (GstMemoryDefault * mem, gsize offset, gsize size)
{
  g_return_if_fail (size + mem->offset + offset <= mem->maxsize);

  mem->offset += offset;
  mem->size = size;
}

static gpointer
_default_mem_map (GstMemoryDefault * mem, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  if (size)
    *size = mem->size;
  if (maxsize)
    *maxsize = mem->maxsize;

  return mem->data + mem->offset;
}

static gboolean
_default_mem_unmap (GstMemoryDefault * mem, gpointer data, gsize size)
{
  if (size != -1)
    mem->size = size;
  return TRUE;
}

static void
_default_mem_free (GstMemoryDefault * mem)
{
  if (mem->mem.parent)
    gst_memory_unref (mem->mem.parent);

  if (mem->free_func)
    mem->free_func (mem->data);

  g_slice_free1 (mem->slice_size, mem);
}

static GstMemoryDefault *
_default_mem_copy (GstMemoryDefault * mem, gsize offset, gsize size)
{
  GstMemoryDefault *copy;

  if (size == -1)
    size = mem->size > offset ? mem->size - offset : 0;

  copy = _default_mem_new_block (mem->maxsize, 0, mem->offset + offset, size);
  memcpy (copy->data, mem->data, mem->maxsize);

  return copy;
}

static GstMemoryDefault *
_default_mem_share (GstMemoryDefault * mem, gsize offset, gsize size)
{
  GstMemoryDefault *sub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->size - offset;

  sub = _default_mem_new (parent->flags, parent, mem->data, NULL, mem->maxsize,
      mem->offset + offset, size);

  return sub;
}

static gboolean
_default_mem_is_span (GstMemoryDefault * mem1, GstMemoryDefault * mem2,
    gsize * offset)
{

  if (offset) {
    GstMemoryDefault *parent;

    parent = (GstMemoryDefault *) mem1->mem.parent;

    *offset = mem1->offset - parent->offset;
  }

  /* and memory is contiguous */
  return mem1->data + mem1->offset + mem1->size == mem2->data + mem2->offset;
}

static GstMemory *
_fallback_copy (GstMemory * mem, gsize offset, gsize size)
{
  GstMemory *copy;
  guint8 *data, *dest;
  gsize msize;

  data = gst_memory_map (mem, &msize, NULL, GST_MAP_READ);
  if (size == -1)
    size = msize > offset ? msize - offset : 0;
  /* use the same allocator as the memory we copy, FIXME, alignment?  */
  copy = gst_memory_allocator_alloc (mem->allocator, size, 1);
  dest = gst_memory_map (copy, NULL, NULL, GST_MAP_WRITE);
  memcpy (dest, data + offset, size);
  gst_memory_unmap (copy, dest, size);

  gst_memory_unmap (mem, data, msize);

  return (GstMemory *) copy;
}

static gboolean
_fallback_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GStaticRWLock lock = G_STATIC_RW_LOCK_INIT;
static GHashTable *allocators;

void
_gst_memory_init (void)
{
  static const GstMemoryInfo _mem_info = {
    (GstMemoryAllocFunction) _default_mem_alloc,
    (GstMemoryGetSizesFunction) _default_mem_get_sizes,
    (GstMemoryResizeFunction) _default_mem_resize,
    (GstMemoryMapFunction) _default_mem_map,
    (GstMemoryUnmapFunction) _default_mem_unmap,
    (GstMemoryFreeFunction) _default_mem_free,
    (GstMemoryCopyFunction) _default_mem_copy,
    (GstMemoryShareFunction) _default_mem_share,
    (GstMemoryIsSpanFunction) _default_mem_is_span,
    NULL
  };

  allocators = g_hash_table_new (g_str_hash, g_str_equal);

#ifdef HAVE_GETPAGESIZE
#ifdef MEMORY_ALIGNMENT_PAGESIZE
  gst_memory_alignment = getpagesize () - 1;
#endif
#endif

  _default_mem_impl =
      gst_memory_allocator_register ("GstMemoryDefault", &_mem_info);

  _default_allocator = _default_mem_impl;
}

/**
 * gst_memory_new_wrapped:
 * @flags: #GstMemoryFlags
 * @data: data to wrap
 * @free_func: function to free @data
 * @maxsize: allocated size of @data
 * @offset: offset in @data
 * @size: size of valid data
 *
 * Allocate a new memory block that wraps the given @data.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_new_wrapped (GstMemoryFlags flags, gpointer data,
    GFreeFunc free_func, gsize maxsize, gsize offset, gsize size)
{
  GstMemoryDefault *mem;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (offset + size <= maxsize, NULL);

  mem = _default_mem_new (flags, NULL, data, free_func, maxsize, offset, size);

  return (GstMemory *) mem;
}

/**
 * gst_memory_ref:
 * @mem: a #GstMemory
 *
 * Increases the refcount of @mem.
 *
 * Returns: @mem with increased refcount
 */
GstMemory *
gst_memory_ref (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, NULL);

  g_atomic_int_inc (&mem->refcount);

  return mem;
}

/**
 * gst_memory_unref:
 * @mem: a #GstMemory
 *
 * Decreases the refcount of @mem. When the refcount reaches 0, the free
 * function of @mem will be called.
 */
void
gst_memory_unref (GstMemory * mem)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (mem->allocator != NULL);

  if (g_atomic_int_dec_and_test (&mem->refcount))
    mem->allocator->info.free (mem);
}

/**
 * gst_memory_get_sizes:
 * @mem: a #GstMemory
 * @maxsize: pointer to maxsize
 *
 * Get the current @size and @maxsize of @mem.
 *
 * Returns: the current sizes of @mem
 */
gsize
gst_memory_get_sizes (GstMemory * mem, gsize * maxsize)
{
  g_return_val_if_fail (mem != NULL, 0);

  return mem->allocator->info.get_sizes (mem, maxsize);
}

/**
 * gst_memory_resize:
 * @mem: a #GstMemory
 * @offset: a new offset
 * @size: a new size
 *
 * Resize the memory region. @mem should be writable and offset + size should be
 * less than the maxsize of @mem.
 */
void
gst_memory_resize (GstMemory * mem, gsize offset, gsize size)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (GST_MEMORY_IS_WRITABLE (mem));

  mem->allocator->info.resize (mem, offset, size);
}

/**
 * gst_memory_map:
 * @mem: a #GstMemory
 * @size: pointer for size
 * @maxsize: pointer for maxsize
 * @flags: mapping flags
 *
 * Get a pointer to the memory of @mem that can be accessed according to @flags.
 *
 * @size and @maxsize will contain the size of the memory and the maximum
 * allocated memory of @mem respectively. They can be set to NULL.
 *
 * Returns: a pointer to the memory of @mem.
 */
gpointer
gst_memory_map (GstMemory * mem, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (!(flags & GST_MAP_WRITE) ||
      GST_MEMORY_IS_WRITABLE (mem), NULL);

  return mem->allocator->info.map (mem, size, maxsize, flags);
}

/**
 * gst_memory_unmap:
 * @mem: a #GstMemory
 * @data: data to unmap
 * @size: new size of @mem
 *
 * Release the memory pointer obtained with gst_memory_map() and set the size of
 * the memory to @size. @size can be set to -1 when the size should not be
 * updated.
 *
 * Returns: TRUE when the memory was release successfully.
 */
gboolean
gst_memory_unmap (GstMemory * mem, gpointer data, gsize size)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return mem->allocator->info.unmap (mem, data, size);
}

/**
 * gst_memory_copy:
 * @mem: a #GstMemory
 * @offset: an offset to copy
 * @size: size to copy
 *
 * Return a copy of @size bytes from @mem starting from @offset. This copy is
 * guaranteed to be writable. @size can be set to -1 to return a copy all bytes
 * from @offset.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_copy (GstMemory * mem, gsize offset, gsize size)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->allocator->info.copy (mem, offset, size);
}

/**
 * gst_memory_share:
 * @mem: a #GstMemory
 * @offset: an offset to share
 * @size: size to share
 *
 * Return a shared copy of @size bytes from @mem starting from @offset. No memory
 * copy is performed and the memory region is simply shared. The result is
 * guaranteed to be not-writable. @size can be set to -1 to return a share all bytes
 * from @offset.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_share (GstMemory * mem, gsize offset, gsize size)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->allocator->info.share (mem, offset, size);
}

/**
 * gst_memory_is_span:
 * @mem1: a #GstMemory
 * @mem2: a #GstMemory
 * @offset: a pointer to a result offset
 *
 * Check if @mem1 and mem2 share the memory with a common parent memory object
 * and that the memory is contiguous.
 *
 * If this is the case, the memory of @mem1 and @mem2 can be merged
 * efficiently by performing gst_memory_share() on the parent object from
 * the returned @offset.
 *
 * Returns: %TRUE if the memory is contiguous and of a common parent.
 */
gboolean
gst_memory_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  g_return_val_if_fail (mem1 != NULL, FALSE);
  g_return_val_if_fail (mem2 != NULL, FALSE);

  /* need to have the same allocators */
  if (mem1->allocator != mem2->allocator)
    return FALSE;

  /* need to have the same parent */
  if (mem1->parent == NULL || mem1->parent != mem2->parent)
    return FALSE;

  /* and memory is contiguous */
  if (!mem1->allocator->info.is_span (mem1, mem2, offset))
    return FALSE;

  return TRUE;
}

/**
 * gst_memory_allocator_register:
 * @name: the name of the allocator
 * @info: #GstMemoryInfo
 *
 * Registers the memory allocator with @name and implementation functions
 * @info.
 *
 * All functions in @info are mandatory exept the copy and is_span
 * functions, which will have a default implementation when left NULL.
 *
 * The user_data field in @info will be passed to all calls of the alloc
 * function.
 *
 * Returns: a new #GstMemoryAllocator.
 */
const GstMemoryAllocator *
gst_memory_allocator_register (const gchar * name, const GstMemoryInfo * info)
{
  GstMemoryAllocator *allocator;

#define INSTALL_FALLBACK(_t) \
  if (allocator->info._t == NULL) allocator->info._t = _fallback_ ##_t;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->alloc != NULL, NULL);
  g_return_val_if_fail (info->get_sizes != NULL, NULL);
  g_return_val_if_fail (info->resize != NULL, NULL);
  g_return_val_if_fail (info->map != NULL, NULL);
  g_return_val_if_fail (info->unmap != NULL, NULL);
  g_return_val_if_fail (info->free != NULL, NULL);
  g_return_val_if_fail (info->share != NULL, NULL);

  allocator = g_slice_new (GstMemoryAllocator);
  allocator->name = g_quark_from_string (name);
  allocator->info = *info;
  INSTALL_FALLBACK (copy);
  INSTALL_FALLBACK (is_span);
#undef INSTALL_FALLBACK

  GST_DEBUG ("register \"%s\" of size %" G_GSIZE_FORMAT, name);

  g_static_rw_lock_writer_lock (&lock);
  g_hash_table_insert (allocators, (gpointer) name, (gpointer) allocator);
  g_static_rw_lock_writer_unlock (&lock);

  return allocator;
}

/**
 * gst_memory_allocator_find:
 * @name: the name of the allocator
 *
 * Find a previously registered allocator with @name. When @name is NULL, the
 * default allocator will be returned.
 *
 * Returns: a #GstMemoryAllocator or NULL when the allocator with @name was not
 * registered.
 */
const GstMemoryAllocator *
gst_memory_allocator_find (const gchar * name)
{
  const GstMemoryAllocator *allocator;

  g_static_rw_lock_reader_lock (&lock);
  if (name) {
    allocator = g_hash_table_lookup (allocators, (gconstpointer) name);
  } else {
    allocator = _default_allocator;
  }
  g_static_rw_lock_reader_unlock (&lock);

  return allocator;
}

/**
 * gst_memory_allocator_set_default:
 * @allocator: a ##GstMemoryAllocator
 *
 * Set the default allocator.
 */
void
gst_memory_allocator_set_default (const GstMemoryAllocator * allocator)
{
  g_return_if_fail (allocator != NULL);

  g_static_rw_lock_writer_lock (&lock);
  _default_allocator = allocator;
  g_static_rw_lock_writer_unlock (&lock);
}

/**
 * gst_memory_allocator_alloc:
 * @allocator: a #GstMemoryAllocator to use
 * @maxsize: allocated size of @data
 * @align: alignment for the data
 *
 * Use @allocator to allocate a new memory block with memory that is at least
 * @maxsize big and has the given alignment.
 *
 * When @allocator is NULL, the default allocator will be used.
 *
 * @align is given as a bitmask so that @align + 1 equals the amount of bytes to
 * align to. For example, to align to 8 bytes, use an alignment of 7.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_allocator_alloc (const GstMemoryAllocator * allocator,
    gsize maxsize, gsize align)
{
  g_return_val_if_fail (((align + 1) & align) == 0, NULL);

  if (allocator == NULL)
    allocator = _default_allocator;

  return allocator->info.alloc (allocator, maxsize, align,
      allocator->info.user_data);
}
