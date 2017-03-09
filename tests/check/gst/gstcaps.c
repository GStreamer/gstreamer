/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstcaps.c: Unit test for GstCaps
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
#include <gst/gstcaps.h>
#include "capslist.h"

GST_START_TEST (test_from_string)
{
  GstCaps *caps;
  GstCaps *caps2;
  gchar *to_str;
  int i;

  for (i = 0; i < G_N_ELEMENTS (caps_list); i++) {
    caps = gst_caps_from_string (caps_list[i]);
    fail_if (caps == NULL,
        "Could not create caps from string %s\n", caps_list[i]);
    to_str = gst_caps_to_string (caps);
    fail_if (to_str == NULL,
        "Could not convert caps back to string %s\n", caps_list[i]);
    caps2 = gst_caps_from_string (to_str);
    fail_if (caps2 == NULL, "Could not create caps from string %s\n", to_str);

    fail_unless (gst_caps_is_equal (caps, caps));
    fail_unless (gst_caps_is_equal (caps, caps2));

    gst_caps_unref (caps);
    gst_caps_unref (caps2);
    g_free (to_str);
  }
}

GST_END_TEST;

GST_START_TEST (test_double_append)
{
  GstStructure *s1;
  GstCaps *c1;

  c1 = gst_caps_new_any ();
  s1 = gst_structure_from_string ("audio/x-raw,rate=44100", NULL);
  gst_caps_append_structure (c1, s1);
  ASSERT_CRITICAL (gst_caps_append_structure (c1, s1));

  gst_caps_unref (c1);
}

GST_END_TEST;

GST_START_TEST (test_mutability)
{
  GstStructure *s1;
  GstCaps *c1;
  gint ret;

  c1 = gst_caps_new_any ();
  s1 = gst_structure_from_string ("audio/x-raw,rate=44100", NULL);
  gst_structure_set (s1, "rate", G_TYPE_INT, 48000, NULL);
  gst_caps_append_structure (c1, s1);
  gst_structure_set (s1, "rate", G_TYPE_INT, 22500, NULL);
  gst_caps_ref (c1);
  ASSERT_CRITICAL (gst_structure_set (s1, "rate", G_TYPE_INT, 11250, NULL));
  fail_unless (gst_structure_get_int (s1, "rate", &ret));
  fail_unless (ret == 22500);
  ASSERT_CRITICAL (gst_caps_set_simple (c1, "rate", G_TYPE_INT, 11250, NULL));
  fail_unless (gst_structure_get_int (s1, "rate", &ret));
  fail_unless (ret == 22500);
  gst_caps_unref (c1);
  gst_structure_set (s1, "rate", G_TYPE_INT, 11250, NULL);
  fail_unless (gst_structure_get_int (s1, "rate", &ret));
  fail_unless (ret == 11250);
  gst_caps_set_simple (c1, "rate", G_TYPE_INT, 1, NULL);
  fail_unless (gst_structure_get_int (s1, "rate", &ret));
  fail_unless (ret == 1);
  gst_caps_unref (c1);
}

GST_END_TEST;

GST_START_TEST (test_static_caps)
{
  static GstStaticCaps scaps = GST_STATIC_CAPS ("audio/x-raw,rate=44100");
  GstCaps *caps1;
  GstCaps *caps2;
  static GstStaticCaps sany = GST_STATIC_CAPS_ANY;
  static GstStaticCaps snone = GST_STATIC_CAPS_NONE;

  /* caps creation */
  caps1 = gst_static_caps_get (&scaps);
  fail_unless (caps1 != NULL);
  /* 1 refcount core, one from us */
  fail_unless (GST_CAPS_REFCOUNT (caps1) == 2);

  /* caps should be the same */
  caps2 = gst_static_caps_get (&scaps);
  fail_unless (caps2 != NULL);
  /* 1 refcount core, two from us */
  fail_unless (GST_CAPS_REFCOUNT (caps1) == 3);
  /* caps must be equal */
  fail_unless (caps1 == caps2);

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);

  caps1 = gst_static_caps_get (&sany);
  fail_unless (gst_caps_is_equal (caps1, GST_CAPS_ANY));
  caps2 = gst_static_caps_get (&snone);
  fail_unless (gst_caps_is_equal (caps2, GST_CAPS_NONE));
  fail_if (gst_caps_is_equal (caps1, caps2));
  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
}

GST_END_TEST;

static const gchar non_simple_caps_string[] =
    "video/x-raw, format=(string)I420, framerate=(fraction)[ 1/100, 100 ], "
    "width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-raw, "
    "format=(string)YUY2, framerate=(fraction)[ 1/100, 100 ], width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ]; video/x-raw, format=(string)RGB8_PALETTED, "
    "framerate=(fraction)[ 1/100, 100 ], width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ]; video/x-raw, "
    "format=(string){ I420, YUY2, YV12 }, width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ], framerate=(fraction)[ 1/100, 100 ]";

static gboolean
check_string_list (const GValue * format_value)
{
  const GValue *string_value;
  gboolean got_rgb8 = FALSE;
  gboolean got_yv12 = FALSE;
  gboolean got_i420 = FALSE;
  gboolean got_yuy2 = FALSE;
  const gchar *string;

  string_value = gst_value_list_get_value (format_value, 0);
  fail_unless (string_value != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (string_value));
  string = g_value_get_string (string_value);
  fail_unless (string != NULL);
  got_rgb8 = got_rgb8 || (g_str_equal (string, "RGB8_PALETTED"));
  got_i420 = got_i420 || (g_str_equal (string, "I420"));
  got_yuy2 = got_yuy2 || (g_str_equal (string, "YUY2"));
  got_yv12 = got_yv12 || (g_str_equal (string, "YV12"));

  string_value = gst_value_list_get_value (format_value, 1);
  fail_unless (string_value != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (string_value));
  string = g_value_get_string (string_value);
  fail_unless (string != NULL);
  got_rgb8 = got_rgb8 || (g_str_equal (string, "RGB8_PALETTED"));
  got_i420 = got_i420 || (g_str_equal (string, "I420"));
  got_yuy2 = got_yuy2 || (g_str_equal (string, "YUY2"));
  got_yv12 = got_yv12 || (g_str_equal (string, "YV12"));

  string_value = gst_value_list_get_value (format_value, 2);
  fail_unless (string_value != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (string_value));
  string = g_value_get_string (string_value);
  fail_unless (string != NULL);
  got_rgb8 = got_rgb8 || (g_str_equal (string, "RGB8_PALETTED"));
  got_i420 = got_i420 || (g_str_equal (string, "I420"));
  got_yuy2 = got_yuy2 || (g_str_equal (string, "YUY2"));
  got_yv12 = got_yv12 || (g_str_equal (string, "YV12"));

  string_value = gst_value_list_get_value (format_value, 3);
  fail_unless (string_value != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (string_value));
  string = g_value_get_string (string_value);
  fail_unless (string != NULL);
  got_rgb8 = got_rgb8 || (g_str_equal (string, "RGB8_PALETTED"));
  got_i420 = got_i420 || (g_str_equal (string, "I420"));
  got_yuy2 = got_yuy2 || (g_str_equal (string, "YUY2"));
  got_yv12 = got_yv12 || (g_str_equal (string, "YV12"));

  return (got_rgb8 && got_i420 && got_yuy2 && got_yv12);
}

GST_START_TEST (test_simplify)
{
  GstStructure *s1;
  GstCaps *caps;

  caps = gst_caps_from_string (non_simple_caps_string);
  fail_unless (caps != NULL,
      "gst_caps_from_string (non_simple_caps_string) failed");

  caps = gst_caps_simplify (caps);
  fail_unless (caps != NULL, "gst_caps_simplify() should have worked");

  /* check simplified caps, should be:
   *
   * video/x-raw, format=(string){ RGB8_PALETTED, YV12, YUY2, I420 },
   *     width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ],
   *     framerate=(fraction)[ 1/100, 100 ]
   */
  GST_DEBUG ("simplyfied %" GST_PTR_FORMAT, caps);
  fail_unless (gst_caps_get_size (caps) == 1);
  s1 = gst_caps_get_structure (caps, 0);
  fail_unless (s1 != NULL);

  fail_unless (gst_structure_has_name (s1, "video/x-raw"));
  {
    const GValue *framerate_value;
    const GValue *format_value;
    const GValue *width_value;
    const GValue *height_value;
    const GValue *val_fps;
    GValue test_fps = { 0, };
    gint min_width, max_width;
    gint min_height, max_height;

    format_value = gst_structure_get_value (s1, "format");
    fail_unless (format_value != NULL);
    fail_unless (GST_VALUE_HOLDS_LIST (format_value));
    fail_unless (gst_value_list_get_size (format_value) == 4);
    fail_unless (check_string_list (format_value) == TRUE);

    g_value_init (&test_fps, GST_TYPE_FRACTION);
    framerate_value = gst_structure_get_value (s1, "framerate");
    fail_unless (framerate_value != NULL);
    fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (framerate_value));

    val_fps = gst_value_get_fraction_range_min (framerate_value);
    gst_value_set_fraction (&test_fps, 1, 100);
    fail_unless (gst_value_compare (&test_fps, val_fps) == GST_VALUE_EQUAL);

    val_fps = gst_value_get_fraction_range_max (framerate_value);
    gst_value_set_fraction (&test_fps, 100, 1);
    fail_unless (gst_value_compare (&test_fps, val_fps) == GST_VALUE_EQUAL);

    g_value_unset (&test_fps);

    width_value = gst_structure_get_value (s1, "width");
    fail_unless (width_value != NULL);
    fail_unless (GST_VALUE_HOLDS_INT_RANGE (width_value));
    min_width = gst_value_get_int_range_min (width_value);
    max_width = gst_value_get_int_range_max (width_value);
    fail_unless (min_width == 16 && max_width == 4096);

    height_value = gst_structure_get_value (s1, "height");
    fail_unless (height_value != NULL);
    fail_unless (GST_VALUE_HOLDS_INT_RANGE (height_value));
    min_height = gst_value_get_int_range_min (height_value);
    max_height = gst_value_get_int_range_max (height_value);
    fail_unless (min_height == 16 && max_height == 4096);
  }

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_truncate)
{
  GstCaps *caps;

  caps = gst_caps_from_string (non_simple_caps_string);
  fail_unless (caps != NULL,
      "gst_caps_from_string (non_simple_caps_string) failed");
  fail_unless_equals_int (gst_caps_get_size (caps), 4);
  caps = gst_caps_truncate (caps);
  fail_unless_equals_int (gst_caps_get_size (caps), 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_subset)
{
  GstCaps *c1, *c2;

  c1 = gst_caps_from_string ("video/x-raw; video/x-raw");
  c2 = gst_caps_from_string ("video/x-raw, format=(string)YUY2");
  fail_unless (gst_caps_is_subset (c2, c1));
  fail_if (gst_caps_is_subset (c1, c2));
  gst_caps_unref (c1);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string
      ("audio/x-raw, channels=(int)[ 1, 2 ], rate=(int)44100");
  c2 = gst_caps_from_string ("audio/x-raw, channels=(int)1, rate=(int)44100");
  fail_unless (gst_caps_is_subset (c2, c1));
  fail_if (gst_caps_is_subset (c1, c2));
  gst_caps_unref (c1);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("audio/x-raw, channels=(int) {1}");
  c2 = gst_caps_from_string ("audio/x-raw, channels=(int)1");
  fail_unless (gst_caps_is_subset (c2, c1));
  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_equal (c1, c2));
  gst_caps_unref (c1);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string
      ("audio/x-raw, rate=(int)44100, channels=(int)3, format=(string)U16_LE");
  c2 = gst_caps_from_string
      ("audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 2147483647 ], format=(string){ S16_LE, U16_LE }");
  fail_unless (gst_caps_is_subset (c1, c2));
  fail_if (gst_caps_is_subset (c2, c1));
  gst_caps_unref (c1);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-h264, parsed=(boolean)true");
  c2 = gst_caps_from_string
      ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)nal");
  fail_if (gst_caps_is_subset (c2, c1));
  fail_if (gst_caps_is_subset (c1, c2));
  fail_if (gst_caps_is_equal (c1, c2));
  gst_caps_unref (c1);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_subset_duplication)
{
  GstCaps *c1, *c2;

  c1 = gst_caps_from_string ("audio/x-raw, format=(string)F32LE");
  c2 = gst_caps_from_string ("audio/x-raw, format=(string)F32LE");

  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));

  gst_caps_unref (c2);
  c2 = gst_caps_from_string ("audio/x-raw, format=(string){ F32LE }");

  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));

  gst_caps_unref (c2);
  c2 = gst_caps_from_string ("audio/x-raw, format=(string){ F32LE, F32LE }");

  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));

  gst_caps_unref (c2);
  c2 = gst_caps_from_string
      ("audio/x-raw, format=(string){ F32LE, F32LE, F32LE }");

  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));

  gst_caps_unref (c1);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_merge_fundamental)
{
  GstCaps *c1, *c2;

  /* ANY + specific = ANY */
  c1 = gst_caps_from_string ("audio/x-raw,rate=44100");
  c2 = gst_caps_new_any ();
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 0, NULL);
  fail_unless (gst_caps_is_any (c2), NULL);
  gst_caps_unref (c2);

  /* specific + ANY = ANY */
  c2 = gst_caps_from_string ("audio/x-raw,rate=44100");
  c1 = gst_caps_new_any ();
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 0, NULL);
  fail_unless (gst_caps_is_any (c2), NULL);
  gst_caps_unref (c2);

  /* EMPTY + specific = specific */
  c1 = gst_caps_from_string ("audio/x-raw,rate=44100");
  c2 = gst_caps_new_empty ();
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  fail_if (gst_caps_is_empty (c2), NULL);
  gst_caps_unref (c2);

  /* specific + EMPTY = specific */
  c2 = gst_caps_from_string ("audio/x-raw,rate=44100");
  c1 = gst_caps_new_empty ();
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  fail_if (gst_caps_is_empty (c2), NULL);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_merge_same)
{
  GstCaps *c1, *c2, *test;

  /* this is the same */
  c1 = gst_caps_from_string ("audio/x-raw,rate=44100,channels=1");
  c2 = gst_caps_from_string ("audio/x-raw,rate=44100,channels=1");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,rate=44100,channels=1");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (test);
  gst_caps_unref (c2);

  /* and so is this */
  c1 = gst_caps_from_string ("audio/x-raw,rate=44100,channels=1");
  c2 = gst_caps_from_string ("audio/x-raw,channels=1,rate=44100");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AABB");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AABB");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-bar, data=(buffer)AA");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_merge_subset)
{
  GstCaps *c1, *c2, *test;

  /* the 2nd is already covered */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,2]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=1");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,2]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* here it is not */
  c2 = gst_caps_from_string ("audio/x-raw,channels=1,rate=44100");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[1,2],rate=44100");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,2],rate=44100");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,3]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[1,2]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,3]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[1,2]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[2,4]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[2,3]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* these caps cannot be merged */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[2,3]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[1,4]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test =
      gst_caps_from_string
      ("audio/x-raw,channels=[2,3];audio/x-raw,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* these caps cannot be merged */
  c2 = gst_caps_from_string ("audio/x-raw,channels=[1,2]");
  c1 = gst_caps_from_string ("audio/x-raw,channels=[1,3]");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test =
      gst_caps_from_string
      ("audio/x-raw,channels=[1,2];audio/x-raw,channels=[1,3]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("audio/x-raw,channels={1,2}");
  c1 = gst_caps_from_string ("audio/x-raw,channels={1,2,3,4}");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels={1,2};"
      "audio/x-raw,channels={1,2,3,4}");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("audio/x-raw,channels={1,2}");
  c1 = gst_caps_from_string ("audio/x-raw,channels={1,3}");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels={1,2};"
      "audio/x-raw,channels={1,3}");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("video/x-raw, framerate=(fraction){ 15/2, 5/1 }");
  c1 = gst_caps_from_string ("video/x-raw, framerate=(fraction){ 15/1, 5/1 }");
  test = gst_caps_copy (c1);
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_is_subset (test, c2));
  gst_caps_unref (test);
  gst_caps_unref (c2);

  c2 = gst_caps_from_string ("audio/x-raw");
  c1 = gst_caps_from_string ("audio/x-raw,channels=1");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("audio/x-raw,channels=1");
  c1 = gst_caps_from_string ("audio/x-raw");
  c2 = gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw,channels=1; audio/x-raw");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);
}

GST_END_TEST;

GST_START_TEST (test_intersect)
{
  GstStructure *s;
  GstCaps *c1, *c2, *ci1, *ci2;

  /* field not specified = any value possible, so the intersection
   * should keep fields which are only part of one set of caps */
  c2 = gst_caps_from_string ("video/x-raw,format=(string)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw,format=(string)I420");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_get_size (ci1) == 1, NULL);
  s = gst_caps_get_structure (ci1, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_get_size (ci2) == 1, NULL);
  s = gst_caps_get_structure (ci2, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);

  fail_unless (gst_caps_is_equal (ci1, ci2));

  gst_caps_unref (ci1);
  gst_caps_unref (ci2);

  gst_caps_unref (c1);
  gst_caps_unref (c2);

  /* ========== */

  c2 = gst_caps_from_string ("video/x-raw,format=(string)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw,format=(string)I420,width=30");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_is_empty (ci1), NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_is_empty (ci2), NULL);

  fail_unless (gst_caps_is_equal (ci1, ci2));

  gst_caps_unref (ci1);
  gst_caps_unref (ci2);

  gst_caps_unref (c1);
  gst_caps_unref (c2);

  /* ========== */

  c2 = gst_caps_from_string ("video/x-raw,format=(string)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw2,format=(string)I420,width=20");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_is_empty (ci1), NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_is_empty (ci2), NULL);

  fail_unless (gst_caps_is_equal (ci1, ci2));

  gst_caps_unref (ci1);
  gst_caps_unref (ci2);

  gst_caps_unref (c1);
  gst_caps_unref (c2);

  /* ========== */

  c2 = gst_caps_from_string ("video/x-raw,format=(string)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw,format=(string)I420,height=30");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_get_size (ci1) == 1, NULL);
  s = gst_caps_get_structure (ci1, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);
  fail_unless (gst_structure_get_value (s, "height") != NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_get_size (ci2) == 1, NULL);
  s = gst_caps_get_structure (ci2, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "height") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);

  fail_unless (gst_caps_is_equal (ci1, ci2));

  gst_caps_unref (ci1);
  gst_caps_unref (ci2);

  gst_caps_unref (c1);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_intersect2)
{
  GstCaps *caps1, *caps2, *icaps;

  /* tests array subtraction */
  caps1 = gst_caps_from_string ("audio/x-raw, "
      "channel-positions=(int)<                      "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         " "{ 1, 2, 3, 4, 5, 6 }>");
  caps2 = gst_caps_from_string ("audio/x-raw, "
      "channel-positions=(int)< 1, 2, 3, 4, 5, 6 >");
  icaps = gst_caps_intersect (caps1, caps2);
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, caps2));
  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
  gst_caps_unref (icaps);

  /* ===== */

  caps1 = gst_caps_from_string ("some/type, foo=(int)< { 1, 2 }, { 3, 4} >");
  caps2 = gst_caps_from_string ("some/type, foo=(int)< 1, 3 >");
  icaps = gst_caps_intersect (caps1, caps2);
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, caps2));
  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
  gst_caps_unref (icaps);
}

GST_END_TEST;

GST_START_TEST (test_intersect_list_duplicate)
{
  GstCaps *caps1, *caps2, *icaps;

  /* make sure we don't take too long to intersect these.. */
  caps1 = gst_caps_from_string ("video/x-raw, format=(string)YV12; "
      "video/x-raw, format=(string)I420; video/x-raw, format=(string)YUY2; "
      "video/x-raw, format=(string)UYVY; "
      "video/x-raw, format=(string){ I420, YV12, YUY2, UYVY, AYUV, RGBx, BGRx,"
      " xRGB, xBGR, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } } } } }, BGRA, ARGB, { ABGR, ABGR, "
      "{ ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } } } } }, "
      "RGB, BGR, Y41B, Y42B, YVYU, Y444 }; "
      "video/x-raw, format=(string){ I420, YV12, YUY2, UYVY, AYUV, RGBx, BGRx, "
      "xRGB, xBGR, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } } } } }, BGRA, ARGB, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } } } } }, "
      "RGB, BGR, Y41B, Y42B, YVYU, Y444, NV12, NV21 }; "
      "video/x-raw, format=(string){ I420, YV12, YUY2, UYVY, AYUV, RGBx, "
      "BGRx, xRGB, xBGR, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } }, { RGBA, RGBA, { RGBA, RGBA }, "
      "{ RGBA, RGBA, { RGBA, RGBA } } } }, BGRA, ARGB, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } }, "
      "{ ABGR, ABGR, { ABGR, ABGR }, { ABGR, ABGR, { ABGR, ABGR } } } }, "
      "RGB, BGR, Y41B, Y42B, YVYU, Y444, NV12, NV21 }");

  caps2 = gst_caps_copy (caps1);

  icaps = gst_caps_intersect (caps1, caps2);

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
  gst_caps_unref (icaps);
}

GST_END_TEST;

GST_START_TEST (test_intersect_zigzag)
{
  GstCaps *caps1, *caps2, *icaps, *result;

  /* tests if caps order is maintained */
  caps1 = gst_caps_from_string ("format/A; format/B; format/C; format/D");
  caps2 = gst_caps_from_string ("format/D; format/A; format/B; format/C");

  icaps = gst_caps_intersect_full (caps1, caps2, GST_CAPS_INTERSECT_ZIG_ZAG);
  result = gst_caps_from_string ("format/B; format/A; format/D; format/C");
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, result));
  gst_caps_unref (icaps);
  gst_caps_unref (result);

  icaps = gst_caps_intersect_full (caps2, caps1, GST_CAPS_INTERSECT_FIRST);
  result = gst_caps_from_string ("format/A; format/B; format/D; format/C");
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, result));
  gst_caps_unref (icaps);
  gst_caps_unref (result);

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
}

GST_END_TEST;


GST_START_TEST (test_intersect_first)
{
  GstCaps *caps1, *caps2, *icaps, *result;

  /* tests if caps order is maintained */
  caps1 = gst_caps_from_string ("format/A; format/B; format/C; format/D");
  caps2 = gst_caps_from_string ("format/C; format/D; format/A");
  icaps = gst_caps_intersect_full (caps1, caps2, GST_CAPS_INTERSECT_FIRST);
  result = gst_caps_from_string ("format/A; format/C; format/D");
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, result));
  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
  gst_caps_unref (icaps);
  gst_caps_unref (result);
}

GST_END_TEST;


GST_START_TEST (test_intersect_first2)
{
  GstCaps *caps1, *caps2, *icaps, *result;

  /* tests if caps order is maintained */
  caps1 = gst_caps_from_string ("format/A; format/B; format/C; format/D");
  caps2 = gst_caps_from_string ("format/D; format/A; format/B; format/C");

  icaps = gst_caps_intersect_full (caps1, caps2, GST_CAPS_INTERSECT_FIRST);
  result = gst_caps_from_string ("format/A; format/B; format/C; format/D");
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, result));
  gst_caps_unref (icaps);
  gst_caps_unref (result);

  icaps = gst_caps_intersect_full (caps2, caps1, GST_CAPS_INTERSECT_FIRST);
  result = gst_caps_from_string ("format/D; format/A; format/B; format/C");
  GST_LOG ("intersected caps: %" GST_PTR_FORMAT, icaps);
  fail_if (gst_caps_is_empty (icaps));
  fail_unless (gst_caps_is_equal (icaps, result));
  gst_caps_unref (icaps);
  gst_caps_unref (result);

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
}

GST_END_TEST;

GST_START_TEST (test_intersect_duplication)
{
  GstCaps *c1, *c2, *test;

  c1 = gst_caps_from_string
      ("audio/x-raw, format=(string)S16_LE, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 2 ]");
  c2 = gst_caps_from_string
      ("audio/x-raw, format=(string) { S16_LE, S16_BE, U16_LE, U16_BE }, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 2 ]; audio/x-raw, format=(string) { S16_LE, S16_BE, U16_LE, U16_BE }, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 11 ]; audio/x-raw, format=(string) { S16_LE, S16_BE, U16_LE, U16_BE }, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 11 ]");

  test = gst_caps_intersect_full (c1, c2, GST_CAPS_INTERSECT_FIRST);
  fail_unless_equals_int (gst_caps_get_size (test), 1);
  fail_unless (gst_caps_is_equal (c1, test));
  gst_caps_unref (c1);
  gst_caps_unref (c2);
  gst_caps_unref (test);
}

GST_END_TEST;

GST_START_TEST (test_intersect_flagset)
{
  GstCaps *c1, *c2, *test;
  GType test_flagset_type;
  GstSeekFlags test_flags, test_mask;
  gchar *test_string;

  /* Test that matching bits inside the mask intersect,
   * and bits outside the mask don't matter */
  c1 = gst_caps_from_string ("test/x-caps,field=ffd81d:fffff0");
  c2 = gst_caps_from_string ("test/x-caps,field=0fd81f:0ffff0");

  test = gst_caps_intersect_full (c1, c2, GST_CAPS_INTERSECT_FIRST);
  fail_unless_equals_int (gst_caps_get_size (test), 1);
  fail_unless (gst_caps_is_equal (c1, test));
  gst_caps_unref (c1);
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* Test that non-matching bits in the mask don't intersect */
  c1 = gst_caps_from_string ("test/x-caps,field=ff001d:0ffff0");
  c2 = gst_caps_from_string ("test/x-caps,field=0fd81f:0ffff0");

  test = gst_caps_intersect_full (c1, c2, GST_CAPS_INTERSECT_FIRST);
  fail_unless (gst_caps_is_empty (test));
  gst_caps_unref (c1);
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* Check custom flags type serialisation and de-serialisation */
  test_flagset_type = gst_flagset_register (GST_TYPE_SEEK_FLAGS);
  fail_unless (g_type_is_a (test_flagset_type, GST_TYPE_FLAG_SET));

  test_flags =
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_TRICKMODE |
      GST_SEEK_FLAG_TRICKMODE_KEY_UNITS;
  test_mask =
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_TRICKMODE |
      GST_SEEK_FLAG_TRICKMODE_NO_AUDIO;

  c1 = gst_caps_new_simple ("test/x-caps", "field", test_flagset_type,
      test_flags, test_mask, NULL);

  test_string = gst_caps_to_string (c1);
  fail_if (test_string == NULL);

  GST_DEBUG ("Serialised caps to %s", test_string);
  c2 = gst_caps_from_string (test_string);
  g_free (test_string);

  fail_unless (gst_caps_is_equal (c1, c2), "Caps %s != %s",
      gst_caps_to_string (c1), gst_caps_to_string (c2));

  gst_caps_unref (c1);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_union)
{
  GstCaps *c1, *c2, *test, *expect;

  /* Test that matching bits inside the masks union OK, */
  c1 = gst_caps_from_string ("test/x-caps,field=ffd81d:0ffff0");
  c2 = gst_caps_from_string ("test/x-caps,field=0fd81f:0ffff0");

  test = gst_caps_merge (c1, c2);
  test = gst_caps_simplify (test);
  /* c1, c2 now invalid */
  fail_unless_equals_int (gst_caps_get_size (test), 1);
  gst_caps_unref (test);

  /* Test that non-intersecting sets of masked bits are OK */
  c1 = gst_caps_from_string ("test/x-caps,field=ff001d:0ffff0");
  c2 = gst_caps_from_string ("test/x-caps,field=4fd81f:f00000");
  expect = gst_caps_from_string ("test/x-caps,field=4f001d:fffff0");
  test = gst_caps_simplify (gst_caps_merge (c1, c2));
  /* c1, c2 now invalid */
  GST_LOG ("Expected caps %" GST_PTR_FORMAT " got %" GST_PTR_FORMAT "\n",
      expect, test);
  fail_unless (gst_caps_is_equal (test, expect));
  gst_caps_unref (test);
  gst_caps_unref (expect);

  /* Test that partially-intersecting sets of masked bits that match are OK */
  c1 = gst_caps_from_string ("test/x-caps,field=ff001d:0ffff0");
  c2 = gst_caps_from_string ("test/x-caps,field=4fd81f:ff0000");
  expect = gst_caps_from_string ("test/x-caps,field=4f001d:fffff0");
  test = gst_caps_simplify (gst_caps_merge (c1, c2));
  /* c1, c2 now invalid */
  GST_LOG ("Expected caps %" GST_PTR_FORMAT " got %" GST_PTR_FORMAT "\n",
      expect, test);
  fail_unless (gst_caps_is_equal (test, expect));
  gst_caps_unref (test);
  gst_caps_unref (expect);
}

GST_END_TEST;

static gboolean
_caps_is_fixed_foreach (GQuark field_id, const GValue * value, gpointer unused)
{
  return gst_value_is_fixed (value);
}


GST_START_TEST (test_normalize)
{
  GstCaps *in, *norm, *out;
  guint i;

  in = gst_caps_from_string ("some/type, foo=(int){ 1 , 2 }");
  out = gst_caps_from_string ("some/type, foo=(int) 1; some/type, foo=(int) 2");
  norm = gst_caps_normalize (in);
  fail_if (gst_caps_is_empty (norm));
  fail_unless (gst_caps_is_equal (norm, out));
  for (i = 0; i < gst_caps_get_size (norm); i++) {
    GstStructure *st = gst_caps_get_structure (norm, i);
    /* Make sure all fields of all structures are fixed */
    fail_unless (gst_structure_foreach (st, _caps_is_fixed_foreach, NULL));
  }

  gst_caps_unref (out);
  gst_caps_unref (norm);

  in = gst_caps_from_string
      ("some/type, foo=(int){ 1 , 2 }, bar=(int){ 3, 4 }");
  out =
      gst_caps_from_string
      ("some/type, foo=(int) 1, bar=(int) 3; some/type, foo=(int) 2, bar=(int) 3;"
      "some/type, foo=(int) 1, bar=(int) 4; some/type, foo=(int) 2, bar=(int) 4;");
  norm = gst_caps_normalize (in);
  fail_if (gst_caps_is_empty (norm));
  fail_unless (gst_caps_is_equal (norm, out));
  for (i = 0; i < gst_caps_get_size (norm); i++) {
    GstStructure *st = gst_caps_get_structure (norm, i);
    /* Make sure all fields of all structures are fixed */
    fail_unless (gst_structure_foreach (st, _caps_is_fixed_foreach, NULL));
  }

  gst_caps_unref (out);
  gst_caps_unref (norm);

  in = gst_caps_from_string
      ("some/type, foo=(string){ 1 , 2 }, bar=(string) { 3 }");
  out =
      gst_caps_from_string
      ("some/type, foo=(string) 1, bar=(string) 3; some/type, foo=(string) 2, bar=(string) 3");
  norm = gst_caps_normalize (in);
  fail_if (gst_caps_is_empty (norm));
  fail_unless (gst_caps_is_equal (norm, out));
  for (i = 0; i < gst_caps_get_size (norm); i++) {
    GstStructure *st = gst_caps_get_structure (norm, i);
    /* Make sure all fields of all structures are fixed */
    fail_unless (gst_structure_foreach (st, _caps_is_fixed_foreach, NULL));
  }

  gst_caps_unref (out);
  gst_caps_unref (norm);
}

GST_END_TEST;

GST_START_TEST (test_broken)
{
  GstCaps *c1;

  /* NULL is not valid for media_type */
  ASSERT_CRITICAL (c1 =
      gst_caps_new_simple (NULL, "field", G_TYPE_INT, 1, NULL));
  fail_if (c1);

#ifndef G_DISABLE_CHECKS
  /* such a name is not valid, see gst_structure_validate_name() */
  ASSERT_CRITICAL (c1 =
      gst_caps_new_simple ("1#@abc", "field", G_TYPE_INT, 1, NULL));
  fail_if (c1);
#endif
}

GST_END_TEST;

GST_START_TEST (test_features)
{
  GstCaps *c1, *c2, *c3;
  GstStructure *s1, *s2;
  GstCapsFeatures *f1, *f2;
  gchar *str1;
  static GstStaticCaps scaps =
      GST_STATIC_CAPS
      ("video/x-raw(memory:EGLImage), width=320, height=[ 240, 260 ]");

  c1 = gst_caps_new_empty ();
  fail_unless (c1 != NULL);
  s1 = gst_structure_new ("video/x-raw", "width", G_TYPE_INT, 320, "height",
      GST_TYPE_INT_RANGE, 240, 260, NULL);
  fail_unless (s1 != NULL);
  f1 = gst_caps_features_new ("memory:EGLImage", NULL);
  fail_unless (f1 != NULL);

  gst_caps_append_structure_full (c1, s1, f1);
  s2 = gst_caps_get_structure (c1, 0);
  fail_unless (s1 == s2);
  f2 = gst_caps_get_features (c1, 0);
  fail_unless (f1 == f2);

  str1 = gst_caps_to_string (c1);
  fail_unless (str1 != NULL);
  c2 = gst_caps_from_string (str1);
  fail_unless (c2 != NULL);
  g_free (str1);

  fail_unless (gst_caps_is_equal (c1, c2));
  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));
  fail_unless (gst_caps_can_intersect (c1, c2));

  gst_caps_unref (c2);

  c2 = gst_caps_new_empty ();
  fail_unless (c2 != NULL);
  s2 = gst_structure_new ("video/x-raw", "width", G_TYPE_INT, 320, "height",
      GST_TYPE_INT_RANGE, 240, 260, NULL);
  fail_unless (s2 != NULL);
  f2 = gst_caps_features_new ("memory:VASurface", "meta:VAMeta", NULL);
  fail_unless (f2 != NULL);
  gst_caps_append_structure_full (c2, s2, f2);

  fail_if (gst_caps_is_equal (c1, c2));
  fail_if (gst_caps_is_subset (c1, c2));
  fail_if (gst_caps_is_subset (c2, c1));
  fail_if (gst_caps_can_intersect (c1, c2));

  str1 = gst_caps_to_string (c2);
  fail_unless (str1 != NULL);
  c3 = gst_caps_from_string (str1);
  fail_unless (c3 != NULL);
  g_free (str1);

  fail_unless (gst_caps_is_equal (c2, c3));
  fail_unless (gst_caps_is_subset (c2, c3));
  fail_unless (gst_caps_is_subset (c3, c2));
  fail_unless (gst_caps_can_intersect (c2, c3));

  f1 = gst_caps_get_features (c3, 0);
  fail_unless (f1 != NULL);
  fail_if (f1 == f2);
  gst_caps_features_contains (f1, "memory:VASurface");
  gst_caps_features_remove (f1, "memory:VASurface");
  fail_if (gst_caps_is_equal (c2, c3));
  fail_if (gst_caps_is_subset (c2, c3));
  fail_if (gst_caps_is_subset (c3, c2));
  fail_if (gst_caps_can_intersect (c2, c3));

  gst_caps_unref (c3);
  gst_caps_unref (c2);

  c2 = gst_static_caps_get (&scaps);
  fail_unless (c2 != NULL);
  fail_unless (gst_caps_is_equal (c1, c2));
  fail_unless (gst_caps_is_subset (c1, c2));
  fail_unless (gst_caps_is_subset (c2, c1));
  fail_unless (gst_caps_can_intersect (c1, c2));
  gst_caps_unref (c2);

  c2 = gst_caps_from_string
      ("video/x-raw(ANY), width=320, height=[ 240, 260 ]");
  fail_unless (c2 != NULL);
  fail_if (gst_caps_is_equal (c1, c2));
  fail_unless (gst_caps_is_subset (c1, c2));
  fail_if (gst_caps_is_subset (c2, c1));
  fail_unless (gst_caps_can_intersect (c1, c2));

  c3 = gst_caps_intersect (c1, c2);
  fail_unless (gst_caps_is_equal (c3, c1));

  gst_caps_unref (c3);
  gst_caps_unref (c2);
  gst_caps_unref (c1);

  c1 = gst_caps_from_string ("video/x-raw");
  c2 = gst_caps_from_string ("video/x-raw");

  f1 = gst_caps_get_features (c1, 0);
  gst_caps_features_add (f1, "memory:VASurface");

  fail_unless (gst_caps_features_is_equal (f1, gst_caps_get_features (c1, 0)));
  fail_if (gst_caps_can_intersect (c1, c2));

  f2 = gst_caps_get_features (c2, 0);
  fail_unless (gst_caps_features_is_equal
      (GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY, f2));

  gst_caps_unref (c2);
  gst_caps_unref (c1);

  c1 = gst_caps_from_string ("video/x-raw");
  f1 = gst_caps_get_features (c1, 0);
  f2 = gst_caps_features_new ("memory:dmabuf", NULL);
  gst_caps_set_features (c1, 0, f2);

  gst_caps_unref (c1);
}

GST_END_TEST;

GST_START_TEST (test_special_caps)
{
  GstCaps *caps;

  caps = gst_caps_new_any ();
  fail_unless (gst_caps_is_any (caps));
  fail_unless (gst_caps_is_any (caps) == TRUE);
  fail_if (gst_caps_is_empty (caps));
  fail_unless (gst_caps_is_empty (caps) == FALSE);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty ();
  fail_if (gst_caps_is_any (caps));
  fail_unless (gst_caps_is_any (caps) == FALSE);
  fail_unless (gst_caps_is_empty (caps));
  fail_unless (gst_caps_is_empty (caps) == TRUE);
  gst_caps_unref (caps);
}

GST_END_TEST;

static gboolean
foreach_append_function (GstCapsFeatures * features, GstStructure * structure,
    gpointer user_data)
{
  GstCaps *caps = user_data;

  gst_caps_append_structure_full (caps, gst_structure_copy (structure),
      features ? gst_caps_features_copy (features) : NULL);

  return TRUE;
}

GST_START_TEST (test_foreach)
{
  GstCaps *caps, *caps2;

  caps =
      gst_caps_from_string
      ("video/x-raw, format=I420; video/x-raw(foo:bar); video/x-h264");
  caps2 = gst_caps_new_empty ();
  fail_unless (gst_caps_foreach (caps, foreach_append_function, caps2));
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);

  caps = gst_caps_new_empty ();
  caps2 = gst_caps_new_empty ();
  fail_unless (gst_caps_foreach (caps, foreach_append_function, caps2));
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);
}

GST_END_TEST;

static gboolean
map_function (GstCapsFeatures * features, GstStructure * structure,
    gpointer user_data)
{
  /* Remove caps features if there are any, otherwise add some dummy */
  if (gst_caps_features_contains (features, "foo:bar")) {
    gst_caps_features_remove (features, "foo:bar");
  } else {
    gst_caps_features_add (features, "foo:bar");
    gst_caps_features_remove (features, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
  }

  /* Set some dummy integer in the structure */
  gst_structure_set (structure, "foo", G_TYPE_INT, 123, NULL);

  return TRUE;
}

GST_START_TEST (test_map_in_place)
{
  GstCaps *caps, *caps2;

  caps =
      gst_caps_from_string
      ("video/x-raw, format=I420; video/x-raw(foo:bar); video/x-h264");
  caps2 =
      gst_caps_from_string
      ("video/x-raw(foo:bar), foo=(int)123, format=I420; video/x-raw, foo=(int)123; video/x-h264(foo:bar), foo=(int)123");
  fail_unless (gst_caps_map_in_place (caps, map_function, NULL));
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);

  caps = gst_caps_new_empty ();
  caps2 = gst_caps_new_empty ();
  fail_unless (gst_caps_map_in_place (caps, map_function, NULL));
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);
}

GST_END_TEST;

static gboolean
filter_map_function (GstCapsFeatures * features, GstStructure * structure,
    gpointer user_data)
{
  if (!gst_structure_has_name (structure, "video/x-raw"))
    return FALSE;

  if (!gst_caps_features_contains (features, "foo:bar"))
    return FALSE;

  /* Set some dummy integer in the structure */
  gst_structure_set (structure, "foo", G_TYPE_INT, 123, NULL);

  return TRUE;
}

GST_START_TEST (test_filter_and_map_in_place)
{
  GstCaps *caps, *caps2;

  caps =
      gst_caps_from_string
      ("video/x-raw, format=I420; video/x-raw(foo:bar); video/x-h264");
  caps2 = gst_caps_from_string ("video/x-raw(foo:bar), foo=(int)123");
  gst_caps_filter_and_map_in_place (caps, filter_map_function, NULL);
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);

  caps = gst_caps_from_string ("video/x-raw, format=I420; video/x-h264");
  caps2 = gst_caps_new_empty ();
  gst_caps_filter_and_map_in_place (caps, filter_map_function, NULL);
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);

  caps = gst_caps_new_empty ();
  caps2 = gst_caps_new_empty ();
  gst_caps_filter_and_map_in_place (caps, filter_map_function, NULL);
  fail_unless (gst_caps_is_strictly_equal (caps, caps2));
  gst_caps_unref (caps);
  gst_caps_unref (caps2);
}

GST_END_TEST;

static Suite *
gst_caps_suite (void)
{
  Suite *s = suite_create ("GstCaps");
  TCase *tc_chain = tcase_create ("operations");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_from_string);
  tcase_add_test (tc_chain, test_double_append);
  tcase_add_test (tc_chain, test_mutability);
  tcase_add_test (tc_chain, test_static_caps);
  tcase_add_test (tc_chain, test_simplify);
  tcase_add_test (tc_chain, test_truncate);
  tcase_add_test (tc_chain, test_subset);
  tcase_add_test (tc_chain, test_subset_duplication);
  tcase_add_test (tc_chain, test_merge_fundamental);
  tcase_add_test (tc_chain, test_merge_same);
  tcase_add_test (tc_chain, test_merge_subset);
  tcase_add_test (tc_chain, test_intersect);
  tcase_add_test (tc_chain, test_intersect2);
  tcase_add_test (tc_chain, test_intersect_list_duplicate);
  tcase_add_test (tc_chain, test_intersect_zigzag);
  tcase_add_test (tc_chain, test_intersect_first);
  tcase_add_test (tc_chain, test_intersect_first2);
  tcase_add_test (tc_chain, test_intersect_duplication);
  tcase_add_test (tc_chain, test_intersect_flagset);
  tcase_add_test (tc_chain, test_union);
  tcase_add_test (tc_chain, test_normalize);
  tcase_add_test (tc_chain, test_broken);
  tcase_add_test (tc_chain, test_features);
  tcase_add_test (tc_chain, test_special_caps);
  tcase_add_test (tc_chain, test_foreach);
  tcase_add_test (tc_chain, test_map_in_place);
  tcase_add_test (tc_chain, test_filter_and_map_in_place);

  return s;
}

GST_CHECK_MAIN (gst_caps);
