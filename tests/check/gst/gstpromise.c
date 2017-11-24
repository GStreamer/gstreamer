/* GStreamer
 *
 * unit test for GstPromise
 *
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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

struct event_queue
{
  GMutex lock;
  GCond cond;
  GThread *thread;
  GMainContext *main_context;
  GMainLoop *main_loop;
  gpointer user_data;
};

static gboolean
_unlock_thread (GMutex * lock)
{
  g_mutex_unlock (lock);
  return G_SOURCE_REMOVE;
}

static gpointer
_promise_thread (struct event_queue *q)
{
  g_mutex_lock (&q->lock);
  q->main_context = g_main_context_new ();
  q->main_loop = g_main_loop_new (q->main_context, FALSE);

  g_cond_broadcast (&q->cond);
  g_main_context_invoke (q->main_context, (GSourceFunc) _unlock_thread,
      &q->lock);

  g_main_loop_run (q->main_loop);

  g_mutex_lock (&q->lock);
  g_main_context_unref (q->main_context);
  q->main_context = NULL;
  g_main_loop_unref (q->main_loop);
  q->main_loop = NULL;
  g_cond_broadcast (&q->cond);
  g_mutex_unlock (&q->lock);

  return NULL;
}

static void
event_queue_start (struct event_queue *q)
{
  g_mutex_lock (&q->lock);
  q->thread = g_thread_new ("promise-thread", (GThreadFunc) _promise_thread, q);

  while (!q->main_loop)
    g_cond_wait (&q->cond, &q->lock);
  g_mutex_unlock (&q->lock);
}

static void
event_queue_stop (struct event_queue *q)
{
  g_mutex_lock (&q->lock);
  if (q->main_loop)
    g_main_loop_quit (q->main_loop);
  g_mutex_unlock (&q->lock);
}

static void
event_queue_stop_wait (struct event_queue *q)
{
  g_mutex_lock (&q->lock);
  while (q->main_loop) {
    g_main_loop_quit (q->main_loop);
    g_cond_wait (&q->cond, &q->lock);
  }
  g_mutex_unlock (&q->lock);

  g_thread_unref (q->thread);
}

static struct event_queue *
event_queue_new (void)
{
  struct event_queue *q = g_new0 (struct event_queue, 1);

  GST_LOG ("starting event queue %p", q);

  g_mutex_init (&q->lock);
  g_cond_init (&q->cond);
  event_queue_start (q);

  return q;
}

static void
event_queue_free (struct event_queue *q)
{
  event_queue_stop_wait (q);

  g_mutex_clear (&q->lock);
  g_cond_clear (&q->cond);

  GST_LOG ("stopped event queue %p", q);

  g_free (q);
}

static void
_enqueue_task (struct event_queue *q, GSourceFunc func, gpointer data,
    GDestroyNotify notify)
{
  GSource *source;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, (GSourceFunc) func, data, notify);
  g_source_attach (source, q->main_context);
  g_source_unref (source);
}

GST_START_TEST (test_reply)
{
  GstPromise *r;

  r = gst_promise_new ();

  gst_promise_reply (r, NULL);
  fail_unless (gst_promise_wait (r) == GST_PROMISE_RESULT_REPLIED);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_data)
{
  GstPromise *r;
  GstStructure *s;
  const GstStructure *ret;

  r = gst_promise_new ();

  s = gst_structure_new ("promise", "test", G_TYPE_INT, 1, NULL);
  gst_promise_reply (r, s);
  fail_unless (gst_promise_wait (r) == GST_PROMISE_RESULT_REPLIED);
  ret = gst_promise_get_reply (r);
  fail_unless (gst_structure_is_equal (ret, s));

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_immutable)
{
  GstPromise *r;
  GstStructure *s, *ret;

  r = gst_promise_new ();

  s = gst_structure_new ("promise", "test", G_TYPE_INT, 1, NULL);
  gst_promise_reply (r, s);
  ret = (GstStructure *) gst_promise_get_reply (r);

  /* immutable result must not be able to modify the reply */
  ASSERT_CRITICAL (gst_structure_set (ret, "foo", G_TYPE_STRING, "bar", NULL));
  fail_unless (gst_structure_get_string (ret, "foo") == NULL);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_interrupt)
{
  GstPromise *r;

  r = gst_promise_new ();

  gst_promise_interrupt (r);
  fail_unless (gst_promise_wait (r) == GST_PROMISE_RESULT_INTERRUPTED);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_expire)
{
  GstPromise *r;

  r = gst_promise_new ();

  gst_promise_expire (r);
  fail_unless (gst_promise_wait (r) == GST_PROMISE_RESULT_EXPIRED);

  gst_promise_unref (r);
}

GST_END_TEST;

struct change_data
{
  int change_count;
  GstPromiseResult result;
};

static void
on_change (GstPromise * promise, gpointer user_data)
{
  struct change_data *res = user_data;

  res->result = gst_promise_wait (promise);
  res->change_count += 1;
}

GST_START_TEST (test_change_func)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_reply (r, NULL);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_expire)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_reply (r, NULL);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_discard)
{
  GstPromise *r;

  /* NULL promise => discard reply */
  r = NULL;

  /* no-op, we don't want a reply */
  gst_promise_reply (r, NULL);

  if (r)
    gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_interrupt)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_reply (r, NULL);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);
  gst_promise_interrupt (r);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_reply_reply)
{
  GstPromise *r;
  GstStructure *s;
  struct change_data data = { 0, };
  const GstStructure *ret;

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  s = gst_structure_new ("promise", "test", G_TYPE_INT, 1, NULL);
  gst_promise_reply (r, s);
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);
  ASSERT_CRITICAL (gst_promise_reply (r, NULL));
  fail_unless (gst_promise_wait (r) == GST_PROMISE_RESULT_REPLIED);
  ret = gst_promise_get_reply (r);
  fail_unless (gst_structure_is_equal (ret, s));
  fail_unless (data.result == GST_PROMISE_RESULT_REPLIED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_interrupt_expire)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_interrupt (r);
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_interrupt_reply)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_interrupt (r);
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);
  gst_promise_reply (r, NULL);
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_interrupt_interrupt)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_interrupt (r);
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);
  ASSERT_CRITICAL (gst_promise_interrupt (r));
  fail_unless (data.result == GST_PROMISE_RESULT_INTERRUPTED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_expire_expire)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_expire_interrupt)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);
  ASSERT_CRITICAL (gst_promise_interrupt (r));
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

GST_START_TEST (test_expire_reply)
{
  GstPromise *r;
  struct change_data data = { 0, };

  r = gst_promise_new_with_change_func (on_change, &data, NULL);
  gst_promise_expire (r);
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);
  ASSERT_CRITICAL (gst_promise_reply (r, NULL));
  fail_unless (data.result == GST_PROMISE_RESULT_EXPIRED);
  fail_unless (data.change_count == 1);

  gst_promise_unref (r);
}

GST_END_TEST;

struct stress_item
{
  struct event_queue *q;
  GstPromise *promise;
  GstPromiseResult result;
};

static void
stress_reply (struct stress_item *item)
{
  switch (item->result) {
    case GST_PROMISE_RESULT_REPLIED:
      gst_promise_reply (item->promise, NULL);
      break;
    case GST_PROMISE_RESULT_INTERRUPTED:
      gst_promise_interrupt (item->promise);
      break;
    case GST_PROMISE_RESULT_EXPIRED:
      gst_promise_expire (item->promise);
      break;
    default:
      g_assert_not_reached ();
  }
}

struct stress_queues
{
  GAsyncQueue *push_queue;
  GAsyncQueue *wait_queue;
  guint64 push_count;
};

static gboolean
_push_random_promise (struct event_queue *q)
{
  struct stress_queues *s_q = q->user_data;
  struct stress_item *item;

  item = g_new0 (struct stress_item, 1);
  item->promise = gst_promise_new ();
  while (item->result == GST_PROMISE_RESULT_PENDING)
    item->result = g_random_int () % 4;

  g_async_queue_push (s_q->wait_queue, item);
  g_async_queue_push (s_q->push_queue, item);

  s_q->push_count++;

  return G_SOURCE_CONTINUE;
}

static void
_push_stop_promise (struct event_queue *q)
{
  struct stress_queues *s_q = q->user_data;
  gpointer item = GINT_TO_POINTER (1);

  g_async_queue_push (s_q->wait_queue, item);
  g_async_queue_push (s_q->push_queue, item);
}

static gboolean
_pop_promise (struct event_queue *q)
{
  struct stress_queues *s_q = q->user_data;
  struct stress_item *item;

  item = g_async_queue_pop (s_q->push_queue);

  if (item == (void *) 1)
    return G_SOURCE_REMOVE;

  stress_reply (item);

  return G_SOURCE_CONTINUE;
}

static gboolean
_wait_promise (struct event_queue *q)
{
  struct stress_queues *s_q = q->user_data;
  struct stress_item *item;

  item = g_async_queue_pop (s_q->wait_queue);

  if (item == (void *) 1)
    return G_SOURCE_REMOVE;

  fail_unless (gst_promise_wait (item->promise) == item->result);

  gst_promise_unref (item->promise);
  g_free (item);

  return G_SOURCE_CONTINUE;
}

GST_START_TEST (test_stress)
{
#define N_QUEUES 3
  struct event_queue *pushers[N_QUEUES];
  struct event_queue *poppers[N_QUEUES];
  struct event_queue *waiters[N_QUEUES];
  struct stress_queues s_q = { 0, };
  int i;

  s_q.push_queue = g_async_queue_new ();
  s_q.wait_queue = g_async_queue_new ();

  for (i = 0; i < N_QUEUES; i++) {
    pushers[i] = event_queue_new ();
    pushers[i]->user_data = &s_q;
    _enqueue_task (pushers[i], (GSourceFunc) _push_random_promise, pushers[i],
        NULL);
    waiters[i] = event_queue_new ();
    waiters[i]->user_data = &s_q;
    _enqueue_task (waiters[i], (GSourceFunc) _wait_promise, waiters[i], NULL);
    poppers[i] = event_queue_new ();
    poppers[i]->user_data = &s_q;
    _enqueue_task (poppers[i], (GSourceFunc) _pop_promise, poppers[i], NULL);
  }

  GST_INFO ("all set up, waiting.");
  g_usleep (100000);
  GST_INFO ("wait done, cleaning up the test.");

  {
    struct stress_item *item;
    int push_size;

    for (i = 0; i < N_QUEUES; i++) {
      event_queue_stop (pushers[i]);
      event_queue_stop (poppers[i]);
      event_queue_stop (waiters[i]);
      _push_stop_promise (pushers[i]);
    }

    for (i = 0; i < N_QUEUES; i++) {
      event_queue_free (pushers[i]);
      event_queue_free (poppers[i]);
    }

    push_size = g_async_queue_length (s_q.push_queue);

    /* push through all the promises so all the waits will complete */
    while ((item = g_async_queue_try_pop (s_q.push_queue))) {
      if (item == (void *) 1)
        continue;
      stress_reply (item);
    }

    for (i = 0; i < N_QUEUES; i++)
      event_queue_free (waiters[i]);

    GST_INFO ("pushed %" G_GUINT64_FORMAT ", %d leftover in push queue, "
        "%d leftover in wait queue", s_q.push_count, push_size,
        g_async_queue_length (s_q.wait_queue));

    while ((item = g_async_queue_try_pop (s_q.wait_queue))) {
      if (item == (void *) 1)
        continue;

      fail_unless (gst_promise_wait (item->promise) == item->result);

      gst_promise_unref (item->promise);
      g_free (item);
    }
  }

  g_async_queue_unref (s_q.push_queue);
  g_async_queue_unref (s_q.wait_queue);
}

GST_END_TEST;

static Suite *
gst_promise_suite (void)
{
  Suite *s = suite_create ("GstPromise");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_reply);
  tcase_add_test (tc_chain, test_reply_data);
  tcase_add_test (tc_chain, test_reply_immutable);
  tcase_add_test (tc_chain, test_interrupt);
  tcase_add_test (tc_chain, test_expire);
  tcase_add_test (tc_chain, test_change_func);
  tcase_add_test (tc_chain, test_reply_expire);
  tcase_add_test (tc_chain, test_reply_discard);
  tcase_add_test (tc_chain, test_reply_interrupt);
  tcase_add_test (tc_chain, test_reply_reply);
  tcase_add_test (tc_chain, test_interrupt_reply);
  tcase_add_test (tc_chain, test_interrupt_expire);
  tcase_add_test (tc_chain, test_interrupt_interrupt);
  tcase_add_test (tc_chain, test_expire_expire);
  tcase_add_test (tc_chain, test_expire_interrupt);
  tcase_add_test (tc_chain, test_expire_reply);
  tcase_add_test (tc_chain, test_stress);

  return s;
}

GST_CHECK_MAIN (gst_promise);
