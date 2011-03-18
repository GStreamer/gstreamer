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

G_BEGIN_DECLS

typedef struct _GstMemory GstMemory;
typedef struct _GstMemoryInfo GstMemoryInfo;

/**
 * GstMemory:
 * @info: pointer to the #GstMemoryInfo
 *
 * Base structure for memory implementations. Custom memory will put this structure
 * as the first member of their structure.
 */
struct _GstMemory {
  const GstMemoryInfo *info;

  gint refcount;
};

/**
 * GST_MEMORY_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_MEMORY_TRACE_NAME           "GstMemory"

typedef gsize (*GstMemoryGetSizesFunction)  (GstMemory *mem, gsize *maxsize);

typedef void  (*GstMemorySetSizeFunction)   (GstMemory *mem, gsize size);
typedef gpointer (*GstMemoryMapFunction)    (GstMemory *mem, gsize offset, gsize *size,
                                             GstMapFlags flags);
typedef gboolean (*GstMemoryUnmapFunction)  (GstMemory *mem, gpointer data, gsize size);

typedef GstMemory * (*GstMemoryCopyFunction)  (GstMemory *mem);

/**
 * GstMemoryInfo:
 * @impl: tag indentifying the implementor of the api
 * @size: size of the memory structure
 *
 * The #GstMemoryInfo provides information about a specific metadata
 * structure.
 */
struct _GstMemoryInfo {
  GQuark                    impl;
  gsize                     size;

  GstMemoryGetSizesFunction get_sizes;
  GstMemorySetSizeFunction  set_size;
  GstMemoryMapFunction      map;
  GstMemoryUnmapFunction    unmap;
  GstMemoryCopyFunction     copy;
};

void _gst_memory_init (void);

GstMemory * gst_memory_ref        (GstMemory *mem);
void        gst_memory_unref      (GstMemory *mem);

gsize       gst_memory_get_sizes  (GstMemory *mem, gsize *maxsize);
void        gst_memory_set_size   (GstMemory *mem, gsize size);

gpointer    gst_memory_map        (GstMemory *mem, gsize *size, gsize *maxsize,
                                   GstMapFlags flags);
gboolean    gst_memory_unmap      (GstMemory *mem, gpointer data, gsize size);

GstMemory * gst_memory_copy       (GstMemory *mem);


GstMemory * gst_memory_new_wrapped (gpointer data, GFreeFunc free_func,
                                    gsize maxsize, gsize offset, gsize size);
GstMemory * gst_memory_new_alloc   (gsize maxsize, gsize align);


#if 0
const GstMemoryInfo *  gst_memory_register    (const gchar *impl, gsize size,
                                               GstMemoryGetSizesFunction get_sizes,
                                               GstMemorySetSizeFunction  set_size,
                                               GstMemoryMapFunction      map,
                                               GstMemoryUnmapFunction    unmap);
const GstMemoryInfo *  gst_memory_get_info    (const gchar * impl);
#endif

G_END_DECLS

#endif /* __GST_MEMORY_H__ */
