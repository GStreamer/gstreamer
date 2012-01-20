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
 * A GstMemory object has an allocated region of memory of maxsize. The maximum
 * size does not change during the lifetime of the memory object. The memory
 * also has an offset and size property that specifies the valid range of memory
 * in the allocated region.
 *
 * Memory is usually created by allocators with a gst_allocator_alloc()
 * method call. When NULL is used as the allocator, the default allocator will
 * be used.
 *
 * New allocators can be registered with gst_allocator_register().
 * Allocators are identified by name and can be retrieved with
 * gst_allocator_find().
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
 * The call will return a pointer to offset bytes into the region of memory.
 * After the memory access is completed, gst_memory_unmap() should be called.
 *
 * Memory can be copied with gst_memory_copy(), which will returnn a writable
 * copy. gst_memory_share() will create a new memory block that shares the
 * memory with an existing memory block at a custom offset and with a custom
 * size.
 *
 * Memory can be efficiently merged when gst_memory_is_span() returns TRUE.
 *
 * Last reviewed on 2011-06-08 (0.11.0)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstmemory.h"

G_DEFINE_BOXED_TYPE (GstMemory, gst_memory, (GBoxedCopyFunc) gst_memory_ref,
    (GBoxedFreeFunc) gst_memory_unref);

/**
 * gst_memory_alignment:
 *
 * The default memory alignment in bytes - 1
 * an alignment of 7 would be the same as what malloc() guarantees.
 */
#if defined(MEMORY_ALIGNMENT_MALLOC)
size_t gst_memory_alignment = 7;
#elif defined(MEMORY_ALIGNMENT_PAGESIZE)
/* we fill this in in the _init method */
size_t gst_memory_alignment = 0;
#elif defined(MEMORY_ALIGNMENT)
size_t gst_memory_alignment = MEMORY_ALIGNMENT - 1;
#else
#error "No memory alignment configured"
size_t gst_memory_alignment = 0;
#endif

struct _GstAllocator
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
} GstMemoryDefault;

/* the default allocator */
static const GstAllocator *_default_allocator;

/* our predefined allocators */
static const GstAllocator *_default_mem_impl;

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
  mem->mem.state = (flags & GST_MEMORY_FLAG_READONLY ? 0x5 : 0);
  mem->mem.maxsize = maxsize;
  mem->mem.offset = offset;
  mem->mem.size = size;
  mem->slice_size = slice_size;
  mem->data = data;
  mem->free_func = free_func;
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
_default_mem_alloc (const GstAllocator * allocator, gsize maxsize, gsize align)
{
  return (GstMemory *) _default_mem_new_block (maxsize, align, 0, maxsize);
}

static gpointer
_default_mem_map (GstMemoryDefault * mem, GstMapFlags flags)
{
  return mem->data;
}

static gboolean
_default_mem_unmap (GstMemoryDefault * mem)
{
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
_default_mem_copy (GstMemoryDefault * mem, gssize offset, gsize size)
{
  GstMemoryDefault *copy;

  if (size == -1)
    size = mem->mem.size > offset ? mem->mem.size - offset : 0;

  copy =
      _default_mem_new_block (mem->mem.maxsize, 0, mem->mem.offset + offset,
      size);
  memcpy (copy->data, mem->data, mem->mem.maxsize);

  return copy;
}

static GstMemoryDefault *
_default_mem_share (GstMemoryDefault * mem, gssize offset, gsize size)
{
  GstMemoryDefault *sub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub =
      _default_mem_new (parent->flags, parent, mem->data, NULL,
      mem->mem.maxsize, mem->mem.offset + offset, size);

  return sub;
}

static gboolean
_default_mem_is_span (GstMemoryDefault * mem1, GstMemoryDefault * mem2,
    gsize * offset)
{

  if (offset) {
    GstMemoryDefault *parent;

    parent = (GstMemoryDefault *) mem1->mem.parent;

    *offset = mem1->mem.offset - parent->mem.offset;
  }

  /* and memory is contiguous */
  return mem1->data + mem1->mem.offset + mem1->mem.size ==
      mem2->data + mem2->mem.offset;
}

static GstMemory *
_fallback_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstMemory *copy;
  GstMapInfo sinfo, dinfo;

  if (!gst_memory_map (mem, &sinfo, GST_MAP_READ))
    return NULL;

  if (size == -1)
    size = sinfo.size > offset ? sinfo.size - offset : 0;

  /* use the same allocator as the memory we copy  */
  copy = gst_allocator_alloc (mem->allocator, size, mem->align);
  if (!gst_memory_map (copy, &dinfo, GST_MAP_WRITE)) {
    GST_WARNING ("could not write map memory %p", copy);
    gst_memory_unmap (mem, &sinfo);
    return NULL;
  }

  memcpy (dinfo.data, sinfo.data + offset, size);
  gst_memory_unmap (copy, &dinfo);
  gst_memory_unmap (mem, &sinfo);

  return copy;
}

static gboolean
_fallback_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GRWLock lock;
static GHashTable *allocators;

void
_priv_gst_memory_initialize (void)
{
  static const GstMemoryInfo _mem_info = {
    (GstMemoryAllocFunction) _default_mem_alloc,
    (GstMemoryMapFunction) _default_mem_map,
    (GstMemoryUnmapFunction) _default_mem_unmap,
    (GstMemoryFreeFunction) _default_mem_free,
    (GstMemoryCopyFunction) _default_mem_copy,
    (GstMemoryShareFunction) _default_mem_share,
    (GstMemoryIsSpanFunction) _default_mem_is_span,
    NULL
  };

  g_rw_lock_init (&lock);
  allocators = g_hash_table_new (g_str_hash, g_str_equal);

#ifdef HAVE_GETPAGESIZE
#ifdef MEMORY_ALIGNMENT_PAGESIZE
  gst_memory_alignment = getpagesize () - 1;
#endif
#endif

  GST_DEBUG ("memory alignment: %" G_GSIZE_FORMAT, gst_memory_alignment);

  _default_mem_impl = gst_allocator_register (GST_ALLOCATOR_SYSMEM, &_mem_info);

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
 * @offset: pointer to offset
 * @maxsize: pointer to maxsize
 *
 * Get the current @size, @offset and @maxsize of @mem.
 *
 * Returns: the current sizes of @mem
 */
gsize
gst_memory_get_sizes (GstMemory * mem, gsize * offset, gsize * maxsize)
{
  g_return_val_if_fail (mem != NULL, 0);

  if (offset)
    *offset = mem->offset;
  if (maxsize)
    *maxsize = mem->maxsize;

  return mem->size;
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
gst_memory_resize (GstMemory * mem, gssize offset, gsize size)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (gst_memory_is_writable (mem));
  g_return_if_fail (offset >= 0 || mem->offset >= -offset);
  g_return_if_fail (size + mem->offset + offset <= mem->maxsize);

  mem->offset += offset;
  mem->size = size;
}

/**
 * gst_memory_is_writable:
 * @mem: a #GstMemory
 *
 * Check if @mem is writable.
 *
 * Returns: %TRUE is @mem is writable.
 */
gboolean
gst_memory_is_writable (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return (mem->refcount == 1) &&
      ((mem->parent == NULL) || (mem->parent->refcount == 1)) &&
      ((mem->flags & GST_MEMORY_FLAG_READONLY) == 0);
}

static gboolean
gst_memory_lock (GstMemory * mem, GstMapFlags flags)
{
  gint access_mode, state, newstate;

  access_mode = flags & 3;

  do {
    state = g_atomic_int_get (&mem->state);
    if (state == 0) {
      /* nothing mapped, set access_mode and refcount */
      newstate = 4 | access_mode;
    } else {
      /* access_mode must match */
      if ((state & access_mode) != access_mode)
        goto lock_failed;
      /* increase refcount */
      newstate = state + 4;
    }
  } while (!g_atomic_int_compare_and_exchange (&mem->state, state, newstate));

  return TRUE;

lock_failed:
  {
    GST_DEBUG ("lock failed %p: state %d, access_mode %d", mem, state,
        access_mode);
    return FALSE;
  }
}

static void
gst_memory_unlock (GstMemory * mem)
{
  gint state, newstate;

  do {
    state = g_atomic_int_get (&mem->state);
    /* decrease the refcount */
    newstate = state - 4;
    /* last refcount, unset access_mode */
    if (newstate < 4)
      newstate = 0;
  } while (!g_atomic_int_compare_and_exchange (&mem->state, state, newstate));
}


/**
 * gst_memory_make_mapped:
 * @mem: (transfer full): a #GstMemory
 * @info: (out): pointer for info
 * @flags: mapping flags
 *
 * Create a #GstMemory object that is mapped with @flags. If @mem is mappable
 * with @flags, this function returns the mapped @mem directly. Otherwise a
 * mapped copy of @mem is returned.
 *
 * This function takes ownership of old @mem and returns a reference to a new
 * #GstMemory.
 *
 * Returns: (transfer full): a #GstMemory object mapped with @flags or NULL when
 * a mapping is not possible.
 */
GstMemory *
gst_memory_make_mapped (GstMemory * mem, GstMapInfo * info, GstMapFlags flags)
{
  GstMemory *result;

  if (gst_memory_map (mem, info, flags)) {
    result = mem;
  } else {
    result = gst_memory_copy (mem, 0, -1);
    if (result == NULL)
      goto cannot_copy;

    if (!gst_memory_map (result, info, flags))
      goto cannot_map;
  }
  return result;

  /* ERRORS */
cannot_copy:
  {
    GST_DEBUG ("cannot copy memory %p", mem);
    return NULL;
  }
cannot_map:
  {
    GST_DEBUG ("cannot map memory %p with flags %d", mem, flags);
    return NULL;
  }
}

/**
 * gst_memory_map:
 * @mem: a #GstMemory
 * @info: (out): pointer for info
 * @flags: mapping flags
 *
 * Fill @info with the pointer and sizes of the memory in @mem that can be
 * accessed according to @flags.
 *
 * This function can return %FALSE for various reasons:
 * - the memory backed by @mem is not accessible with the given @flags.
 * - the memory was already mapped with a different mapping.
 *
 * @info and its contents remains valid for as long as @mem is alive and until
 * gst_memory_unmap() is called.
 *
 * For each gst_memory_map() call, a corresponding gst_memory_unmap() call
 * should be done.
 *
 * Returns: %TRUE if the map operation was successful.
 */
gboolean
gst_memory_map (GstMemory * mem, GstMapInfo * info, GstMapFlags flags)
{
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (!gst_memory_lock (mem, flags))
    goto lock_failed;

  info->data = mem->allocator->info.map (mem, mem->maxsize, flags);

  if (G_UNLIKELY (info->data == NULL))
    goto error;

  info->memory = mem;
  info->size = mem->size;
  info->maxsize = mem->maxsize - mem->offset;
  info->data = info->data + mem->offset;

  return TRUE;

  /* ERRORS */
lock_failed:
  {
    GST_DEBUG ("mem %p: lock %d failed", flags);
    return FALSE;
  }
error:
  {
    /* something went wrong, restore the orginal state again */
    GST_ERROR ("mem %p: map failed", mem);
    gst_memory_unlock (mem);
    return FALSE;
  }
}

/**
 * gst_memory_unmap:
 * @mem: a #GstMemory
 * @info: a #GstMapInfo
 *
 * Release the memory obtained with gst_memory_map()
 */
void
gst_memory_unmap (GstMemory * mem, GstMapInfo * info)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (info != NULL);
  g_return_if_fail (info->memory == mem);
  /* there must be a ref */
  g_return_if_fail (g_atomic_int_get (&mem->state) >= 4);

  mem->allocator->info.unmap (mem);
  gst_memory_unlock (mem);
}

/**
 * gst_memory_copy:
 * @mem: a #GstMemory
 * @offset: an offset to copy
 * @size: size to copy or -1 to copy all bytes from offset
 *
 * Return a copy of @size bytes from @mem starting from @offset. This copy is
 * guaranteed to be writable. @size can be set to -1 to return a copy all bytes
 * from @offset.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstMemory *copy;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (gst_memory_lock (mem, GST_MAP_READ), NULL);

  copy = mem->allocator->info.copy (mem, offset, size);

  gst_memory_unlock (mem);

  return copy;
}

/**
 * gst_memory_share:
 * @mem: a #GstMemory
 * @offset: an offset to share
 * @size: size to share or -1 to share bytes from offset
 *
 * Return a shared copy of @size bytes from @mem starting from @offset. No
 * memory copy is performed and the memory region is simply shared. The result
 * is guaranteed to be not-writable. @size can be set to -1 to return a share
 * all bytes from @offset.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_share (GstMemory * mem, gssize offset, gssize size)
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
 * gst_allocator_register:
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
 * Returns: a new #GstAllocator.
 */
const GstAllocator *
gst_allocator_register (const gchar * name, const GstMemoryInfo * info)
{
  GstAllocator *allocator;

#define INSTALL_FALLBACK(_t) \
  if (allocator->info._t == NULL) allocator->info._t = _fallback_ ##_t;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->alloc != NULL, NULL);
  g_return_val_if_fail (info->map != NULL, NULL);
  g_return_val_if_fail (info->unmap != NULL, NULL);
  g_return_val_if_fail (info->free != NULL, NULL);
  g_return_val_if_fail (info->share != NULL, NULL);

  allocator = g_slice_new (GstAllocator);
  allocator->name = g_quark_from_string (name);
  allocator->info = *info;
  INSTALL_FALLBACK (copy);
  INSTALL_FALLBACK (is_span);
#undef INSTALL_FALLBACK

  GST_DEBUG ("registering allocator \"%s\"", name);

  g_rw_lock_writer_lock (&lock);
  g_hash_table_insert (allocators, (gpointer) name, (gpointer) allocator);
  g_rw_lock_writer_unlock (&lock);

  return allocator;
}

/**
 * gst_allocator_find:
 * @name: the name of the allocator
 *
 * Find a previously registered allocator with @name. When @name is NULL, the
 * default allocator will be returned.
 *
 * Returns: a #GstAllocator or NULL when the allocator with @name was not
 * registered.
 */
const GstAllocator *
gst_allocator_find (const gchar * name)
{
  const GstAllocator *allocator;

  g_rw_lock_reader_lock (&lock);
  if (name) {
    allocator = g_hash_table_lookup (allocators, (gconstpointer) name);
  } else {
    allocator = _default_allocator;
  }
  g_rw_lock_reader_unlock (&lock);

  return allocator;
}

/**
 * gst_allocator_set_default:
 * @allocator: a #GstAllocator
 *
 * Set the default allocator.
 */
void
gst_allocator_set_default (const GstAllocator * allocator)
{
  g_return_if_fail (allocator != NULL);

  g_rw_lock_writer_lock (&lock);
  _default_allocator = allocator;
  g_rw_lock_writer_unlock (&lock);
}

/**
 * gst_allocator_alloc:
 * @allocator: (transfer none) (allow-none): a #GstAllocator to use
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
 * Returns: (transfer full): a new #GstMemory.
 */
GstMemory *
gst_allocator_alloc (const GstAllocator * allocator, gsize maxsize, gsize align)
{
  g_return_val_if_fail (((align + 1) & align) == 0, NULL);

  if (allocator == NULL)
    allocator = _default_allocator;

  return allocator->info.alloc (allocator, maxsize, align,
      allocator->info.user_data);
}
