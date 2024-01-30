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

#include <gst/check/gstcheck.h>
#include <gst/codecparsers/gstjpegbitwriter.h>

GST_START_TEST (test_jpeg_bitwriter_segments)
{
  GstJpegBitWriterResult writer_res;
  gboolean parser_res;
  guint8 data[2048] = { 0, };
  guint8 app_data[14] =
      { 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x02, 0, 0, 0x01, 0, 0x01, 0, 0 };
  GstJpegQuantTables quant_tables = { 0, };
  GstJpegQuantTables quant_tables2 = { 0, };
  GstJpegHuffmanTables huf_tables = { 0, };
  GstJpegHuffmanTables huf_tables2 = { 0, };
  GstJpegFrameHdr frame_hdr;
  GstJpegFrameHdr frame_hdr2 = { 0, };
  GstJpegScanHdr scan_hdr;
  GstJpegScanHdr scan_hdr2 = { 0, };
  GstJpegSegment seg;
  guint size, offset;
  guint i, j;

  offset = 0;
  size = sizeof (data);
  writer_res = gst_jpeg_bit_writer_segment_with_data (GST_JPEG_MARKER_SOI,
      NULL, 0, data, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_segment_with_data (GST_JPEG_MARKER_APP_MIN,
      app_data, sizeof (app_data), data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  gst_jpeg_get_default_quantization_tables (&quant_tables);
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    if (i % 2)
      quant_tables.quant_tables[0].quant_table[i] += 10;

    if (i % 3)
      quant_tables.quant_tables[1].quant_table[i] += 5;

    if (i % 4)
      quant_tables.quant_tables[2].quant_table[i] /= 2;
  }

  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_quantization_table (&quant_tables,
      data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  /* *INDENT-OFF* */
  frame_hdr = (GstJpegFrameHdr) {
    .sample_precision = 8,
    .width = 1920,
    .height = 1080,
    .num_components = 3,
    .components[0] = {
      .identifier = 1,
      .horizontal_factor= 3,
      .vertical_factor = 2,
      .quant_table_selector = 1,
    },
    .components[1] = {
      .identifier = 2,
      .horizontal_factor= 1,
      .vertical_factor = 4,
      .quant_table_selector = 2,
    },
    .components[2] = {
      .identifier = 0,
      .horizontal_factor = 2,
      .vertical_factor = 1,
      .quant_table_selector = 3,
    },
  };
  /* *INDENT-ON* */

  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_frame_header (&frame_hdr,
      GST_JPEG_MARKER_SOF_MIN, data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  gst_jpeg_get_default_huffman_tables (&huf_tables);
  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_huffman_table (&huf_tables,
      data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  /* *INDENT-OFF* */
  scan_hdr = (GstJpegScanHdr) {
    .num_components = 3,
    .components[0] = {
      .component_selector = 85,
      .dc_selector = 2,
      .ac_selector = 1,
    },
    .components[1] = {
      .component_selector = 16,
      .dc_selector = 1,
      .ac_selector = 0,
    },
    .components[2] = {
      .component_selector = 25,
      .dc_selector = 2,
      .ac_selector = 1,
    },
  };
  /* *INDENT-ON* */
  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_scan_header (&scan_hdr,
      data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  offset += size;
  fail_if (offset + 2 >= sizeof (data));

  offset = sizeof (data) - 2;
  size = 2;
  writer_res = gst_jpeg_bit_writer_segment_with_data (GST_JPEG_MARKER_EOI,
      NULL, 0, data + offset, &size);
  fail_if (writer_res != GST_JPEG_BIT_WRITER_OK);

  /* Parse it back and check. */
  /* SOI */
  offset = 0;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_SOI);

  /* APP0 */
  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_APP_MIN);
  fail_if (*(seg.data + seg.offset) * 256 + *(seg.data + seg.offset + 1) !=
      seg.size);
  for (i = 0; i < sizeof (app_data); i++) {
    const guint8 *d = seg.data + seg.offset + 2;
    fail_if (d[i] != app_data[i]);
  }

  /* Quantization tables */
  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_DQT);
  fail_if (*(seg.data + seg.offset) * 256 + *(seg.data + seg.offset + 1) !=
      seg.size);
  parser_res = gst_jpeg_segment_parse_quantization_table (&seg, &quant_tables2);
  fail_if (parser_res != TRUE);

  for (i = 0; i < GST_JPEG_MAX_SCAN_COMPONENTS; i++) {
    GstJpegQuantTable *quant_table = &quant_tables.quant_tables[i];
    GstJpegQuantTable *quant_table2 = &quant_tables2.quant_tables[i];

    fail_if (quant_table->quant_precision != quant_table2->quant_precision);
    fail_if (quant_table->valid != quant_table2->valid);

    for (j = 0; j < GST_JPEG_MAX_QUANT_ELEMENTS; j++)
      fail_if (quant_table->quant_table[j] != quant_table2->quant_table[j]);
  }

  /* SOF */
  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_SOF_MIN);
  fail_if (*(seg.data + seg.offset) * 256 + *(seg.data + seg.offset + 1) !=
      seg.size);
  parser_res = gst_jpeg_segment_parse_frame_header (&seg, &frame_hdr2);
  fail_if (parser_res != TRUE);

  fail_if (frame_hdr.sample_precision != frame_hdr2.sample_precision);
  fail_if (frame_hdr.width != frame_hdr2.width);
  fail_if (frame_hdr.height != frame_hdr2.height);
  fail_if (frame_hdr.num_components != frame_hdr2.num_components);
  for (i = 0; i < frame_hdr.num_components; i++) {
    fail_if (frame_hdr.components[i].identifier !=
        frame_hdr2.components[i].identifier);
    fail_if (frame_hdr.components[i].horizontal_factor !=
        frame_hdr2.components[i].horizontal_factor);
    fail_if (frame_hdr.components[i].vertical_factor !=
        frame_hdr2.components[i].vertical_factor);
    fail_if (frame_hdr.components[i].quant_table_selector !=
        frame_hdr2.components[i].quant_table_selector);
  }

  /* huffman tables */
  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_DHT);
  fail_if (*(seg.data + seg.offset) * 256 + *(seg.data + seg.offset + 1) !=
      seg.size);
  parser_res = gst_jpeg_segment_parse_huffman_table (&seg, &huf_tables2);
  fail_if (parser_res != TRUE);
  fail_if (memcmp (&huf_tables, &huf_tables2, sizeof (huf_tables)) != 0);

  /* Scan header */
  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_SOS);
  parser_res = gst_jpeg_segment_parse_scan_header (&seg, &scan_hdr2);
  fail_if (parser_res != TRUE);

  fail_if (scan_hdr.num_components != scan_hdr2.num_components);
  for (i = 0; i < scan_hdr.num_components; i++) {
    fail_if (scan_hdr.components[i].component_selector !=
        scan_hdr2.components[i].component_selector);
    fail_if (scan_hdr.components[i].dc_selector !=
        scan_hdr2.components[i].dc_selector);
    fail_if (scan_hdr.components[i].ac_selector !=
        scan_hdr2.components[i].ac_selector);
  }

  offset += 2 + seg.size;
  parser_res = gst_jpeg_parse (&seg, data, sizeof (data), offset);
  fail_if (parser_res != TRUE);
  fail_if (seg.marker != GST_JPEG_MARKER_EOI);
}

GST_END_TEST;

static Suite *
jpegbitwriter_suite (void)
{
  Suite *s = suite_create ("jpeg bitwriter library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_jpeg_bitwriter_segments);

  return s;
}

GST_CHECK_MAIN (jpegbitwriter);
