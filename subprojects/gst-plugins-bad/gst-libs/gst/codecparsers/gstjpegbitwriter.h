/* GStreamer
 *  Copyright (C) 2024 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_JPEG_BIT_WRITER_H__
#define __GST_JPEG_BIT_WRITER_H__

#include <gst/codecparsers/gstjpegparser.h>
#include <gst/codecparsers/codecparsers-prelude.h>

G_BEGIN_DECLS

/**
 * GstJpegBitWriterResult:
 * @GST_JPEG_BIT_WRITER_OK: The writing succeeded
 * @GST_JPEG_BIT_WRITER_INVALID_DATA: The input data to write is invalid
 * @GST_JPEG_BIT_WRITER_NO_MORE_SPACE: The output does not have enough size
 * @GST_JPEG_BIT_WRITER_ERROR: An general error occurred when writing
 *
 * The result of writing JPEG data into bit stream.
 *
 * Since: 1.24
 */
typedef enum
{
  GST_JPEG_BIT_WRITER_OK,
  GST_JPEG_BIT_WRITER_INVALID_DATA,
  GST_JPEG_BIT_WRITER_NO_MORE_SPACE,
  GST_JPEG_BIT_WRITER_ERROR
} GstJpegBitWriterResult;
G_END_DECLS

GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_frame_header     (const GstJpegFrameHdr * frame_hdr,
                                                                 GstJpegMarker marker,
                                                                 guint8 * data,
                                                                 guint * size);
GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_scan_header      (const GstJpegScanHdr * scan_hdr,
                                                                 guint8 * data,
                                                                 guint * size);
GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_huffman_table    (const GstJpegHuffmanTables * huff_tables,
                                                                 guint8 * data,
                                                                 guint * size);
GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_quantization_table (const GstJpegQuantTables * quant_tables,
                                                                   guint8 * data,
                                                                   guint * size);
GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_restart_interval (guint16 interval,
                                                                 guint8 * data,
                                                                 guint * size);
GST_CODEC_PARSERS_API
GstJpegBitWriterResult     gst_jpeg_bit_writer_segment_with_data (GstJpegMarker marker,
                                                                  guint8 * seg_data,
                                                                  guint seg_size,
                                                                  guint8 * data,
                                                                  guint * size);
#endif /* __GST_JPEG_BIT_WRITER_H__ */
