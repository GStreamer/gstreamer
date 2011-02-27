/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.be>
 *
 * gstmeta.h: Header for Metadata structures
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


#ifndef __GST_META_H__
#define __GST_META_H__

G_BEGIN_DECLS

typedef struct _GstMeta GstMeta;
typedef struct _GstMetaInfo GstMetaInfo;

/**
 * GstMeta:
 * @info: pointer to the #GstMetaInfo
 *
 * Base structure for metadata. Custom metadata will put this structure
 * as the first member of their structure.
 */
struct _GstMeta {
  const GstMetaInfo *info;
};

/**
 * GST_META_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_META_TRACE_NAME           "GstMeta"

/**
 * GstMetaInitFunction:
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when @meta is initialized in @buffer.
 */
typedef gboolean (*GstMetaInitFunction) (GstMeta *meta, gpointer params, GstBuffer *buffer);

/**
 * GstMetaFreeFunction:
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when @meta is freed in @buffer.
 */
typedef void (*GstMetaFreeFunction)     (GstMeta *meta, GstBuffer *buffer);

/**
 * GstMetaCopyFunction:
 * @copy: a #GstBuffer
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when a copy of @buffer is made and @meta should be copied to
 * @copy.
 */
typedef void (*GstMetaCopyFunction)     (GstBuffer *copy, GstMeta *meta,
                                         const GstBuffer *buffer);
/**
 * GstMetaSubFunction:
 * @subbuf: a #GstBuffer
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 * @offset: subbuffer offset
 * @size: subbuffer size
 *
 * Function called for each @meta in @buffer as a result from creating a
 * subbuffer @subbuf from @buffer at @offset and with @size. An
 * implementation could decide to copy and update the metadata on @subbuf.
 */
typedef void (*GstMetaSubFunction)      (GstBuffer *subbuf, GstMeta *meta,
                                         GstBuffer *buffer, guint offset, guint size);

/**
 * GstMetaSerializeFunction:
 * @meta: a #GstMeta
 */
typedef gchar * (*GstMetaSerializeFunction) (GstMeta *meta);

/**
 * GstMetaDeserializeFunction:
 * @meta: a #GstMeta
 */
typedef gboolean (*GstMetaDeserializeFunction) (GstMeta *meta,
                                                const gchar *s);

/**
 * GstMetaInfo:
 * @api: tag indentifying the metadata structure and api
 * @impl: tag indentifying the implementor of the api
 * @size: size of the metadata
 * @init_func: function for initializing the metadata
 * @free_func: function for freeing the metadata
 * @copy_func: function for copying the metadata
 * @sub_func: function for when a subbuffer is taken
 * @serialize_func: function for serializing
 * @deserialize_func: function for deserializing
 *
 * The #GstMetaInfo provides information about a specific metadata
 * structure.
 */
struct _GstMetaInfo {
  GQuark                     api;
  GQuark                     impl;
  gsize                      size;

  GstMetaInitFunction        init_func;
  GstMetaFreeFunction        free_func;
  GstMetaCopyFunction        copy_func;
  GstMetaSubFunction         sub_func;
  GstMetaSerializeFunction   serialize_func;
  GstMetaDeserializeFunction deserialize_func;
};

void _gst_meta_init (void);

const GstMetaInfo *  gst_meta_register        (const gchar *api, const gchar *impl,
                                               gsize size,
                                               GstMetaInitFunction        init_func,
                                               GstMetaFreeFunction        free_func,
                                               GstMetaCopyFunction        copy_func,
                                               GstMetaSubFunction         sub_func,
                                               GstMetaSerializeFunction   serialize_func,
                                               GstMetaDeserializeFunction deserialize_func);
const GstMetaInfo *  gst_meta_get_info        (const gchar * impl);

/* default metadata */

typedef struct _GstMetaMemory GstMetaMemory;

const GstMetaInfo *gst_meta_memory_get_info(void);
#define GST_META_MEMORY_INFO (gst_meta_memory_get_info())

typedef enum {
  GST_META_MAP_NONE  = 0,
  GST_META_MAP_READ  = (1 << 0),
  GST_META_MAP_WRITE = (1 << 1)
} GstMetaMapFlags;

struct _GstMetaMemory
{
  GstMeta      meta;

  gpointer   (*mmap_func)      (GstMetaMemory *meta, gsize offset, gsize *size,
                                GstMetaMapFlags flags);
  gboolean   (*munmap_func)    (GstMetaMemory *meta, gpointer data, gsize size);
};

#define gst_meta_memory_map(m,o,s,f)   ((m)->mmap_func(m, o, s, f))
#define gst_meta_memory_unmap(m,d,s)   ((m)->munmap_func(m, d, s))

#define gst_buffer_get_meta_memory(b)  ((GstMetaMemory*)gst_buffer_get_meta((b),GST_META_MEMORY_INFO))

GstMetaMemory * gst_buffer_add_meta_memory (GstBuffer *buffer, gpointer data,
                                            GFreeFunc free_func,
                                            gsize size, gsize offset);

G_END_DECLS

#endif /* __GST_META_H__ */
