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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gst.h"
#include "gstthread.h"
#include "gstscheduler.h"
#include "gstqueue.h"

#define STACK_SIZE 0x200000

#define g_thread_equal(a,b) ((a) == (b))

GstElementDetails gst_thread_details = {
  "Threaded container",
  "Generic/Bin",
  "LGPL",
  "Container that creates/manages a thread",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999, 2000",
};


/* Thread signals and args */
enum {
  SHUTDOWN,
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
  ARG_SCHEDPOLICY,
  ARG_PRIORITY,
};



static void			gst_thread_class_init		(GstThreadClass *klass);
static void			gst_thread_init			(GstThread *thread);

static void 			gst_thread_dispose 	(GObject *object);

static void			gst_thread_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_thread_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_thread_change_state		(GstElement *element);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr		gst_thread_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_thread_restore_thyself	(GstObject *object, xmlNodePtr self);
#endif

static void*			gst_thread_main_loop		(void *arg);

#define GST_TYPE_THREAD_SCHEDPOLICY (gst_thread_schedpolicy_get_type())
static GType
gst_thread_schedpolicy_get_type(void) {
  static GType thread_schedpolicy_type = 0;
  static GEnumValue thread_schedpolicy[] = {
    {G_THREAD_PRIORITY_LOW,    "LOW", "Low Priority Scheduling"},
    {G_THREAD_PRIORITY_NORMAL, "NORMAL",  "Normal Scheduling"},
    {G_THREAD_PRIORITY_HIGH,   "HIGH",  "High Priority Scheduling"},
    {G_THREAD_PRIORITY_URGENT, "URGENT", "Urgent Scheduling"},
    {0, NULL, NULL},
  };
  if (!thread_schedpolicy_type) {
    thread_schedpolicy_type = g_enum_register_static("GstThreadSchedPolicy", thread_schedpolicy);
  }
  return thread_schedpolicy_type;
}

static GstBinClass *parent_class = NULL;
static guint gst_thread_signals[LAST_SIGNAL] = { 0 }; 

GType
gst_thread_get_type(void) {
  static GType thread_type = 0;

  if (!thread_type) {
    static const GTypeInfo thread_info = {
      sizeof(GstThreadClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_thread_class_init,
      NULL,
      NULL,
      sizeof(GstThread),
      4,
      (GInstanceInitFunc)gst_thread_init,
      NULL
    };
    thread_type = g_type_register_static(GST_TYPE_BIN, "GstThread", &thread_info, 0);
  }
  return thread_type;
}

static void
gst_thread_class_init (GstThreadClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class =	(GObjectClass*)klass;
  gstobject_class =	(GstObjectClass*)klass;
  gstelement_class =	(GstElementClass*)klass;
  gstbin_class =	(GstBinClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_SCHEDPOLICY,
    g_param_spec_enum("schedpolicy", "Scheduling Policy", "The scheduling policy of the thread",
                      GST_TYPE_THREAD_SCHEDPOLICY, G_THREAD_PRIORITY_NORMAL, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS (klass), ARG_PRIORITY,
    g_param_spec_int("priority", "Scheduling Priority", "The scheduling priority of the thread",
                     0, 99, 0, G_PARAM_READWRITE));

  gst_thread_signals[SHUTDOWN] =
    g_signal_new ("shutdown", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstThreadClass, shutdown), NULL, NULL,
                  gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose =		gst_thread_dispose;

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself =	GST_DEBUG_FUNCPTR (gst_thread_save_thyself);
  gstobject_class->restore_thyself =	GST_DEBUG_FUNCPTR (gst_thread_restore_thyself);
#endif

  gstelement_class->change_state =	GST_DEBUG_FUNCPTR (gst_thread_change_state);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_thread_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_thread_get_property);

}

static void
gst_thread_init (GstThread *thread)
{
  GstScheduler *scheduler;

  GST_DEBUG (GST_CAT_THREAD, "initializing thread");

  /* threads are managing bins and iterate themselves */
  /* CR1: the GstBin code checks these flags */
  GST_FLAG_SET (thread, GST_BIN_FLAG_MANAGER);
  GST_FLAG_SET (thread, GST_BIN_SELF_SCHEDULABLE);

  scheduler = gst_scheduler_factory_make (NULL, GST_ELEMENT (thread));

  thread->lock = g_mutex_new ();
  thread->cond = g_cond_new ();

  thread->ppid = getpid ();
  thread->thread_id = (GThread *) NULL;
  thread->sched_policy = G_THREAD_PRIORITY_NORMAL;
  thread->priority = 0;
  thread->stack = NULL;
}

static void
gst_thread_dispose (GObject *object)
{
  GstThread *thread = GST_THREAD (object);

  GST_DEBUG (GST_CAT_REFCOUNTING, "dispose");

  g_mutex_free (thread->lock);
  g_cond_free (thread->cond);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (GST_ELEMENT_SCHED (thread)) {
    gst_object_unref (GST_OBJECT (GST_ELEMENT_SCHED (thread)));
  }
}

static void
gst_thread_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstThread *thread;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  thread = GST_THREAD (object);

  switch (prop_id) {
    case ARG_SCHEDPOLICY:
      thread->sched_policy = g_value_get_enum (value);
      break;
    case ARG_PRIORITY:
      thread->priority = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_thread_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstThread *thread;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  thread = GST_THREAD (object);

  switch (prop_id) {
    case ARG_SCHEDPOLICY:
      g_value_set_enum (value, thread->sched_policy);
      break;
    case ARG_PRIORITY:
      g_value_set_int (value, thread->priority);
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
GstElement*
gst_thread_new (const gchar *name)
{
  return gst_element_factory_make ("thread", name);
}

/* these two macros are used for debug/info from the state_change function */

#define THR_INFO(format,args...) \
  GST_INFO_ELEMENT(GST_CAT_THREAD, thread, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )
  
#define THR_DEBUG(format,args...) \
  GST_DEBUG_ELEMENT(GST_CAT_THREAD, thread, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )

/* these two macros are used for debug/info from the gst_thread_main_loop
 * function
 */

#define THR_INFO_MAIN(format,args...) \
  GST_INFO_ELEMENT(GST_CAT_THREAD, thread, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->ppid) , ## args )

#define THR_DEBUG_MAIN(format,args...) \
  GST_DEBUG_ELEMENT(GST_CAT_THREAD, thread, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->ppid) , ## args )

static GstElementStateReturn 
gst_thread_update_state (GstThread *thread)
{
  GST_DEBUG_ELEMENT (GST_CAT_THREAD, thread, "updating state of thread");
  /* check for state change */
  if (GST_STATE_PENDING (thread) != GST_STATE_VOID_PENDING) {
    /* punt and change state on all the children */
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
      return GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT (thread));
  }

  /* FIXME: in the case of no change_state function in the parent's class,
   * shouldn't we actually change the thread's state ? */
  g_warning ("thread's parent doesn't have change_state, returning success");
  return GST_STATE_SUCCESS;
}


static GstElementStateReturn
gst_thread_change_state (GstElement * element)
{
  GstThread *thread;
  gboolean stateset = GST_STATE_SUCCESS;
  gint transition;
  GThread * self = g_thread_self ();
  GError * error = NULL;

  g_return_val_if_fail (GST_IS_THREAD (element), GST_STATE_FAILURE);
  g_return_val_if_fail (gst_has_threads (), GST_STATE_FAILURE);

  thread = GST_THREAD (element);

  transition = GST_STATE_TRANSITION (element);

  THR_INFO ("changing state from %s to %s",
	    gst_element_state_get_name (GST_STATE (element)),
	    gst_element_state_get_name (GST_STATE_PENDING (element)));

  if (g_thread_equal (self, thread->thread_id)) {
    GST_DEBUG (GST_CAT_THREAD,
	       "no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to spinning",
	       GST_DEBUG_THREAD_ARGS (thread->pid));
    return gst_thread_update_state (thread);
  }

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      /* set the state to idle */
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      THR_DEBUG ("creating thread \"%s\"", GST_ELEMENT_NAME (element));

      /* this bit of code handles creation of GThreads
       * this is therefor tricky code
       * compare it with the block of code that handles the destruction
       * in GST_STATE_READY_TO_NULL below
       */
      g_mutex_lock (thread->lock);

      /* create a new GThread
       * use the specified attributes
       * make it execute gst_thread_main_loop (thread)
       */
      GST_DEBUG (GST_CAT_THREAD, "going to g_thread_create_full...");
      thread->thread_id = g_thread_create_full(gst_thread_main_loop,
	  thread, STACK_SIZE, TRUE, TRUE, G_THREAD_PRIORITY_NORMAL,
	  &error);
      if (!thread->thread_id){
        GST_DEBUG (GST_CAT_THREAD, "g_thread_create_full failed");
	g_mutex_unlock (thread->lock);
        GST_DEBUG (GST_CAT_THREAD, "could not create thread \"%s\"", 
	           GST_ELEMENT_NAME (element));
	return GST_STATE_FAILURE;
      }
      GST_DEBUG (GST_CAT_THREAD, "GThread created");

      /* wait for it to 'spin up' */
      THR_DEBUG ("waiting for child thread spinup");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("thread claims to be up");
      g_mutex_unlock (thread->lock);
      break;
    case GST_STATE_READY_TO_PAUSED:
      THR_INFO ("readying thread");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack");
      g_mutex_unlock (thread->lock);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    {
      /* fixme: recurse into sub-bins */
      const GList *elements = gst_bin_get_list (GST_BIN (thread));
      while (elements) {
        gst_element_enable_threadsafe_properties ((GstElement*)elements->data);
        elements = g_list_next (elements);
      }
      
      THR_DEBUG ("telling thread to start spinning");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack");
      g_mutex_unlock (thread->lock);
      break;
    }
    case GST_STATE_PLAYING_TO_PAUSED:
    {
      const GList *elements = (GList *) gst_bin_get_list (GST_BIN (thread));

      THR_INFO ("pausing thread");

      /* the following code ensures that the bottom half of thread will run
       * to perform each elements' change_state() (by calling gstbin.c::
       * change_state()).
       * + the pending state was already set by gstelement.c::set_state()
       * + unlock all elements so the bottom half can start the state change.
       */ 
      g_mutex_lock (thread->lock);

      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      while (elements) {
	GstElement *element = GST_ELEMENT (elements->data);
	GList *pads;
	

	g_assert (element);
	THR_DEBUG ("  waking element \"%s\"", GST_ELEMENT_NAME (element));
	elements = g_list_next (elements);

	if (!gst_element_release_locks (element)) {
          g_warning ("element %s could not release locks", GST_ELEMENT_NAME (element));
	}

	pads = GST_ELEMENT_PADS (element);

	while (pads) {
	  GstRealPad *peer = NULL;
	  GstElement *peerelement;

          if (GST_PAD_PEER (pads->data))
            peer = GST_REAL_PAD (GST_PAD_PEER (pads->data));

	  pads = g_list_next (pads);

	  if (!peer)
	    continue;

	  peerelement = GST_PAD_PARENT (peer);
	  if (!peerelement)
	    continue;		/* deal with case where there's no peer */

	  if (!GST_FLAG_IS_SET (peerelement, GST_ELEMENT_DECOUPLED)) {
	    GST_DEBUG (GST_CAT_THREAD, "peer element isn't DECOUPLED");
	    continue;
	  }

	  if (GST_ELEMENT_SCHED (peerelement) != GST_ELEMENT_SCHED (thread)) {
	    THR_DEBUG ("  element \"%s\" has pad cross sched boundary", GST_ELEMENT_NAME (element));
	    THR_DEBUG ("  waking element \"%s\"", GST_ELEMENT_NAME (peerelement));
	    if (!gst_element_release_locks (peerelement)) {
              g_warning ("element %s could not release locks", GST_ELEMENT_NAME (peerelement));
	    }
	  }
	}

      }
      THR_DEBUG ("telling thread to pause, signaling");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack");
      g_mutex_unlock (thread->lock);

      elements = gst_bin_get_list (GST_BIN (thread));
      while (elements) {
        gst_element_disable_threadsafe_properties ((GstElement*)elements->data);
        elements = g_list_next (elements);
      }
      break;
    }
    case GST_STATE_READY_TO_NULL:
      THR_DEBUG ("telling thread to pause (null) - and joining");
      /* MattH FIXME revisit */
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack");

      /* this block of code is very tricky
       * basically, we try to clean up the whole thread and
       * everything related to it in the right order without
       * triggering segfaults
       * compare this block to the block
       */

      GST_DEBUG (GST_CAT_THREAD, "joining GThread %p", thread->thread_id);
      g_thread_join (thread->thread_id);

      thread->thread_id = NULL;
      
      /* the stack was allocated when we created the thread
       * using scheduler->get_preferred_stack */
      if (thread->stack) {
        GST_DEBUG (GST_CAT_THREAD, "freeing allocated stack (%p)", 
	           thread->stack);
        free (thread->stack);
        thread->stack = NULL;
      }
     
      THR_DEBUG ("unlocking mutex");
      g_mutex_unlock (thread->lock);

      GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_STARTED);
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      break;
    case GST_STATE_PAUSED_TO_READY:
      THR_DEBUG ("telling thread to stop spinning");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack");
      g_mutex_unlock (thread->lock);

      break;
    default:
      GST_DEBUG_ELEMENT (GST_CAT_THREAD, element, "UNHANDLED STATE CHANGE! %x", transition);
      break;
  }

  return stateset;
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
  gint stateset;
  glong page_size;
  gpointer stack_pointer;
  gulong stack_offset;

  GST_DEBUG (GST_CAT_THREAD, "gst_thread_main_loop started");
  thread = GST_THREAD (arg);
  g_mutex_lock (thread->lock);

  /* set up the element's scheduler */
  gst_scheduler_setup (GST_ELEMENT_SCHED (thread));
  GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);

  thread->pid = getpid();
  THR_INFO_MAIN ("thread is running");

  page_size = sysconf(_SC_PAGESIZE);
  stack_pointer = (gpointer) &stack_pointer;

  if(((gulong)stack_pointer & (page_size-1)) < (page_size>>1)){
    /* stack grows up, I think */
    /* FIXME this is probably not true for the main thread */
    stack_offset = (gulong)stack_pointer & (page_size - 1);
  }else{
    /* stack grows down, I think */
    stack_offset = STACK_SIZE - ((gulong)stack_pointer & (page_size - 1));
  }
  /* note the subtlety with pointer arithmetic */
  thread->stack = stack_pointer - stack_offset;
  thread->stack_size = STACK_SIZE;

  /* first we need to change the state of all the children */
  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

    if (stateset != GST_STATE_SUCCESS) {
      THR_DEBUG_MAIN ("state change of children failed");
    }
  }

  THR_DEBUG_MAIN ("indicating spinup");
  g_cond_signal (thread->cond);
  /* don't unlock the mutex because we hold it into the top of the while loop */
  THR_DEBUG_MAIN ("thread has indicated spinup to parent process");

  /***** THREAD IS NOW IN READY STATE *****/

  /* CR1: most of this code is handshaking */
  /* do this while the thread lives */
  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    /* NOTE we hold the thread lock at this point */
    /* what we do depends on what state we're in */
    switch (GST_STATE (thread)) {
      /* NOTE: cannot be in NULL, we're not running in that state at all */
      case GST_STATE_READY:
        /* wait to be set to either the NULL or PAUSED states */
        THR_DEBUG_MAIN ("thread in %s state, waiting for either %s or %s",
                        gst_element_state_get_name (GST_STATE_READY),
                        gst_element_state_get_name (GST_STATE_NULL),
                        gst_element_state_get_name (GST_STATE_PAUSED));
        g_cond_wait (thread->cond, thread->lock);
	
	/* this must have happened by a state change in the thread context */
	if (GST_STATE_PENDING (thread) != GST_STATE_NULL &&
	    GST_STATE_PENDING (thread) != GST_STATE_PAUSED) {
          g_cond_signal (thread->cond);
	  continue;
	}

        /* been signaled, we need to state transition now and signal back */
        gst_thread_update_state (thread);
        THR_DEBUG_MAIN ("done with state transition, "
	                "signaling back to parent process");
        g_cond_signal (thread->cond);
        /* now we decide what to do next */
        if (GST_STATE (thread) == GST_STATE_NULL) {
          /* REAPING must be set, we can simply break this iteration */
          THR_DEBUG_MAIN ("set GST_THREAD_STATE_REAPING");
          GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
	}
        continue;

      case GST_STATE_PAUSED:
        /* wait to be set to either the READY or PLAYING states */
        THR_DEBUG_MAIN ("thread in %s state, waiting for either %s or %s",
                        gst_element_state_get_name (GST_STATE_PAUSED),
                        gst_element_state_get_name (GST_STATE_READY),
                        gst_element_state_get_name (GST_STATE_PLAYING));
        g_cond_wait (thread->cond, thread->lock);

	/* this must have happened by a state change in the thread context */
	if (GST_STATE_PENDING (thread) != GST_STATE_READY &&
	    GST_STATE_PENDING (thread) != GST_STATE_PLAYING) {
          g_cond_signal (thread->cond);
	  continue;
	}

        /* been signaled, we need to state transition now and signal back */
        gst_thread_update_state (thread);
        /* now we decide what to do next */
        if (GST_STATE (thread) != GST_STATE_PLAYING) {
          /* either READY or the state change failed for some reason */
          g_cond_signal (thread->cond);
          continue;
        } 
	else {
          GST_FLAG_SET (thread, GST_THREAD_STATE_SPINNING);
          /* PLAYING is coming up, so we can now start spinning */
          while (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
	    gboolean status;

            g_cond_signal (thread->cond);
            g_mutex_unlock (thread->lock);
            status = gst_bin_iterate (GST_BIN (thread));
            g_mutex_lock (thread->lock);
            /* g_cond_signal(thread->cond); */

	    if (!status || GST_STATE_PENDING (thread) != GST_STATE_VOID_PENDING)
              GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
          }
	  /* looks like we were stopped because of a statechange */
	  if (GST_STATE_PENDING (thread)) {
            gst_thread_update_state (thread);
	  }
          /* once we're here, SPINNING has stopped, we should signal that we're done */
          THR_DEBUG_MAIN ("SPINNING stopped, signaling back to parent process");
          g_cond_signal (thread->cond);
          /* now we can wait for PAUSED */
          continue;
        }
      case GST_STATE_PLAYING:
        /* wait to be set to PAUSED */
        THR_DEBUG_MAIN ("thread in %s state, waiting for %s",
                        gst_element_state_get_name (GST_STATE_PLAYING),
                        gst_element_state_get_name (GST_STATE_PAUSED));
        g_cond_wait (thread->cond,thread->lock);

        /* been signaled, we need to state transition now and signal back */
        gst_thread_update_state (thread);
        g_cond_signal (thread->cond);
        /* now we decide what to do next */
        /* there's only PAUSED, we we just wait for it */
        continue;
      case GST_STATE_NULL:
        THR_DEBUG_MAIN ("thread in %s state, preparing to die",
                        gst_element_state_get_name (GST_STATE_NULL));
        GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
        break;
      default:
	g_assert_not_reached ();
        break;
    }
  }

  /* THREAD HAS STOPPED RUNNING */
  
  /* we need to destroy the scheduler here because it has mapped it's
   * stack into the threads stack space */
  gst_scheduler_reset (GST_ELEMENT_SCHED (thread));

  /* since we don't unlock at the end of the while loop, do it here */
  g_mutex_unlock (thread->lock);

  GST_INFO (GST_CAT_THREAD, "gstthread: thread \"%s\" is stopped",
		  GST_ELEMENT_NAME (thread));

  g_signal_emit (G_OBJECT (thread), gst_thread_signals[SHUTDOWN], 0);

  return NULL;
}

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr
gst_thread_save_thyself (GstObject *object,
		         xmlNodePtr self)
{
  if (GST_OBJECT_CLASS (parent_class)->save_thyself)
    GST_OBJECT_CLASS (parent_class)->save_thyself (object, self);
  return NULL;
}

static void
gst_thread_restore_thyself (GstObject *object,
		            xmlNodePtr self)
{
  GST_DEBUG (GST_CAT_THREAD,"gstthread: restore");

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */
