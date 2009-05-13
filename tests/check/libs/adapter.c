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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

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
  const guint8 *bufdata, *data1, *data2;

  adapter = gst_adapter_new ();
  fail_if (adapter == NULL);

  /* push single buffer in adapter */
  buffer = gst_buffer_new_and_alloc (512);
  bufdata = GST_BUFFER_DATA (buffer);

  fail_if (buffer == NULL);
  gst_adapter_push (adapter, buffer);

  /* available and available_fast should return the size of the 
   * buffer */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 512);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 512);

  /* should g_critical with NULL as result */
  ASSERT_CRITICAL (data1 = gst_adapter_peek (adapter, 0));
  fail_if (data1 != NULL);

  /* should return NULL as result */
  data1 = gst_adapter_peek (adapter, 513);
  fail_if (data1 != NULL);

  /* this should work */
  data1 = gst_adapter_peek (adapter, 512);
  fail_if (data1 == NULL);
  /* it should point to the buffer data as well */
  fail_if (data1 != bufdata);
  data2 = gst_adapter_peek (adapter, 512);
  fail_if (data2 == NULL);
  /* second peek should return the same pointer */
  fail_if (data2 != data1);

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
  data2 = gst_adapter_peek (adapter, 503);
  fail_if (data2 != NULL);

  /* should work fine */
  data2 = gst_adapter_peek (adapter, 502);
  fail_if (data2 == NULL);
  /* peek should return the same old pointer + 10 */
  fail_if (data2 != data1 + 10);
  fail_if (data2 != bufdata + 10);

  /* flush some more */
  gst_adapter_flush (adapter, 500);

  /* see if we have 2 bytes available */
  avail = gst_adapter_available (adapter);
  fail_if (avail != 2);
  avail = gst_adapter_available_fast (adapter);
  fail_if (avail != 2);

  data2 = gst_adapter_peek (adapter, 2);
  fail_if (data2 == NULL);
  fail_if (data2 != data1 + 510);
  fail_if (data2 != bufdata + 510);

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
  guint8 *data, *data2;

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_DATA (buffer) != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 100);

  data = GST_BUFFER_DATA (buffer);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* take out buffer */
  buffer2 = gst_adapter_take_buffer (adapter, 100);
  fail_unless (buffer2 != NULL);
  fail_unless (GST_BUFFER_DATA (buffer2) != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer2) == 100);
  data2 = GST_BUFFER_DATA (buffer2);

  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);

  /* the buffer should be the same */
  fail_unless (buffer == buffer2);
  fail_unless (data == data2);

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
    GstBuffer *buf = gst_buffer_new_and_alloc (sizeof (guint32) * 4);
    guint8 *data;

    fail_unless (buf != NULL);
    data = GST_BUFFER_DATA (buf);

    for (j = 0; j < 4; j++) {
      GST_WRITE_UINT32_LE (data, i + j);
      data += sizeof (guint32);
    }
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

    fail_unless (GST_READ_UINT32_LE (data) == i);
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

    fail_unless (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf)) == i);
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

  adapter = gst_adapter_new ();
  fail_unless (adapter != NULL);

  buffer = gst_buffer_new_and_alloc (100);

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  /* timestamp is now undefined */
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 0);

  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  /* still undefined, dist changed, though */
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 50);

  buffer = gst_buffer_new_and_alloc (100);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;

  /* push in the adapter */
  gst_adapter_push (adapter, buffer);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);

  /* timestamp is still undefined */
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 50);

  /* flush out first buffer we are now at the second buffer timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);

  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 0);

  /* move some more, still the same timestamp but further away */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 50);

  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 50);

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
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 50);

  /* flush away buffer with the timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 200);
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 100);

  /* move into the second buffer */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 150);
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 1 * GST_SECOND);
  fail_unless (dist == 150);

  /* move to third buffer we move to the new timestamp */
  gst_adapter_flush (adapter, 50);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 100);
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 0);

  /* move everything out */
  gst_adapter_flush (adapter, 100);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == 2 * GST_SECOND);
  fail_unless (dist == 100);

  /* clear everything */
  gst_adapter_clear (adapter);
  avail = gst_adapter_available (adapter);
  fail_unless (avail == 0);
  timestamp = gst_adapter_prev_timestamp (adapter, &dist);
  fail_unless (timestamp == GST_CLOCK_TIME_NONE);
  fail_unless (dist == 0);

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

  return s;
}

GST_CHECK_MAIN (gst_adapter);
