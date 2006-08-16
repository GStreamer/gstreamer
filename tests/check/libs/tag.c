/* GStreamer
 *
 * unit tests for the tag support library
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/tag/tag.h>
#include <string.h>

GST_START_TEST (test_parse_extended_comment)
{
  gchar *key, *val, *lang;

  /* first check the g_return_val_if_fail conditions */
  ASSERT_CRITICAL (gst_tag_parse_extended_comment (NULL, NULL, NULL, NULL,
          FALSE));
  ASSERT_CRITICAL (gst_tag_parse_extended_comment ("\777\000", NULL, NULL, NULL,
          FALSE));

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("a=b", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "a");
  fail_unless_equals_string (val, "b");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("a[l]=b", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang != NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "a");
  fail_unless_equals_string (lang, "l");
  fail_unless_equals_string (val, "b");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo=bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (val, "bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang != NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (lang, "fr");
  fail_unless_equals_string (val, "bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo=[fr]bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (val, "[fr]bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  /* test NULL for output locations */
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", NULL, NULL, NULL,
          FALSE) == TRUE);

  /* test strict mode (key must be specified) */
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", NULL, NULL, NULL,
          TRUE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("foo=bar", NULL, NULL, NULL,
          TRUE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("foobar", NULL, NULL, NULL,
          TRUE) == FALSE);

  /* test non-strict mode (if there's no key, that's fine too) */
  fail_unless (gst_tag_parse_extended_comment ("foobar", NULL, NULL, NULL,
          FALSE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("[fr]bar", NULL, NULL, NULL,
          FALSE) == TRUE);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("[fr]bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key == NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (val, "[fr]bar");
  g_free (key);
  g_free (lang);
  g_free (val);
}

GST_END_TEST;

static Suite *
tag_suite (void)
{
  Suite *s = suite_create ("tag support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_extended_comment);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = tag_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
