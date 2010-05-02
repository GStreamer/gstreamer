/* GStreamer byte writer
 *
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>.
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

#ifndef __GST_BYTE_WRITER_H__
#define __GST_BYTE_WRITER_H__

#include <gst/gst.h>
#include <gst/base/gstbytereader.h>

G_BEGIN_DECLS

#define GST_BYTE_WRITER(writer) ((GstByteWriter *) (writer))

/**
 * GstByteWriter:
 * @parent: #GstByteReader parent
 * @alloc_size: Allocation size of the data
 * @fixed: If %TRUE no reallocations are allowed
 * @owned: If %FALSE no reallocations are allowed and copies of data are returned
 *
 * A byte writer instance.
 */
typedef struct {
  GstByteReader parent;

  guint alloc_size;

  gboolean fixed;
  gboolean owned;
} GstByteWriter;

GstByteWriter * gst_byte_writer_new (void);
GstByteWriter * gst_byte_writer_new_with_size (guint size, gboolean fixed);
GstByteWriter * gst_byte_writer_new_with_data (guint8 *data, guint size, gboolean initialized);
GstByteWriter * gst_byte_writer_new_with_buffer (GstBuffer *buffer, gboolean initialized);

void gst_byte_writer_init (GstByteWriter *writer);
void gst_byte_writer_init_with_size (GstByteWriter *writer, guint size, gboolean fixed);
void gst_byte_writer_init_with_data (GstByteWriter *writer, guint8 *data, guint size, gboolean initialized);
void gst_byte_writer_init_with_buffer (GstByteWriter *writer, GstBuffer *buffer, gboolean initialized);

void gst_byte_writer_free (GstByteWriter *writer);
guint8 * gst_byte_writer_free_and_get_data (GstByteWriter *writer);
GstBuffer *gst_byte_writer_free_and_get_buffer (GstByteWriter *writer);

void gst_byte_writer_reset (GstByteWriter *writer);
guint8 * gst_byte_writer_reset_and_get_data (GstByteWriter *writer);
GstBuffer *gst_byte_writer_reset_and_get_buffer (GstByteWriter *writer);

/**
 * gst_byte_writer_get_pos:
 * @writer: #GstByteWriter instance
 *
 * Returns: The current position of the read/write cursor
 *
 * Since: 0.10.26
 */
/**
 * gst_byte_writer_set_pos:
 * @writer: #GstByteWriter instance
 * @pos: new position
 *
 * Sets the current read/write cursor of @writer. The new position
 * can only be between 0 and the current size.
 *
 * Returns: %TRUE if the new position could be set
 *
 * Since: 0.10.26
 */
/**
 * gst_byte_writer_get_size:
 * @writer: #GstByteWriter instance
 *
 * Returns: The current, initialized size of the data
 *
 * Since: 0.10.26
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC guint gst_byte_writer_get_pos (const GstByteWriter *writer);
G_INLINE_FUNC gboolean gst_byte_writer_set_pos (const GstByteWriter *writer, guint pos);
G_INLINE_FUNC guint gst_byte_writer_get_size (const GstByteWriter *writer);
#else
static inline guint
gst_byte_writer_get_pos (const GstByteWriter *writer)
{
  return gst_byte_reader_get_pos (GST_BYTE_READER (writer));
}

static inline gboolean
gst_byte_writer_set_pos (const GstByteWriter *writer, guint pos)
{
  return gst_byte_reader_set_pos (GST_BYTE_READER (writer), pos);
}

static inline guint
gst_byte_writer_get_size (const GstByteWriter *writer)
{
  return gst_byte_reader_get_size (GST_BYTE_READER (writer));
}
#endif

guint gst_byte_writer_get_remaining (const GstByteWriter *writer);
gboolean gst_byte_writer_ensure_free_space (GstByteWriter *writer, guint size);

gboolean gst_byte_writer_put_uint8 (GstByteWriter *writer, guint8 val);
gboolean gst_byte_writer_put_int8 (GstByteWriter *writer, gint8 val);
gboolean gst_byte_writer_put_uint16_be (GstByteWriter *writer, guint16 val);
gboolean gst_byte_writer_put_uint16_le (GstByteWriter *writer, guint16 val);
gboolean gst_byte_writer_put_int16_be (GstByteWriter *writer, gint16 val);
gboolean gst_byte_writer_put_int16_le (GstByteWriter *writer, gint16 val);
gboolean gst_byte_writer_put_uint24_be (GstByteWriter *writer, guint32 val);
gboolean gst_byte_writer_put_uint24_le (GstByteWriter *writer, guint32 val);
gboolean gst_byte_writer_put_int24_be (GstByteWriter *writer, gint32 val);
gboolean gst_byte_writer_put_int24_le (GstByteWriter *writer, gint32 val);
gboolean gst_byte_writer_put_uint32_be (GstByteWriter *writer, guint32 val);
gboolean gst_byte_writer_put_uint32_le (GstByteWriter *writer, guint32 val);
gboolean gst_byte_writer_put_int32_be (GstByteWriter *writer, gint32 val);
gboolean gst_byte_writer_put_int32_le (GstByteWriter *writer, gint32 val);
gboolean gst_byte_writer_put_uint64_be (GstByteWriter *writer, guint64 val);
gboolean gst_byte_writer_put_uint64_le (GstByteWriter *writer, guint64 val);
gboolean gst_byte_writer_put_int64_be (GstByteWriter *writer, gint64 val);
gboolean gst_byte_writer_put_int64_le (GstByteWriter *writer, gint64 val);

gboolean gst_byte_writer_put_float32_be (GstByteWriter *writer, gfloat val);
gboolean gst_byte_writer_put_float32_le (GstByteWriter *writer, gfloat val);
gboolean gst_byte_writer_put_float64_be (GstByteWriter *writer, gdouble val);
gboolean gst_byte_writer_put_float64_le (GstByteWriter *writer, gdouble val);

gboolean gst_byte_writer_put_data (GstByteWriter *writer, const guint8 *data, guint size);
gboolean gst_byte_writer_fill (GstByteWriter *writer, const guint8 value, guint size);
gboolean gst_byte_writer_put_string_utf8 (GstByteWriter *writer, const gchar *data);
gboolean gst_byte_writer_put_string_utf16 (GstByteWriter *writer, const guint16 *data);
gboolean gst_byte_writer_put_string_utf32 (GstByteWriter *writer, const guint32 *data);

/**
 * gst_byte_writer_put_string:
 * @writer: #GstByteWriter instance
 * @data: Null terminated string
 *
 * Write a NUL-terminated string to @writer (including the terminator). The
 * string is assumed to be in an 8-bit encoding (e.g. ASCII,UTF-8 or
 * ISO-8859-1).
 *
 * Returns: %TRUE if the string could be written
 *
 * Since: 0.10.26
 */
#define gst_byte_writer_put_string(writer, data) \
  gst_byte_writer_put_string_utf8(writer, data)

G_END_DECLS

#endif /* __GST_BYTE_WRITER_H__ */
