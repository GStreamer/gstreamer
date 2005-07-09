/* GStreamer
 *
 * unit test for GstData
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

GST_START_TEST (test_copy)
{
  GstBuffer *buffer, *copy;

  buffer = gst_buffer_new_and_alloc (4);

  copy = GST_BUFFER (gst_data_copy (GST_DATA (buffer)));

  fail_if (copy == NULL, "Copy of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (copy) == 4,
      "Copy of buffer has different size");
}

GST_END_TEST
GST_START_TEST (test_is_writable)
{
  GstBuffer *buffer;
  GstData *data;

  buffer = gst_buffer_new_and_alloc (4);
  data = GST_DATA (buffer);

  fail_unless (gst_data_is_writable (data),
      "A buffer with one ref should be writable");

  GST_DATA_FLAG_SET (data, GST_DATA_READONLY);
  fail_if (gst_data_is_writable (data),
      "A buffer with READONLY set should not be writable");
  GST_DATA_FLAG_UNSET (data, GST_DATA_READONLY);
  fail_unless (gst_data_is_writable (data),
      "A buffer with one ref and READONLY not set should be writable");

  fail_if (gst_data_ref (data) == NULL, "Could not ref the data");

  fail_if (gst_data_is_writable (data),
      "A buffer with two refs should not be writable");
}

GST_END_TEST
GST_START_TEST (test_copy_on_write)
{
  GstBuffer *buffer;
  GstData *data, *data2, *data3;

  buffer = gst_buffer_new_and_alloc (4);
  data = GST_DATA (buffer);

  data2 = gst_data_copy_on_write (data);
  fail_unless (GST_IS_BUFFER (data2), "copy_on_write did not return a buffer");
  fail_unless (data == data2,
      "copy_on_write returned a copy for a buffer with refcount 1");

  data2 = gst_data_ref (data);
  data3 = gst_data_copy_on_write (data);
  fail_unless (GST_IS_BUFFER (data3), "copy_on_write did not return a buffer");
  fail_if (data == data3,
      "copy_on_write returned same object for a buffer with refcount > 1");

  fail_unless (GST_DATA_REFCOUNT_VALUE (data) == 1,
      "refcount of original data object should be back to 1");

  data2 = gst_data_copy_on_write (data);
  fail_unless (GST_IS_BUFFER (data2), "copy_on_write did not return a buffer");
  fail_unless (data == data2,
      "copy_on_write returned a copy for a buffer with refcount 1");

}

GST_END_TEST gint num_threads = 10;
gint refs_per_thread = 10000;

/* test thread-safe refcounting of GstData */
void
thread_ref (GstData * data)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    fail_if (gst_data_ref (data) == NULL, "Could not ref data from thread");

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
  g_message ("thread stopped\n");
}

GST_START_TEST (test_ref_threaded)
{
  GstBuffer *buffer;
  GstData *data;
  gint expected;

  buffer = gst_buffer_new_and_alloc (4);

  data = GST_DATA (buffer);

  MAIN_START_THREADS (num_threads, thread_ref, data);

  MAIN_STOP_THREADS ();

  expected = num_threads * refs_per_thread + 1;
  fail_unless (GST_DATA_REFCOUNT_VALUE (data) == expected,
      "Refcount of data is %d != %d", GST_DATA_REFCOUNT_VALUE (data), expected);
}
GST_END_TEST void
thread_unref (GstData * data)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    gst_data_unref (data);

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
}

GST_START_TEST (test_unref_threaded)
{
  GstBuffer *buffer;
  GstData *data;

  buffer = gst_buffer_new_and_alloc (4);

  data = GST_DATA (buffer);

  gst_data_ref_by_count (data, num_threads * refs_per_thread);

  MAIN_START_THREADS (num_threads, thread_unref, data);

  MAIN_STOP_THREADS ();

  fail_unless (GST_DATA_REFCOUNT_VALUE (data) == 1,
      "Refcount of data is %d != %d", GST_DATA_REFCOUNT_VALUE (data), 1);

  /* final unref */
  gst_data_unref (data);
}
GST_END_TEST Suite *
gst_data_suite (void)
{
  Suite *s = suite_create ("GstData");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_is_writable);
  tcase_add_test (tc_chain, test_copy_on_write);
  tcase_add_test (tc_chain, test_ref_threaded);
  tcase_add_test (tc_chain, test_unref_threaded);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_data_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
