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

GST_START_TEST (test_caps)
{
  GstBuffer *buffer;
  GstCaps *caps, *caps2;

  buffer = gst_buffer_new_and_alloc (4);
  caps = gst_caps_from_string ("audio/x-raw-int");
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  fail_unless (gst_buffer_get_caps (buffer) == NULL);

  gst_buffer_set_caps (buffer, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  fail_unless (gst_buffer_get_caps (buffer) == caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  caps2 = gst_caps_from_string ("audio/x-raw-float");
  ASSERT_CAPS_REFCOUNT (caps2, "caps2", 1);

  gst_buffer_set_caps (buffer, caps2);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  ASSERT_CAPS_REFCOUNT (caps2, "caps2", 2);

  gst_buffer_set_caps (buffer, NULL);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  ASSERT_CAPS_REFCOUNT (caps2, "caps2", 1);

  /* clean up, with caps2 still set as caps */
  gst_buffer_set_caps (buffer, caps2);
  ASSERT_CAPS_REFCOUNT (caps2, "caps2", 2);
  gst_buffer_unref (buffer);
  ASSERT_CAPS_REFCOUNT (caps2, "caps2", 1);
  gst_caps_unref (caps);
  gst_caps_unref (caps2);
}

GST_END_TEST;


GST_START_TEST (test_subbuffer)
{
  GstBuffer *buffer, *sub;

  buffer = gst_buffer_new_and_alloc (4);
  memset (GST_BUFFER_DATA (buffer), 0, 4);

  sub = gst_buffer_create_sub (buffer, 1, 2);
  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 2, "subbuffer has wrong size");
  fail_unless (memcmp (GST_BUFFER_DATA (buffer) + 1, GST_BUFFER_DATA (sub),
          2) == 0, "subbuffer contains the wrong data");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 2);
  ASSERT_BUFFER_REFCOUNT (sub, "subbuffer", 1);

  /* clean up */
  gst_buffer_unref (sub);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

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

GST_END_TEST;

GST_START_TEST (test_span)
{
  GstBuffer *buffer, *sub1, *sub2, *span;

  buffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (buffer), "data", 4);

  ASSERT_CRITICAL (gst_buffer_span (NULL, 1, NULL, 2));
  ASSERT_CRITICAL (gst_buffer_span (buffer, 1, NULL, 2));
  ASSERT_CRITICAL (gst_buffer_span (NULL, 1, buffer, 2));
  ASSERT_CRITICAL (gst_buffer_span (buffer, 0, buffer, 10));

  sub1 = gst_buffer_create_sub (buffer, 0, 2);
  fail_if (sub1 == NULL, "create_sub of buffer returned NULL");

  sub2 = gst_buffer_create_sub (buffer, 2, 2);
  fail_if (sub2 == NULL, "create_sub of buffer returned NULL");

  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);

  /* span will create a new subbuffer from the parent */
  span = gst_buffer_span (sub1, 0, sub2, 4);
  fail_unless (GST_BUFFER_SIZE (span) == 4, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 4);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (span), "data", 4) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);

  /* span from non-contiguous buffers will create new buffers */
  span = gst_buffer_span (sub2, 0, sub1, 4);
  fail_unless (GST_BUFFER_SIZE (span) == 4, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (span), "tada", 4) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);

  /* span with different sizes */
  span = gst_buffer_span (sub1, 1, sub2, 3);
  fail_unless (GST_BUFFER_SIZE (span) == 3, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 4);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (span), "ata", 3) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);

  span = gst_buffer_span (sub2, 0, sub1, 3);
  fail_unless (GST_BUFFER_SIZE (span) == 3, "spanned buffer is wrong size");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  ASSERT_BUFFER_REFCOUNT (sub2, "sub2", 1);
  ASSERT_BUFFER_REFCOUNT (span, "span", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (span), "tad", 3) == 0,
      "spanned buffer contains the wrong data");
  gst_buffer_unref (span);
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 3);

/* clean up */
  gst_buffer_unref (sub1);
  gst_buffer_unref (sub2);
  gst_buffer_unref (buffer);
}

GST_END_TEST;


Suite *
gst_test_suite (void)
{
  Suite *s = suite_create ("GstBuffer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_caps);
  tcase_add_test (tc_chain, test_subbuffer);
  tcase_add_test (tc_chain, test_is_span_fast);
  tcase_add_test (tc_chain, test_span);
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
