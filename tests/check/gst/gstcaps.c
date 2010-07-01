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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
    caps2 = gst_caps_from_string (caps_list[i]);
    fail_if (caps2 == NULL, "Could not create caps from string %s\n", to_str);

    fail_unless (gst_caps_is_equal (caps, caps));
    fail_unless (gst_caps_is_equal (caps, caps2));

    gst_caps_unref (caps);
    gst_caps_unref (caps2);
    g_free (to_str);
  }
}

GST_END_TEST;

GST_START_TEST (test_buffer)
{
  GstCaps *c1;
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (1000);
  c1 = gst_caps_new_simple ("audio/x-raw-int",
      "buffer", GST_TYPE_BUFFER, buffer, NULL);

  GST_DEBUG ("caps: %" GST_PTR_FORMAT, c1);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (1000);
  gst_buffer_set_caps (buffer, c1);     /* doesn't give away our c1 ref */

  gst_caps_unref (c1);
  gst_buffer_unref (buffer);    /* Should now drop both references */
}

GST_END_TEST;

GST_START_TEST (test_double_append)
{
  GstStructure *s1;
  GstCaps *c1;

  c1 = gst_caps_new_any ();
  s1 = gst_structure_from_string ("audio/x-raw-int,rate=44100", NULL);
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
  s1 = gst_structure_from_string ("audio/x-raw-int,rate=44100", NULL);
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
  static GstStaticCaps scaps = GST_STATIC_CAPS ("audio/x-raw-int,rate=44100");
  GstCaps *caps1;
  GstCaps *caps2;

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
}

GST_END_TEST;

static const gchar non_simple_caps_string[] =
    "video/x-raw-yuv, format=(fourcc)I420, framerate=(fraction)[ 1/100, 100 ], "
    "width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-raw-yuv, "
    "format=(fourcc)YUY2, framerate=(fraction)[ 1/100, 100 ], width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ]; video/x-raw-rgb, bpp=(int)8, depth=(int)8, "
    "endianness=(int)1234, framerate=(fraction)[ 1/100, 100 ], width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ]; video/x-raw-yuv, "
    "format=(fourcc){ I420, YUY2, YV12 }, width=(int)[ 16, 4096 ], "
    "height=(int)[ 16, 4096 ], framerate=(fraction)[ 1/100, 100 ]";

static gboolean
check_fourcc_list (const GValue * format_value)
{
  const GValue *fourcc_value;
  gboolean got_yv12 = FALSE;
  gboolean got_i420 = FALSE;
  gboolean got_yuy2 = FALSE;
  guint32 fourcc;

  fourcc_value = gst_value_list_get_value (format_value, 0);
  fail_unless (fourcc_value != NULL);
  fail_unless (GST_VALUE_HOLDS_FOURCC (fourcc_value));
  fourcc = gst_value_get_fourcc (fourcc_value);
  fail_unless (fourcc != 0);
  got_i420 = got_i420 || (fourcc == GST_STR_FOURCC ("I420"));
  got_yuy2 = got_yuy2 || (fourcc == GST_STR_FOURCC ("YUY2"));
  got_yv12 = got_yv12 || (fourcc == GST_STR_FOURCC ("YV12"));

  fourcc_value = gst_value_list_get_value (format_value, 1);
  fail_unless (fourcc_value != NULL);
  fail_unless (GST_VALUE_HOLDS_FOURCC (fourcc_value));
  fourcc = gst_value_get_fourcc (fourcc_value);
  fail_unless (fourcc != 0);
  got_i420 = got_i420 || (fourcc == GST_STR_FOURCC ("I420"));
  got_yuy2 = got_yuy2 || (fourcc == GST_STR_FOURCC ("YUY2"));
  got_yv12 = got_yv12 || (fourcc == GST_STR_FOURCC ("YV12"));

  fourcc_value = gst_value_list_get_value (format_value, 2);
  fail_unless (fourcc_value != NULL);
  fail_unless (GST_VALUE_HOLDS_FOURCC (fourcc_value));
  fourcc = gst_value_get_fourcc (fourcc_value);
  fail_unless (fourcc != 0);
  got_i420 = got_i420 || (fourcc == GST_STR_FOURCC ("I420"));
  got_yuy2 = got_yuy2 || (fourcc == GST_STR_FOURCC ("YUY2"));
  got_yv12 = got_yv12 || (fourcc == GST_STR_FOURCC ("YV12"));

  return (got_i420 && got_yuy2 && got_yv12);
}

GST_START_TEST (test_simplify)
{
  GstStructure *s1, *s2;
  gboolean did_simplify;
  GstCaps *caps;

  caps = gst_caps_from_string (non_simple_caps_string);
  fail_unless (caps != NULL,
      "gst_caps_from_string (non_simple_caps_string) failed");

  did_simplify = gst_caps_do_simplify (caps);
  fail_unless (did_simplify == TRUE,
      "gst_caps_do_simplify() should have worked");

  /* check simplified caps, should be:
   *
   * video/x-raw-rgb, bpp=(int)8, depth=(int)8, endianness=(int)1234,
   *     framerate=(fraction)[ 1/100, 100 ], width=(int)[ 16, 4096 ],
   *     height=(int)[ 16, 4096 ];
   * video/x-raw-yuv, format=(fourcc){ YV12, YUY2, I420 },
   *     width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ],
   *     framerate=(fraction)[ 1/100, 100 ]
   */
  fail_unless (gst_caps_get_size (caps) == 2);
  s1 = gst_caps_get_structure (caps, 0);
  s2 = gst_caps_get_structure (caps, 1);
  fail_unless (s1 != NULL);
  fail_unless (s2 != NULL);

  if (!gst_structure_has_name (s1, "video/x-raw-rgb")) {
    GstStructure *tmp;

    tmp = s1;
    s1 = s2;
    s2 = tmp;
  }

  fail_unless (gst_structure_has_name (s1, "video/x-raw-rgb"));
  {
    const GValue *framerate_value;
    const GValue *width_value;
    const GValue *height_value;
    const GValue *val_fps;
    GValue test_fps = { 0, };
    gint bpp, depth, endianness;
    gint min_width, max_width;
    gint min_height, max_height;

    fail_unless (gst_structure_get_int (s1, "bpp", &bpp));
    fail_unless (bpp == 8);

    fail_unless (gst_structure_get_int (s1, "depth", &depth));
    fail_unless (depth == 8);

    fail_unless (gst_structure_get_int (s1, "endianness", &endianness));
    fail_unless (endianness == G_LITTLE_ENDIAN);

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

  fail_unless (gst_structure_has_name (s2, "video/x-raw-yuv"));
  {
    const GValue *framerate_value;
    const GValue *format_value;
    const GValue *width_value;
    const GValue *height_value;
    const GValue *val_fps;
    GValue test_fps = { 0, };
    gint min_width, max_width;
    gint min_height, max_height;

    format_value = gst_structure_get_value (s2, "format");
    fail_unless (format_value != NULL);
    fail_unless (GST_VALUE_HOLDS_LIST (format_value));
    fail_unless (gst_value_list_get_size (format_value) == 3);
    fail_unless (check_fourcc_list (format_value) == TRUE);

    g_value_init (&test_fps, GST_TYPE_FRACTION);
    framerate_value = gst_structure_get_value (s2, "framerate");
    fail_unless (framerate_value != NULL);
    fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (framerate_value));

    val_fps = gst_value_get_fraction_range_min (framerate_value);
    gst_value_set_fraction (&test_fps, 1, 100);
    fail_unless (gst_value_compare (&test_fps, val_fps) == GST_VALUE_EQUAL);

    val_fps = gst_value_get_fraction_range_max (framerate_value);
    gst_value_set_fraction (&test_fps, 100, 1);
    fail_unless (gst_value_compare (&test_fps, val_fps) == GST_VALUE_EQUAL);

    g_value_unset (&test_fps);

    width_value = gst_structure_get_value (s2, "width");
    fail_unless (width_value != NULL);
    fail_unless (GST_VALUE_HOLDS_INT_RANGE (width_value));
    min_width = gst_value_get_int_range_min (width_value);
    max_width = gst_value_get_int_range_max (width_value);
    fail_unless (min_width == 16 && max_width == 4096);

    height_value = gst_structure_get_value (s2, "height");
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
  gst_caps_truncate (caps);
  fail_unless_equals_int (gst_caps_get_size (caps), 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_merge_fundamental)
{
  GstCaps *c1, *c2;

  /* ANY + specific = ANY */
  c1 = gst_caps_from_string ("audio/x-raw-int,rate=44100");
  c2 = gst_caps_new_any ();
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 0, NULL);
  fail_unless (gst_caps_is_any (c2), NULL);
  gst_caps_unref (c2);

  /* specific + ANY = ANY */
  c2 = gst_caps_from_string ("audio/x-raw-int,rate=44100");
  c1 = gst_caps_new_any ();
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 0, NULL);
  fail_unless (gst_caps_is_any (c2), NULL);
  gst_caps_unref (c2);

  /* EMPTY + specific = specific */
  c1 = gst_caps_from_string ("audio/x-raw-int,rate=44100");
  c2 = gst_caps_new_empty ();
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  fail_if (gst_caps_is_empty (c2), NULL);
  gst_caps_unref (c2);

  /* specific + EMPTY = specific */
  c2 = gst_caps_from_string ("audio/x-raw-int,rate=44100");
  c1 = gst_caps_new_empty ();
  gst_caps_merge (c2, c1);
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
  c1 = gst_caps_from_string ("audio/x-raw-int,rate=44100,channels=1");
  c2 = gst_caps_from_string ("audio/x-raw-int,rate=44100,channels=1");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,rate=44100,channels=1");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (test);
  gst_caps_unref (c2);

  /* and so is this */
  c1 = gst_caps_from_string ("audio/x-raw-int,rate=44100,channels=1");
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=1,rate=44100");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AABB");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AABB");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  gst_caps_unref (c2);

  c1 = gst_caps_from_string ("video/x-foo, data=(buffer)AA");
  c2 = gst_caps_from_string ("video/x-bar, data=(buffer)AA");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_merge_subset)
{
  GstCaps *c1, *c2, *test;

  /* the 2nd is already covered */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,2]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=1");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,2]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* here it is not */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=1,rate=44100");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[1,2],rate=44100");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,2],rate=44100");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,3]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[1,2]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,3]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[1,2]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[2,4]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* second one was already contained in the first one */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[2,3]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 1, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* these caps cannot be merged */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[2,3]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[1,4]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test =
      gst_caps_from_string
      ("audio/x-raw-int,channels=[2,3];audio/x-raw-int,channels=[1,4]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  /* these caps cannot be merged */
  c2 = gst_caps_from_string ("audio/x-raw-int,channels=[1,2]");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels=[1,3]");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test =
      gst_caps_from_string
      ("audio/x-raw-int,channels=[1,2];audio/x-raw-int,channels=[1,3]");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("audio/x-raw-int,channels={1,2}");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels={1,2,3,4}");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels={1,2};"
      "audio/x-raw-int,channels={1,2,3,4}");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string ("audio/x-raw-int,channels={1,2}");
  c1 = gst_caps_from_string ("audio/x-raw-int,channels={1,3}");
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_get_size (c2) == 2, NULL);
  test = gst_caps_from_string ("audio/x-raw-int,channels={1,2};"
      "audio/x-raw-int,channels={1,3}");
  fail_unless (gst_caps_is_equal (c2, test));
  gst_caps_unref (c2);
  gst_caps_unref (test);

  c2 = gst_caps_from_string
      ("video/x-raw-yuv, framerate=(fraction){ 15/2, 5/1 }");
  c1 = gst_caps_from_string
      ("video/x-raw-yuv, framerate=(fraction){ 15/1, 5/1 }");
  test = gst_caps_copy (c1);
  gst_caps_merge (c2, c1);
  GST_DEBUG ("merged: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (c2), c2);
  fail_unless (gst_caps_is_subset (test, c2));
  gst_caps_unref (test);
  gst_caps_unref (c2);
}

GST_END_TEST;

GST_START_TEST (test_intersect)
{
  GstStructure *s;
  GstCaps *c1, *c2, *ci1, *ci2;

  /* field not specified = any value possible, so the intersection
   * should keep fields which are only part of one set of caps */
  c2 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_get_size (ci1) == 1, NULL);
  s = gst_caps_get_structure (ci1, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw-yuv"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_get_size (ci2) == 1, NULL);
  s = gst_caps_get_structure (ci2, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw-yuv"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);

  fail_unless (gst_caps_is_equal (ci1, ci2));

  gst_caps_unref (ci1);
  gst_caps_unref (ci2);

  gst_caps_unref (c1);
  gst_caps_unref (c2);

  /* ========== */

  c2 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,width=30");

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

  c2 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw-rgb,format=(fourcc)I420,width=20");

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

  c2 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,width=20");
  c1 = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420,height=30");

  ci1 = gst_caps_intersect (c2, c1);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci1);
  fail_unless (gst_caps_get_size (ci1) == 1, NULL);
  s = gst_caps_get_structure (ci1, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw-yuv"));
  fail_unless (gst_structure_get_value (s, "format") != NULL);
  fail_unless (gst_structure_get_value (s, "width") != NULL);
  fail_unless (gst_structure_get_value (s, "height") != NULL);

  /* with changed order */
  ci2 = gst_caps_intersect (c1, c2);
  GST_DEBUG ("intersected: %" GST_PTR_FORMAT, ci2);
  fail_unless (gst_caps_get_size (ci2) == 1, NULL);
  s = gst_caps_get_structure (ci2, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw-yuv"));
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
  caps1 = gst_caps_from_string ("audio/x-raw-float, "
      "channel-positions=(int)<                      "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         "
      "{ 1, 2, 3, 4, 5, 6 },                         " "{ 1, 2, 3, 4, 5, 6 }>");
  caps2 = gst_caps_from_string ("audio/x-raw-float, "
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

  gst_caps_unref (in);
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

  gst_caps_unref (in);
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

  gst_caps_unref (in);
  gst_caps_unref (out);
  gst_caps_unref (norm);
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
  tcase_add_test (tc_chain, test_buffer);
  tcase_add_test (tc_chain, test_static_caps);
  tcase_add_test (tc_chain, test_simplify);
  tcase_add_test (tc_chain, test_truncate);
  tcase_add_test (tc_chain, test_merge_fundamental);
  tcase_add_test (tc_chain, test_merge_same);
  tcase_add_test (tc_chain, test_merge_subset);
  tcase_add_test (tc_chain, test_intersect);
  tcase_add_test (tc_chain, test_intersect2);
  tcase_add_test (tc_chain, test_normalize);

  return s;
}

GST_CHECK_MAIN (gst_caps);
