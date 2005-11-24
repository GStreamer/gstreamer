/* GStreamer
 *
 * unit test for GstMiniObject
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

#include <gst/check/gstcheck.h>

GST_START_TEST (test_copy)
{
  GstBuffer *buffer, *copy;

  buffer = gst_buffer_new_and_alloc (4);

  copy = GST_BUFFER (gst_mini_object_copy (GST_MINI_OBJECT (buffer)));

  fail_if (copy == NULL, "Copy of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (copy) == 4,
      "Copy of buffer has different size");
}

GST_END_TEST;

GST_START_TEST (test_is_writable)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;

  buffer = gst_buffer_new_and_alloc (4);
  mobj = GST_MINI_OBJECT (buffer);

  fail_unless (gst_mini_object_is_writable (mobj),
      "A buffer with one ref should be writable");

  GST_MINI_OBJECT_FLAG_SET (mobj, GST_MINI_OBJECT_FLAG_READONLY);
  fail_if (gst_mini_object_is_writable (mobj),
      "A buffer with READONLY set should not be writable");
  GST_MINI_OBJECT_FLAG_UNSET (mobj, GST_MINI_OBJECT_FLAG_READONLY);
  fail_unless (gst_mini_object_is_writable (mobj),
      "A buffer with one ref and READONLY not set should be writable");

  fail_if (gst_mini_object_ref (mobj) == NULL, "Could not ref the mobj");

  fail_if (gst_mini_object_is_writable (mobj),
      "A buffer with two refs should not be writable");
}

GST_END_TEST;

GST_START_TEST (test_make_writable)
{
  GstBuffer *buffer;
  GstMiniObject *mobj, *mobj2, *mobj3;

  buffer = gst_buffer_new_and_alloc (4);
  mobj = GST_MINI_OBJECT (buffer);

  mobj2 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj2), "make_writable did not return a buffer");
  fail_unless (mobj == mobj2,
      "make_writable returned a copy for a buffer with refcount 1");

  mobj2 = gst_mini_object_ref (mobj);
  mobj3 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj3), "make_writable did not return a buffer");
  fail_if (mobj == mobj3,
      "make_writable returned same object for a buffer with refcount > 1");

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (mobj) == 1,
      "refcount of original mobj object should be back to 1");

  mobj2 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj2), "make_writable did not return a buffer");
  fail_unless (mobj == mobj2,
      "make_writable returned a copy for a buffer with refcount 1");

}

GST_END_TEST;

gint num_threads = 10;
gint refs_per_thread = 10000;

/* test thread-safe refcounting of GstMiniObject */
void
thread_ref (GstMiniObject * mobj)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    gst_mini_object_ref (mobj);

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
  GST_DEBUG ("thread stopped");
}

GST_START_TEST (test_ref_threaded)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;
  gint expected;

  buffer = gst_buffer_new_and_alloc (4);

  mobj = GST_MINI_OBJECT (buffer);

  MAIN_START_THREADS (num_threads, thread_ref, mobj);

  MAIN_STOP_THREADS ();

  expected = num_threads * refs_per_thread + 1;
  ASSERT_MINI_OBJECT_REFCOUNT (mobj, "miniobject", expected);
}

GST_END_TEST;

void
thread_unref (GstMiniObject * mobj)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    gst_mini_object_unref (mobj);

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
}

GST_START_TEST (test_unref_threaded)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;
  int i;

  buffer = gst_buffer_new_and_alloc (4);

  mobj = GST_MINI_OBJECT (buffer);

  for (i = 0; i < num_threads * refs_per_thread; ++i)
    gst_mini_object_ref (mobj);

  MAIN_START_THREADS (num_threads, thread_unref, mobj);

  MAIN_STOP_THREADS ();

  ASSERT_MINI_OBJECT_REFCOUNT (mobj, "miniobject", 1);

  /* final unref */
  gst_mini_object_unref (mobj);
}

GST_END_TEST;

Suite *
gst_mini_object_suite (void)
{
  Suite *s = suite_create ("GstMiniObject");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_is_writable);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_ref_threaded);
  tcase_add_test (tc_chain, test_unref_threaded);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_mini_object_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
