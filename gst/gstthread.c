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
#include "gstqueue.h"

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

static void 			gst_thread_dispose 	(GObject *object);

static void			gst_thread_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_thread_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_thread_change_state		(GstElement *element);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr		gst_thread_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_thread_restore_thyself	(GstObject *object, xmlNodePtr self);
#endif

static void			gst_thread_signal_thread	(GstThread *thread, gboolean spinning);

static void*			gst_thread_main_loop		(void *arg);

static GstBinClass *parent_class = NULL;
//static guint gst_thread_signals[LAST_SIGNAL] = { 0 };

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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CREATE_THREAD,
    g_param_spec_boolean("create_thread", "Create Thread", "Whether to create a thread.",
                         TRUE,G_PARAM_READWRITE));

  gobject_class->dispose =		gst_thread_dispose;

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself =	GST_DEBUG_FUNCPTR (gst_thread_save_thyself);
  gstobject_class->restore_thyself =	GST_DEBUG_FUNCPTR(gst_thread_restore_thyself);
#endif

  gstelement_class->change_state =	GST_DEBUG_FUNCPTR (gst_thread_change_state);

//  gstbin_class->schedule = gst_thread_schedule_dummy;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_thread_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_thread_get_property);

}

static void
gst_thread_init (GstThread *thread)
{

  GST_DEBUG (GST_CAT_THREAD,"initializing thread\n");

  // we're a manager by default
  GST_FLAG_SET (thread, GST_BIN_FLAG_MANAGER);

  // default is to create a thread
  GST_FLAG_SET (thread, GST_THREAD_CREATE);

  thread->lock = g_mutex_new();
  thread->cond = g_cond_new();

  GST_ELEMENT_SCHED(thread) = gst_schedule_new(GST_ELEMENT(thread));
  GST_DEBUG(GST_CAT_THREAD, "thread's scheduler is %p\n",GST_ELEMENT_SCHED(thread));

  thread->ppid = getpid();

//  gst_element_set_manager(GST_ELEMENT(thread),GST_ELEMENT(thread));
}

static void
gst_thread_dispose (GObject *object)
{
  GstThread *thread = GST_THREAD (object);

  GST_DEBUG (GST_CAT_REFCOUNTING,"dispose\n");

  g_mutex_free (thread->lock);
  g_cond_free (thread->cond);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  gst_object_destroy (GST_OBJECT (GST_ELEMENT_SCHED (thread)));
  gst_object_unref (GST_OBJECT (GST_ELEMENT_SCHED (thread)));

}

static void
gst_thread_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  switch (prop_id) {
    case ARG_CREATE_THREAD:
      if (g_value_get_boolean(value)) {
        GST_INFO (GST_CAT_THREAD,"turning ON the creation of the thread");
        GST_FLAG_SET (object, GST_THREAD_CREATE);
//        GST_DEBUG (GST_CAT_THREAD,"flags are 0x%08x\n", GST_FLAGS (object));
      } else {
        GST_INFO (GST_CAT_THREAD,"gstthread: turning OFF the creation of the thread");
        GST_FLAG_UNSET (object, GST_THREAD_CREATE);
//        GST_DEBUG (GST_CAT_THREAD,"gstthread: flags are 0x%08x\n", GST_FLAGS (object));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_thread_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_THREAD (object));

  switch (prop_id) {
    case ARG_CREATE_THREAD:
      g_value_set_boolean(value, GST_FLAG_IS_SET (object, GST_THREAD_CREATE));
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
gst_thread_new (const guchar *name)
{
  return gst_elementfactory_make ("thread", name);
}


#define THR_INFO(format,args...) \
  GST_INFO_ELEMENT(GST_CAT_THREAD, thread, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )
#define THR_DEBUG(format,args...) \
  GST_DEBUG_ELEMENT(GST_CAT_THREAD, thread, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )

#define THR_INFO_MAIN(format,args...) \
  GST_INFO_ELEMENT(GST_CAT_THREAD, thread, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->ppid) , ## args )
#define THR_DEBUG_MAIN(format,args...) \
  GST_DEBUG_ELEMENT(GST_CAT_THREAD, thread, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->ppid) , ## args )


static GstElementStateReturn
gst_thread_change_state (GstElement *element)
{
  GstThread *thread;
  gboolean stateset = GST_STATE_SUCCESS;
  gint transition;
  pthread_t self = pthread_self();

  g_return_val_if_fail (GST_IS_THREAD(element), FALSE);
//  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME(element));

  thread = GST_THREAD (element);
//  GST_DEBUG (GST_CAT_THREAD, "**** THREAD %ld changing THREAD %ld ****\n",self,thread->thread_id);
//  GST_DEBUG (GST_CAT_THREAD, "**** current pid=%d\n",getpid());

  transition = GST_STATE_TRANSITION (element);

  THR_INFO("changing state from %s to %s",
           gst_element_statename(GST_STATE (element)),
           gst_element_statename(GST_STATE_PENDING (element)));

  //GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      // set the state to idle
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);

      // create the thread if that's what we're supposed to do
      if (GST_FLAG_IS_SET (thread, GST_THREAD_CREATE)) {
        THR_DEBUG ("creating thread \"%s\"\n",
                   GST_ELEMENT_NAME (element));

        g_mutex_lock (thread->lock);

        // create the thread
        pthread_create (&thread->thread_id, NULL,
                        gst_thread_main_loop, thread);

        // wait for it to 'spin up'
        THR_DEBUG("waiting for child thread spinup\n");
        g_cond_wait(thread->cond,thread->lock);
        THR_DEBUG("thread claims to be up\n");
        g_mutex_unlock(thread->lock);
      } else {
        GST_INFO (GST_CAT_THREAD, "NOT creating thread \"%s\"",
                GST_ELEMENT_NAME (GST_ELEMENT (element)));

        // punt and change state on all the children
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
          stateset = GST_ELEMENT_CLASS (parent_class)->change_state (element);
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      THR_INFO("readying thread");

      // check to see if the thread is somehow changing its own state.
      // FIXME this is currently illegal, but must somehow be made legal at some point.
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        GST_FLAG_SET(thread, GST_THREAD_STATE_SPINNING);
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to spinning\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
      }
      else
      {
        g_mutex_lock(thread->lock);
        gst_thread_signal_thread(thread,FALSE);
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      THR_INFO("starting thread");

      // check to see if the thread is somehow changing its own state.
      // FIXME this is currently illegal, but must somehow be made legal at some point.
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        GST_FLAG_SET(thread, GST_THREAD_STATE_SPINNING);
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to spinning\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
      }
      else
      {
        THR_DEBUG("telling thread to start spinning\n");
        g_mutex_lock(thread->lock);
        gst_thread_signal_thread(thread,TRUE);
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      THR_INFO("pausing thread");

      // check to see if the thread is somehow changing its own state.
      // FIXME this is currently illegal, but must somehow be made legal at some point.
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to paused\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
        GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
        g_assert(!pthread_equal(self, thread->thread_id));
      }
      else
      {
        GList *elements = (element->sched)->elements;

        // the following code ensures that the bottom half of thread will run
        // to perform each elements' change_state() (by calling gstbin.c::
        // change_state()).
        // + the pending state was already set by gstelement.c::set_state()
        // + find every queue we manage, and signal its empty and full conditions

        g_mutex_lock(thread->lock);

        GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

        while (elements)
        {
          GstElement *e = GST_ELEMENT(elements->data);
          g_assert(e);
          THR_DEBUG("  element \"%s\"\n",GST_ELEMENT_NAME(e));
          elements = g_list_next(elements);
          if (GST_IS_QUEUE(e))
          {
            //FIXME make this more efficient by only waking queues that are asleep
            //FIXME and only waking the appropriate condition (depending on if it's
            //FIXME on up- or down-stream side)
            //
            //FIXME also make this more efficient by keeping list of managed queues
            THR_DEBUG("waking queue \"%s\"\n",GST_ELEMENT_NAME(e));
            GST_LOCK(e);
            g_cond_signal((GST_QUEUE(e)->emptycond));
            g_cond_signal((GST_QUEUE(e)->fullcond));
            GST_UNLOCK(e);
          }
          else
          {
            GList *pads = GST_ELEMENT_PADS(e);
            while (pads)
            {
	      GstRealPad *peer;
	      GstElement *peerelement;
              GstPad *p = GST_PAD(pads->data);
              pads = g_list_next(pads);

	      peer = GST_PAD_PEER(p);
	      if (!peer) continue;

              peerelement = GST_PAD_PARENT(peer);
              if (!peerelement) continue;		// deal with case where there's no peer

              if (!GST_FLAG_IS_SET(peerelement,GST_ELEMENT_DECOUPLED)) {
                GST_DEBUG(GST_CAT_THREAD,"peer element isn't DECOUPLED\n");
                continue;
              }

              // FIXME this needs to go away eventually
              if (!GST_IS_QUEUE(peerelement)) {
                GST_DEBUG(GST_CAT_THREAD,"peer element isn't a Queue\n");
                continue;
              }

              if (GST_ELEMENT_SCHED(peerelement) != GST_ELEMENT_SCHED(thread))
              {
                THR_DEBUG("  element \"%s\" has pad cross sched boundary\n",GST_ELEMENT_NAME(e));
                GST_LOCK(peerelement);
                g_cond_signal(GST_QUEUE(peerelement)->emptycond);
                g_cond_signal(GST_QUEUE(peerelement)->fullcond);
                GST_UNLOCK(peerelement);
              }
            }
          }
        }
        THR_DEBUG("waiting for thread to stop spinning\n");
        g_cond_wait (thread->cond, thread->lock);
        THR_DEBUG("telling thread to pause\n");
        gst_thread_signal_thread(thread,FALSE);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      THR_INFO("stopping thread");

      GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);

      // check to see if the thread is somehow changing its own state.
      // FIXME this is currently illegal, but must somehow be made legal at some point.
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        THR_DEBUG("setting own thread's state to NULL (paused)\n");
        GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      }
      else
      {
        THR_DEBUG("telling thread to pause (null) - and joining\n");
        //MattH FIXME revisit
        g_mutex_lock(thread->lock);
        gst_thread_signal_thread(thread,FALSE);
        pthread_join(thread->thread_id,NULL);
      }

      GST_FLAG_UNSET(thread,GST_THREAD_STATE_REAPING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_STARTED);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_ELEMENT_CHANGED);

      break;
    case GST_STATE_PAUSED_TO_READY:
      THR_INFO("stopping thread");

      // check to see if the thread is somehow changing its own state.
      // FIXME this is currently illegal, but must somehow be made legal at some point.
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        GST_FLAG_SET(thread, GST_THREAD_STATE_SPINNING);
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to spinning\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
      }
      else
      {
        THR_DEBUG("telling thread to stop spinning\n");
        g_mutex_lock(thread->lock);
        gst_thread_signal_thread(thread,FALSE);
      }
      
      break;
    default:
      GST_DEBUG_ELEMENT(GST_CAT_THREAD, element, "UNHANDLED STATE CHANGE! %x\n",transition);
      break;
  }

  return stateset;
}

static void gst_thread_update_state (GstThread *thread)
{
  // check for state change
  if (GST_STATE_PENDING(thread) != GST_STATE_VOID_PENDING) {
    // punt and change state on all the children
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
      GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));
  }
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

  thread->pid = getpid();
  THR_INFO_MAIN("thread is running");

  // first we need to change the state of all the children
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

  // construct the plan and signal back
/* DEPRACATED for INCSCHED1
  THR_DEBUG_MAIN("creating plan for thread\n");
  if (GST_BIN_CLASS (parent_class)->schedule)
    GST_BIN_CLASS (parent_class)->schedule (GST_BIN (thread));
*/

//  THR_DEBUG_MAIN("indicating spinup\n");
  g_mutex_lock (thread->lock);
  g_cond_signal (thread->cond);
  // don't unlock the mutex because we hold it into the top of the while loop
  THR_DEBUG_MAIN("thread has indicated spinup to parent process\n");

  /***** THREAD IS NOW IN READY STATE *****/

  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    // NOTE we hold the thread lock at this point
    // what we do depends on what state we're in
    switch (GST_STATE(thread)) {
      // NOTE: cannot be in NULL, we're not running in that state at all
      case GST_STATE_READY:
        // wait to be set to either the NULL or PAUSED states
        THR_DEBUG_MAIN("thread in %s state, waiting for either %s or %s\n",
                       gst_element_statename(GST_STATE_READY),
                       gst_element_statename(GST_STATE_NULL),
                       gst_element_statename(GST_STATE_PAUSED));
        g_cond_wait(thread->cond,thread->lock);
        // been signaled, we need to state transition now and signal back
        gst_thread_update_state(thread);
        THR_DEBUG_MAIN("done with state transition, signaling back to parent process\n");
        g_cond_signal(thread->cond);
//        g_mutex_unlock(thread->lock);
        // now we decide what to do next (FIXME can be collapsed to a continue)
        if (GST_STATE(thread) == GST_STATE_NULL) {
          // REAPING must be set, we can simply break this iteration
          continue;
        } else {
          // PAUSED is the next state, we can wait for that next
          continue;
        }
        break;
      case GST_STATE_PAUSED:
        // wait to be set to either the READY or PLAYING states
        THR_DEBUG_MAIN("thread in %s state, waiting for either %s or %s\n",
                       gst_element_statename(GST_STATE_PAUSED),
                       gst_element_statename(GST_STATE_READY),
                       gst_element_statename(GST_STATE_PLAYING));
        g_cond_wait(thread->cond,thread->lock);
        // been signaled, we need to state transition now and signal back
        gst_thread_update_state(thread);
        g_cond_signal(thread->cond);
//        g_mutex_unlock(thread->lock);
        // now we decide what to do next
        if (GST_STATE(thread) == GST_STATE_READY) {
          // READY is the next state, we can wait for that next
          continue;
        } else {
          g_mutex_unlock(thread->lock);
          // PLAYING is coming up, so we can now start spinning
          while (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
            if (!gst_bin_iterate (GST_BIN (thread))) {
//              GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
//              THR_DEBUG_MAIN("removed spinning state due to failed iteration!\n");
              // FIXME FIXME FIXME this is ugly!
              THR_DEBUG_MAIN("iteration failed, something very wrong, spinning to let parent sync\n");
              while (GST_FLAG_IS_SET(thread, GST_THREAD_STATE_SPINNING)) ;
            }
          }
          g_mutex_lock(thread->lock);
          // once we're here, SPINNING has stopped, we should signal that we're done
          THR_DEBUG_MAIN("SPINNING stopped, signaling back to parent process\n");
          g_cond_signal (thread->cond);
          // now we can wait for PAUSED
          continue;
        }
        break;
      case GST_STATE_PLAYING:
        // wait to be set to PAUSED
        THR_DEBUG_MAIN("thread in %s state, waiting for %s\n",
                       gst_element_statename(GST_STATE_PLAYING),
                       gst_element_statename(GST_STATE_PAUSED));
        g_cond_wait(thread->cond,thread->lock);
        // been signaled, we need to state transition now and signal back
        gst_thread_update_state(thread);
        g_cond_signal(thread->cond);
//        g_mutex_unlock(thread->lock);
        // now we decide what to do next
        // there's only PAUSED, we we just wait for it
        continue;
        break;
    }

    // need to grab the lock so we're ready for the top of the loop
//    g_mutex_lock(thread->lock);
  }

/*
  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    // start out by waiting for a state change into spinning
    THR_DEBUG_MAIN("waiting for signal from parent process (at top of while())\n");
    g_cond_wait (thread->cond,thread->lock);
    THR_DEBUG_MAIN("woken up with %s pending\n",gst_element_statename(GST_STATE(thread)));
    // now is a good time to change the state of the children and the thread itself
    gst_thread_update_state (thread);
    THR_DEBUG_MAIN("done changing state, signaling back\n");
    g_cond_signal (thread->cond);
    g_mutex_unlock (thread->lock);
    THR_DEBUG_MAIN("finished sycnronizing with main process\n");

    while (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
      if (!gst_bin_iterate (GST_BIN (thread))) {
	GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
        THR_DEBUG_MAIN("removed spinning state due to failed iteration!\n");
      }
    }

    g_mutex_lock (thread->lock);

    if (GST_STATE_PENDING(thread) == GST_STATE_PAUSED) {
      // we've stopped spinning, because of PLAYING->PAUSED
      THR_DEBUG_MAIN("SPINNING flag unset, signaling parent process we're stopped\n");
      // we need to signal back that we've stopped spinning
      g_cond_signal (thread->cond);
    }

//    THR_DEBUG_MAIN("signaling that the thread is out of the SPINNING loop\n");
//    g_cond_signal (thread->cond);
//    g_cond_wait (thread->cond, thread->lock);
//    THR_DEBUG_MAIN("parent process has signaled at bottom of while\n");
//    // now change the children's and thread's state
//    gst_thread_update_state (thread);
//    THR_DEBUG_MAIN("done changing state, signaling back to parent process\n");
//    g_cond_signal (thread->cond);
//    // don't release the mutex, we hold that into the top of the loop
//    THR_DEBUG_MAIN("done syncing with parent process at bottom of while\n");
  }
*/

  // since we don't unlock at the end of the while loop, do it here
  g_mutex_unlock (thread->lock);

  GST_INFO (GST_CAT_THREAD, "gstthread: thread \"%s\" is stopped",
		  GST_ELEMENT_NAME (thread));
  return NULL;
}

// the set flag is to say whether it should set TRUE or FALSE
//
// WARNING: this has synchronization built in!  if you remove or add any
// locks, waits, signals, or unlocks you need to be sure they match the 
// code above (in gst_thread_main_loop()).  basically, don't change anything.
static void
gst_thread_signal_thread (GstThread *thread, gboolean spinning)
{
  // set the spinning state
  if (spinning) GST_FLAG_SET(thread,GST_THREAD_STATE_SPINNING);
  else GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

  THR_DEBUG("thread locked\n");
//  g_mutex_lock(thread->lock);

//  if (!spinning) {
//    THR_DEBUG("waiting for spindown\n");
//    g_cond_wait (thread->cond, thread->lock);
//  }
  THR_DEBUG("signaling\n");
  g_cond_signal (thread->cond);
  THR_DEBUG("waiting for ack\n");
  g_cond_wait (thread->cond,thread->lock);
  THR_DEBUG("got ack\n");

  THR_DEBUG("unlocking\n");
  g_mutex_unlock(thread->lock);
  THR_DEBUG("unlocked\n");
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
  GST_DEBUG (GST_CAT_THREAD,"gstthread: restore\n");

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, self);
}
#endif // GST_DISABLE_LOADSAVE
