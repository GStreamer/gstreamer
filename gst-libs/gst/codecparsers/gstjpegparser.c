/*
 *  gstjpegparser.c - JPEG parser for baseline
 *
 *  Copyright (C) 2011 Intel Corporation
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

#include "gstjpegparser.h"

#include <string.h>
#include <gst/base/gstbytereader.h>

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("codecparsers_jpeg", 0,
        "GstJpegCodecParser");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else

#define ensure_debug_category() /* NOOP */

#endif /* GST_DISABLE_GST_DEBUG */

#define DEBUG_PRINT_COMMENT 0

#define CHECK_FAILED(exp, ret) G_STMT_START {   \
    if (!(exp)) {                               \
      result = ret;                             \
      goto wrong_state;                         \
    }                                           \
  } G_STMT_END

#define JPEG_RESULT_CHECK(result) G_STMT_START {        \
    if ((result) != GST_JPEG_PARSER_OK) {               \
      goto wrong_state;                                 \
    }                                                   \
  } G_STMT_END

#define READ_UINT8(reader, val) G_STMT_START {                  \
    if (!gst_byte_reader_get_uint8 ((reader), &(val))) {       \
      GST_WARNING ("failed to read uint8");                     \
      goto failed;                                              \
    }                                                           \
  } G_STMT_END

#define READ_UINT16(reader, val) G_STMT_START {                 \
    if (!gst_byte_reader_get_uint16_be ((reader), &(val))) {   \
      GST_WARNING ("failed to read uint16");                    \
      goto failed;                                              \
    }                                                           \
  } G_STMT_END

#define READ_BYTES(reader, buf, length) G_STMT_START {          \
    const guint8 *vals;                                         \
    if (!gst_byte_reader_get_data(reader, length, &vals)) {     \
      GST_WARNING("failed to read bytes, size:%d", length);     \
      goto failed;                                              \
    }                                                           \
    memcpy(buf, vals, length);                                  \
  } G_STMT_END

static gboolean jpeg_parse_to_next_marker (GstByteReader * reader,
    guint8 * marker);

/* CCITT T.81, Annex K.1 Quantization tables for luminance and chrominance components */
/* only for 8-bit per sample image*/
static const GstJpegQuantTable
default_quant_tables_zigzag[GST_JPEG_MAX_SCAN_COMPONENTS] = {
  /* luma */
  {0, { 0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10, 0x0e,
        0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x27,
        0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25,
        0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33,
        0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40, 0xa8,
        0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0xb5, 0x57,
        0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d, 0x71,
        0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63 } },
  /* chroma */
  {0, { 0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
        0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63 } },
  /* chroma */
  {0, { 0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
        0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
        0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63 } },
  {0, }
};

/* Table K.3: typical Huffman tables for 8-bit precision luminance and chrominance */
/*
0..GST_JPEG_MAX_SCAN_COMPONENTS -1, DC huffman tables
GST_JPEG_MAX_SCAN_COMPONENTS...GST_JPEG_MAX_SCAN_COMPONENTS*2-1, AC huffman tables
*/
static const
GstJpegHuffmanTable default_huf_tables[GST_JPEG_MAX_SCAN_COMPONENTS*2] = {
  /* DC luma */
  { { 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b }
  },
  /* DC chroma */
  { { 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b }
  },
  /* DC chroma */
  { { 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b }
  },
  {  { 0 },
     { 0 }
  },
  /* AC luma */
  { { 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
      0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d },
    { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
      0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
      0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
      0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
      0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
      0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
      0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
      0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
      0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
      0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa}
  },
  /* AC chroma */
  { { 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
      0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77 },
    { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa }
  },
  /* AC chroma */
  { { 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
      0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77 },
    { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa }
  },
  {  { 0 },
     { 0 }
  }
};

static gboolean
jpeg_parse_to_next_marker (GstByteReader * reader, guint8 * marker)
{
  guint8 value;

  while (gst_byte_reader_get_uint8 (reader, &value)) {
    if (value != 0xFF)
      continue;
    while (value == 0xFF) {
      READ_UINT8 (reader, value);
    }
    if (value == 0x00)
      continue;
    *marker = value;
    return TRUE;
  }

failed:
  return FALSE;
}

GstJpegParserResult
gst_jpeg_parse_frame_hdr (GstJpegFrameHdr * frame_hdr,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (data + offset, size - offset);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 val;
  u_int i;

  g_assert (frame_hdr && data && size);
  READ_UINT8 (&bytes_reader, frame_hdr->sample_precision);
  READ_UINT16 (&bytes_reader, frame_hdr->height);
  READ_UINT16 (&bytes_reader, frame_hdr->width);
  READ_UINT8 (&bytes_reader, frame_hdr->num_components);
  CHECK_FAILED (frame_hdr->num_components <= GST_JPEG_MAX_SCAN_COMPONENTS,
      GST_JPEG_PARSER_ERROR);
  for (i = 0; i < frame_hdr->num_components; i++) {
    READ_UINT8 (&bytes_reader, frame_hdr->components[i].identifier);
    READ_UINT8 (&bytes_reader, val);
    frame_hdr->components[i].horizontal_factor = (val >> 4) & 0x0F;
    frame_hdr->components[i].vertical_factor = (val & 0x0F);
    READ_UINT8 (&bytes_reader, frame_hdr->components[i].quant_table_selector);
    CHECK_FAILED ((frame_hdr->components[i].horizontal_factor <= 4 &&
            frame_hdr->components[i].vertical_factor <= 4 &&
            frame_hdr->components[i].quant_table_selector < 4),
        GST_JPEG_PARSER_ERROR);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_ERROR;

wrong_state:
  return result;
}

GstJpegParserResult
gst_jpeg_parse_scan_hdr (GstJpegScanHdr * scan_hdr,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (data + offset, size - offset);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 val;
  u_int i;

  g_assert (scan_hdr && data && size);
  READ_UINT8 (&bytes_reader, scan_hdr->num_components);
  CHECK_FAILED (scan_hdr->num_components <= GST_JPEG_MAX_SCAN_COMPONENTS,
      GST_JPEG_PARSER_BROKEN_DATA);
  for (i = 0; i < scan_hdr->num_components; i++) {
    READ_UINT8 (&bytes_reader,
        scan_hdr->components[i].component_selector);
    READ_UINT8 (&bytes_reader, val);
    scan_hdr->components[i].dc_selector = (val >> 4) & 0x0F;
    scan_hdr->components[i].ac_selector = val & 0x0F;
    g_assert (scan_hdr->components[i].dc_selector < 4 &&
        scan_hdr->components[i].ac_selector < 4);
    CHECK_FAILED ((scan_hdr->components[i].dc_selector < 4 &&
            scan_hdr->components[i].ac_selector < 4),
        GST_JPEG_PARSER_BROKEN_DATA);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_ERROR;

wrong_state:
  return result;
}

GstJpegParserResult
gst_jpeg_parse_huffman_table (
    GstJpegHuffmanTable huf_tables[GST_JPEG_MAX_SCAN_COMPONENTS*2],
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (data+offset, size-offset);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  GstJpegHuffmanTable * huf_table;
  guint8 tmp_val;
  gboolean is_dc;
  guint8 table_index;
  guint32 value_count;
  u_int i;

  g_assert (huf_tables && data && size);
  while (gst_byte_reader_get_remaining (&bytes_reader)) {
    READ_UINT8 (&bytes_reader, tmp_val);
    is_dc = !((tmp_val >> 4) & 0x0F);
    table_index = (tmp_val & 0x0F);
    CHECK_FAILED (table_index < GST_JPEG_MAX_SCAN_COMPONENTS,
        GST_JPEG_PARSER_BROKEN_DATA);
    if (is_dc) {
      huf_table = &huf_tables[table_index];
    } else {
      huf_table = &huf_tables[GST_JPEG_MAX_SCAN_COMPONENTS + table_index];
    }
    READ_BYTES (&bytes_reader, huf_table->huf_bits, 16);
    value_count = 0;
    for (i = 0; i < 16; i++)
      value_count += huf_table->huf_bits[i];
    READ_BYTES (&bytes_reader, huf_table->huf_values, value_count);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_ERROR;

wrong_state:
  return result;
}

GstJpegParserResult
gst_jpeg_parse_quant_table (
    GstJpegQuantTable *quant_tables, guint num_quant_tables,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (data + offset, size - offset);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  GstJpegQuantTable *quant_table;
  guint8 val;
  guint8 table_index;
  guint i;

  g_assert (quant_tables && num_quant_tables && data && size);
  while (gst_byte_reader_get_remaining (&bytes_reader)) {
    READ_UINT8 (&bytes_reader, val);
    table_index = (val & 0x0f);
    CHECK_FAILED (table_index < GST_JPEG_MAX_SCAN_COMPONENTS && table_index < num_quant_tables,
        GST_JPEG_PARSER_BROKEN_DATA);
    quant_table = &quant_tables[table_index];
    quant_table->quant_precision = ((val >> 4) & 0x0f);
    for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
        if (!quant_table->quant_precision) {        /* 8-bit values */
          READ_UINT8(&bytes_reader, val);
          quant_table->quant_table[i] = val;
        } else {                    /* 16-bit values */
          READ_UINT16(&bytes_reader, quant_table->quant_table[i]);
        }
    }
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_ERROR;

wrong_state:
  return result;
}

GstJpegParserResult
gst_jpeg_parse_restart_interval (guint * interval,
    const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (data + offset, size - offset);
  guint16 tmp;

  g_assert (interval && data && size);
  READ_UINT16 (&bytes_reader, tmp);
  *interval = tmp;
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_BROKEN_DATA;
}

void
gst_jpeg_get_default_huffman_table (GstJpegHuffmanTable huf_tables[GST_JPEG_MAX_SCAN_COMPONENTS*2])
{
  memcpy(huf_tables,
         default_huf_tables,
         sizeof(GstJpegHuffmanTable)*GST_JPEG_MAX_SCAN_COMPONENTS*2);
}

void
gst_jpeg_get_default_quantization_table (GstJpegQuantTable *quant_tables, guint num_quant_tables)
{
  int i = 1;
  g_assert(quant_tables && num_quant_tables);
  for (i = 0; i < num_quant_tables && i < GST_JPEG_MAX_SCAN_COMPONENTS; i++)
    memcpy(&quant_tables[i],
           &default_quant_tables_zigzag[i],
           sizeof(GstJpegQuantTable));
}

static gint32
jpeg_scan_to_end (const guint8 *start, guint32 size)
{
  const guint8 *pos = start, *end = start + size;

  for (; pos < end; ++pos) {
    if (*pos != 0xFF)
      continue;
    while (*pos == 0xFF && pos + 1 < end)
      ++pos;
    if (*pos == 0x00 || *pos == 0xFF ||
        (*pos >= GST_JPEG_MARKER_RST_MIN && *pos <= GST_JPEG_MARKER_RST_MAX))
      continue;
    break;
  }
  if (pos >= end)
    return size;
  return pos - start - 1;
}

static GstJpegTypeOffsetSize *
gst_jpeg_segment_new (guint8 marker, guint offset, gint size)
{
    GstJpegTypeOffsetSize *seg;

    if (GST_JPEG_MARKER_SOS == marker)
      seg = g_malloc0 (sizeof (GstJpegScanOffsetSize));
    else
      seg = g_malloc0 (sizeof (GstJpegTypeOffsetSize));
    seg->type = marker;
    seg->offset = offset;
    seg->size = size;
    return seg;
}

GList *
gst_jpeg_parse(const guint8 * data, gsize size, guint offset)
{
  GstByteReader bytes_reader;
  guint8 marker;
  guint16 header_length;
  GList *segments = NULL;
  GstJpegTypeOffsetSize *seg;
  const guint8 *scan_start;
  gint scan_size = 0;

  size -= offset;

  if ((gssize)size <= 0) {
      GST_DEBUG ("Can't parse from offset %d, buffer is too small", offset);
      return NULL;
  }

  gst_byte_reader_init (&bytes_reader, &data[offset], size);

  /* read SOI */
  if (!jpeg_parse_to_next_marker (&bytes_reader, &marker) ||
      marker != GST_JPEG_MARKER_SOI)
    goto failed;

  while (jpeg_parse_to_next_marker (&bytes_reader, &marker)) {
    if (marker == GST_JPEG_MARKER_EOI)
      break;

    READ_UINT16 (&bytes_reader, header_length);
    if (header_length < 2 ||
        gst_byte_reader_get_remaining (&bytes_reader) < header_length - 2)
      goto failed;

    seg = gst_jpeg_segment_new (marker,
                                gst_byte_reader_get_pos(&bytes_reader) + offset,
                                header_length - 2);
    segments = g_list_prepend(segments, seg);
    gst_byte_reader_skip (&bytes_reader, header_length - 2);

    if (seg->type == GST_JPEG_MARKER_SOS) {
      GstJpegScanOffsetSize * const scan_seg = (GstJpegScanOffsetSize *)seg;
      scan_start = gst_byte_reader_peek_data_unchecked (&bytes_reader);
      scan_size = jpeg_scan_to_end (scan_start,
          gst_byte_reader_get_remaining (&bytes_reader));
      if (scan_size <= 0)
        break;

      scan_seg->data_offset = gst_byte_reader_get_pos (&bytes_reader) + offset;
      scan_seg->data_size = scan_size;
      gst_byte_reader_skip (&bytes_reader, scan_size);
    }
  }
  return g_list_reverse (segments);

failed:
  {
    GST_WARNING ("Failed to parse");
    return g_list_reverse (segments);
  }
}
