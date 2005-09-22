/* GStreamer
 * Copyright (C) <2004> David Schleef <david at schleef dot org>
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstvalue.c: Unit tests for GstValue
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


GST_START_TEST (test_deserialize_buffer)
{
  GValue value = { 0 };
  GstBuffer *buf;

  g_value_init (&value, GST_TYPE_BUFFER);
  fail_unless (gst_value_deserialize (&value, "1234567890abcdef"));
  buf = GST_BUFFER (gst_value_get_mini_object (&value));

  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* cleanup */
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_deserialize_gint64)
{
  GValue value = { 0 };
  const char *strings[] = {
    "12345678901",
    "-12345678901",
  };
  gint64 results[] = {
    12345678901LL,
    -12345678901LL,
  };
  int i;

  g_value_init (&value, G_TYPE_INT64);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_int64 (&value) == results[i],
        "resulting value is %" G_GINT64_FORMAT ", not %" G_GINT64_FORMAT
        ", for string %s (%d)", value, results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_gint)
{
  GValue value = { 0 };
  const char *strings[] = {
    "123456",
    "-123456",
    "0xFFFF",
    "0x0000FFFF",
    /* a positive long long, serializing to highest possible positive sint */
    "0x7FFFFFFF",
    /* a positive long long, serializing to lowest possible negative sint */
    "0x80000000",
    /* a negative long long, serializing to lowest possible negative sint */
    "0xFFFFFFFF80000000",
    "0xFF000000",
    /* a positive long long serializing to -1 */
    "0xFFFFFFFF",
    "0xFFFFFFFF",
    /* a negative long long serializing to -1 */
    "0xFFFFFFFFFFFFFFFF",
    "0xFFFFFFFFFFFFFFFF",
    "0xEFFFFFFF",
  };
  gint results[] = {
    123456,
    -123456,
    0xFFFF,
    0xFFFF,
    0x7FFFFFFF,
    0x80000000,
    0x80000000,
    0xFF000000,
    -1,
    0xFFFFFFFF,
    -1,
    /* cast needs to be explicit because of unsigned -> signed */
    (gint) 0xFFFFFFFFFFFFFFFFLL,
    0xEFFFFFFF,
  };
  int i;

  g_value_init (&value, G_TYPE_INT);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_int (&value) == results[i],
        "resulting value is %d, not %d, for string %s (%d)",
        g_value_get_int (&value), results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_gint_failures)
{
  GValue value = { 0 };
  const char *strings[] = {
    "-",                        /* not a complete number */
    "- TEST",                   /* not a complete number */
    "0x0000000100000000",       /* lowest long long that cannot fit in 32 bits */
    "0xF000000000000000",
    "0xFFFFFFF000000000",
    "0xFFFFFFFF00000000",
    "0x10000000000000000",      /* first number too long to fit into a long long */
    /* invent a new processor first before trying to make this one pass */
    "0x10000000000000000000000000000000000000000000",
  };
  int i;

  g_value_init (&value, G_TYPE_INT);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_if (gst_value_deserialize (&value, strings[i]),
        "deserialized %s (%d), while it should have failed", strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_guint)
{
  GValue value = { 0 };
  const char *strings[] = {
    "123456",
    "-123456",
    "0xFFFF",
    "0x0000FFFF",
    /* a positive long long, serializing to highest possible positive sint */
    "0x7FFFFFFF",
    /* a positive long long, serializing to lowest possible negative sint */
    "0x80000000",
    "2147483648",
    /* a negative long long, serializing to lowest possible negative sint */
    "0xFFFFFFFF80000000",
    /* a value typically used for rgb masks */
    "0xFF000000",
    /* a positive long long serializing to highest possible positive uint */
    "0xFFFFFFFF",
    "0xFFFFFFFF",
    /* a negative long long serializing to highest possible positive uint */
    "0xFFFFFFFFFFFFFFFF",
    "0xEFFFFFFF",
  };
  guint results[] = {
    123456,
    -123456,
    0xFFFF,
    0xFFFF,
    0x7FFFFFFF,
    0x80000000,
    (guint) 2147483648LL,
    0x80000000,
    0xFF000000,
    0xFFFFFFFF,
    G_MAXUINT,
    (guint) 0xFFFFFFFFFFFFFFFFLL,
    0xEFFFFFFF,
  };
  int i;

  g_value_init (&value, G_TYPE_UINT);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_uint (&value) == results[i],
        "resulting value is %d, not %d, for string %s (%d)",
        g_value_get_uint (&value), results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_guint_failures)
{
  GValue value = { 0 };
  const char *strings[] = {
    "-",                        /* not a complete number */
    "- TEST",                   /* not a complete number */
#if 0
/* FIXME: these values should not be deserializable, since they overflow
 * the target format */
    "0x0000000100000000",       /* lowest long long that cannot fit in 32 bits */
    "0xF000000000000000",
    "0xFFFFFFF000000000",
    "0xFFFFFFFF00000000",
    "0x10000000000000000",      /* first number too long to fit into a long long */
    /* invent a new processor first before trying to make this one pass */
    "0x10000000000000000000000000000000000000000000",
#endif
  };
  int i;

  g_value_init (&value, G_TYPE_UINT);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_if (gst_value_deserialize (&value, strings[i]),
        "deserialized %s (%d), while it should have failed", strings[i], i);
  }
}

GST_END_TEST;


GST_START_TEST (test_string)
{
  gchar *try[] = {
    "Dude",
    "Hi, I'm a string",
    "tüüüt!"
  };
  gchar *tmp;
  GValue v = { 0, };
  guint i;

  g_value_init (&v, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (try); i++) {
    g_value_set_string (&v, try[i]);
    tmp = gst_value_serialize (&v);
    fail_if (tmp == NULL, "couldn't serialize: %s\n", try[i]);
    fail_unless (gst_value_deserialize (&v, tmp),
        "couldn't deserialize: %s\n", tmp);
    g_free (tmp);

    fail_unless (g_str_equal (g_value_get_string (&v), try[i]),
        "\nserialized  : %s\ndeserialized: %s", try[i],
        g_value_get_string (&v));
  }
  g_value_unset (&v);
}

GST_END_TEST;

GST_START_TEST (test_deserialize_string)
{
  struct
  {
    gchar *from;
    gchar *to;
  } tests[] = {
    {
    "", ""},                    /* empty strings */
    {
    "\"\"", ""},                /* FAILURES */
    {
    "\"", NULL},                /* missing second quote */
    {
    "\"Hello\\ World", NULL},   /* missing second quote */
    {
    "\"\\", NULL},              /* quote at end, missing second quote */
    {
    "\"\\0", NULL},             /* missing second quote */
    {
    "\"\\0\"", NULL},           /* unfinished escaped character */
    {
    "\" \"", NULL},             /* spaces must be escaped */
#if 0
        /* FIXME 0.9: this test should fail, but it doesn't */
    {
    "tüüt", NULL}             /* string with special chars must be escaped */
#endif
  };
  guint i;
  GValue v = { 0, };
  gboolean ret = TRUE;

  g_value_init (&v, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    if (gst_value_deserialize (&v, tests[i].from)) {
      fail_if (tests[i].to == NULL,
          "I got %s instead of a failure", g_value_get_string (&v));
      fail_unless (g_str_equal (g_value_get_string (&v), tests[i].to),
          "\nwanted: %s\ngot    : %s", tests[i].to, g_value_get_string (&v));
    } else {
      fail_if (tests[i].to != NULL, "failed, but wanted: %s", tests[i].to);
      ret = FALSE;
    }
  }
  g_value_unset (&v);
}

GST_END_TEST;

GST_START_TEST (test_value_compare)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };

  g_value_init (&value1, G_TYPE_INT);
  g_value_set_int (&value1, 10);
  g_value_init (&value2, G_TYPE_INT);
  g_value_set_int (&value2, 20);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, G_TYPE_DOUBLE);
  g_value_set_double (&value1, 10);
  g_value_init (&value2, G_TYPE_DOUBLE);
  g_value_set_double (&value2, 20);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, G_TYPE_STRING);
  g_value_set_string (&value1, "a");
  g_value_init (&value2, G_TYPE_STRING);
  g_value_set_string (&value2, "b");
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&value1, GST_MAKE_FOURCC ('a', 'b', 'c', 'd'));
  g_value_init (&value2, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&value2, GST_MAKE_FOURCC ('1', '2', '3', '4'));
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_UNORDERED);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing 2/3 with 3/4 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, 2, 3);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 3, 4);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing -4/5 with 2/-3 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, -4, 5);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 2, -3);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing 10/100 with 200/2000 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, 10, 100);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 200, 2000);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing -4/5 with 2/-3 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, -4, 5);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 2, -3);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);
}

GST_END_TEST;

GST_START_TEST (test_value_intersect)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  GValue item = { 0 };
  gboolean ret;

  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 10);
  g_value_init (&src2, G_TYPE_INT);
  g_value_set_int (&src2, 20);
  ret = gst_value_intersect (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  g_value_init (&src1, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&src1, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
  g_value_init (&src2, GST_TYPE_LIST);
  g_value_init (&item, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&item, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
  gst_value_list_append_value (&src2, &item);
  gst_value_set_fourcc (&item, GST_MAKE_FOURCC ('I', '4', '2', '0'));
  gst_value_list_append_value (&src2, &item);
  gst_value_set_fourcc (&item, GST_MAKE_FOURCC ('A', 'B', 'C', 'D'));
  gst_value_list_append_value (&src2, &item);

  fail_unless (gst_value_intersect (&dest, &src1, &src2));
  fail_unless (GST_VALUE_HOLDS_FOURCC (&dest));
  fail_unless (gst_value_get_fourcc (&dest) ==
      GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
}

GST_END_TEST;


GST_START_TEST (test_value_subtract_int)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  const GValue *tmp;
  gboolean ret;

  /*  int <-> int 
   */
  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 10);
  g_value_init (&src2, G_TYPE_INT);
  g_value_set_int (&src2, 20);
  /* subtract as in sets, result is 10 */
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (gst_value_compare (&dest, &src1) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* same values, yields empty set */
  ret = gst_value_subtract (&dest, &src1, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /*  int <-> int_range 
   */

  /* would yield an empty set */
  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 10);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 0, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should create a list of two ranges. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 0);
  fail_unless (gst_value_get_int_range_max (tmp) == 9);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 11);
  fail_unless (gst_value_get_int_range_max (tmp) == 20);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* border case 1, empty set */
  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 10);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 10, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should create a new range. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 11);
  fail_unless (gst_value_get_int_range_max (&dest) == 20);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* border case 2, empty set */
  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 20);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 10, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should create a new range. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 10);
  fail_unless (gst_value_get_int_range_max (&dest) == 19);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* case 3, valid set */
  g_value_init (&src1, G_TYPE_INT);
  g_value_set_int (&src1, 0);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 10, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (G_VALUE_HOLDS_INT (&dest) == TRUE);
  fail_unless (gst_value_compare (&dest, &src1) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* and the other way around, should keep the range. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 10);
  fail_unless (gst_value_get_int_range_max (&dest) == 20);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /*  int_range <-> int_range 
   */

  /* same range, empty set */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 20);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 10, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* non overlapping ranges */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 20);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 30, 40);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 10);
  fail_unless (gst_value_get_int_range_max (&dest) == 20);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 30);
  fail_unless (gst_value_get_int_range_max (&dest) == 40);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* completely overlapping ranges */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 20);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 10, 30);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 21);
  fail_unless (gst_value_get_int_range_max (&dest) == 30);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* partially overlapping ranges */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 20);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 15, 30);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 10);
  fail_unless (gst_value_get_int_range_max (&dest) == 14);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_int_range_min (&dest) == 21);
  fail_unless (gst_value_get_int_range_max (&dest) == 30);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole { int_range, int_range } */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 30);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 15, 20);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 10);
  fail_unless (gst_value_get_int_range_max (tmp) == 14);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 21);
  fail_unless (gst_value_get_int_range_max (tmp) == 30);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole, { int, int } */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 30);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 11, 29);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (G_VALUE_HOLDS_INT (tmp) == TRUE);
  fail_unless (g_value_get_int (tmp) == 10);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (G_VALUE_HOLDS_INT (tmp) == TRUE);
  fail_unless (g_value_get_int (tmp) == 30);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole, { int, int_range } */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 30);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 11, 28);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (G_VALUE_HOLDS_INT (tmp) == TRUE);
  fail_unless (g_value_get_int (tmp) == 10);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 29);
  fail_unless (gst_value_get_int_range_max (tmp) == 30);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole, { int_range, int } */
  g_value_init (&src1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src1, 10, 30);
  g_value_init (&src2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range (&src2, 12, 29);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (GST_VALUE_HOLDS_INT_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_int_range_min (tmp) == 10);
  fail_unless (gst_value_get_int_range_max (tmp) == 11);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (G_VALUE_HOLDS_INT (tmp) == TRUE);
  fail_unless (g_value_get_int (tmp) == 30);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);
}

GST_END_TEST;

GST_START_TEST (test_value_subtract_double)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  const GValue *tmp;
  gboolean ret;

  /*  double <-> double 
   */
  g_value_init (&src1, G_TYPE_DOUBLE);
  g_value_set_double (&src1, 10.0);
  g_value_init (&src2, G_TYPE_DOUBLE);
  g_value_set_double (&src2, 20.0);
  /* subtract as in sets, result is 10 */
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (gst_value_compare (&dest, &src1) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* same values, yields empty set */
  ret = gst_value_subtract (&dest, &src1, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /*  double <-> double_range 
   */

  /* would yield an empty set */
  g_value_init (&src1, G_TYPE_DOUBLE);
  g_value_set_double (&src1, 10.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 0.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, we cannot create open ranges
   * so the result is the range again */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 0.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 20.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* border case 1, empty set */
  g_value_init (&src1, G_TYPE_DOUBLE);
  g_value_set_double (&src1, 10.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 10.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should keep same range as
   * we don't have open ranges. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 10.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 20.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* border case 2, empty set */
  g_value_init (&src1, G_TYPE_DOUBLE);
  g_value_set_double (&src1, 20.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 10.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should keep same range as
   * we don't have open ranges. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 10.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 20.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* case 3, valid set */
  g_value_init (&src1, G_TYPE_DOUBLE);
  g_value_set_double (&src1, 0.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 10.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (G_VALUE_HOLDS_DOUBLE (&dest) == TRUE);
  fail_unless (gst_value_compare (&dest, &src1) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* and the other way around, should keep the range. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 10.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 20.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /*  double_range <-> double_range 
   */

  /* same range, empty set */
  g_value_init (&src1, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src1, 10.0, 20.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 10.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* non overlapping ranges */
  g_value_init (&src1, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src1, 10.0, 20.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 30.0, 40.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 10.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 20.0);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 30.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 40.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* completely overlapping ranges */
  g_value_init (&src1, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src1, 10.0, 20.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 10.0, 30.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 20.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 30.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* partially overlapping ranges */
  g_value_init (&src1, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src1, 10.0, 20.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 15.0, 30.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 10.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 15.0);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (&dest) == TRUE);
  fail_unless (gst_value_get_double_range_min (&dest) == 20.0);
  fail_unless (gst_value_get_double_range_max (&dest) == 30.0);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole { double_range, double_range } */
  g_value_init (&src1, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src1, 10.0, 30.0);
  g_value_init (&src2, GST_TYPE_DOUBLE_RANGE);
  gst_value_set_double_range (&src2, 15.0, 20.0);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_double_range_min (tmp) == 10.0);
  fail_unless (gst_value_get_double_range_max (tmp) == 15.0);
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (GST_VALUE_HOLDS_DOUBLE_RANGE (tmp) == TRUE);
  fail_unless (gst_value_get_double_range_min (tmp) == 20.0);
  fail_unless (gst_value_get_double_range_max (tmp) == 30.0);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);
}

GST_END_TEST;

GST_START_TEST (test_date)
{
  GstStructure *s;
  GDate *date, *date2;
  gchar *str;

  date = g_date_new_dmy (22, 9, 2005);

  s = gst_structure_new ("media/x-type", "SOME_DATE_TAG", GST_TYPE_DATE,
      date, NULL);

  fail_unless (gst_structure_has_field_typed (s, "SOME_DATE_TAG",
          GST_TYPE_DATE));
  fail_unless (gst_structure_get_date (s, "SOME_DATE_TAG", &date2));
  fail_unless (date2 != NULL);
  fail_unless (g_date_valid (date2));
  fail_unless (g_date_compare (date, date2) == 0);

  g_date_free (date);
  date = NULL;

  str = gst_structure_to_string (s);
  gst_structure_free (s);
  s = NULL;

  fail_unless (g_str_equal (str,
          "media/x-type, SOME_DATE_TAG=(GstDate)2005-09-22"));

  s = gst_structure_from_string (str, NULL);
  g_free (str);
  str = NULL;

  fail_unless (s != NULL);
  fail_unless (gst_structure_has_name (s, "media/x-type"));
  fail_unless (gst_structure_has_field_typed (s, "SOME_DATE_TAG",
          GST_TYPE_DATE));
  fail_unless (gst_structure_get_date (s, "SOME_DATE_TAG", &date));
  fail_unless (date != NULL);
  fail_unless (g_date_valid (date));
  fail_unless (g_date_get_day (date) == 22);
  fail_unless (g_date_get_month (date) == 9);
  fail_unless (g_date_get_year (date) == 2005);

  str = gst_structure_to_string (s);
  gst_structure_free (s);
  s = NULL;

  fail_unless (g_str_equal (str,
          "media/x-type, SOME_DATE_TAG=(GstDate)2005-09-22"));
  g_free (str);
  str = NULL;
}

GST_END_TEST;

Suite *
gst_value_suite (void)
{
  Suite *s = suite_create ("GstValue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_deserialize_buffer);
  tcase_add_test (tc_chain, test_deserialize_gint);
  tcase_add_test (tc_chain, test_deserialize_gint_failures);
  tcase_add_test (tc_chain, test_deserialize_guint);
  tcase_add_test (tc_chain, test_deserialize_guint_failures);
  tcase_add_test (tc_chain, test_deserialize_gint64);
  tcase_add_test (tc_chain, test_string);
  tcase_add_test (tc_chain, test_deserialize_string);
  tcase_add_test (tc_chain, test_value_compare);
  tcase_add_test (tc_chain, test_value_intersect);
  tcase_add_test (tc_chain, test_value_subtract_int);
  tcase_add_test (tc_chain, test_value_subtract_double);
  tcase_add_test (tc_chain, test_date);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_value_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
