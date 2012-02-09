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

#define READ_UINT8(reader, val) G_STMT_START {          \
    if (!gst_byte_reader_get_uint8 ((reader), &val)) {  \
      GST_WARNING ("failed to read uint8");             \
      goto failed;                                      \
    }                                                   \
  } G_STMT_END

#define READ_UINT16(reader, val) G_STMT_START {                 \
    if (!gst_byte_reader_get_uint16_be ((reader), &val)) {      \
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

/* Table B.1: marker code assignments */
enum JPEG_MARKER
{
  GST_JPEG_SOF0 = 0xC0,         /* Frame Profile, Baseline */
  GST_JPEG_SOFF = 0xCF,
  GST_JPEG_DHT = 0xC4,          /* Define Huffman Table */
  GST_JPEG_DAC = 0xCC,          /* Define Arithmetic Coding conditioning */
  GST_JPEG_RST0 = 0xD0,         /* Restart with modulo 8 */
  GST_JPEG_RST7 = 0xD7,
  GST_JPEG_SOI = 0xD8,          /* Start Of Image */
  GST_JPEG_EOI = 0xD9,          /* End Of Image */
  GST_JPEG_SOS = 0xDA,          /* Start Of Scan */
  GST_JPEG_DQT = 0xDB,          /* Define Quantization Table */
  GST_JPEG_DNL = 0xDC,          /* Define Num of Lines */
  GST_JPEG_DRI = 0xDD,          /* Define Restart Interval */
  GST_JPEG_APP0 = 0xE0,         /* Application segments */
  GST_JPEG_APPF = 0xEF,
  GST_JPEG_COM = 0xFE,          /* Comments */
};
static gboolean jpeg_parse_to_next_marker (GstByteReader * reader,
    guint8 * marker);

/* CCITT T.81, Annex K.1 Quantization tables for luminance and chrominance components */
/* only for 8-bit per sample image*/
static const guint8 default_quant_luma_zigzag[64] = {
  0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10, 0x0e,
  0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x27,
  0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25,
  0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33,
  0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40, 0xa8,
  0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0xb5, 0x57,
  0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d, 0x71,
  0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63,
};

static const guint8 default_quant_chroma_zigzag[64] = {
  0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
  0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63
};

/* Table K.3: typical Huffman tables for 8-bit precision luminance and chrominance */
static const guint8 default_dc_luma_bits[16] = {
  0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};

static const guint8 default_dc_luma_vals[16] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const guint8 default_dc_chroma_bits[16] = {
  0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};

static const guint8 default_dc_chroma_vals[16] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const guint8 default_ac_luma_bits[16] = {
  0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125
};

static const guint8 default_ac_luma_vals[256] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
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
  0xf9, 0xfa
};

static const guint8 default_ac_chroma_bits[16] = {
  0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119
};

static const guint8 default_ac_chroma_vals[256] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
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
  0xf9, 0xfa
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

static GstJpegParserResult
jpeg_parse_frame (GstJpegImage * image, const guint8 * buf, guint32 length)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (buf, length);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 val;
  u_int i;

  g_assert (image && buf && length);
  READ_UINT8 (&bytes_reader, image->sample_precision);
  READ_UINT16 (&bytes_reader, image->height);
  READ_UINT16 (&bytes_reader, image->width);
  READ_UINT8 (&bytes_reader, image->num_components);
  CHECK_FAILED (image->num_components <= GST_JPEG_MAX_COMPONENTS,
      GST_JPEG_PARSER_FRAME_ERROR);
  for (i = 0; i < image->num_components; i++) {
    READ_UINT8 (&bytes_reader, image->components[i].identifier);
    READ_UINT8 (&bytes_reader, val);
    image->components[i].horizontal_factor = (val >> 4) & 0x0F;
    image->components[i].vertical_factor = (val & 0x0F);
    READ_UINT8 (&bytes_reader, image->components[i].quant_table_selector);
    CHECK_FAILED ((image->components[i].horizontal_factor <= 4 &&
            image->components[i].vertical_factor <= 4 &&
            image->components[i].quant_table_selector < 4),
        GST_JPEG_PARSER_FRAME_ERROR);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_FRAME_ERROR;

wrong_state:
  return result;
}

static GstJpegParserResult
jpeg_parse_scan (GstJpegImage * image, const guint8 * buf, guint32 length)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (buf, length);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 val;
  u_int i;

  g_assert (image && buf && length);
  READ_UINT8 (&bytes_reader, image->current_scan.num_components);
  CHECK_FAILED (image->current_scan.num_components <= GST_JPEG_MAX_COMPONENTS,
      GST_JPEG_PARSER_SCAN_ERROR);
  for (i = 0; i < image->current_scan.num_components; i++) {
    READ_UINT8 (&bytes_reader,
        image->current_scan.components[i].component_selector);
    READ_UINT8 (&bytes_reader, val);
    image->current_scan.components[i].dc_selector = (val >> 4) & 0x0F;
    image->current_scan.components[i].ac_selector = val & 0x0F;
    g_assert (image->current_scan.components[i].dc_selector < 4 &&
        image->current_scan.components[i].ac_selector < 4);
    CHECK_FAILED ((image->current_scan.components[i].dc_selector < 4 &&
            image->current_scan.components[i].ac_selector < 4),
        GST_JPEG_PARSER_SCAN_ERROR);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_SCAN_ERROR;

wrong_state:
  return result;
}

static GstJpegParserResult
jpeg_parse_huffman_tables (GstJpegImage * image, const guint8 * buf,
    guint32 length)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (buf, length);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  GstJpegHuffmanTable *huf_table;
  guint8 tmp_val;
  gboolean is_dc;
  guint8 table_index;
  guint32 value_count;
  u_int i;

  g_assert (image && buf && length);
  while (gst_byte_reader_get_remaining (&bytes_reader)) {
    READ_UINT8 (&bytes_reader, tmp_val);
    is_dc = !((tmp_val >> 4) & 0x0F);
    table_index = (tmp_val & 0x0F);
    CHECK_FAILED (table_index < GST_JPEG_MAX_COMPONENTS,
        GST_JPEG_PARSER_HUFFMAN_ERROR);
    if (is_dc) {
      huf_table = &image->dc_huf_tables[table_index];
    } else {
      huf_table = &image->ac_huf_tables[table_index];
    }
    READ_BYTES (&bytes_reader, huf_table->huf_bits, 16);
    value_count = 0;
    for (i = 0; i < 16; i++)
      value_count += huf_table->huf_bits[i];
    READ_BYTES (&bytes_reader, huf_table->huf_values, value_count);
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_HUFFMAN_ERROR;

wrong_state:
  return result;
}

static GstJpegParserResult
jpeg_parse_quantization_table (GstJpegImage * image, const guint8 * buf,
    guint32 length)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (buf, length);
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  GstJpegQuantTable *quant_table;
  guint8 val;
  guint8 table_index;

  g_assert (image && buf && length);
  while (gst_byte_reader_get_remaining (&bytes_reader)) {
    READ_UINT8 (&bytes_reader, val);
    table_index = (val & 0x0f);
    CHECK_FAILED (table_index < GST_JPEG_MAX_COMPONENTS,
        GST_JPEG_PARSER_QUANT_ERROR);
    quant_table = &(image->quant_tables[table_index]);
    quant_table->quant_precision = ((val >> 4) & 0x0f);
    if (!quant_table->quant_precision) {        /* 8-bit values */
      READ_BYTES (&bytes_reader, quant_table->quant_table, 64);
    } else {                    /* 16-bit values */
      READ_BYTES (&bytes_reader, quant_table->quant_table, 128);
    }
  }
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_QUANT_ERROR;

wrong_state:
  return result;
}

static GstJpegParserResult
jpeg_parse_restart_interval (GstJpegImage * image, const guint8 * buf,
    guint32 length)
{
  GstByteReader bytes_reader = GST_BYTE_READER_INIT (buf, length);
  guint16 interval;

  g_assert (image && buf && length);
  READ_UINT16 (&bytes_reader, interval);
  image->restart_interval = interval;
  return GST_JPEG_PARSER_OK;

failed:
  return GST_JPEG_PARSER_QUANT_ERROR;
}

static GstJpegParserResult
jpeg_parse_comments (GstJpegImage * image, const guint8 * buf, guint32 length)
{
  g_assert (image);
#if DEBUG_PRINT_COMMENT
  char *comments = (char *) g_malloc0 (length + 1);
  memcpy (comments, buf, length);
  comments[length] = '\0';
  GST_DEBUG ("jpeg comments:%s\n", comments);
  g_free (comments);
#endif
  return GST_JPEG_PARSER_OK;
}

static void
jpeg_set_default_huffman_tables (GstJpegImage * image)
{
  /* luma */
  memcpy (image->dc_huf_tables[0].huf_bits, default_dc_luma_bits, 16);
  memcpy (image->dc_huf_tables[0].huf_values, default_dc_luma_vals, 16);
  memcpy (image->ac_huf_tables[0].huf_bits, default_ac_luma_bits, 16);
  memcpy (image->ac_huf_tables[0].huf_values, default_ac_luma_vals, 256);

  /* chroma */
  memcpy (image->dc_huf_tables[1].huf_bits, default_dc_chroma_bits, 16);
  memcpy (image->dc_huf_tables[1].huf_values, default_dc_chroma_vals, 16);
  memcpy (image->ac_huf_tables[1].huf_bits, default_ac_chroma_bits, 16);
  memcpy (image->ac_huf_tables[1].huf_values, default_ac_chroma_vals, 256);
}

static void
jpeg_set_default_quantization_tables (GstJpegImage * image)
{
  /* luma */
  image->quant_tables[0].quant_precision = 0;
  memcpy (image->quant_tables[0].quant_table, default_quant_luma_zigzag, 64);

  /* chroma */
  image->quant_tables[1].quant_precision = 0;
  memcpy (image->quant_tables[1].quant_table, default_quant_chroma_zigzag, 64);
}

GstJpegParserResult
gst_jpeg_parse_image (GstJpegImage * image, const guint8 * buf, guint32 size)
{
  GstByteReader bytes_reader;
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 marker;
  guint16 header_length;
  const guint8 *header_buf;
  gboolean first_scan_reached = FALSE;
  gboolean has_huffman_tables = FALSE, has_quant_tables = FALSE;

  g_assert (image && buf && size);
  memset (image, 0, sizeof (GstJpegImage));
  gst_byte_reader_init (&bytes_reader, buf, size);

  /* read SOI */
  CHECK_FAILED (jpeg_parse_to_next_marker (&bytes_reader, &marker)
      && GST_JPEG_SOI == marker, GST_JPEG_PARSER_NOT_JPEG);
  image->jpeg_begin = buf + gst_byte_reader_get_pos (&bytes_reader) - 2;
  image->jpeg_end = buf + size;

  while (jpeg_parse_to_next_marker (&bytes_reader, &marker)) {
    if (marker >= GST_JPEG_SOF0 && marker <= GST_JPEG_SOFF
        && marker != GST_JPEG_DHT && marker != GST_JPEG_DAC) {
      g_assert (marker == GST_JPEG_SOF0);
      if (marker > GST_JPEG_SOF0) {
        GST_WARNING ("codecparser_jpeg cannot support this image type:%02x",
            marker);
        result = GST_JPEG_PARSER_UNSUPPORTED_PROFILE;
        goto wrong_state;
      }
      image->frame_type = marker;
      marker = GST_JPEG_SOF0;
    }
    if (marker == GST_JPEG_EOI) {
      image->jpeg_end = buf + gst_byte_reader_get_pos (&bytes_reader);
      break;
    } else {
      READ_UINT16 (&bytes_reader, header_length);
      CHECK_FAILED ((header_length >= 2 &&
              gst_byte_reader_get_remaining (&bytes_reader) + 2 >=
              header_length), GST_JPEG_PARSER_BROKEN_DATA);
      header_length -= 2;
      header_buf = buf + gst_byte_reader_get_pos (&bytes_reader);
    }

    switch (marker) {
      case GST_JPEG_SOF0:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_frame (image, header_buf, header_length));
        break;
      case GST_JPEG_DHT:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_huffman_tables (image, header_buf, header_length));
        has_huffman_tables = TRUE;
        break;
      case GST_JPEG_SOS:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_scan (image, header_buf, header_length));
        first_scan_reached = TRUE;      /* read to first scan stop */
        break;
      case GST_JPEG_DQT:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_quantization_table (image, header_buf, header_length));
        has_quant_tables = TRUE;
        break;
      case GST_JPEG_DRI:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_restart_interval (image, header_buf, header_length));
        break;
      case GST_JPEG_COM:
        JPEG_RESULT_CHECK (result =
            jpeg_parse_comments (image, header_buf, header_length));
        break;
      case GST_JPEG_DAC:
      case GST_JPEG_DNL:
      default:
        /* Unsupported markers, skip them */
        break;
    }
    gst_byte_reader_skip (&bytes_reader, header_length);
    if (first_scan_reached)
      break;
  }

  image->jpeg_pos = buf + gst_byte_reader_get_pos (&bytes_reader);
  if (!first_scan_reached) {
    GST_WARNING ("jpeg no scan was found\n");
    return GST_JPEG_PARSER_NO_SCAN_FOUND;
  }
  if (!has_huffman_tables)
    jpeg_set_default_huffman_tables (image);
  if (!has_quant_tables)
    jpeg_set_default_quantization_tables (image);

  return GST_JPEG_PARSER_OK;

failed:
  GST_WARNING ("jpeg parsing read broken data");
  return GST_JPEG_PARSER_BROKEN_DATA;

wrong_state:
  return result;

}

GstJpegParserResult
gst_jpeg_parse_next_scan (GstJpegImage * image)
{
  GstByteReader bytes_reader;
  GstJpegParserResult result = GST_JPEG_PARSER_OK;
  guint8 marker;
  guint16 header_length;
  const guint8 *header_buf;
  gboolean scan_found = FALSE;

  g_assert (image->jpeg_begin && image->jpeg_end && image->jpeg_pos);
  if (!image->jpeg_begin || !image->jpeg_end || !image->jpeg_pos
      || image->jpeg_pos >= image->jpeg_end)
    return GST_JPEG_PARSER_NO_SCAN_FOUND;
  gst_byte_reader_init (&bytes_reader, image->jpeg_pos,
      image->jpeg_end - image->jpeg_pos);

  while (jpeg_parse_to_next_marker (&bytes_reader, &marker)) {
    if (marker == GST_JPEG_EOI) {
      image->jpeg_end =
          image->jpeg_pos + gst_byte_reader_get_pos (&bytes_reader);
      break;
    }
    if (marker != GST_JPEG_SOS)
      continue;
    READ_UINT16 (&bytes_reader, header_length);
    CHECK_FAILED ((header_length >= 2 &&
            gst_byte_reader_get_remaining (&bytes_reader) + 2 >= header_length),
        GST_JPEG_PARSER_SCAN_ERROR);
    header_length -= 2;
    header_buf = image->jpeg_pos + gst_byte_reader_get_pos (&bytes_reader);
    JPEG_RESULT_CHECK (result =
        jpeg_parse_scan (image, header_buf, header_length));
    scan_found = TRUE;
    break;
  }
  image->jpeg_pos += gst_byte_reader_get_pos (&bytes_reader);
  if (!scan_found)
    return GST_JPEG_PARSER_NO_SCAN_FOUND;

  return GST_JPEG_PARSER_OK;
failed:
  GST_WARNING ("jpeg parsing read broken data");
  return GST_JPEG_PARSER_BROKEN_DATA;

wrong_state:
  return result;
}

guint32
gst_jpeg_skip_to_scan_end (GstJpegImage * image)
{
  GstByteReader bytes_reader;
  guint8 marker;
  guint32 skip_bytes = 0;
  gboolean next_marker_found = FALSE;

  g_assert (image->jpeg_begin && image->jpeg_end && image->jpeg_pos);
  if (!image->jpeg_begin || !image->jpeg_end || !image->jpeg_pos
      || image->jpeg_pos >= image->jpeg_end)
    return 0;
  gst_byte_reader_init (&bytes_reader, image->jpeg_pos,
      image->jpeg_end - image->jpeg_pos);
  while (jpeg_parse_to_next_marker (&bytes_reader, &marker)) {
    if (marker >= GST_JPEG_RST0 && marker <= GST_JPEG_RST7)
      continue;
    next_marker_found = TRUE;
    break;
  }
  if (next_marker_found)
    skip_bytes = gst_byte_reader_get_pos (&bytes_reader) - 2;
  else
    skip_bytes = gst_byte_reader_get_pos (&bytes_reader);
  image->jpeg_pos += skip_bytes;
  return skip_bytes;
}

const guint8 *
gst_jpeg_get_position (GstJpegImage * image)
{
  return image->jpeg_pos;
}

guint32
gst_jpeg_get_left_size (GstJpegImage * image)
{
  if (image->jpeg_end > image->jpeg_pos)
    return image->jpeg_end - image->jpeg_pos;
  return 0;
}
