/* GStreamer
 *
 * Copyright (C) 2023 Jonas Danielsson <jonas.danielsson@spiideo.com>
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
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/check.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

#define buffer_from_array(a) gst_buffer_new_memdup (a, G_N_ELEMENTS (a))

static guint8 klv_data[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x02, 0x00, 0x03,
};

GST_START_TEST (test_pay_depay_passthrough)
{
  GstHarness *h =
      gst_harness_new_parse ("rtpklvpay ! rtppassthroughpay ! rtpklvdepay");
  GstBuffer *buf = buffer_from_array (klv_data);

  gst_harness_set_src_caps_str (h, "meta/x-klv,parsed=true");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));

  gst_buffer_unref (gst_harness_pull (h));
  gst_harness_teardown (h);
}

GST_END_TEST
GST_START_TEST (test_read_properties)
{
  GstElement *passthrough_pay;
  GstElement *klv_pay;
  GstStructure *stats;
  guint pt;
  guint ssrc_klv;
  guint ssrc_passthrough;
  GstHarness *h =
      gst_harness_new_parse ("rtpklvpay pt=97 ssrc=424242 ! rtppassthroughpay");
  GstBuffer *buf = buffer_from_array (klv_data);

  gst_harness_set_src_caps_str (h, "meta/x-klv,parsed=true");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));

  passthrough_pay = gst_harness_find_element (h, "rtppassthroughpay");
  g_object_get (G_OBJECT (passthrough_pay), "pt", &pt, NULL);
  fail_unless_equals_uint64 (pt, 97U);

  klv_pay = gst_harness_find_element (h, "rtpklvpay");
  g_object_get (G_OBJECT (klv_pay), "ssrc", &ssrc_klv, NULL);
  g_object_get (G_OBJECT (passthrough_pay), "stats", &stats, NULL);
  gst_structure_get_uint (stats, "ssrc", &ssrc_passthrough);
  fail_unless_equals_uint64 (424242U, ssrc_passthrough);

  gst_structure_free (stats);
  gst_object_unref (klv_pay);
  gst_object_unref (passthrough_pay);
  gst_buffer_unref (gst_harness_pull (h));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_override_payload_type)
{
  GstElement *passthrough_pay;
  guint pt;
  GstHarness *h =
      gst_harness_new_parse ("rtpklvpay pt=97 ! rtppassthroughpay pt=98");
  GstBuffer *buf = buffer_from_array (klv_data);
  GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;

  gst_harness_set_src_caps_str (h, "meta/x-klv,parsed=true");
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));

  passthrough_pay = gst_harness_find_element (h, "rtppassthroughpay");
  g_object_get (G_OBJECT (passthrough_pay), "pt", &pt, NULL);
  fail_unless_equals_uint64 (pt, 98U);

  buf = gst_harness_pull (h);
  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp_buf);
  fail_unless_equals_uint64 (gst_rtp_buffer_get_payload_type (&rtp_buf), 98U);
  gst_rtp_buffer_unmap (&rtp_buf);

  gst_object_unref (passthrough_pay);
  gst_buffer_unref (buf);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtppassthrough_suite (void)
{
  Suite *s = suite_create ("rtppassthrough");
  TCase *tc_chain;

  suite_add_tcase (s, (tc_chain = tcase_create ("rtppassthrough")));
  tcase_add_test (tc_chain, test_pay_depay_passthrough);
  tcase_add_test (tc_chain, test_read_properties);
  tcase_add_test (tc_chain, test_override_payload_type);

  return s;
}

GST_CHECK_MAIN (rtppassthrough);
