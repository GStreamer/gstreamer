/* GStreamer
 *
 * unit test for h265seiinserter
 *
 * Copyright (C) 2026 Fluendo S.A.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/gsth274.h>
#include <gst/video/gstvideodscmeta.h>

static const guint8 h265_128x128_vps[] = {
  0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01,
  0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
  0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
  0x3f, 0x95, 0x98, 0x09
};

static const guint8 h265_128x128_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01,
  0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0xa0, 0x10,
  0x20, 0x20, 0x59, 0x65, 0x66, 0x92, 0x4c, 0xaf,
  0xff, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00,
  0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x1e,
  0x08
};

static const guint8 h265_128x128_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc1, 0x72,
  0xb4, 0x22, 0x40
};

static const guint8 h265_128x128_slice_idr_n_lp[] = {
  0x00, 0x00, 0x00, 0x01, 0x28, 0x01, 0xaf, 0x0e,
  0xe0, 0x34, 0x82, 0x15, 0x84, 0xf4, 0x70, 0x4f,
  0xff, 0xed, 0x41, 0x3f, 0xff, 0xe4, 0xcd, 0xc4,
  0x7c, 0x03, 0x0c, 0xc2, 0xbb, 0xb0, 0x74, 0xe5,
  0xef, 0x4f, 0xe1, 0xa3, 0xd4, 0x00, 0x02, 0xc2
};

static GstBuffer *
create_test_au (void)
{
  gsize total_size = sizeof (h265_128x128_vps) + sizeof (h265_128x128_sps) +
      sizeof (h265_128x128_pps) + sizeof (h265_128x128_slice_idr_n_lp);
  GstBuffer *buf = gst_buffer_new_and_alloc (total_size);
  gsize offset = 0;

  gst_buffer_fill (buf, offset, h265_128x128_vps, sizeof (h265_128x128_vps));
  offset += sizeof (h265_128x128_vps);
  gst_buffer_fill (buf, offset, h265_128x128_sps, sizeof (h265_128x128_sps));
  offset += sizeof (h265_128x128_sps);
  gst_buffer_fill (buf, offset, h265_128x128_pps, sizeof (h265_128x128_pps));
  offset += sizeof (h265_128x128_pps);
  gst_buffer_fill (buf, offset, h265_128x128_slice_idr_n_lp,
      sizeof (h265_128x128_slice_idr_n_lp));

  return buf;
}

static void
count_dsc_sei_payloads (GstBuffer * buffer, guint * dsci, guint * dscs,
    guint * dscv)
{
  GstH265Parser *parser;
  GstMapInfo map;
  GstH265NalUnit nalu;
  GstH265ParserResult pres;

  *dsci = 0;
  *dscs = 0;
  *dscv = 0;

  parser = gst_h265_parser_new ();

  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));

  pres = gst_h265_parser_identify_nalu (parser, map.data, 0, map.size, &nalu);
  if (pres == GST_H265_PARSER_NO_NAL_END)
    pres = GST_H265_PARSER_OK;

  while (pres == GST_H265_PARSER_OK) {
    if (nalu.type == GST_H265_NAL_PREFIX_SEI
        || nalu.type == GST_H265_NAL_SUFFIX_SEI) {
      GArray *messages = NULL;
      guint i;

      fail_unless_equals_int (gst_h265_parser_parse_sei (parser, &nalu,
              &messages), GST_H265_PARSER_OK);
      fail_unless (messages != NULL);

      for (i = 0; i < messages->len; i++) {
        GstH265SEIMessage *msg =
            &g_array_index (messages, GstH265SEIMessage, i);

        switch (msg->payloadType) {
          case GST_H265_SEI_DIGITALLY_SIGNED_CONTENT_INITIALIZATION:
            (*dsci)++;
            break;
          case GST_H265_SEI_DIGITALLY_SIGNED_CONTENT_SELECTION:
            (*dscs)++;
            break;
          case GST_H265_SEI_DIGITALLY_SIGNED_CONTENT_VERIFICATION:
            (*dscv)++;
            break;
          default:
            break;
        }
      }

      g_array_unref (messages);
    }

    pres = gst_h265_parser_identify_nalu (parser, map.data,
        nalu.offset + nalu.size, map.size, &nalu);
    if (pres == GST_H265_PARSER_NO_NAL_END)
      pres = GST_H265_PARSER_OK;
  }

  gst_buffer_unmap (buffer, &map);
  gst_h265_parser_free (parser);
}

GST_START_TEST (test_h265_seiinserter_dsc_init_selection)
{
  GstHarness *h = gst_harness_new_with_padnames ("h265seiinserter", "sink",
      "src");
  GstBuffer *inbuf, *outbuf;
  GstH274DigitallySignedContentInitialization dsci = { 0, };
  GstH274DigitallySignedContentSelection dscs = { 0, };
  guint dsci_count, dscs_count, dscv_count;

  gst_harness_set_src_caps_str (h,
      "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au");

  inbuf = create_test_au ();

  dsci.id = 1;
  dsci.hash_method_type = 0;
  dsci.num_verification_substreams = 1;
  dsci.key_source_uri = (gchar *) "https://key.com";

  dscs.id = 1;
  dscs.verification_substream_id = 0;

  gst_buffer_add_video_dsc_initialization_meta (inbuf, &dsci);
  gst_buffer_add_video_dsc_selection_meta (inbuf, &dscs);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  count_dsc_sei_payloads (outbuf, &dsci_count, &dscs_count, &dscv_count);

  fail_unless_equals_int (dsci_count, 1);
  fail_unless_equals_int (dscs_count, 1);
  fail_unless_equals_int (dscv_count, 0);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h265_seiinserter_dsc_selection_only)
{
  GstHarness *h = gst_harness_new_with_padnames ("h265seiinserter", "sink",
      "src");
  GstBuffer *inbuf, *outbuf;
  GstH274DigitallySignedContentSelection dscs = { 0, };
  guint dsci_count, dscs_count, dscv_count;

  gst_harness_set_src_caps_str (h,
      "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au");

  inbuf = create_test_au ();

  dscs.id = 1;
  dscs.verification_substream_id = 0;

  gst_buffer_add_video_dsc_selection_meta (inbuf, &dscs);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  count_dsc_sei_payloads (outbuf, &dsci_count, &dscs_count, &dscv_count);

  fail_unless_equals_int (dsci_count, 0);
  fail_unless_equals_int (dscs_count, 1);
  fail_unless_equals_int (dscv_count, 0);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h265_seiinserter_dsc_selection_verification)
{
  GstHarness *h = gst_harness_new_with_padnames ("h265seiinserter", "sink",
      "src");
  GstBuffer *inbuf, *outbuf;
  GstH274DigitallySignedContentSelection dscs = { 0, };
  GstH274DigitallySignedContentVerification dscv = { 0, };
  guint8 signature[] = { 0xaa };
  guint dsci_count, dscs_count, dscv_count;

  gst_harness_set_src_caps_str (h,
      "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au");

  inbuf = create_test_au ();

  dscs.id = 1;
  dscs.verification_substream_id = 0;

  dscv.id = 1;
  dscv.verification_substream_id = 0;
  dscv.signature_length_in_octets_minus1 = 0;
  dscv.signature = signature;
  dscv.signed_content_end_flag = 1;

  gst_buffer_add_video_dsc_selection_meta (inbuf, &dscs);
  gst_buffer_add_video_dsc_verification_meta (inbuf, &dscv);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  count_dsc_sei_payloads (outbuf, &dsci_count, &dscs_count, &dscv_count);

  fail_unless_equals_int (dsci_count, 0);
  fail_unless_equals_int (dscs_count, 1);
  fail_unless_equals_int (dscv_count, 1);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
h265seiinserter_suite (void)
{
  Suite *s = suite_create ("h265seiinserter");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_h265_seiinserter_dsc_init_selection);
  tcase_add_test (tc, test_h265_seiinserter_dsc_selection_only);
  tcase_add_test (tc, test_h265_seiinserter_dsc_selection_verification);

  return s;
}

GST_CHECK_MAIN (h265seiinserter);
