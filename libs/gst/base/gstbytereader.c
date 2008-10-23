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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbytereader.h"

#include <string.h>

/**
 * SECTION:gstbytereader
 * @short_description: Reads different integer and floating point types from a memory buffer
 *
 * #GstByteReader provides a byte reader that can read different integer and
 * floating point types from a memory buffer. It provides functions for reading
 * signed/unsigned, little/big endian integers of 8, 16, 24, 32 and 64 bits
 * and functions for reading little/big endian floating points numbers of
 * 32 and 64 bits.
 */

/**
 * gst_byte_reader_new:
 * @data: Data from which the #GstByteReader should read
 * @size: Size of @data in bytes
 *
 * Create a new #GstByteReader instance, which will read from @data.
 *
 * Returns: a new #GstByteReader instance
 *
 * Since: 0.10.22
 */
GstByteReader *
gst_byte_reader_new (const guint8 * data, guint size)
{
  GstByteReader *ret = g_slice_new0 (GstByteReader);

  ret->data = data;
  ret->size = size;

  return ret;
}

/**
 * gst_byte_reader_new_from_buffer:
 * @buffer: Buffer from which the #GstByteReader should read
 *
 * Create a new #GstByteReader instance, which will read from the
 * #GstBuffer @buffer.
 *
 * Returns: a new #GstByteReader instance
 *
 * Since: 0.10.22
 */
GstByteReader *
gst_byte_reader_new_from_buffer (const GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return gst_byte_reader_new (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_byte_reader_free:
 * @reader: a #GstByteReader instance
 *
 * Frees a #GstByteReader instance, which was previously allocated by
 * gst_byte_reader_new() or gst_byte_reader_new_from_buffer().
 * 
 * Since: 0.10.22
 */
void
gst_byte_reader_free (GstByteReader * reader)
{
  g_return_if_fail (reader != NULL);

  g_slice_free (GstByteReader, reader);
}

/**
 * gst_byte_reader_init:
 * @reader: a #GstByteReader instance
 * @data: Data from which the #GstByteReader should read
 * @size: Size of @data in bytes
 *
 * Initializes a #GstByteReader instance to read from @data. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_byte_reader_init (GstByteReader * reader, const guint8 * data, guint size)
{
  g_return_if_fail (reader != NULL);

  reader->data = data;
  reader->size = size;
  reader->byte = 0;
}

/**
 * gst_byte_reader_init_from_buffer:
 * @reader: a #GstByteReader instance
 * @buffer: Buffer from which the #GstByteReader should read
 *
 * Initializes a #GstByteReader instance to read from @buffer. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_byte_reader_init_from_buffer (GstByteReader * reader,
    const GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));

  gst_byte_reader_init (reader, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_byte_reader_set_pos:
 * @reader: a #GstByteReader instance
 * @pos: The new position in bytes
 *
 * Sets the new position of a #GstByteReader instance to @pos in bytes.
 *
 * Returns: %TRUE if the position could be set successfully, %FALSE
 * otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_byte_reader_set_pos (GstByteReader * reader, guint pos)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (pos > reader->size)
    return FALSE;

  reader->byte = pos;

  return TRUE;
}

/**
 * gst_byte_reader_get_pos:
 * @reader: a #GstByteReader instance
 *
 * Returns the current position of a #GstByteReader instance in bytes.
 *
 * Returns: The current position of @reader in bytes.
 * 
 * Since: 0.10.22
 */
guint
gst_byte_reader_get_pos (const GstByteReader * reader)
{
  g_return_val_if_fail (reader != NULL, 0);

  return reader->byte;
}

/**
 * gst_byte_reader_get_remaining:
 * @reader: a #GstByteReader instance
 *
 * Returns the remaining number of bytes of a #GstByteReader instance.
 *
 * Returns: The remaining number of bytes of @reader instance.
 * 
 * Since: 0.10.22
 */
guint
gst_byte_reader_get_remaining (const GstByteReader * reader)
{
  g_return_val_if_fail (reader != NULL, 0);

  return reader->size - reader->byte;
}

/**
 * gst_byte_reader_skip:
 * @reader: a #GstByteReader instance
 * @nbytes: the number of bytes to skip
 *
 * Skips @nbytes bytes of the #GstByteReader instance.
 *
 * Returns: %TRUE if @nbytes bytes could be skipped, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_byte_reader_skip (GstByteReader * reader, guint nbytes)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (gst_byte_reader_get_remaining (reader) < nbytes)
    return FALSE;

  reader->byte += nbytes;

  return TRUE;
}

/**
 * gst_byte_reader_get_uint8:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint8 to store the result
 *
 * Read an unsigned 8 bit integer into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int8:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint8 to store the result
 *
 * Read a signed 8 bit integer into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint8:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint8 to store the result
 *
 * Read a signed 8 bit integer into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int8:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint8 to store the result
 *
 * Read a signed 8 bit integer into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint16_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint16 to store the result
 *
 * Read an unsigned 16 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int16_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint16 to store the result
 *
 * Read a signed 16 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint16_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint16 to store the result
 *
 * Read a signed 16 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int16_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint16 to store the result
 *
 * Read a signed 16 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint16_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint16 to store the result
 *
 * Read an unsigned 16 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int16_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint16 to store the result
 *
 * Read a signed 16 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint16_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint16 to store the result
 *
 * Read a signed 16 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int16_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint16 to store the result
 *
 * Read a signed 16 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint24_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read an unsigned 24 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int24_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 24 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint24_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read a signed 24 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int24_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 24 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint24_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read an unsigned 24 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int24_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 24 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint24_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read a signed 24 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int24_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 24 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */


/**
 * gst_byte_reader_get_uint32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read an unsigned 32 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 32 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read a signed 32 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 32 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read an unsigned 32 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 32 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read a signed 32 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed 32 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint64 to store the result
 *
 * Read an unsigned 64 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint64 to store the result
 *
 * Read a signed 64 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint64 to store the result
 *
 * Read a signed 64 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint64 to store the result
 *
 * Read a signed 64 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_uint64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint64 to store the result
 *
 * Read an unsigned 64 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_int64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint64 to store the result
 *
 * Read a signed 64 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_uint64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #guint64 to store the result
 *
 * Read a signed 64 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_int64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gint64 to store the result
 *
 * Read a signed 64 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

#define GST_BYTE_READER_READ_INTS(bits) \
gboolean \
gst_byte_reader_get_uint##bits##_le (GstByteReader *reader, guint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_LE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_get_uint##bits##_be (GstByteReader *reader, guint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_BE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_get_int##bits##_le (GstByteReader *reader, gint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_LE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_get_int##bits##_be (GstByteReader *reader, gint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_BE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
gboolean \
gst_byte_reader_peek_uint##bits##_le (GstByteReader *reader, guint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_LE (&reader->data[reader->byte]); \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_peek_uint##bits##_be (GstByteReader *reader, guint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_BE (&reader->data[reader->byte]); \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_peek_int##bits##_le (GstByteReader *reader, gint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_LE (&reader->data[reader->byte]); \
  return TRUE; \
} \
\
gboolean \
gst_byte_reader_peek_int##bits##_be (GstByteReader *reader, gint##bits *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_UINT##bits##_BE (&reader->data[reader->byte]); \
  return TRUE; \
}


GST_BYTE_READER_READ_INTS (16);
GST_BYTE_READER_READ_INTS (32);
GST_BYTE_READER_READ_INTS (64);

gboolean
gst_byte_reader_get_uint8 (GstByteReader * reader, guint8 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 1 > reader->size)
    return FALSE;

  *val = GST_READ_UINT8 (&reader->data[reader->byte]);
  reader->byte++;
  return TRUE;
}

gboolean
gst_byte_reader_get_int8 (GstByteReader * reader, gint8 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 1 > reader->size)
    return FALSE;

  *val = GST_READ_UINT8 (&reader->data[reader->byte]);
  reader->byte++;
  return TRUE;
}

gboolean
gst_byte_reader_peek_uint8 (GstByteReader * reader, guint8 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 1 > reader->size)
    return FALSE;

  *val = GST_READ_UINT8 (&reader->data[reader->byte]);
  return TRUE;
}

gboolean
gst_byte_reader_peek_int8 (GstByteReader * reader, gint8 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 1 > reader->size)
    return FALSE;

  *val = GST_READ_UINT8 (&reader->data[reader->byte]);
  return TRUE;
}

gboolean
gst_byte_reader_get_uint24_le (GstByteReader * reader, guint32 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  *val = GST_READ_UINT24_LE (&reader->data[reader->byte]);
  reader->byte += 3;
  return TRUE;
}

gboolean
gst_byte_reader_get_uint24_be (GstByteReader * reader, guint32 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  *val = GST_READ_UINT24_BE (&reader->data[reader->byte]);
  reader->byte += 3;
  return TRUE;
}

gboolean
gst_byte_reader_get_int24_le (GstByteReader * reader, gint32 * val)
{
  guint32 ret;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  ret = GST_READ_UINT24_LE (&reader->data[reader->byte]);
  if (ret & 0x00800000)
    ret |= 0xff000000;

  reader->byte += 3;

  *val = ret;
  return TRUE;
}

gboolean
gst_byte_reader_get_int24_be (GstByteReader * reader, gint32 * val)
{
  guint32 ret;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  ret = GST_READ_UINT24_BE (&reader->data[reader->byte]);
  if (ret & 0x00800000)
    ret |= 0xff000000;

  reader->byte += 3;

  *val = ret;
  return TRUE;
}

gboolean
gst_byte_reader_peek_uint24_le (GstByteReader * reader, guint32 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  *val = GST_READ_UINT24_LE (&reader->data[reader->byte]);
  return TRUE;
}

gboolean
gst_byte_reader_peek_uint24_be (GstByteReader * reader, guint32 * val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  *val = GST_READ_UINT24_BE (&reader->data[reader->byte]);
  return TRUE;
}

gboolean
gst_byte_reader_peek_int24_le (GstByteReader * reader, gint32 * val)
{
  guint32 ret;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  ret = GST_READ_UINT24_LE (&reader->data[reader->byte]);
  if (ret & 0x00800000)
    ret |= 0xff000000;

  *val = ret;
  return TRUE;
}

gboolean
gst_byte_reader_peek_int24_be (GstByteReader * reader, gint32 * val)
{
  guint32 ret;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + 3 > reader->size)
    return FALSE;

  ret = GST_READ_UINT24_BE (&reader->data[reader->byte]);
  if (ret & 0x00800000)
    ret |= 0xff000000;

  *val = ret;
  return TRUE;
}

/**
 * gst_byte_reader_get_float32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gfloat to store the result
 *
 * Read a 32 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_float32_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gfloat to store the result
 *
 * Read a 32 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_float32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gfloat to store the result
 *
 * Read a 32 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_float32_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gfloat to store the result
 *
 * Read a 32 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_float64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gdouble to store the result
 *
 * Read a 64 bit little endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_float64_le:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gdouble to store the result
 *
 * Read a 64 bit little endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_get_float64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gdouble to store the result
 *
 * Read a 64 bit big endian integer into @val
 * and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_float64_be:
 * @reader: a #GstByteReader instance
 * @val: Pointer to a #gdouble to store the result
 *
 * Read a 64 bit big endian integer into @val
 * but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

#define GST_BYTE_READER_READ_FLOATS(bits, type, TYPE) \
gboolean \
gst_byte_reader_get_float##bits##_le (GstByteReader *reader, g##type *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_##TYPE##_LE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
gboolean \
gst_byte_reader_get_float##bits##_be (GstByteReader *reader, g##type *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_##TYPE##_BE (&reader->data[reader->byte]); \
  reader->byte += bits / 8; \
  return TRUE; \
} \
gboolean \
gst_byte_reader_peek_float##bits##_le (GstByteReader *reader, g##type *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_##TYPE##_LE (&reader->data[reader->byte]); \
  return TRUE; \
} \
gboolean \
gst_byte_reader_peek_float##bits##_be (GstByteReader *reader, g##type *val) \
{ \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  \
  if (reader->byte + bits / 8 > reader->size) \
    return FALSE; \
  \
  *val = GST_READ_##TYPE##_BE (&reader->data[reader->byte]); \
  return TRUE; \
}

GST_BYTE_READER_READ_FLOATS (32, float, FLOAT);
GST_BYTE_READER_READ_FLOATS (64, double, DOUBLE);

/**
 * gst_byte_reader_get_data:
 * @reader: a #GstByteReader instance
 * @size: Size in bytes
 * @val: Pointer to a #guint8 to store the result
 *
 * Returns a constant pointer to the current data
 * position if at least @size bytes are left and
 * updates the current position.
 *
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_byte_reader_peek_data:
 * @reader: a #GstByteReader instance
 * @size: Size in bytes
 * @val: Pointer to a #guint8 to store the result
 *
 * Returns a constant pointer to the current data
 * position if at least @size bytes are left and
 * keeps the current position.
 *
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

gboolean
gst_byte_reader_get_data (GstByteReader * reader, guint size,
    const guint8 ** val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + size > reader->size)
    return FALSE;

  *val = reader->data + reader->byte;
  reader->byte += size;
  return TRUE;
}

gboolean
gst_byte_reader_peek_data (GstByteReader * reader, guint size,
    const guint8 ** val)
{
  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  if (reader->byte + size > reader->size)
    return FALSE;

  *val = reader->data + reader->byte;
  return TRUE;
}
