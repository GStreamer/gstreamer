/* GStreamer2
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * threadscheduler.c: scheduler using threads 
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include "../gst-i18n-lib.h"

GST_DEBUG_CATEGORY_STATIC (debug_scheduler);
#define GST_CAT_DEFAULT debug_scheduler

#define GST_TYPE_THREAD_SCHEDULER \
  (gst_thread_scheduler_get_type ())
#define GST_THREAD_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THREAD_SCHEDULER,GstThreadScheduler))
#define GST_THREAD_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THREAD_SCHEDULER,GstThreadSchedulerClass))
#define GST_IS_THREAD_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THREAD_SCHEDULER))
#define GST_IS_THREAD_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THREAD_SCHEDULER))

#define SCHED(element) (GST_THREAD_SCHEDULER ((element)->sched))

GType gst_thread_scheduler_get_type (void);

typedef struct _GstThreadScheduler GstThreadScheduler;
typedef struct _GstThreadSchedulerClass GstThreadSchedulerClass;

struct _GstThreadScheduler
{
  GstScheduler scheduler;

  GThreadPool *pool;
};

struct _GstThreadSchedulerClass
{
  GstSchedulerClass scheduler_class;
};

#define ELEMENT_PRIVATE(element) GST_ELEMENT (element)->sched_private
#define PAD_PRIVATE(pad) (GST_REAL_PAD (pad))->sched_private

#define GST_TYPE_THREAD_SCHEDULER_TASK \
  (gst_thread_scheduler_task_get_type ())
#define GST_THREAD_SCHEDULER_TASK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THREAD_SCHEDULER_TASK,GstThreadSchedulerTask))
#define GST_THREAD_SCHEDULER_TASK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THREAD_SCHEDULER_TASK,GstThreadSchedulerTaskClass))
#define GST_IS_THREAD_SCHEDULER_TASK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THREAD_SCHEDULER_TASK))
#define GST_IS_THREAD_SCHEDULER_TASK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THREAD_SCHEDULER_TASK))

typedef struct _GstThreadSchedulerTask GstThreadSchedulerTask;
typedef struct _GstThreadSchedulerTaskClass GstThreadSchedulerTaskClass;

typedef enum
{
  STATE_STOPPED,
  STATE_STARTED,
  STATE_PAUSED
} TaskState;


struct _GstThreadSchedulerTask
{
  GstTask task;

  TaskState state;
  GMutex *lock;
  GCond *cond;

  GstTaskFunction func;
  gpointer data;
};

struct _GstThreadSchedulerTaskClass
{
  GstTaskClass parent_class;

};

static void gst_thread_scheduler_task_class_init (gpointer g_class,
    gpointer data);
static void gst_thread_scheduler_task_init (GstThreadSchedulerTask * object);

static gboolean gst_thread_scheduler_task_start (GstTask * task);
static gboolean gst_thread_scheduler_task_stop (GstTask * task);
static gboolean gst_thread_scheduler_task_pause (GstTask * task);

GType
gst_thread_scheduler_task_get_type (void)
{
  static GType object_type = 0;

  if (object_type == 0) {
    static const GTypeInfo object_info = {
      sizeof (GstThreadSchedulerTaskClass),
      NULL,
      NULL,
      gst_thread_scheduler_task_class_init,
      NULL,
      NULL,
      sizeof (GstThreadSchedulerTask),
      0,
      (GInstanceInitFunc) gst_thread_scheduler_task_init
    };

    object_type =
        g_type_register_static (GST_TYPE_TASK,
        "GstThreadSchedulerTask", &object_info, 0);
  }
  return object_type;
}

static void
gst_thread_scheduler_task_class_init (gpointer klass, gpointer class_data)
{
  GstTaskClass *task = GST_TASK_CLASS (klass);

  task->start = gst_thread_scheduler_task_start;
  task->stop = gst_thread_scheduler_task_stop;
  task->pause = gst_thread_scheduler_task_pause;
}

static void
gst_thread_scheduler_task_init (GstThreadSchedulerTask * task)
{
  task->state = STATE_STOPPED;
  task->lock = g_mutex_new ();
  task->cond = g_cond_new ();
}

static gboolean
gst_thread_scheduler_task_start (GstTask * task)
{
  GstThreadSchedulerTask *ttask = GST_THREAD_SCHEDULER_TASK (task);
  GstThreadScheduler *tsched =
      GST_THREAD_SCHEDULER (gst_object_get_parent (GST_OBJECT (task)));

  g_mutex_lock (ttask->lock);
  if (ttask->state == STATE_STOPPED) {
    ttask->state = STATE_STARTED;
    g_thread_pool_push (tsched->pool, task, NULL);
  } else {
    ttask->state = STATE_STARTED;
    g_cond_signal (ttask->cond);
  }
  g_mutex_unlock (ttask->lock);

  return TRUE;
}

static gboolean
gst_thread_scheduler_task_stop (GstTask * task)
{
  GstThreadSchedulerTask *ttask = GST_THREAD_SCHEDULER_TASK (task);

  g_mutex_lock (ttask->lock);
  if (ttask->state != STATE_STOPPED) {
    ttask->state = STATE_STOPPED;
    g_cond_signal (ttask->cond);
  }
  g_mutex_unlock (ttask->lock);
  return TRUE;
}

static gboolean
gst_thread_scheduler_task_pause (GstTask * task)
{
  GstThreadSchedulerTask *ttask = GST_THREAD_SCHEDULER_TASK (task);

  g_mutex_lock (ttask->lock);
  if (ttask->state != STATE_PAUSED) {
    ttask->state = STATE_PAUSED;
  }
  g_mutex_unlock (ttask->lock);
  return TRUE;
}

static void gst_thread_scheduler_class_init (gpointer g_class, gpointer data);
static void gst_thread_scheduler_init (GstThreadScheduler * object);

GType
gst_thread_scheduler_get_type (void)
{
  static GType object_type = 0;

  if (object_type == 0) {
    static const GTypeInfo object_info = {
      sizeof (GstThreadSchedulerClass),
      NULL,
      NULL,
      gst_thread_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstThreadScheduler),
      0,
      (GInstanceInitFunc) gst_thread_scheduler_init
    };

    object_type =
        g_type_register_static (GST_TYPE_SCHEDULER,
        "GstThreadScheduler", &object_info, 0);
  }
  return object_type;
}

static void gst_thread_scheduler_setup (GstScheduler * sched);
static void gst_thread_scheduler_reset (GstScheduler * sched);
static void gst_thread_scheduler_add_element (GstScheduler * sched,
    GstElement * element);
static void gst_thread_scheduler_remove_element (GstScheduler * sched,
    GstElement * element);
static GstTask *gst_thread_scheduler_create_task (GstScheduler * sched,
    GstTaskFunction func, gpointer data);
static void gst_thread_scheduler_show (GstScheduler * scheduler);

static void
gst_thread_scheduler_class_init (gpointer klass, gpointer class_data)
{
  GstSchedulerClass *scheduler = GST_SCHEDULER_CLASS (klass);

  scheduler->setup = gst_thread_scheduler_setup;
  scheduler->reset = gst_thread_scheduler_reset;
  scheduler->add_element = gst_thread_scheduler_add_element;
  scheduler->remove_element = gst_thread_scheduler_remove_element;
  scheduler->create_task = gst_thread_scheduler_create_task;
  scheduler->clock_wait = NULL;
  scheduler->show = gst_thread_scheduler_show;
}

static void
gst_thread_scheduler_func (GstThreadSchedulerTask * task,
    GstThreadScheduler * sched)
{
  gboolean res;

  gst_object_ref (GST_OBJECT (task));
  GST_DEBUG_OBJECT (sched, "Entering task %p, thread %p", task,
      g_thread_self ());
  g_mutex_lock (task->lock);
  while (G_LIKELY (task->state != STATE_STOPPED)) {
    if (task->state == STATE_PAUSED) {
      g_cond_wait (task->cond, task->lock);
      if (task->state == STATE_STOPPED)
        break;
    }
    g_mutex_unlock (task->lock);

    res = task->func (task->data);

    g_mutex_lock (task->lock);
    if (G_UNLIKELY (!res)) {
      task->state = STATE_STOPPED;
    }
  }
  g_mutex_unlock (task->lock);
  GST_DEBUG_OBJECT (sched, "Exit task %p, thread %p", task, g_thread_self ());
  gst_object_unref (GST_OBJECT (task));
}

static void
gst_thread_scheduler_init (GstThreadScheduler * scheduler)
{
  scheduler->pool = g_thread_pool_new (
      (GFunc) gst_thread_scheduler_func, scheduler, -1, FALSE, NULL);
}

static GstTask *
gst_thread_scheduler_create_task (GstScheduler * sched, GstTaskFunction func,
    gpointer data)
{
  GstThreadSchedulerTask *task;

  task =
      GST_THREAD_SCHEDULER_TASK (g_object_new (GST_TYPE_THREAD_SCHEDULER_TASK,
          NULL));
  gst_object_set_parent (GST_OBJECT (task), GST_OBJECT (sched));
  task->func = func;
  task->data = data;

  GST_DEBUG_OBJECT (sched, "Created task %p", task);

  return GST_TASK (task);
}

static void
gst_thread_scheduler_setup (GstScheduler * sched)
{
}

static void
gst_thread_scheduler_reset (GstScheduler * sched)
{
}

static void
gst_thread_scheduler_add_element (GstScheduler * scheduler,
    GstElement * element)
{
  g_print ("add element\n");
}

static void
gst_thread_scheduler_remove_element (GstScheduler * scheduler,
    GstElement * element)
{
  GstThreadSchedulerTask *task;;

  task = ELEMENT_PRIVATE (element);
  if (task) {
    g_object_unref (G_OBJECT (task));
    ELEMENT_PRIVATE (element) = NULL;;
  }
}

static void
gst_thread_scheduler_show (GstScheduler * scheduler)
{
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstSchedulerFactory *factory;

  GST_DEBUG_CATEGORY_INIT (debug_scheduler, "thread", 0, "thread scheduler");

  factory = gst_scheduler_factory_new ("thread",
      "A scheduler using threads", GST_TYPE_THREAD_SCHEDULER);
  if (factory == NULL)
    return FALSE;

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "gstthreadscheduler",
    "a thread scheduler", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE,
    GST_ORIGIN)
