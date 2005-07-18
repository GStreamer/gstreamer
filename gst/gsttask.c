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

static void gst_task_class_init (GstTaskClass * klass);
static void gst_task_init (GstTask * task);
static void gst_task_finalize (GObject * object);

static void gst_task_func (GstTask * task, GstTaskClass * tclass);

static GstObjectClass *parent_class = NULL;

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
  GST_DEBUG ("Entering task %p, thread %p", task, g_thread_self ());

  /* locking order is TASK_LOCK, LOCK */
  GST_TASK_LOCK (task);
  GST_LOCK (task);
  while (G_LIKELY (task->state != GST_TASK_STOPPED)) {
    while (G_UNLIKELY (task->state == GST_TASK_PAUSED)) {
      gint t;

      t = GST_TASK_UNLOCK_FULL (task);
      if (t <= 0) {
        g_warning ("wrong STREAM_LOCK count %d", t);
      }
      GST_TASK_SIGNAL (task);
      GST_TASK_WAIT (task);
      GST_UNLOCK (task);
      /* locking order.. */
      if (t > 0)
        GST_TASK_LOCK_FULL (task, t);

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
  GST_TASK_UNLOCK (task);

  GST_DEBUG ("Exit task %p, thread %p", task, g_thread_self ());

  gst_object_unref (task);
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
  task->lock = mutex;
  GST_UNLOCK (task);
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
  GstTaskClass *tclass;
  GstTaskState old;
  GStaticRecMutex *lock;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  GST_DEBUG_OBJECT (task, "Starting task %p", task);

  GST_LOCK (task);
  if (G_UNLIKELY (GST_TASK_GET_LOCK (task) == NULL)) {
    lock = g_new (GStaticRecMutex, 1);
    g_static_rec_mutex_init (lock);
    GST_TASK_GET_LOCK (task) = lock;
  }

  old = task->state;
  task->state = GST_TASK_STARTED;
  switch (old) {
    case GST_TASK_STOPPED:
      gst_object_ref (task);
      g_thread_pool_push (tclass->pool, task, NULL);
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
  GstTaskClass *tclass;
  GstTaskState old;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  GST_DEBUG_OBJECT (task, "Pausing task %p", task);

  GST_LOCK (task);
  old = task->state;
  task->state = GST_TASK_PAUSED;
  switch (old) {
    case GST_TASK_STOPPED:
      gst_object_ref (task);
      g_thread_pool_push (tclass->pool, task, NULL);
      break;
    case GST_TASK_PAUSED:
      break;
    case GST_TASK_STARTED:
      break;
  }
  GST_UNLOCK (task);

  return TRUE;
}
