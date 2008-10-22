/* GStreamer
 *
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>.
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

#ifndef __GST_BYTE_READER_H__
#define __GST_BYTE_READER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstByteReader:
 * @data: Data from which the bit reader will read
 * @size: Size of @data in bytes
 * @byte: Current byte position
 *
 * A byte reader instance.
 */
typedef struct {
  const guint8 *data;
  guint size;

  guint byte;  /* Byte position */
} GstByteReader;

GstByteReader * gst_byte_reader_new (const guint8 *data, guint size);
GstByteReader * gst_byte_reader_new_from_buffer (const GstBuffer *buffer);
void gst_byte_reader_free (GstByteReader *reader);

void gst_byte_reader_init (GstByteReader *reader, const guint8 *data, guint size);
void gst_byte_reader_init_from_buffer (GstByteReader *reader, const GstBuffer *buffer);

gboolean gst_byte_reader_set_pos (GstByteReader *reader, guint pos);

guint gst_byte_reader_get_pos (const GstByteReader *reader);
guint gst_byte_reader_get_remaining (const GstByteReader *reader);

gboolean gst_byte_reader_skip (GstByteReader *reader, guint nbytes);

gboolean gst_byte_reader_get_uint8 (GstByteReader *reader, guint8 *val);
gboolean gst_byte_reader_get_int8 (GstByteReader *reader, gint8 *val);
gboolean gst_byte_reader_get_uint16_le (GstByteReader *reader, guint16 *val);
gboolean gst_byte_reader_get_int16_le (GstByteReader *reader, gint16 *val);
gboolean gst_byte_reader_get_uint16_be (GstByteReader *reader, guint16 *val);
gboolean gst_byte_reader_get_int16_be (GstByteReader *reader, gint16 *val);
gboolean gst_byte_reader_get_uint24_le (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_get_int24_le (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_get_uint24_be (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_get_int24_be (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_get_uint32_le (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_get_int32_le (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_get_uint32_be (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_get_int32_be (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_get_uint64_le (GstByteReader *reader, guint64 *val);
gboolean gst_byte_reader_get_int64_le (GstByteReader *reader, gint64 *val);
gboolean gst_byte_reader_get_uint64_be (GstByteReader *reader, guint64 *val);
gboolean gst_byte_reader_get_int64_be (GstByteReader *reader, gint64 *val);

gboolean gst_byte_reader_peek_uint8 (GstByteReader *reader, guint8 *val);
gboolean gst_byte_reader_peek_int8 (GstByteReader *reader, gint8 *val);
gboolean gst_byte_reader_peek_uint16_le (GstByteReader *reader, guint16 *val);
gboolean gst_byte_reader_peek_int16_le (GstByteReader *reader, gint16 *val);
gboolean gst_byte_reader_peek_uint16_be (GstByteReader *reader, guint16 *val);
gboolean gst_byte_reader_peek_int16_be (GstByteReader *reader, gint16 *val);
gboolean gst_byte_reader_peek_uint24_le (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_peek_int24_le (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_peek_uint24_be (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_peek_int24_be (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_peek_uint32_le (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_peek_int32_le (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_peek_uint32_be (GstByteReader *reader, guint32 *val);
gboolean gst_byte_reader_peek_int32_be (GstByteReader *reader, gint32 *val);
gboolean gst_byte_reader_peek_uint64_le (GstByteReader *reader, guint64 *val);
gboolean gst_byte_reader_peek_int64_le (GstByteReader *reader, gint64 *val);
gboolean gst_byte_reader_peek_uint64_be (GstByteReader *reader, guint64 *val);
gboolean gst_byte_reader_peek_int64_be (GstByteReader *reader, gint64 *val);

gboolean gst_byte_reader_get_float32_le (GstByteReader *reader, gfloat *val);
gboolean gst_byte_reader_get_float32_be (GstByteReader *reader, gfloat *val);
gboolean gst_byte_reader_get_float64_le (GstByteReader *reader, gdouble *val);
gboolean gst_byte_reader_get_float64_be (GstByteReader *reader, gdouble *val);

gboolean gst_byte_reader_peek_float32_le (GstByteReader *reader, gfloat *val);
gboolean gst_byte_reader_peek_float32_be (GstByteReader *reader, gfloat *val);
gboolean gst_byte_reader_peek_float64_le (GstByteReader *reader, gdouble *val);
gboolean gst_byte_reader_peek_float64_be (GstByteReader *reader, gdouble *val);

gboolean gst_byte_reader_get_data (GstByteReader *reader, guint size, const guint8 **val);
gboolean gst_byte_reader_peek_data (GstByteReader *reader, guint size, const guint8 **val);

/**
 * GST_BYTE_READER_INIT:
 * @data: Data from which the #GstByteReader should read
 * @size: Size of @data in bytes
 *
 * A #GstByteReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_byte_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_BYTE_READER_INIT(data, size) {data, size, 0}

/**
 * GST_BYTE_READER_INIT_FROM_BUFFER:
 * @buffer: Buffer from which the #GstByteReader should read
 *
 * A #GstByteReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_byte_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_BYTE_READER_INIT_FROM_BUFFER(buffer) {GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 0}

G_END_DECLS

#endif /* __GST_BYTE_READER_H__ */
