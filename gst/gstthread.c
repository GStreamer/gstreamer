/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
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

#include <unistd.h>

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstthread.h"
#include "gstscheduler.h"


GstElementDetails gst_thread_details = {
  "Threaded container",
  "Bin",
  "Container that creates/manages a thread",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999, 2000",
};


/* Thread signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  SPINUP=0,
  STATECHANGE,
  STARTUP
};

enum {
  ARG_0,
  ARG_CREATE_THREAD,
};


static void			gst_thread_class_init		(GstThreadClass *klass);
static void			gst_thread_init			(GstThread *thread);

static void			gst_thread_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void			gst_thread_get_arg		(GtkObject *object, GtkArg *arg, guint id);

static GstElementStateReturn	gst_thread_change_state		(GstElement *element);

static xmlNodePtr		gst_thread_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_thread_restore_thyself	(GstObject *object, xmlNodePtr self);

static void			gst_thread_signal_thread	(GstThread *thread, guint syncflag,gboolean set);
static void			gst_thread_wait_thread		(GstThread *thread, guint syncflag,gboolean set);
static void			gst_thread_schedule_dummy	(GstBin *bin);

static void*			gst_thread_main_loop		(void *arg);

static GstBinClass *parent_class = NULL;
//static guint gst_thread_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_thread_get_type(void) {
  static GtkType thread_type = 0;

  if (!thread_type) {
    static const GtkTypeInfo thread_info = {
      "GstThread",
      sizeof(GstThread),
      sizeof(GstThreadClass),
      (GtkClassInitFunc)gst_thread_class_init,
      (GtkObjectInitFunc)gst_thread_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    thread_type = gtk_type_unique(GST_TYPE_BIN,&thread_info);
  }
  return thread_type;
}

static void
gst_thread_class_init (GstThreadClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gtkobject_class =	(GtkObjectClass*)klass;
  gstobject_class =	(GstObjectClass*)klass;
  gstelement_class =	(GstElementClass*)klass;
  gstbin_class =	(GstBinClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_BIN);

  gtk_object_add_arg_type ("GstThread::create_thread", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_CREATE_THREAD);

  gstobject_class->save_thyself =	gst_thread_save_thyself;
  gstobject_class->restore_thyself =	gst_thread_restore_thyself;

  gstelement_class->change_state =	gst_thread_change_state;

//  gstbin_class->schedule = gst_thread_schedule_dummy;

  gtkobject_class->set_arg = gst_thread_set_arg;
  gtkobject_class->get_arg = gst_thread_get_arg;

}

static void
gst_thread_init (GstThread *thread)
{
  GST_DEBUG (0,"initializing thread '%s'\n",GST_ELEMENT_NAME (thread));

  // we're a manager by default
  GST_FLAG_SET (thread, GST_BIN_FLAG_MANAGER);

  // default is to create a thread
  GST_FLAG_SET (thread, GST_THREAD_CREATE);

  thread->lock = g_mutex_new();
  thread->cond = g_cond_new();

  GST_ELEMENT_SCHED(thread) = gst_schedule_new(GST_ELEMENT(thread));
  g_print("thread's scheduler is %p\n",GST_ELEMENT_SCHED(thread));

//  gst_element_set_manager(GST_ELEMENT(thread),GST_ELEMENT(thread));
}

static void
gst_thread_schedule_dummy (GstBin *bin)
{
  g_return_if_fail (GST_IS_THREAD (bin));

  if (!GST_FLAG_IS_SET (GST_THREAD (bin), GST_THREAD_STATE_SPINNING))
    GST_INFO (GST_CAT_THREAD,"scheduling delayed until thread starts");
}

static void
gst_thread_set_arg (GtkObject *object,
		    GtkArg *arg,
		    guint id)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  switch(id) {
    case ARG_CREATE_THREAD:
      if (GTK_VALUE_BOOL (*arg)) {
        GST_INFO (GST_CAT_THREAD,"turning ON the creation of the thread");
        GST_FLAG_SET (object, GST_THREAD_CREATE);
//        GST_DEBUG (0,"flags are 0x%08x\n", GST_FLAGS (object));
      } else {
        GST_INFO (GST_CAT_THREAD,"gstthread: turning OFF the creation of the thread");
        GST_FLAG_UNSET (object, GST_THREAD_CREATE);
//        GST_DEBUG (0,"gstthread: flags are 0x%08x\n", GST_FLAGS (object));
      }
      break;
    default:
      break;
  }
}

static void
gst_thread_get_arg (GtkObject *object,
		    GtkArg *arg,
		    guint id)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  switch (id) {
    case ARG_CREATE_THREAD:
      GTK_VALUE_BOOL (*arg) = GST_FLAG_IS_SET (object, GST_THREAD_CREATE);
      break;
    default:
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
GstElement*
gst_thread_new (guchar *name)
{
  return gst_elementfactory_make ("thread", name);
}



static GstElementStateReturn
gst_thread_change_state (GstElement *element)
{
  GstThread *thread;
  gboolean stateset = GST_STATE_SUCCESS;
  gint transition;

  g_return_val_if_fail (GST_IS_THREAD(element), FALSE);
  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME(element));

  thread = GST_THREAD (element);

  transition = GST_STATE_TRANSITION (element);

  GST_INFO (GST_CAT_THREAD,"thread \"%s\" changing state to %s",
               GST_ELEMENT_NAME (GST_ELEMENT (element)),
	       _gst_print_statename(GST_STATE_PENDING (element)));

  //GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      // we want to prepare our internal state for doing the iterations
      GST_INFO (GST_CAT_THREAD, "preparing thread \"%s\" for iterations:",
               GST_ELEMENT_NAME (GST_ELEMENT (element)));

      // set the state to idle
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      // create the thread if that's what we're supposed to do
//      GST_INFO (GST_CAT_THREAD, "flags are 0x%08x", GST_FLAGS (thread));

      if (GST_FLAG_IS_SET (thread, GST_THREAD_CREATE)) {
        GST_DEBUG (GST_CAT_THREAD, "creating thread \"%s\"\n",
                   GST_ELEMENT_NAME (GST_ELEMENT (element)));

        // create the thread
        pthread_create (&thread->thread_id, NULL,
                        gst_thread_main_loop, thread);

        // wait for it to 'spin up'
        GST_DEBUG (GST_CAT_THREAD, "sync: waiting for spinup\n");
        gst_thread_wait_thread (thread,GST_THREAD_STATE_STARTED,TRUE);
        GST_DEBUG (GST_CAT_THREAD, "sync: thread claims to be up\n");
      } else {
        GST_INFO (GST_CAT_THREAD, "NOT starting thread \"%s\"",
                GST_ELEMENT_NAME (GST_ELEMENT (element)));

        // punt and change state on all the children
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
          stateset = GST_ELEMENT_CLASS (parent_class)->change_state (element);
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_READY_TO_PLAYING:
//      if (!stateset) return FALSE;
      GST_INFO (GST_CAT_THREAD, "starting thread \"%s\"",
              GST_ELEMENT_NAME (GST_ELEMENT (element)));

      GST_DEBUG(0,"sync: telling thread to start spinning\n");
      gst_thread_signal_thread (thread,GST_THREAD_STATE_SPINNING,TRUE);
      GST_DEBUG(0,"sync: done telling thread to start spinning\n");
      GST_INFO(GST_CAT_THREAD, "waiting for thread to start up");
      gst_thread_wait_thread (thread,GST_THREAD_STATE_ELEMENT_CHANGED,TRUE);
      g_mutex_lock(thread->lock);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_ELEMENT_CHANGED);
      g_mutex_unlock(thread->lock);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      GST_INFO (GST_CAT_THREAD,"pausing thread \"%s\"",
              GST_ELEMENT_NAME (GST_ELEMENT (element)));

      //GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      gst_thread_signal_thread (thread,GST_THREAD_STATE_SPINNING,FALSE);
      break;
    case GST_STATE_READY_TO_NULL:
      GST_INFO (GST_CAT_THREAD,"stopping thread \"%s\"",
              GST_ELEMENT_NAME (GST_ELEMENT (element)));

      //GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
      gst_thread_signal_thread (thread,GST_THREAD_STATE_REAPING,TRUE);

      pthread_join(thread->thread_id,NULL);

      GST_FLAG_UNSET(thread,GST_THREAD_STATE_REAPING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_STARTED);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_ELEMENT_CHANGED);

      break;
    default:
      break;
  }

  return stateset;
}

/**
 * gst_thread_main_loop:
 * @arg: the thread to start
 *
 * The main loop of the thread. The thread will iterate
 * while the state is GST_THREAD_STATE_SPINNING
 */
static void *
gst_thread_main_loop (void *arg)
{
  GstThread *thread = GST_THREAD (arg);
  gint stateset;

  GST_INFO (GST_CAT_THREAD,"thread \"%s\" is running with PID %d",
		  GST_ELEMENT_NAME (GST_ELEMENT (thread)), getpid ());

  // first we need to change the state of all the children
  GST_DEBUG (GST_CAT_THREAD,"thread started, setting children's state\n");
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

  // construct the plan and signal back
  GST_DEBUG (GST_CAT_THREAD,"creating plan for thread\n");
  if (GST_BIN_CLASS (parent_class)->schedule)
    GST_BIN_CLASS (parent_class)->schedule (GST_BIN (thread));

  GST_DEBUG(0, "sync: indicating spinup\n");
  gst_thread_signal_thread (thread,GST_THREAD_STATE_STARTED,TRUE);
  GST_DEBUG(0, "sync: done indicating spinup\n");

  GST_INFO (GST_CAT_THREAD,"sync: thread has signaled to parent at startup");

  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    if (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
      if (!gst_bin_iterate (GST_BIN (thread))) {
	/*g_mutex_lock(thread->lock);
	GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
	GST_DEBUG(0,"sync: removed spinning state due to failed iteration\n");
	g_mutex_unlock(thread->lock);*/
	gst_thread_wait_thread(thread,GST_THREAD_STATE_REAPING,TRUE);
      }
    }
    else {
      GST_DEBUG (0, "sync: thread \"%s\" waiting\n", GST_ELEMENT_NAME (GST_ELEMENT (thread)));
      gst_thread_wait_thread (thread,GST_THREAD_STATE_SPINNING,TRUE);
      GST_DEBUG (0, "sync: done waiting\n");

      // check for state change
      if (GST_STATE_PENDING(thread)) {
        // punt and change state on all the children
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
          stateset = GST_ELEMENT_CLASS (parent_class)->change_state (thread);
      }

      gst_thread_signal_thread (thread,GST_THREAD_STATE_ELEMENT_CHANGED,TRUE);
    }
  }

  GST_INFO (GST_CAT_THREAD, "gstthread: thread \"%s\" is stopped",
		  GST_ELEMENT_NAME (thread));
  return NULL;
}

// the set flag is to say whether it should set TRUE or FALSE
static void
gst_thread_signal_thread (GstThread *thread, guint syncflag, gboolean set)
{
  g_mutex_lock (thread->lock);
  GST_DEBUG (0,"sync: signaling thread setting %u to %d\n",syncflag,set);
  if (set)
    GST_FLAG_SET(thread,syncflag);
  else
    GST_FLAG_UNSET(thread,syncflag);
  g_cond_signal (thread->cond);
  g_mutex_unlock (thread->lock);
  GST_DEBUG (0,"sync: done signaling thread\n");
}

// the set flag is to see what flag to wait for
static void
gst_thread_wait_thread (GstThread *thread, guint syncflag, gboolean set)
{
//  if (!thread->signaling) {
    g_mutex_lock (thread->lock);
    GST_DEBUG (0,"sync: waiting for thread for %u to be set %d\n",
	       syncflag,set);
    if (GST_FLAG_IS_SET(thread,syncflag)!=set) {
      g_cond_wait (thread->cond, thread->lock);
    }
    g_mutex_unlock (thread->lock);
    GST_DEBUG (0, "sync: done waiting for thread\n");
//  }
}


static void
gst_thread_restore_thyself (GstObject *object,
		            xmlNodePtr self)
{
  GST_DEBUG (0,"gstthread: restore\n");

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, self);
}

static xmlNodePtr
gst_thread_save_thyself (GstObject *object,
		         xmlNodePtr self)
{
  if (GST_OBJECT_CLASS (parent_class)->save_thyself)
    GST_OBJECT_CLASS (parent_class)->save_thyself (object, self);
  return NULL;
}
