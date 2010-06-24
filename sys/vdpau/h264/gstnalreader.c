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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstnalreader.h"

static gboolean gst_nal_reader_read (GstNalReader * reader, guint nbits);

/**
 * SECTION:gstnalreader
 * @short_description: Bit reader which automatically skips
 * emulation_prevention bytes
 *
 * #GstNalReader provides a bit reader which automatically skips
 * emulation_prevention bytes. It provides functions for reading any number of bits
 * into 8, 16, 32 and 64 bit variables. It also provides functions for reading
 * Exp-Golomb values.
 */

/**
 * gst_nal_reader_new:
 * @data: Data from which the #GstNalReader should read
 * @size: Size of @data in bytes
 *
 * Create a new #GstNalReader instance, which will read from @data.
 *
 * Returns: a new #GstNalReader instance
 *
 * Since: 0.10.22
 */
GstNalReader *
gst_nal_reader_new (const guint8 * data, guint size)
{
  GstNalReader *ret = g_slice_new0 (GstNalReader);

  ret->data = data;
  ret->size = size;

  ret->first_byte = 0xff;
  ret->cache = 0xff;

  return ret;
}

/**
 * gst_nal_reader_new_from_buffer:
 * @buffer: Buffer from which the #GstNalReader should read
 *
 * Create a new #GstNalReader instance, which will read from the
 * #GstBuffer @buffer.
 *
 * Returns: a new #GstNalReader instance
 *
 * Since: 0.10.22
 */
GstNalReader *
gst_nal_reader_new_from_buffer (const GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return gst_nal_reader_new (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_nal_reader_free:
 * @reader: a #GstNalReader instance
 *
 * Frees a #GstNalReader instance, which was previously allocated by
 * gst_nal_reader_new() or gst_nal_reader_new_from_buffer().
 * 
 * Since: 0.10.22
 */
void
gst_nal_reader_free (GstNalReader * reader)
{
  g_return_if_fail (reader != NULL);

  g_slice_free (GstNalReader, reader);
}

/**
 * gst_nal_reader_init:
 * @reader: a #GstNalReader instance
 * @data: Data from which the #GstNalReader should read
 * @size: Size of @data in bytes
 *
 * Initializes a #GstNalReader instance to read from @data. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_nal_reader_init (GstNalReader * reader, const guint8 * data, guint size)
{
  g_return_if_fail (reader != NULL);

  reader->data = data;
  reader->size = size;

  reader->byte = 0;
  reader->bits_in_cache = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  reader->first_byte = 0xff;
  reader->cache = 0xff;
}

/**
 * gst_nal_reader_init_from_buffer:
 * @reader: a #GstNalReader instance
 * @buffer: Buffer from which the #GstNalReader should read
 *
 * Initializes a #GstNalReader instance to read from @buffer. This function
 * can be called on already initialized instances.
 * 
 * Since: 0.10.22
 */
void
gst_nal_reader_init_from_buffer (GstNalReader * reader,
    const GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));

  gst_nal_reader_init (reader, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
}

/**
 * gst_nal_reader_skip:
 * @reader: a #GstNalReader instance
 * @nbits: the number of bits to skip
 *
 * Skips @nbits bits of the #GstNalReader instance.
 *
 * Returns: %TRUE if @nbits bits could be skipped, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_nal_reader_skip (GstNalReader * reader, guint nbits)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (G_UNLIKELY (!gst_nal_reader_read (reader, nbits)))
    return FALSE;

  reader->bits_in_cache -= nbits;

  return TRUE;
}

/**
 * gst_nal_reader_skip_to_byte:
 * @reader: a #GstNalReader instance
 *
 * Skips until the next byte.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */
gboolean
gst_nal_reader_skip_to_byte (GstNalReader * reader)
{
  g_return_val_if_fail (reader != NULL, FALSE);

  if (reader->bits_in_cache == 0) {
    if (G_LIKELY ((reader->size - reader->byte) > 0))
      reader->byte++;
    else
      return FALSE;
  }

  reader->bits_in_cache = 0;

  return TRUE;
}

/**
 * gst_nal_reader_get_pos:
 * @reader: a #GstNalReader instance
 *
 * Returns the current position of a GstNalReader instance in bits.
 *
 * Returns: The current position in bits
 *
 */
guint
gst_nal_reader_get_pos (const GstNalReader * reader)
{
  return reader->byte * 8 - reader->bits_in_cache;
}

/**
 * gst_nal_reader_get_remaining:
 * @reader: a #GstNalReader instance
 *
 * Returns the remaining number of bits of a GstNalReader instance.
 *
 * Returns: The remaining number of bits.
 *
 */
guint
gst_nal_reader_get_remaining (const GstNalReader * reader)
{
  return (reader->size - reader->byte) * 8 + reader->bits_in_cache;
}

/**
 * gst_nal_reader_get_bits_uint8:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_get_bits_uint16:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_get_bits_uint32:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_get_bits_uint64:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_peek_bits_uint8:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_peek_bits_uint16:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_peek_bits_uint32:
 * @reader: a #GstNalReader instance
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
 * gst_nal_reader_peek_bits_uint64:
 * @reader: a #GstNalReader instance
 * @val: Pointer to a #guint64 to store the result
 * @nbits: number of bits to read
 *
 * Read @nbits bits into @val but keep the current position.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 * 
 * Since: 0.10.22
 */

static gboolean
gst_nal_reader_read (GstNalReader * reader, guint nbits)
{
  if (G_UNLIKELY (reader->byte * 8 + (nbits - reader->bits_in_cache) >
          reader->size * 8))
    return FALSE;

  while (reader->bits_in_cache < nbits) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (G_UNLIKELY (reader->byte >= reader->size))
      return FALSE;

    byte = reader->data[reader->byte++];

    /* check if the byte is a emulation_prevention_three_byte */
    if (check_three_byte && byte == 0x03 && reader->first_byte == 0x00 &&
        ((reader->cache & 0xff) == 0)) {
      /* next byte goes unconditionally to the cache, even if it's 0x03 */
      check_three_byte = FALSE;
      goto next_byte;
    }
    reader->cache = (reader->cache << 8) | reader->first_byte;
    reader->first_byte = byte;
    reader->bits_in_cache += 8;
  }

  return TRUE;
}

#define GST_NAL_READER_READ_BITS(bits) \
gboolean \
gst_nal_reader_get_bits_uint##bits (GstNalReader *reader, guint##bits *val, guint nbits) \
{ \
  guint shift; \
  \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  g_return_val_if_fail (nbits <= bits, FALSE); \
  \
  if (!gst_nal_reader_read (reader, nbits)) \
    return FALSE; \
  \
  /* bring the required bits down and truncate */ \
  shift = reader->bits_in_cache - nbits; \
  *val = reader->first_byte >> shift; \
  \
  *val |= reader->cache << (8 - shift); \
  /* mask out required bits */ \
  if (nbits < bits) \
    *val &= ((guint##bits)1 << nbits) - 1; \
  \
  reader->bits_in_cache = shift; \
  \
  return TRUE; \
} \
\
gboolean \
gst_nal_reader_peek_bits_uint##bits (const GstNalReader *reader, guint##bits *val, guint nbits) \
{ \
  GstNalReader tmp; \
  \
  g_return_val_if_fail (reader != NULL, FALSE); \
  tmp = *reader; \
  return gst_nal_reader_get_bits_uint##bits (&tmp, val, nbits); \
}

GST_NAL_READER_READ_BITS (8);
GST_NAL_READER_READ_BITS (16);
GST_NAL_READER_READ_BITS (32);
GST_NAL_READER_READ_BITS (64);

/**
 * gst_nal_reader_get_ue:
 * @reader: a #GstNalReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Reads an unsigned Exp-Golomb value into val
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_nal_reader_get_ue (GstNalReader * reader, guint32 * val)
{
  guint i = 0;
  guint8 bit;
  guint32 value;

  if (G_UNLIKELY (!gst_nal_reader_get_bits_uint8 (reader, &bit, 1)))
    return FALSE;

  while (bit == 0) {
    i++;
    if G_UNLIKELY
      ((!gst_nal_reader_get_bits_uint8 (reader, &bit, 1)))
          return FALSE;
  }

  g_return_val_if_fail (i <= 32, FALSE);

  if (G_UNLIKELY (!gst_nal_reader_get_bits_uint32 (reader, &value, i)))
    return FALSE;

  *val = (1 << i) - 1 + value;

  return TRUE;
}

/**
 * gst_nal_reader_peek_ue:
 * @reader: a #GstNalReader instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Read an unsigned Exp-Golomb value into val but keep the current position
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_nal_reader_peek_ue (const GstNalReader * reader, guint32 * val)
{
  GstNalReader tmp;

  g_return_val_if_fail (reader != NULL, FALSE);

  tmp = *reader;
  return gst_nal_reader_get_ue (&tmp, val);
}

/**
 * gst_nal_reader_get_se:
 * @reader: a #GstNalReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Reads a signed Exp-Golomb value into val
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_nal_reader_get_se (GstNalReader * reader, gint32 * val)
{
  guint32 value;

  if (G_UNLIKELY (!gst_nal_reader_get_ue (reader, &value)))
    return FALSE;

  if (value % 2)
    *val = (value / 2) + 1;
  else
    *val = -(value / 2);

  return TRUE;
}

/**
 * gst_nal_reader_peek_se:
 * @reader: a #GstNalReader instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Read a signed Exp-Golomb value into val but keep the current position
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_nal_reader_peek_se (const GstNalReader * reader, gint32 * val)
{
  GstNalReader tmp;

  g_return_val_if_fail (reader != NULL, FALSE);

  tmp = *reader;
  return gst_nal_reader_get_se (&tmp, val);
}
