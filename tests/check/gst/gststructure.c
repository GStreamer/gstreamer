/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gststructure.c: Unit tests for GstStructure
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


#include <gst/gststructure.h>
#include <gst/check/gstcheck.h>


GST_START_TEST (test_from_string_int)
{
  const char *strings[] = {
    "video/x-raw-rgb, width = (int) 123456",
    "video/x-raw-rgb, stride = (int) -123456",
    "video/x-raw-rgb, red_mask = (int) 0xFFFF",
    "video/x-raw-rgb, red_mask = (int) 0x0000FFFF",
    "video/x-raw-rgb, red_mask = (int) 0x7FFFFFFF",
    "video/x-raw-rgb, red_mask = (int) 0x80000000",
    "video/x-raw-rgb, red_mask = (int) 0xFF000000",
    /* result from
     * gst-launch ... ! "video/x-raw-rgb, red_mask=(int)0xFF000000" ! ... */
    "video/x-raw-rgb,\\ red_mask=(int)0xFF000000",
  };
  gint results[] = {
    123456,
    -123456,
    0xFFFF,
    0xFFFF,
    0x7FFFFFFF,
    0x80000000,
    0xFF000000,
    0xFF000000,
  };
  GstStructure *structure;
  int i;

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    const char *s;
    const gchar *name;
    gint value;

    s = strings[i];

    structure = gst_structure_from_string (s, NULL);
    fail_if (structure == NULL, "Could not get structure from string %s", s);
    name = gst_structure_nth_field_name (structure, 0);
    fail_unless (gst_structure_get_int (structure, name, &value));
    fail_unless (value == results[i],
        "Value %d is not the expected result %d for string %s",
        value, results[i], s);

    /* cleanup */
    gst_structure_free (structure);
  }
}

GST_END_TEST;

/* Test type conversions from string */
GST_START_TEST (test_from_string)
{
  GstStructure *structure;
  const gchar *s;
  const GValue *val;

  s = "test-string,value=1";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  fail_unless ((val = gst_structure_get_value (structure, "value")) != NULL);
  fail_unless (G_VALUE_HOLDS_INT (val));
  gst_structure_free (structure);

  s = "test-string,value=1.0";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  fail_unless ((val = gst_structure_get_value (structure, "value")) != NULL);
  fail_unless (G_VALUE_HOLDS_DOUBLE (val));
  gst_structure_free (structure);

  s = "test-string,value=1/1";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  fail_unless ((val = gst_structure_get_value (structure, "value")) != NULL);
  fail_unless (GST_VALUE_HOLDS_FRACTION (val));
  gst_structure_free (structure);

  s = "test-string,value=bar";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  fail_unless ((val = gst_structure_get_value (structure, "value")) != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (val));
  gst_structure_free (structure);

  s = "test-string,value=true";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  fail_unless ((val = gst_structure_get_value (structure, "value")) != NULL);
  fail_unless (G_VALUE_HOLDS_BOOLEAN (val));
  fail_unless_equals_int (g_value_get_boolean (val), TRUE);
  gst_structure_free (structure);
}

GST_END_TEST;

GST_START_TEST (test_complete_structure)
{
  GstStructure *structure;
  const gchar *s;

  s = "GstEventSeek, rate=(double)1, format=(GstFormat)GST_FORMAT_TIME, flags=(GstSeekFlags)GST_SEEK_FLAGS_NONE, cur_type=(GstSeekType)GST_SEEK_TYPE_SET, cur=(gint64)1000000000, stop_type=(GstSeekType)GST_SEEK_TYPE_NONE, stop=(gint64)0";
  structure = gst_structure_from_string (s, NULL);
  fail_if (structure == NULL, "Could not get structure from string %s", s);
  /* FIXME: TODO: add checks for correct serialization of members ? */
  gst_structure_free (structure);
}

GST_END_TEST;

GST_START_TEST (test_structure_new)
{
  GstStructure *s;
  GError *e;
  GQuark domain;
  gboolean bool;
  gint num, den;
  GstClockTime clocktime;
  guint32 fourcc;

  s = gst_structure_new ("name",
      "key", G_TYPE_STRING, "value",
      "bool", G_TYPE_BOOLEAN, TRUE,
      "fraction", GST_TYPE_FRACTION, 1, 5,
      "clocktime", GST_TYPE_CLOCK_TIME, GST_CLOCK_TIME_NONE,
      "fourcc", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('f', 'o', 'u', 'r'), NULL);

  fail_unless (gst_structure_get_field_type (s, "unknown") == G_TYPE_INVALID);
  /* test setting a different name */
  gst_structure_set_name (s, "newname");
  fail_unless (strcmp (gst_structure_get_string (s, "key"), "value") == 0);
  fail_unless (gst_structure_has_field (s, "key"));
  fail_unless_equals_int (gst_structure_n_fields (s), 5);
  /* test removing a field */
  gst_structure_remove_field (s, "key");
  fail_if (gst_structure_get_string (s, "key"));
  fail_if (gst_structure_has_field (s, "key"));
  fail_unless_equals_int (gst_structure_n_fields (s), 4);

  fail_unless (gst_structure_get_boolean (s, "bool", &bool));
  fail_unless (bool);

  fail_unless (gst_structure_get_fraction (s, "fraction", &num, &den));
  fail_unless_equals_int (num, 1);
  fail_unless_equals_int (den, 5);

  fail_unless (gst_structure_get_clock_time (s, "clocktime", &clocktime));
  fail_unless_equals_uint64 (clocktime, GST_CLOCK_TIME_NONE);

  fail_unless (gst_structure_get_fourcc (s, "fourcc", &fourcc));

  gst_structure_free (s);

  domain = g_quark_from_string ("test");
  e = g_error_new (domain, 0, "a test error");
  s = gst_structure_new ("name", "key", GST_TYPE_G_ERROR, e, NULL);
  g_error_free (e);
  gst_structure_free (s);
}

GST_END_TEST;

GST_START_TEST (test_fixate)
{
  GstStructure *s;

  s = gst_structure_new ("name",
      "int", G_TYPE_INT, 5,
      "intrange", GST_TYPE_INT_RANGE, 5, 10,
      "intrange2", GST_TYPE_INT_RANGE, 5, 10, NULL);

  fail_if (gst_structure_fixate_field_nearest_int (s, "int", 5));
  fail_unless (gst_structure_fixate_field_nearest_int (s, "intrange", 5));
  fail_if (gst_structure_fixate_field_nearest_int (s, "intrange", 5));
  fail_unless (gst_structure_fixate_field_nearest_int (s, "intrange2", 15));
  fail_if (gst_structure_fixate_field_nearest_int (s, "intrange2", 15));
  gst_structure_free (s);
}

GST_END_TEST;


Suite *
gst_structure_suite (void)
{
  Suite *s = suite_create ("GstStructure");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_from_string_int);
  tcase_add_test (tc_chain, test_from_string);
  tcase_add_test (tc_chain, test_complete_structure);
  tcase_add_test (tc_chain, test_structure_new);
  tcase_add_test (tc_chain, test_fixate);
  return s;
}

GST_CHECK_MAIN (gst_structure);
