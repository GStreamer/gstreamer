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

typedef struct _GstMemory GstMemory;
typedef struct _GstMemoryInfo GstMemoryInfo;
typedef struct _GstMemoryImpl GstMemoryImpl;

/**
 * GstMemoryFlags:
 * @GST_MEMORY_FLAG_READONLY: memory is readonly. It is not allowed to map the
 * memory with #GST_MAP_WRITE.
 * @GST_MEMORY_FLAG_NO_SHARE: memory must not be shared. Copies will have to be
 * made when this memory needs to be shared between buffers.
 * @GST_MEMORY_FLAG_LAST: first flag that can be used for custom purposes
 *
 * Flags for wrapped memory.
 */
typedef enum {
  GST_MEMORY_FLAG_READONLY = (1 << 0),
  GST_MEMORY_FLAG_NO_SHARE = (1 << 1),

  GST_MEMORY_FLAG_LAST = (1 << 24)
} GstMemoryFlags;

/**
 * GST_MEMORY_IS_WRITABLE:
 * @mem: a #GstMemory
 *
 * Check if @mem is writable.
 */
#define GST_MEMORY_IS_WRITABLE(mem) (((mem)->refcount == 1) && \
    (((mem)->parent == NULL) || ((mem)->parent->refcount == 1)) && \
    (((mem)->flags & GST_MEMORY_FLAG_READONLY) == 0))

/**
 * GstMemory:
 * @impl: pointer to the #GstMemoryImpl
 * @flags: memory flags
 * @refcount: refcount
 * @parent: parent memory block
 *
 * Base structure for memory implementations. Custom memory will put this structure
 * as the first member of their structure.
 */
struct _GstMemory {
  const GstMemoryImpl *impl;

  GstMemoryFlags  flags;
  gint            refcount;
  GstMemory      *parent;
};

/**
 * GstMapFlags:
 * @GST_MAP_READ: map for read access
 * @GST_MAP_WRITE: map for write access
 *
 * Flags used when mapping memory
 */
typedef enum {
  GST_MAP_READ =  (1 << 0),
  GST_MAP_WRITE = (1 << 1),
} GstMapFlags;

/**
 * GST_MAP_READWRITE:
 *
 * Map for readwrite access
 */
#define GST_MAP_READWRITE      (GST_MAP_READ | GST_MAP_WRITE)

/**
 * GST_MEMORY_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_MEMORY_TRACE_NAME           "GstMemory"

/**
 * GstMemoryGetSizesFunction:
 * @mem: a #GstMemory
 * @maxsize: result pointer for maxsize
 *
 * Retrieve the size and maxsize of @mem.
 *
 * Returns: the size of @mem and the maximum allocated size in @maxsize.
 */
typedef gsize       (*GstMemoryGetSizesFunction)  (GstMemory *mem, gsize *maxsize);

/**
 * GstMemoryResizeFunction:
 * @mem: a #GstMemory
 * @offset: the new offset
 * @size: the new size
 *
 * Adjust the size and offset of @mem. @offset bytes will be skipped from the
 * current first byte in @mem as retrieved with gst_memory_map() and the new
 * size will be set to @size.
 *
 * @size can be set to -1, which will only adjust the offset.
 */
typedef void        (*GstMemoryResizeFunction)    (GstMemory *mem, gsize offset, gsize size);

/**
 * GstMemoryMapFunction:
 * @mem: a #GstMemory
 * @size: pointer for the size
 * @maxsize: pointer for the maxsize
 * @flags: access mode for the memory
 *
 * Get the memory of @mem that can be accessed according to the mode specified
 * in @flags. @size and @maxsize will respectively contain the current amount of
 * valid bytes in the returned memory and the maximum allocated memory.
 * @size and @maxsize can optionally be set to NULL.
 *
 * Returns: a pointer to memory. @size bytes are currently used from the
 * returned pointer and @maxsize bytes can potentially be used.
 */
typedef gpointer    (*GstMemoryMapFunction)       (GstMemory *mem, gsize *size, gsize *maxsize,
                                                   GstMapFlags flags);

/**
 * GstMemoryUnmapFunction:
 * @mem: a #GstMemory
 * @data: the data pointer
 * @size: the new size
 *
 * Return the pointer previously retrieved with gst_memory_map() and adjust the
 * size of the memory with @size. @size can optionally be set to -1 to not
 * modify the size.
 *
 * Returns: %TRUE on success.
 */
typedef gboolean    (*GstMemoryUnmapFunction)     (GstMemory *mem, gpointer data, gsize size);

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
 * @size: a size
 *
 * Copy @size bytes from @mem starting at @offset and return them wrapped in a
 * new GstMemory object.
 * If @size is set to -1, all bytes starting at @offset are copied.
 *
 * Returns: a new #GstMemory object wrapping a copy of the requested region in
 * @mem.
 */
typedef GstMemory * (*GstMemoryCopyFunction)      (GstMemory *mem, gsize offset, gsize size);

/**
 * GstMemoryShareFunction:
 * @mem: a #GstMemory
 * @offset: an offset
 * @size: a size
 *
 * Share @size bytes from @mem starting at @offset and return them wrapped in a
 * new GstMemory object. If @size is set to -1, all bytes starting at @offset are
 * shared. This function does not make a copy of the bytes in @mem.
 *
 * Returns: a new #GstMemory object sharing the requested region in @mem.
 */
typedef GstMemory * (*GstMemoryShareFunction)     (GstMemory *mem, gsize offset, gsize size);

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
 * @get_sizes: the implementation of the GstMemoryGetSizesFunction
 * @resize: the implementation of the GstMemoryResizeFunction
 * @map: the implementation of the GstMemoryMapFunction
 * @unmap: the implementation of the GstMemoryUnmapFunction
 * @free: the implementation of the GstMemoryFreeFunction
 * @copy: the implementation of the GstMemoryCopyFunction
 * @share: the implementation of the GstMemoryShareFunction
 * @is_span: the implementation of the GstMemoryIsSpanFunction
 *
 * The #GstMemoryInfo is used to register new memory implementations and contain
 * the implementations for various memory operations.
 */
struct _GstMemoryInfo {
  GstMemoryGetSizesFunction get_sizes;
  GstMemoryResizeFunction   resize;
  GstMemoryMapFunction      map;
  GstMemoryUnmapFunction    unmap;
  GstMemoryFreeFunction     free;

  GstMemoryCopyFunction     copy;
  GstMemoryShareFunction    share;
  GstMemoryIsSpanFunction   is_span;
};

void _gst_memory_init (void);

/* allocating memory blocks */
GstMemory * gst_memory_new_wrapped (GstMemoryFlags flags, gpointer data, GFreeFunc free_func,
                                    gsize maxsize, gsize offset, gsize size);
GstMemory * gst_memory_new_alloc   (gsize maxsize, gsize align);

/* refcounting */
GstMemory * gst_memory_ref        (GstMemory *mem);
void        gst_memory_unref      (GstMemory *mem);

/* getting/setting memory properties */
gsize       gst_memory_get_sizes  (GstMemory *mem, gsize *maxsize);
void        gst_memory_resize     (GstMemory *mem, gsize offset, gsize size);

/* retriveing data */
gpointer    gst_memory_map        (GstMemory *mem, gsize *size, gsize *maxsize,
                                   GstMapFlags flags);
gboolean    gst_memory_unmap      (GstMemory *mem, gpointer data, gsize size);

/* copy and subregions */
GstMemory * gst_memory_copy       (GstMemory *mem, gsize offset, gsize size);
GstMemory * gst_memory_share      (GstMemory *mem, gsize offset, gsize size);

/* span memory */
gboolean    gst_memory_is_span    (GstMemory *mem1, GstMemory *mem2, gsize *offset);

GstMemory * gst_memory_span       (GstMemory **mem1, gsize len1, gsize offset,
                                   GstMemory **mem2, gsize len2, gsize size);


const GstMemoryImpl *  gst_memory_register    (const gchar *name, const GstMemoryInfo *info);

#if 0
const GstMemoryInfo *  gst_memory_get_info    (const gchar * impl);
#endif

G_END_DECLS

#endif /* __GST_MEMORY_H__ */
