/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gsttask.c: Streaming tasks
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

#include "gst_private.h"

#include "gstinfo.h"
#include "gsttask.h"

GST_DEBUG_CATEGORY (task_debug);
#define GST_CAT_DEFAULT (task_debug)

static void gst_task_class_init (GstTaskClass * klass);
static void gst_task_init (GstTask * task);
static void gst_task_finalize (GObject * object);

static void gst_task_func (GstTask * task, GstTaskClass * tclass);

static GstObjectClass *parent_class = NULL;

static GStaticMutex pool_lock = G_STATIC_MUTEX_INIT;

GType
gst_task_get_type (void)
{
  static GType _gst_task_type = 0;

  if (!_gst_task_type) {
    static const GTypeInfo task_info = {
      sizeof (GstTaskClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_task_class_init,
      NULL,
      NULL,
      sizeof (GstTask),
      0,
      (GInstanceInitFunc) gst_task_init,
      NULL
    };

    _gst_task_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstTask", &task_info, 0);

    GST_DEBUG_CATEGORY_INIT (task_debug, "task", 0, "Processing tasks");
  }
  return _gst_task_type;
}

static void
gst_task_class_init (GstTaskClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_task_finalize);

  klass->pool = g_thread_pool_new (
      (GFunc) gst_task_func, klass, -1, FALSE, NULL);
}

static void
gst_task_init (GstTask * task)
{
  task->running = FALSE;
  task->lock = NULL;
  task->cond = g_cond_new ();
  task->state = GST_TASK_STOPPED;
}

static void
gst_task_finalize (GObject * object)
{
  GstTask *task = GST_TASK (object);

  GST_DEBUG ("task %p finalize", task);

  g_cond_free (task->cond);
  task->cond = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_task_func (GstTask * task, GstTaskClass * tclass)
{
  GStaticRecMutex *lock;

  GST_DEBUG ("Entering task %p, thread %p", task, g_thread_self ());

  /* we have to grab the lock to get the mutex. We also
   * mark our state running so that nobody can mess with
   * the mutex. */
  GST_LOCK (task);
  if (task->state == GST_TASK_STOPPED)
    goto exit;
  lock = GST_TASK_GET_LOCK (task);
  task->running = TRUE;
  GST_UNLOCK (task);

  /* locking order is TASK_LOCK, LOCK */
  g_static_rec_mutex_lock (lock);
  GST_LOCK (task);
  while (G_LIKELY (task->state != GST_TASK_STOPPED)) {
    while (G_UNLIKELY (task->state == GST_TASK_PAUSED)) {
      gint t;

      t = g_static_rec_mutex_unlock_full (lock);
      if (t <= 0) {
        g_warning ("wrong STREAM_LOCK count %d", t);
      }
      GST_TASK_SIGNAL (task);
      GST_TASK_WAIT (task);
      GST_UNLOCK (task);
      /* locking order.. */
      if (t > 0)
        g_static_rec_mutex_lock_full (lock, t);

      GST_LOCK (task);
      if (task->state == GST_TASK_STOPPED)
        goto done;
    }
    GST_UNLOCK (task);

    task->func (task->data);

    GST_LOCK (task);
  }
done:
  GST_UNLOCK (task);
  g_static_rec_mutex_unlock (lock);

  /* now we allow messing with the lock again */
  GST_LOCK (task);
  task->running = FALSE;
exit:
  GST_TASK_SIGNAL (task);
  GST_UNLOCK (task);

  GST_DEBUG ("Exit task %p, thread %p", task, g_thread_self ());

  gst_object_unref (task);
}

/**
 * gst_task_cleanup_all:
 *
 * Wait for all tasks to be stopped. This is mainly used internally
 * to ensure proper cleanup of internal datastructures in testsuites.
 *
 * MT safe.
 */
void
gst_task_cleanup_all (void)
{
  GstTaskClass *klass;

  if ((klass = g_type_class_peek (GST_TYPE_TASK))) {
    g_static_mutex_lock (&pool_lock);
    if (klass->pool) {
      /* Shut down all the threads, we still process the ones scheduled
       * because the unref happens in the thread function.
       * Also wait for currently running ones to finish. */
      g_thread_pool_free (klass->pool, FALSE, TRUE);
      /* create new pool, so we can still do something after this
       * call. */
      klass->pool = g_thread_pool_new (
          (GFunc) gst_task_func, klass, -1, FALSE, NULL);
    }
    g_static_mutex_unlock (&pool_lock);
  }
}

/**
 * gst_task_create:
 * @func: The #GstTaskFunction to use
 * @data: User data to pass to @func
 *
 * Create a new Task that will repeadedly call the provided @func
 * with @data as a parameter. Typically the task will run in
 * a new thread.
 *
 * Returns: A new #GstTask.
 *
 * MT safe.
 */
GstTask *
gst_task_create (GstTaskFunction func, gpointer data)
{
  GstTask *task;

  task = g_object_new (GST_TYPE_TASK, NULL);
  task->func = func;
  task->data = data;

  GST_DEBUG ("Created task %p", task);

  return task;
}

/**
 * gst_task_set_lock:
 * @task: The #GstTask to use
 * @mutex: The GMutex to use
 *
 * Set the mutex used by the task.
 *
 * MT safe.
 */
void
gst_task_set_lock (GstTask * task, GStaticRecMutex * mutex)
{
  GST_LOCK (task);
  if (task->running)
    goto is_running;
  GST_TASK_GET_LOCK (task) = mutex;
  GST_UNLOCK (task);

  return;

  /* ERRORS */
is_running:
  {
    g_warning ("cannot call set_lock on a running task");
    GST_UNLOCK (task);
  }
}


/**
 * gst_task_get_state:
 * @task: The #GstTask to query
 *
 * Get the current state of the task.
 *
 * Returns: The #GstTaskState of the task
 *
 * MT safe.
 */
GstTaskState
gst_task_get_state (GstTask * task)
{
  GstTaskState result;

  g_return_val_if_fail (GST_IS_TASK (task), GST_TASK_STOPPED);

  GST_LOCK (task);
  result = task->state;
  GST_UNLOCK (task);

  return result;
}

/**
 * gst_task_start:
 * @task: The #GstTask to start
 *
 * Starts @task.
 *
 * Returns: TRUE if the task could be started.
 *
 * MT safe.
 */
gboolean
gst_task_start (GstTask * task)
{
  GstTaskState old;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  GST_DEBUG_OBJECT (task, "Starting task %p", task);

  GST_LOCK (task);
  if (G_UNLIKELY (GST_TASK_GET_LOCK (task) == NULL))
    goto no_lock;

  old = task->state;
  task->state = GST_TASK_STARTED;
  switch (old) {
    case GST_TASK_STOPPED:
    {
      GstTaskClass *tclass;

      tclass = GST_TASK_GET_CLASS (task);

      /* new task, push on threadpool. We ref before so
       * that it remains alive while on the threadpool. */
      gst_object_ref (task);
      g_static_mutex_lock (&pool_lock);
      g_thread_pool_push (tclass->pool, task, NULL);
      g_static_mutex_unlock (&pool_lock);
      break;
    }
    case GST_TASK_PAUSED:
      /* PAUSE to PLAY, signal */
      GST_TASK_SIGNAL (task);
      break;
    case GST_TASK_STARTED:
      /* was OK */
      break;
  }
  GST_UNLOCK (task);

  return TRUE;

  /* ERRORS */
no_lock:
  {
    g_warning ("starting task without a lock");
    return FALSE;
  }
}

/**
 * gst_task_stop:
 * @task: The #GstTask to stop
 *
 * Stops @task.
 *
 * Returns: TRUE if the task could be stopped.
 *
 * MT safe.
 */
gboolean
gst_task_stop (GstTask * task)
{
  GstTaskClass *tclass;
  GstTaskState old;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  GST_DEBUG_OBJECT (task, "Stopping task %p", task);

  GST_LOCK (task);
  old = task->state;
  task->state = GST_TASK_STOPPED;
  switch (old) {
    case GST_TASK_STOPPED:
      break;
    case GST_TASK_PAUSED:
      GST_TASK_SIGNAL (task);
      break;
    case GST_TASK_STARTED:
      break;
  }
  GST_UNLOCK (task);

  return TRUE;
}

/**
 * gst_task_pause:
 * @task: The #GstTask to pause
 *
 * Pauses @task.
 *
 * Returns: TRUE if the task could be paused.
 *
 * MT safe.
 */
gboolean
gst_task_pause (GstTask * task)
{
  GstTaskState old;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  GST_DEBUG_OBJECT (task, "Pausing task %p", task);

  GST_LOCK (task);
  old = task->state;
  task->state = GST_TASK_PAUSED;
  switch (old) {
    case GST_TASK_STOPPED:
    {
      GstTaskClass *tclass;

      tclass = GST_TASK_GET_CLASS (task);

      gst_object_ref (task);
      g_static_mutex_lock (&pool_lock);
      g_thread_pool_push (tclass->pool, task, NULL);
      g_static_mutex_unlock (&pool_lock);
      break;
    }
    case GST_TASK_PAUSED:
      break;
    case GST_TASK_STARTED:
      break;
  }
  GST_UNLOCK (task);

  return TRUE;
}

/**
 * gst_task_join:
 * @task: The #GstTask to join
 *
 * Joins @task. After this call, it is safe to unref the task
 * and clean up the lock set with #gst_task_set_lock().
 *
 * The task will automatically be stopped with this call.
 *
 * This function cannot be called from within a task function.
 *
 * Returns: TRUE if the task could be joined.
 *
 * MT safe.
 */
gboolean
gst_task_join (GstTask * task)
{
  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  GST_DEBUG_OBJECT (task, "Joining task %p", task);

  GST_LOCK (task);
  task->state = GST_TASK_STOPPED;
  GST_TASK_SIGNAL (task);
  while (task->running)
    GST_TASK_WAIT (task);
  GST_UNLOCK (task);

  return TRUE;
}
