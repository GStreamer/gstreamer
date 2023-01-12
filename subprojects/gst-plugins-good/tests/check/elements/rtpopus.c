/* GStreamer
 *
 * Copyright (C) 2020 Pexip AS
 *   @author Havard Graff <havard@pexip.com>
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

#include <gst/check/check.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

#define buffer_from_array(a) gst_buffer_new_memdup (a, G_N_ELEMENTS (a))

static guint8 opus_data[] = {
  0xf8, 0xb5, 0x0e, 0x7d, 0x91, 0xcc, 0x05, 0x82,
  0x75, 0x72, 0x48, 0xbd, 0xd3, 0x22, 0x24, 0x2e,
  0x59, 0x63, 0xf8, 0xff, 0x5d, 0x59, 0x27, 0xd8,
  0xad, 0x4b, 0xe8, 0xd7, 0xfa, 0x99, 0xaa, 0x46,
  0xb4, 0xf6, 0x29, 0x16, 0x21, 0x86, 0x2a, 0xb5,
  0x83, 0x7d, 0x3a, 0xce, 0xb3, 0xee, 0x37, 0x3b,
  0xf7, 0xb5, 0x03, 0xe7, 0x13, 0x3b, 0xf6, 0x90,
  0x06, 0xea, 0x79, 0xbe, 0x89, 0xc3, 0x2b, 0x1f,
  0x7f, 0x88, 0x5e, 0xe0, 0xe1, 0x88, 0x59, 0x47,
  0x11, 0x10, 0x94, 0xab, 0x5d, 0xa6, 0x3f, 0x5d,
  0xa7, 0xd7, 0x0e, 0x7d, 0x07, 0x85, 0x0d, 0x2f,
  0x7b, 0x3f, 0xf7, 0xc1, 0x8c, 0xb2, 0xda, 0xac,
  0x79, 0x15, 0xda, 0xc7, 0xd2, 0x6e, 0xcc, 0x88,
  0x61, 0x29, 0xcd, 0x78, 0xf4, 0x6d, 0x1b, 0xa6,
  0xe6, 0xd1, 0x7c, 0x76, 0xc2, 0x86, 0x78, 0x3c,
  0xc2, 0x2e, 0x26, 0xd4, 0xdf, 0x7f, 0x3b, 0x98,
  0x7a, 0x7c, 0xbe, 0x1a, 0x17, 0xd2, 0x2d, 0xa5,
  0x90, 0x2a, 0x1b, 0x0b, 0x43, 0x65, 0x63, 0x37,
  0xe5, 0x0d, 0x5c, 0x9c, 0x6c, 0x38, 0xef, 0x2a,
  0xe8, 0x49, 0x47, 0x05, 0x6d, 0x83, 0xcf, 0x6d,
};

GST_START_TEST (test_pay_to_depay)
{
  GstHarness *h = gst_harness_new_parse ("rtpopuspay ! rtpopusdepay");
  GstBuffer *buf = buffer_from_array (opus_data);
  gst_harness_set_src_caps_str (h, "audio/x-opus,channel-mapping-family=0");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST
GST_START_TEST (test_depay_to_pay)
{
  GstHarness *h = gst_harness_new_parse ("rtpopusdepay ! rtpopuspay");
  guint8 opus_rtp_buf[] = {
    0x80, 0x60, 0x54, 0xfd, 0x3b, 0x5a, 0x93, 0xf9, 0x1c, 0x33, 0x2b, 0xbb,
  };
  GstBuffer *buf = buffer_from_array (opus_rtp_buf);
  gst_harness_set_src_caps_str (h,
      "application/x-rtp,encoding-name=OPUS,media=audio,clock-rate=48000,payload=96");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;
GST_START_TEST (test_pay_to_depay_multichannel)
{
  GstHarness *h = gst_harness_new_parse ("rtpopuspay ! rtpopusdepay");
  GstBuffer *buf;
  GstCaps *caps;
  GstStructure *s;
  const GValue *channel_mapping;
  gint val, i;

  static const int expected_channel_mapping[] = { 0, 4, 1, 2, 3, 5 };

  buf = buffer_from_array (opus_data);

  gst_harness_set_src_caps_str (h, "audio/x-opus,channel-mapping-family=1,"
      "rate=48000,channels=6,stream-count=4,coupled-count=2,channel-mapping=<0,4,1,2,3,5>");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  gst_buffer_unref (gst_harness_pull (h));

  caps = gst_pad_get_current_caps (h->srcpad);
  s = gst_caps_get_structure (caps, 0);

  assert_equals_string (gst_structure_get_name (s), "audio/x-opus");

  fail_unless (gst_structure_get_int (s, "rate", &val));
  fail_unless_equals_int (val, 48000);
  fail_unless (gst_structure_get_int (s, "channels", &val));
  fail_unless_equals_int (val, 6);
  fail_unless (gst_structure_get_int (s, "channel-mapping-family", &val));
  fail_unless_equals_int (val, 1);
  fail_unless (gst_structure_get_int (s, "stream-count", &val));
  fail_unless_equals_int (val, 4);
  fail_unless (gst_structure_get_int (s, "coupled-count", &val));
  fail_unless_equals_int (val, 2);

  channel_mapping = gst_structure_get_value (s, "channel-mapping");
  g_assert (GST_VALUE_HOLDS_ARRAY (channel_mapping));

  for (i = 0; i != gst_value_array_get_size (channel_mapping); ++i) {
    fail_unless_equals_int (expected_channel_mapping[i],
        g_value_get_int (gst_value_array_get_value (channel_mapping, i)));
  }

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_depay_to_pay_multichannel)
{
  GstHarness *h = gst_harness_new_parse ("rtpopusdepay ! rtpopuspay");
  guint8 opus_rtp_buf[] = {
    0x80, 0x60, 0x54, 0xfd, 0x3b, 0x5a, 0x93, 0xf9, 0x1c, 0x33, 0x2b, 0xbb,
  };
  GstBuffer *buf;
  GstCaps *caps;
  GstStructure *s;
  gint val;

  buf = buffer_from_array (opus_rtp_buf);

  gst_harness_set_src_caps_str (h,
      "application/x-rtp,encoding-name=OPUS,media=audio,clock-rate=48000,payload=96,"
      "encoding-params=6,num_streams=4,coupled_streams=2,channel_mapping=\"0,4,1,2,3,5\"");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  gst_buffer_unref (gst_harness_pull (h));

  caps = gst_pad_get_current_caps (h->srcpad);
  s = gst_caps_get_structure (caps, 0);

  assert_equals_string (gst_structure_get_name (s), "application/x-rtp");

  fail_unless (gst_structure_get_int (s, "encoding-params", &val));
  fail_unless_equals_int (val, 6);
  fail_unless_equals_string (gst_structure_get_string (s, "channel_mapping"),
      "0,4,1,2,3,5");
  fail_unless (gst_structure_get_int (s, "num_streams", &val));
  fail_unless_equals_int (val, 4);
  fail_unless (gst_structure_get_int (s, "coupled_streams", &val));
  fail_unless_equals_int (val, 2);

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_pay_getcaps)
{
  GstHarness *h = gst_harness_new ("rtpopuspay");
  GstCaps *ref, *qcaps;

  gst_harness_set_sink_caps_str (h, "application/x-rtp, "
      "encoding-name=(string)OPUS, stereo=(string)0");
  qcaps = gst_pad_peer_query_caps (h->srcpad, NULL);
  /* Check that is also contains stereo */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)2, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_can_intersect (ref, qcaps));
  gst_caps_unref (ref);
  fail_unless_equals_int (gst_caps_get_size (qcaps), 2);
  qcaps = gst_caps_truncate (qcaps);
  /* Check that the first structure is mono */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)1, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_is_equal (ref, qcaps));
  gst_caps_unref (ref);
  gst_caps_unref (qcaps);

  gst_harness_set_sink_caps_str (h, "application/x-rtp, "
      "encoding-name=(string)OPUS, stereo=(string)1");
  qcaps = gst_pad_peer_query_caps (h->srcpad, NULL);
  /* Check that is also contains stereo */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)2, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_can_intersect (ref, qcaps));
  gst_caps_unref (ref);
  fail_unless_equals_int (gst_caps_get_size (qcaps), 2);
  qcaps = gst_caps_truncate (qcaps);
  /* Check that the first structure is mono */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)2, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_is_equal (ref, qcaps));
  gst_caps_unref (ref);
  gst_caps_unref (qcaps);

  gst_harness_set_sink_caps_str (h, "application/x-rtp, "
      "encoding-name=(string)MULTIOPUS");
  qcaps = gst_pad_peer_query_caps (h->srcpad, NULL);
  /* Check that is also contains stereo */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)[3, 255], "
      "channel-mapping-family=(int)1");
  fail_unless (gst_caps_is_equal (ref, qcaps));
  gst_caps_unref (ref);
  fail_unless_equals_int (gst_caps_get_size (qcaps), 1);
  gst_caps_unref (qcaps);

  gst_harness_set_sink_caps_str (h, "application/x-rtp, "
      "encoding-name=(string)MULTIOPUS, stereo=(string)1");
  qcaps = gst_pad_peer_query_caps (h->srcpad, NULL);
  /* Check that is also contains stereo */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)[3, 255], "
      "channel-mapping-family=(int)1");
  fail_unless (gst_caps_is_equal (ref, qcaps));
  gst_caps_unref (ref);
  fail_unless_equals_int (gst_caps_get_size (qcaps), 1);
  gst_caps_unref (qcaps);

  gst_harness_set_sink_caps_str (h, "application/x-rtp, "
      "encoding-name=(string)OPUS, stereo=(string)0;"
      "application/x-rtp, encoding-name=(string)MULTIOPUS");
  qcaps = gst_pad_peer_query_caps (h->srcpad, NULL);
  /* Check that is also contains stereo */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)2, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_can_intersect (ref, qcaps));
  gst_caps_unref (ref);
  /* Check that is also contains 3 channels */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)3, "
      "channel-mapping-family=(int)1");
  fail_unless (gst_caps_can_intersect (ref, qcaps));
  gst_caps_unref (ref);
  fail_unless_equals_int (gst_caps_get_size (qcaps), 3);
  qcaps = gst_caps_truncate (qcaps);
  /* Check that the first structure is mono */
  ref = gst_caps_from_string ("audio/x-opus, channels=(int)1, "
      "channel-mapping-family=(int)0");
  fail_unless (gst_caps_can_intersect (ref, qcaps));
  gst_caps_unref (ref);
  gst_caps_unref (qcaps);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtpopus_suite (void)
{
  Suite *s = suite_create ("rtpopus");
  TCase *tc_chain;

  suite_add_tcase (s, (tc_chain = tcase_create ("rtpopus")));
  tcase_add_test (tc_chain, test_pay_to_depay);
  tcase_add_test (tc_chain, test_depay_to_pay);
  tcase_add_test (tc_chain, test_pay_to_depay_multichannel);
  tcase_add_test (tc_chain, test_depay_to_pay_multichannel);
  tcase_add_test (tc_chain, test_pay_getcaps);

  return s;
}

GST_CHECK_MAIN (rtpopus);
