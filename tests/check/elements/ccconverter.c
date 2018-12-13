/* GStreamer
 *
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#include <string.h>

GST_START_TEST (cdp_requires_framerate)
{
  GstHarness *h;
  GstBuffer *buffer;
  GstMapInfo map;

  h = gst_harness_new ("ccconverter");

  /* Enforce conversion to CDP */
  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cdp");

  /* Try without a framerate first, this has to fail */
  gst_harness_set_src_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data");

  buffer = gst_buffer_new_and_alloc (3);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  map.data[0] = 0xfc;
  map.data[1] = 0x80;
  map.data[2] = 0x80;
  gst_buffer_unmap (buffer, &map);
  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_NOT_NEGOTIATED);

  /* Now set a framerate only on the sink caps, this should still fail:
   * We can't do framerate conversion!
   */
  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cdp,framerate=(fraction)30/1");

  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_NOT_NEGOTIATED);

  /* Then try with a framerate, this should work now */
  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cdp");
  gst_harness_set_src_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)30/1");

  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (framerate_passthrough)
{
  GstHarness *h;
  GstBuffer *buffer;
  GstMapInfo map;
  GstCaps *caps, *expected_caps;

  h = gst_harness_new ("ccconverter");

  gst_harness_set_src_caps_str (h,
      "closedcaption/x-cea-608,format=(string)s334-1a,framerate=(fraction)30/1");

  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data");

  buffer = gst_buffer_new_and_alloc (3);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  map.data[0] = 0x00;
  map.data[1] = 0x80;
  map.data[2] = 0x80;
  gst_buffer_unmap (buffer, &map);

  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_OK);

  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps);
  expected_caps =
      gst_caps_from_string
      ("closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)30/1");
  gst_check_caps_equal (caps, expected_caps);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  /* Now try between the same formats, should still pass through */
  gst_harness_set_src_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)30/1");

  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data");

  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_OK);

  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps);
  expected_caps =
      gst_caps_from_string
      ("closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)30/1");
  gst_check_caps_equal (caps, expected_caps);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  /* And another time with the same format but only framerate on the output
   * side. This should fail as we can't just come up with a framerate! */
  gst_harness_set_src_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data");

  gst_harness_set_sink_caps_str (h,
      "closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)30/1");

  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_teardown (h);
}

GST_END_TEST;

static void
check_conversion (const guint8 * in, guint in_len, const guint8 * out,
    guint out_len, const gchar * in_caps, const gchar * out_caps)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("ccconverter");

  gst_harness_set_src_caps_str (h, in_caps);
  gst_harness_set_sink_caps_str (h, out_caps);

  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) in,
      in_len, 0, in_len, NULL, NULL);

  buffer = gst_harness_push_and_pull (h, buffer);

  fail_unless (buffer != NULL);
  gst_check_buffer_data (buffer, out, out_len);
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_START_TEST (convert_cea608_raw_cea608_s334_1a)
{
  const guint8 in[] = { 0x80, 0x80 };
  const guint8 out[] = { 0x80, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)raw",
      "closedcaption/x-cea-608,format=(string)s334-1a");
}

GST_END_TEST;

GST_START_TEST (convert_cea608_raw_cea708_cc_data)
{
  const guint8 in[] = { 0x80, 0x80 };
  const guint8 out[] = { 0xfc, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)raw",
      "closedcaption/x-cea-708,format=(string)cc_data");
}

GST_END_TEST;

GST_START_TEST (convert_cea608_raw_cea708_cdp)
{
  const guint8 in[] = { 0x80, 0x80 };
  const guint8 out[] =
      { 0x96, 0x69, 0x2b, 0x8f, 0x43, 0x00, 0x00, 0x72, 0xea, 0xfc, 0x80, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x38
  };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)raw,framerate=(fraction)60/1",
      "closedcaption/x-cea-708,format=(string)cdp");
}

GST_END_TEST;

GST_START_TEST (convert_cea608_s334_1a_cea608_raw)
{
  const guint8 in[] = { 0x80, 0x80, 0x80, 0x00, 0x80, 0x80 };
  const guint8 out[] = { 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)s334-1a",
      "closedcaption/x-cea-608,format=(string)raw");
}

GST_END_TEST;

GST_START_TEST (convert_cea608_s334_1a_cea708_cc_data)
{
  const guint8 in[] = { 0x80, 0x80, 0x80, 0x00, 0x80, 0x80 };
  const guint8 out[] = { 0xfc, 0x80, 0x80, 0xfd, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)s334-1a",
      "closedcaption/x-cea-708,format=(string)cc_data");
}

GST_END_TEST;

GST_START_TEST (convert_cea608_s334_1a_cea708_cdp)
{
  const guint8 in[] = { 0x80, 0x80, 0x80, 0x00, 0x80, 0x80 };
  const guint8 out[] =
      { 0x96, 0x69, 0x2b, 0x8f, 0x43, 0x00, 0x00, 0x72, 0xea, 0xfc, 0x80, 0x80,
    0xfd, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x3b
  };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-608,format=(string)s334-1a,framerate=(fraction)60/1",
      "closedcaption/x-cea-708,format=(string)cdp");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cc_data_cea608_raw)
{
  const guint8 in[] = { 0xfc, 0x80, 0x80, 0xfe, 0x80, 0x80 };
  const guint8 out[] = { 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cc_data",
      "closedcaption/x-cea-608,format=(string)raw");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cc_data_cea608_s334_1a)
{
  const guint8 in[] = { 0xfc, 0x80, 0x80, 0xfe, 0x80, 0x80 };
  const guint8 out[] = { 0x80, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cc_data",
      "closedcaption/x-cea-608,format=(string)s334-1a");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cc_data_cea708_cdp)
{
  const guint8 in[] = { 0xfc, 0x80, 0x80, 0xfe, 0x80, 0x80 };
  const guint8 out[] =
      { 0x96, 0x69, 0x2b, 0x8f, 0x43, 0x00, 0x00, 0x72, 0xea, 0xfc, 0x80, 0x80,
    0xfe, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x3a
  };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cc_data,framerate=(fraction)60/1",
      "closedcaption/x-cea-708,format=(string)cdp");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cdp_cea608_raw)
{
  const guint8 in[] =
      { 0x96, 0x69, 0x13, 0x5f, 0x43, 0x00, 0x00, 0x72, 0xe2, 0xfc, 0x80, 0x80,
    0xfe, 0x80, 0x80, 0x74, 0x00, 0x00, 0x8a
  };
  const guint8 out[] = { 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cdp",
      "closedcaption/x-cea-608,format=(string)raw");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cdp_cea608_s334_1a)
{
  const guint8 in[] =
      { 0x96, 0x69, 0x13, 0x5f, 0x43, 0x00, 0x00, 0x72, 0xe2, 0xfc, 0x80, 0x80,
    0xfe, 0x80, 0x80, 0x74, 0x00, 0x00, 0x8a
  };
  const guint8 out[] = { 0x80, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cdp",
      "closedcaption/x-cea-608,format=(string)s334-1a");
}

GST_END_TEST;

GST_START_TEST (convert_cea708_cdp_cea708_cc_data)
{
  const guint8 in[] =
      { 0x96, 0x69, 0x13, 0x5f, 0x43, 0x00, 0x00, 0x72, 0xe2, 0xfc, 0x80, 0x80,
    0xfe, 0x80, 0x80, 0x74, 0x00, 0x00, 0x8a
  };
  const guint8 out[] = { 0xfc, 0x80, 0x80, 0xfe, 0x80, 0x80 };
  check_conversion (in, sizeof (in), out, sizeof (out),
      "closedcaption/x-cea-708,format=(string)cdp",
      "closedcaption/x-cea-708,format=(string)cc_data");
}

GST_END_TEST;

static Suite *
ccextractor_suite (void)
{
  Suite *s = suite_create ("ccconverter");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, cdp_requires_framerate);
  tcase_add_test (tc, framerate_passthrough);
  tcase_add_test (tc, convert_cea608_raw_cea608_s334_1a);
  tcase_add_test (tc, convert_cea608_raw_cea708_cc_data);
  tcase_add_test (tc, convert_cea608_raw_cea708_cdp);
  tcase_add_test (tc, convert_cea608_s334_1a_cea608_raw);
  tcase_add_test (tc, convert_cea608_s334_1a_cea708_cc_data);
  tcase_add_test (tc, convert_cea608_s334_1a_cea708_cdp);
  tcase_add_test (tc, convert_cea708_cc_data_cea608_raw);
  tcase_add_test (tc, convert_cea708_cc_data_cea608_s334_1a);
  tcase_add_test (tc, convert_cea708_cc_data_cea708_cdp);
  tcase_add_test (tc, convert_cea708_cdp_cea608_raw);
  tcase_add_test (tc, convert_cea708_cdp_cea608_s334_1a);
  tcase_add_test (tc, convert_cea708_cdp_cea708_cc_data);

  return s;
}

GST_CHECK_MAIN (ccextractor);
