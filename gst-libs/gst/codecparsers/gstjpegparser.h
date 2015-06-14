/*
 *  GStreamer JPEG parser
 *  Copyright (C) 2011-2012 Intel Corporation
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

/**
 * GST_JPEG_MAX_FRAME_COMPONENTS:
 *
 * Maximum number of image components in a frame (Nf).
 */
#define GST_JPEG_MAX_FRAME_COMPONENTS   256

/**
 * GST_JPEG_MAX_SCAN_COMPONENTS:
 *
 * Maximum number of image components in a scan (Ns).
 */
#define GST_JPEG_MAX_SCAN_COMPONENTS    4

/**
 * GST_JPEG_MAX_QUANT_ELEMENTS:
 *
 * Number of elements in the quantization table.
 */
#define GST_JPEG_MAX_QUANT_ELEMENTS     64

typedef struct _GstJpegQuantTable       GstJpegQuantTable;
typedef struct _GstJpegQuantTables      GstJpegQuantTables;
typedef struct _GstJpegHuffmanTable     GstJpegHuffmanTable;
typedef struct _GstJpegHuffmanTables    GstJpegHuffmanTables;
typedef struct _GstJpegScanComponent    GstJpegScanComponent;
typedef struct _GstJpegScanHdr          GstJpegScanHdr;
typedef struct _GstJpegFrameComponent   GstJpegFrameComponent;
typedef struct _GstJpegFrameHdr         GstJpegFrameHdr;
typedef struct _GstJpegSegment          GstJpegSegment;

/**
 * GstJpegMarkerCode:
 * @GST_JPEG_MARKER_SOF_MIN: Start of frame min marker code
 * @GST_JPEG_MARKER_SOF_MAX: Start of frame max marker code
 * @GST_JPEG_MARKER_DHT: Huffman tabler marker code
 * @GST_JPEG_MARKER_DAC: Arithmetic coding marker code
 * @GST_JPEG_MARKER_RST_MIN: Restart interval min marker code
 * @GST_JPEG_MARKER_RST_MAX: Restart interval max marker code
 * @GST_JPEG_MARKER_SOI: Start of image marker code
 * @GST_JPEG_MARKER_EOI: End of image marker code
 * @GST_JPEG_MARKER_SOS: Start of scan marker code
 * @GST_JPEG_MARKER_DQT: Define quantization table marker code
 * @GST_JPEG_MARKER_DNL: Define number of lines marker code
 * @GST_JPEG_MARKER_DRI: Define restart interval marker code
 * @GST_JPEG_MARKER_APP0: Application segment 0 marker code
 * @GST_JPEG_MARKER_APP1: Application segment 1 marker code
 * @GST_JPEG_MARKER_APP2: Application segment 2 marker code
 * @GST_JPEG_MARKER_APP3: Application segment 3 marker code
 * @GST_JPEG_MARKER_APP4: Application segment 4 marker code
 * @GST_JPEG_MARKER_APP5: Application segment 5 marker code
 * @GST_JPEG_MARKER_APP6: Application segment 6 marker code
 * @GST_JPEG_MARKER_APP7: Application segment 7 marker code
 * @GST_JPEG_MARKER_APP8: Application segment 8 marker code
 * @GST_JPEG_MARKER_APP9: Application segment 9 marker code
 * @GST_JPEG_MARKER_APP10: Application segment 10 marker code
 * @GST_JPEG_MARKER_APP11: Application segment 11 marker code
 * @GST_JPEG_MARKER_APP12: Application segment 12 marker code
 * @GST_JPEG_MARKER_APP13: Application segment 13 marker code
 * @GST_JPEG_MARKER_APP14: Application segment 14 marker code
 * @GST_JPEG_MARKER_APP15: Application segment 15 marker code
 * @GST_JPEG_MARKER_COM: Comment marker code
 *
 * Indicates the type of JPEG segment.
 */
typedef enum {
  GST_JPEG_MARKER_SOF0          = 0xC0,
  GST_JPEG_MARKER_SOF1          = 0xC1,
  GST_JPEG_MARKER_SOF2          = 0xC2,
  GST_JPEG_MARKER_SOF3          = 0xC3,
  GST_JPEG_MARKER_SOF4          = 0xC4,
  GST_JPEG_MARKER_SOF5          = 0xC5,
  GST_JPEG_MARKER_SOF6          = 0xC6,
  GST_JPEG_MARKER_SOF7          = 0xC7,
  GST_JPEG_MARKER_SOF8          = 0xC8,
  GST_JPEG_MARKER_SOF9          = 0xC9,
  GST_JPEG_MARKER_SOF10         = 0xCA,
  GST_JPEG_MARKER_SOF11         = 0xCB,
  GST_JPEG_MARKER_SOF12         = 0xCC,
  GST_JPEG_MARKER_SOF13         = 0xCD,
  GST_JPEG_MARKER_SOF14         = 0xCE,
  GST_JPEG_MARKER_SOF15         = 0xCF,
  GST_JPEG_MARKER_DHT           = 0xC4,
  GST_JPEG_MARKER_DAC           = 0xCC,
  GST_JPEG_MARKER_RST0          = 0xD0,
  GST_JPEG_MARKER_RST1          = 0xD1,
  GST_JPEG_MARKER_RST2          = 0xD2,
  GST_JPEG_MARKER_RST3          = 0xD3,
  GST_JPEG_MARKER_RST4          = 0xD4,
  GST_JPEG_MARKER_RST5          = 0xD5,
  GST_JPEG_MARKER_RST6          = 0xD6,
  GST_JPEG_MARKER_RST7          = 0xD7,
  GST_JPEG_MARKER_SOI           = 0xD8,
  GST_JPEG_MARKER_EOI           = 0xD9,
  GST_JPEG_MARKER_SOS           = 0xDA,
  GST_JPEG_MARKER_DQT           = 0xDB,
  GST_JPEG_MARKER_DNL           = 0xDC,
  GST_JPEG_MARKER_DRI           = 0xDD,
  GST_JPEG_MARKER_APP0          = 0xE0,
  GST_JPEG_MARKER_APP1          = 0xE1,
  GST_JPEG_MARKER_APP2          = 0xE2,
  GST_JPEG_MARKER_APP3          = 0xE3,
  GST_JPEG_MARKER_APP4          = 0xE4,
  GST_JPEG_MARKER_APP5          = 0xE5,
  GST_JPEG_MARKER_APP6          = 0xE6,
  GST_JPEG_MARKER_APP7          = 0xE7,
  GST_JPEG_MARKER_APP8          = 0xE8,
  GST_JPEG_MARKER_APP9          = 0xE9,
  GST_JPEG_MARKER_APP10         = 0xEA,
  GST_JPEG_MARKER_APP11         = 0xEB,
  GST_JPEG_MARKER_APP12         = 0xEC,
  GST_JPEG_MARKER_APP13         = 0xED,
  GST_JPEG_MARKER_APP14         = 0xEE,
  GST_JPEG_MARKER_APP15         = 0xEF,
  GST_JPEG_MARKER_COM           = 0xFE,
} GstJpegMarkerCode;

#define GST_JPEG_MARKER_SOF_MIN GST_JPEG_MARKER_SOF0
#define GST_JPEG_MARKER_SOF_MAX GST_JPEG_MARKER_SOF15

#define GST_JPEG_MARKER_APP_MIN GST_JPEG_MARKER_APP0
#define GST_JPEG_MARKER_APP_MAX GST_JPEG_MARKER_APP15

#define GST_JPEG_MARKER_RST_MIN GST_JPEG_MARKER_RST0
#define GST_JPEG_MARKER_RST_MAX GST_JPEG_MARKER_RST7

/**
 * GstJpegProfile:
 * @GST_JPEG_PROFILE_BASELINE: Baseline DCT
 * @GST_JPEG_PROFILE_EXTENDED: Extended sequential DCT
 * @GST_JPEG_PROFILE_PROGRESSIVE: Progressive DCT
 * @GST_JPEG_PROFILE_LOSSLESS: Lossless (sequential)
 *
 * JPEG encoding processes.
 */
typedef enum {
  GST_JPEG_PROFILE_BASELINE     = 0x00,
  GST_JPEG_PROFILE_EXTENDED     = 0x01,
  GST_JPEG_PROFILE_PROGRESSIVE  = 0x02,
  GST_JPEG_PROFILE_LOSSLESS     = 0x03,
} GstJpegProfile;

/**
 * GstJpegEntropyCodingMode:
 * @GST_JPEG_ENTROPY_CODING_HUFFMAN: Huffman coding
 * @GST_JPEG_ENTROPY_CODING_ARITHMETIC: arithmetic coding
 *
 * JPEG entropy coding mode.
 */
typedef enum {
  GST_JPEG_ENTROPY_CODING_HUFFMAN       = 0x00,
  GST_JPEG_ENTROPY_CODING_ARITHMETIC    = 0x08
} GstJpegEntropyCodingMode;

/**
 * GstJpegQuantTable:
 * @quant_precision: Quantization table element precision (Pq)
 * @quant_table: Quantization table elements (Qk)
 * @valid: If the quantization table is valid, which means it has
 *   already been parsed
 *
 * Quantization table.
 */
struct _GstJpegQuantTable
{
  guint8 quant_precision;
  guint16 quant_table[GST_JPEG_MAX_QUANT_ELEMENTS];
  gboolean valid;
};

/**
 * GstJpegQuantTables:
 * @quant_tables: All quantization tables
 *
 * Helper data structure that holds all quantization tables used to
 * decode an image.
 */
struct _GstJpegQuantTables
{
  GstJpegQuantTable quant_tables[GST_JPEG_MAX_SCAN_COMPONENTS];
};

/**
 * GstJpegHuffmanTable:
 * @huf_bits: Number of Huffman codes of length i + 1 (Li)
 * @huf_vales: Value associated with each Huffman code (Vij)
 * @valid: If the Huffman table is valid, which means it has already
 *   been parsed
 *
 * Huffman table.
 */
struct _GstJpegHuffmanTable
{
  guint8 huf_bits[16];
  guint8 huf_values[256];
  gboolean valid;
};

/**
 * GstJpegHuffmanTables:
 * @dc_tables: DC Huffman tables
 * @ac_tables: AC Huffman tables
 *
 * Helper data structure that holds all AC/DC Huffman tables used to
 * decode an image.
 */
struct _GstJpegHuffmanTables
{
  GstJpegHuffmanTable dc_tables[GST_JPEG_MAX_SCAN_COMPONENTS];
  GstJpegHuffmanTable ac_tables[GST_JPEG_MAX_SCAN_COMPONENTS];
};

/**
 * GstJpegScanComponent:
 * @component_selector: Scan component selector (Csj)
 * @dc_selector: DC entropy coding table destination selector (Tdj)
 * @ac_selector: AC entropy coding table destination selector (Taj)

 * Component-specification parameters.
 */
struct _GstJpegScanComponent
{
    guint8 component_selector;          /* 0 .. 255     */
    guint8 dc_selector;                 /* 0 .. 3       */
    guint8 ac_selector;                 /* 0 .. 3       */
};

/**
 * GstJpegScanHdr:
 * @num_components: Number of image components in scan (Ns)
 * @components: Image components
 *
 * Scan header.
 */
struct _GstJpegScanHdr
{
  guint8 num_components;                /* 1 .. 4       */
  GstJpegScanComponent components[GST_JPEG_MAX_SCAN_COMPONENTS];
};

/**
 * GstJpegFrameComponent:
 * @identifier: Component identifier (Ci)
 * @horizontal_factor: Horizontal sampling factor (Hi)
 * @vertical_factor: Vertical sampling factor (Vi)
 * @quant_table_selector: Quantization table destination selector (Tqi)
 *
 * Component-specification parameters.
 */
struct _GstJpegFrameComponent
{
  guint8 identifier;                    /* 0 .. 255     */
  guint8 horizontal_factor;             /* 1 .. 4       */
  guint8 vertical_factor;               /* 1 .. 4       */
  guint8 quant_table_selector;          /* 0 .. 3       */
};

/**
 * GstJpegFrameHdr:
 * @sample_precision: Sample precision (P)
 * @height: Number of lines (Y)
 * @width: Number of samples per line (X)
 * @num_components: Number of image components in frame (Nf)
 * @components: Image components
 * @restart_interval: Number of MCU in the restart interval (Ri)
 *
 * Frame header.
 */
struct _GstJpegFrameHdr
{
  guint8 sample_precision;              /* 2 .. 16      */
  guint16 width;                        /* 1 .. 65535   */
  guint16 height;                       /* 0 .. 65535   */
  guint8 num_components;                /* 1 .. 255     */
  GstJpegFrameComponent components[GST_JPEG_MAX_FRAME_COMPONENTS];
};

/**
 * GstJpegSegment:
 * @type: The type of the segment that starts at @offset
 * @offset: The offset to the segment start in bytes. This is the
 *   exact start of the segment, no marker code included
 * @size: The size in bytes of the segment, or -1 if the end was not
 *   found. It is the exact size of the segment, no marker code included
 *
 * A structure that contains the type of a segment, its offset and its size.
 */
struct _GstJpegSegment
{
  guint8 marker;
  guint offset;
  gint size;
};

/**
 * gst_jpeg_parse:
 * @seg: (out): pointer to a #GstJpegSegment structure to fill in
 * @data: The data to parse
 * @size: The size of @data
 * @offset: The offset from which to start parsing
 *
 * Parses the JPEG bitstream contained in @data, and returns the
 * detected segment as a #GstJpegSegment.
 *
 * Returns: TRUE if a packet start code was found.
 */
gboolean        gst_jpeg_parse                  (GstJpegSegment * seg,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_parse_frame_header:
 * @frame_hdr: (out): The #GstJpegFrameHdr structure to fill in
 * @data: The data from which to parse the frame header
 * @size: The size of @data
 * @offset: The offset in bytes from which to start parsing @data
 *
 * Parses the @frame_hdr JPEG frame header structure members from @data.
 *
 * Returns: TRUE if the frame header was correctly parsed.
 */
gboolean        gst_jpeg_parse_frame_header     (GstJpegFrameHdr * frame_hdr,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_parse_scan_header:
 * @scan_hdr: (out): The #GstJpegScanHdr structure to fill in
 * @data: The data from which to parse the scan header
 * @size: The size of @data
 * @offset: The offset in bytes from which to start parsing @data
 *
 * Parses the @scan_hdr JPEG scan header structure members from @data.
 *
 * Returns: TRUE if the scan header was correctly parsed
 */
gboolean        gst_jpeg_parse_scan_header      (GstJpegScanHdr * scan_hdr,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_parse_quantization_table:
 * @quant_tables: (out): The #GstJpegQuantizationTable structure to fill in
 * @num_quant_tables: The number of allocated quantization tables in @quant_tables
 * @data: The data from which to parse the quantization table
 * @size: The size of @data
 * @offset: The offset in bytes from which to start parsing @data
 *
 * Parses the JPEG quantization table structure members from @data.
 *
 * Note: @quant_tables represents the complete set of possible
 * quantization tables. However, the parser will only write to the
 * quantization table specified by the table destination identifier
 * (Tq). While doing so, the @valid flag of the specified quantization
 * table will also be set to %TRUE.
 *
 * Returns: TRUE if the quantization table was correctly parsed.
 */
gboolean        gst_jpeg_parse_quant_table      (GstJpegQuantTables *quant_tables,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_parse_huffman_table:
 * @huf_tables: (out): The #GstJpegHuffmanTable structure to fill in
 * @data: The data from which to parse the Huffman table
 * @size: The size of @data
 * @offset: The offset in bytes from which to start parsing @data
 *
 * Parses the JPEG Huffman table structure members from @data.
 *
 * Note: @huf_tables represents the complete set of possible Huffman
 * tables. However, the parser will only write to the Huffman table
 * specified by the table destination identifier (Th). While doing so,
 * the @valid flag of the specified Huffman table will also be set to
 * %TRUE;
 *
 * Returns: TRUE if the Huffman table was correctly parsed.
 */
gboolean        gst_jpeg_parse_huffman_table    (GstJpegHuffmanTables *huf_tables,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_parse_restart_interval:
 * @interval: (out): The parsed restart interval value
 * @data: The data from which to parse the restart interval specification
 * @size: The size of @data
 * @offset: The offset in bytes from which to start parsing @data
 *
 * Returns: TRUE if the restart interval value was correctly parsed.
 */
gboolean        gst_jpeg_parse_restart_interval (guint * interval,
                                                 const guint8 * data,
                                                 gsize size,
                                                 guint offset);

/**
 * gst_jpeg_get_default_huffman_tables:
 * @huf_tables: (out): The default DC/AC Huffman tables to fill in
 *
 * Fills in @huf_tables with the default AC/DC Huffman tables, as
 * specified by the JPEG standard.
 */
void gst_jpeg_get_default_huffman_tables (GstJpegHuffmanTables *huf_tables);

/**
 * gst_jpeg_get_default_quantization_table:
 * @quant_tables: (out): The default luma/chroma quant-tables in zigzag mode
 *
 * Fills in @quant_tables with the default quantization tables, as
 * specified by the JPEG standard.
 */
void gst_jpeg_get_default_quantization_tables (GstJpegQuantTables *quant_tables);

G_END_DECLS

#endif /* GST_JPEG_PARSER_H */
