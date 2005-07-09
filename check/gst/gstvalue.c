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


#include "../gstcheck.h"


GST_START_TEST (test_deserialize_buffer)
{
  GValue value = { 0 };

  g_value_init (&value, GST_TYPE_BUFFER);
  fail_unless (gst_value_deserialize (&value, "1234567890abcdef"));
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
    "", ""}, {
    "\"\"", ""},
        /* FAILURES */
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
