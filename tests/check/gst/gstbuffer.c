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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_VALGRIND_H
# include <valgrind/valgrind.h>
#else
# define RUNNING_ON_VALGRIND FALSE
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_caps)
{
  GstBuffer *buffer;
  GstCaps *caps, *caps2;

  buffer = gst_buffer_new_and_alloc (4);
  caps = gst_caps_from_string ("audio/x-raw-int");
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);

  gst_buffer_set_caps (buffer, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  fail_unless (GST_BUFFER_CAPS (buffer) == caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  fail_unless (gst_buffer_get_caps (buffer) == caps);
  gst_caps_unref (caps);
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
  /* set some metadata */
  GST_BUFFER_TIMESTAMP (buffer) = 1;
  GST_BUFFER_DURATION (buffer) = 2;
  GST_BUFFER_OFFSET (buffer) = 3;
  GST_BUFFER_OFFSET_END (buffer) = 4;

  sub = gst_buffer_create_sub (buffer, 1, 2);
  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 2, "subbuffer has wrong size");
  fail_unless (memcmp (GST_BUFFER_DATA (buffer) + 1, GST_BUFFER_DATA (sub),
          2) == 0, "subbuffer contains the wrong data");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 2);
  ASSERT_BUFFER_REFCOUNT (sub, "subbuffer", 1);
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == -1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_DURATION (sub) == -1, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET (sub) == -1, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == -1,
      "subbuffer has wrong offset end");
  gst_buffer_unref (sub);

  /* create a subbuffer of size 0 */
  sub = gst_buffer_create_sub (buffer, 1, 0);
  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 0, "subbuffer has wrong size");
  fail_unless (memcmp (GST_BUFFER_DATA (buffer) + 1, GST_BUFFER_DATA (sub),
          0) == 0, "subbuffer contains the wrong data");
  ASSERT_BUFFER_REFCOUNT (buffer, "parent", 2);
  ASSERT_BUFFER_REFCOUNT (sub, "subbuffer", 1);
  gst_buffer_unref (sub);

  /* test if metadata is coppied, not a complete buffer copy so only the
   * timestamp and offset fields are copied. */
  sub = gst_buffer_create_sub (buffer, 0, 1);
  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 1, "subbuffer has wrong size");
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == 1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_OFFSET (sub) == 3, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_DURATION (sub) == -1, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == -1,
      "subbuffer has wrong offset end");
  gst_buffer_unref (sub);

  /* test if metadata is coppied, a complete buffer is copied so all the timing
   * fields should be copied. */
  sub = gst_buffer_create_sub (buffer, 0, 4);
  fail_if (sub == NULL, "create_sub of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (sub) == 4, "subbuffer has wrong size");
  fail_unless (GST_BUFFER_TIMESTAMP (sub) == 1,
      "subbuffer has wrong timestamp");
  fail_unless (GST_BUFFER_DURATION (sub) == 2, "subbuffer has wrong duration");
  fail_unless (GST_BUFFER_OFFSET (sub) == 3, "subbuffer has wrong offset");
  fail_unless (GST_BUFFER_OFFSET_END (sub) == 4,
      "subbuffer has wrong offset end");

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


static const char ro_memory[] = "abcdefghijklmnopqrstuvwxyz";

static GstBuffer *
create_read_only_buffer (void)
{
  GstBuffer *buf;

  buf = (GstBuffer *) gst_mini_object_new (GST_TYPE_BUFFER);

  /* assign some read-only data to the new buffer */
  GST_BUFFER_DATA (buf) = (guint8 *) ro_memory;
  GST_BUFFER_SIZE (buf) = sizeof (ro_memory);

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  return buf;
}

GST_START_TEST (test_make_writable)
{
  GstBuffer *buf, *buf2;

  /* create read-only buffer and make it writable */
  buf = create_read_only_buffer ();
  fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY),
      "read-only buffer should have read-only flag set");
  buf = gst_buffer_make_writable (buf);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY),
      "writable buffer must not have read-only flag set");
  GST_BUFFER_DATA (buf)[4] = 'a';
  gst_buffer_unref (buf);

  /* alloc'ed buffer with refcount 1 should be writable */
  buf = gst_buffer_new_and_alloc (32);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY),
      "_new_and_alloc'ed buffer must not have read-only flag set");
  buf2 = gst_buffer_make_writable (buf);
  fail_unless (buf == buf2,
      "_make_writable() should have returned same buffer");
  gst_buffer_unref (buf2);

  /* alloc'ed buffer with refcount >1 should be copied */
  buf = gst_buffer_new_and_alloc (32);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY),
      "_new_and_alloc'ed buffer must not have read-only flag set");
  gst_buffer_ref (buf);
  buf2 = gst_buffer_make_writable (buf);
  fail_unless (buf != buf2, "_make_writable() should have returned a copy!");
  gst_buffer_unref (buf2);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_subbuffer_make_writable)
{
  GstBuffer *buf, *sub_buf;

  /* create sub-buffer of read-only buffer and make it writable */
  buf = create_read_only_buffer ();
  fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY),
      "read-only buffer should have read-only flag set");

  sub_buf = gst_buffer_create_sub (buf, 0, 8);
  fail_unless (GST_BUFFER_FLAG_IS_SET (sub_buf, GST_BUFFER_FLAG_READONLY),
      "sub-buffer of read-only buffer should have read-only flag set");

  sub_buf = gst_buffer_make_writable (sub_buf);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (sub_buf, GST_BUFFER_FLAG_READONLY),
      "writable buffer must not have read-only flag set");
  GST_BUFFER_DATA (sub_buf)[4] = 'a';
  gst_buffer_unref (sub_buf);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_metadata_writable)
{
  GstBuffer *buffer, *sub1;

  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* Buffer with refcount 1 should have writable metadata */
  fail_unless (gst_buffer_is_metadata_writable (buffer) == TRUE);

  /* Check that a buffer with refcount 2 does not have writable metadata */
  gst_buffer_ref (buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 2);
  fail_unless (gst_buffer_is_metadata_writable (buffer) == FALSE);

  /* Check that make_metadata_writable produces a new sub-buffer with 
   * writable metadata. */
  sub1 = gst_buffer_make_metadata_writable (buffer);
  fail_if (sub1 == buffer);
  fail_unless (gst_buffer_is_metadata_writable (sub1) == TRUE);

  /* Check that the original metadata is still not writable 
   * (subbuffer should be holding a reference, and so should we) */
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 2);
  fail_unless (gst_buffer_is_metadata_writable (buffer) == FALSE);

  /* Check that make_metadata_writable() maintains the buffer flags */
  fail_unless (GST_BUFFER_FLAG_IS_SET (sub1, GST_BUFFER_FLAG_DISCONT));
  fail_unless (GST_BUFFER_FLAG_IS_SET (sub1, GST_BUFFER_FLAG_DELTA_UNIT));

  /* Unset flags on writable buffer, then make sure they're still
   * set on the original buffer */
  GST_BUFFER_FLAG_UNSET (sub1, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_FLAG_UNSET (sub1, GST_BUFFER_FLAG_DELTA_UNIT);
  fail_unless (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT));
  fail_unless (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));

  /* Drop the subbuffer and check that the metadata is now writable again */
  ASSERT_BUFFER_REFCOUNT (sub1, "sub1", 1);
  gst_buffer_unref (sub1);
  fail_unless (gst_buffer_is_metadata_writable (buffer) == TRUE);

  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstBuffer *buffer, *copy;

  buffer = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);

  copy = gst_buffer_copy (buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  ASSERT_BUFFER_REFCOUNT (copy, "copy", 1);
  /* data must be copied and thus point to different memory */
  fail_if (GST_BUFFER_DATA (buffer) == GST_BUFFER_DATA (copy));

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);

  /* a 0-sized buffer has NULL data as per docs */
  buffer = gst_buffer_new_and_alloc (0);
  fail_unless (GST_BUFFER_DATA (buffer) == NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 0);

  /* copying a 0-sized buffer should not crash and also set
   * the data member NULL. */
  copy = gst_buffer_copy (buffer);
  fail_unless (GST_BUFFER_DATA (copy) == NULL);
  fail_unless (GST_BUFFER_SIZE (copy) == 0);

  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_try_new_and_alloc)
{
  GstBuffer *buf;

  /* special case: alloc of 0 bytes results in new buffer with NULL data */
  buf = gst_buffer_try_new_and_alloc (0);
  fail_unless (buf != NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless (GST_BUFFER_SIZE (buf) == 0);
  fail_unless (GST_BUFFER_DATA (buf) == NULL);
  fail_unless (GST_BUFFER_MALLOCDATA (buf) == NULL);
  gst_buffer_unref (buf);

  /* normal alloc should still work */
  buf = gst_buffer_try_new_and_alloc (640 * 480 * 4);
  fail_unless (buf != NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless (GST_BUFFER_SIZE (buf) == (640 * 480 * 4));
  fail_unless (GST_BUFFER_DATA (buf) != NULL);
  fail_unless (GST_BUFFER_MALLOCDATA (buf) != NULL);
  GST_BUFFER_DATA (buf)[640 * 479 * 4 + 479] = 0xff;
  gst_buffer_unref (buf);

#if 0
  /* Disabled this part of the test, because it happily succeeds on 64-bit
   * machines that have enough memory+swap, because the address space is large
   * enough. There's not really any way to test the failure case except by 
   * allocating chunks of memory until it fails, which would suck. */

  /* now this better fail (don't run in valgrind, it will abort
   * or warn when passing silly arguments to malloc) */
  if (!RUNNING_ON_VALGRIND) {
    buf = gst_buffer_try_new_and_alloc ((guint) - 1);
    fail_unless (buf == NULL);
  }
#endif
}

GST_END_TEST;

GST_START_TEST (test_qdata)
{
  GstStructure *s;
  GstBuffer *buf, *buf2, *buf3;
  GQuark q1, q2, q3;

  q1 = g_quark_from_static_string ("GstFooBar");
  q2 = g_quark_from_static_string ("MyBorkData");
  q3 = g_quark_from_static_string ("DoNotExist");

  buf = gst_buffer_new ();
  ASSERT_CRITICAL (gst_buffer_set_qdata (buf, q1, (s =
              gst_structure_id_empty_new (q2))));
  gst_structure_free (s);

  gst_buffer_set_qdata (buf, q1, gst_structure_id_empty_new (q1));
  gst_buffer_set_qdata (buf, q2, gst_structure_id_empty_new (q2));
  fail_unless (gst_buffer_get_qdata (buf, q3) == NULL);
  fail_unless (gst_buffer_get_qdata (buf, q1) != NULL);
  fail_unless (gst_buffer_get_qdata (buf, q2) != NULL);

  /* full copy */
  buf2 = gst_buffer_copy (buf);

  /* now back to the original buffer... */
  gst_buffer_set_qdata (buf, q1, NULL);
  fail_unless (gst_buffer_get_qdata (buf, q1) == NULL);

  /* force creation of sub-buffer with writable metadata */
  gst_buffer_ref (buf);
  buf3 = gst_buffer_make_metadata_writable (buf);

  /* and check the copies/subbuffers.. */
  fail_unless (gst_buffer_get_qdata (buf2, q3) == NULL);
  fail_unless (gst_buffer_get_qdata (buf2, q1) != NULL);
  fail_unless (gst_buffer_get_qdata (buf2, q2) != NULL);

  fail_unless (gst_buffer_get_qdata (buf3, q3) == NULL);
  fail_unless (gst_buffer_get_qdata (buf3, q1) == NULL);
  fail_unless (gst_buffer_get_qdata (buf3, q2) != NULL);
  gst_buffer_set_qdata (buf3, q1, gst_structure_id_empty_new (q1));
  fail_unless (gst_buffer_get_qdata (buf3, q1) != NULL);

  /* original buffer shouldn't have changed */
  fail_unless (gst_buffer_get_qdata (buf, q1) == NULL);

  gst_buffer_unref (buf);
  gst_buffer_unref (buf2);
  gst_buffer_unref (buf3);
}

GST_END_TEST;

static Suite *
gst_buffer_suite (void)
{
  Suite *s = suite_create ("GstBuffer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_caps);
  tcase_add_test (tc_chain, test_subbuffer);
  tcase_add_test (tc_chain, test_subbuffer_make_writable);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_is_span_fast);
  tcase_add_test (tc_chain, test_span);
  tcase_add_test (tc_chain, test_metadata_writable);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_try_new_and_alloc);
  tcase_add_test (tc_chain, test_qdata);

  return s;
}

GST_CHECK_MAIN (gst_buffer);
