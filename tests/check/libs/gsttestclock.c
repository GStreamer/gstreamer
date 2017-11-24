/*
 * Unit test for a deterministic clock for Gstreamer unit tests
 *
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2012 Sebastian Rasmussen <sebastian.rasmussen@axis.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gsttestclock.h>

typedef struct
{
  GstTestClock *test_clock;
  GstClockID id;
  GstClockTime reference;
} GtuClockWaitContext;

typedef struct
{
  GstClockID clock_id;
  GstClockTimeDiff jitter;
} SyncClockWaitContext;

#define assert_pending_id(pending_id, id, type, time) \
G_STMT_START { \
  GstClockEntry *entry = GST_CLOCK_ENTRY (pending_id); \
  g_assert (entry == (id)); \
  g_assert (GST_CLOCK_ENTRY_TYPE (entry) == (type)); \
  g_assert_cmpuint (GST_CLOCK_ENTRY_TIME (entry), ==, (time)); \
} G_STMT_END

#define assert_processed_id(processed_id, id, type, time) \
G_STMT_START { \
  GstClockEntry *entry = GST_CLOCK_ENTRY (processed_id); \
  g_assert (entry == (id)); \
  g_assert (GST_CLOCK_ENTRY_TYPE (entry) == (type)); \
  g_assert_cmpuint (GST_CLOCK_ENTRY_STATUS (entry), ==, (time)); \
} G_STMT_END

static gpointer test_wait_pending_single_shot_id_sync_worker (gpointer data);
static gpointer test_wait_pending_single_shot_id_async_worker (gpointer data);
static gpointer test_wait_pending_periodic_id_waiter_thread (gpointer data);
static gboolean test_async_wait_cb (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data);

static GtuClockWaitContext *gst_test_util_wait_for_clock_id_begin (GstTestClock
    * clock, GstClockID id, GstClockTimeDiff * jitter);
static GstClockReturn gst_test_util_wait_for_clock_id_end (GtuClockWaitContext *
    wait_ctx);
static gboolean
gst_test_util_clock_wait_context_has_completed (GtuClockWaitContext * wait_ctx);

static gpointer
test_wait_pending_single_shot_id_sync_worker (gpointer data)
{
  SyncClockWaitContext *ctx = data;

  gst_clock_id_wait (ctx->clock_id, &ctx->jitter);

  return NULL;
}

static gpointer
test_wait_pending_single_shot_id_async_worker (gpointer data)
{
  GstClockID clock_id = data;

  g_usleep (G_USEC_PER_SEC / 10);
  gst_clock_id_wait_async (clock_id, test_async_wait_cb, NULL, NULL);

  return NULL;
}

static gpointer
test_wait_pending_periodic_id_waiter_thread (gpointer data)
{
  GstClockID clock_id = data;
  gst_clock_id_wait (clock_id, NULL);
  return NULL;
}

static gboolean
test_async_wait_cb (GstClock * clock,
    GstClockTime time, GstClockID id, gpointer user_data)
{

  gboolean *wait_complete = user_data;

  if (wait_complete != NULL)
    *wait_complete = TRUE;

  return TRUE;
}

static GtuClockWaitContext *
gst_test_util_wait_for_clock_id_begin (GstTestClock * test_clock, GstClockID id,
    GstClockTimeDiff * jitter)
{
  GtuClockWaitContext *wait_ctx;

  wait_ctx = g_slice_new (GtuClockWaitContext);
  wait_ctx->test_clock = gst_object_ref (test_clock);
  wait_ctx->reference = gst_clock_get_time (GST_CLOCK (wait_ctx->test_clock));
  wait_ctx->id = gst_clock_id_ref (id);

  if (jitter) {
    GstClockEntry *entry = GST_CLOCK_ENTRY (wait_ctx->id);
    GstClockTime requested = GST_CLOCK_ENTRY_TIME (entry);
    GstClockTime reference = wait_ctx->reference;

    *jitter = GST_CLOCK_DIFF (requested, reference);
  }

  if (!gst_test_clock_has_id (wait_ctx->test_clock, wait_ctx->id)) {
    GstClockClass *klass = GST_CLOCK_GET_CLASS (wait_ctx->test_clock);
    GstClock *clock = GST_CLOCK (wait_ctx->test_clock);
    g_assert (klass->wait_async (clock, wait_ctx->id) == GST_CLOCK_OK);
  }

  g_assert (gst_test_clock_has_id (wait_ctx->test_clock, wait_ctx->id));
  g_assert_cmpint (gst_test_clock_peek_id_count (wait_ctx->test_clock), >, 0);

  return wait_ctx;
}

static GstClockReturn
gst_test_util_wait_for_clock_id_end (GtuClockWaitContext * wait_ctx)
{
  GstClockReturn status = GST_CLOCK_ERROR;
  GstClockEntry *entry = GST_CLOCK_ENTRY (wait_ctx->id);

  if (G_UNLIKELY (GST_CLOCK_ENTRY_STATUS (entry) == GST_CLOCK_UNSCHEDULED)) {
    status = GST_CLOCK_UNSCHEDULED;
  } else {
    GstClockTime requested = GST_CLOCK_ENTRY_TIME (entry);
    GstClockTimeDiff diff;

    g_assert (gst_test_clock_has_id (wait_ctx->test_clock, wait_ctx->id));

    diff = GST_CLOCK_DIFF (requested, wait_ctx->reference);

    if (diff > 0) {
      status = GST_CLOCK_EARLY;
    } else {
      status = GST_CLOCK_OK;
    }

    g_atomic_int_set (&GST_CLOCK_ENTRY_STATUS (entry), status);
  }

  if (GST_CLOCK_ENTRY_TYPE (entry) == GST_CLOCK_ENTRY_SINGLE) {
    GstClockClass *klass = GST_CLOCK_GET_CLASS (wait_ctx->test_clock);
    GstClock *clock = GST_CLOCK (wait_ctx->test_clock);

    klass->unschedule (clock, wait_ctx->id);
    g_assert (!gst_test_clock_has_id (wait_ctx->test_clock, wait_ctx->id));
  } else {
    GST_CLOCK_ENTRY_TIME (entry) += GST_CLOCK_ENTRY_INTERVAL (entry);
    g_assert (gst_test_clock_has_id (wait_ctx->test_clock, wait_ctx->id));
  }

  gst_clock_id_unref (wait_ctx->id);
  gst_object_unref (wait_ctx->test_clock);
  g_slice_free (GtuClockWaitContext, wait_ctx);

  return status;
}

static gboolean
gst_test_util_clock_wait_context_has_completed (GtuClockWaitContext * wait_ctx)
{
  GstClock *clock = GST_CLOCK (wait_ctx->test_clock);
  GstClockEntry *entry = GST_CLOCK_ENTRY (wait_ctx->id);
  GstClockTime requested = GST_CLOCK_ENTRY_TIME (entry);
  GstClockTime now = gst_clock_get_time (clock);

  return requested < now;
}

GST_START_TEST (test_object_flags)
{
  GstClock *clock = gst_test_clock_new ();
  g_assert (GST_OBJECT_FLAG_IS_SET (clock, GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock, GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock,
          GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock,
          GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC));
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_resolution_query)
{
  GstClock *clock = gst_test_clock_new ();
  g_assert_cmpuint (gst_clock_get_resolution (clock), ==, 1);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_start_time)
{
  GstClock *clock;
  guint64 start_time;

  clock = gst_test_clock_new ();
  g_assert_cmpuint (gst_clock_get_time (clock), ==, 0);
  g_object_get (clock, "start-time", &start_time, NULL);
  g_assert_cmpuint (start_time, ==, 0);
  gst_object_unref (clock);

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  g_object_get (clock, "start-time", &start_time, NULL);
  g_assert_cmpuint (start_time, ==, GST_SECOND);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_set_time)
{
  GstClock *clock = gst_test_clock_new_with_start_time (GST_SECOND);
  gst_test_clock_set_time (GST_TEST_CLOCK (clock), GST_SECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  gst_test_clock_set_time (GST_TEST_CLOCK (clock), GST_SECOND + 1);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND + 1);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_advance_time)
{
  GstClock *clock = gst_test_clock_new_with_start_time (GST_SECOND);
  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 0);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 42 * GST_MSECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==,
      GST_SECOND + (42 * GST_MSECOND));
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_wait_synchronous_no_timeout)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GThread *worker_thread;
  GstClockID pending_id;
  GstClockID processed_id;
  SyncClockWaitContext context;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND - 1);
  context.clock_id = gst_clock_id_ref (clock_id);
  context.jitter = 0;
  worker_thread =
      g_thread_new ("worker_thread",
      test_wait_pending_single_shot_id_sync_worker, &context);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_SECOND - 1);
  gst_clock_id_unref (pending_id);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_EARLY);
  gst_clock_id_unref (processed_id);
  g_thread_join (worker_thread);
  g_assert_cmpuint (context.jitter, ==, 1);
  gst_clock_id_unref (context.clock_id);
  gst_clock_id_unref (clock_id);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND);
  context.clock_id = gst_clock_id_ref (clock_id);
  context.jitter = 0;
  worker_thread =
      g_thread_new ("worker_thread",
      test_wait_pending_single_shot_id_sync_worker, &context);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_SINGLE, GST_SECOND);
  gst_clock_id_unref (pending_id);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  g_thread_join (worker_thread);
  g_assert_cmpuint (context.jitter, ==, 0);
  gst_clock_id_unref (context.clock_id);
  gst_clock_id_unref (clock_id);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND + 1);
  context.clock_id = gst_clock_id_ref (clock_id);
  context.jitter = 0;
  worker_thread =
      g_thread_new ("worker_thread",
      test_wait_pending_single_shot_id_sync_worker, &context);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_SECOND + 1);
  gst_clock_id_unref (pending_id);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  g_assert (processed_id == NULL);
  gst_test_clock_advance_time (test_clock, 1);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  g_thread_join (worker_thread);
  g_assert_cmpuint (context.jitter, ==, -1);
  gst_clock_id_unref (context.clock_id);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_wait_pending_single_shot_id)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GstClockID processed_id;
  GThread *worker_thread;
  GstClockID pending_id;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND);
  gst_clock_id_wait_async (clock_id, test_async_wait_cb, NULL, NULL);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_SINGLE, GST_SECOND);
  gst_clock_id_unref (pending_id);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  gst_clock_id_unref (clock_id);

  clock_id = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  worker_thread =
      g_thread_new ("worker_thread",
      test_wait_pending_single_shot_id_async_worker, clock_id);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      2 * GST_SECOND);
  gst_clock_id_unref (pending_id);
  g_thread_join (worker_thread);
  gst_clock_id_unref (clock_id);

  clock_id = gst_clock_new_single_shot_id (clock, 3 * GST_SECOND);
  worker_thread =
      g_thread_new ("worker_thread",
      test_wait_pending_single_shot_id_async_worker, clock_id);
  gst_test_clock_wait_for_next_pending_id (test_clock, NULL);
  g_thread_join (worker_thread);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_wait_pending_periodic_id)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GstClockID processed_id;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);
  clock_id = gst_clock_new_periodic_id (clock, GST_SECOND, GST_MSECOND);

  {
    GThread *waiter_thread;

    waiter_thread =
        g_thread_new ("waiter_thread",
        test_wait_pending_periodic_id_waiter_thread, clock_id);

    gst_test_clock_wait_for_next_pending_id (test_clock, NULL);
    gst_test_clock_set_time (test_clock, GST_SECOND);
    processed_id = gst_test_clock_process_next_clock_id (test_clock);
    assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_PERIODIC,
        GST_CLOCK_OK);
    gst_clock_id_unref (processed_id);

    g_thread_join (waiter_thread);
  }

  {
    guint i;
    GThread *waiter_thread;

    for (i = 0; i < 3; i++) {
      g_assert (!gst_test_clock_peek_next_pending_id (test_clock, NULL));
      g_usleep (G_USEC_PER_SEC / 10 / 10);
    }

    waiter_thread =
        g_thread_new ("waiter_thread",
        test_wait_pending_periodic_id_waiter_thread, clock_id);

    gst_test_clock_wait_for_next_pending_id (test_clock, NULL);
    gst_clock_id_unschedule (clock_id);

    g_thread_join (waiter_thread);
  }

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_past)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GstClockTimeDiff jitter;
  GtuClockWaitContext *wait_ctx;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND - 1);
  wait_ctx =
      gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, &jitter);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx) == GST_CLOCK_EARLY);
  g_assert_cmpint (jitter, ==, 1);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_present)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GstClockTimeDiff jitter;
  GtuClockWaitContext *wait_ctx;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND);
  wait_ctx =
      gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, &jitter);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx) == GST_CLOCK_OK);
  g_assert_cmpint (jitter, ==, 0);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_future)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GstClockTimeDiff jitter;
  GtuClockWaitContext *wait_ctx;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  wait_ctx =
      gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, &jitter);
  gst_test_clock_advance_time (test_clock, GST_SECOND);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx) == GST_CLOCK_OK);
  g_assert_cmpint (jitter, ==, -GST_SECOND);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_unschedule)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  GtuClockWaitContext *wait_ctx;
  gboolean wait_complete = FALSE;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND);
  gst_clock_id_unschedule (clock_id);
  /* any wait should timeout immediately */
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_UNSCHEDULED);
  g_assert (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED);
  gst_clock_id_unref (clock_id);

  clock_id = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  wait_ctx = gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, NULL);
  gst_clock_id_unschedule (clock_id);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx)
      == GST_CLOCK_UNSCHEDULED);
  gst_clock_id_unref (clock_id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_ordering)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id_a, clock_id_b;
  GtuClockWaitContext *wait_ctx_a, *wait_ctx_b;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id_a = gst_clock_new_single_shot_id (clock, 3 * GST_SECOND);
  wait_ctx_a =
      gst_test_util_wait_for_clock_id_begin (test_clock, clock_id_a, NULL);

  gst_test_clock_advance_time (test_clock, GST_SECOND);

  clock_id_b = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  wait_ctx_b =
      gst_test_util_wait_for_clock_id_begin (test_clock, clock_id_b, NULL);

  gst_test_clock_advance_time (test_clock, GST_SECOND);

  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx_b) == GST_CLOCK_OK);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx_a) == GST_CLOCK_OK);

  gst_clock_id_unref (clock_id_b);
  gst_clock_id_unref (clock_id_a);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_ordering_parallel)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id_a, clock_id_b;
  GtuClockWaitContext *wait_ctx_a, *wait_ctx_b;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id_a = gst_clock_new_single_shot_id (clock, 3 * GST_SECOND);
  clock_id_b = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  wait_ctx_a = gst_test_util_wait_for_clock_id_begin (test_clock, clock_id_a,
      NULL);
  wait_ctx_b = gst_test_util_wait_for_clock_id_begin (test_clock, clock_id_b,
      NULL);

  g_assert_cmpuint (gst_test_clock_get_next_entry_time (test_clock), ==,
      2 * GST_SECOND);
  gst_test_clock_advance_time (test_clock, GST_SECOND);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx_b) == GST_CLOCK_OK);

  g_assert_cmpuint (gst_test_clock_get_next_entry_time (test_clock), ==,
      3 * GST_SECOND);
  gst_test_clock_advance_time (test_clock, GST_SECOND);
  g_assert (gst_test_util_wait_for_clock_id_end (wait_ctx_a) == GST_CLOCK_OK);

  gst_clock_id_unref (clock_id_b);
  gst_clock_id_unref (clock_id_a);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_sync_simultaneous_no_timeout)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id_a;
  GstClockID clock_id_b;
  SyncClockWaitContext context_a;
  SyncClockWaitContext context_b;
  GThread *worker_thread_a;
  GThread *worker_thread_b;
  GstClockID processed_id;
  GstClockID pending_id;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  clock_id_a = gst_clock_new_single_shot_id (clock, 5 * GST_SECOND);
  clock_id_b = gst_clock_new_single_shot_id (clock, 6 * GST_SECOND);

  context_a.clock_id = gst_clock_id_ref (clock_id_a);
  context_a.jitter = 0;
  context_b.clock_id = gst_clock_id_ref (clock_id_b);
  context_b.jitter = 0;

  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 0, NULL);

  worker_thread_b =
      g_thread_new ("worker_thread_b",
      test_wait_pending_single_shot_id_sync_worker, &context_b);

  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 1, NULL);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id_b, GST_CLOCK_ENTRY_SINGLE,
      6 * GST_SECOND);
  gst_clock_id_unref (pending_id);

  worker_thread_a =
      g_thread_new ("worker_thread_a",
      test_wait_pending_single_shot_id_sync_worker, &context_a);

  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 2, NULL);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id_a, GST_CLOCK_ENTRY_SINGLE,
      5 * GST_SECOND);
  gst_clock_id_unref (pending_id);

  g_assert_cmpuint (gst_test_clock_get_next_entry_time (test_clock), ==,
      5 * GST_SECOND);
  gst_test_clock_advance_time (test_clock, 5 * GST_SECOND);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id_a, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);

  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 1, NULL);
  gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
  assert_pending_id (pending_id, clock_id_b, GST_CLOCK_ENTRY_SINGLE,
      6 * GST_SECOND);
  gst_clock_id_unref (pending_id);

  g_assert_cmpuint (gst_test_clock_get_next_entry_time (test_clock), ==,
      6 * GST_SECOND);
  gst_test_clock_advance_time (test_clock, 6 * GST_SECOND);
  processed_id = gst_test_clock_process_next_clock_id (test_clock);
  assert_processed_id (processed_id, clock_id_b, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);

  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 0, NULL);

  g_thread_join (worker_thread_a);
  g_thread_join (worker_thread_b);

  g_assert_cmpuint (context_a.jitter, ==, -4 * GST_SECOND);
  g_assert_cmpuint (context_b.jitter, ==, -5 * GST_SECOND);

  gst_clock_id_unref (context_a.clock_id);
  gst_clock_id_unref (context_b.clock_id);

  gst_clock_id_unref (clock_id_a);
  gst_clock_id_unref (clock_id_b);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_processing_multiple_ids)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id_a;
  GstClockID clock_id_b;
  SyncClockWaitContext context_a;
  SyncClockWaitContext context_b;
  GThread *worker_thread_a;
  GThread *worker_thread_b;
  GList *pending_list = NULL;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  /* register a wait for 5 seconds */
  clock_id_a = gst_clock_new_single_shot_id (clock, 5 * GST_SECOND);
  context_a.clock_id = gst_clock_id_ref (clock_id_a);
  context_a.jitter = 0;
  worker_thread_a =
      g_thread_new ("worker_thread_a",
      test_wait_pending_single_shot_id_sync_worker, &context_a);

  /* register another wait for 6 seconds */
  clock_id_b = gst_clock_new_single_shot_id (clock, 6 * GST_SECOND);
  context_b.clock_id = gst_clock_id_ref (clock_id_b);
  context_b.jitter = 0;
  worker_thread_b =
      g_thread_new ("worker_thread_b",
      test_wait_pending_single_shot_id_sync_worker, &context_b);

  /* wait for two waits */
  gst_test_clock_wait_for_multiple_pending_ids (test_clock, 2, &pending_list);

  /* assert they are correct */
  assert_pending_id (pending_list->data, clock_id_a, GST_CLOCK_ENTRY_SINGLE,
      5 * GST_SECOND);
  assert_pending_id (pending_list->next->data, clock_id_b,
      GST_CLOCK_ENTRY_SINGLE, 6 * GST_SECOND);

  /* verify we are waiting for 6 seconds as the latest time */
  fail_unless_equals_int64 (6 * GST_SECOND,
      gst_test_clock_id_list_get_latest_time (pending_list));

  /* process both ID's at the same time */
  gst_test_clock_process_id_list (test_clock, pending_list);
  g_list_free_full (pending_list, (GDestroyNotify) gst_clock_id_unref);

  g_thread_join (worker_thread_a);
  g_thread_join (worker_thread_b);

  fail_unless_equals_int64 (-4 * GST_SECOND, context_a.jitter);
  fail_unless_equals_int64 (-5 * GST_SECOND, context_b.jitter);

  gst_clock_id_unref (context_a.clock_id);
  gst_clock_id_unref (context_b.clock_id);

  gst_clock_id_unref (clock_id_a);
  gst_clock_id_unref (clock_id_b);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_async_past)
{
  GstClock *clock;
  GstClockID clock_id;
  GstClockID processed_id;
  gboolean wait_complete = FALSE;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND - 1);
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_OK);
  g_assert (!wait_complete);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (wait_complete);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_EARLY);
  gst_clock_id_unref (processed_id);
  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_async_present)
{
  GstClock *clock;
  GstClockID clock_id;
  GstClockID processed_id;
  gboolean wait_complete = FALSE;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  clock_id = gst_clock_new_single_shot_id (clock, GST_SECOND);
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_OK);
  g_assert (!wait_complete);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (wait_complete);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_async_future)
{
  GstClock *clock;
  GstClockID clock_id;
  GstClockID processed_id;
  gboolean wait_complete = FALSE;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  clock_id = gst_clock_new_single_shot_id (clock, 2 * GST_SECOND);
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_OK);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (processed_id == NULL);
  g_assert (!wait_complete);
  g_assert (GST_CLOCK_ENTRY_STATUS (GST_CLOCK_ENTRY (clock_id))
      == GST_CLOCK_OK);

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), GST_SECOND - 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (processed_id == NULL);
  g_assert (!wait_complete);
  g_assert (GST_CLOCK_ENTRY_STATUS (GST_CLOCK_ENTRY (clock_id))
      == GST_CLOCK_OK);

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (wait_complete);
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_SINGLE,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  g_assert (GST_CLOCK_ENTRY_STATUS (GST_CLOCK_ENTRY (clock_id))
      == GST_CLOCK_OK);

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_single_shot_async_unschedule)
{
  GstClock *clock;
  GstClockID clock_id;
  gboolean wait_complete = FALSE;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);

  clock_id = gst_clock_new_single_shot_id (clock, 3 * GST_SECOND);
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_OK);

  gst_clock_id_unschedule (clock_id);

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 2 * GST_SECOND);
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock))
      == NULL);
  g_assert (!wait_complete);

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_periodic_sync)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  guint i;
  const GstClockTime interval = 4 * GST_MSECOND;

  clock = gst_test_clock_new ();
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_periodic_id (clock, GST_SECOND, interval);

  for (i = 0; i < 3; i++) {
    GtuClockWaitContext *wait_ctx;
    GstClockID pending_id;
    guint j;

    wait_ctx =
        gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, NULL);

    gst_test_clock_wait_for_next_pending_id (test_clock, &pending_id);
    assert_pending_id (pending_id, clock_id, GST_CLOCK_ENTRY_PERIODIC,
        GST_SECOND + (i * interval));
    gst_clock_id_unref (pending_id);

    for (j = 0; j < 10; j++) {
      g_usleep (G_USEC_PER_SEC / 10 / 10);
      g_assert (!gst_test_util_clock_wait_context_has_completed (wait_ctx));
    }

    if (i == 0)
      gst_test_clock_advance_time (test_clock, GST_SECOND);
    else
      gst_test_clock_advance_time (test_clock, interval);

    gst_test_util_wait_for_clock_id_end (wait_ctx);
  }

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_periodic_async)
{
  GstClock *clock;
  GstClockID clock_id;
  GstClockID processed_id;
  gboolean wait_complete = FALSE;
  const GstClockTime interval = 4 * GST_MSECOND;

  clock = gst_test_clock_new ();
  clock_id = gst_clock_new_periodic_id (clock, gst_clock_get_time (clock),
      interval);
  g_assert (gst_clock_id_wait_async (clock_id, test_async_wait_cb,
          &wait_complete, NULL) == GST_CLOCK_OK);

  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_PERIODIC,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);

  g_assert (wait_complete);
  wait_complete = FALSE;

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), interval - 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (processed_id == NULL);
  g_assert (!wait_complete);

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_PERIODIC,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  g_assert (wait_complete);
  wait_complete = FALSE;

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), interval - 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  g_assert (processed_id == NULL);
  g_assert (!wait_complete);

  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 1);
  processed_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (clock));
  assert_processed_id (processed_id, clock_id, GST_CLOCK_ENTRY_PERIODIC,
      GST_CLOCK_OK);
  gst_clock_id_unref (processed_id);
  g_assert (wait_complete);
  wait_complete = FALSE;

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_periodic_uniqueness)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  guint i;
  const GstClockTime interval = 4 * GST_MSECOND;

  clock = gst_test_clock_new ();
  test_clock = GST_TEST_CLOCK (clock);

  clock_id = gst_clock_new_periodic_id (clock, 0, interval);

  for (i = 0; i < 3; i++) {
    GtuClockWaitContext *wait_ctx;
    guint j;

    wait_ctx =
        gst_test_util_wait_for_clock_id_begin (test_clock, clock_id, NULL);

    for (j = 0; j < 10; j++) {
      g_usleep (G_USEC_PER_SEC / 10 / 10);
      g_assert_cmpuint (gst_test_clock_peek_id_count (test_clock), ==, 1);
    }

    gst_test_clock_advance_time (test_clock, interval);
    gst_test_util_wait_for_clock_id_end (wait_ctx);
  }

  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_crank)
{
  GstClock *clock;
  GstTestClock *test_clock;
  GstClockID clock_id;
  SyncClockWaitContext context;
  GThread *worker_thread;

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  test_clock = GST_TEST_CLOCK (clock);

  /* register a wait for 5 seconds */
  clock_id = gst_clock_new_single_shot_id (clock, 5 * GST_SECOND);
  context.clock_id = gst_clock_id_ref (clock_id);
  context.jitter = 0;
  worker_thread =
      g_thread_new ("worker_thread_a",
      test_wait_pending_single_shot_id_sync_worker, &context);

  /* crank */
  gst_test_clock_crank (test_clock);

  /* the clock should have advanced and the wait released */
  g_thread_join (worker_thread);

  /* 4 seconds was spent waiting for the clock */
  fail_unless_equals_int64 (-4 * GST_SECOND, context.jitter);

  /* and the clock is now at 5 seconds */
  fail_unless_equals_int64 (5 * GST_SECOND, gst_clock_get_time (clock));

  gst_clock_id_unref (context.clock_id);
  gst_clock_id_unref (clock_id);
  gst_object_unref (clock);
}

GST_END_TEST;

static Suite *
gst_test_clock_suite (void)
{
  Suite *s = suite_create ("GstTestClock");
  TCase *tc_chain = tcase_create ("testclock");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_flags);
  tcase_add_test (tc_chain, test_resolution_query);
  tcase_add_test (tc_chain, test_start_time);
  tcase_add_test (tc_chain, test_set_time);
  tcase_add_test (tc_chain, test_advance_time);
  tcase_add_test (tc_chain, test_wait_synchronous_no_timeout);
  tcase_add_test (tc_chain, test_wait_pending_single_shot_id);
  tcase_add_test (tc_chain, test_wait_pending_periodic_id);
  tcase_add_test (tc_chain, test_single_shot_sync_simultaneous_no_timeout);
  tcase_add_test (tc_chain, test_processing_multiple_ids);
  tcase_add_test (tc_chain, test_single_shot_sync_past);
  tcase_add_test (tc_chain, test_single_shot_sync_present);
  tcase_add_test (tc_chain, test_single_shot_sync_future);
  tcase_add_test (tc_chain, test_single_shot_sync_unschedule);
  tcase_add_test (tc_chain, test_single_shot_sync_ordering);
  tcase_add_test (tc_chain, test_single_shot_sync_ordering_parallel);
  tcase_add_test (tc_chain, test_single_shot_async_past);
  tcase_add_test (tc_chain, test_single_shot_async_present);
  tcase_add_test (tc_chain, test_single_shot_async_future);
  tcase_add_test (tc_chain, test_single_shot_async_unschedule);
  tcase_add_test (tc_chain, test_periodic_sync);
  tcase_add_test (tc_chain, test_periodic_async);
  tcase_add_test (tc_chain, test_periodic_uniqueness);
  tcase_add_test (tc_chain, test_crank);

  return s;
}

GST_CHECK_MAIN (gst_test_clock);
