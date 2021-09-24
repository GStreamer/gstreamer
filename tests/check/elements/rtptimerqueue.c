/* GStreamer
 *
 * Copyright (C) 2019 Net Insight AB
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
#include "gst/rtpmanager/rtptimerqueue.h"

GST_START_TEST (test_timer_queue_set_timer)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer10, *timer0;

  rtp_timer_queue_set_timer (queue, RTP_TIMER_EXPECTED, 10,
      1 * GST_SECOND, 2 * GST_SECOND, 5 * GST_SECOND, 0);
  timer10 = rtp_timer_queue_find (queue, 10);
  fail_unless (timer10);
  fail_unless_equals_int (10, timer10->seqnum);
  fail_unless_equals_int (RTP_TIMER_EXPECTED, timer10->type);
  /* timer10->timeout = timerout + delay */
  fail_unless_equals_uint64 (3 * GST_SECOND, timer10->timeout);
  fail_unless_equals_uint64 (5 * GST_SECOND, timer10->duration);
  fail_unless_equals_uint64 (1 * GST_SECOND, timer10->rtx_base);
  fail_unless_equals_uint64 (GST_CLOCK_TIME_NONE, timer10->rtx_last);
  fail_unless_equals_int (0, timer10->num_rtx_retry);
  fail_unless_equals_int (0, timer10->num_rtx_received);

  rtp_timer_queue_set_timer (queue, RTP_TIMER_LOST, 0,
      0 * GST_SECOND, 2 * GST_SECOND, 0, 0);
  timer0 = rtp_timer_queue_find (queue, 0);
  fail_unless (timer0);
  fail_unless_equals_int (0, timer0->seqnum);
  fail_unless_equals_int (RTP_TIMER_LOST, timer0->type);
  fail_unless_equals_uint64 (2 * GST_SECOND, timer0->timeout);
  fail_unless_equals_uint64 (0, timer0->duration);
  fail_unless_equals_uint64 (0, timer0->rtx_base);
  fail_unless_equals_uint64 (GST_CLOCK_TIME_NONE, timer0->rtx_last);
  fail_unless_equals_int (0, timer0->num_rtx_retry);
  fail_unless_equals_int (0, timer0->num_rtx_received);

  /* also check order while at it */
  fail_unless (timer10->list.next == NULL);
  fail_unless (timer10->list.prev == (GList *) timer0);
  fail_unless (timer0->list.next == (GList *) timer10);
  fail_unless (timer0->list.prev == NULL);

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_insert_head)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer, *next, *prev;

  rtp_timer_queue_set_deadline (queue, 1, -1, 0);
  rtp_timer_queue_set_deadline (queue, 3, -1, 0);
  rtp_timer_queue_set_deadline (queue, 2, -1, 0);
  rtp_timer_queue_set_deadline (queue, 0, -1, 0);

  timer = rtp_timer_queue_find (queue, 0);
  fail_if (timer == NULL);
  fail_unless_equals_int (0, timer->seqnum);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_unless (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, next->seqnum);

  timer = rtp_timer_queue_find (queue, 3);
  fail_if (timer == NULL);
  fail_unless_equals_int (3, timer->seqnum);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_unless_equals_int (2, prev->seqnum);
  fail_unless (next == NULL);

  timer = rtp_timer_queue_find (queue, 2);
  fail_if (timer == NULL);
  fail_unless_equals_int (2, timer->seqnum);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, prev->seqnum);
  fail_unless_equals_int (3, next->seqnum);

  timer = rtp_timer_queue_find (queue, 1);
  fail_if (timer == NULL);
  fail_unless_equals_int (1, timer->seqnum);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (0, prev->seqnum);
  fail_unless_equals_int (2, next->seqnum);

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_reschedule)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer, *next, *prev;

  rtp_timer_queue_set_deadline (queue, 3, 1 * GST_SECOND, 0);
  rtp_timer_queue_set_deadline (queue, 1, 2 * GST_SECOND, 0);
  rtp_timer_queue_set_deadline (queue, 2, 3 * GST_SECOND, 0);
  rtp_timer_queue_set_deadline (queue, 0, 4 * GST_SECOND, 0);

  timer = rtp_timer_queue_find (queue, 1);
  fail_if (timer == NULL);

  /* move to head, making sure seqnum order is respected */
  rtp_timer_queue_set_deadline (queue, 1, 1 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_unless (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (3, next->seqnum);

  /* move head back */
  rtp_timer_queue_set_deadline (queue, 1, 2 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (3, prev->seqnum);
  fail_unless_equals_int (2, next->seqnum);

  /* move to tail */
  timer = rtp_timer_queue_find (queue, 2);
  fail_if (timer == NULL);
  rtp_timer_queue_set_deadline (queue, 2, 4 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_unless (next == NULL);
  fail_unless_equals_int (0, prev->seqnum);

  /* move tail back */
  rtp_timer_queue_set_deadline (queue, 2, 3 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, prev->seqnum);
  fail_unless_equals_int (0, next->seqnum);

  /* not moving toward head */
  rtp_timer_queue_set_deadline (queue, 2, 2 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, prev->seqnum);
  fail_unless_equals_int (0, next->seqnum);

  /* not moving toward tail */
  rtp_timer_queue_set_deadline (queue, 2, 3 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, prev->seqnum);
  fail_unless_equals_int (0, next->seqnum);

  /* inner move toward head */
  rtp_timer_queue_set_deadline (queue, 2, GST_SECOND + GST_SECOND / 2, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (3, prev->seqnum);
  fail_unless_equals_int (1, next->seqnum);

  /* inner move toward tail */
  rtp_timer_queue_set_deadline (queue, 2, 3 * GST_SECOND, 0);
  next = (RtpTimer *) timer->list.next;
  prev = (RtpTimer *) timer->list.prev;
  fail_if (prev == NULL);
  fail_if (next == NULL);
  fail_unless_equals_int (1, prev->seqnum);
  fail_unless_equals_int (0, next->seqnum);

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_pop_until)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer;

  rtp_timer_queue_set_deadline (queue, 2, 2 * GST_SECOND, 0);
  rtp_timer_queue_set_deadline (queue, 1, 1 * GST_SECOND, 0);
  rtp_timer_queue_set_deadline (queue, 0, -1, 0);

  timer = rtp_timer_queue_pop_until (queue, 1 * GST_SECOND);
  fail_if (timer == NULL);
  fail_unless_equals_int (0, timer->seqnum);
  rtp_timer_free (timer);

  timer = rtp_timer_queue_pop_until (queue, 1 * GST_SECOND);
  fail_if (timer == NULL);
  fail_unless_equals_int (1, timer->seqnum);
  rtp_timer_free (timer);

  timer = rtp_timer_queue_pop_until (queue, 1 * GST_SECOND);
  fail_unless (timer == NULL);

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_update_timer_seqnum)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer;

  rtp_timer_queue_set_deadline (queue, 2, 2 * GST_SECOND, 0);

  timer = rtp_timer_queue_find (queue, 2);
  fail_if (timer == NULL);

  rtp_timer_queue_update_timer (queue, timer, 3, 3 * GST_SECOND, 0, 0, FALSE);

  timer = rtp_timer_queue_find (queue, 2);
  fail_unless (timer == NULL);
  timer = rtp_timer_queue_find (queue, 3);
  fail_if (timer == NULL);

  fail_unless_equals_int (1, rtp_timer_queue_length (queue));

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_dup_timer)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer;

  rtp_timer_queue_set_deadline (queue, 2, 2 * GST_SECOND, 0);

  timer = rtp_timer_queue_find (queue, 2);
  fail_if (timer == NULL);

  timer = rtp_timer_dup (timer);
  timer->seqnum = 3;
  rtp_timer_queue_insert (queue, timer);

  fail_unless_equals_int (2, rtp_timer_queue_length (queue));

  g_object_unref (queue);
}

GST_END_TEST;

GST_START_TEST (test_timer_queue_timer_offset)
{
  RtpTimerQueue *queue = rtp_timer_queue_new ();
  RtpTimer *timer;

  rtp_timer_queue_set_timer (queue, RTP_TIMER_EXPECTED, 2, 2 * GST_SECOND,
      GST_MSECOND, 0, GST_USECOND);

  timer = rtp_timer_queue_find (queue, 2);
  fail_if (timer == NULL);
  fail_unless_equals_uint64 (2 * GST_SECOND + GST_MSECOND + GST_USECOND,
      timer->timeout);
  fail_unless_equals_int64 (GST_USECOND, timer->offset);

  rtp_timer_queue_update_timer (queue, timer, 2, 3 * GST_SECOND,
      2 * GST_MSECOND, 2 * GST_USECOND, FALSE);
  fail_unless_equals_uint64 (3 * GST_SECOND + 2 * GST_MSECOND +
      2 * GST_USECOND, timer->timeout);
  fail_unless_equals_int64 (2 * GST_USECOND, timer->offset);

  g_object_unref (queue);
}

GST_END_TEST;

static Suite *
rtptimerqueue_suite (void)
{
  Suite *s = suite_create ("rtptimerqueue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_timer_queue_set_timer);
  tcase_add_test (tc_chain, test_timer_queue_insert_head);
  tcase_add_test (tc_chain, test_timer_queue_reschedule);
  tcase_add_test (tc_chain, test_timer_queue_pop_until);
  tcase_add_test (tc_chain, test_timer_queue_update_timer_seqnum);
  tcase_add_test (tc_chain, test_timer_queue_dup_timer);
  tcase_add_test (tc_chain, test_timer_queue_timer_offset);

  return s;
}

GST_CHECK_MAIN (rtptimerqueue);
