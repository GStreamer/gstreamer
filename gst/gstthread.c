/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstthread.c: Threaded container object
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

#include "gstthread.h"
#include "gstmarshal.h"
#include "gstscheduler.h"
#include "gstinfo.h"

#define GST_CAT_DEFAULT GST_CAT_THREAD
#define STACK_SIZE 0x200000

static GstElementDetails gst_thread_details =
GST_ELEMENT_DETAILS ("Threaded container",
    "Generic/Bin",
    "Container that creates/manages a thread",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Benjamin Otte <in7y118@informatik.uni-hamburg.de");

/* Thread signals and args */
enum
{
  SHUTDOWN,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  SPINUP = 0,
  STATECHANGE,
  STARTUP
};

enum
{
  ARG_0,
  ARG_PRIORITY
};


static void gst_thread_base_init (gpointer g_class);
static void gst_thread_class_init (gpointer g_class, gpointer class_data);
static void gst_thread_init (GTypeInstance * instance, gpointer g_class);

static void gst_thread_dispose (GObject * object);

static void gst_thread_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_thread_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_thread_change_state (GstElement * element);
static void gst_thread_child_state_change (GstBin * bin,
    GstElementState oldstate, GstElementState newstate, GstElement * element);

static void gst_thread_catch (GstThread * thread);
static void gst_thread_release (GstThread * thread);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_thread_save_thyself (GstObject * object,
    xmlNodePtr parent);
static void gst_thread_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

static void *gst_thread_main_loop (void *arg);

#define GST_TYPE_THREAD_PRIORITY (gst_thread_priority_get_type())
static GType
gst_thread_priority_get_type (void)
{
  static GType thread_priority_type = 0;
  static GEnumValue thread_priority[] = {
    {G_THREAD_PRIORITY_LOW, "LOW", "Low Priority Scheduling"},
    {G_THREAD_PRIORITY_NORMAL, "NORMAL", "Normal Scheduling"},
    {G_THREAD_PRIORITY_HIGH, "HIGH", "High Priority Scheduling"},
    {G_THREAD_PRIORITY_URGENT, "URGENT", "Urgent Scheduling"},
    {0, NULL, NULL},
  };

  if (!thread_priority_type) {
    thread_priority_type =
        g_enum_register_static ("GstThreadPriority", thread_priority);
  }
  return thread_priority_type;
}

static GstBinClass *parent_class = NULL;
static guint gst_thread_signals[LAST_SIGNAL] = { 0 };
GPrivate *gst_thread_current;

GType
gst_thread_get_type (void)
{
  static GType thread_type = 0;

  if (!thread_type) {
    static const GTypeInfo thread_info = {
      sizeof (GstThreadClass),
      gst_thread_base_init,
      NULL,
      gst_thread_class_init,
      NULL,
      NULL,
      sizeof (GstThread),
      0,
      gst_thread_init,
      NULL
    };

    thread_type = g_type_register_static (GST_TYPE_BIN, "GstThread",
        &thread_info, 0);
  }
  return thread_type;
}

static void
gst_thread_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_thread_details);
}
static void
do_nothing (gpointer hi)
{
}
static void
gst_thread_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);

#ifndef GST_DISABLE_LOADSAVE
  GstObjectClass *gstobject_class = GST_OBJECT_CLASS (g_class);
#endif
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstBinClass *gstbin_class = GST_BIN_CLASS (g_class);
  GstThreadClass *klass = GST_THREAD_CLASS (g_class);

  /* setup gst_thread_current */
  gst_thread_current = g_private_new (do_nothing);

  parent_class = g_type_class_peek_parent (g_class);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PRIORITY,
      g_param_spec_enum ("priority", "Scheduling Policy",
          "The scheduling priority of the thread", GST_TYPE_THREAD_PRIORITY,
          G_THREAD_PRIORITY_NORMAL, G_PARAM_READWRITE));

  gst_thread_signals[SHUTDOWN] =
      g_signal_new ("shutdown", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstThreadClass, shutdown), NULL, NULL,
      gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose = gst_thread_dispose;

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_thread_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_thread_restore_thyself);
#endif

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_thread_change_state);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_thread_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_thread_get_property);

  gstbin_class->child_state_change =
      GST_DEBUG_FUNCPTR (gst_thread_child_state_change);
}

static void
gst_thread_init (GTypeInstance * instance, gpointer g_class)
{
  GstScheduler *scheduler;
  GstThread *thread = GST_THREAD (instance);

  GST_DEBUG ("initializing thread");

  /* threads are managing bins and iterate themselves */
  /* CR1: the GstBin code checks these flags */
  GST_FLAG_SET (thread, GST_BIN_FLAG_MANAGER);
  GST_FLAG_SET (thread, GST_BIN_SELF_SCHEDULABLE);

  scheduler = gst_scheduler_factory_make (NULL, GST_ELEMENT (thread));
  g_assert (scheduler);

  thread->lock = g_mutex_new ();
  thread->cond = g_cond_new ();

  thread->thread_id = (GThread *) NULL; /* set in NULL -> READY */
  thread->priority = G_THREAD_PRIORITY_NORMAL;
}

static void
gst_thread_dispose (GObject * object)
{
  GstThread *thread = GST_THREAD (object);

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "GstThread: dispose");

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_assert (GST_STATE (thread) == GST_STATE_NULL);

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "GstThread: dispose, freeing locks");

  g_mutex_free (thread->lock);
  g_cond_free (thread->cond);

  gst_object_replace ((GstObject **) & GST_ELEMENT_SCHED (thread), NULL);
}

/**
 * gst_thread_set_priority:
 * @thread: the thread to change 
 * @priority: the new priority for the thread
 *
 * change the thread's priority
 */
void
gst_thread_set_priority (GstThread * thread, GThreadPriority priority)
{
  g_return_if_fail (GST_IS_THREAD (thread));

  thread->priority = priority;
}

static void
gst_thread_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstThread *thread;

  thread = GST_THREAD (object);

  switch (prop_id) {
    case ARG_PRIORITY:
      thread->priority = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_thread_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstThread *thread;

  thread = GST_THREAD (object);

  switch (prop_id) {
    case ARG_PRIORITY:
      g_value_set_enum (value, thread->priority);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * gst_thread_new:
 * @name: the name of the thread
 *
 * Create a new thread with the given name.
 *
 * Returns: The new thread
 */
GstElement *
gst_thread_new (const gchar * name)
{
  return gst_element_factory_make ("thread", name);
}

/**
 * gst_thread_get_current:
 *
 * Gets the current GstThread.
 *
 * Returns: The current GstThread or NULL if you are not running inside a 
 *          #GstThread.
 */
GstThread *
gst_thread_get_current (void)
{
  return (GstThread *) g_private_get (gst_thread_current);
}

static inline void
gst_thread_release_children_locks (GstThread * thread)
{
  GstRealPad *peer = NULL;
  GstElement *peerelement;
  GList *elements = (GList *) gst_bin_get_list (GST_BIN (thread));

  while (elements) {
    GstElement *element = GST_ELEMENT (elements->data);
    GList *pads;

    g_assert (element);
    GST_DEBUG_OBJECT (thread, "waking element \"%s\"",
        GST_ELEMENT_NAME (element));
    elements = g_list_next (elements);

    if (!gst_element_release_locks (element))
      g_warning ("element %s could not release locks",
          GST_ELEMENT_NAME (element));

    pads = GST_ELEMENT_PADS (element);

    while (pads) {
      if (GST_PAD_PEER (pads->data)) {
        peer = GST_REAL_PAD (GST_PAD_PEER (pads->data));
        pads = g_list_next (pads);
      } else {
        pads = g_list_next (pads);
        continue;
      }

      if (!peer)
        continue;

      peerelement = GST_PAD_PARENT (peer);
      if (!peerelement)
        continue;               /* FIXME: deal with case where there's no peer */

      if (GST_ELEMENT_SCHED (peerelement) != GST_ELEMENT_SCHED (thread)) {
        GST_LOG_OBJECT (thread, "element \"%s\" has pad cross sched boundary",
            GST_ELEMENT_NAME (element));
        GST_LOG_OBJECT (thread, "waking element \"%s\"",
            GST_ELEMENT_NAME (peerelement));
        if (!gst_element_release_locks (peerelement))
          g_warning ("element %s could not release locks",
              GST_ELEMENT_NAME (peerelement));
      }
    }
  }
}

/* stops the main thread, if there is one and grabs the thread's mutex */
static void
gst_thread_catch (GstThread * thread)
{
  gboolean wait;

  if (thread == gst_thread_get_current ()) {
    /* we're trying to catch ourself */
    if (!GST_FLAG_IS_SET (thread, GST_THREAD_MUTEX_LOCKED)) {
      GST_DEBUG_OBJECT (thread, "catching itself, grabbing lock");
      g_mutex_lock (thread->lock);
      GST_FLAG_SET (thread, GST_THREAD_MUTEX_LOCKED);
    }
    GST_DEBUG_OBJECT (thread, "catching itself");
    GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
  } else {
    GST_DEBUG_OBJECT (thread, "catching thread, grabbing lock");
    /* another thread is trying to catch us */
    g_mutex_lock (thread->lock);
    wait = !GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING);
    while (!wait) {
      GTimeVal tv;

      GST_LOG_OBJECT (thread, "catching thread...");
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      g_cond_signal (thread->cond);
      gst_thread_release_children_locks (thread);
      g_get_current_time (&tv);
      g_time_val_add (&tv, 1000);       /* wait a millisecond to catch the thread */
      wait = g_cond_timed_wait (thread->cond, thread->lock, &tv);
    }
    GST_LOG_OBJECT (thread, "caught thread");
  }
  g_assert (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING));
}

static void
gst_thread_release (GstThread * thread)
{
  if (thread != gst_thread_get_current ()) {
    g_cond_signal (thread->cond);
    g_mutex_unlock (thread->lock);
  }
}

static GstElementStateReturn
gst_thread_change_state (GstElement * element)
{
  GstThread *thread;
  GstElementStateReturn ret;
  gint transition;

  g_return_val_if_fail (GST_IS_THREAD (element), GST_STATE_FAILURE);
  transition = GST_STATE_TRANSITION (element);

  thread = GST_THREAD (element);

  GST_DEBUG_OBJECT (element, "changing state from %s to %s",
      gst_element_state_get_name (GST_STATE (element)),
      gst_element_state_get_name (GST_STATE_PENDING (element)));

  gst_thread_catch (thread);

  /* FIXME: (or GStreamers ideas about "threading"): the element variables are
     commonly accessed by multiple threads at the same time (see bug #111146
     for an example) */
  if (transition != GST_STATE_TRANSITION (element)) {
    g_warning ("inconsistent state information, fix threading please");
  }

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      /* create the thread */
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);
      thread->thread_id = g_thread_create_full (gst_thread_main_loop,
          thread, STACK_SIZE, FALSE, TRUE, thread->priority, NULL);
      if (!thread->thread_id) {
        GST_ERROR_OBJECT (element, "g_thread_create_full failed");
        goto error_out;
      }
      GST_LOG_OBJECT (element, "GThread created");

      /* wait for it to 'spin up' */
      g_cond_wait (thread->cond, thread->lock);
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    {
      /* FIXME: recurse into sub-bins */
      GList *elements = (GList *) gst_bin_get_list (GST_BIN (thread));

      while (elements) {
        gst_element_enable_threadsafe_properties ((GstElement *) elements->
            data);
        elements = g_list_next (elements);
      }
      /* reset self to spinning */
      if (thread == gst_thread_get_current ())
        GST_FLAG_SET (thread, GST_THREAD_STATE_SPINNING);
      break;
    }
    case GST_STATE_PLAYING_TO_PAUSED:
    {
      GList *elements = (GList *) gst_bin_get_list (GST_BIN (thread));

      while (elements) {
        gst_element_disable_threadsafe_properties ((GstElement *) elements->
            data);
        elements = g_list_next (elements);
      }
      break;
    }
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      /* we can't join the threads here, because this could have been triggered
         by ourself (ouch) */
      GST_LOG_OBJECT (thread, "destroying GThread %p", thread->thread_id);
      GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
      thread->thread_id = NULL;
      if (thread == gst_thread_get_current ()) {
        /* or should we continue? */
        g_warning
            ("Thread %s is destroying itself. Function call will not return!",
            GST_ELEMENT_NAME (thread));
        gst_scheduler_reset (GST_ELEMENT_SCHED (thread));

        /* unlock and signal - we are out */
        gst_thread_release (thread);

        GST_INFO_OBJECT (thread, "GThread %p is exiting", g_thread_self ());

        g_signal_emit (G_OBJECT (thread), gst_thread_signals[SHUTDOWN], 0);

        g_thread_exit (NULL);
      }
      /* now wait for the thread to destroy itself */
      g_cond_signal (thread->cond);
      g_cond_wait (thread->cond, thread->lock);
      /* it should be dead now */
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT (thread));
  } else {
    ret = GST_STATE_SUCCESS;
  }

  gst_thread_release (thread);
  return ret;

error_out:
  GST_CAT_DEBUG (GST_CAT_STATES, "changing state from %s to %s failed for %s",
      gst_element_state_get_name (GST_STATE (element)),
      gst_element_state_get_name (GST_STATE_PENDING (element)),
      GST_ELEMENT_NAME (element));
  gst_thread_release (thread);
  return GST_STATE_FAILURE;
}

/* state changes work this way: We grab the lock and stop the thread from 
   spinning (via gst_thread_catch) - then we change the state. After that the
   thread may spin on. */
static void
gst_thread_child_state_change (GstBin * bin, GstElementState oldstate,
    GstElementState newstate, GstElement * element)
{
  GST_LOG_OBJECT (bin, "(from thread %s) child %s changed state from %s to %s",
      gst_thread_get_current ()? GST_ELEMENT_NAME (gst_thread_get_current ()) :
      "(none)", GST_ELEMENT_NAME (element),
      gst_element_state_get_name (oldstate),
      gst_element_state_get_name (newstate));
  if (parent_class->child_state_change)
    parent_class->child_state_change (bin, oldstate, newstate, element);
  /* We'll wake up the main thread now. Note that we can't lock the thread here,
     because we might be called from inside gst_thread_change_state when holding
     the lock. But this doesn't cause any problems. */
  if (newstate == GST_STATE_PLAYING)
    g_cond_signal (GST_THREAD (bin)->cond);
}

/**
 * gst_thread_main_loop:
 * @arg: the thread to start
 *
 * The main loop of the thread. The thread will iterate
 * while the state is GST_THREAD_STATE_SPINNING.
 */
static void *
gst_thread_main_loop (void *arg)
{
  GstThread *thread = NULL;
  gboolean status;
  GstScheduler *sched;

  thread = GST_THREAD (arg);
  g_mutex_lock (thread->lock);
  GST_LOG_OBJECT (thread, "Started main loop");

  /* initialize gst_thread_current */
  g_private_set (gst_thread_current, thread);

  /* set up the element's scheduler */
  gst_scheduler_setup (GST_ELEMENT_SCHED (thread));
  GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);

  g_cond_signal (thread->cond);
  while (!(GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING))) {
    if (GST_STATE (thread) == GST_STATE_PLAYING) {
      GST_FLAG_SET (thread, GST_THREAD_STATE_SPINNING);
      status = TRUE;
      GST_LOG_OBJECT (thread, "starting to iterate");
      while (status && GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
        g_mutex_unlock (thread->lock);
        status = gst_bin_iterate (GST_BIN (thread));
        if (!status)
          GST_DEBUG_OBJECT (thread, "iterate returned false");
        if (GST_FLAG_IS_SET (thread, GST_THREAD_MUTEX_LOCKED)) {
          GST_FLAG_UNSET (thread, GST_THREAD_MUTEX_LOCKED);
        } else {
          g_mutex_lock (thread->lock);
        }
      }
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
    }
    if (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING))
      break;
    GST_LOG_OBJECT (thread, "we're caught");
    g_cond_signal (thread->cond);
    g_cond_wait (thread->cond, thread->lock);
  }

  /* we need to destroy the scheduler here because it has mapped it's
   * stack into the threads stack space */
  sched = GST_ELEMENT_SCHED (thread);
  if (sched)
    gst_scheduler_reset (sched);

  /* must do that before releasing the lock - we might get disposed before being done */
  g_signal_emit (G_OBJECT (thread), gst_thread_signals[SHUTDOWN], 0);

  /* unlock and signal - we are out */

  GST_LOG_OBJECT (thread, "Thread %p exits main loop", g_thread_self ());
  g_cond_signal (thread->cond);
  g_mutex_unlock (thread->lock);
  /* don't assume the GstThread object exists anymore now */

  return NULL;
}

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr
gst_thread_save_thyself (GstObject * object, xmlNodePtr self)
{
  if (GST_OBJECT_CLASS (parent_class)->save_thyself)
    GST_OBJECT_CLASS (parent_class)->save_thyself (object, self);
  return NULL;
}

static void
gst_thread_restore_thyself (GstObject * object, xmlNodePtr self)
{
  GST_LOG_OBJECT (object, "restoring");

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */
