/* GStreamer unit tests for GstAlphaDecodeBin
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

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

static GstStaticPadTemplate test_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
static GstStaticPadTemplate test_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
static GstStaticPadTemplate test_alpha_src_template =
GST_STATIC_PAD_TEMPLATE ("alpha", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
static GstStaticPadTemplate test_alpha_sink_template =
GST_STATIC_PAD_TEMPLATE ("alpha", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

typedef GstElement TestAlphaDemux;
typedef GstElementClass TestAlphaDemuxClass;

GType test_alpha_demux_get_type (void);
G_DEFINE_TYPE (TestAlphaDemux, test_alpha_demux, GST_TYPE_ELEMENT);

static void
test_alpha_demux_class_init (TestAlphaDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);
  gst_element_class_add_static_pad_template (element_class, &test_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &test_alpha_src_template);
  gst_element_class_set_static_metadata (element_class, "Test Alpha Demux",
      "Demuxer/Video", "Test alpha demux element", "GStreamer");
}

static void
test_alpha_demux_init (TestAlphaDemux * self)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (self);

  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "sink"), "sink"));
  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "src"), "src"));
  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "alpha"), "alpha"));
}

typedef GstElement TestAlphaCombine;
typedef GstElementClass TestAlphaCombineClass;

GType test_alpha_combine_get_type (void);
G_DEFINE_TYPE (TestAlphaCombine, test_alpha_combine, GST_TYPE_ELEMENT);

static void
test_alpha_combine_class_init (TestAlphaCombineClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &test_alpha_sink_template);
  gst_element_class_add_static_pad_template (element_class, &test_src_template);
  gst_element_class_set_static_metadata (element_class, "Test Alpha Combine",
      "Filter/Video", "Test alpha combine element", "GStreamer");
}

static void
test_alpha_combine_init (TestAlphaCombine * self)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (self);

  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "sink"), "sink"));
  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "alpha"), "alpha"));
  gst_element_add_pad (GST_ELEMENT (self),
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
              "src"), "src"));
}

typedef GstAlphaDecodeBin TestAlphaDecodeBin;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinClass;

GType test_alpha_decode_bin_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBin, test_alpha_decode_bin,
    GST_TYPE_ALPHA_DECODE_BIN);

static GstElement *
test_alpha_decode_bin_create_role_element (GstAlphaDecodeBin * self,
    GstAlphaDecodeBinRole role, const GstCaps * input_caps, GError ** error)
{
  return NULL;
}

static void
test_alpha_decode_bin_configure_class (GstAlphaDecodeBinClass * klass)
{
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_DEMUX, "testalphademux");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER, "identity");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER, "identity");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_COMBINE, "testalphacombine");
}

static void
test_alpha_decode_bin_class_init (TestAlphaDecodeBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);

  klass->create_role_element = test_alpha_decode_bin_create_role_element;
  test_alpha_decode_bin_configure_class (klass);
}

static void
test_alpha_decode_bin_init (TestAlphaDecodeBin * self)
{
}

typedef GstAlphaDecodeBin TestAlphaDecodeBinError;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinErrorClass;

GType test_alpha_decode_bin_error_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBinError, test_alpha_decode_bin_error,
    GST_TYPE_ALPHA_DECODE_BIN);

static GstElement *
test_alpha_decode_bin_error_create_role_element (GstAlphaDecodeBin * self,
    GstAlphaDecodeBinRole role, const GstCaps * input_caps, GError ** error)
{
  if (role != GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER)
    return NULL;

  g_set_error_literal (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
      "synthetic role creation failure");
  return NULL;
}

static void
test_alpha_decode_bin_error_class_init (TestAlphaDecodeBinErrorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);

  klass->create_role_element = test_alpha_decode_bin_error_create_role_element;
  test_alpha_decode_bin_configure_class (klass);
}

static void
test_alpha_decode_bin_error_init (TestAlphaDecodeBinError * self)
{
}

typedef GstAlphaDecodeBin TestAlphaDecodeBinElementAndError;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinElementAndErrorClass;

GType test_alpha_decode_bin_element_and_error_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBinElementAndError,
    test_alpha_decode_bin_element_and_error, GST_TYPE_ALPHA_DECODE_BIN);

static GstElement *test_alpha_decode_bin_element_and_error_create_role_element
    (GstAlphaDecodeBin * self, GstAlphaDecodeBinRole role,
    const GstCaps * input_caps, GError ** error)
{
  if (role != GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER)
    return NULL;

  g_set_error_literal (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
      "synthetic warning");
  return gst_element_factory_make ("identity", "maindec");
}

static void
    test_alpha_decode_bin_element_and_error_class_init
    (TestAlphaDecodeBinElementAndErrorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);

  klass->create_role_element =
      test_alpha_decode_bin_element_and_error_create_role_element;
  test_alpha_decode_bin_configure_class (klass);
}

static void
    test_alpha_decode_bin_element_and_error_init
    (TestAlphaDecodeBinElementAndError * self)
{
}

typedef GstAlphaDecodeBin TestAlphaDecodeBinNoFactory;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinNoFactoryClass;

GType test_alpha_decode_bin_no_factory_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBinNoFactory, test_alpha_decode_bin_no_factory,
    GST_TYPE_ALPHA_DECODE_BIN);

static void
test_alpha_decode_bin_no_factory_class_init (TestAlphaDecodeBinNoFactoryClass *
    klass)
{
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &test_sink_template);
}

static void
test_alpha_decode_bin_no_factory_init (TestAlphaDecodeBinNoFactory * self)
{
}

typedef GstAlphaDecodeBin TestAlphaDecodeBinMissingFactory;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinMissingFactoryClass;

GType test_alpha_decode_bin_missing_factory_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBinMissingFactory,
    test_alpha_decode_bin_missing_factory, GST_TYPE_ALPHA_DECODE_BIN);

static void
    test_alpha_decode_bin_missing_factory_class_init
    (TestAlphaDecodeBinMissingFactoryClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_DEMUX, "testalphademux");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER, "missing-alpha-test-decoder");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER, "identity");
  gst_alpha_decode_bin_class_set_role_factory_name (klass,
      GST_ALPHA_DECODE_BIN_ROLE_COMBINE, "testalphacombine");
}

static void
test_alpha_decode_bin_missing_factory_init (TestAlphaDecodeBinMissingFactory *
    self)
{
}

typedef GstAlphaDecodeBin TestAlphaDecodeBinCapsChoice;
typedef GstAlphaDecodeBinClass TestAlphaDecodeBinCapsChoiceClass;

GType test_alpha_decode_bin_caps_choice_get_type (void);
G_DEFINE_TYPE (TestAlphaDecodeBinCapsChoice, test_alpha_decode_bin_caps_choice,
    GST_TYPE_ALPHA_DECODE_BIN);

static GstElement *
test_alpha_decode_bin_caps_choice_create_role_element (GstAlphaDecodeBin * self,
    GstAlphaDecodeBinRole role, const GstCaps * input_caps, GError ** error)
{
  GstCaps *expected_caps;
  gboolean matches;

  if (role != GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER)
    return NULL;

  if (!input_caps) {
    g_set_error_literal (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
        "no input caps");
    return NULL;
  }

  expected_caps = gst_caps_from_string ("application/x-test-alpha, "
      "variant = (string) expected");
  matches = gst_caps_can_intersect (input_caps, expected_caps);
  gst_caps_unref (expected_caps);

  if (!matches) {
    g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
        "unexpected input caps %" GST_PTR_FORMAT, input_caps);
    return NULL;
  }

  return gst_element_factory_make ("identity", "maindec");
}

static void
    test_alpha_decode_bin_caps_choice_class_init
    (TestAlphaDecodeBinCapsChoiceClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &test_sink_template);

  klass->create_role_element =
      test_alpha_decode_bin_caps_choice_create_role_element;
  test_alpha_decode_bin_configure_class (klass);
}

static void
test_alpha_decode_bin_caps_choice_init (TestAlphaDecodeBinCapsChoice * self)
{
}

static void
register_test_elements (void)
{
  static gsize registered = 0;

  if (g_once_init_enter (&registered)) {
    fail_unless (gst_element_register (NULL, "testalphademux", GST_RANK_NONE,
            test_alpha_demux_get_type ()));
    fail_unless (gst_element_register (NULL, "testalphacombine",
            GST_RANK_NONE, test_alpha_combine_get_type ()));
    g_once_init_leave (&registered, 1);
  }
}

GST_START_TEST (test_fallback_and_delayed_construction)
{
  GstElement *bin;
  GstElement *child;

  register_test_elements ();

  bin = g_object_new (test_alpha_decode_bin_get_type (), NULL);
  child = gst_bin_get_by_name (GST_BIN (bin), "maindec");
  fail_unless (child == NULL);

  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  child = gst_bin_get_by_name (GST_BIN (bin), "maindec");
  fail_unless (child != NULL);
  gst_object_unref (child);

  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  child = gst_bin_get_by_name (GST_BIN (bin), "maindec");
  fail_unless (child == NULL);

  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_hook_error_fails_construction)
{
  GstElement *bin;

  register_test_elements ();

  bin = g_object_new (test_alpha_decode_bin_error_get_type (), NULL);
  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_READY),
      GST_STATE_CHANGE_FAILURE);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_element_plus_error_uses_element)
{
  GstElement *bin;

  register_test_elements ();

  bin =
      g_object_new (test_alpha_decode_bin_element_and_error_get_type (), NULL);
  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_hook_receives_upstream_caps)
{
  GstElement *pipeline, *src, *filter, *bin;
  GstCaps *caps;

  register_test_elements ();

  pipeline = gst_pipeline_new (NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  filter = gst_element_factory_make ("capsfilter", NULL);
  bin = g_object_new (test_alpha_decode_bin_caps_choice_get_type (), NULL);

  fail_unless (pipeline != NULL);
  fail_unless (src != NULL);
  fail_unless (filter != NULL);
  fail_unless (bin != NULL);

  caps = gst_caps_from_string ("application/x-test-alpha, "
      "variant = (string) expected");
  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add_many (GST_BIN (pipeline), src, filter, bin, NULL);
  fail_unless (gst_element_link_many (src, filter, bin, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_unset_factory_name_fails_construction)
{
  GstElement *bin;

  register_test_elements ();

  bin = g_object_new (test_alpha_decode_bin_no_factory_get_type (), NULL);
  fail_unless_equals_int (gst_element_set_state (bin, GST_STATE_READY),
      GST_STATE_CHANGE_FAILURE);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_missing_factory_posts_missing_plugin_message)
{
  GstElement *pipeline, *bin;
  GstBus *bus;
  GstMessage *message;
  const GstStructure *s;

  register_test_elements ();

  pipeline = gst_pipeline_new (NULL);
  bin = g_object_new (test_alpha_decode_bin_missing_factory_get_type (), NULL);
  gst_bin_add (GST_BIN (pipeline), bin);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_FAILURE);

  bus = gst_element_get_bus (pipeline);
  message = gst_bus_pop_filtered (bus, GST_MESSAGE_ELEMENT | GST_MESSAGE_ERROR);
  fail_unless (message != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (message), GST_MESSAGE_ELEMENT);
  s = gst_message_get_structure (message);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "element");
  fail_unless_equals_string (gst_structure_get_string (s, "detail"),
      "missing-alpha-test-decoder");
  gst_message_unref (message);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
alphadecodebin_suite (void)
{
  Suite *s = suite_create ("alphadecodebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_fallback_and_delayed_construction);
  tcase_add_test (tc_chain, test_hook_error_fails_construction);
  tcase_add_test (tc_chain, test_element_plus_error_uses_element);
  tcase_add_test (tc_chain, test_hook_receives_upstream_caps);
  tcase_add_test (tc_chain, test_unset_factory_name_fails_construction);
  tcase_add_test (tc_chain, test_missing_factory_posts_missing_plugin_message);

  return s;
}

GST_CHECK_MAIN (alphadecodebin);
