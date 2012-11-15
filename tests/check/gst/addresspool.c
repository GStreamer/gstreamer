/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
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

#include <rtsp-address-pool.h>

GST_START_TEST (test_pool)
{
  GstRTSPAddressPool *pool;
  GstRTSPAddress *addr;

  pool = gst_rtsp_address_pool_new ();

  fail_if (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.1", "233.252.0.0", 5000, 5010, 1));
  ASSERT_CRITICAL (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.0", "233.252.0.1", 5010, 5000, 1));

  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.0", "233.252.0.255", 5000, 5010, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.255.0.0", "233.255.0.0", 5000, 5010, 1));
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.255.0.0", "233.255.0.0", 5020, 5020, 1));

  /* should fail, we can't allocate a block of 256 ports */
  addr = gst_rtsp_address_pool_acquire_address (pool, 0, 256);
  fail_unless (addr == NULL);

  addr = gst_rtsp_address_pool_acquire_address (pool, 0, 2);
  fail_unless (addr != NULL);

  gst_rtsp_address_free (addr);

  addr = gst_rtsp_address_pool_acquire_address (pool, 0, 4);
  fail_unless (addr != NULL);

  gst_rtsp_address_free (addr);

  g_object_unref (pool);
}

GST_END_TEST;

static Suite *
rtspaddresspool_suite (void)
{
  Suite *s = suite_create ("rtspaddresspool");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_pool);

  return s;
}

GST_CHECK_MAIN (rtspaddresspool);
