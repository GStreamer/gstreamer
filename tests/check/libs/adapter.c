/* GStreamer
 *
 * unit test for adapter
 *
 * Copyright (C) <2005> Wim Taymans <wim at fluendo dot com>
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

#include <gst/base/gstadapter.h>

/* does some implementation dependent checking that should 
 * also be optimal 
 */

/*
 * Start peeking on an adapter with 1 buffer pushed. 
 */
GST_START_TEST (test_peek1)
{
  GstAdapter *adapter;
  GstBuffer *buffer;
  guint avail;
  GstMapInfo info;
  const guint8 *data1, *data2, *idata;

  adapter = gst_adapter_new ();
  fail_if (adapter == NULL);

  /* push single buffer in adapter */
  buffer = gst_buffer_new_and_alloc (512);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  idata = info.data;
  gst_buffer_unmap (buffer, &info);

  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  /* available and available_fast should return the size of the 
   * buffer */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 512);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 512);

  /* should g_critical with NULL as result */
  ASSERT_CRITICAL (data1 = gst_adapter_map (adapter, 0));
  fail_if (data1 != NULL);

  /* should return NULL as result */
  data1 = gst_adapter_map (adapter, 513);
  fail_if (data1 != NULL);

  /* this should work */
  data1 = gst_adapter_map (adapter, 512);
  fail_if (data1 == NULL);
  /* it should point to the buffer data as well */
  fail_if (data1 != idata);
  gst_adapter_unmap (adapter);

  data2 = gst_adapter_map (adapter, 512);
  fail_if (data2 == NULL);
  /* second peek should return the same pointer */
  fail_if (data2 != data1);
  gst_adapter_unmap (adapter);

  /* this should fail since we don't have that many bytes */
  ASSERT_CRITICAL (gst_adapter_flush (adapter, 513));

  /* this should work fine */
  gst_adapter_flush (adapter, 10);

  /* see if we have 10 bytes less available */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 502);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 502);

  /* should return NULL as result */
  data2 = gst_adapter_map (adapter, 503);
  fail_if (data2 != NULL);

  /* should work fine */
  data2 = gst_adapter_map (adapter, 502);
  fail_if (data2 == NULL);
  /* peek should return the same old pointer + 10 */
  fail_if (data2 != data1 + 10);
  fail_if (data2 != (guint8 *) idata + 10);
  gst_adapter_unmap (adapter);

  /* flush some more */
  gst_adapter_flush (adapter, 500);

  /* see if we have 2 bytes available */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 2);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 2);

  data2 = gst_adapter_map (adapter, 2);
  fail_if (data2 == NULL);
  fail_if (data2 != data1 + 510);
  fail_if (data2 != (guint8 *) idata + 510);
  gst_adapter_unmap (adapter);

  /* flush some more */
  gst_adapter_flush (adapter, 2);

  /* see if we have 0 bytes available */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 0);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 0);

  /* silly clear just for fun */
  gst_adapter_clear (adapter);

  g_object_unref (adapter);
}

GST_END_TEST;

/* Start peeking on an adapter with 2 non-mergeable buffers 
 * pushed. 
 */
GST_START_TEST (test_peek2)
{
}

GST_END_TEST;

/* Start peeking on an adapter with 2 mergeable buffers 
 * pushed. 
 */
GST_START_TEST (test_peek3)
{
}

GST_END_TEST;

/* take data from an adapter with 1 buffer pushed.
 */
GST_START_TEST (test_take1)
{
  GstAdapter *adapter;
  GstBuffer *buffer, *buffer2;
  guint avail;
  GstMapInfo info, info2;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* take out buffer */
  buffer2 = gst_adapter_take_buffer (adapter, 100);
  fail_unless (buffer2 != NULL);

  fail_unless (gst_buffer_map (buffer2, &info2, GST_MAP_READ));
  fail_unless (info2.data != NULL);
  fail_unless (info2.size == 100);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);

  /* the buffer should be the same */
  fail_unless (buffer == buffer2);
  fail_unless (info.data == info2.data);

  gst_buffer_unmap (buffer, &info);
  gst_buffer_unmap (buffer2, &info2);

  gst_buffer_unref (buffer2);

  g_object_unref (adapter);
}

GST_END_TEST;

/* take data from an adapter with 2 non-mergeable buffers 
 * pushed.
 */
GST_START_TEST (test_take2)
{
}

GST_END_TEST;

/* take data from an adapter with 2 mergeable buffers 
 * pushed.
 */
GST_START_TEST (test_take3)
{
  GstAdapter *adapter;
  GstBuffer *buffer, *buffer2;
  guint avail;
  GstMapInfo info, info2;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  fail_unless (info.data != NULL);
  fail_unless (info.size == 100);
  gst_buffer_unmap (buffer, &info);

  /* set up and push subbuffers */
  buffer2 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0, 25);
  gst_adapter_push (adapter, buffer2);
  buffer2 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 25, 25);
  gst_adapter_push (adapter, buffer2);
  buffer2 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 50, 25);
  gst_adapter_push (adapter, buffer2);
  buffer2 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 75, 25);
  gst_adapter_push (adapter, buffer2);

  gst_buffer_unref (buffer);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* take out buffer */
  buffer2 = gst_adapter_take_buffer (adapter, 100);
  fail_unless (buffer2 != NULL);
  fail_unless (gst_buffer_map (buffer2, &info2, GST_MAP_READ));
  fail_unless (info2.data != NULL);
  fail_unless (info2.size == 100);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);

#if 0
  /* the data should be the same FIXME, implement span in adapter again. */
  fail_unless (info.data == info2.data);
#endif

  gst_buffer_unmap (buffer2, &info2);
  gst_buffer_unref (buffer2);

  g_object_unref (adapter);
}

GST_END_TEST;

static GstAdapter *
create_and_fill_adapter (void)
{
  GstAdapter *adapter;
  gint i, j;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  for (i = 0; i < 10000; i += 4) {
    GstBuffer *buf;
    GstMapInfo info;
    guint8 *ptr;

    buf = gst_buffer_new_and_alloc (sizeof (guint32) * 4);
    fail_unless (buf != NULL);

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
    ptr = info.data;

    for (j = 0; j < 4; j++) {
      GST_WRITE_UINT32_LE (ptr, i + j);
      ptr += sizeof (guint32);
    }
    gst_buffer_unmap (buf, &info);

    gst_adapter_push (adapter, buf);
  }

  return adapter;
}

/* Fill a buffer with a sequence of 32 bit ints and read them back out,
 * checking that they're still in the right order */
GST_START_TEST (test_take_order)
{
  GstAdapter *adapter;
  int i = 0;

  adapter = create_and_fill_adapter ();
  while (gst_adapter_available (adapter) >= sizeof (guint32)) {
    guint8 *data = gst_adapter_take (adapter, sizeof (guint32));
    guint32 val = GST_READ_UINT32_LE (data);

    GST_DEBUG ("val %8u", val);
    fail_unless (val == i);
    i++;
    g_free (data);
  }
  fail_unless (gst_adapter_available (adapter) == 0,
      "Data was left in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

/* Fill a buffer with a sequence of 32 bit ints and read them back out
 * using take_buffer, checking that they're still in the right order */
GST_START_TEST (test_take_buf_order)
{
  GstAdapter *adapter;
  int i = 0;

  adapter = create_and_fill_adapter ();
  while (gst_adapter_available (adapter) >= sizeof (guint32)) {
    GstBuffer *buf = gst_adapter_take_buffer (adapter, sizeof (guint32));
    GstMapInfo info;

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));
    fail_unless (GST_READ_UINT32_LE (info.data) == i);
    gst_buffer_unmap (buf, &info);

    i++;

    gst_buffer_unref (buf);
  }
  fail_unless (gst_adapter_available (adapter) == 0,
      "Data was left in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

GST_START_TEST (test_timestamp)
{
  GstAdapter *adapter;
  GstBuffer *buffer;
  guint avail;
  GstClockTime timestamp;
  guint64 dist;
  guint8 *data;
  const guint8 *cdata;
  guint64 offset;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* timestamp is now undefined */
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 0);

  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* Offset should be undefined */
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless (offset == GST_BUFFER_OFFSET_NONE);
  fail_unless (dist == 0);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless (offset == GST_CLOCK_TIME_NONE);

  /* 0 bytes since discont */
  dist = gst_adapter_distance_from_discont (adapter);
  fail_unless (dist == 0);

  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  /* still undefined, dist changed, though */
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 50);

  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* Offset still undefined */
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless (offset == GST_BUFFER_OFFSET_NONE);
  fail_unless (dist == 50);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless (offset == GST_CLOCK_TIME_NONE);

  /* 50 bytes since discont */
  dist = gst_adapter_distance_from_discont (adapter);
  fail_unless (dist == 50);

  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);

  /* timestamp is still undefined */
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 50);

  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* Offset still undefined */
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless (offset == GST_BUFFER_OFFSET_NONE);
  fail_unless (dist == 50);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless (offset == GST_CLOCK_TIME_NONE);

  /* 50 bytes since discont */
  dist = gst_adapter_distance_from_discont (adapter);
  fail_unless (dist == 50);

  /* flush out first buffer we are now at the second buffer timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 0);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* 100 bytes since discont */
  dist = gst_adapter_distance_from_discont (adapter);
  fail_unless (dist == 100);

  /* move some more, still the same timestamp but further away */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 50);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* push a buffer without timestamp in the adapter */
  buffer = gst_buffer_new_and_alloc (100);
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);
  /* push a buffer with timestamp in the adapter */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 250);

  /* timestamp still as it was before the push */
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 50);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* flush away buffer with the timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 200);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 100);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* move into the second buffer */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 150);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* move to third buffer we move to the new timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 0);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* move everything out */
  gst_adapter_flush (adapter, 100);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 100);

  /* clear everything */
  gst_adapter_clear (adapter);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 0);

  /* timestamp was undefined at discont */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* push an empty buffer with timestamp in the adapter */
  buffer = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 0);

  /* timestamp was undefined at discont (clearing the adapter is not
   * necessarily a discont) */
  timestamp = gst_adapter_pts_at_discont (adapter);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);

  /* push another empty buffer */
  buffer = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 0);

  /* push a buffer with timestamp in the adapter */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 4 * GST_SECOND;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 0);

  gst_adapter_flush (adapter, 1);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 99);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 4 * GST_SECOND);
  fail_unless (dist == 1);

  /* push an empty buffer with timestamp in the adapter */
  buffer = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 99);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 4 * GST_SECOND);
  fail_unless (dist == 1);

  /* push buffer without timestamp */
  buffer = gst_buffer_new_and_alloc (100);
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 199);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 4 * GST_SECOND);
  fail_unless (dist == 1);

  /* remove first buffer, timestamp of empty buffer is visible */
  buffer = gst_adapter_take_buffer (adapter, 99);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 99);
  gst_buffer_unref (buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 5 * GST_SECOND);
  fail_unless (dist == 0);

  /* remove empty buffer, timestamp still visible */
  cdata = gst_adapter_map (adapter, 50);
  fail_unless (cdata != NULL);
  gst_adapter_unmap (adapter);

  data = gst_adapter_take (adapter, 50);
  fail_unless (data != NULL);
  g_free (data);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);
  timestamp = gst_adapter_prev_pts (adapter, &dist);
  fail_unless (timestamp == 5 * GST_SECOND);
  fail_unless (dist == 50);

  /* clearing to start a new test */
  gst_adapter_clear (adapter);

  buffer = gst_buffer_new_and_alloc (100);

  /* push the buffer without timestamp in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 100);

  /* timestamp is now undefined */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 0, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (dist, 0);

  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 50);

  /* still undefined, dist changed, though */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 0, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (dist, 50);

  /* alloc first buffer with timestamp */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 150);

  /* timestamp is still undefined at offset 0 */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 0, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (dist, 50);

  /* timestamp is still undefined at offset 50 */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 50, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (dist, 100);

  /* timestamp is 1 second at offset 51 */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 51, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless_equals_int (dist, 1);

  /* timestamp is still 1 second at offset 100 */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 100, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless_equals_int (dist, 50);

  /* flush out the last 50 bytes of the buffer with undefined timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 100);

  /* alloc second buffer with timestamp */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 200);

  /* alloc third buffer with timestamp */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 300);

  /* alloc fourth buffer with timestamp */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 4 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 400);

  timestamp = gst_adapter_prev_pts_at_offset (adapter, 0, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless_equals_int (dist, 0);

  timestamp = gst_adapter_prev_pts_at_offset (adapter, 100, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless_equals_int (dist, 100);

  timestamp = gst_adapter_prev_pts_at_offset (adapter, 200, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless_equals_int (dist, 100);

  timestamp = gst_adapter_prev_pts_at_offset (adapter, 300, &dist);
  fail_unless (timestamp == 3 * GST_SECOND);
  fail_unless_equals_int (dist, 100);

  timestamp = gst_adapter_prev_pts_at_offset (adapter, 400, &dist);
  fail_unless (timestamp == 4 * GST_SECOND);
  fail_unless_equals_int (dist, 100);

  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless_equals_int (avail, 350);

  /* We flushed 50 bytes, we are still on the first buffer */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 0, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless_equals_int (dist, 50);

  /* As we flushed 50 bytes, offset 100 is on the second buffer */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 100, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless_equals_int (dist, 50);

  /* Third buffer */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 200, &dist);
  fail_unless (timestamp == 3 * GST_SECOND);
  fail_unless_equals_int (dist, 50);

  /* Fourth buffer */
  timestamp = gst_adapter_prev_pts_at_offset (adapter, 300, &dist);
  fail_unless (timestamp == 4 * GST_SECOND);
  fail_unless_equals_int (dist, 50);

  g_object_unref (adapter);
}

GST_END_TEST;

GST_START_TEST (test_offset)
{
  GstAdapter *adapter;
  GstBuffer *buffer;
  guint avail;
  guint64 offset;
  guint64 dist;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* Offset should be undefined */
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 0);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);
  fail_unless_equals_int (dist, 0);

  /* Offset is undefined */
  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);

  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  /* Offset still undefined, dist changed though */
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 50);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);
  fail_unless_equals_int (dist, 50);

  /* Offset is undefined */
  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);

  /* Let's push in a discont buffer with a valid offset */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_OFFSET (buffer) = 10000;
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);

  /* offset is still undefined */
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 50);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);
  fail_unless_equals_int (dist, 50);

  /* Offset is undefined */
  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);

  /* flush out first buffer we are now at the second buffer offset */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 0);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 10000);
  fail_unless_equals_int (dist, 0);

  /* Offset should be defined now */
  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* move some more, we should have an updated offset */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 50);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 10000);
  fail_unless_equals_int (dist, 50);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* push a buffer without offset in the adapter (contiguous with the
     other) */
  buffer = gst_buffer_new_and_alloc (100);
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);

  /* push a buffer with offset in the adapter (but contiguous with the
     other), the offset shouldn't be taken into account */
  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_OFFSET (buffer) = 50000;
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 250);

  /* offset still as it was before the push */
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 50);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 10000);
  fail_unless_equals_int (dist, 50);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* flush away buffer with the offset */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 200);
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 100);
  /* The previous valid offset seen is now 100 bytes away */
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 10000);
  fail_unless_equals_int (dist, 100);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* move into the second buffer */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 150);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 10000);
  fail_unless_equals_int (dist, 150);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* move to third buffer, we should still see a continuously increasing
   * offset and ignore the non-discont offset */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 200);
  /* But the prev_offset *does* give us the actual buffer offset value */
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 50000);
  fail_unless_equals_int (dist, 0);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* move everything out, we end up at the last offset */
  gst_adapter_flush (adapter, 100);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 300);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, 50000);
  fail_unless_equals_int (dist, 100);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, 10000);

  /* clear everything */
  gst_adapter_clear (adapter);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  offset = gst_adapter_distance_from_discont (adapter);
  fail_unless_equals_uint64 (offset, 0);
  offset = gst_adapter_prev_offset (adapter, &dist);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);
  fail_unless_equals_int (dist, 0);

  offset = gst_adapter_offset_at_discont (adapter);
  fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET_NONE);

  g_object_unref (adapter);
}

GST_END_TEST;

GST_START_TEST (test_scan)
{
  GstAdapter *adapter;
  GstBuffer *buffer;
  GstMapInfo info;
  guint offset;
  guint i;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  /* fill with pattern */
  for (i = 0; i < 100; i++)
    ((guint8 *) info.data)[i] = i;
  gst_buffer_unmap (buffer, &info);

  gst_adapter_push (adapter, buffer);

  /* find first bytes */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x00010203, 0, 100);
  fail_unless (offset == 0);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x01020304, 0, 100);
  fail_unless (offset == 1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x01020304, 1, 99);
  fail_unless (offset == 1);
  /* offset is past the pattern start */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x01020304, 2, 98);
  fail_unless (offset == -1);
  /* not enough bytes to find the pattern */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x02030405, 2, 3);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x02030405, 2, 4);
  fail_unless (offset == 2);
  /* size does not include the last scanned byte */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x40414243, 0, 0x41);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x40414243, 0, 0x43);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x40414243, 0, 0x44);
  fail_unless (offset == 0x40);
  /* past the start */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x40414243, 65, 10);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x40414243, 64, 5);
  fail_unless (offset == 64);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x60616263, 65, 35);
  fail_unless (offset == 0x60);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x60616263, 0x60, 4);
  fail_unless (offset == 0x60);
  /* past the start */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x60616263, 0x61, 3);
  fail_unless (offset == -1);

  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x60616263, 99, 1);
  fail_unless (offset == -1);

  /* add another buffer */
  buffer = gst_buffer_new_and_alloc (100);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  /* fill with pattern */
  for (i = 0; i < 100; i++)
    ((guint8 *) info.data)[i] = i + 100;
  gst_buffer_unmap (buffer, &info);

  gst_adapter_push (adapter, buffer);

  /* past the start */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x60616263, 0x61, 6);
  fail_unless (offset == -1);
  /* this should work */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x61626364, 0x61, 4);
  fail_unless (offset == 0x61);
  /* not enough data */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x62636465, 0x61, 4);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x62636465, 0x61, 5);
  fail_unless (offset == 0x62);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x62636465, 0, 120);
  fail_unless (offset == 0x62);

  /* border conditions */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x62636465, 0, 200);
  fail_unless (offset == 0x62);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x63646566, 0, 200);
  fail_unless (offset == 0x63);
  /* we completely searched the first list */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x64656667, 0, 200);
  fail_unless (offset == 0x64);
  /* skip first buffer */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x64656667, 0x64,
      100);
  fail_unless (offset == 0x64);
  /* past the start */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x64656667, 0x65,
      10);
  fail_unless (offset == -1);
  /* not enough data to scan */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x64656667, 0x63, 4);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x64656667, 0x63, 5);
  fail_unless (offset == 0x64);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0, 199);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0x62,
      102);
  fail_unless (offset == 0xc4);
  /* different masks */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0x00ffffff, 0x00656667, 0x64,
      100);
  fail_unless (offset == 0x64);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0x000000ff, 0x00000000, 0, 100);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0x000000ff, 0x00000003, 0, 100);
  fail_unless (offset == 0);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0x000000ff, 0x00000061, 0x61,
      100);
  fail_unless (offset == -1);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xff000000, 0x61000000, 0, 0x62);
  fail_unless (offset == -1);
  /* does not even exist */
  ASSERT_CRITICAL (offset =
      gst_adapter_masked_scan_uint32 (adapter, 0x00ffffff, 0xffffffff, 0x65,
          99));
  fail_unless (offset == -1);

  /* flush some bytes */
  gst_adapter_flush (adapter, 0x20);

  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x20212223, 0, 100);
  fail_unless (offset == 0);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0x20212223, 0, 4);
  fail_unless (offset == 0);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0x62,
      70);
  fail_unless (offset == 0xa4);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0, 168);
  fail_unless (offset == 0xa4);

  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 164, 4);
  fail_unless (offset == 0xa4);
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0x44,
      100);
  fail_unless (offset == 0xa4);
  /* not enough bytes */
  offset =
      gst_adapter_masked_scan_uint32 (adapter, 0xffffffff, 0xc4c5c6c7, 0x44,
      99);
  fail_unless (offset == -1);

  g_object_unref (adapter);
}

GST_END_TEST;

/* Fill a buffer with a sequence of 32 bit ints and read them back out
 * using take_buffer, checking that they're still in the right order */
GST_START_TEST (test_take_list)
{
  GstAdapter *adapter;
  int i = 0;

  adapter = create_and_fill_adapter ();
  while (gst_adapter_available (adapter) >= sizeof (guint32)) {
    GList *list, *walk;
    GstBuffer *buf;
    gsize left;
    GstMapInfo info;
    guint8 *ptr;

    list = gst_adapter_take_list (adapter, sizeof (guint32) * 5);
    fail_unless (list != NULL);

    for (walk = list; walk; walk = g_list_next (walk)) {
      buf = walk->data;

      fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));

      ptr = info.data;
      left = info.size;

      while (left > 0) {
        fail_unless (GST_READ_UINT32_LE (ptr) == i);
        i++;
        ptr += sizeof (guint32);
        left -= sizeof (guint32);
      }
      gst_buffer_unmap (buf, &info);

      gst_buffer_unref (buf);
    }
    g_list_free (list);
  }
  fail_unless (gst_adapter_available (adapter) == 0,
      "Data was left in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

/* Fill a buffer with a sequence of 32 bit ints and read a couple of front
 * buffers using get_buffer, checking that they're still in the right order */
GST_START_TEST (test_get_list)
{
  GstAdapter *adapter;
  int i = 0;
  gsize size = 0;
  GList *list, *walk;
  GstBuffer *buf;
  gsize left;
  GstMapInfo info;
  guint8 *ptr;

  adapter = create_and_fill_adapter ();
  size = gst_adapter_available (adapter);

  list = gst_adapter_get_list (adapter, sizeof (guint32) * 5);
  fail_unless (list != NULL);

  for (walk = list; walk; walk = g_list_next (walk)) {
    buf = walk->data;

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));

    ptr = info.data;
    left = info.size;

    while (left > 0) {
      fail_unless (GST_READ_UINT32_LE (ptr) == i);
      i++;
      ptr += sizeof (guint32);
      left -= sizeof (guint32);
    }
    gst_buffer_unmap (buf, &info);

    gst_buffer_unref (buf);
  }
  g_list_free (list);

  fail_unless (gst_adapter_available (adapter) == size,
      "All data should remain in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

/* Fill a buffer with a sequence of 32 bit ints and read them back out
 * using take_buffer, checking that they're still in the right order */
GST_START_TEST (test_take_buffer_list)
{
  GstAdapter *adapter;
  int i = 0;

  adapter = create_and_fill_adapter ();
  while (gst_adapter_available (adapter) >= sizeof (guint32)) {
    GstBufferList *buffer_list;
    GstBuffer *buf;
    gsize left;
    GstMapInfo info;
    guint8 *ptr;
    guint n, num;

    buffer_list = gst_adapter_take_buffer_list (adapter, sizeof (guint32) * 5);
    fail_unless (buffer_list != NULL);
    num = gst_buffer_list_length (buffer_list);
    fail_unless (num > 0);

    for (n = 0; n < num; ++n) {
      buf = gst_buffer_list_get (buffer_list, n);

      fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));

      ptr = info.data;
      left = info.size;

      while (left > 0) {
        fail_unless (GST_READ_UINT32_LE (ptr) == i);
        i++;
        ptr += sizeof (guint32);
        left -= sizeof (guint32);
      }
      gst_buffer_unmap (buf, &info);
    }
    gst_buffer_list_unref (buffer_list);
  }
  fail_unless (gst_adapter_available (adapter) == 0,
      "Data was left in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

/* Fill a buffer with a sequence of 32 bit ints and read a couple of front
 * buffers using get_buffer, checking that they're still in the right order */
GST_START_TEST (test_get_buffer_list)
{
  GstAdapter *adapter;
  int i = 0;
  gsize size = 0;
  GstBufferList *buffer_list;
  GstBuffer *buf;
  gsize left;
  GstMapInfo info;
  guint8 *ptr;
  guint n, num;

  adapter = create_and_fill_adapter ();
  size = gst_adapter_available (adapter);

  buffer_list = gst_adapter_get_buffer_list (adapter, sizeof (guint32) * 5);
  fail_unless (buffer_list != NULL);

  num = gst_buffer_list_length (buffer_list);
  fail_unless (num > 0);

  for (n = 0; n < num; ++n) {
    buf = gst_buffer_list_get (buffer_list, n);

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));

    ptr = info.data;
    left = info.size;

    while (left > 0) {
      fail_unless (GST_READ_UINT32_LE (ptr) == i);
      i++;
      ptr += sizeof (guint32);
      left -= sizeof (guint32);
    }
    gst_buffer_unmap (buf, &info);
  }
  gst_buffer_list_unref (buffer_list);

  fail_unless (gst_adapter_available (adapter) == size,
      "All data should remain in the adapter");

  g_object_unref (adapter);
}

GST_END_TEST;

GST_START_TEST (test_merge)
{
  GstAdapter *adapter;
  GstBuffer *buffer;
  gint i;

  adapter = gst_adapter_new ();
  fail_if (adapter == NULL);

  buffer = gst_buffer_new_and_alloc (10);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  for (i = 0; i < 1000; i++) {
    buffer = gst_buffer_new_and_alloc (10);
    gst_adapter_push (adapter, buffer);

    fail_unless (gst_adapter_map (adapter, 20) != NULL);
    gst_adapter_unmap (adapter);

    gst_adapter_flush (adapter, 10);
  }
  g_object_unref (adapter);
}

GST_END_TEST;

GST_START_TEST (test_take_buffer_fast)
{
  GstAdapter *adapter;
  GstBuffer *buffer;

  adapter = gst_adapter_new ();
  fail_if (adapter == NULL);

  buffer = gst_buffer_new_and_alloc (5);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  buffer = gst_buffer_new_and_alloc (10);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  buffer = gst_buffer_new_and_alloc (15);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  fail_unless (gst_adapter_available (adapter) == 30);

  buffer = gst_adapter_take_buffer_fast (adapter, 30);
  fail_unless (gst_adapter_available (adapter) == 0);
  fail_unless (gst_buffer_n_memory (buffer) == 3);
  fail_unless (gst_buffer_get_sizes_range (buffer, 0, 1, NULL, NULL) == 5);
  fail_unless (gst_buffer_get_sizes_range (buffer, 1, 1, NULL, NULL) == 10);
  fail_unless (gst_buffer_get_sizes_range (buffer, 2, 1, NULL, NULL) == 15);
  gst_buffer_unref (buffer);

  fail_unless (gst_adapter_available (adapter) == 0);

  buffer = gst_buffer_new_and_alloc (10);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  buffer = gst_buffer_new_and_alloc (20);
  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  gst_adapter_flush (adapter, 2);
  fail_unless (gst_adapter_available (adapter) == 28);

  buffer = gst_adapter_take_buffer_fast (adapter, 10);
  fail_unless (gst_buffer_n_memory (buffer) == 2);
  fail_unless (gst_buffer_get_sizes_range (buffer, 0, 1, NULL, NULL) == 8);
  fail_unless (gst_buffer_get_sizes_range (buffer, 1, 1, NULL, NULL) == 2);
  fail_unless (gst_adapter_available (adapter) == 18);
  gst_buffer_unref (buffer);

  buffer = gst_adapter_take_buffer_fast (adapter, 8);
  fail_unless (gst_buffer_n_memory (buffer) == 1);
  fail_unless (gst_buffer_get_size (buffer) == 8);
  fail_unless (gst_adapter_available (adapter) == 10);
  gst_buffer_unref (buffer);

  gst_adapter_clear (adapter);
  fail_unless (gst_adapter_available (adapter) == 0);

  g_object_unref (adapter);
}

GST_END_TEST;

static Suite *
gst_adapter_suite (void)
{
  Suite *s = suite_create ("adapter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_peek1);
  tcase_add_test (tc_chain, test_peek2);
  tcase_add_test (tc_chain, test_peek3);
  tcase_add_test (tc_chain, test_take1);
  tcase_add_test (tc_chain, test_take2);
  tcase_add_test (tc_chain, test_take3);
  tcase_add_test (tc_chain, test_take_order);
  tcase_add_test (tc_chain, test_take_buf_order);
  tcase_add_test (tc_chain, test_timestamp);
  tcase_add_test (tc_chain, test_scan);
  tcase_add_test (tc_chain, test_take_list);
  tcase_add_test (tc_chain, test_get_list);
  tcase_add_test (tc_chain, test_take_buffer_list);
  tcase_add_test (tc_chain, test_get_buffer_list);
  tcase_add_test (tc_chain, test_merge);
  tcase_add_test (tc_chain, test_take_buffer_fast);
  tcase_add_test (tc_chain, test_offset);

  return s;
}

GST_CHECK_MAIN (gst_adapter);
