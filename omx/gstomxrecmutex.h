/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_REC_MUTEX_H__
#define __GST_OMX_REC_MUTEX_H__

#include <glib.h>

G_BEGIN_DECLS

/*
 * This is a recursive mutex implementation that serves a very specific
 * purpose; it is used to allow OpenMAX callbacks to be run in the context
 * of some OpenMAX function call while the calling function is holding the
 * master lock.
 *
 * According to the OpenMAX specification, we have 2 possible ways that
 * callbacks may be called. First, we have out-of-context calls, which means
 * that callbacks are called from a different thread at any point in time.
 * In this case, callbacks must take the appropriate lock to protect the data
 * that they are changing. Second, we have in-context calls, which means
 * that callbacks are called when we call some OpenMAX function, before this
 * function returns. In this case, we need to allow the callback to run
 * without taking any locks that the caller of the OpenMAX function is holding.
 *
 * This can be solved by a recusrive mutex. However, a normal GRecMutex is
 * not enough because it allows being locked multiple times only from
 * the same thread. Unfortunatly, what we see in Broadcom's implementation,
 * for instance, OpenMAX callbacks may be in-context, but from a different
 * thread. This is achieved like this:
 *
 * - OMX_Foo is called
 * - OMX_Foo waits on a condition
 * - The callback is executed in a different thread
 * - When the callback returns, its calling function
 *   signals the condition that OMX_Foo waits on
 * - OMX_Foo wakes up and returns
 *
 * This recursive mutex implementation attempts to fix this problem.
 * This is achieved like this: All functions lock this mutex normally
 * using gst_omx_rec_mutex_lock() / _unlock(). These functions
 * effectively lock the master mutex and they are identical in behavior
 * with g_mutex_lock() / _unlock(). When a function that has already
 * locked this mutex is about to call some OpenMAX function, it must
 * call gst_omx_rec_mutex_begin_recursion() to indicate that recursive
 * locking is now allowed, and similarly, call gst_omx_rec_mutex_end_recursion()
 * after the OpenMAX function has returned to indicate that no recursive
 * locking is allowed from this point on. Callbacks should lock the
 * mutex using gst_omx_rec_mutex_recursive_lock() / _recursive_unlock().
 * These two functions, depending on whether recursion is allowed
 * will take/release either the master lock or the recursion_lock.
 * Effectively, this allows callbacks to run in the context any code between
 * calls to gst_omx_rec_mutex_begin_recursion() / _end_recursion().
 *
 * Note that this doesn't prevent out-of-context callback executions
 * to be run at that point, but due to the fact that _end_recursion()
 * also locks the recursion_lock, it is at least guaranteed that they
 * will have finished their execution before _end_recursion() returns.
 */
typedef struct _GstOMXRecMutex GstOMXRecMutex;

struct _GstOMXRecMutex {
  /* The master lock */
  GMutex *lock;

  /* This lock is taken when recursing.
   * The master lock must always be taken before this one,
   * by the caller of _begin_recursion().
   */
  GMutex *recursion_lock;

  /* Indicates whether recursion is allowed.
   * When it is allowed, _recursive_lock() takes
   * the recursion_lock instead of the master lock.
   * This variable is protected by both locks.
   */
  volatile gboolean recursion_allowed;
};

void            gst_omx_rec_mutex_init (GstOMXRecMutex * mutex);
void            gst_omx_rec_mutex_clear (GstOMXRecMutex * mutex);

void            gst_omx_rec_mutex_lock (GstOMXRecMutex * mutex);
void            gst_omx_rec_mutex_unlock (GstOMXRecMutex * mutex);

void            gst_omx_rec_mutex_begin_recursion (GstOMXRecMutex * mutex);
void            gst_omx_rec_mutex_end_recursion (GstOMXRecMutex * mutex);

void            gst_omx_rec_mutex_recursive_lock (GstOMXRecMutex * mutex);
void            gst_omx_rec_mutex_recursive_unlock (GstOMXRecMutex * mutex);

G_END_DECLS

#endif /* __GST_OMX_REC_MUTEX_H__ */
