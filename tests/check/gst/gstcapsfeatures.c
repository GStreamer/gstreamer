/* GStreamer
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstcapsfeatures.c: Unit test for GstCapsFeatures
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
#include <gst/gstcapsfeatures.h>

GST_START_TEST (test_basic_operations)
{
  GstCapsFeatures *a, *b;

  a = gst_caps_features_new ("m:abc", "m:def", "m:ghi", NULL);
  fail_unless (a != NULL);
  b = gst_caps_features_copy (a);
  fail_unless (b != NULL);
  fail_unless (gst_caps_features_is_equal (a, b));
  fail_if (gst_caps_features_is_equal (a,
          GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY));
  fail_unless_equals_int (gst_caps_features_get_size (a), 3);
  fail_unless_equals_string (gst_caps_features_get_nth (a, 1), "m:def");
  gst_caps_features_add (b, "m:jkl");
  fail_if (gst_caps_features_is_equal (a, b));
  fail_unless_equals_int (gst_caps_features_get_size (b), 4);
  fail_unless_equals_string (gst_caps_features_get_nth (b, 3), "m:jkl");
  gst_caps_features_add (b, "m:jkl");
  fail_unless_equals_int (gst_caps_features_get_size (b), 4);

  gst_caps_features_remove (b, "m:jkl");
  fail_unless (gst_caps_features_is_equal (a, b));
  gst_caps_features_remove (b, "m:abc");
  gst_caps_features_add (b, "m:abc");
  fail_unless (gst_caps_features_is_equal (a, b));
  gst_caps_features_remove (b, "m:abc");
  gst_caps_features_remove (b, "m:def");
  gst_caps_features_remove (b, "m:ghi");
  fail_unless (gst_caps_features_is_equal (b,
          GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY));
  gst_caps_features_add (b, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);

  gst_caps_features_free (a);
  gst_caps_features_free (b);
}

GST_END_TEST;

GST_START_TEST (test_from_to_string)
{
  GstCapsFeatures *a, *b;
  gchar *str;

  a = gst_caps_features_new ("m:abc", "m:def", "m:ghi", NULL);
  fail_unless (a != NULL);
  str = gst_caps_features_to_string (a);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "m:abc, m:def, m:ghi");
  b = gst_caps_features_from_string (str);
  fail_unless (b != NULL);
  fail_unless (gst_caps_features_is_equal (a, b));
  gst_caps_features_free (a);
  gst_caps_features_free (b);
  g_free (str);

  a = gst_caps_features_new_any ();
  fail_unless (a != NULL);
  fail_unless (gst_caps_features_is_any (a));
  str = gst_caps_features_to_string (a);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "ANY");
  b = gst_caps_features_from_string (str);
  fail_unless (b != NULL);
  fail_unless (gst_caps_features_is_equal (a, b));
  fail_unless (gst_caps_features_is_any (b));
  gst_caps_features_free (a);
  gst_caps_features_free (b);
  g_free (str);
}

GST_END_TEST;

static Suite *
gst_capsfeatures_suite (void)
{
  Suite *s = suite_create ("GstCapsFeatures");
  TCase *tc_chain = tcase_create ("operations");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_basic_operations);
  tcase_add_test (tc_chain, test_from_to_string);

  return s;
}

GST_CHECK_MAIN (gst_capsfeatures);
