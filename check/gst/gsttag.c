/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * parse1.c: Test various parsing stuff
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/check/gstcheck.h>

#include <string.h>

/* multiple artists are possible */
#define UTAG GST_TAG_ARTIST
#define UNFIXED1 "Britney Spears"
#define UNFIXED2 "Evanescence"
#define UNFIXED3 "AC/DC"
#define UNFIXED4 "The Prodigy"

/* license is fixed */
#define FTAG GST_TAG_LICENSE
#define FIXED1 "Lesser General Public License"
#define FIXED2 "Microsoft End User License Agreement"
#define FIXED3 "Mozilla Public License"
#define FIXED4 "Public Domain"

/* checks that a tag contains the given values and not more values */
static void
check_tags (const GstTagList * list, const gchar * tag, gchar * value, ...)
{
  va_list args;
  gchar *str;
  guint i = 0;

  va_start (args, value);
  while (value != NULL) {
    fail_unless (gst_tag_list_get_string_index (list, tag, i, &str));
    fail_unless (strcmp (value, str) == 0);
    g_free (str);

    value = va_arg (args, gchar *);
    i++;
  }
  fail_unless (i == gst_tag_list_get_tag_size (list, tag));
  va_end (args);
}

#define NEW_LIST_FIXED(mode)					\
G_STMT_START {							\
  if (list) gst_tag_list_free (list);				\
  list = gst_tag_list_new ();					\
  gst_tag_list_add (list, mode, FTAG, FIXED1, FTAG, FIXED2,	\
		    FTAG, FIXED3, FTAG, FIXED4, NULL);		\
} G_STMT_END;

#define NEW_LIST_UNFIXED(mode)					\
G_STMT_START {							\
  if (list) gst_tag_list_free (list);				\
  list = gst_tag_list_new ();					\
  gst_tag_list_add (list, mode, UTAG, UNFIXED1, UTAG, UNFIXED2, \
		    UTAG, UNFIXED3, UTAG, UNFIXED4, NULL);	\
} G_STMT_END;

#define NEW_LISTS_FIXED(mode)					\
G_STMT_START {							\
  if (list) gst_tag_list_free (list);				\
  list = gst_tag_list_new ();					\
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, FTAG, FIXED1,	\
		    FTAG, FIXED2, NULL);			\
  if (list2) gst_tag_list_free (list2);				\
  list2 = gst_tag_list_new ();					\
  gst_tag_list_add (list2, GST_TAG_MERGE_APPEND, FTAG, FIXED3,  \
		    FTAG, FIXED4, NULL);			\
  if (merge) gst_tag_list_free (merge);				\
  merge = gst_tag_list_merge (list, list2, mode);		\
} G_STMT_END;

#define NEW_LISTS_UNFIXED(mode)					\
G_STMT_START {							\
  if (list) gst_tag_list_free (list);				\
  list = gst_tag_list_new ();					\
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, UTAG, UNFIXED1, \
		    UTAG, UNFIXED2, NULL);			\
  if (list2) gst_tag_list_free (list2);				\
  list2 = gst_tag_list_new ();					\
  gst_tag_list_add (list2, GST_TAG_MERGE_APPEND, UTAG, UNFIXED3,\
		    UTAG, UNFIXED4, NULL);			\
  if (merge) gst_tag_list_free (merge);				\
  merge = gst_tag_list_merge (list, list2, mode);		\
} G_STMT_END;


GST_START_TEST (test_merge)
{
  GstTagList *list = NULL, *list2 = NULL, *merge = NULL;

  /* make sure the assumptions work */
  fail_unless (gst_tag_is_fixed (FTAG));
  fail_unless (!gst_tag_is_fixed (UTAG));
  /* we check string here only */
  fail_unless (gst_tag_get_type (FTAG) == G_TYPE_STRING);
  fail_unless (gst_tag_get_type (UTAG) == G_TYPE_STRING);

  /* check additions */

  /* unfixed */
  NEW_LIST_UNFIXED (GST_TAG_MERGE_REPLACE_ALL);
  check_tags (list, UTAG, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_REPLACE);
  check_tags (list, UTAG, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_PREPEND);
  check_tags (list, UTAG, UNFIXED4, UNFIXED3, UNFIXED2, UNFIXED1, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_APPEND);
  check_tags (list, UTAG, UNFIXED1, UNFIXED2, UNFIXED3, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_KEEP);
  check_tags (list, UTAG, UNFIXED1, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_KEEP_ALL);
  check_tags (list, UTAG, NULL);

  /* fixed */
  NEW_LIST_FIXED (GST_TAG_MERGE_REPLACE_ALL);
  check_tags (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_REPLACE);
  check_tags (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_PREPEND);
  check_tags (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_APPEND);
  check_tags (list, FTAG, FIXED1, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_KEEP);
  check_tags (list, FTAG, FIXED1, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_KEEP_ALL);
  check_tags (list, FTAG, NULL);

  /* check merging */
  /* unfixed */
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_REPLACE_ALL);
  check_tags (merge, UTAG, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_REPLACE);
  check_tags (merge, UTAG, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_PREPEND);
  check_tags (merge, UTAG, UNFIXED3, UNFIXED4, UNFIXED1, UNFIXED2, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_APPEND);
  check_tags (merge, UTAG, UNFIXED1, UNFIXED2, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_KEEP);
  check_tags (merge, UTAG, UNFIXED1, UNFIXED2, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_KEEP_ALL);
  check_tags (merge, UTAG, UNFIXED1, UNFIXED2, NULL);

  /* fixed */
  NEW_LISTS_FIXED (GST_TAG_MERGE_REPLACE_ALL);
  check_tags (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_REPLACE);
  check_tags (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_PREPEND);
  check_tags (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_APPEND);
  check_tags (merge, FTAG, FIXED1, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_KEEP);
  check_tags (merge, FTAG, FIXED1, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_KEEP_ALL);
  check_tags (merge, FTAG, FIXED1, NULL);

  /* clean up */
  if (list)
    gst_tag_list_free (list);
  if (list2)
    gst_tag_list_free (list2);
  if (merge)
    gst_tag_list_free (merge);
}

GST_END_TEST
GST_START_TEST (test_date_tags)
{
  GstTagList *tag_list, *tag_list2;
  GDate *date, *date2;
  gchar *str;

  date = g_date_new_dmy (14, 10, 2005);
  tag_list = gst_tag_list_new ();
  gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, GST_TAG_DATE, date, NULL);

  str = gst_structure_to_string (tag_list);
  fail_if (str == NULL);
  fail_if (strstr (str, "2005-10-14") == NULL);

  tag_list2 = gst_structure_from_string (str, NULL);
  fail_if (tag_list2 == NULL);
  fail_if (!gst_tag_list_get_date (tag_list2, GST_TAG_DATE, &date2));
  gst_tag_list_free (tag_list2);
  g_free (str);

  fail_if (g_date_compare (date, date2) != 0);
  fail_if (g_date_get_day (date) != 14);
  fail_if (g_date_get_month (date) != 10);
  fail_if (g_date_get_year (date) != 2005);
  fail_if (g_date_get_day (date2) != 14);
  fail_if (g_date_get_month (date2) != 10);
  fail_if (g_date_get_year (date2) != 2005);
  g_date_free (date2);

  gst_tag_list_free (tag_list);
  g_date_free (date);
}
GST_END_TEST Suite * gst_tag_suite (void)
{
  Suite *s = suite_create ("GstTag");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_merge);
  tcase_add_test (tc_chain, test_date_tags);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_tag_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
