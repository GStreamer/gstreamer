/* GStreamer
 *
 * unit test for GstBuffer
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

GST_START_TEST (test_subbuffer)
{
  GstBuffer *buffer, *sub;
  int rc;

  buffer = gst_buffer_new_and_alloc (4);
  memset (GST_BUFFER_DATA (buffer), 0, 4);

  sub = gst_buffer_create_sub (buffer, 1, 2);

  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 2, "subbuffer has wrong size");
  fail_unless (memcmp (GST_BUFFER_DATA (buffer) + 1, GST_BUFFER_DATA (sub),
          2) == 0, "subbuffer contains the wrong data");

  rc = GST_MINI_OBJECT_REFCOUNT_VALUE (buffer);
  fail_unless (rc == 2, "parent refcount is %d instead of 3", rc);

  /* clean up */
  gst_buffer_unref (sub);
  gst_buffer_unref (buffer);
}

GST_END_TEST
GST_START_TEST (test_is_span_fast)
{
  GstBuffer *buffer, *sub1, *sub2;

  buffer = gst_buffer_new_and_alloc (4);

  sub1 = gst_buffer_create_sub (buffer, 0, 2);
  fail_if (sub1 == NULL, "create_sub of buffer returned NULL");

  sub2 = gst_buffer_create_sub (buffer, 2, 2);
  fail_if (sub2 == NULL, "create_sub of buffer returned NULL");

  fail_if (gst_buffer_is_span_fast (buffer, sub2) == TRUE,
      "a parent buffer can't be span_fasted");

  fail_if (gst_buffer_is_span_fast (sub1, buffer) == TRUE,
      "a parent buffer can't be span_fasted");

  fail_if (gst_buffer_is_span_fast (sub1, sub2) == FALSE,
      "two subbuffers next to each other should be span_fast");

  /* clean up */
  gst_buffer_unref (sub1);
  gst_buffer_unref (sub2);
  gst_buffer_unref (buffer);
}
GST_END_TEST Suite *
gst_test_suite (void)
{
  Suite *s = suite_create ("GstBuffer");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_subbuffer);
  tcase_add_test (tc_chain, test_is_span_fast);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_test_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
