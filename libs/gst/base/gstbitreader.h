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

#ifndef __GST_BIT_READER_H__
#define __GST_BIT_READER_H__

#include <gst/gst.h>

/* FIXME: inline functions */

G_BEGIN_DECLS

#define GST_BIT_READER(reader) ((GstBitReader *) (reader))

/**
 * GstBitReader:
 * @data: Data from which the bit reader will read
 * @size: Size of @data in bytes
 * @byte: Current byte position
 * @bit: Bit position in the current byte
 *
 * A bit reader instance.
 */
typedef struct {
  const guint8 *data;
  guint size;

  guint byte;  /* Byte position */
  guint bit;   /* Bit position in the current byte */
} GstBitReader;

GstBitReader * gst_bit_reader_new (const guint8 *data, guint size);
GstBitReader * gst_bit_reader_new_from_buffer (const GstBuffer *buffer);
void gst_bit_reader_free (GstBitReader *reader);

void gst_bit_reader_init (GstBitReader *reader, const guint8 *data, guint size);
void gst_bit_reader_init_from_buffer (GstBitReader *reader, const GstBuffer *buffer);

gboolean gst_bit_reader_set_pos (GstBitReader *reader, guint pos);

guint gst_bit_reader_get_pos (const GstBitReader *reader);
guint gst_bit_reader_get_remaining (const GstBitReader *reader);

guint gst_bit_reader_get_size (const GstBitReader *reader);

gboolean gst_bit_reader_skip (GstBitReader *reader, guint nbits);
gboolean gst_bit_reader_skip_to_byte (GstBitReader *reader);

gboolean gst_bit_reader_get_bits_uint8 (GstBitReader *reader, guint8 *val, guint nbits);
gboolean gst_bit_reader_get_bits_uint16 (GstBitReader *reader, guint16 *val, guint nbits);
gboolean gst_bit_reader_get_bits_uint32 (GstBitReader *reader, guint32 *val, guint nbits);
gboolean gst_bit_reader_get_bits_uint64 (GstBitReader *reader, guint64 *val, guint nbits);

gboolean gst_bit_reader_peek_bits_uint8 (const GstBitReader *reader, guint8 *val, guint nbits);
gboolean gst_bit_reader_peek_bits_uint16 (const GstBitReader *reader, guint16 *val, guint nbits);
gboolean gst_bit_reader_peek_bits_uint32 (const GstBitReader *reader, guint32 *val, guint nbits);
gboolean gst_bit_reader_peek_bits_uint64 (const GstBitReader *reader, guint64 *val, guint nbits);

/**
 * GST_BIT_READER_INIT:
 * @data: Data from which the #GstBitReader should read
 * @size: Size of @data in bytes
 *
 * A #GstBitReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_bit_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_BIT_READER_INIT(data, size) {data, size, 0, 0}

/**
 * GST_BIT_READER_INIT_FROM_BUFFER:
 * @buffer: Buffer from which the #GstBitReader should read
 *
 * A #GstBitReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_bit_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_BIT_READER_INIT_FROM_BUFFER(buffer) {GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 0, 0}

G_END_DECLS

#endif /* __GST_BIT_READER_H__ */
