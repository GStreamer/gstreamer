/*
 * GStreamer
 *
 * unit test for h265parse
 *
 * Copyright (C) 2019 Stéphane Cerveau <scerveau@collabora.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>
#include "parser.h"

#define SRC_CAPS_TMPL   "video/x-h265, parsed=(boolean)false"
#define SINK_CAPS_TMPL  "video/x-h265, parsed=(boolean)true"

GstStaticPadTemplate sinktemplate_bs_au = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) byte-stream, alignment = (string) au")
    );

GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_TMPL)
    );

/* Data generated with:
 *
 * gst-launch-1.0 videotestsrc num-buffers=1 ! video/x-raw,width=16,height=16 ! x265enc ! h265parse ! fakesink
 *
 * x265enc SEI has been dropped.
 *
 */

static guint8 h265_vps[] = {
  0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00,
  0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0x95,
  0x98, 0x09
};

static guint8 h265_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
  0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0xa0, 0x88, 0x45, 0x96,
  0x56, 0x6a, 0xbc, 0xaf, 0xff, 0x00, 0x01, 0x00, 0x01, 0x6a, 0x0c, 0x02, 0x0c,
  0x08, 0x00, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x40
};

static guint8 h265_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc1, 0x73, 0xd0, 0x89
};

static guint8 h265_idr[] = {
  0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf, 0x06, 0xb8, 0xcf, 0xbc, 0x65, 0x85,
  0x3b, 0x49, 0xff, 0xd0, 0x2c, 0xff, 0x3b, 0x61, 0x6d, 0x1b, 0xae, 0xf1, 0xf4,
  0x96, 0x15, 0xef, 0x3e, 0xc6, 0x67, 0x3c, 0x0a, 0xd0, 0x6a, 0xb9, 0xfb, 0xf8,
  0xb4, 0xb8, 0x4a, 0x4c, 0x4e, 0xe2, 0xf6, 0xb0, 0x29, 0x41, 0x4e, 0x14, 0xe8,
  0x1f, 0x41, 0x58, 0xcb, 0x7a, 0x94, 0xdc, 0xba, 0x3d, 0x2e, 0xe0, 0x83, 0x4d,
  0x3c, 0x3d, 0x2d, 0x70, 0xd1, 0xc4, 0x3d, 0x65, 0xf8, 0x3a, 0xe3, 0xdf, 0xb1,
  0xf1, 0x1c, 0x48, 0x45, 0x63, 0x5b, 0x55, 0x0e, 0x0d, 0xef, 0xfc, 0x07, 0xd3,
  0xce, 0x14, 0xc2, 0xac, 0x79, 0xd6, 0x1c, 0x44, 0x2c, 0xbd, 0x00, 0xff, 0xe5,
  0x0c, 0x09, 0x3a, 0x3b, 0x53, 0xa8, 0x58, 0xb5, 0xb0, 0x29, 0xe6, 0x64, 0x14,
  0x3a, 0xec, 0x8c, 0x7d, 0xd9, 0x19, 0xb4, 0xc2, 0x75, 0x37, 0xa2, 0x64, 0xa3,
  0x1f, 0x26, 0x78, 0xe0, 0xa4, 0xde, 0xed, 0xb1, 0x52, 0x67, 0x90, 0xf1, 0x8e,
  0xf9, 0x99, 0xa8, 0x9e, 0xfa, 0x55, 0xfc, 0x92, 0x3d, 0xd1, 0x03, 0xff, 0xff,
  0xf7, 0x79, 0xaf, 0xa5, 0x90, 0x72, 0x35, 0x4e, 0x64, 0x16, 0x48, 0xa8, 0x28,
  0xc4, 0xcf, 0x51, 0x83, 0x78, 0x6d, 0x90, 0x3a, 0xdf, 0xff, 0xb1, 0x1b, 0xb4,
  0x3e, 0xa5, 0xd3, 0xc9, 0x2b, 0x75, 0x16, 0x01, 0x16, 0xa6, 0xc5, 0x1d, 0x1e,
  0xd6, 0x63, 0x0c, 0xba, 0x2f, 0x77, 0x58, 0x5a, 0x4c, 0xb6, 0x49, 0x63, 0xb4,
  0xa5, 0xb3, 0x25, 0x1b, 0xfd, 0xea, 0x13, 0x8b, 0xb3, 0x8f, 0x42, 0x81, 0xa1,
  0x89, 0xe1, 0x36, 0x80, 0x11, 0x3c, 0x88, 0x84, 0x29, 0x51, 0x59, 0x2c, 0xb2,
  0x9c, 0x90, 0xa5, 0x12, 0x80, 0x2d, 0x16, 0x61, 0x8e, 0xf1, 0x28, 0xba, 0x0f,
  0x71, 0xdf, 0x7b, 0xdb, 0xd7, 0xb0, 0x3d, 0xa1, 0xbe, 0x4f, 0x7c, 0xcf, 0x09,
  0x73, 0xe1, 0x10, 0xea, 0x64, 0x96, 0x89, 0x5d, 0x7e, 0x7f, 0x26, 0x18, 0x43,
  0xbb, 0x0d, 0x2c, 0x95, 0xaa, 0xec, 0x03, 0x9d, 0x55, 0x56, 0xdf, 0xd3, 0x7e,
  0x4f, 0xf7, 0x47, 0x60, 0x89, 0x35, 0x6e, 0x08, 0x9a, 0xcf, 0x11, 0x26, 0xc3,
  0xec, 0x31, 0x23, 0xca, 0x51, 0x10, 0x80
};
};

static const gchar *ctx_suite;
static gboolean ctx_codec_data;

/* A single access unit comprising of VPS, SPS, PPS and IDR frame */
static gboolean
verify_buffer_bs_au (buffer_verify_data_s * vdata, GstBuffer * buffer)
{
  GstMapInfo map;

  fail_unless (ctx_sink_template == &sinktemplate_bs_au);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  fail_unless (map.size > 4);

  if (vdata->buffer_counter == 0) {
    guint8 *data = map.data;

    /* VPS, SPS, PPS */
    fail_unless (map.size == vdata->data_to_verify_size +
        ctx_headers[0].size + ctx_headers[1].size + ctx_headers[2].size);

    fail_unless (memcmp (data, ctx_headers[0].data, ctx_headers[0].size) == 0);
    data += ctx_headers[0].size;
    fail_unless (memcmp (data, ctx_headers[1].data, ctx_headers[1].size) == 0);
    data += ctx_headers[1].size;
    fail_unless (memcmp (data, ctx_headers[2].data, ctx_headers[2].size) == 0);
    data += ctx_headers[2].size;

    /* IDR frame */
    fail_unless (memcmp (data, vdata->data_to_verify,
            vdata->data_to_verify_size) == 0);
  } else {
    /* IDR frame */
    fail_unless (map.size == vdata->data_to_verify_size);

    fail_unless (memcmp (map.data, vdata->data_to_verify, map.size) == 0);
  }

  gst_buffer_unmap (buffer, &map);
  return TRUE;
}

GST_START_TEST (test_parse_normal)
{
  gst_parser_test_normal (h265_idr, sizeof (h265_idr));
}

GST_END_TEST;


GST_START_TEST (test_parse_drain_single)
{
  gst_parser_test_drain_single (h265_idr, sizeof (h265_idr));
}

GST_END_TEST;


GST_START_TEST (test_parse_split)
{
  gst_parser_test_split (h265_idr, sizeof (h265_idr));
}

GST_END_TEST;


#define structure_get_int(s,f) \
    (g_value_get_int(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_int_equals(s,field,num) \
    fail_unless_equals_int (structure_get_int(s,field), num)

#define structure_get_string(s,f) \
    (g_value_get_string(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_string_equals(s,field,name) \
    fail_unless_equals_string (structure_get_string(s,field), name)

GST_START_TEST (test_parse_detect_stream)
{
  GstCaps *caps;
  GstStructure *s;

  caps = gst_parser_test_get_output_caps (h265_idr, sizeof (h265_idr), NULL);
  fail_unless (caps != NULL);

  /* Check that the negotiated caps are as expected */
  GST_DEBUG ("output caps: %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-h265"));
  fail_unless_structure_field_int_equals (s, "width", 16);
  fail_unless_structure_field_int_equals (s, "height", 16);
  fail_unless_structure_field_string_equals (s, "stream-format", "byte-stream");
  fail_unless_structure_field_string_equals (s, "alignment", "au");
  fail_unless_structure_field_string_equals (s, "profile", "main");
  fail_unless_structure_field_string_equals (s, "tier", "main");
  fail_unless_structure_field_string_equals (s, "level", "2.1");

  gst_caps_unref (caps);
}

GST_END_TEST;

static Suite *
h265parse_suite (void)
{
  Suite *s = suite_create (ctx_suite);
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_normal);
  tcase_add_test (tc_chain, test_parse_drain_single);
  tcase_add_test (tc_chain, test_parse_split);
  tcase_add_test (tc_chain, test_parse_detect_stream);

  return s;
}

int
main (int argc, char **argv)
{
  int nf = 0;

  Suite *s;
  //SRunner *sr;

  gst_check_init (&argc, &argv);


  /* init test context */
  ctx_factory = "h265parse";
  ctx_sink_template = &sinktemplate_bs_au;
  ctx_src_template = &srctemplate;
  /* no timing info to parse */
  ctx_headers[0].data = h265_vps;
  ctx_headers[0].size = sizeof (h265_vps);
  ctx_headers[1].data = h265_sps;
  ctx_headers[1].size = sizeof (h265_sps);
  ctx_headers[2].data = h265_pps;
  ctx_headers[2].size = sizeof (h265_pps);
  ctx_verify_buffer = verify_buffer_bs_au;

  /* discard initial vps/sps/pps buffers */
  ctx_discard = 0;
  /* no timing info to parse */
  ctx_no_metadata = TRUE;
  ctx_codec_data = FALSE;

  ctx_suite = "h265parse_to_bs_au";
  s = h265parse_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_to_bs_au.c");

  return nf;
}
