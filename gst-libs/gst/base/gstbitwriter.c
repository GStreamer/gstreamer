/*
 *  gstbitwriter.c - bitstream writer
 *
 *  Copyright (C) 2013 Intel Corporation
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

#define GST_BIT_WRITER_DISABLE_INLINES

#include "gstbitwriter.h"

/**
 * gst_bit_writer_init:
 * @bitwriter: a #GstBitWriter instance
 * @reserved_bits: reserved bits to allocate data
 *
 * Initializes a #GstBitWriter instance and allocate @reserved_bits
 * data inside.
 *
 * Cleanup function: gst_bit_writer_clear
 */
void
gst_bit_writer_init (GstBitWriter * bitwriter, guint32 reserved_bits)
{
  bitwriter->bit_size = 0;
  bitwriter->data = NULL;
  bitwriter->bit_capacity = 0;
  bitwriter->auto_grow = TRUE;
  if (reserved_bits)
    _gst_bit_writer_check_space (bitwriter, reserved_bits);
}

/**
 * gst_bit_writer_init_fill:
 * @bitwriter: a #GstBitWriter instance
 * @data: allocated data
  * @bits: size of allocated @data in bits
 *
 * Initializes a #GstBitWriter instance with alocated @data and @bit outside.
 *
 * Cleanup function: gst_bit_writer_clear
 */
void
gst_bit_writer_init_fill (GstBitWriter * bitwriter, guint8 * data, guint bits)
{
  bitwriter->bit_size = 0;
  bitwriter->data = data;
  bitwriter->bit_capacity = bits;
  bitwriter->auto_grow = FALSE;
}

/**
 * gst_bit_writer_clear:
 * @bitwriter: a #GstBitWriter instance
 * @free_data: flag to free #GstBitWriter allocated data
 *
 * Clear a #GstBitWriter instance and destroy allocated data inside
 * if @free_data is %TRUE.
 */
void
gst_bit_writer_clear (GstBitWriter * bitwriter, gboolean free_data)
{
  if (bitwriter->auto_grow && bitwriter->data && free_data)
    g_free (bitwriter->data);

  bitwriter->data = NULL;
  bitwriter->bit_size = 0;
  bitwriter->bit_capacity = 0;
}

/**
 * gst_bit_writer_new:
 * @bitwriter: a #GstBitWriter instance
 * @reserved_bits: reserved bits to allocate data
 *
 * Create a #GstBitWriter instance and allocate @reserved_bits data inside.
 *
 * Free-function: gst_bit_writer_free
 *
 * Returns: a new #GstBitWriter instance
 */
GstBitWriter *
gst_bit_writer_new (guint32 reserved_bits)
{
  GstBitWriter *ret = g_slice_new0 (GstBitWriter);

  gst_bit_writer_init (ret, reserved_bits);

  return ret;
}

/**
 * gst_bit_writer_new_fill:
 * @bitwriter: a #GstBitWriter instance
 * @data: allocated data
 * @bits: size of allocated @data in bits
 *
 * Create a #GstBitWriter instance with allocated @data and @bit outside.
 *
 * Free-function: gst_bit_writer_free
 *
 * Returns: a new #GstBitWriter instance
 */
GstBitWriter *
gst_bit_writer_new_fill (guint8 * data, guint bits)
{
  GstBitWriter *ret = g_slice_new0 (GstBitWriter);

  gst_bit_writer_init_fill (ret, data, bits);

  return ret;
}

/**
 * gst_bit_writer_free:
 * @bitwriter: a #GstBitWriter instance
 * @free_data:  flag to free @data which is allocated inside
 *
 * Clear a #GstBitWriter instance and destroy allocated data inside if
 * @free_data is %TRUE
 */
void
gst_bit_writer_free (GstBitWriter * writer, gboolean free_data)
{
  g_return_if_fail (writer != NULL);

  gst_bit_writer_clear (writer, free_data);

  g_slice_free (GstBitWriter, writer);
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
gst_bit_writer_get_size (GstBitWriter * bitwriter)
{
  return _gst_bit_writer_get_size_inline (bitwriter);
}

/**
 * gst_bit_writer_get_data:
 * @bitwriter: a #GstBitWriter instance
 *
 * Get written @data pointer
 *
 * Returns: @data pointer
 */
guint8 *
gst_bit_writer_get_data (GstBitWriter * bitwriter)
{
  return _gst_bit_writer_get_data_inline (bitwriter);
}

/**
 * gst_bit_writer_get_data:
 * @bitwriter: a #GstBitWriter instance
 * @pos: new position of data end
 *
 * Set the new postion of data end which should be the new size of @data.
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

/**
 * gst_bit_writer_put_bytes:
 * @bitwriter: a #GstBitWriter instance
 * @data: pointer of data to write
 * @nbytes: number of bytes to write
 *
 * Write @nbytes bytes of @data to #GstBitWriter.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_bit_writer_put_bytes (GstBitWriter * bitwriter,
    const guint8 * data, guint nbytes)
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
