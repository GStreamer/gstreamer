/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.be>
 *
 * gstbuffermeta.h: Header for Buffer Metadata structures
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


#ifndef __GST_BUFFER_META_H__
#define __GST_BUFFER_META_H__

G_BEGIN_DECLS

typedef struct _GstBufferMeta GstBufferMeta;
typedef struct _GstBufferMetaInfo GstBufferMetaInfo;

/**
 * GstBufferMeta:
 * @info: pointer to the #GstBufferMetaInfo
 *
 * Base structure for buffer metadata. Custom metadata will put this structure
 * as the first member of their structure.
 */
struct _GstBufferMeta {
  const GstBufferMetaInfo *info;
};

/**
 * GST_BUFFER_META_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_BUFFER_META_TRACE_NAME           "GstBufferMeta"

/**
 * GstMetaInitFunction:
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when @meta is initialized in @buffer.
 */
typedef void (*GstMetaInitFunction)     (GstBufferMeta *meta, GstBuffer *buffer);

/**
 * GstMetaFreeFunction:
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when @meta is freed in @buffer.
 */
typedef void (*GstMetaFreeFunction)     (GstBufferMeta *meta, GstBuffer *buffer);

/**
 * GstMetaCopyFunction:
 * @copy: a #GstBuffer
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 *
 * Function called when a copy of @buffer is made and @meta should be copied to
 * @copy.
 */
typedef void (*GstMetaCopyFunction)     (GstBuffer *copy, GstBufferMeta *meta,
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
typedef void (*GstMetaSubFunction)      (GstBuffer *subbuf, GstBufferMeta *meta,
                                         GstBuffer *buffer, guint offset, guint size);

/**
 * GstMetaSerializeFunction:
 * @meta: a #GstMeta
 */
typedef gchar * (*GstMetaSerializeFunction) (GstBufferMeta *meta);

/**
 * GstMetaDeserializeFunction:
 * @meta: a #GstMeta
 */
typedef gboolean (*GstMetaDeserializeFunction) (GstBufferMeta *meta, 
                                                const gchar *s);

/**
 * GstBufferMetaInfo:
 * @name: tag indentifying the metadata
 * @size: size of the metadata
 * @init_func: function for initializing the metadata
 * @free_func: function for freeing the metadata
 * @copy_func: function for copying the metadata
 * @sub_func: function for when a subbuffer is taken
 * @serialize_func: function for serializing
 * @deserialize_func: function for deserializing
 *
 * The #GstBufferMetaInfo provides information about a specific metadata
 * structure.
 */
struct _GstBufferMetaInfo {
  const gchar               *name;
  gsize                      size;

  GstMetaInitFunction        init_func;
  GstMetaFreeFunction        free_func;
  GstMetaCopyFunction        copy_func;
  GstMetaSubFunction         sub_func;
  GstMetaSerializeFunction   serialize_func;
  GstMetaDeserializeFunction deserialize_func;
};

void _gst_buffer_meta_init (void);

const GstBufferMetaInfo *  gst_buffer_meta_register_info   (const GstBufferMetaInfo *info);
const GstBufferMetaInfo *  gst_buffer_meta_get_info        (const gchar * name);

G_END_DECLS

#endif /* __GST_BUFFER_META_H__ */
