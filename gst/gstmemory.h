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

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstMemory GstMemory;
typedef struct _GstMemoryInfo GstMemoryInfo;
typedef struct _GstMemoryImpl GstMemoryImpl;

typedef enum {
  GST_MEMORY_FLAG_READONLY = (1 << 0),
  GST_MEMORY_FLAG_MUTABLE  = (1 << 1),
} GstMemoryFlags;

/**
 * GstMemory:
 * @impl: pointer to the #GstMemoryImpl
 * @refcount: refcount
 * @paret: parent memory block
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

typedef enum {
  GST_MAP_READ,
  GST_MAP_WRITE,
} GstMapFlags;

/**
 * GST_MEMORY_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_MEMORY_TRACE_NAME           "GstMemory"

typedef gsize (*GstMemoryGetSizesFunction)  (GstMemory *mem, gsize *maxsize);

typedef gpointer (*GstMemoryMapFunction)    (GstMemory *mem, gsize *size, gsize *maxsize,
                                             GstMapFlags flags);
typedef gboolean (*GstMemoryUnmapFunction)  (GstMemory *mem, gpointer data, gsize size);

typedef void        (*GstMemoryFreeFunction)      (GstMemory *mem);
typedef GstMemory * (*GstMemoryCopyFunction)      (GstMemory *mem);
typedef void        (*GstMemoryExtractFunction)   (GstMemory *mem, gsize offset,
                                                   gpointer dest, gsize size);
typedef void        (*GstMemoryTrimFunction)  (GstMemory *mem, gsize offset, gsize size);
typedef GstMemory * (*GstMemorySubFunction)   (GstMemory *mem, gsize offset, gsize size);
typedef gboolean    (*GstMemoryIsSpanFunction) (GstMemory *mem1, GstMemory *mem2);
typedef GstMemory * (*GstMemorySpanFunction)  (GstMemory *mem1, gsize offset,
                                               GstMemory *mem2, gsize size);

/**
 * GstMemoryInfo:
 * @get_sizes:
 *
 * The #GstMemoryInfo is used to register new memory implementations.
 */
struct _GstMemoryInfo {
  GstMemoryGetSizesFunction get_sizes;
  GstMemoryTrimFunction     trim;
  GstMemoryMapFunction      map;
  GstMemoryUnmapFunction    unmap;
  GstMemoryFreeFunction     free;

  GstMemoryCopyFunction     copy;
  GstMemoryExtractFunction  extract;
  GstMemorySubFunction      sub;
  GstMemoryIsSpanFunction   is_span;
  GstMemorySpanFunction     span;
};

void _gst_memory_init (void);

GstMemory * gst_memory_ref        (GstMemory *mem);
void        gst_memory_unref      (GstMemory *mem);

gsize       gst_memory_get_sizes  (GstMemory *mem, gsize *maxsize);
void        gst_memory_trim       (GstMemory *mem, gsize offset, gsize size);

gpointer    gst_memory_map        (GstMemory *mem, gsize *size, gsize *maxsize,
                                   GstMapFlags flags);
gboolean    gst_memory_unmap      (GstMemory *mem, gpointer data, gsize size);

GstMemory * gst_memory_copy       (GstMemory *mem);
void        gst_memory_extract    (GstMemory *mem, gsize offset, gpointer dest,
                                   gsize size);
GstMemory * gst_memory_sub        (GstMemory *mem, gsize offset, gsize size);

gboolean    gst_memory_is_span    (GstMemory *mem1, GstMemory *mem2);
GstMemory * gst_memory_span       (GstMemory *mem1, gsize offset,
                                   GstMemory *mem2, gsize size);


GstMemory * gst_memory_new_wrapped (gpointer data, GFreeFunc free_func,
                                    gsize maxsize, gsize offset, gsize size);
GstMemory * gst_memory_new_alloc   (gsize maxsize, gsize align);


const GstMemoryImpl *  gst_memory_register    (const gchar *impl, const GstMemoryInfo *info);

#if 0
const GstMemoryInfo *  gst_memory_get_info    (const gchar * impl);
#endif

G_END_DECLS

#endif /* __GST_MEMORY_H__ */
