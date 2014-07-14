/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <ts.santos@sisa.samsung.com>
 *
 * gsttaglist.c: Unit tests for GstTaglist
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


#include <gst/gst.h>
#include <gst/check/gstcheck.h>


GST_START_TEST (test_no_tags_string_serialization)
{
  GstTagList *taglist, *taglist2;
  gchar *str;

  taglist = gst_tag_list_new_empty ();
  str = gst_tag_list_to_string (taglist);
  taglist2 = gst_tag_list_new_from_string (str);
  fail_if (taglist2 == NULL);
  fail_unless (gst_tag_list_is_equal (taglist, taglist2));

  gst_tag_list_unref (taglist);
  gst_tag_list_unref (taglist2);
  g_free (str);
}

GST_END_TEST;

static Suite *
gst_tag_list_suite (void)
{
  Suite *s = suite_create ("GstTaglist");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_no_tags_string_serialization);
  return s;
}

GST_CHECK_MAIN (gst_tag_list);
