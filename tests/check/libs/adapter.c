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

Suite *
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

  return s;
}

GST_CHECK_MAIN (gst_adapter);
