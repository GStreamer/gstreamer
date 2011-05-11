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

typedef void (*GstMetaCopyFunction)     (GstBuffer *dest, GstMeta *meta,
                                         GstBuffer *buffer, gsize offset, gsize size);
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
 */
typedef void (*GstMetaTransformFunction) (GstBuffer *transbuf, GstMeta *meta,
                                          GstBuffer *buffer, gpointer data);

/**
 * GstMetaInfo:
 * @api: tag indentifying the metadata structure and api
 * @type: type indentifying the implementor of the api
 * @size: size of the metadata
 * @init_func: function for initializing the metadata
 * @free_func: function for freeing the metadata
 * @copy_func: function for copying the metadata
 * @transform_func: function for transforming the metadata
 *
 * The #GstMetaInfo provides information about a specific metadata
 * structure.
 */
struct _GstMetaInfo {
  GQuark                     api;
  GType                      type;
  gsize                      size;

  GstMetaInitFunction        init_func;
  GstMetaFreeFunction        free_func;
  GstMetaCopyFunction        copy_func;
  GstMetaTransformFunction   transform_func;
};

void _gst_meta_init (void);

const GstMetaInfo *  gst_meta_register        (const gchar *api, const gchar *impl,
                                               gsize size,
                                               GstMetaInitFunction        init_func,
                                               GstMetaFreeFunction        free_func,
                                               GstMetaCopyFunction        copy_func,
                                               GstMetaTransformFunction   transform_func);
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
