/*
 *  gstjpegparser.h - JPEG parser for baseline
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

#ifndef GST_JPEG_PARSER_H
#define GST_JPEG_PARSER_H

#ifndef GST_USE_UNSTABLE_API
#  warning "The JPEG parsing library is unstable API and may change in future."
#  warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_JPEG_MAX_COMPONENTS         4
#define GST_JPEG_QUANT_ELEMENTS_SIZE    64

typedef struct _GstJpegQuantTable       GstJpegQuantTable;
typedef struct _GstJpegHuffmanTable     GstJpegHuffmanTable;
typedef struct _GstJpegScan             GstJpegScan;
typedef struct _GstJpegImage            GstJpegImage;

typedef enum
{
  GST_JPEG_PARSER_OK,
  GST_JPEG_PARSER_UNSUPPORTED_PROFILE,
  GST_JPEG_PARSER_NOT_JPEG,
  GST_JPEG_PARSER_BROKEN_DATA,
  GST_JPEG_PARSER_NO_SCAN_FOUND,
  GST_JPEG_PARSER_FRAME_ERROR,
  GST_JPEG_PARSER_SCAN_ERROR,
  GST_JPEG_PARSER_HUFFMAN_ERROR,
  GST_JPEG_PARSER_QUANT_ERROR,
  GST_JPEG_PARSER_DRI_ERROR,
} GstJpegParserResult;

struct _GstJpegQuantTable
{
  guint8 quant_precision;       /* 4 bits; Value 0 indicates 8bit Q values; value 1 indicates 16bit Q values */
  guint8 quant_table[GST_JPEG_QUANT_ELEMENTS_SIZE * 2]; /* 64*8(or 64*16) bits, zigzag mode */
};

struct _GstJpegHuffmanTable
{
  guint8 huf_bits[16];
  guint8 huf_values[256];
};

struct _GstJpegScan
{
  guint8 num_components;        /* 8 bits */
  struct
  {
    guint8 component_selector;  /* 8 bits */
    guint8 dc_selector;         /* 4 bits */
    guint8 ac_selector;         /* 4 bits */
  } components[GST_JPEG_MAX_COMPONENTS];
};

struct _GstJpegImage
{
  guint32 frame_type;
  guint8 sample_precision;      /* 8 bits */
  guint16 height;               /* 16 bits */
  guint16 width;                /* 16 bits */
  guint8 num_components;        /* 8 bits */
  struct
  {
    guint8 identifier;          /* 8 bits */
    guint8 horizontal_factor;   /* 4 bits */
    guint8 vertical_factor;     /* 4 bits */
    guint8 quant_table_selector;        /* 8 bits */
  } components[GST_JPEG_MAX_COMPONENTS];

  GstJpegQuantTable quant_tables[GST_JPEG_MAX_COMPONENTS];
  GstJpegHuffmanTable dc_huf_tables[GST_JPEG_MAX_COMPONENTS];
  GstJpegHuffmanTable ac_huf_tables[GST_JPEG_MAX_COMPONENTS];
  guint32 restart_interval;     /* DRI */

  GstJpegScan current_scan;

  const guint8 *jpeg_pos;
  const guint8 *jpeg_begin;
  const guint8 *jpeg_end;
};

/* parse to first scan and stop there */
GstJpegParserResult     gst_jpeg_parse_image            (GstJpegImage * image,
                                                         const guint8 * buf,
                                                         guint32 size);

GstJpegParserResult     gst_jpeg_parse_next_scan        (GstJpegImage * image);

/* return skip bytes length */
guint32                 gst_jpeg_skip_to_scan_end       (GstJpegImage * image);
const guint8 *          gst_jpeg_get_position           (GstJpegImage * image);
guint32                 gst_jpeg_get_left_size          (GstJpegImage * image);

G_END_DECLS

#endif /* GST_JPEG_PARSER_H */
