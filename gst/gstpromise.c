/* GStreamer
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
# include "config.h"
#endif

#include "gst_private.h"

#include "gstpromise.h"

#define GST_CAT_DEFAULT gst_promise_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/**
 * SECTION:gstpromise
 * @title: GstPromise
 * @short_description: a miniobject for future/promise-like functionality
 * @see_also:
 *
 * The #GstPromise object implements the container for values that may
 * be available later. i.e. a Future or a Promise in
 * <ulink url="https://en.wikipedia.org/wiki/Futures_and_promises">https://en.wikipedia.org/wiki/Futures_and_promises</ulink>
 * As with all Future/Promise-like functionality, there is the concept of the
 * producer of the value and the consumer of the value.
 *
 * A #GstPromise is created with gst_promise_new() by the consumer and passed
 * to the producer to avoid thread safety issues with the change callback.
 * A #GstPromise can be replied to with a value (or an error) by the producer
 * with gst_promise_reply(). gst_promise_interrupt() is for the consumer to
 * indicate to the producer that the value is not needed anymore and producing
 * that value can stop.  The @GST_PROMISE_RESULT_EXPIRED state set by a call
 * to gst_promise_expire() indicates to the consumer that a value will never
 * be produced and is intended to be called by a third party that implements
 * some notion of message handling such as #GstBus.
 * A callback can also be installed at #GstPromise creation for
 * result changes with gst_promise_new_with_change_func().
 * The change callback can be used to chain #GstPromises's together as in the
 * following example.
 * |[<!-- language="C" -->
 * const GstStructure *reply;
 * GstPromise *p;
 * if (gst_promise_wait (promise) != GST_PROMISE_RESULT_REPLIED)
 *   return; // interrupted or expired value
 * reply = gst_promise_get_reply (promise);
 * if (error in reply)
 *   return; // propagate error
 * p = gst_promise_new_with_change_func (another_promise_change_func, user_data, notify);
 * pass p to promise-using API
 * ]|
 *
 * Each #GstPromise starts out with a #GstPromiseResult of
 * %GST_PROMISE_RESULT_PENDING and only ever transitions once
 * into one of the other #GstPromiseResult's.
 *
 * In order to support multi-threaded code, gst_promise_reply(),
 * gst_promise_interrupt() and gst_promise_expire() may all be from
 * different threads with some restrictions and the final result of the promise
 * is whichever call is made first.  There are two restrictions on ordering:
 *
 * 1. That gst_promise_reply() and gst_promise_interrupt() cannot be called
 * after gst_promise_expire()
 * 2. That gst_promise_reply() and gst_promise_interrupt()
 * cannot be called twice.
 *
 * The change function set with gst_promise_new_with_change_func() is
 * called directly from either the gst_promise_reply(),
 * gst_promise_interrupt() or gst_promise_expire() and can be called
 * from an arbitrary thread.  #GstPromise using APIs can restrict this to
 * a single thread or a subset of threads but that is entirely up to the API
 * that uses #GstPromise.
 */

static const int immutable_structure_refcount = 2;

#define GST_PROMISE_REPLY(p)            (((GstPromiseImpl *)(p))->reply)
#define GST_PROMISE_RESULT(p)           (((GstPromiseImpl *)(p))->result)
#define GST_PROMISE_LOCK(p)             (&(((GstPromiseImpl *)(p))->lock))
#define GST_PROMISE_COND(p)             (&(((GstPromiseImpl *)(p))->cond))
#define GST_PROMISE_CHANGE_FUNC(p)      (((GstPromiseImpl *)(p))->change_func)
#define GST_PROMISE_CHANGE_DATA(p)      (((GstPromiseImpl *)(p))->user_data)
#define GST_PROMISE_CHANGE_NOTIFY(p)    (((GstPromiseImpl *)(p))->notify)

typedef struct
{
  GstPromise promise;

  GstPromiseResult result;
  GstStructure *reply;

  GMutex lock;
  GCond cond;
  GstPromiseChangeFunc change_func;
  gpointer user_data;
  GDestroyNotify notify;
} GstPromiseImpl;

/**
 * gst_promise_wait:
 * @promise: a #GstPromise
 *
 * Wait for @promise to move out of the %GST_PROMISE_RESULT_PENDING state.
 * If @promise is not in %GST_PROMISE_RESULT_PENDING then it will return
 * immediately with the current result.
 *
 * Returns: the result of the promise
 *
 * Since: 1.14
 */
GstPromiseResult
gst_promise_wait (GstPromise * promise)
{
  GstPromiseResult ret;

  g_return_val_if_fail (promise != NULL, GST_PROMISE_RESULT_EXPIRED);

  g_mutex_lock (GST_PROMISE_LOCK (promise));
  ret = GST_PROMISE_RESULT (promise);

  while (ret == GST_PROMISE_RESULT_PENDING) {
    GST_LOG ("%p waiting", promise);
    g_cond_wait (GST_PROMISE_COND (promise), GST_PROMISE_LOCK (promise));
    ret = GST_PROMISE_RESULT (promise);
  }
  GST_LOG ("%p waited", promise);

  g_mutex_unlock (GST_PROMISE_LOCK (promise));

  return ret;
}

/**
 * gst_promise_reply:
 * @promise: (allow-none): a #GstPromise
 * @s: (transfer full): a #GstStructure with the the reply contents
 *
 * Set a reply on @promise.  This will wake up any waiters with
 * %GST_PROMISE_RESULT_REPLIED.  Called by the producer of the value to
 * indicate success (or failure).
 *
 * If @promise has already been interrupted by the consumer, then this reply
 * is not visible to the consumer.
 *
 * Since: 1.14
 */
void
gst_promise_reply (GstPromise * promise, GstStructure * s)
{
  GstPromiseChangeFunc change_func = NULL;
  gpointer change_data = NULL;

  /* Caller requested that no reply is necessary */
  if (promise == NULL)
    return;

  g_mutex_lock (GST_PROMISE_LOCK (promise));
  if (GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_PENDING &&
      GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_INTERRUPTED) {
    GstPromiseResult result = GST_PROMISE_RESULT (promise);
    g_mutex_unlock (GST_PROMISE_LOCK (promise));
    g_return_if_fail (result == GST_PROMISE_RESULT_PENDING ||
        result == GST_PROMISE_RESULT_INTERRUPTED);
  }

  /* XXX: is this necessary and valid? */
  if (GST_PROMISE_REPLY (promise) && GST_PROMISE_REPLY (promise) != s)
    gst_structure_free (GST_PROMISE_REPLY (promise));

  /* Only reply iff we are currently in pending */
  if (GST_PROMISE_RESULT (promise) == GST_PROMISE_RESULT_PENDING) {
    if (s
        && !gst_structure_set_parent_refcount (s,
            (int *) &immutable_structure_refcount)) {
      g_critical ("Input structure has a parent already!");
      g_mutex_unlock (GST_PROMISE_LOCK (promise));
      return;
    }

    GST_PROMISE_RESULT (promise) = GST_PROMISE_RESULT_REPLIED;
    GST_LOG ("%p replied", promise);

    GST_PROMISE_REPLY (promise) = s;

    change_func = GST_PROMISE_CHANGE_FUNC (promise);
    change_data = GST_PROMISE_CHANGE_DATA (promise);
  } else {
    /* eat the value */
    if (s)
      gst_structure_free (s);
  }

  g_cond_broadcast (GST_PROMISE_COND (promise));
  g_mutex_unlock (GST_PROMISE_LOCK (promise));

  if (change_func)
    change_func (promise, change_data);
}

/**
 * gst_promise_get_reply:
 * @promise: a #GstPromise
 *
 * Retrieve the reply set on @promise.  @promise must be in
 * %GST_PROMISE_RESULT_REPLIED and the returned structure is owned by @promise
 *
 * Returns: (transfer none): The reply set on @promise
 *
 * Since: 1.14
 */
const GstStructure *
gst_promise_get_reply (GstPromise * promise)
{
  g_return_val_if_fail (promise != NULL, NULL);

  g_mutex_lock (GST_PROMISE_LOCK (promise));
  if (GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_REPLIED) {
    GstPromiseResult result = GST_PROMISE_RESULT (promise);
    g_mutex_unlock (GST_PROMISE_LOCK (promise));
    g_return_val_if_fail (result == GST_PROMISE_RESULT_REPLIED, NULL);
  }

  g_mutex_unlock (GST_PROMISE_LOCK (promise));

  return GST_PROMISE_REPLY (promise);
}

/**
 * gst_promise_interrupt:
 * @promise: a #GstPromise
 *
 * Interrupt waiting for a @promise.  This will wake up any waiters with
 * %GST_PROMISE_RESULT_INTERRUPTED.  Called when the consumer does not want
 * the value produced anymore.
 *
 * Since: 1.14
 */
void
gst_promise_interrupt (GstPromise * promise)
{
  GstPromiseChangeFunc change_func = NULL;
  gpointer change_data = NULL;

  g_return_if_fail (promise != NULL);

  g_mutex_lock (GST_PROMISE_LOCK (promise));
  if (GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_PENDING &&
      GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_REPLIED) {
    GstPromiseResult result = GST_PROMISE_RESULT (promise);
    g_mutex_unlock (GST_PROMISE_LOCK (promise));
    g_return_if_fail (result == GST_PROMISE_RESULT_PENDING ||
        result == GST_PROMISE_RESULT_REPLIED);
  }
  /* only interrupt if we are currently in pending */
  if (GST_PROMISE_RESULT (promise) == GST_PROMISE_RESULT_PENDING) {
    GST_PROMISE_RESULT (promise) = GST_PROMISE_RESULT_INTERRUPTED;
    g_cond_broadcast (GST_PROMISE_COND (promise));
    GST_LOG ("%p interrupted", promise);

    change_func = GST_PROMISE_CHANGE_FUNC (promise);
    change_data = GST_PROMISE_CHANGE_DATA (promise);
  }
  g_mutex_unlock (GST_PROMISE_LOCK (promise));

  if (change_func)
    change_func (promise, change_data);
}

/**
 * gst_promise_expire:
 * @promise: a #GstPromise
 *
 * Expire a @promise.  This will wake up any waiters with
 * %GST_PROMISE_RESULT_EXPIRED.  Called by a message loop when the parent
 * message is handled and/or destroyed (possibly unanswered).
 *
 * Since: 1.14
 */
void
gst_promise_expire (GstPromise * promise)
{
  GstPromiseChangeFunc change_func = NULL;
  gpointer change_data = NULL;

  g_return_if_fail (promise != NULL);

  g_mutex_lock (GST_PROMISE_LOCK (promise));
  if (GST_PROMISE_RESULT (promise) == GST_PROMISE_RESULT_PENDING) {
    GST_PROMISE_RESULT (promise) = GST_PROMISE_RESULT_EXPIRED;
    g_cond_broadcast (GST_PROMISE_COND (promise));
    GST_LOG ("%p expired", promise);

    change_func = GST_PROMISE_CHANGE_FUNC (promise);
    change_data = GST_PROMISE_CHANGE_DATA (promise);
    GST_PROMISE_CHANGE_FUNC (promise) = NULL;
    GST_PROMISE_CHANGE_DATA (promise) = NULL;
  }
  g_mutex_unlock (GST_PROMISE_LOCK (promise));

  if (change_func)
    change_func (promise, change_data);
}

static void
gst_promise_free (GstMiniObject * object)
{
  GstPromise *promise = (GstPromise *) object;

  /* the promise *must* be dealt with in some way before destruction */
  g_warn_if_fail (GST_PROMISE_RESULT (promise) != GST_PROMISE_RESULT_PENDING);

  if (GST_PROMISE_CHANGE_NOTIFY (promise))
    GST_PROMISE_CHANGE_NOTIFY (promise) (GST_PROMISE_CHANGE_DATA (promise));

  if (GST_PROMISE_REPLY (promise)) {
    gst_structure_set_parent_refcount (GST_PROMISE_REPLY (promise), NULL);
    gst_structure_free (GST_PROMISE_REPLY (promise));
  }
  g_mutex_clear (GST_PROMISE_LOCK (promise));
  g_cond_clear (GST_PROMISE_COND (promise));
  GST_LOG ("%p finalized", promise);

  g_free (promise);
}

static void
gst_promise_init (GstPromise * promise)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_promise_debug, "gstpromise", 0, "gstpromise");
    g_once_init_leave (&_init, 1);
  }

  gst_mini_object_init (GST_MINI_OBJECT (promise), 0, GST_TYPE_PROMISE, NULL,
      NULL, gst_promise_free);

  GST_PROMISE_REPLY (promise) = NULL;
  GST_PROMISE_RESULT (promise) = GST_PROMISE_RESULT_PENDING;
  g_mutex_init (GST_PROMISE_LOCK (promise));
  g_cond_init (GST_PROMISE_COND (promise));
}

/**
 * gst_promise_new:
 *
 * Returns: a new #GstPromise
 *
 * Since: 1.14
 */
GstPromise *
gst_promise_new (void)
{
  GstPromise *promise = GST_PROMISE (g_new0 (GstPromiseImpl, 1));

  gst_promise_init (promise);
  GST_LOG ("new promise %p", promise);

  return promise;
}

/**
 * gst_promise_new_with_change_func:
 * @func: (scope notified): a #GstPromiseChangeFunc to call
 * @user_data: (closure): argument to call @func with
 * @notify: notification function that @user_data is no longer needed
 *
 * @func will be called exactly once when transitioning out of
 * %GST_PROMISE_RESULT_PENDING into any of the other #GstPromiseResult
 * states.
 *
 * Returns: a new #GstPromise
 *
 * Since: 1.14
 */
GstPromise *
gst_promise_new_with_change_func (GstPromiseChangeFunc func, gpointer user_data,
    GDestroyNotify notify)
{
  GstPromise *promise = gst_promise_new ();

  GST_PROMISE_CHANGE_FUNC (promise) = func;
  GST_PROMISE_CHANGE_DATA (promise) = user_data;
  GST_PROMISE_CHANGE_NOTIFY (promise) = notify;

  return promise;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstPromise, gst_promise);
