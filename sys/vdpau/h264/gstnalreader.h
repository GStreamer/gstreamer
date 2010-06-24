/* GStreamer
 *
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
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

#ifndef __GST_NAL_READER_H__
#define __GST_NAL_READER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstNalReader GstNalReader;

struct _GstNalReader
{
  const guint8 *data;
  guint size;

  guint byte;                   /* Byte position */
  guint bits_in_cache;          /* bitpos in the cache of next bit */
  guint8 first_byte;
  guint64 cache;                /* cached bytes */
};

GstNalReader *gst_nal_reader_new (const guint8 *data, guint size);
GstNalReader *gst_nal_reader_new_from_buffer (const GstBuffer *buffer);
void gst_nal_reader_free (GstNalReader * reader);

void gst_nal_reader_init (GstNalReader * reader, const guint8 * data, guint size);
void gst_nal_reader_init_from_buffer (GstNalReader * reader, const GstBuffer * buffer);

gboolean gst_nal_reader_skip (GstNalReader *reader, guint nbits);
gboolean gst_nal_reader_skip_to_byte (GstNalReader *reader);

guint gst_nal_reader_get_pos (const GstNalReader * reader);
guint gst_nal_reader_get_remaining (const GstNalReader *reader);

gboolean gst_nal_reader_get_bits_uint8 (GstNalReader *reader, guint8 *val, guint nbits);
gboolean gst_nal_reader_get_bits_uint16 (GstNalReader *reader, guint16 *val, guint nbits);
gboolean gst_nal_reader_get_bits_uint32 (GstNalReader *reader, guint32 *val, guint nbits);
gboolean gst_nal_reader_get_bits_uint64 (GstNalReader *reader, guint64 *val, guint nbits);

gboolean gst_nal_reader_peek_bits_uint8 (const GstNalReader *reader, guint8 *val, guint nbits);
gboolean gst_nal_reader_peek_bits_uint16 (const GstNalReader *reader, guint16 *val, guint nbits);
gboolean gst_nal_reader_peek_bits_uint32 (const GstNalReader *reader, guint32 *val, guint nbits);
gboolean gst_nal_reader_peek_bits_uint64 (const GstNalReader *reader, guint64 *val, guint nbits);

gboolean gst_nal_reader_get_ue (GstNalReader *reader, guint32 *val);
gboolean gst_nal_reader_peek_ue (const GstNalReader *reader, guint32 *val);

gboolean gst_nal_reader_get_se (GstNalReader *reader, gint32 *val);
gboolean gst_nal_reader_peek_se (const GstNalReader *reader, gint32 *val);

/**
 * GST_NAL_READER_INIT:
 * @data: Data from which the #GstNalReader should read
 * @size: Size of @data in bytes
 *
 * A #GstNalReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_bit_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_NAL_READER_INIT(data, size) {data, size, 0, 0, 0xff, 0xff}

/**
 * GST_NAL_READER_INIT_FROM_BUFFER:
 * @buffer: Buffer from which the #GstNalReader should read
 *
 * A #GstNalReader must be initialized with this macro, before it can be
 * used. This macro can used be to initialize a variable, but it cannot
 * be assigned to a variable. In that case you have to use
 * gst_bit_reader_init().
 *
 * Since: 0.10.22
 */
#define GST_NAL_READER_INIT_FROM_BUFFER(buffer) {GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 0, 0, 0xff, 0xff}

G_END_DECLS

#endif /* __GST_NAL_READER_H__ */
