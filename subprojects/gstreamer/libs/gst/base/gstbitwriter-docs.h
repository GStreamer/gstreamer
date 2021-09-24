/*
 *  GStreamer bit writer dummy header for gtk-doc
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

/* This header is not installed, it just contains stuff for gtk-doc to parse,
 * in particular docs and some dummy function declarations for the static
 * inline functions we generate via macros in gstbitwriter.h.
 */

#error "This header should never be included in code, it is only for gtk-doc"

/**
 * gst_bit_writer_put_bits_uint8_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint8 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter without checking whether
 * there is enough space
 */
void gst_bit_writer_put_bits_uint8_unchecked (GstBitWriter *bitwriter, guint8 value, guint nbits);

/**
 * gst_bit_writer_put_bits_uint16_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint16 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter without checking whether
 * there is enough space
 */
void gst_bit_writer_put_bits_uint16_unchecked (GstBitWriter *bitwriter, guint16 value, guint nbits);

/**
 * gst_bit_writer_put_bits_uint32_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint32 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter without checking whether
 * there is enough space
 */
void gst_bit_writer_put_bits_uint32_unchecked (GstBitWriter *bitwriter, guint32 value, guint nbits);

/**
 * gst_bit_writer_put_bits_uint64_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @value: value of #guint64 to write
 * @nbits: number of bits to write
 *
 * Write @nbits bits of @value to #GstBitWriter without checking whether
 * there is enough space
 */
void gst_bit_writer_put_bits_uint64_unchecked (GstBitWriter *bitwriter, guint64 value, guint nbits);

/**
 * gst_bit_writer_put_bytes_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @data: pointer of data to write
 * @nbytes: number of bytes to write
 *
 * Write @nbytes bytes of @data to #GstBitWriter without checking whether
 * there is enough space
 */
void gst_bit_writer_put_bytes_unchecked (GstBitWriter *bitwriter, const guint8 *data, guint nbytes);

/**
 * gst_bit_writer_align_bytes_unchecked:
 * @bitwriter: a #GstBitWriter instance
 * @trailing_bit: trailing bits of last byte, 0 or 1
 *
 * Write trailing bit to align last byte of @data without checking whether there
 * is enough space
 */
void gst_bit_writer_align_bytes_unchecked (GstBitWriter *bitwriter, guint8 trailing_bit);
