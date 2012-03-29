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
 * gst_allocator_find(). gst_allocator_set_default() can be used to change the
 * default allocator.
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
 * Memory can be copied with gst_memory_copy(), which will return a writable
 * copy. gst_memory_share() will create a new memory block that shares the
 * memory with an existing memory block at a custom offset and with a custom
 * size.
 *
 * Memory can be efficiently merged when gst_memory_is_span() returns TRUE.
 *
 * Last reviewed on 2012-03-28 (0.11.3)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstmemory.h"

#ifndef GST_DISABLE_TRACE
#include "gsttrace.h"
static GstAllocTrace *_gst_memory_trace;
static GstAllocTrace *_gst_allocator_trace;
#endif

G_DEFINE_BOXED_TYPE (GstMemory, gst_memory, (GBoxedCopyFunc) gst_memory_ref,
    (GBoxedFreeFunc) gst_memory_unref);

G_DEFINE_BOXED_TYPE (GstAllocator, gst_allocator,
    (GBoxedCopyFunc) gst_allocator_ref, (GBoxedFreeFunc) gst_allocator_unref);

G_DEFINE_BOXED_TYPE (GstAllocationParams, gst_allocation_params,
    (GBoxedCopyFunc) gst_allocation_params_copy,
    (GBoxedFreeFunc) gst_allocation_params_free);

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
  gint refcount;

  GstMemoryInfo info;

  gpointer user_data;
  GDestroyNotify notify;
};

/* default memory implementation */
typedef struct
{
  GstMemory mem;
  gsize slice_size;
  guint8 *data;
  gpointer user_data;
  GDestroyNotify notify;
} GstMemoryDefault;

/* the default allocator */
static GstAllocator *_default_allocator;

/* our predefined allocators */
static GstAllocator *_default_mem_impl;

/* initialize the fields */
static void
_default_mem_init (GstMemoryDefault * mem, GstMemoryFlags flags,
    GstMemory * parent, gsize slice_size, gpointer data,
    gsize maxsize, gsize offset, gsize size, gsize align,
    gpointer user_data, GDestroyNotify notify)
{
  mem->mem.allocator = _default_mem_impl;
  mem->mem.flags = flags;
  mem->mem.refcount = 1;
  mem->mem.parent = parent ? gst_memory_ref (parent) : NULL;
  mem->mem.state = (flags & GST_MEMORY_FLAG_READONLY ? 0x1 : 0);
  mem->mem.maxsize = maxsize;
  mem->mem.align = align;
  mem->mem.offset = offset;
  mem->mem.size = size;
  mem->slice_size = slice_size;
  mem->data = data;
  mem->user_data = user_data;
  mem->notify = notify;

  GST_CAT_DEBUG (GST_CAT_MEMORY, "new memory %p, maxsize:%" G_GSIZE_FORMAT
      " offset:%" G_GSIZE_FORMAT " size:%" G_GSIZE_FORMAT, mem, maxsize,
      offset, size);
}

/* create a new memory block that manages the given memory */
static GstMemoryDefault *
_default_mem_new (GstMemoryFlags flags, GstMemory * parent, gpointer data,
    gsize maxsize, gsize offset, gsize size, gsize align, gpointer user_data,
    GDestroyNotify notify)
{
  GstMemoryDefault *mem;
  gsize slice_size;

  slice_size = sizeof (GstMemoryDefault);

  mem = g_slice_alloc (slice_size);
  _default_mem_init (mem, flags, parent, slice_size,
      data, maxsize, offset, size, align, user_data, notify);

  return mem;
}

/* allocate the memory and structure in one block */
static GstMemoryDefault *
_default_mem_new_block (GstMemoryFlags flags, gsize maxsize, gsize align,
    gsize offset, gsize size)
{
  GstMemoryDefault *mem;
  gsize aoffset, slice_size, padding;
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

  /* do alignment */
  if ((aoffset = ((guintptr) data & align))) {
    aoffset = (align + 1) - aoffset;
    data += aoffset;
    maxsize -= aoffset;
  }

  if (offset && (flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (data, 0, offset);

  padding = maxsize - (offset + size);
  if (padding && (flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset (data + offset + size, 0, padding);

  _default_mem_init (mem, flags, NULL, slice_size, data, maxsize,
      offset, size, align, NULL, NULL);

  return mem;
}

static GstMemory *
_default_alloc_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params, gpointer user_data)
{
  gsize maxsize = size + params->prefix + params->padding;

  return (GstMemory *) _default_mem_new_block (params->flags,
      maxsize, params->align, params->prefix, size);
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
  GST_CAT_DEBUG (GST_CAT_MEMORY, "free memory %p", mem);

  if (mem->mem.parent)
    gst_memory_unref (mem->mem.parent);

  if (mem->notify)
    mem->notify (mem->user_data);

  g_slice_free1 (mem->slice_size, mem);
}

static GstMemoryDefault *
_default_mem_copy (GstMemoryDefault * mem, gssize offset, gsize size)
{
  GstMemoryDefault *copy;

  if (size == -1)
    size = mem->mem.size > offset ? mem->mem.size - offset : 0;

  copy =
      _default_mem_new_block (0, mem->mem.maxsize, 0, mem->mem.offset + offset,
      size);
  memcpy (copy->data, mem->data, mem->mem.maxsize);
  GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy memory %p -> %p", mem, copy);

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
      _default_mem_new (parent->flags, parent, mem->data,
      mem->mem.maxsize, mem->mem.offset + offset, size, mem->mem.align, NULL,
      NULL);

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
_fallback_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstMemory *copy;
  GstMapInfo sinfo, dinfo;
  GstAllocationParams params = { 0, 0, 0, mem->align, };

  if (!gst_memory_map (mem, &sinfo, GST_MAP_READ))
    return NULL;

  if (size == -1)
    size = sinfo.size > offset ? sinfo.size - offset : 0;

  /* use the same allocator as the memory we copy  */
  copy = gst_allocator_alloc (mem->allocator, size, &params);
  if (!gst_memory_map (copy, &dinfo, GST_MAP_WRITE)) {
    GST_CAT_WARNING (GST_CAT_MEMORY, "could not write map memory %p", copy);
    gst_memory_unmap (mem, &sinfo);
    return NULL;
  }

  memcpy (dinfo.data, sinfo.data + offset, size);
  GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy memory %p -> %p", mem, copy);
  gst_memory_unmap (copy, &dinfo);
  gst_memory_unmap (mem, &sinfo);

  return copy;
}

static gboolean
_fallback_mem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GRWLock lock;
static GHashTable *allocators;

static void
_priv_sysmem_notify (gpointer user_data)
{
  g_warning ("The default memory allocator was freed!");
}

void
_priv_gst_memory_initialize (void)
{
  static const GstMemoryInfo _mem_info = {
    GST_ALLOCATOR_SYSMEM,
    (GstAllocatorAllocFunction) _default_alloc_alloc,
    (GstMemoryMapFunction) _default_mem_map,
    (GstMemoryUnmapFunction) _default_mem_unmap,
    (GstMemoryFreeFunction) _default_mem_free,
    (GstMemoryCopyFunction) _default_mem_copy,
    (GstMemoryShareFunction) _default_mem_share,
    (GstMemoryIsSpanFunction) _default_mem_is_span,
  };

#ifndef GST_DISABLE_TRACE
  _gst_memory_trace = _gst_alloc_trace_register ("GstMemory", -1);
  _gst_allocator_trace = _gst_alloc_trace_register ("GstAllocator", -1);
#endif

  g_rw_lock_init (&lock);
  allocators = g_hash_table_new (g_str_hash, g_str_equal);

#ifdef HAVE_GETPAGESIZE
#ifdef MEMORY_ALIGNMENT_PAGESIZE
  gst_memory_alignment = getpagesize () - 1;
#endif
#endif

  GST_CAT_DEBUG (GST_CAT_MEMORY, "memory alignment: %" G_GSIZE_FORMAT,
      gst_memory_alignment);

  _default_mem_impl = gst_allocator_new (&_mem_info, NULL, _priv_sysmem_notify);

  _default_allocator = gst_allocator_ref (_default_mem_impl);
  gst_allocator_register (GST_ALLOCATOR_SYSMEM,
      gst_allocator_ref (_default_mem_impl));
}

/**
 * gst_memory_new_wrapped:
 * @flags: #GstMemoryFlags
 * @data: data to wrap
 * @maxsize: allocated size of @data
 * @offset: offset in @data
 * @size: size of valid data
 * @user_data: user_data
 * @notify: called with @user_data when the memory is freed
 *
 * Allocate a new memory block that wraps the given @data.
 *
 * The prefix/padding must be filled with 0 if @flags contains
 * #GST_MEMORY_FLAG_ZERO_PREFIXED and #GST_MEMORY_FLAG_ZERO_PADDED respectively.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_memory_new_wrapped (GstMemoryFlags flags, gpointer data,
    gsize maxsize, gsize offset, gsize size, gpointer user_data,
    GDestroyNotify notify)
{
  GstMemoryDefault *mem;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (offset + size <= maxsize, NULL);

  mem =
      _default_mem_new (flags, NULL, data, maxsize, offset, size, 0, user_data,
      notify);

#ifndef GST_DISABLE_TRACE
  _gst_alloc_trace_new (_gst_memory_trace, mem);
#endif

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

  GST_CAT_TRACE (GST_CAT_MEMORY, "memory %p, %d->%d", mem, mem->refcount,
      mem->refcount + 1);

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

  GST_CAT_TRACE (GST_CAT_MEMORY, "memory %p, %d->%d", mem, mem->refcount,
      mem->refcount - 1);

  if (g_atomic_int_dec_and_test (&mem->refcount)) {
    /* there should be no outstanding mappings */
    g_return_if_fail (g_atomic_int_get (&mem->state) < 4);
#ifndef GST_DISABLE_TRACE
    _gst_alloc_trace_free (_gst_memory_trace, mem);
#endif
    mem->allocator->info.mem_free (mem);
  }
}

/**
 * gst_memory_is_exclusive:
 * @mem: a #GstMemory
 *
 * Check if the current ref to @mem is exclusive, this means that no other
 * references exist other than @mem.
 */
gboolean
gst_memory_is_exclusive (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return (g_atomic_int_get (&mem->refcount) == 1);
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
 *
 * #GST_MEMORY_FLAG_ZERO_PREFIXED and #GST_MEMORY_FLAG_ZERO_PADDED will be
 * cleared when offset or padding is increased respectively.
 */
void
gst_memory_resize (GstMemory * mem, gssize offset, gsize size)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (offset >= 0 || mem->offset >= -offset);
  g_return_if_fail (size + mem->offset + offset <= mem->maxsize);

  /* if we increase the prefix, we can't guarantee it is still 0 filled */
  if ((offset > 0) && GST_MEMORY_IS_ZERO_PREFIXED (mem))
    GST_MEMORY_FLAG_UNSET (mem, GST_MEMORY_FLAG_ZERO_PREFIXED);

  /* if we increase the padding, we can't guarantee it is still 0 filled */
  if ((offset + size < mem->size) && GST_MEMORY_IS_ZERO_PADDED (mem))
    GST_MEMORY_FLAG_UNSET (mem, GST_MEMORY_FLAG_ZERO_PADDED);

  mem->offset += offset;
  mem->size = size;
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
    GST_CAT_DEBUG (GST_CAT_MEMORY, "lock failed %p: state %d, access_mode %d",
        mem, state, access_mode);
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
    gst_memory_unref (mem);

    if (result == NULL)
      goto cannot_copy;

    if (!gst_memory_map (result, info, flags))
      goto cannot_map;
  }
  return result;

  /* ERRORS */
cannot_copy:
  {
    GST_CAT_DEBUG (GST_CAT_MEMORY, "cannot copy memory %p", mem);
    return NULL;
  }
cannot_map:
  {
    GST_CAT_DEBUG (GST_CAT_MEMORY, "cannot map memory %p with flags %d", mem,
        flags);
    gst_memory_unref (result);
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
 * @info and its contents remain valid for as long as @mem is valid and
 * until gst_memory_unmap() is called.
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

  info->data = mem->allocator->info.mem_map (mem, mem->maxsize, flags);

  if (G_UNLIKELY (info->data == NULL))
    goto error;

  info->memory = mem;
  info->flags = flags;
  info->size = mem->size;
  info->maxsize = mem->maxsize - mem->offset;
  info->data = info->data + mem->offset;

  return TRUE;

  /* ERRORS */
lock_failed:
  {
    GST_CAT_DEBUG (GST_CAT_MEMORY, "mem %p: lock %d failed", mem, flags);
    return FALSE;
  }
error:
  {
    /* something went wrong, restore the orginal state again */
    GST_CAT_ERROR (GST_CAT_MEMORY, "mem %p: map failed", mem);
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

  mem->allocator->info.mem_unmap (mem);
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

  copy = mem->allocator->info.mem_copy (mem, offset, size);

#ifndef GST_DISABLE_TRACE
  _gst_alloc_trace_new (_gst_memory_trace, copy);
#endif

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
  GstMemory *shared;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (!GST_MEMORY_FLAG_IS_SET (mem, GST_MEMORY_FLAG_NO_SHARE),
      NULL);

  shared = mem->allocator->info.mem_share (mem, offset, size);

#ifndef GST_DISABLE_TRACE
  _gst_alloc_trace_new (_gst_memory_trace, shared);
#endif

  return shared;
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
  if (!mem1->allocator->info.mem_is_span (mem1, mem2, offset))
    return FALSE;

  return TRUE;
}

/**
 * gst_allocator_new:
 * @info: a #GstMemoryInfo
 * @user_data: user data
 * @notify: a #GDestroyNotify for @user_data
 *
 * Create a new memory allocator with @info and @user_data.
 *
 * All functions in @info are mandatory exept the copy and is_span
 * functions, which will have a default implementation when left NULL.
 *
 * The @user_data will be passed to all calls of the alloc function. @notify
 * will be called with @user_data when the allocator is freed.
 *
 * Returns: a new #GstAllocator.
 */
GstAllocator *
gst_allocator_new (const GstMemoryInfo * info, gpointer user_data,
    GDestroyNotify notify)
{
  GstAllocator *allocator;

#define INSTALL_FALLBACK(_t) \
  if (allocator->info._t == NULL) allocator->info._t = _fallback_ ##_t;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->alloc != NULL, NULL);
  g_return_val_if_fail (info->mem_map != NULL, NULL);
  g_return_val_if_fail (info->mem_unmap != NULL, NULL);
  g_return_val_if_fail (info->mem_free != NULL, NULL);
  g_return_val_if_fail (info->mem_share != NULL, NULL);

  allocator = g_slice_new (GstAllocator);
  allocator->refcount = 1;
  allocator->info = *info;
  allocator->user_data = user_data;
  allocator->notify = notify;
  INSTALL_FALLBACK (mem_copy);
  INSTALL_FALLBACK (mem_is_span);
#undef INSTALL_FALLBACK

  GST_CAT_DEBUG (GST_CAT_MEMORY, "new allocator %p", allocator);

#ifndef GST_DISABLE_TRACE
  _gst_alloc_trace_new (_gst_allocator_trace, allocator);
#endif

  return allocator;
}

/**
 * gst_allocator_get_memory_type:
 * @allocator: a #GstAllocator
 *
 * Get the memory type allocated by this allocator
 *
 * Returns: the memory type provided by @allocator
 */
const gchar *
gst_allocator_get_memory_type (GstAllocator * allocator)
{
  g_return_val_if_fail (allocator != NULL, NULL);

  return allocator->info.mem_type;
}

/**
 * gst_allocator_ref:
 * @allocator: a #GstAllocator
 *
 * Increases the refcount of @allocator.
 *
 * Returns: @allocator with increased refcount
 */
GstAllocator *
gst_allocator_ref (GstAllocator * allocator)
{
  g_return_val_if_fail (allocator != NULL, NULL);

  GST_CAT_TRACE (GST_CAT_MEMORY, "allocator %p, %d->%d", allocator,
      allocator->refcount, allocator->refcount + 1);

  g_atomic_int_inc (&allocator->refcount);

  return allocator;
}

/**
 * gst_allocator_unref:
 * @allocator: a #GstAllocator
 *
 * Decreases the refcount of @allocator. When the refcount reaches 0, the notify
 * function of @allocator will be called and the allocator will be freed.
 */
void
gst_allocator_unref (GstAllocator * allocator)
{
  g_return_if_fail (allocator != NULL);

  GST_CAT_TRACE (GST_CAT_MEMORY, "allocator %p, %d->%d", allocator,
      allocator->refcount, allocator->refcount - 1);

  if (g_atomic_int_dec_and_test (&allocator->refcount)) {
    if (allocator->notify)
      allocator->notify (allocator->user_data);
#ifndef GST_DISABLE_TRACE
    _gst_alloc_trace_free (_gst_allocator_trace, allocator);
#endif
    g_slice_free1 (sizeof (GstAllocator), allocator);
  }
}

/**
 * gst_allocator_register:
 * @name: the name of the allocator
 * @allocator: (transfer full): #GstAllocator
 *
 * Registers the memory @allocator with @name. This function takes ownership of
 * @allocator.
 */
void
gst_allocator_register (const gchar * name, GstAllocator * allocator)
{
  g_return_if_fail (name != NULL);
  g_return_if_fail (allocator != NULL);

  GST_CAT_DEBUG (GST_CAT_MEMORY, "registering allocator %p with name \"%s\"",
      allocator, name);

  g_rw_lock_writer_lock (&lock);
  g_hash_table_insert (allocators, (gpointer) name, (gpointer) allocator);
  g_rw_lock_writer_unlock (&lock);
}

/**
 * gst_allocator_find:
 * @name: the name of the allocator
 *
 * Find a previously registered allocator with @name. When @name is NULL, the
 * default allocator will be returned.
 *
 * Returns: (transfer full): a #GstAllocator or NULL when the allocator with @name was not
 * registered. Use gst_allocator_unref() to release the allocator after usage.
 */
GstAllocator *
gst_allocator_find (const gchar * name)
{
  GstAllocator *allocator;

  g_rw_lock_reader_lock (&lock);
  if (name) {
    allocator = g_hash_table_lookup (allocators, (gconstpointer) name);
  } else {
    allocator = _default_allocator;
  }
  if (allocator)
    gst_allocator_ref (allocator);
  g_rw_lock_reader_unlock (&lock);

  return allocator;
}

/**
 * gst_allocator_set_default:
 * @allocator: (transfer full): a #GstAllocator
 *
 * Set the default allocator. This function takes ownership of @allocator.
 */
void
gst_allocator_set_default (GstAllocator * allocator)
{
  GstAllocator *old;
  g_return_if_fail (allocator != NULL);

  g_rw_lock_writer_lock (&lock);
  old = _default_allocator;
  _default_allocator = allocator;
  g_rw_lock_writer_unlock (&lock);

  if (old)
    gst_allocator_unref (old);
}

/**
 * gst_allocation_params_init:
 * @params: a #GstAllocationParams
 *
 * Initialize @params to its default values
 */
void
gst_allocation_params_init (GstAllocationParams * params)
{
  g_return_if_fail (params != NULL);

  memset (params, 0, sizeof (GstAllocationParams));
}

/**
 * gst_allocation_params_copy:
 * @params: (transfer none): a #GstAllocationParams
 *
 * Create a copy of @params.
 *
 * Free-function: gst_allocation_params_free
 *
 * Returns: (transfer full): a new ##GstAllocationParams, free with
 * gst_allocation_params_free().
 */
GstAllocationParams *
gst_allocation_params_copy (const GstAllocationParams * params)
{
  GstAllocationParams *result = NULL;

  if (params) {
    result =
        (GstAllocationParams *) g_slice_copy (sizeof (GstAllocationParams),
        params);
  }
  return result;
}

/**
 * gst_allocation_params_free:
 * @params: (in) (transfer full): a #GstAllocationParams
 *
 * Free @params
 */
void
gst_allocation_params_free (GstAllocationParams * params)
{
  g_slice_free (GstAllocationParams, params);
}

/**
 * gst_allocator_alloc:
 * @allocator: (transfer none) (allow-none): a #GstAllocator to use
 * @size: size of the visible memory area
 * @params: (transfer none) (allow-none): optional parameters
 *
 * Use @allocator to allocate a new memory block with memory that is at least
 * @size big.
 *
 * The optional @params can specify the prefix and padding for the memory. If
 * NULL is passed, no flags, no extra prefix/padding and a default alignment is
 * used.
 *
 * The prefix/padding will be filled with 0 if flags contains
 * #GST_MEMORY_FLAG_ZERO_PREFIXED and #GST_MEMORY_FLAG_ZERO_PADDED respectively.
 *
 * When @allocator is NULL, the default allocator will be used.
 *
 * The alignment in @params is given as a bitmask so that @align + 1 equals
 * the amount of bytes to align to. For example, to align to 8 bytes,
 * use an alignment of 7.
 *
 * Returns: (transfer full): a new #GstMemory.
 */
GstMemory *
gst_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstMemory *mem;
  static GstAllocationParams defparams = { 0, 0, 0, 0, };

  if (params) {
    g_return_val_if_fail (((params->align + 1) & params->align) == 0, NULL);
  } else {
    params = &defparams;
  }

  if (allocator == NULL)
    allocator = _default_allocator;

  mem = allocator->info.alloc (allocator, size, params, allocator->user_data);

#ifndef GST_DISABLE_TRACE
  _gst_alloc_trace_new (_gst_memory_trace, mem);
#endif
  return mem;
}
