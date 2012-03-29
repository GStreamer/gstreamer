/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.be>
 *
 * gstmemory.h: Header for memory blocks
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


#ifndef __GST_MEMORY_H__
#define __GST_MEMORY_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_MEMORY (gst_memory_get_type())
GType gst_memory_get_type(void);

#define GST_TYPE_ALLOCATOR (gst_allocator_get_type())
GType gst_allocator_get_type(void);

#define GST_TYPE_ALLOCATION_PARAMS (gst_allocation_params_get_type())
GType gst_allocation_params_get_type(void);

typedef struct _GstMemory GstMemory;
typedef struct _GstMemoryInfo GstMemoryInfo;
typedef struct _GstAllocator GstAllocator;
typedef struct _GstAllocationParams GstAllocationParams;

/**
 * gst_memory_alignment:
 *
 * The default memory alignment in bytes - 1
 * an alignment of 7 would be the same as what malloc() guarantees.
 */
GST_EXPORT gsize gst_memory_alignment;

#define GST_MEMORY_CAST(mem)   ((GstMemory *)(mem))

/**
 * GstMemoryFlags:
 * @GST_MEMORY_FLAG_READONLY: memory is readonly. It is not allowed to map the
 * memory with #GST_MAP_WRITE.
 * @GST_MEMORY_FLAG_NO_SHARE: memory must not be shared. Copies will have to be
 * made when this memory needs to be shared between buffers.
 * @GST_MEMORY_FLAG_ZERO_PREFIXED: the memory prefix is filled with 0 bytes
 * @GST_MEMORY_FLAG_ZERO_PADDED: the memory padding is filled with 0 bytes
 * @GST_MEMORY_FLAG_LAST: first flag that can be used for custom purposes
 *
 * Flags for wrapped memory.
 */
typedef enum {
  GST_MEMORY_FLAG_READONLY      = (1 << 0),
  GST_MEMORY_FLAG_NO_SHARE      = (1 << 1),
  GST_MEMORY_FLAG_ZERO_PREFIXED = (1 << 2),
  GST_MEMORY_FLAG_ZERO_PADDED   = (1 << 3),

  GST_MEMORY_FLAG_LAST          = (1 << 16)
} GstMemoryFlags;

/**
 * GST_MEMORY_FLAGS:
 * @mem: a #GstMemory.
 *
 * A flags word containing #GstMemoryFlags flags set on @mem
 */
#define GST_MEMORY_FLAGS(mem)  (GST_MEMORY_CAST (mem)->flags)
/**
 * GST_MEMORY_FLAG_IS_SET:
 * @mem: a #GstMemory.
 * @flag: the #GstMemoryFlags to check.
 *
 * Gives the status of a specific flag on a @mem.
 */
#define GST_MEMORY_FLAG_IS_SET(mem,flag)   !!(GST_MEMORY_FLAGS (mem) & (flag))
/**
 * GST_MEMORY_FLAG_UNSET:
 * @mem: a #GstMemory.
 * @flag: the #GstMemoryFlags to clear.
 *
 * Clear a specific flag on a @mem.
 */
#define GST_MEMORY_FLAG_UNSET(mem,flag)   (GST_MEMORY_FLAGS (mem) &= ~(flag))

/**
 * GST_MEMORY_IS_READONLY:
 * @mem: a #GstMemory.
 *
 * Check if @mem is readonly.
 */
#define GST_MEMORY_IS_READONLY(mem)        GST_MEMORY_FLAG_IS_SET(mem,GST_MEMORY_FLAG_READONLY)
/**
 * GST_MEMORY_IS_ZERO_PREFIXED:
 * @mem: a #GstMemory.
 *
 * Check if the prefix in @mem is 0 filled.
 */
#define GST_MEMORY_IS_ZERO_PREFIXED(mem)   GST_MEMORY_FLAG_IS_SET(mem,GST_MEMORY_FLAG_ZERO_PREFIXED)
/**
 * GST_MEMORY_IS_ZERO_PADDED:
 * @mem: a #GstMemory.
 *
 * Check if the padding in @mem is 0 filled.
 */
#define GST_MEMORY_IS_ZERO_PADDED(mem)     GST_MEMORY_FLAG_IS_SET(mem,GST_MEMORY_FLAG_ZERO_PADDED)


/**
 * GstMemory:
 * @allocator: pointer to the #GstAllocator
 * @flags: memory flags
 * @refcount: refcount
 * @parent: parent memory block
 * @state: private state
 * @maxsize: the maximum size allocated
 * @align: the alignment of the memory
 * @offset: the offset where valid data starts
 * @size: the size of valid data
 *
 * Base structure for memory implementations. Custom memory will put this structure
 * as the first member of their structure.
 */
struct _GstMemory {
  GstAllocator   *allocator;

  GstMemoryFlags  flags;
  gint            refcount;
  GstMemory      *parent;
  volatile gint   state;
  gsize           maxsize;
  gsize           align;
  gsize           offset;
  gsize           size;
};

/**
 * GstMapFlags:
 * @GST_MAP_READ: map for read access
 * @GST_MAP_WRITE: map for write access
 * @GST_MAP_FLAG_LAST: first flag that can be used for custom purposes
 *
 * Flags used when mapping memory
 */
typedef enum {
  GST_MAP_READ      = (1 << 0),
  GST_MAP_WRITE     = (1 << 1),

  GST_MAP_FLAG_LAST = (1 << 16)
} GstMapFlags;

/**
 * GstMapInfo:
 * @memory: a pointer to the mapped memory
 * @flags: flags used when mapping the memory
 * @data: a pointer to the mapped data
 * @size: the valid size in @data
 * @maxsize: the maximum bytes in @data
 * @user_data: extra private user_data that the implementation of the memory
 *             can use to store extra info.
 *
 * A structure containing the result of a map operation such as
 * gst_memory_map(). It contains the data and size.
 */
typedef struct {
  GstMemory *memory;
  GstMapFlags flags;
  guint8 *data;
  gsize size;
  gsize maxsize;
  /*< private >*/
  gpointer user_data[4];
} GstMapInfo;

/**
 * GST_MAP_INFO_INIT:
 *
 * Initializer for #GstMapInfo
 */
#define GST_MAP_INFO_INIT { NULL, 0, NULL, 0, 0, }

/**
 * GST_MAP_READWRITE:
 *
 * Map for readwrite access
 */
#define GST_MAP_READWRITE      (GST_MAP_READ | GST_MAP_WRITE)

/**
 * GST_ALLOCATOR_SYSMEM:
 *
 * The allocator name for the default system memory allocator
 */
#define GST_ALLOCATOR_SYSMEM   "SystemMemory"

/**
 * GstAllocationParams:
 * @flags: flags to control allocation
 * @align: the desired alignment of the memory
 * @prefix: the disired prefix
 * @padding: the desired padding
 *
 * Parameters to control the allocation of memory
 */
struct _GstAllocationParams {
  GstMemoryFlags flags;
  gsize          align;
  gsize          prefix;
  gsize          padding;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstAllocatorAllocFunction:
 * @allocator: a #GstAllocator
 * @size: the size
 * @params: allocator params
 * @user_data: user data
 *
 * Allocate a new #GstMemory from @allocator that can hold at least @size
 * bytes (+ padding) and is aligned to (@align + 1) bytes.
 *
 * The offset and size of the memory should be set and the prefix/padding must
 * be filled with 0 if @params flags contains #GST_MEMORY_FLAG_ZERO_PREFIXED and
 * #GST_MEMORY_FLAG_ZERO_PADDED respectively.
 *
 * @user_data is the data that was used when creating @allocator.
 *
 * Returns: a newly allocated #GstMemory. Free with gst_memory_unref()
 */
typedef GstMemory *  (*GstAllocatorAllocFunction)  (GstAllocator *allocator,
                                                    gsize size, GstAllocationParams *params,
                                                    gpointer user_data);

/**
 * GstMemoryMapFunction:
 * @mem: a #GstMemory
 * @maxsize: size to map
 * @flags: access mode for the memory
 *
 * Get the memory of @mem that can be accessed according to the mode specified
 * in @flags. The function should return a pointer that contains at least
 * @maxsize bytes.
 *
 * Returns: a pointer to memory of which at least @maxsize bytes can be
 * accessed according to the access pattern in @flags.
 */
typedef gpointer    (*GstMemoryMapFunction)       (GstMemory *mem, gsize maxsize, GstMapFlags flags);

/**
 * GstMemoryUnmapFunction:
 * @mem: a #GstMemory
 *
 * Return the pointer previously retrieved with gst_memory_map().
 *
 * Returns: %TRUE on success.
 */
typedef void        (*GstMemoryUnmapFunction)     (GstMemory *mem);

/**
 * GstMemoryFreeFunction:
 * @mem: a #GstMemory
 *
 * Free the memory used by @mem. This function is usually called when the
 * refcount of the @mem has reached 0.
 */
typedef void        (*GstMemoryFreeFunction)      (GstMemory *mem);

/**
 * GstMemoryCopyFunction:
 * @mem: a #GstMemory
 * @offset: an offset
 * @size: a size or -1
 *
 * Copy @size bytes from @mem starting at @offset and return them wrapped in a
 * new GstMemory object.
 * If @size is set to -1, all bytes starting at @offset are copied.
 *
 * Returns: a new #GstMemory object wrapping a copy of the requested region in
 * @mem.
 */
typedef GstMemory * (*GstMemoryCopyFunction)      (GstMemory *mem, gssize offset, gssize size);

/**
 * GstMemoryShareFunction:
 * @mem: a #GstMemory
 * @offset: an offset
 * @size: a size or -1
 *
 * Share @size bytes from @mem starting at @offset and return them wrapped in a
 * new GstMemory object. If @size is set to -1, all bytes starting at @offset are
 * shared. This function does not make a copy of the bytes in @mem.
 *
 * Returns: a new #GstMemory object sharing the requested region in @mem.
 */
typedef GstMemory * (*GstMemoryShareFunction)     (GstMemory *mem, gssize offset, gssize size);

/**
 * GstMemoryIsSpanFunction:
 * @mem1: a #GstMemory
 * @mem2: a #GstMemory
 * @offset: a result offset
 *
 * Check if @mem1 and @mem2 occupy contiguous memory and return the offset of
 * @mem1 in the parent buffer in @offset.
 *
 * Returns: %TRUE if @mem1 and @mem2 are in contiguous memory.
 */
typedef gboolean    (*GstMemoryIsSpanFunction)    (GstMemory *mem1, GstMemory *mem2, gsize *offset);

/**
 * GstMemoryInfo:
 * @mem_type: the memory type this allocator provides
 * @alloc: the implementation of the GstAllocatorAllocFunction
 * @mem_map: the implementation of the GstMemoryMapFunction
 * @mem_unmap: the implementation of the GstMemoryUnmapFunction
 * @mem_free: the implementation of the GstMemoryFreeFunction
 * @mem_copy: the implementation of the GstMemoryCopyFunction
 * @mem_share: the implementation of the GstMemoryShareFunction
 * @mem_is_span: the implementation of the GstMemoryIsSpanFunction
 *
 * The #GstMemoryInfo is used to register new memory allocators and contain
 * the implementations for various memory operations.
 */
struct _GstMemoryInfo {
  const gchar              *mem_type;

  GstAllocatorAllocFunction alloc;

  GstMemoryMapFunction      mem_map;
  GstMemoryUnmapFunction    mem_unmap;
  GstMemoryFreeFunction     mem_free;

  GstMemoryCopyFunction     mem_copy;
  GstMemoryShareFunction    mem_share;
  GstMemoryIsSpanFunction   mem_is_span;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstAllocator:
 *
 * An opaque type returned from gst_allocator_new() or gst_allocator_find()
 * that can be used to allocator memory.
 */

/* allocators */
GstAllocator * gst_allocator_new             (const GstMemoryInfo * info,
                                              gpointer user_data, GDestroyNotify notify);
const gchar *  gst_allocator_get_memory_type (GstAllocator * allocator);

GstAllocator * gst_allocator_ref             (GstAllocator * allocator);
void           gst_allocator_unref           (GstAllocator * allocator);

void           gst_allocator_register        (const gchar *name, GstAllocator *allocator);
GstAllocator * gst_allocator_find            (const gchar *name);

void           gst_allocator_set_default     (GstAllocator * allocator);

/* allocating memory blocks */
void           gst_allocation_params_init     (GstAllocationParams *params);
GstAllocationParams *
               gst_allocation_params_copy     (const GstAllocationParams *params) G_GNUC_MALLOC;
void           gst_allocation_params_free     (GstAllocationParams *params);

GstMemory *    gst_allocator_alloc           (GstAllocator * allocator, gsize size,
                                              GstAllocationParams *params);

GstMemory *    gst_memory_new_wrapped  (GstMemoryFlags flags, gpointer data, gsize maxsize,
                                        gsize offset, gsize size, gpointer user_data,
                                        GDestroyNotify notify);

/* refcounting */
GstMemory *    gst_memory_ref          (GstMemory *mem);
void           gst_memory_unref        (GstMemory *mem);

gboolean       gst_memory_is_exclusive (GstMemory *mem);

/* getting/setting memory properties */
gsize          gst_memory_get_sizes    (GstMemory *mem, gsize *offset, gsize *maxsize);
void           gst_memory_resize       (GstMemory *mem, gssize offset, gsize size);

/* retrieving data */
GstMemory *    gst_memory_make_mapped  (GstMemory *mem, GstMapInfo *info, GstMapFlags flags);
gboolean       gst_memory_map          (GstMemory *mem, GstMapInfo *info, GstMapFlags flags);
void           gst_memory_unmap        (GstMemory *mem, GstMapInfo *info);

/* copy and subregions */
GstMemory *    gst_memory_copy         (GstMemory *mem, gssize offset, gssize size);
GstMemory *    gst_memory_share        (GstMemory *mem, gssize offset, gssize size);

/* span memory */
gboolean       gst_memory_is_span      (GstMemory *mem1, GstMemory *mem2, gsize *offset);

G_END_DECLS

#endif /* __GST_MEMORY_H__ */
