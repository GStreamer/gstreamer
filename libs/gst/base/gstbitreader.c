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

#include "gstbitreader.h"

#include <string.h>

/**
 * SECTION:gstbitreader
 * @short_description: Reads any number of bits from a memory buffer
 *
 * #GstBitReader provides a bit reader that can read any number of bits
 * from a memory buffer. It provides functions for reading any number of bits
 * into 8, 16, 32 and 64 bit variables.
 */

/**
 * gst_bit_reader_new:
 * @data: Data from which the #GstBitReader should read
 * @size: Size of @data in bytes
 *
 * Create a new #GstBitReader instance, which will read from @data.
 *
 * Returns: a new #GstBitReader instance
 *
 * Since: 0.10.22
 */
GstBitReader *
gst_bit_reader_new (const guint8 * data, guint size)
{
  GstBitReader *ret = g_slice_new0 (GstBitReader);

  ret->data = data;
  ret->size = size;

  return ret;
}

/**
 * gst_bit_reader_new_from_buffer:
 * @buffer: Buffer from which the #GstBitReader should read
 *
 * Create a new #GstBitReader instance, which will read from the
 * #GstBuffer @buffer.
 *
 * Returns: a new #GstBitReader instance
 *
 * Since: 0.10.22
 */
GstBitReader *
gst_bit_reader_new_from_buffer (const GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return gst_bit_reader_new (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_bit_reader_free:
 * @reader: a #GstBitReader instance
 *
 * Frees a #GstBitReader instance, which was previously allocated by
 * gst_bit_reader_new() or gst_bit_reader_new_from_buffer().
 * 
 * Since: 0.10.22
 */
void
gst_bit_reader_free (GstBitReader * reader)
{
  g_return_if_fail (reader != NULL);

  g_slice_free (GstBitReader, reader);
}

/**
 * gst_bit_reader_init:
 * @reader: a #GstBitReader instance
 * @data: Data from which the #GstBitReader should read
 * @size: Size of @data in bytes
 *
 * Initializes a #GstBitReader instance to read from @data. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_bit_reader_init (GstBitReader * reader, const guint8 * data, guint size)
{
  g_return_if_fail (reader != NULL);

  reader->data = data;
  reader->size = size;
  reader->byte = reader->bit = 0;
}

/**
 * gst_bit_reader_init_from_buffer:
 * @reader: a #GstBitReader instance
 * @buffer: Buffer from which the #GstBitReader should read
 *
 * Initializes a #GstBitReader instance to read from @buffer. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_bit_reader_init_from_buffer (GstBitReader * reader,
    const GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));

  gst_bit_reader_init (reader, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_bit_reader_set_pos:
 * @reader: a #GstBitReader instance
 * @pos: The new position in bits
 *
 * Sets the new position of a #GstBitReader instance to @pos in bits.
 *
 * Returns: %TRUE if the position could be set successfully, %FALSE
 * otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_bit_reader_set_pos (GstBitReader * reader, guint pos)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (pos > reader->size * 8)
    return FALSE;

  reader->byte = pos / 8;
  reader->bit = pos % 8;

  return TRUE;
}

/**
 * gst_bit_reader_get_pos:
 * @reader: a #GstBitReader instance
 *
 * Returns the current position of a #GstBitReader instance in bits.
 *
 * Returns: The current position of @reader in bits.
 * 
 * Since: 0.10.22
 */
guint
gst_bit_reader_get_pos (const GstBitReader * reader)
{
  g_return_val_if_fail (reader != NULL, 0);

  return reader->byte * 8 + reader->bit;
}

/**
 * gst_bit_reader_get_remaining:
 * @reader: a #GstBitReader instance
 *
 * Returns the remaining number of bits of a #GstBitReader instance.
 *
 * Returns: The remaining number of bits of @reader instance.
 * 
 * Since: 0.10.22
 */
guint
gst_bit_reader_get_remaining (const GstBitReader * reader)
{
  g_return_val_if_fail (reader != NULL, 0);

  return reader->size * 8 - (reader->byte * 8 + reader->bit);
}

/**
 * gst_bit_reader_get_size:
 * @reader: a #GstBitReader instance
 *
 * Returns the total number of bits of a #GstBitReader instance.
 *
 * Returns: The total number of bits of @reader instance.
 * 
 * Since: 0.10.26
 */
guint
gst_bit_reader_get_size (const GstBitReader * reader)
{
  g_return_val_if_fail (reader != NULL, 0);

  return reader->size * 8;
}

/**
 * gst_bit_reader_skip:
 * @reader: a #GstBitReader instance
 * @nbits: the number of bits to skip
 *
 * Skips @nbits bits of the #GstBitReader instance.
 *
 * Returns: %TRUE if @nbits bits could be skipped, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_bit_reader_skip (GstBitReader * reader, guint nbits)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (gst_bit_reader_get_remaining (reader) < nbits)
    return FALSE;

  reader->bit += nbits;
  reader->byte += reader->bit / 8;
  reader->bit = reader->bit % 8;

  return TRUE;
}

/**
 * gst_bit_reader_skip_to_byte:
 * @reader: a #GstBitReader instance
 *
 * Skips until the next byte.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_bit_reader_skip_to_byte (GstBitReader * reader)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (reader->byte > reader->size)
    return FALSE;

  if (reader->bit) {
    reader->bit = 0;
    reader->byte++;
  }

  return TRUE;
}

/**
 * gst_bit_reader_get_bits_uint8:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint8 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_get_bits_uint16:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint16 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_get_bits_uint32:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint32 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_get_bits_uint64:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint64 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val and update the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_peek_bits_uint8:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint8 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_peek_bits_uint16:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint16 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_peek_bits_uint32:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint32 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

/**
 * gst_bit_reader_peek_bits_uint64:
 * @reader: a #GstBitReader instance
 * @val: Pointer to a #guint64 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

#define GST_BIT_READER_READ_BITS(bits) \
gboolean \
gst_bit_reader_get_bits_uint##bits (GstBitReader *reader, guint##bits *val, guint nbits) \
{ \
  guint##bits ret = 0; \
  \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  g_return_val_if_fail (nbits <= bits, FALSE); \
  \
  if (reader->byte * 8 + reader->bit + nbits > reader->size * 8) \
    return FALSE; \
  \
  while (nbits > 0) { \
    guint toread = MIN (nbits, 8 - reader->bit); \
    \
    ret <<= toread; \
    ret |= (reader->data[reader->byte] & (0xff >> reader->bit)) >> (8 - toread - reader->bit); \
    \
    reader->bit += toread; \
    if (reader->bit >= 8) { \
      reader->byte++; \
      reader->bit = 0; \
    } \
    nbits -= toread; \
  } \
  \
  *val = ret; \
  return TRUE; \
} \
\
gboolean \
gst_bit_reader_peek_bits_uint##bits (const GstBitReader *reader, guint##bits *val, guint nbits) \
{ \
  GstBitReader tmp; \
  \
  g_return_val_if_fail (reader != NULL, FALSE); \
  tmp = *reader; \
  return gst_bit_reader_get_bits_uint##bits (&tmp, val, nbits); \
}

GST_BIT_READER_READ_BITS (8);
GST_BIT_READER_READ_BITS (16);
GST_BIT_READER_READ_BITS (32);
GST_BIT_READER_READ_BITS (64);
