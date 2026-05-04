/* GStreamer
 *
 * unit test for vkh264enc element
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

gboolean has_profile_main = FALSE;
gboolean has_profile_constrained_baseline = FALSE;
gboolean has_profile_constrained_high = FALSE;

static inline void
check_for_profiles (const GValue * value)
{
  const gchar *profiles = g_value_get_string (value);
  if (g_strcmp0 (profiles, "main") == 0) {
    has_profile_main = TRUE;
    return;
  }
  if (g_strcmp0 (profiles, "constrained-baseline") == 0) {
    has_profile_constrained_baseline = TRUE;
    return;
  }
  if (g_strcmp0 (profiles, "constrained-high") == 0) {
    has_profile_constrained_high = TRUE;
    return;
  }
}

static gboolean
check_enc_available (void)
{
  GstElement *encoder;

  encoder = gst_element_factory_make ("vulkanh264enc", NULL);
  if (!encoder) {
    GST_WARNING ("vulkanh264enc is not available");
    return FALSE;
  }

  GstPadTemplate *tmpl =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (encoder),
      "src");
  GstCaps *caps = gst_pad_template_get_caps (tmpl);
  const GstStructure *s = gst_caps_get_structure (caps, 0);
  const GValue *profiles = gst_structure_get_value (s, "profile");
  if (!profiles)
    return FALSE;
  if (G_VALUE_HOLDS_STRING (profiles))
    check_for_profiles (profiles);
  else if (GST_VALUE_HOLDS_LIST (profiles)) {
    for (guint i = 0; i < gst_value_list_get_size (profiles); i++) {
      const GValue *profile = gst_value_list_get_value (profiles, i);
      check_for_profiles (profile);
    }
  }
  gst_caps_unref (caps);

  gst_object_unref (encoder);
  return TRUE;
}

static inline GstHarness *
harness_new (gint num_buffers)
{
  GstHarness *h;

  char *launchline = g_strdup_printf ("videotestsrc num-buffers=%i pattern=blue"
      " ! vulkanupload ! vulkanh264enc ! h264parse", num_buffers);

  h = gst_harness_new_parse (launchline);
  g_free (launchline);
  fail_unless (h, "No harness object");

  return h;
}

static inline void
set_sink_caps (GstHarness * h, const char *profile)
{
  /* XXX: it's a pretty common resolution */
  char *caps_str = g_strdup_printf ("video/x-h264, profile=%s, width=(int)640, "
      "height=(int)480, framerate=(fraction)30/1", profile);

  gst_harness_set_sink_caps_str (h, caps_str);
  g_free (caps_str);
}

static inline void
pull_num_buffers (GstHarness * h, gint num_buffers)
{
  gint count = 0;
  while (count < num_buffers) {
    GstBuffer *outbuf = gst_harness_pull (h);
    fail_unless (outbuf, "No buffer %i returned", count);

    {
      GstMapInfo map;
      gboolean ret = gst_buffer_map (outbuf, &map, GST_MAP_READ);
      fail_unless (ret, "Cannot map output buffer %i", count);
      GST_MEMDUMP ("encoded buffer", map.data, map.size);
      gst_buffer_unmap (outbuf, &map);
    }

    gst_buffer_unref (outbuf);
    count++;
  }
}

GST_START_TEST (test_encode_main_single_frame)
{
  if (!has_profile_main)
    return;

  GstHarness *h = harness_new (1);
  set_sink_caps (h, "main");
  gst_harness_play (h);
  pull_num_buffers (h, 1);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_encode_constrained_baseline)
{
  if (!has_profile_constrained_baseline)
    return;

  GstHarness *h = harness_new (30);
  set_sink_caps (h, "constrained-baseline");
  gst_harness_play (h);
  pull_num_buffers (h, 30);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_encode_constrained_high)
{
  if (!has_profile_constrained_high)
    return;

  GstHarness *h = harness_new (30);
  set_sink_caps (h, "constrained-high");
  gst_harness_play (h);
  pull_num_buffers (h, 30);

  gst_harness_teardown (h);
}

GST_END_TEST;


static Suite *
vkh264enc_suite (void)
{
  Suite *s = suite_create ("vkh264enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  if (check_enc_available ()) {
    tcase_add_test (tc_chain, test_encode_main_single_frame);
    tcase_add_test (tc_chain, test_encode_constrained_baseline);
    tcase_add_test (tc_chain, test_encode_constrained_high);
  }

  return s;
}

GST_CHECK_MAIN (vkh264enc);
