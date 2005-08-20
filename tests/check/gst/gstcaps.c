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
#include "capslist.h"

GST_START_TEST (test_from_string)
{
  GstCaps *caps;
  int i;

  for (i = 0; i < G_N_ELEMENTS (caps_list); i++) {
    caps = gst_caps_from_string (caps_list[i]);
    fail_if (caps == NULL,
        "Could not create caps from string %s\n", caps_list[i]);
    g_free (caps);
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
}

GST_END_TEST;

Suite *
gst_caps_suite (void)
{
  Suite *s = suite_create ("GstCaps");
  TCase *tc_chain = tcase_create ("mutability");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_from_string);
  tcase_add_test (tc_chain, test_double_append);
  tcase_add_test (tc_chain, test_mutability);
  tcase_add_test (tc_chain, test_buffer);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_caps_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
