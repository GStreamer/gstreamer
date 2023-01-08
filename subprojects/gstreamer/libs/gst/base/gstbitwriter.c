/*
 *  gstbitwriter.c - bitstream writer
 *
 *  Copyright (C) 2013 Intel Corporation
 *  Copyright (C) 2018 Igalia, S.L.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define GST_BIT_WRITER_DISABLE_INLINES
#include "gstbitwriter.h"

#include "gst/glib-compat-private.h"

/**
 * SECTION:gstbitwriter
 * @title: GstBitWriter
 * @short_description: Writes any number of bits into a memory buffer
 *
 * #GstBitWriter provides a bit writer that can write any number of
 * bits into a memory buffer. It provides functions for writing any
 * number of bits into 8, 16, 32 and 64 bit variables.
 */

/**
 * gst_bit_writer_new: (skip)
 *
 * Creates a new, empty #GstBitWriter instance.
 *
 * Free-function: gst_bit_writer_free
 *
 * Returns: (transfer full): a new, empty #GstByteWriter instance
 **/
GstBitWriter *
gst_bit_writer_new (void)
{
  GstBitWriter *ret = g_new0 (GstBitWriter, 1);

  ret->owned = TRUE;
  ret->auto_grow = TRUE;
  return ret;
}

/**
 * gst_bit_writer_new_with_size: (skip)
 * @size: Initial size of data in bytes
 * @fixed: If %TRUE the data can't be reallocated
 *
 * Creates a #GstBitWriter instance with the given initial data size.
 *
 * Free-function: gst_bit_writer_free
 *
 * Returns: (transfer full): a new #GstBitWriter instance
 */
GstBitWriter *
gst_bit_writer_new_with_size (guint size, gboolean fixed)
{
  GstBitWriter *ret = g_new0 (GstBitWriter, 1);

  gst_bit_writer_init_with_size (ret, size, fixed);
  return ret;
}

/**
 * gst_bit_writer_new_with_data: (skip)
 * @data: (array length=size) (transfer none): Memory area for writing
 * @size: Size of @data in bytes
 * @initialized: if %TRUE the complete data can be read from the beginning
 *
 * Creates a new #GstBitWriter instance with the given memory area. If
 * @initialized is %TRUE it is possible to read @size bits from the
 * #GstBitWriter from the beginning.
 *
 * Free-function: gst_bit_writer_free
 *
 * Returns: (transfer full): a new #GstBitWriter instance
 */
GstBitWriter *
gst_bit_writer_new_with_data (guint8 * data, guint size, gboolean initialized)
{
  GstBitWriter *ret = g_new0 (GstBitWriter, 1);

  gst_bit_writer_init_with_data (ret, data, size, initialized);

  return ret;
}

/**
 * gst_bit_writer_init: (skip)
 * @bitwriter: #GstBitWriter instance
 *
 * Initializes @bitwriter to an empty instance.
 **/
void
gst_bit_writer_init (GstBitWriter * bitwriter)
{
  g_return_if_fail (bitwriter != NULL);

  memset (bitwriter, 0, sizeof (GstBitWriter));
  bitwriter->owned = TRUE;
  bitwriter->auto_grow = TRUE;
}

/**
 * gst_bit_writer_init_with_size: (skip)
 * @bitwriter: #GstBitWriter instance
 * @size: the size on bytes to allocate for data
 * @fixed: If %TRUE the data can't be reallocated
 *
 * Initializes a #GstBitWriter instance and allocates the given data
 * @size.
 */
void
gst_bit_writer_init_with_size (GstBitWriter * bitwriter, guint size,
    gboolean fixed)
{
  g_return_if_fail (bitwriter != NULL);

  gst_bit_writer_init (bitwriter);

  _gst_bit_writer_check_remaining (bitwriter, size << 3);

  bitwriter->auto_grow = !fixed;
}

/**
 * gst_bit_writer_init_with_data: (skip)
 * @bitwriter: #GstBitWriter instance
 * @data: (array length=size) (transfer none): Memory area for writing
 * @size: Size of @data in bytes
 * @initialized: If %TRUE the complete data can be read from the beginning
 *
 * Initializes @bitwriter with the given memory area @data. IF
 * @initialized is %TRUE it is possible to read @size bits from the
 * #GstBitWriter from the beginning.
 */
void
gst_bit_writer_init_with_data (GstBitWriter * bitwriter, guint8 * data,
    guint size, gboolean initialized)
{
  g_return_if_fail (bitwriter != NULL);

  gst_bit_writer_init (bitwriter);

  bitwriter->data = data;
  bitwriter->bit_capacity = size * 8;
  bitwriter->bit_size = (initialized) ? size << 3 : 0;
  bitwriter->auto_grow = FALSE;
  bitwriter->owned = FALSE;
}

/**
 * gst_bit_writer_reset:
 * @bitwriter: #GstBitWriter instance
 *
 * Resets @bitwriter and frees the data if it's owned by @bitwriter.
 */
void
gst_bit_writer_reset (GstBitWriter * bitwriter)
{
  g_return_if_fail (bitwriter != NULL);

  if (bitwriter->owned)
    g_free (bitwriter->data);
  memset (bitwriter, 0, sizeof (GstBitWriter));
}

/**
 * gst_bit_writer_reset_and_get_data:
 * @bitwriter: a #GstBitWriter instance
 *
 * Resets @bitwriter and returns the current data.
 *
 * Free-function: g_free
 *
 * Returns: (array) (transfer full): the current data. g_free() after
 *     usage.
 **/
guint8 *
gst_bit_writer_reset_and_get_data (GstBitWriter * bitwriter)
{
  guint8 *data;

  g_return_val_if_fail (bitwriter != NULL, NULL);

  data = bitwriter->data;
  if (bitwriter->owned)
    data = g_memdup2 (data, GST_ROUND_UP_8 (bitwriter->bit_size) >> 3);
  gst_bit_writer_reset (bitwriter);

  return data;
}

/**
 * gst_bit_writer_reset_and_get_buffer:
 * @bitwriter: a #GstBitWriter instance
 *
 * Resets @bitwriter and returns the current data as #GstBuffer.
 *
 * Free-function: gst_buffer_unref
 *
 * Returns: (transfer full): a new allocated #GstBuffer wrapping the
 *     current data. gst_buffer_unref() after usage.
 **/
GstBuffer *
gst_bit_writer_reset_and_get_buffer (GstBitWriter * bitwriter)
{
  GstBuffer *buffer;
  gpointer data;
  gsize size;
  gboolean owned;

  g_return_val_if_fail (bitwriter != NULL, NULL);

  owned = bitwriter->owned;

  size = GST_ROUND_UP_8 (bitwriter->bit_size) >> 3;
  data = gst_bit_writer_reset_and_get_data (bitwriter);

  /* we cannot rely on buffers allocated externally, thus let's dup
   * the data */
  if (data && !owned)
    data = g_memdup2 (data, size);

  buffer = gst_buffer_new ();
  if (data != NULL) {
    gst_buffer_append_memory (buffer,
        gst_memory_new_wrapped (0, data, size, 0, size, data, g_free));
  }

  return buffer;
}

/**
 * gst_bit_writer_free:
 * @bitwriter: (in) (transfer full): #GstBitWriter instance
 *
 * Frees @bitwriter and the allocated data inside.
 */
void
gst_bit_writer_free (GstBitWriter * bitwriter)
{
  g_return_if_fail (bitwriter != NULL);

  gst_bit_writer_reset (bitwriter);
  g_free (bitwriter);
}

/**
 * gst_bit_writer_free_and_get_data:
 * @bitwriter: (in) (transfer full): #GstBitWriter instance
 *
 * Frees @bitwriter without destroying the internal data, which is
 * returned.
 *
 * Free-function: g_free
 *
 * Returns: (array) (transfer full): the current data. g_free() after
 *     usage.
 **/
guint8 *
gst_bit_writer_free_and_get_data (GstBitWriter * bitwriter)
{
  guint8 *data;

  g_return_val_if_fail (bitwriter != NULL, NULL);

  data = gst_bit_writer_reset_and_get_data (bitwriter);
  g_free (bitwriter);

  return data;
}

/**
 * gst_bit_writer_free_and_get_buffer:
 * @bitwriter: (in) (transfer full): #GstBitWriter instance
 *
 * Frees @bitwriter without destroying the internal data, which is
 * returned as #GstBuffer.
 *
 * Free-function: gst_buffer_unref
 *
 * Returns: (transfer full): a new allocated #GstBuffer wrapping the
 *     data inside. gst_buffer_unref() after usage.
 **/
GstBuffer *
gst_bit_writer_free_and_get_buffer (GstBitWriter * bitwriter)
{
  GstBuffer *buffer;

  g_return_val_if_fail (bitwriter != NULL, NULL);

  buffer = gst_bit_writer_reset_and_get_buffer (bitwriter);
  g_free (bitwriter);

  return buffer;
}

/**
 * gst_bit_writer_get_size:
 * @bitwriter: a #GstBitWriter instance
 *
 * Get size of written @data
 *
 * Returns: size of bits written in @data
 */
guint
gst_bit_writer_get_size (const GstBitWriter * bitwriter)
{
  return _gst_bit_writer_get_size_inline (bitwriter);
}

/**
 * gst_bit_writer_get_data:
 * @bitwriter: a #GstBitWriter instance
 *
 * Get written data pointer
 *
 * Returns: (array) (transfer none): data pointer
 */
guint8 *
gst_bit_writer_get_data (const GstBitWriter * bitwriter)
{
  return _gst_bit_writer_get_data_inline (bitwriter);
}

/**
 * gst_bit_writer_get_pos:
 * @bitwriter: a #GstBitWriter instance
 * @pos: The new position in bits
 *
 * Set the new position of data end which should be the new size of @data.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 */
gboolean
gst_bit_writer_set_pos (GstBitWriter * bitwriter, guint pos)
{
  return _gst_bit_writer_set_pos_inline (bitwriter, pos);
}

/**
 * gst_bit_writer_put_bits_uint8:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint8 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */

/**
 * gst_bit_writer_put_bits_uint16:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint16 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */

/**
 * gst_bit_writer_put_bits_uint32:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint32 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */

/**
 * gst_bit_writer_put_bits_uint64:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint64 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */

/* *INDENT-OFF* */
#define GST_BIT_WRITER_WRITE_BITS(bits) \
gboolean \
gst_bit_writer_put_bits_uint##bits (GstBitWriter *bitwriter, guint##bits value, guint nbits) \
{ \
  return _gst_bit_writer_put_bits_uint##bits##_inline (bitwriter, value, nbits); \
}

GST_BIT_WRITER_WRITE_BITS (8)
GST_BIT_WRITER_WRITE_BITS (16)
GST_BIT_WRITER_WRITE_BITS (32)
GST_BIT_WRITER_WRITE_BITS (64)
#undef GST_BIT_WRITER_WRITE_BITS
/* *INDENT-ON* */

/**
 * gst_bit_writer_put_bytes:
 * @bitwriter: a #GstBitWriter instance
 * @data: (array): pointer of data to write
 * @nbytes: number of bytes to write
 *
 * Write @nbytes bytes of @data to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_bit_writer_put_bytes (GstBitWriter * bitwriter, const guint8 * data,
    guint nbytes)
{
  return _gst_bit_writer_put_bytes_inline (bitwriter, data, nbytes);
}

/**
 * gst_bit_writer_align_bytes:
 * @bitwriter: a #GstBitWriter instance
 * @trailing_bit: trailing bits of last byte, 0 or 1
 *
 * Write trailing bit to align last byte of @data. @trailing_bit can
 * only be 1 or 0.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_bit_writer_align_bytes (GstBitWriter * bitwriter, guint8 trailing_bit)
{
  return _gst_bit_writer_align_bytes_inline (bitwriter, trailing_bit);
}
