/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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
#include <gst/base/gstbaseparse.h>
#include <gst/pbutils/codec-utils.h>

static gboolean
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static gboolean
require_elements_or_skip (const gchar * const *elements, gsize n_elements)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;
  gsize i;

  for (i = 0; i < n_elements; i++) {
    if (element_available (elements[i]))
      continue;
    if (strict)
      fail_unless (FALSE, "Missing required element: %s", elements[i]);
    GST_INFO ("Skipping test, missing required element: %s", elements[i]);
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
run_parse_and_get_caps_with_level (const gchar * level_str)
{
  gchar *pipeline_desc;
  GstElement *pipeline;
  GstElement *parse;
  GstBus *bus;
  GstMessage *msg;
  GstCaps *caps;

  if (level_str) {
    pipeline_desc =
        g_strdup_printf ("videotestsrc num-buffers=10 ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=25/1 ! "
        "vp9enc ! video/x-vp9,level=(string)%s ! "
        "vp9parse name=parse ! fakesink sync=false", level_str);
  } else {
    pipeline_desc =
        g_strdup ("videotestsrc num-buffers=10 ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=25/1 ! "
        "vp9enc ! vp9parse name=parse ! fakesink sync=false");
  }

  pipeline = gst_parse_launch (pipeline_desc, NULL);
  fail_unless (pipeline != NULL);
  g_free (pipeline_desc);

  parse = gst_bin_get_by_name (GST_BIN (pipeline), "parse");
  fail_unless (parse != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *error = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &error, &debug);
    gst_object_default_error (GST_MESSAGE_SRC (msg), error, debug);
    g_error_free (error);
    g_free (debug);
    fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  }

  gst_message_unref (msg);

  caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (parse));
  fail_unless (caps != NULL);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (parse);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return caps;
}

static GstCaps *
run_parse_and_get_caps (void)
{
  return run_parse_and_get_caps_with_level (NULL);
}

GST_START_TEST (test_vp9parse_vpcc_level_estimated_from_caps)
{
  GstCaps *caps;
  GstCaps *expected_caps;
  GstStructure *s;
  const gchar *level;
  guint8 level_idc;
  const gchar *expected_level;
  const gchar *required[] = { "vp9enc", "vp9parse" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  caps = run_parse_and_get_caps ();

  s = gst_caps_get_structure (caps, 0);
  level = gst_structure_get_string (s, "level");
  fail_unless (level != NULL);

  expected_caps = gst_caps_copy (caps);
  gst_structure_remove_field (gst_caps_get_structure (expected_caps, 0),
      "level");
  level_idc = gst_codec_utils_vp9_estimate_level_idc_from_caps (expected_caps);
  expected_level = gst_codec_utils_vp9_get_level (level_idc);
  fail_unless (expected_level != NULL);
  fail_unless_equals_string (level, expected_level);

  gst_caps_unref (expected_caps);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_vp9parse_vpcc_level_passthrough)
{
  GstCaps *caps;
  GstStructure *s;
  const gchar *level;
  const gchar *forced_level;
  const gchar *required[] = { "vp9enc", "vp9parse" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  forced_level = gst_codec_utils_vp9_get_level (11);
  fail_unless (forced_level != NULL);

  caps = run_parse_and_get_caps_with_level (forced_level);

  s = gst_caps_get_structure (caps, 0);
  level = gst_structure_get_string (s, "level");
  fail_unless (level != NULL);
  fail_unless_equals_string (level, forced_level);

  gst_caps_unref (caps);
}

GST_END_TEST;

static Suite *
vp9parse_vpcc_suite (void)
{
  Suite *s = suite_create ("vp9parse vpcC pipeline");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_vp9parse_vpcc_level_estimated_from_caps);
  tcase_add_test (tc_chain, test_vp9parse_vpcc_level_passthrough);

  return s;
}

GST_CHECK_MAIN (vp9parse_vpcc);
