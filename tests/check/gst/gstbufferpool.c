/* GStreamer
 * Copyright (C) 2014 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstbufferpool.c: Unit test for GstBufferPool
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

static GstBufferPool *
create_pool (guint size, guint min_buf, guint max_buf)
{
  GstBufferPool *pool = gst_buffer_pool_new ();
  GstStructure *conf = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (conf, NULL, size, min_buf, max_buf);
  gst_buffer_pool_set_config (pool, conf);

  return pool;
}

GST_START_TEST (test_new_buffer_from_empty_pool)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf = NULL;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_if (buf == NULL, "acquiring buffer returned NULL");

  gst_buffer_unref (buf);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;


GST_START_TEST (test_buffer_is_recycled)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf = NULL, *prev;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  prev = buf;
  gst_buffer_unref (buf);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (buf == prev, "got a fresh buffer instead of previous");

  gst_buffer_unref (buf);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;


GST_START_TEST (test_buffer_out_of_order_reuse)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf1 = NULL, *buf2 = NULL, *prev;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf1, NULL);
  gst_buffer_pool_acquire_buffer (pool, &buf2, NULL);
  prev = buf2;
  gst_buffer_unref (buf2);

  gst_buffer_pool_acquire_buffer (pool, &buf2, NULL);
  fail_unless (buf2 == prev, "got a fresh buffer instead of previous");

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;


GST_START_TEST (test_pool_config_buffer_size)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf = NULL;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  ck_assert_int_eq (gst_buffer_get_size (buf), 10);

  gst_buffer_unref (buf);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;


GST_START_TEST (test_inactive_pool_returns_flushing)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstFlowReturn ret;
  GstBuffer *buf = NULL;

  ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  ck_assert_int_eq (ret, GST_FLOW_FLUSHING);

  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_buffer_modify_discard)
{

  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf = NULL, *prev;
  GstMemory *mem;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  prev = buf;
  /* remove all memory, pool should not reuse this buffer */
  gst_buffer_remove_all_memory (buf);
  gst_buffer_unref (buf);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_if (buf == prev, "got a reused buffer instead of new one");
  prev = buf;
  /* do resize, pool should not reuse this buffer */
  gst_buffer_resize (buf, 5, 2);
  gst_buffer_unref (buf);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_if (buf == prev, "got a reused buffer instead of new one");
  prev = buf;
  /* keep ref to memory, not exclusive so pool should reuse this buffer */
  mem = gst_buffer_get_memory (buf, 0);
  gst_buffer_unref (buf);
  gst_memory_unref (mem);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (buf == prev, "got a fresh buffer instead of previous");
  mem = gst_buffer_get_memory (buf, 0);

  gst_buffer_unref (buf);

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;

static Suite *
gst_buffer_pool_suite (void)
{
  Suite *s = suite_create ("GstBufferPool");
  TCase *tc_chain = tcase_create ("buffer_pool tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_new_buffer_from_empty_pool);
  tcase_add_test (tc_chain, test_buffer_is_recycled);
  tcase_add_test (tc_chain, test_buffer_out_of_order_reuse);
  tcase_add_test (tc_chain, test_pool_config_buffer_size);
  tcase_add_test (tc_chain, test_inactive_pool_returns_flushing);
  tcase_add_test (tc_chain, test_buffer_modify_discard);

  return s;
}

GST_CHECK_MAIN (gst_buffer_pool);
