/* GStreamer
 *
 * unit test for GstBufferList
 *
 * Copyright (C) 2009 Axis Communications <dev-gstreamer at axis dot com>
 * @author Jonas Holmberg <jonas dot holmberg at axis dot com>
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

#include <gst/check/gstcheck.h>
#include <gst/gstbufferlist.h>
#include <string.h>

static GstBufferList *list;

static void
setup (void)
{
  list = gst_buffer_list_new ();
}

static void
cleanup (void)
{
  gst_buffer_list_unref (list);
  list = NULL;
}

GST_START_TEST (test_add_and_iterate)
{
  GstBuffer *buf1;
  GstBuffer *buf2;

  /* buffer list is initially empty */
  fail_unless (gst_buffer_list_length (list) == 0);

  ASSERT_CRITICAL (gst_buffer_list_insert (list, 0, NULL));
  ASSERT_CRITICAL (gst_buffer_list_insert (NULL, 0, NULL));

  buf1 = gst_buffer_new ();

  /* add a group of 2 buffers */
  fail_unless (gst_buffer_list_length (list) == 0);
  ASSERT_CRITICAL (gst_buffer_list_insert (list, -1, NULL));
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  gst_buffer_list_add (list, buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);     /* list takes ownership */
  fail_unless (gst_buffer_list_length (list) == 1);
  buf2 = gst_buffer_new ();
  gst_buffer_list_add (list, buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  fail_unless (gst_buffer_list_length (list) == 2);
}

GST_END_TEST;

GST_START_TEST (test_remove)
{
  GstBuffer *buf;

  /* buffer list is initially empty */
  fail_unless (gst_buffer_list_length (list) == 0);

  buf = gst_buffer_new ();

  /* add our own ref so it stays alive after removal from the list */
  buf = gst_buffer_ref (buf);

  /* add a buffer */
  fail_unless (gst_buffer_list_length (list) == 0);
  ASSERT_CRITICAL (gst_buffer_list_insert (list, -1, NULL));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);
  gst_buffer_list_add (list, buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);       /* list takes ownership */
  fail_unless (gst_buffer_list_length (list) == 1);
  gst_buffer_list_remove (list, 0, 1);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  gst_buffer_unref (buf);
  fail_unless (gst_buffer_list_length (list) == 0);
}

GST_END_TEST;

GST_START_TEST (test_make_writable)
{
  GstBufferList *wlist;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to list */
  buf1 = gst_buffer_new_allocate (NULL, 1, NULL);
  gst_buffer_list_add (list, buf1);

  buf2 = gst_buffer_new_allocate (NULL, 2, NULL);
  buf3 = gst_buffer_new_allocate (NULL, 3, NULL);
  gst_buffer_list_add (list, gst_buffer_append (buf2, buf3));

  /* making it writable with refcount 1 returns the same list */
  wlist = gst_buffer_list_make_writable (list);
  fail_unless (wlist == list);
  fail_unless_equals_int (gst_buffer_list_length (list), 2);
  buf = gst_buffer_list_get (list, 0);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  fail_unless_equals_int (gst_buffer_get_size (buf), 1);
  buf = gst_buffer_list_get (list, 1);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  fail_unless_equals_int (gst_buffer_n_memory (buf), 2);

  /* making it writable with refcount 2 returns a copy of the list with
   * increased refcount on the buffers in the list */
  gst_buffer_list_ref (list);
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (list), 2);
  wlist = gst_buffer_list_make_writable (list);
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (list), 1);
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (wlist), 1);
  fail_unless (wlist != list);
  /* check list */
  fail_unless_equals_int (gst_buffer_list_length (list), 2);
  buf = gst_buffer_list_get (list, 0);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless_equals_int (gst_buffer_get_size (buf), 1);
  buf = gst_buffer_list_get (list, 1);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  fail_unless_equals_int (gst_buffer_n_memory (buf), 2);
  /* check wlist */
  fail_unless_equals_int (gst_buffer_list_length (wlist), 2);
  buf = gst_buffer_list_get (wlist, 0);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless_equals_int (gst_buffer_get_size (buf), 1);
  buf = gst_buffer_list_get (wlist, 1);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  fail_unless_equals_int (gst_buffer_n_memory (buf), 2);
  gst_buffer_list_unref (wlist);
  /* list will be unrefed in cleanup hook */
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstBufferList *list_copy;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to the list */
  buf1 = gst_buffer_new_allocate (NULL, 1, NULL);
  gst_buffer_list_add (list, buf1);

  buf2 = gst_buffer_new_allocate (NULL, 2, NULL);
  buf3 = gst_buffer_new_allocate (NULL, 3, NULL);
  gst_buffer_list_add (list, gst_buffer_append (buf2, buf3));

  /* make a copy */
  list_copy = gst_buffer_list_copy (list);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list) == 1);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list_copy) == 1);
  fail_unless (list_copy != list);
  fail_unless_equals_int (gst_buffer_list_length (list_copy), 2);
  buf = gst_buffer_list_get (list_copy, 0);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless_equals_int (gst_buffer_get_size (buf1), 1);
  buf = gst_buffer_list_get (list_copy, 1);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  fail_unless_equals_int (gst_buffer_get_size (buf2), 5);
  fail_unless_equals_int (gst_buffer_n_memory (buf2), 2);

  gst_buffer_list_unref (list_copy);
}

GST_END_TEST;

GST_START_TEST (test_copy_deep)
{
  GstBufferList *list_copy;
  GstMapInfo info, sinfo;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf_copy;

  /* add buffers to the list */
  buf1 = gst_buffer_new_allocate (NULL, 1, NULL);
  gst_buffer_list_add (list, buf1);

  buf2 = gst_buffer_new_allocate (NULL, 2, NULL);
  gst_buffer_list_add (list, buf2);

  /* make a copy */
  list_copy = gst_buffer_list_copy_deep (list);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list) == 1);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list_copy) == 1);
  fail_unless (list_copy != list);
  fail_unless_equals_int (gst_buffer_list_length (list_copy), 2);

  buf_copy = gst_buffer_list_get (list_copy, 0);
  /* each buffer in the list is copied and must point to different memory */
  fail_unless (buf_copy != buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  fail_unless_equals_int (gst_buffer_get_size (buf1), 1);

  buf_copy = gst_buffer_list_get (list_copy, 1);
  fail_unless (buf_copy != buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  fail_unless_equals_int (gst_buffer_get_size (buf2), 2);

  fail_unless (gst_buffer_map (buf2, &info, GST_MAP_READ));
  fail_unless (gst_buffer_map (buf_copy, &sinfo, GST_MAP_READ));

  /* NOTE that data is refcounted */
  fail_unless (info.size == sinfo.size);
  /* copy_deep() forces new GstMemory to be used */
  fail_unless (info.data != sinfo.data);

  gst_buffer_unmap (buf_copy, &sinfo);
  gst_buffer_unmap (buf2, &info);

  gst_buffer_list_unref (list_copy);
}

GST_END_TEST;

typedef struct
{
  GstBuffer *buf[2];
  guint iter;
} ForeachData;

static gboolean
foreach_func1 (GstBuffer ** buffer, guint idx, ForeachData * data)
{
  fail_unless (buffer != NULL);
  fail_unless (GST_IS_BUFFER (*buffer));
  fail_unless (*buffer == data->buf[idx]);

  data->iter++;

  return TRUE;
}

static gboolean
foreach_func3 (GstBuffer ** buffer, guint idx, ForeachData * data)
{
  fail_unless (idx == 0);
  fail_unless (buffer != NULL);
  fail_unless (GST_IS_BUFFER (*buffer));
  fail_unless (*buffer == data->buf[idx]);

  data->iter++;

  return FALSE;
}

static gboolean
foreach_func4 (GstBuffer ** buffer, guint idx, ForeachData * data)
{
  fail_unless (idx == 0);
  fail_unless (buffer != NULL);
  fail_unless (GST_IS_BUFFER (*buffer));
  fail_unless (*buffer == data->buf[data->iter]);

  /* remove first */
  if (*buffer == data->buf[0]) {
    gst_buffer_unref (*buffer);
    *buffer = NULL;
  }

  data->iter++;

  return TRUE;
}

static gboolean
foreach_func5 (GstBuffer ** buffer, guint idx, ForeachData * data)
{
  fail_unless (buffer != NULL);
  fail_unless (GST_IS_BUFFER (*buffer));

  data->iter++;

  return TRUE;
}

GST_START_TEST (test_foreach)
{
  GstBuffer *buf2, *buf3;
  ForeachData data;

  /* add buffers to the list */
  data.buf[0] = gst_buffer_new_allocate (NULL, 1, NULL);
  gst_buffer_list_add (list, data.buf[0]);

  buf2 = gst_buffer_new_allocate (NULL, 2, NULL);
  buf3 = gst_buffer_new_allocate (NULL, 3, NULL);
  data.buf[1] = gst_buffer_append (buf2, buf3);
  gst_buffer_list_add (list, data.buf[1]);

  fail_unless (gst_buffer_list_get (list, 0) == data.buf[0]);
  fail_unless (gst_buffer_list_get (list, 1) == data.buf[1]);

  /* iterate everything */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func1, &data);
  fail_unless (data.iter == 2);

  /* iterate only the first buffer */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func3, &data);
  fail_unless (data.iter == 1);

  /* remove the first buffer */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func4, &data);
  fail_unless (data.iter == 2);

  fail_unless (gst_buffer_list_get (list, 0) == data.buf[1]);
  fail_unless_equals_int (gst_buffer_list_length (list), 1);

  /* iterate everything, just one more buffer now */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func5, &data);
  fail_unless (data.iter == 1);
}

GST_END_TEST;

/* make sure everything is fine if we exceed the pre-allocated size */
GST_START_TEST (test_expand_and_remove)
{
  GArray *arr;
  GstBuffer *buf;
  guint i, idx, num, counter = 0;

  gst_buffer_list_unref (list);

  arr = g_array_new (FALSE, FALSE, sizeof (guint));

  list = gst_buffer_list_new_sized (1);

  for (i = 0; i < 250; ++i) {
    num = ++counter;
    buf = gst_buffer_new_allocate (NULL, num, NULL);
    gst_buffer_list_add (list, buf);
    g_array_append_val (arr, num);
  }

  for (i = 0; i < 250; ++i) {
    num = ++counter;
    buf = gst_buffer_new_allocate (NULL, num, NULL);
    idx = g_random_int_range (0, gst_buffer_list_length (list));
    gst_buffer_list_insert (list, idx, buf);
    g_array_insert_val (arr, idx, num);
  }

  /* make sure the list looks like it should */
  fail_unless_equals_int (arr->len, gst_buffer_list_length (list));
  for (i = 0; i < arr->len; ++i) {
    buf = gst_buffer_list_get (list, i);
    num = gst_buffer_get_size (buf);
    fail_unless_equals_int (num, g_array_index (arr, guint, i));
  }

  for (i = 0; i < 44; ++i) {
    num = g_random_int_range (1, 5);
    idx = g_random_int_range (0, gst_buffer_list_length (list) - num);
    gst_buffer_list_remove (list, idx, num);
    g_array_remove_range (arr, idx, num);
  }

  /* make sure the list still looks like it should */
  fail_unless_equals_int (arr->len, gst_buffer_list_length (list));
  for (i = 0; i < arr->len; ++i) {
    buf = gst_buffer_list_get (list, i);
    num = gst_buffer_get_size (buf);
    fail_unless_equals_int (num, g_array_index (arr, guint, i));
  }

  for (i = 0; i < 500; ++i) {
    num = ++counter;
    buf = gst_buffer_new_allocate (NULL, num, NULL);
    gst_buffer_list_add (list, buf);
    g_array_append_val (arr, num);
  }

  for (i = 0; i < 500; ++i) {
    num = ++counter;
    buf = gst_buffer_new_allocate (NULL, num, NULL);
    idx = g_random_int_range (0, gst_buffer_list_length (list));
    gst_buffer_list_insert (list, idx, buf);
    g_array_insert_val (arr, idx, num);
  }

  /* make sure the list still looks like it should */
  fail_unless_equals_int (arr->len, gst_buffer_list_length (list));
  for (i = 0; i < arr->len; ++i) {
    buf = gst_buffer_list_get (list, i);
    num = gst_buffer_get_size (buf);
    fail_unless_equals_int (num, g_array_index (arr, guint, i));
  }

  g_array_unref (arr);
}

GST_END_TEST;

static Suite *
gst_buffer_list_suite (void)
{
  Suite *s = suite_create ("GstBufferList");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, cleanup);
  tcase_add_test (tc_chain, test_add_and_iterate);
  tcase_add_test (tc_chain, test_remove);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_copy_deep);
  tcase_add_test (tc_chain, test_foreach);
  tcase_add_test (tc_chain, test_expand_and_remove);

  return s;
}

GST_CHECK_MAIN (gst_buffer_list);
