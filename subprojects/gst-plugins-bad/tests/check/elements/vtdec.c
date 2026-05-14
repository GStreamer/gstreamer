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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

typedef struct
{
  const gchar *name;
  const gchar *caps;
} VtdecElement;

static const VtdecElement vtdec_elements[] = {
  {"vth264dec",
      "video/x-h264, stream-format=avc, alignment=au, "
        "width=(int)[8, MAX], height=(int)[8, MAX]"},
  {"vth265dec",
      "video/x-h265, stream-format=(string){ hev1, hvc1 }, alignment=au, "
        "width=(int)[16, MAX], height=(int)[16, MAX]"},
  {"vtmpeg2dec",
      "video/mpeg, mpegversion=2, systemstream=false, parsed=true"},
  {"vtjpegdec", "image/jpeg"},
  {"vtproresdec",
      "video/x-prores, variant = { (string)standard, (string)hq, "
        "(string)lt, (string)proxy, (string)4444, (string)4444xq }"},
};

static const VtdecElement vtdec_optional_elements[] = {
  {"vtav1dec",
      "video/x-av1, stream-format=obu-stream, "
        "alignment=(string){ tu, frame }, "
        "width=(int)[64, MAX], height=(int)[64, MAX]"},
  {"vtvp9dec",
      "video/x-vp9, profile=(string){ 0, 2 }, "
        "width=(int)[64, MAX], height=(int)[64, MAX]"},
};

static gboolean
require_factory_or_skip (const gchar * name)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  if (strict)
    fail ("Missing required element: %s", name);

  GST_INFO ("Skipping test, missing required element: %s", name);
  return FALSE;
}

static GstCaps *
get_sink_template_caps (GstElementFactory * factory)
{
  const GList *templates;

  for (templates = gst_element_factory_get_static_pad_templates (factory);
      templates; templates = templates->next) {
    GstStaticPadTemplate *static_template = templates->data;

    if (static_template->direction == GST_PAD_SINK)
      return gst_static_caps_get (&static_template->static_caps);
  }

  return NULL;
}

static void
assert_factory_rank (const gchar * name, GstRank rank)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  fail_unless (factory != NULL, "Missing factory %s", name);
  fail_unless_equals_int (gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE
          (factory)), rank);
  gst_object_unref (factory);
}

static void
assert_factory_rank_if_registered (const gchar * name, GstRank rank)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory == NULL)
    return;

  fail_unless_equals_int (gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE
          (factory)), rank);
  gst_object_unref (factory);
}

static void
assert_factory_sink_caps (const gchar * name,
    const gchar * expected_caps_string)
{
  GstElementFactory *factory = gst_element_factory_find (name);
  GstCaps *expected_caps;
  GstCaps *caps;

  fail_unless (factory != NULL, "Missing factory %s", name);

  expected_caps = gst_caps_from_string (expected_caps_string);
  fail_unless (expected_caps != NULL);

  caps = get_sink_template_caps (factory);
  fail_unless (caps != NULL, "Missing sink pad template for %s", name);
  fail_unless (gst_caps_is_equal (caps, expected_caps),
      "Unexpected sink caps for %s: expected %" GST_PTR_FORMAT ", got %"
      GST_PTR_FORMAT, name, expected_caps, caps);

  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);
  gst_object_unref (factory);
}

static void
assert_factory_sink_caps_if_registered (const gchar * name,
    const gchar * expected_caps_string)
{
  GstElementFactory *factory = gst_element_factory_find (name);
  GstCaps *expected_caps;
  GstCaps *caps;

  if (factory == NULL)
    return;

  expected_caps = gst_caps_from_string (expected_caps_string);
  fail_unless (expected_caps != NULL);

  caps = get_sink_template_caps (factory);
  fail_unless (caps != NULL, "Missing sink pad template for %s", name);
  fail_unless (gst_caps_is_equal (caps, expected_caps),
      "Unexpected sink caps for %s: expected %" GST_PTR_FORMAT ", got %"
      GST_PTR_FORMAT, name, expected_caps, caps);

  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);
  gst_object_unref (factory);
}

GST_START_TEST (test_legacy_factories_have_no_rank)
{
  if (!require_factory_or_skip ("vtdec"))
    return;

  assert_factory_rank ("vtdec", GST_RANK_NONE);
  assert_factory_rank_if_registered ("vtdec_hw", GST_RANK_NONE);
}

GST_END_TEST;

GST_START_TEST (test_codec_factories_have_codec_specific_sink_caps)
{
  guint i;

  if (!require_factory_or_skip ("vtdec"))
    return;

  for (i = 0; i < G_N_ELEMENTS (vtdec_elements); i++)
    assert_factory_sink_caps (vtdec_elements[i].name, vtdec_elements[i].caps);

  for (i = 0; i < G_N_ELEMENTS (vtdec_optional_elements); i++)
    assert_factory_sink_caps_if_registered (vtdec_optional_elements[i].name,
        vtdec_optional_elements[i].caps);
}

GST_END_TEST;

GST_START_TEST (test_codec_factories_are_ranked)
{
  guint i;

  if (!require_factory_or_skip ("vtdec"))
    return;

  for (i = 0; i < G_N_ELEMENTS (vtdec_elements); i++) {
    GstElementFactory *factory =
        gst_element_factory_find (vtdec_elements[i].name);
    guint rank;

    fail_unless (factory != NULL, "Missing factory %s", vtdec_elements[i].name);
    rank = gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory));
    fail_unless (rank > GST_RANK_NONE, "Expected %s to be ranked",
        vtdec_elements[i].name);
    gst_object_unref (factory);
  }

  for (i = 0; i < G_N_ELEMENTS (vtdec_optional_elements); i++) {
    GstElementFactory *factory =
        gst_element_factory_find (vtdec_optional_elements[i].name);
    guint rank;

    if (factory == NULL)
      continue;

    rank = gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory));
    fail_unless (rank > GST_RANK_NONE, "Expected %s to be ranked",
        vtdec_optional_elements[i].name);
    gst_object_unref (factory);
  }
}

GST_END_TEST;

GST_START_TEST (test_hw_factories_match_codec_sink_caps)
{
  guint i;

  if (!require_factory_or_skip ("vtdec"))
    return;

  for (i = 0; i < G_N_ELEMENTS (vtdec_elements); i++) {
    gchar *name = g_strconcat (vtdec_elements[i].name, "_hw", NULL);

    assert_factory_sink_caps_if_registered (name, vtdec_elements[i].caps);
    g_free (name);
  }

  for (i = 0; i < G_N_ELEMENTS (vtdec_optional_elements); i++) {
    gchar *name = g_strconcat (vtdec_optional_elements[i].name, "_hw", NULL);

    assert_factory_sink_caps_if_registered (name,
        vtdec_optional_elements[i].caps);
    g_free (name);
  }
}

GST_END_TEST;

static Suite *
vtdec_suite (void)
{
  Suite *s = suite_create ("vtdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_legacy_factories_have_no_rank);
  tcase_add_test (tc_chain, test_codec_factories_have_codec_specific_sink_caps);
  tcase_add_test (tc_chain, test_codec_factories_are_ranked);
  tcase_add_test (tc_chain, test_hw_factories_match_codec_sink_caps);

  return s;
}

GST_CHECK_MAIN (vtdec);
