/* GStreamer unit tests for VP8/VP9 alpha decode bins
 *
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/app/gstappsink.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

static gboolean
element_available (const gchar * element_name)
{
  GstElementFactory *factory = gst_element_factory_find (element_name);

  if (!factory)
    return FALSE;

  gst_object_unref (factory);
  return TRUE;
}

static gboolean
require_elements_or_skip (const gchar * const *elements, gsize n_elements)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;

  for (gsize i = 0; i < n_elements; i++) {
    if (element_available (elements[i]))
      continue;
    if (strict)
      fail_unless (FALSE, "Missing required element: %s", elements[i]);
    GST_INFO ("Skipping test, missing required element: %s", elements[i]);
    return FALSE;
  }

  return TRUE;
}

static void
check_alpha_decode_bin_factory (const gchar * factory_name,
    const gchar * expected_sink_caps)
{
  GstElementFactory *factory = gst_element_factory_find (factory_name);
  GstStaticPadTemplate *sink_template = NULL;
  GstCaps *expected_caps;
  GstCaps *template_caps;

  fail_unless (factory != NULL);
  fail_unless_equals_int (gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE
          (factory)), GST_RANK_PRIMARY + GST_ALPHA_DECODE_BIN_RANK_OFFSET);

  for (const GList * templates =
      gst_element_factory_get_static_pad_templates (factory);
      templates; templates = templates->next) {
    GstStaticPadTemplate *template = templates->data;

    if (template->direction == GST_PAD_SINK) {
      sink_template = template;
      break;
    }
  }

  fail_unless (sink_template != NULL);

  expected_caps = gst_caps_from_string (expected_sink_caps);
  template_caps = gst_static_caps_get (&sink_template->static_caps);
  fail_unless (gst_caps_is_equal (template_caps, expected_caps));
  gst_caps_unref (template_caps);
  gst_caps_unref (expected_caps);
  gst_object_unref (factory);
}

static GstSample *
pull_encoded_sample (const gchar * launchline)
{
  GError *error = NULL;
  GstElement *pipeline = gst_parse_launch (launchline, &error);
  GstElement *appsink;
  GstStateChangeReturn state_ret;
  GstSample *sample;
  GstBus *bus;
  GstMessage *msg;

  fail_unless (pipeline != NULL, "Failed to parse pipeline: %s",
      error ? error->message : "unknown error");
  g_clear_error (&error);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS ||
      state_ret == GST_STATE_CHANGE_ASYNC);

  sample = gst_app_sink_try_pull_sample (GST_APP_SINK (appsink),
      10 * GST_SECOND);
  fail_unless (sample != NULL);

  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);
  if (msg) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    fail ("Encoding pipeline failed: %s (%s)", err->message,
        debug ? debug : "no debug");
    g_clear_error (&err);
    g_free (debug);
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (appsink);
  gst_object_unref (pipeline);

  return sample;
}

static void
run_alpha_decode_bin_smoke (const gchar * element_name,
    const gchar * encoded_caps, const gchar * launchline)
{
  GstSample *main_sample = pull_encoded_sample (launchline);
  GstSample *alpha_sample = pull_encoded_sample (launchline);
  GstBuffer *main_buffer =
      gst_buffer_copy (gst_sample_get_buffer (main_sample));
  GstBuffer *alpha_buffer =
      gst_buffer_copy (gst_sample_get_buffer (alpha_sample));
  GstCaps *caps = gst_caps_from_string (encoded_caps);
  GstHarness *h = gst_harness_new (element_name);
  GstBuffer *out_buffer;
  GstVideoInfo out_info;

  fail_unless (main_buffer != NULL);
  fail_unless (alpha_buffer != NULL);
  fail_unless (gst_buffer_add_video_codec_alpha_meta (main_buffer,
          alpha_buffer) != NULL);

  fail_unless (caps != NULL);
  gst_harness_set_src_caps (h, caps);
  out_buffer = gst_harness_push_and_pull (h, main_buffer);
  fail_unless (out_buffer != NULL);

  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&out_info, caps));
  fail_unless (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (out_info.finfo));
  gst_caps_unref (caps);
  gst_buffer_unref (out_buffer);
  gst_harness_teardown (h);

  gst_sample_unref (main_sample);
  gst_sample_unref (alpha_sample);
}

GST_START_TEST (test_vp8alphadecodebin_factory)
{
  const gchar *required[] = { "vp8dec", "vp8alphadecodebin" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  check_alpha_decode_bin_factory ("vp8alphadecodebin",
      "video/x-vp8, codec-alpha = (boolean) true");
}

GST_END_TEST;

GST_START_TEST (test_vp9alphadecodebin_factory)
{
  const gchar *required[] = { "vp9dec", "vp9alphadecodebin" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  check_alpha_decode_bin_factory ("vp9alphadecodebin",
      "video/x-vp9, codec-alpha = (boolean) true, "
      "alignment = (string) super-frame");
}

GST_END_TEST;

GST_START_TEST (test_vp8alphadecodebin_smoke)
{
  const gchar *launchline =
      "videotestsrc num-buffers=1 pattern=smpte ! "
      "video/x-raw,format=I420,width=16,height=16,framerate=1/1 ! "
      "vp8enc deadline=1 ! appsink name=sink sync=false";
  const gchar *required[] = { "vp8enc", "vp8dec", "codecalphademux",
    "alphacombine", "vp8alphadecodebin"
  };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  run_alpha_decode_bin_smoke ("vp8alphadecodebin",
      "video/x-vp8,codec-alpha=(boolean)true", launchline);
}

GST_END_TEST;

GST_START_TEST (test_vp9alphadecodebin_smoke)
{
  const gchar *launchline =
      "videotestsrc num-buffers=1 pattern=smpte ! "
      "video/x-raw,format=I420,width=16,height=16,framerate=1/1 ! "
      "vp9enc deadline=1 cpu-used=8 ! vp9parse ! "
      "video/x-vp9,alignment=(string)super-frame ! "
      "appsink name=sink sync=false";
  const gchar *required[] = { "vp9enc", "vp9dec", "vp9parse",
    "codecalphademux", "alphacombine", "vp9alphadecodebin"
  };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  run_alpha_decode_bin_smoke ("vp9alphadecodebin",
      "video/x-vp9,codec-alpha=(boolean)true,alignment=(string)super-frame",
      launchline);
}

GST_END_TEST;

static Suite *
vpxalphadecodebin_suite (void)
{
  Suite *s = suite_create ("vpxalphadecodebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 20);

  tcase_add_test (tc_chain, test_vp8alphadecodebin_factory);
  tcase_add_test (tc_chain, test_vp9alphadecodebin_factory);
  tcase_add_test (tc_chain, test_vp8alphadecodebin_smoke);
  tcase_add_test (tc_chain, test_vp9alphadecodebin_smoke);

  return s;
}

GST_CHECK_MAIN (vpxalphadecodebin);
