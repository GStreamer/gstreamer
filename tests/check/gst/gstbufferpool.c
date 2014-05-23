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
  GstCaps *caps = gst_caps_new_empty_simple ("test/data");

  gst_buffer_pool_config_set_params (conf, caps, size, min_buf, max_buf);
  gst_buffer_pool_set_config (pool, conf);
  gst_caps_unref (caps);

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
  /* exclusive lock so pool should not reuse this buffer */
  gst_memory_lock (mem, GST_LOCK_FLAG_EXCLUSIVE);
  gst_buffer_unref (buf);
  gst_memory_unlock (mem, GST_LOCK_FLAG_EXCLUSIVE);
  gst_memory_unref (mem);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_if (buf == prev, "got a reused buffer instead of new one");
  gst_buffer_unref (buf);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_pool_activation_and_config)
{
  GstBufferPool *pool = gst_buffer_pool_new ();
  GstStructure *config = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_caps_new_empty_simple ("test/data");
  GstBuffer *buffer = NULL;

  /* unconfigured pool cannot be activated */
  fail_if (gst_buffer_pool_set_active (pool, TRUE));

  gst_buffer_pool_config_set_params (config, caps, 10, 10, 0);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  /* setting the same config on an active pool is ok */
  config = gst_buffer_pool_get_config (pool);
  fail_unless (gst_buffer_pool_set_config (pool, config));

  /* setting a different config should deactivate the pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 12, 10, 0);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_if (gst_buffer_pool_is_active (pool));

  /* though it should fail if there is outstanding buffers */
  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_if (buffer == NULL);
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 10, 10, 0);
  fail_if (gst_buffer_pool_set_config (pool, config));

  /* and work when last buffer is back */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 10, 10, 0);
  gst_buffer_unref (buffer);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_pool_config_validate)
{
  GstBufferPool *pool = create_pool (5, 4, 30);
  GstStructure *config = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_caps_new_empty_simple ("test/data");

  fail_unless (gst_buffer_pool_config_validate_params (config, caps, 5, 4, 0));
  fail_unless (gst_buffer_pool_config_validate_params (config, caps, 5, 2, 0));
  fail_if (gst_buffer_pool_config_validate_params (config, caps, 5, 6, 0));
  fail_if (gst_buffer_pool_config_validate_params (config, caps, 4, 4, 0));

  gst_caps_unref (caps);
  caps = gst_caps_new_empty_simple ("test/data2");
  fail_if (gst_buffer_pool_config_validate_params (config, caps, 5, 4, 0));

  gst_caps_unref (caps);
  gst_structure_free (config);
  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_flushing_pool_returns_flushing)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstFlowReturn ret;
  GstBuffer *buf = NULL;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_set_flushing (pool, TRUE);

  ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  ck_assert_int_eq (ret, GST_FLOW_FLUSHING);

  gst_buffer_pool_set_flushing (pool, FALSE);
  ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  gst_buffer_unref (buf);
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
  tcase_add_test (tc_chain, test_pool_activation_and_config);
  tcase_add_test (tc_chain, test_pool_config_validate);
  tcase_add_test (tc_chain, test_flushing_pool_returns_flushing);

  return s;
}

GST_CHECK_MAIN (gst_buffer_pool);
