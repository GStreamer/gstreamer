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
static void gst_task_dispose (GObject * object);

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
        g_type_register_static (GST_TYPE_OBJECT, "GstTask",
        &task_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_task_type;
}

static void
gst_task_class_init (GstTaskClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_task_dispose);
}

static void
gst_task_init (GstTask * task)
{
  task->lock = NULL;
  task->cond = g_cond_new ();
  task->state = GST_TASK_STOPPED;
}

static void
gst_task_dispose (GObject * object)
{
  GstTask *task = GST_TASK (object);

  GST_DEBUG ("task %p dispose", task);

  g_cond_free (task->cond);

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
  return NULL;
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
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  if (tclass->start)
    result = tclass->start (task);

  return result;
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
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  if (tclass->stop)
    result = tclass->stop (task);

  return result;
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
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_TASK (task), FALSE);

  tclass = GST_TASK_GET_CLASS (task);

  if (tclass->pause)
    result = tclass->pause (task);

  return result;
}
