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
 * GstMetaTransformType:
 * @GST_META_TRANSFORM_NONE: invalid transform type
 * @GST_META_TRANSFORM_COPY: copy transform
 * @GST_META_TRANSFORM_MAKE_WRITABLE: make writable type
 * @GST_META_TRANSFORM_TRIM: trim buffer
 * @GST_META_TRANSFORM_CUSTOM: start of custom transform types
 *
 * Different default transform types.
 */
typedef enum {
  GST_META_TRANSFORM_NONE = 0,
  GST_META_TRANSFORM_COPY,
  GST_META_TRANSFORM_MAKE_WRITABLE,
  GST_META_TRANSFORM_TRIM,

  GST_META_TRANSFORM_CUSTOM = 256 
} GstMetaTransformType;

/**
 * GstMetaTransformData:
 * @type: a #GstMetaTransformType
 *
 * Common structure that should be put as the first field in the type specific
 * structure for the #GstMetaTransformFunction. It contains the type of the
 * transform that should be performed.
 */
typedef struct {
  GstMetaTransformType type;
} GstMetaTransformData;

/**
 * GstMetaTransformSubbuffer:
 * @data: parent #GstMetaTransformData
 * @offset: the offset of the subbuffer
 * @size: the new size of the subbuffer
 *
 * The subbuffer specific extra info.
 */
typedef struct {
  GstMetaTransformData data;
  gsize offset;
  gsize size;
} GstMetaTransformSubbuffer;

/**
 * GstMetaTransformFunction:
 * @transbuf: a #GstBuffer
 * @meta: a #GstMeta
 * @buffer: a #GstBuffer
 * @data: transform specific data.
 *
 * Function called for each @meta in @buffer as a result of performing a
 * transformation on @transbuf. Additional type specific transform data
 * is passed to the function.
 *
 * Implementations should check the type of the transform @data and parse
 * additional type specific field that should be used to perform the transform.
 *
 * If @data is NULL, the metadata should be shallow copied. This is done when
 * gst_buffer_make_metadata_writable() is called.
 */
typedef void (*GstMetaTransformFunction) (GstBuffer *transbuf, GstMeta *meta,
                                          GstBuffer *buffer, GstMetaTransformData *data);

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
 * @transform_func: function for transforming the metadata
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
  GstMetaTransformFunction   transform_func;
  GstMetaSerializeFunction   serialize_func;
  GstMetaDeserializeFunction deserialize_func;
};

void _gst_meta_init (void);

const GstMetaInfo *  gst_meta_register        (const gchar *api, const gchar *impl,
                                               gsize size,
                                               GstMetaInitFunction        init_func,
                                               GstMetaFreeFunction        free_func,
                                               GstMetaTransformFunction   transform_func,
                                               GstMetaSerializeFunction   serialize_func,
                                               GstMetaDeserializeFunction deserialize_func);
const GstMetaInfo *  gst_meta_get_info        (const gchar * impl);

/* default metadata */

/* timing metadata */
typedef struct _GstMetaTiming GstMetaTiming;

const GstMetaInfo *gst_meta_timing_get_info(void);
#define GST_META_TIMING_INFO (gst_meta_timing_get_info())

struct _GstMetaTiming {
  GstMeta        meta;        /* common meta header */

  GstClockTime   dts;         /* decoding timestamp */
  GstClockTime   pts;         /* presentation timestamp */
  GstClockTime   duration;    /* duration of the data */
  GstClockTime   clock_rate;  /* clock rate for the above values */
};

#define gst_buffer_get_meta_timing(b)  ((GstMetaTiming*)gst_buffer_get_meta((b),GST_META_TIMING_INFO))
#define gst_buffer_add_meta_timing(b)  ((GstMetaTiming*)gst_buffer_add_meta((b),GST_META_TIMING_INFO,NULL))

G_END_DECLS

#endif /* __GST_META_H__ */
