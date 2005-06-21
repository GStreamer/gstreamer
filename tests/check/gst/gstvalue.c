/* GStreamer
 * Copyright (C) <2004> David Schleef <david at schleef dot org>
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


START_TEST (test_deserialize_buffer)
{
  GValue value = { 0 };

  g_value_init (&value, GST_TYPE_BUFFER);
  fail_unless (gst_value_deserialize (&value, "1234567890abcdef"));
}

END_TEST;

START_TEST (test_string)
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

END_TEST;

START_TEST (test_deserialize_string)
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

END_TEST;

Suite *
gst_value_suite (void)
{
  Suite *s = suite_create ("GstValue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_deserialize_buffer);
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
