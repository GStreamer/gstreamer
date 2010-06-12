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


GST_START_TEST (test_serialize_fourcc)
{
  int i;

  guint32 fourccs[] = {
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('Y', '8', '0', '0'),
    GST_MAKE_FOURCC ('Y', '8', ' ', ' '),
    GST_MAKE_FOURCC ('Y', '1', '6', ' '),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '_'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '#'),
  };
  gint fourccs_size = sizeof (fourccs) / sizeof (fourccs[0]);
  const gchar *fourcc_strings[] = {
    "YUY2",
    "Y800",
    "Y8  ",
    "Y16 ",
    "0x5f595559",               /* Ascii values of YUY_ */
    "0x23595559",               /* Ascii values of YUY# */
  };
  gint fourcc_strings_size =
      sizeof (fourcc_strings) / sizeof (fourcc_strings[0]);

  fail_unless (fourccs_size == fourcc_strings_size);

  for (i = 0; i < fourccs_size; ++i) {
    gchar *str;
    GValue value = { 0 };
    g_value_init (&value, GST_TYPE_FOURCC);

    gst_value_set_fourcc (&value, fourccs[i]);
    str = gst_value_serialize (&value);

    fail_unless (strcmp (str, fourcc_strings[i]) == 0);

    g_free (str);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_fourcc)
{
  int i;

  guint32 fourccs[] = {
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('Y', '8', '0', '0'),
    GST_MAKE_FOURCC ('Y', '8', ' ', ' '),
    GST_MAKE_FOURCC ('Y', '8', ' ', ' '),
    GST_MAKE_FOURCC ('Y', '8', ' ', ' '),
    GST_MAKE_FOURCC ('Y', '1', '6', ' '),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '_'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '#'),
  };
  gint fourccs_size = sizeof (fourccs) / sizeof (fourccs[0]);
  const gchar *fourcc_strings[] = {
    "YUY2",
    "Y800",
    "Y8  ",
    "Y8 ",
    "Y8",
    "Y16 ",
    "0x5f595559",               /* Ascii values of YUY_ */
    "0x23595559",               /* Ascii values of YUY# */
  };
  gint fourcc_strings_size =
      sizeof (fourcc_strings) / sizeof (fourcc_strings[0]);

  fail_unless (fourccs_size == fourcc_strings_size);

  for (i = 0; i < fourccs_size; ++i) {
    GValue value = { 0 };

    g_value_init (&value, GST_TYPE_FOURCC);

    fail_unless (gst_value_deserialize (&value, fourcc_strings[i]));
    fail_unless_equals_int (gst_value_get_fourcc (&value), fourccs[i]);

    g_value_unset (&value);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_buffer)
{
  GValue value = { 0 };
  GstBuffer *buf;

  g_value_init (&value, GST_TYPE_BUFFER);
  fail_unless (gst_value_deserialize (&value, "1234567890abcdef"));
  /* does not increase the refcount */
  buf = GST_BUFFER (gst_value_get_mini_object (&value));
  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* does not increase the refcount */
  buf = gst_value_get_buffer (&value);
  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* cleanup */
  g_value_unset (&value);
}

GST_END_TEST;

/* create and serialize a buffer */
GST_START_TEST (test_serialize_buffer)
{
  GValue value = { 0 };
  GstBuffer *buf;
  gchar *serialized;
  static const char *buf_data = "1234567890abcdef";
  gint len;

  len = strlen (buf_data);
  buf = gst_buffer_new_and_alloc (len);
  memcpy (GST_BUFFER_DATA (buf), buf_data, len);
  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* and assign buffer to mini object */
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_take_buffer (&value, buf);
  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* now serialize it */
  serialized = gst_value_serialize (&value);
  GST_DEBUG ("serialized buffer to %s", serialized);
  fail_unless (serialized != NULL);

  /* refcount should not change */
  ASSERT_MINI_OBJECT_REFCOUNT (buf, "buffer", 1);

  /* cleanup */
  g_free (serialized);
  g_value_unset (&value);

  /* take NULL buffer */
  g_value_init (&value, GST_TYPE_BUFFER);
  GST_DEBUG ("setting NULL buffer");
  gst_value_take_buffer (&value, NULL);

  /* now serialize it */
  GST_DEBUG ("serializing NULL buffer");
  serialized = gst_value_serialize (&value);
  /* should return NULL */
  fail_unless (serialized == NULL);

  g_free (serialized);
  g_value_unset (&value);
}

GST_END_TEST;

GST_START_TEST (test_deserialize_gint64)
{
  GValue value = { 0 };
  const char *strings[] = {
    "12345678901",
    "-12345678901",
    "1152921504606846976",
    "-1152921504606846976",
  };
  gint64 results[] = {
    12345678901LL,
    -12345678901LL,
    1152921504606846976LL,
    -1152921504606846976LL,
  };
  int i;

  g_value_init (&value, G_TYPE_INT64);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_int64 (&value) == results[i],
        "resulting value is %" G_GINT64_FORMAT ", not %" G_GINT64_FORMAT
        ", for string %s (%d)", g_value_get_int64 (&value),
        results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_guint64)
{
  GValue value = { 0 };
  const char *strings[] = {
    "0xffffffffffffffff",
    "9223372036854775810",
    "-9223372036854775810",
    "-1",
    "1",
    "-0",
  };
  guint64 results[] = {
    0xffffffffffffffffULL,
    9223372036854775810ULL,
    9223372036854775806ULL,
    (guint64) - 1,
    1,
    0,
  };
  int i;

  g_value_init (&value, G_TYPE_UINT64);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_uint64 (&value) == results[i],
        "resulting value is %" G_GUINT64_FORMAT ", not %" G_GUINT64_FORMAT
        ", for string %s (%d)", g_value_get_uint64 (&value),
        results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_deserialize_gstfraction)
{
  GValue value = { 0 };
  const char *strings[] = {
    "4/5",
    "-8/9"
  };
  gint64 result_numers[] = {
    4,
    -8
  };
  gint64 result_denoms[] = {
    5,
    9
  };

  int i;

  g_value_init (&value, GST_TYPE_FRACTION);
  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (gst_value_get_fraction_numerator (&value) == result_numers[i],
        "resulting numerator value is %d, not %d"
        ", for string %s (%d)", gst_value_get_fraction_numerator (&value),
        result_numers[i], strings[i], i);
    fail_unless (gst_value_get_fraction_denominator (&value) ==
        result_denoms[i], "resulting denominator value is %d, not %d"
        ", for string %s (%d)", gst_value_get_fraction_denominator (&value),
        result_denoms[i], strings[i], i);
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
  /* some casts need to be explicit because of unsigned -> signed */
  gint results[] = {
    123456,
    -123456,
    0xFFFF,
    0xFFFF,
    0x7FFFFFFF,
    (gint) 0x80000000,
    (gint) 0x80000000,
    (gint) 0xFF000000,
    -1,
    (gint) 0xFFFFFFFF,
    -1,
    (gint) 0xFFFFFFFFFFFFFFFFLL,
    (gint) 0xEFFFFFFF,
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
    (guint) - 123456,
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

GST_START_TEST (test_serialize_flags)
{
  GValue value = { 0 };
  gchar *string;
  GstSeekFlags flags[] = {
    0,
    GST_SEEK_FLAG_NONE,
    GST_SEEK_FLAG_FLUSH,
    GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
  };
  const char *results[] = {
    "GST_SEEK_FLAG_NONE",
    "GST_SEEK_FLAG_NONE",
    "GST_SEEK_FLAG_FLUSH",
    "GST_SEEK_FLAG_FLUSH+GST_SEEK_FLAG_ACCURATE",
  };
  int i;

  g_value_init (&value, GST_TYPE_SEEK_FLAGS);

  for (i = 0; i < G_N_ELEMENTS (flags); ++i) {
    g_value_set_flags (&value, flags[i]);
    string = gst_value_serialize (&value);
    fail_if (string == NULL, "could not serialize flags %d", i);
    fail_unless (strcmp (string, results[i]) == 0,
        "resulting value is %s, not %s, for flags #%d", string, results[i], i);
    g_free (string);
  }
}

GST_END_TEST;


GST_START_TEST (test_deserialize_flags)
{
  GValue value = { 0 };
  const char *strings[] = {
    "",
    "0",
    "GST_SEEK_FLAG_NONE",
    "GST_SEEK_FLAG_FLUSH",
    "GST_SEEK_FLAG_FLUSH+GST_SEEK_FLAG_ACCURATE",
  };
  GstSeekFlags results[] = {
    GST_SEEK_FLAG_NONE,
    GST_SEEK_FLAG_NONE,
    GST_SEEK_FLAG_NONE,
    GST_SEEK_FLAG_FLUSH,
    GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
  };
  int i;

  g_value_init (&value, GST_TYPE_SEEK_FLAGS);

  for (i = 0; i < G_N_ELEMENTS (strings); ++i) {
    fail_unless (gst_value_deserialize (&value, strings[i]),
        "could not deserialize %s (%d)", strings[i], i);
    fail_unless (g_value_get_flags (&value) == results[i],
        "resulting value is %d, not %d, for string %s (%d)",
        g_value_get_flags (&value), results[i], strings[i], i);
  }
}

GST_END_TEST;

GST_START_TEST (test_string)
{
  const gchar *try[] = {
    "Dude",
    "Hi, I'm a string",
    "tüüüt!",
    "\"\""                      /* Empty string */
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
  /* NULL strings should not be serializable */
  g_value_set_string (&v, NULL);
  fail_unless (gst_value_serialize (&v) == NULL);
  g_value_unset (&v);
}

GST_END_TEST;

GST_START_TEST (test_deserialize_string)
{
  struct
  {
    const gchar *from;
    const gchar *to;
  } tests[] = {
    {
    "", ""},                    /* empty strings */
    {
    "\"\"", ""},                /* quoted empty string -> empty string */
        /* Expected FAILURES: */
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
  GValue tmp = { 0 };

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
  /* Test some NULL string comparisons */
  g_value_set_string (&value2, NULL);
  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_UNORDERED);
  fail_unless (gst_value_compare (&value2, &value1) == GST_VALUE_UNORDERED);
  fail_unless (gst_value_compare (&value2, &value2) == GST_VALUE_EQUAL);

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

  /* Check that lists are equal regardless of order */
  g_value_init (&value1, GST_TYPE_LIST);
  g_value_init (&tmp, G_TYPE_INT);
  g_value_set_int (&tmp, 1);
  gst_value_list_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 2);
  gst_value_list_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 3);
  gst_value_list_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 4);
  gst_value_list_append_value (&value1, &tmp);

  g_value_init (&value2, GST_TYPE_LIST);
  g_value_set_int (&tmp, 4);
  gst_value_list_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 3);
  gst_value_list_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 2);
  gst_value_list_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 1);
  gst_value_list_append_value (&value2, &tmp);

  fail_unless (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL,
      "value lists with different order were not equal when they should be");
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL,
      "value lists with same order were not equal when they should be");
  fail_unless (gst_value_compare (&value2, &value2) == GST_VALUE_EQUAL,
      "value lists with same order were not equal when they should be");

  /* Carry over the lists to this next check: */
  /* Lists with different sizes are unequal */
  g_value_set_int (&tmp, 1);
  gst_value_list_append_value (&value2, &tmp);

  fail_if (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL,
      "Value lists with different size were equal when they shouldn't be");

  /* Carry over the lists to this next check: */
  /* Lists with same size but list1 contains one more element not in list2 */
  g_value_set_int (&tmp, 5);
  gst_value_list_append_value (&value1, &tmp);

  fail_if (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL,
      "Value lists with different elements were equal when they shouldn't be");
  fail_if (gst_value_compare (&value2, &value1) == GST_VALUE_EQUAL,
      "Value lists with different elements were equal when they shouldn't be");

  g_value_unset (&value1);
  g_value_unset (&value2);
  g_value_unset (&tmp);

  /* Arrays are only equal when in the same order */
  g_value_init (&value1, GST_TYPE_ARRAY);
  g_value_init (&tmp, G_TYPE_INT);
  g_value_set_int (&tmp, 1);
  gst_value_array_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 2);
  gst_value_array_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 3);
  gst_value_array_append_value (&value1, &tmp);
  g_value_set_int (&tmp, 4);
  gst_value_array_append_value (&value1, &tmp);

  g_value_init (&value2, GST_TYPE_ARRAY);
  g_value_set_int (&tmp, 4);
  gst_value_array_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 3);
  gst_value_array_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 2);
  gst_value_array_append_value (&value2, &tmp);
  g_value_set_int (&tmp, 1);
  gst_value_array_append_value (&value2, &tmp);

  fail_if (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL,
      "Value arrays with different order were equal when they shouldn't be");
  fail_unless (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL,
      "Identical value arrays were not equal when they should be");
  fail_unless (gst_value_compare (&value2, &value2) == GST_VALUE_EQUAL,
      "Identical value arrays were not equal when they should be");

  /* Carry over the arrays to this next check: */
  /* Arrays with different sizes are unequal */
  g_value_unset (&value2);
  g_value_init (&value2, GST_TYPE_ARRAY);
  g_value_copy (&value1, &value2);

  g_value_set_int (&tmp, 1);
  gst_value_array_append_value (&value2, &tmp);

  fail_if (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL,
      "Value arrays with different size were equal when they shouldn't be");
  /* order should not matter */
  fail_if (gst_value_compare (&value2, &value1) == GST_VALUE_EQUAL,
      "Value arrays with different size were equal when they shouldn't be");

  g_value_unset (&value1);
  g_value_unset (&value2);
  g_value_unset (&tmp);
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

  g_value_unset (&src1);
  g_value_unset (&src2);
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

/* Test arithmetic subtraction of fractions */
GST_START_TEST (test_value_subtract_fraction)
{
  GValue result = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };

  /* Subtract 1/4 from 1/2 */
  g_value_init (&src1, GST_TYPE_FRACTION);
  g_value_init (&src2, GST_TYPE_FRACTION);
  g_value_init (&result, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 1, 2);
  gst_value_set_fraction (&src2, 1, 4);
  fail_unless (gst_value_fraction_subtract (&result, &src1, &src2) == TRUE);
  fail_unless (gst_value_get_fraction_numerator (&result) == 1);
  fail_unless (gst_value_get_fraction_denominator (&result) == 4);

  g_value_unset (&src1);
  g_value_unset (&src2);
  g_value_unset (&result);

  /* Subtract 1/12 from 7/8 */
  g_value_init (&src1, GST_TYPE_FRACTION);
  g_value_init (&src2, GST_TYPE_FRACTION);
  g_value_init (&result, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 7, 8);
  gst_value_set_fraction (&src2, 1, 12);
  fail_unless (gst_value_fraction_subtract (&result, &src1, &src2) == TRUE);
  fail_unless (gst_value_get_fraction_numerator (&result) == 19);
  fail_unless (gst_value_get_fraction_denominator (&result) == 24);

  g_value_unset (&src1);
  g_value_unset (&src2);
  g_value_unset (&result);

  /* Subtract 12/13 from 4/3 */
  g_value_init (&src1, GST_TYPE_FRACTION);
  g_value_init (&src2, GST_TYPE_FRACTION);
  g_value_init (&result, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 4, 3);
  gst_value_set_fraction (&src2, 12, 13);
  fail_unless (gst_value_fraction_subtract (&result, &src1, &src2) == TRUE);
  fail_unless (gst_value_get_fraction_numerator (&result) == 16);
  fail_unless (gst_value_get_fraction_denominator (&result) == 39);

  g_value_unset (&src1);
  g_value_unset (&src2);
  g_value_unset (&result);

  /* Subtract 1/12 from 7/8 */
}

GST_END_TEST;

/* Test set subtraction operations on fraction ranges */
GST_START_TEST (test_value_subtract_fraction_range)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  GValue cmp = { 0 };
  const GValue *tmp;
  gboolean ret;

  /* Value for tests */
  g_value_init (&cmp, GST_TYPE_FRACTION);

  /*  fraction <-> fraction
   */
  g_value_init (&src1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 10, 1);
  g_value_init (&src2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src2, 20, 1);
  gst_value_set_fraction (&src1, 10, 1);

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

  /*  fraction <-> fraction_range
   */

  /* would yield an empty set */
  g_value_init (&src1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 10, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 0, 1, 20, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, we cannot create open ranges
   * so the result is the range again */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 0, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* border case 1, empty set */
  g_value_init (&src1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 10, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 10, 1, 20, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);

  /* and the other way around, should keep same range as
   * we don't have open ranges. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 10, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* case 2, valid set */
  g_value_init (&src1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&src1, 0, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 10, 1, 20, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION (&dest) == TRUE);
  fail_unless (gst_value_compare (&dest, &src1) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* and the other way around, should keep the range. */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  fail_unless (gst_value_compare (&dest, &src2) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /*  fraction_range <-> fraction_range
   */

  /* same range, empty set */
  g_value_init (&src1, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src1, 10, 2, 20, 2);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 10, 2, 20, 2);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* non overlapping ranges */
  g_value_init (&src1, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src1, 10, 2, 10, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 30, 2, 40, 2);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 5, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 10, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);

  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 15, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* completely overlapping ranges */
  g_value_init (&src1, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src1, 10, 1, 20, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 10, 1, 30, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == FALSE);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 30, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* partially overlapping ranges */
  g_value_init (&src1, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src1, 10, 1, 20, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 15, 1, 30, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 10, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 15, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);

  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (&dest) == TRUE);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (&dest),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 30, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (&dest),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  g_value_unset (&src1);
  g_value_unset (&src2);

  /* create a hole { double_range, double_range } */
  g_value_init (&src1, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src1, 10, 1, 30, 1);
  g_value_init (&src2, GST_TYPE_FRACTION_RANGE);
  gst_value_set_fraction_range_full (&src2, 15, 1, 20, 1);
  ret = gst_value_subtract (&dest, &src1, &src2);
  fail_unless (ret == TRUE);
  fail_unless (GST_VALUE_HOLDS_LIST (&dest) == TRUE);
  /* 1st list entry */
  tmp = gst_value_list_get_value (&dest, 0);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (tmp) == TRUE);
  gst_value_set_fraction (&cmp, 10, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (tmp),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 15, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (tmp),
          &cmp) == GST_VALUE_EQUAL);
  /* 2nd list entry */
  tmp = gst_value_list_get_value (&dest, 1);
  fail_unless (GST_VALUE_HOLDS_FRACTION_RANGE (tmp) == TRUE);
  gst_value_set_fraction (&cmp, 20, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_min (tmp),
          &cmp) == GST_VALUE_EQUAL);
  gst_value_set_fraction (&cmp, 30, 1);
  fail_unless (gst_value_compare (gst_value_get_fraction_range_max (tmp),
          &cmp) == GST_VALUE_EQUAL);
  g_value_unset (&dest);
  /* the other way */
  ret = gst_value_subtract (&dest, &src2, &src1);
  fail_unless (ret == FALSE);
  g_value_unset (&src1);
  g_value_unset (&src2);

  g_value_unset (&cmp);
}

GST_END_TEST;

/* Test set subtraction operations on fraction lists */
GST_START_TEST (test_value_subtract_fraction_list)
{
  GValue list1 = { 0 };
  GValue list2 = { 0 };
  GValue val1 = { 0 };
  GValue val2 = { 0 };
  GValue tmp = { 0 };
  gboolean ret;

  g_value_init (&list1, GST_TYPE_LIST);
  g_value_init (&val1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&val1, 15, 2);
  gst_value_list_append_value (&list1, &val1);
  g_value_init (&tmp, GST_TYPE_FRACTION);
  gst_value_set_fraction (&tmp, 5, 1);
  gst_value_list_append_value (&list1, &tmp);
  g_value_unset (&tmp);

  g_value_init (&list2, GST_TYPE_LIST);
  g_value_init (&val2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&val2, 15, 1);
  gst_value_list_append_value (&list2, &val2);
  g_value_init (&tmp, GST_TYPE_FRACTION);
  gst_value_set_fraction (&tmp, 5, 1);
  gst_value_list_append_value (&list2, &tmp);
  g_value_unset (&tmp);

  /* should subtract all common elements */
  ret = gst_value_subtract (&tmp, &list1, &list2);
  fail_unless (ret == TRUE);
  fail_unless (gst_value_compare (&tmp, &val1) == GST_VALUE_EQUAL);
  g_value_unset (&val1);
  g_value_unset (&tmp);

  ret = gst_value_subtract (&tmp, &list2, &list1);
  fail_unless (ret == TRUE);
  fail_unless (gst_value_compare (&tmp, &val2) == GST_VALUE_EQUAL);
  g_value_unset (&val2);
  g_value_unset (&tmp);

  g_value_unset (&list1);
  g_value_unset (&list2);
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
  g_date_free (date2);
  date = NULL;
  date2 = NULL;

  str = gst_structure_to_string (s);
  gst_structure_free (s);
  s = NULL;

  fail_unless (g_str_equal (str,
          "media/x-type, SOME_DATE_TAG=(GstDate)2005-09-22;"));

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
  g_date_free (date);
  date = NULL;

  str = gst_structure_to_string (s);
  gst_structure_free (s);
  s = NULL;

  fail_unless (g_str_equal (str,
          "media/x-type, SOME_DATE_TAG=(GstDate)2005-09-22;"));
  g_free (str);
  str = NULL;
}

GST_END_TEST;

GST_START_TEST (test_fraction_range)
{
  GValue range = { 0, };
  GValue start = { 0, }, end = {
  0,};
  GValue src = { 0, }, dest = {
  0,};
  GValue range2 = { 0, };

  g_value_init (&range, GST_TYPE_FRACTION_RANGE);
  g_value_init (&range2, GST_TYPE_FRACTION_RANGE);
  g_value_init (&start, GST_TYPE_FRACTION);
  g_value_init (&end, GST_TYPE_FRACTION);
  g_value_init (&src, GST_TYPE_FRACTION);

  gst_value_set_fraction (&src, 1, 2);

  /* Check that a intersection of fraction & range = fraction */
  gst_value_set_fraction (&start, 1, 4);
  gst_value_set_fraction (&end, 2, 3);
  gst_value_set_fraction_range (&range, &start, &end);

  fail_unless (gst_value_intersect (&dest, &src, &range) == TRUE);
  fail_unless (G_VALUE_TYPE (&dest) == GST_TYPE_FRACTION);
  fail_unless (gst_value_compare (&dest, &src) == GST_VALUE_EQUAL);

  /* Check that a intersection selects the overlapping range */
  gst_value_set_fraction (&start, 1, 3);
  gst_value_set_fraction (&end, 2, 3);
  gst_value_set_fraction_range (&range2, &start, &end);
  g_value_unset (&dest);
  fail_unless (gst_value_intersect (&dest, &range, &range2) == TRUE);
  fail_unless (G_VALUE_TYPE (&dest) == GST_TYPE_FRACTION_RANGE);

  gst_value_set_fraction_range (&range2, &start, &end);
  fail_unless (gst_value_compare (&dest, &range2) == GST_VALUE_EQUAL);

  /* Check that non intersection ranges don't intersect */
  gst_value_set_fraction (&start, 4, 2);
  gst_value_set_fraction (&end, 5, 2);
  gst_value_set_fraction_range (&range2, &start, &end);
  g_value_unset (&dest);
  fail_unless (gst_value_intersect (&dest, &range, &range2) == FALSE);

  g_value_unset (&start);
  g_value_unset (&end);
  g_value_unset (&range);
  g_value_unset (&range2);
  g_value_unset (&src);
}

GST_END_TEST;

GST_START_TEST (test_serialize_deserialize_format_enum)
{
  GstStructure *s, *s2;
  GstFormat foobar_fmt;
  gchar *str, *str2, *end = NULL;

  /* make sure custom formats are serialised properly as well */
  foobar_fmt = gst_format_register ("foobar", "GST_FORMAT_FOOBAR");
  fail_unless (foobar_fmt != GST_FORMAT_UNDEFINED);

  s = gst_structure_new ("foo/bar", "format1", GST_TYPE_FORMAT,
      GST_FORMAT_BYTES, "format2", GST_TYPE_FORMAT, GST_FORMAT_TIME,
      "format3", GST_TYPE_FORMAT, GST_FORMAT_DEFAULT, "format4",
      GST_TYPE_FORMAT, foobar_fmt, NULL);

  str = gst_structure_to_string (s);
  GST_LOG ("Got structure string '%s'", GST_STR_NULL (str));
  fail_unless (str != NULL);
  fail_unless (strstr (str, "TIME") != NULL);
  fail_unless (strstr (str, "BYTE") != NULL);
  fail_unless (strstr (str, "DEFAULT") != NULL);
  fail_unless (strstr (str, "FOOBAR") != NULL);

  s2 = gst_structure_from_string (str, &end);
  fail_unless (s2 != NULL);

  str2 = gst_structure_to_string (s2);
  fail_unless (str2 != NULL);

  fail_unless (g_str_equal (str, str2));

  g_free (str);
  g_free (str2);
  gst_structure_free (s);
  gst_structure_free (s2);
}

GST_END_TEST;

GST_START_TEST (test_serialize_deserialize_caps)
{
  GValue value = { 0 }, value2 = {
  0};
  GstCaps *caps, *caps2;
  gchar *serialized;

  caps = gst_caps_new_simple ("test/caps",
      "foo", G_TYPE_INT, 10, "bar", G_TYPE_STRING, "test", NULL);
  fail_if (GST_CAPS_REFCOUNT_VALUE (caps) != 1);

  /* and assign caps to gvalue */
  g_value_init (&value, GST_TYPE_CAPS);
  g_value_take_boxed (&value, caps);
  fail_if (GST_CAPS_REFCOUNT_VALUE (caps) != 1);

  /* now serialize it */
  serialized = gst_value_serialize (&value);
  GST_DEBUG ("serialized caps to %s", serialized);
  fail_unless (serialized != NULL);

  /* refcount should not change */
  fail_if (GST_CAPS_REFCOUNT_VALUE (caps) != 1);

  /* now deserialize again */
  g_value_init (&value2, GST_TYPE_CAPS);
  gst_value_deserialize (&value2, serialized);

  caps2 = g_value_get_boxed (&value2);
  fail_if (GST_CAPS_REFCOUNT_VALUE (caps2) != 1);

  /* they should be equal */
  fail_unless (gst_caps_is_equal (caps, caps2));

  /* cleanup */
  g_value_unset (&value);
  g_value_unset (&value2);
  g_free (serialized);
}

GST_END_TEST;

static Suite *
gst_value_suite (void)
{
  Suite *s = suite_create ("GstValue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_serialize_fourcc);
  tcase_add_test (tc_chain, test_deserialize_fourcc);
  tcase_add_test (tc_chain, test_deserialize_buffer);
  tcase_add_test (tc_chain, test_serialize_buffer);
  tcase_add_test (tc_chain, test_deserialize_gint);
  tcase_add_test (tc_chain, test_deserialize_gint_failures);
  tcase_add_test (tc_chain, test_deserialize_guint);
  tcase_add_test (tc_chain, test_deserialize_guint_failures);
  tcase_add_test (tc_chain, test_deserialize_gint64);
  tcase_add_test (tc_chain, test_deserialize_guint64);
  tcase_add_test (tc_chain, test_deserialize_gstfraction);
  tcase_add_test (tc_chain, test_serialize_flags);
  tcase_add_test (tc_chain, test_deserialize_flags);
  tcase_add_test (tc_chain, test_serialize_deserialize_format_enum);
  tcase_add_test (tc_chain, test_string);
  tcase_add_test (tc_chain, test_deserialize_string);
  tcase_add_test (tc_chain, test_value_compare);
  tcase_add_test (tc_chain, test_value_intersect);
  tcase_add_test (tc_chain, test_value_subtract_int);
  tcase_add_test (tc_chain, test_value_subtract_double);
  tcase_add_test (tc_chain, test_value_subtract_fraction);
  tcase_add_test (tc_chain, test_value_subtract_fraction_range);
  tcase_add_test (tc_chain, test_value_subtract_fraction_list);
  tcase_add_test (tc_chain, test_date);
  tcase_add_test (tc_chain, test_fraction_range);
  tcase_add_test (tc_chain, test_serialize_deserialize_caps);

  return s;
}

GST_CHECK_MAIN (gst_value);
