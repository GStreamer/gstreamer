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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

static void
buffer_destroy_notify (gpointer ptr)
{
  gint *counter = ptr;

  GST_DEBUG ("buffer destroyed");

  *counter += 1;
}

/* Track when a buffer is destroyed. The counter will be increased if the
 * buffer is finalized (but not if it was re-surrected in dispose and put
 * back into the buffer pool. */
static void
buffer_track_destroy (GstBuffer * buf, gint * counter)
{
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buf),
      g_quark_from_static_string ("TestTracker"),
      counter, buffer_destroy_notify);
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
  gint dcount = 0;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  prev = buf;
  buffer_track_destroy (buf, &dcount);
  gst_buffer_unref (buf);

  /* buffer should not have been freed, but have been recycled */
  fail_unless (dcount == 0);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (buf == prev, "got a fresh buffer instead of previous");

  gst_buffer_unref (buf);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  /* buffer should now be gone */
  fail_unless (dcount == 1);
}

GST_END_TEST;


GST_START_TEST (test_buffer_out_of_order_reuse)
{
  GstBufferPool *pool = create_pool (10, 0, 0);
  GstBuffer *buf1 = NULL, *buf2 = NULL, *prev;
  gint dcount1 = 0, dcount2 = 0;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf1, NULL);
  buffer_track_destroy (buf1, &dcount1);
  gst_buffer_pool_acquire_buffer (pool, &buf2, NULL);
  buffer_track_destroy (buf2, &dcount2);
  prev = buf2;
  gst_buffer_unref (buf2);

  /* buffer should not have been freed, but have been recycled */
  fail_unless (dcount2 == 0);

  gst_buffer_pool_acquire_buffer (pool, &buf2, NULL);
  fail_unless (buf2 == prev, "got a fresh buffer instead of previous");

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  fail_unless (dcount1 == 1);
  fail_unless (dcount2 == 1);
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
  gint dcount = 0;

  gst_buffer_pool_set_active (pool, TRUE);
  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (buf != NULL);
  prev = buf;
  buffer_track_destroy (buf, &dcount);
  /* remove all memory, pool should not reuse this buffer */
  gst_buffer_remove_all_memory (buf);
  gst_buffer_unref (buf);

  /* buffer should've been destroyed instead of going back into pool */
  fail_unless_equals_int (dcount, 1);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  prev = buf;
  buffer_track_destroy (buf, &dcount);
  /* do resize, pool should not reuse this buffer */
  gst_buffer_resize (buf, 5, 2);
  gst_buffer_unref (buf);

  /* buffer should've been destroyed instead of going back into pool */
  fail_unless_equals_int (dcount, 2);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  prev = buf;
  buffer_track_destroy (buf, &dcount);
  /* keep ref to memory, not exclusive so pool should reuse this buffer */
  mem = gst_buffer_get_memory (buf, 0);
  gst_buffer_unref (buf);
  gst_memory_unref (mem);

  /* buffer should not have been destroyed and gone back into pool */
  fail_unless_equals_int (dcount, 2);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (buf == prev, "got a fresh buffer instead of previous");
  /* we're already did track_destroy on this buf, so no need to do it again */
  mem = gst_buffer_get_memory (buf, 0);
  /* exclusive lock so pool should not reuse this buffer */
  gst_memory_lock (mem, GST_LOCK_FLAG_EXCLUSIVE);
  gst_buffer_unref (buf);
  gst_memory_unlock (mem, GST_LOCK_FLAG_EXCLUSIVE);
  gst_memory_unref (mem);

  /* buffer should have been destroyed and not gone back into pool because
   * of the exclusive lock */
  fail_unless_equals_int (dcount, 3);

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_pool_activation_and_config)
{
  GstBufferPool *pool = gst_buffer_pool_new ();
  GstStructure *config = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_caps_new_empty_simple ("test/data");

  /* unconfigured pool cannot be activated */
  fail_if (gst_buffer_pool_set_active (pool, TRUE));

  gst_buffer_pool_config_set_params (config, caps, 10, 10, 0);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  /* setting the same config on an active pool is ok */
  config = gst_buffer_pool_get_config (pool);
  fail_unless (gst_buffer_pool_set_config (pool, config));

  /* setting a different config on active pool should fail */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 12, 10, 0);
  fail_if (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_is_active (pool));

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
  fail_unless (gst_buffer_pool_config_validate_params (config, caps, 4, 4, 0));
  fail_if (gst_buffer_pool_config_validate_params (config, caps, 5, 6, 0));

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
