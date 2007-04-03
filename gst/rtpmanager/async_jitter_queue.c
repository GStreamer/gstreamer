/* 
 * Async Jitter Queue based on g_async_queue
 * This code is GST RTP smart and deals with timestamps
 *
 * Farsight Voice+Video library
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
 *   @author: Philippe Khalaf <philippe.khalaf@collabora.co.uk>.
 *
 *   This is an async queue that has a buffering mecanism based on the set low
 *   and high threshold. When the lower threshold is reached, the queue will
 *   fill itself up until the higher threshold is reached before allowing any
 *   pops to occur. This allows a jitterbuffer of at least min threshold items
 *   to be available.
 */

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * GAsyncQueue: asynchronous queue implementation, based on Gqueue.
 * Copyright (C) 2000 Sebastian Wilhelmi; University of Karlsruhe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * MT safe
 */

#include "config.h"

#include "async_jitter_queue.h"

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

#define DEFAULT_LOW_THRESHOLD 0.1
#define DEFAULT_HIGH_THRESHOLD 0.9

struct _AsyncJitterQueue
{
  GMutex *mutex;
  GCond *cond;
  GQueue *queue;
  guint waiting_threads;
  gint32 ref_count;
  gfloat low_threshold;
  gfloat high_threshold;
  guint32 max_queue_length;
  gboolean buffering;
  gboolean pop_flushing;
  gboolean pop_blocking;
  guint pops_remaining;
  guint32 tail_buffer_duration;
};

/**
 * async_jitter_queue_new:
 * 
 * Creates a new asynchronous queue with the initial reference count of 1.
 * 
 * Return value: the new #AsyncJitterQueue.
 **/
AsyncJitterQueue *
async_jitter_queue_new (void)
{
  AsyncJitterQueue *retval = g_new (AsyncJitterQueue, 1);

  retval->mutex = g_mutex_new ();
  retval->cond = g_cond_new ();
  retval->queue = g_queue_new ();
  retval->waiting_threads = 0;
  retval->ref_count = 1;
  retval->low_threshold = DEFAULT_LOW_THRESHOLD;
  retval->high_threshold = DEFAULT_HIGH_THRESHOLD;
  retval->buffering = TRUE;     /* we need to buffer initially */
  retval->pop_flushing = TRUE;
  retval->pop_blocking = TRUE;
  retval->pops_remaining = 0;
  retval->tail_buffer_duration = 0;
  return retval;
}

/* checks buffering state and wakes up waiting pops */
void
signal_waiting_threads (AsyncJitterQueue * queue)
{
  if (async_jitter_queue_length_ts_units_unlocked (queue) >=
      queue->high_threshold * queue->max_queue_length) {
    queue->buffering = FALSE;
  }

  if (queue->waiting_threads > 0) {
    if (!queue->buffering) {
      g_cond_signal (queue->cond);
    }
  }
}

/**
 * async_jitter_queue_ref:
 * @queue: a #AsyncJitterQueue.
 *
 * Increases the reference count of the asynchronous @queue by 1. You
 * do not need to hold the lock to call this function.
 *
 * Returns: the @queue that was passed in (since 2.6)
 **/
AsyncJitterQueue *
async_jitter_queue_ref (AsyncJitterQueue * queue)
{
  g_return_val_if_fail (queue, NULL);
  g_return_val_if_fail (g_atomic_int_get (&queue->ref_count) > 0, NULL);

  g_atomic_int_inc (&queue->ref_count);

  return queue;
}

/**
 * async_jitter_queue_ref_unlocked:
 * @queue: a #AsyncJitterQueue.
 * 
 * Increases the reference count of the asynchronous @queue by 1.
 **/
void
async_jitter_queue_ref_unlocked (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  g_atomic_int_inc (&queue->ref_count);
}

/**
 * async_jitter_queue_set_low_threshold:
 * @queue: a #AsyncJitterQueue.
 * @threshold: the lower threshold (fraction of max size)
 * 
 * Sets the low threshold on the queue. This threshold indicates the minimum
 * number of items allowed in the queue before we refill it up to the set
 * maximum threshold.
 **/
void
async_jitter_queue_set_low_threshold (AsyncJitterQueue * queue,
    gfloat threshold)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->low_threshold = threshold;
}

/**
 * async_jitter_queue_set_max_threshold:
 * @queue: a #AsyncJitterQueue.
 * @threshold: the higher threshold (fraction of max size)
 * 
 * Sets the high threshold on the queue. This threshold indicates the amount of
 * items to fill in the queue before releasing any blocking pop calls. This
 * blocking mecanism is only triggered when we reach the low threshold and must
 * refill the queue.
 **/
void
async_jitter_queue_set_high_threshold (AsyncJitterQueue * queue,
    gfloat threshold)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->high_threshold = threshold;
}

/* set the maximum queue length in RTP timestamp units */
void
async_jitter_queue_set_max_queue_length (AsyncJitterQueue * queue,
    guint32 max_length)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->max_queue_length = max_length;
}

GQueue *
async_jitter_queue_get_g_queue (AsyncJitterQueue * queue)
{
  g_return_val_if_fail (queue, NULL);

  return queue->queue;
}

static guint32
calculate_ts_diff (guint32 high_ts, guint32 low_ts)
{
  /* it needs to work if ts wraps */
  if (high_ts >= low_ts) {
    return high_ts - low_ts;
  } else {
    return high_ts + G_MAXUINT32 + 1 - low_ts;
  }
}

/* this function returns the length of the queue in timestamp units. It will
 * also add the duration of the last buffer in the queue */
/* FIXME This function wrongly assumes that there are no missing packets inside
 * the buffer, in reality it needs to check for gaps and subsctract those from
 * the total */
guint32
async_jitter_queue_length_ts_units_unlocked (AsyncJitterQueue * queue)
{
  guint32 tail_ts;
  guint32 head_ts;
  guint32 ret;
  GstBuffer *head;
  GstBuffer *tail;

  g_return_val_if_fail (queue, 0);

  if (queue->queue->length < 2) {
    return 0;
  }

  tail = g_queue_peek_tail (queue->queue);
  head = g_queue_peek_head (queue->queue);

  if (!GST_IS_BUFFER (tail) || !GST_IS_BUFFER (head))
    return 0;

  tail_ts = gst_rtp_buffer_get_timestamp (tail);
  head_ts = gst_rtp_buffer_get_timestamp (head);

  ret = calculate_ts_diff (head_ts, tail_ts);

  /* let's add the duration of the tail buffer */
  ret += queue->tail_buffer_duration;

  return ret;
}

/**
 * async_jitter_queue_unref_and_unlock:
 * @queue: a #AsyncJitterQueue.
 * 
 * Decreases the reference count of the asynchronous @queue by 1 and
 * releases the lock. This function must be called while holding the
 * @queue's lock. If the reference count went to 0, the @queue will be
 * destroyed and the memory allocated will be freed.
 **/
void
async_jitter_queue_unref_and_unlock (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  g_mutex_unlock (queue->mutex);
  async_jitter_queue_unref (queue);
}

/**
 * async_jitter_queue_unref:
 * @queue: a #AsyncJitterQueue.
 * 
 * Decreases the reference count of the asynchronous @queue by 1. If
 * the reference count went to 0, the @queue will be destroyed and the
 * memory allocated will be freed. So you are not allowed to use the
 * @queue afterwards, as it might have disappeared. You do not need to
 * hold the lock to call this function.
 **/
void
async_jitter_queue_unref (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  if (g_atomic_int_dec_and_test (&queue->ref_count)) {
    g_return_if_fail (queue->waiting_threads == 0);
    g_mutex_free (queue->mutex);
    if (queue->cond)
      g_cond_free (queue->cond);
    g_queue_free (queue->queue);
    g_free (queue);
  }
}

/**
 * async_jitter_queue_lock:
 * @queue: a #AsyncJitterQueue.
 * 
 * Acquires the @queue's lock. After that you can only call the
 * <function>async_jitter_queue_*_unlocked()</function> function variants on that
 * @queue. Otherwise it will deadlock.
 **/
void
async_jitter_queue_lock (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  g_mutex_lock (queue->mutex);
}

/**
 * async_jitter_queue_unlock:
 * @queue: a #AsyncJitterQueue.
 * 
 * Releases the queue's lock.
 **/
void
async_jitter_queue_unlock (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  g_mutex_unlock (queue->mutex);
}

/**
 * async_jitter_queue_push:
 * @queue: a #AsyncJitterQueue.
 * @data: @data to push into the @queue.
 *
 * Pushes the @data into the @queue. @data must not be %NULL.
 **/
void
async_jitter_queue_push (AsyncJitterQueue * queue, gpointer data)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);
  g_return_if_fail (data);

  g_mutex_lock (queue->mutex);
  async_jitter_queue_push_unlocked (queue, data);
  g_mutex_unlock (queue->mutex);
}

/**
 * async_jitter_queue_push_unlocked:
 * @queue: a #AsyncJitterQueue.
 * @data: @data to push into the @queue.
 * 
 * Pushes the @data into the @queue. @data must not be %NULL. This
 * function must be called while holding the @queue's lock.
 **/
void
async_jitter_queue_push_unlocked (AsyncJitterQueue * queue, gpointer data)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);
  g_return_if_fail (data);

  g_queue_push_head (queue->queue, data);

  signal_waiting_threads (queue);
}

/**
 * async_jitter_queue_push_sorted:
 * @queue: a #AsyncJitterQueue
 * @data: the @data to push into the @queue
 * @func: the #GCompareDataFunc is used to sort @queue. This function
 *     is passed two elements of the @queue. The function should return
 *     0 if they are equal, a negative value if the first element
 *     should be higher in the @queue or a positive value if the first
 *     element should be lower in the @queue than the second element.
 * @user_data: user data passed to @func.
 * 
 * Inserts @data into @queue using @func to determine the new
 * position. 
 * 
 * This function requires that the @queue is sorted before pushing on
 * new elements.
 * 
 * This function will lock @queue before it sorts the queue and unlock
 * it when it is finished.
 * 
 * For an example of @func see async_jitter_queue_sort(). 
 *
 * Since: 2.10
 **/
gboolean
async_jitter_queue_push_sorted (AsyncJitterQueue * queue,
    gpointer data, GCompareDataFunc func, gpointer user_data)
{
  g_return_val_if_fail (queue != NULL, FALSE);
  gboolean ret;

  g_mutex_lock (queue->mutex);
  ret = async_jitter_queue_push_sorted_unlocked (queue, data, func, user_data);
  g_mutex_unlock (queue->mutex);

  return ret;
}

/**
 * async_jitter_queue_push_sorted_unlocked:
 * @queue: a #AsyncJitterQueue
 * @data: the @data to push into the @queue
 * @func: the #GCompareDataFunc is used to sort @queue. This function
 *     is passed two elements of the @queue. The function should return
 *     0 if they are equal, a negative value if the first element
 *     should be higher in the @queue or a positive value if the first
 *     element should be lower in the @queue than the second element.
 * @user_data: user data passed to @func.
 * 
 * Inserts @data into @queue using @func to determine the new
 * position.
 * 
 * This function requires that the @queue is sorted before pushing on
 * new elements.
 *
 * If @GCompareDataFunc returns 0, this function does not insert @data and
 * return FALSE.
 * 
 * This function is called while holding the @queue's lock.
 * 
 * For an example of @func see async_jitter_queue_sort(). 
 *
 * Since: 2.10
 **/
gboolean
async_jitter_queue_push_sorted_unlocked (AsyncJitterQueue * queue,
    gpointer data, GCompareDataFunc func, gpointer user_data)
{
  GList *list;
  gint func_ret = TRUE;

  g_return_val_if_fail (queue != NULL, FALSE);

  list = queue->queue->head;
  while (list && (func_ret = func (list->data, data, user_data)) < 0)
    list = list->next;

  if (func_ret == 0) {
    return FALSE;
  }
  if (list) {
    g_queue_insert_before (queue->queue, list, data);
  } else {
    g_queue_push_tail (queue->queue, data);
  }

  signal_waiting_threads (queue);
  return TRUE;
}

void
async_jitter_queue_insert_after_unlocked (AsyncJitterQueue * queue,
    GList * sibling, gpointer data)
{
  g_return_if_fail (queue != NULL);

  g_queue_insert_before (queue->queue, sibling, data);

  signal_waiting_threads (queue);
}

static gpointer
async_jitter_queue_pop_intern_unlocked (AsyncJitterQueue * queue)
{
  gpointer retval;
  GstBuffer *tail_buffer = NULL;

  if (queue->pop_flushing)
    return NULL;

  while (queue->pop_blocking) {
    queue->waiting_threads++;
    g_cond_wait (queue->cond, queue->mutex);
    queue->waiting_threads--;
    if (queue->pop_flushing)
      return NULL;
  }

  if (async_jitter_queue_length_ts_units_unlocked (queue) <=
      queue->low_threshold * queue->max_queue_length
      && queue->pops_remaining == 0) {
    if (!queue->buffering) {
      queue->buffering = TRUE;
      queue->pops_remaining = queue->queue->length;
    } else {
      while (!g_queue_peek_tail (queue->queue) || queue->pop_blocking) {
        queue->waiting_threads++;
        g_cond_wait (queue->cond, queue->mutex);
        queue->waiting_threads--;
        if (queue->pop_flushing)
          return NULL;
      }
    }
  }

  retval = g_queue_pop_tail (queue->queue);
  if (queue->pops_remaining)
    queue->pops_remaining--;

  tail_buffer = g_queue_peek_tail (queue->queue);
  if (tail_buffer) {
    if (!GST_IS_BUFFER (tail_buffer) || !GST_IS_BUFFER (retval)) {
      queue->tail_buffer_duration = 0;
    } else if (gst_rtp_buffer_get_seq (tail_buffer)
        - gst_rtp_buffer_get_seq (retval) == 1) {
      queue->tail_buffer_duration =
          calculate_ts_diff (gst_rtp_buffer_get_timestamp (tail_buffer),
          gst_rtp_buffer_get_timestamp (retval));
    } else {
      /* There is a sequence number gap -> we can't calculate the duration
       * let's just set it to 0 */
      queue->tail_buffer_duration = 0;
    }
  }

  g_assert (retval);

  return retval;
}

/**
 * async_jitter_queue_pop:
 * @queue: a #AsyncJitterQueue.
 * 
 * Pops data from the @queue. This function blocks until data become
 * available. If pop is disabled, tis function return NULL.
 *
 * Return value: data from the queue.
 **/
gpointer
async_jitter_queue_pop (AsyncJitterQueue * queue)
{
  gpointer retval;

  g_return_val_if_fail (queue, NULL);
  g_return_val_if_fail (g_atomic_int_get (&queue->ref_count) > 0, NULL);

  g_mutex_lock (queue->mutex);
  retval = async_jitter_queue_pop_intern_unlocked (queue);
  g_mutex_unlock (queue->mutex);

  return retval;
}

/**
 * async_jitter_queue_pop_unlocked:
 * @queue: a #AsyncJitterQueue.
 * 
 * Pops data from the @queue. This function blocks until data become
 * available. This function must be called while holding the @queue's
 * lock.
 *
 * Return value: data from the queue.
 **/
gpointer
async_jitter_queue_pop_unlocked (AsyncJitterQueue * queue)
{
  g_return_val_if_fail (queue, NULL);
  g_return_val_if_fail (g_atomic_int_get (&queue->ref_count) > 0, NULL);

  return async_jitter_queue_pop_intern_unlocked (queue);
}

/**
 * async_jitter_queue_length:
 * @queue: a #AsyncJitterQueue.
 * 
 * Returns the length of the queue
 * Return value: the length of the @queue.
 **/
gint
async_jitter_queue_length (AsyncJitterQueue * queue)
{
  gint retval;

  g_return_val_if_fail (queue, 0);
  g_return_val_if_fail (g_atomic_int_get (&queue->ref_count) > 0, 0);

  g_mutex_lock (queue->mutex);
  retval = queue->queue->length;
  g_mutex_unlock (queue->mutex);

  return retval;
}

/**
 * async_jitter_queue_length_unlocked:
 * @queue: a #AsyncJitterQueue.
 *
 * Returns the length of the queue.
 *
 * Return value: the length of the @queue.
 **/
gint
async_jitter_queue_length_unlocked (AsyncJitterQueue * queue)
{
  g_return_val_if_fail (queue, 0);
  g_return_val_if_fail (g_atomic_int_get (&queue->ref_count) > 0, 0);

  return queue->queue->length;
}

/**
 * async_jitter_queue_set_flushing_unlocked:
 * @queue: a #AsyncJitterQueue.
 * @free_func: a function to call to free the elements
 * @user_data: user data passed to @free_func
 * 
 * This function is used to set/unset flushing. If flushing is set any
 * waiting/blocked pops will be unblocked. Any subsequent calls to pop will
 * return NULL. Flushing is set by default.
 */
void
async_jitter_queue_set_flushing_unlocked (AsyncJitterQueue * queue,
    GFunc free_func, gpointer user_data)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->pop_flushing = TRUE;
  /* let's unblock any remaining pops */
  if (queue->waiting_threads > 0)
    g_cond_broadcast (queue->cond);
  /* free data from queue */
  g_queue_foreach (queue->queue, free_func, user_data);
}

/**
 * async_jitter_queue_unset_flushing_unlocked:
 * @queue: a #AsyncJitterQueue.
 * @free_func: a function to call to free the elements
 * @user_data: user data passed to @free_func
 * 
 * This function is used to set/unset flushing. If flushing is set any
 * waiting/blocked pops will be unblocked. Any subsequent calls to pop will
 * return NULL. Flushing is set by default.
 */
void
async_jitter_queue_unset_flushing_unlocked (AsyncJitterQueue * queue)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->pop_flushing = FALSE;
  /* let's unblock any remaining pops */
  if (queue->waiting_threads > 0)
    g_cond_broadcast (queue->cond);
}

/**
 * async_jitter_queue_set_blocking_unlocked:
 * @queue: a #AsyncJitterQueue.
 * @enabled: a boolean to enable/disable blocking
 * 
 * This function is used to enable/disable blocking. If blocking is enabled any
 * pops will be blocked until the queue is unblocked. The queue is blocked by
 * default.
 */
void
async_jitter_queue_set_blocking_unlocked (AsyncJitterQueue * queue,
    gboolean blocking)
{
  g_return_if_fail (queue);
  g_return_if_fail (g_atomic_int_get (&queue->ref_count) > 0);

  queue->pop_blocking = blocking;
  /* let's unblock any remaining pops */
  if (queue->waiting_threads > 0)
    g_cond_broadcast (queue->cond);
}
