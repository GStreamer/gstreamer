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

  thread->lock = g_mutex_new();
  thread->cond = g_cond_new();

  GST_ELEMENT_SCHED(thread) = gst_schedulerfactory_make ("basic", GST_ELEMENT(thread));
  GST_DEBUG(GST_CAT_THREAD, "thread's scheduler is %p\n",GST_ELEMENT_SCHED(thread));

  thread->ppid = getpid();
  thread->thread_id = -1;

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
gst_thread_update_state (GstThread *thread)
{
  // check for state change
  if (GST_STATE_PENDING(thread) != GST_STATE_VOID_PENDING) {
    // punt and change state on all the children
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
      return GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));
  }

  return GST_STATE_SUCCESS;
}


static GstElementStateReturn
gst_thread_change_state (GstElement * element)
{
  GstThread *thread;
  gboolean stateset = GST_STATE_SUCCESS;
  gint transition;
  pthread_t self = pthread_self ();

  g_return_val_if_fail (GST_IS_THREAD (element), FALSE);

  thread = GST_THREAD (element);

  transition = GST_STATE_TRANSITION (element);

  THR_INFO ("changing state from %s to %s",
	    gst_element_statename (GST_STATE (element)),
	    gst_element_statename (GST_STATE_PENDING (element)));

  if (pthread_equal (self, thread->thread_id)) {
    GST_DEBUG (GST_CAT_THREAD,
	       "no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to spinning\n",
	       GST_DEBUG_THREAD_ARGS (thread->pid));
    return gst_thread_update_state (thread);
  }

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      // set the state to idle
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      THR_DEBUG ("creating thread \"%s\"\n", GST_ELEMENT_NAME (element));

      g_mutex_lock (thread->lock);

      // create the thread
      pthread_create (&thread->thread_id, NULL, gst_thread_main_loop, thread);

      // wait for it to 'spin up'
      THR_DEBUG ("waiting for child thread spinup\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("thread claims to be up\n");
      g_mutex_unlock (thread->lock);
      break;
    case GST_STATE_READY_TO_PAUSED:
      THR_INFO ("readying thread");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling\n");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack\n");
      g_mutex_unlock (thread->lock);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      THR_DEBUG ("telling thread to start spinning\n");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling\n");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack\n");
      g_mutex_unlock (thread->lock);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
    {
      GList *elements = (element->sched)->elements;

      THR_INFO ("pausing thread");

      // the following code ensures that the bottom half of thread will run
      // to perform each elements' change_state() (by calling gstbin.c::
      // change_state()).
      // + the pending state was already set by gstelement.c::set_state()
      // + find every queue we manage, and signal its empty and full conditions
      g_mutex_lock (thread->lock);

      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      while (elements) {
	GstElement *e = GST_ELEMENT (elements->data);

	g_assert (e);
	THR_DEBUG ("  element \"%s\"\n", GST_ELEMENT_NAME (e));
	elements = g_list_next (elements);
	if (GST_IS_QUEUE (e)) {
	  //FIXME make this more efficient by only waking queues that are asleep
	  //FIXME and only waking the appropriate condition (depending on if it's
	  //FIXME on up- or down-stream side)
	  //
	  //FIXME also make this more efficient by keeping list of managed queues
	  THR_DEBUG ("waking queue \"%s\"\n", GST_ELEMENT_NAME (e));
	  gst_element_set_state (e, GST_STATE_PAUSED);
	}
	else {
	  GList *pads = GST_ELEMENT_PADS (e);

	  while (pads) {
	    GstRealPad *peer;
	    GstElement *peerelement;
	    GstPad *p = GST_PAD (pads->data);

	    pads = g_list_next (pads);

	    peer = GST_PAD_PEER (p);
	    if (!peer)
	      continue;

	    peerelement = GST_PAD_PARENT (peer);
	    if (!peerelement)
	      continue;		// deal with case where there's no peer

	    if (!GST_FLAG_IS_SET (peerelement, GST_ELEMENT_DECOUPLED)) {
	      GST_DEBUG (GST_CAT_THREAD, "peer element isn't DECOUPLED\n");
	      continue;
	    }

	    // FIXME this needs to go away eventually
	    if (!GST_IS_QUEUE (peerelement)) {
	      GST_DEBUG (GST_CAT_THREAD, "peer element isn't a Queue\n");
	      continue;
	    }

	    if (GST_ELEMENT_SCHED (peerelement) != GST_ELEMENT_SCHED (thread)) {
	      GstQueue *queue = GST_QUEUE (peerelement);

	      THR_DEBUG ("  element \"%s\" has pad cross sched boundary\n", GST_ELEMENT_NAME (e));
	      // FIXME!!
	      g_mutex_lock (queue->qlock);
	      g_cond_signal (queue->not_full);
	      g_cond_signal (queue->not_empty);
	      g_mutex_unlock (queue->qlock);
	    }
	  }
	}
      }
      THR_DEBUG ("telling thread to pause, signaling\n");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack\n");
      g_mutex_unlock (thread->lock);
      break;
    }
    case GST_STATE_READY_TO_NULL:
      THR_DEBUG ("telling thread to pause (null) - and joining\n");
      //MattH FIXME revisit
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling\n");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack\n");
      pthread_join (thread->thread_id, NULL);
      thread->thread_id = -1;
      g_mutex_unlock (thread->lock);

      GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_STARTED);
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

      break;
    case GST_STATE_PAUSED_TO_READY:
      THR_DEBUG ("telling thread to stop spinning\n");
      g_mutex_lock (thread->lock);
      THR_DEBUG ("signaling\n");
      g_cond_signal (thread->cond);
      THR_DEBUG ("waiting for ack\n");
      g_cond_wait (thread->cond, thread->lock);
      THR_DEBUG ("got ack\n");
      g_mutex_unlock (thread->lock);

      break;
    default:
      GST_DEBUG_ELEMENT (GST_CAT_THREAD, element, "UNHANDLED STATE CHANGE! %x\n", transition);
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

  g_mutex_lock (thread->lock);

  GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);

  thread->pid = getpid();
  THR_INFO_MAIN("thread is running");

  // first we need to change the state of all the children
  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

    if (stateset != GST_STATE_SUCCESS) {
      THR_DEBUG_MAIN ("state change of children failed\n");
    }
  }


  THR_DEBUG_MAIN ("indicating spinup\n");
  g_cond_signal (thread->cond);
  // don't unlock the mutex because we hold it into the top of the while loop
  THR_DEBUG_MAIN ("thread has indicated spinup to parent process\n");

  /***** THREAD IS NOW IN READY STATE *****/

  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    // NOTE we hold the thread lock at this point
    // what we do depends on what state we're in
    switch (GST_STATE (thread)) {
      // NOTE: cannot be in NULL, we're not running in that state at all
      case GST_STATE_READY:
        // wait to be set to either the NULL or PAUSED states
        THR_DEBUG_MAIN ("thread in %s state, waiting for either %s or %s\n",
                        gst_element_statename (GST_STATE_READY),
                        gst_element_statename (GST_STATE_NULL),
                        gst_element_statename (GST_STATE_PAUSED));
        g_cond_wait (thread->cond,thread->lock);
	
	g_assert (GST_STATE_PENDING (thread) == GST_STATE_NULL ||
	          GST_STATE_PENDING (thread) == GST_STATE_PAUSED);

        // been signaled, we need to state transition now and signal back
        gst_thread_update_state (thread);
        THR_DEBUG_MAIN ("done with state transition, signaling back to parent process\n");
        g_cond_signal (thread->cond);
        // now we decide what to do next 
        if (GST_STATE (thread) == GST_STATE_NULL) {
          // REAPING must be set, we can simply break this iteration
          GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
	}
        continue;
      case GST_STATE_PAUSED:
        // wait to be set to either the READY or PLAYING states
        THR_DEBUG_MAIN("thread in %s state, waiting for either %s or %s\n",
                       gst_element_statename (GST_STATE_PAUSED),
                       gst_element_statename (GST_STATE_READY),
                       gst_element_statename (GST_STATE_PLAYING));
        g_cond_wait (thread->cond,thread->lock);

	g_assert (GST_STATE_PENDING (thread) == GST_STATE_READY ||
	          GST_STATE_PENDING (thread) == GST_STATE_PLAYING);

        // been signaled, we need to state transition now and signal back
        gst_thread_update_state (thread);
        // now we decide what to do next
        if (GST_STATE (thread) != GST_STATE_PLAYING) {
          // either READY or the state change failed for some reason
          g_cond_signal (thread->cond);
          continue;
        } 
	else {
          GST_FLAG_SET (thread, GST_THREAD_STATE_SPINNING);
          // PLAYING is coming up, so we can now start spinning
          while (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
	    gboolean status;

            g_cond_signal (thread->cond);
            g_mutex_unlock (thread->lock);
            status = gst_bin_iterate (GST_BIN (thread));
            g_mutex_lock (thread->lock);
            //g_cond_signal(thread->cond);

	    if (!status || GST_STATE_PENDING (thread) != GST_STATE_VOID_PENDING)
              GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
          }
	  // looks like we were stopped because of a statechange
	  if (GST_STATE_PENDING (thread)) {
            gst_thread_update_state (thread);
	  }
          // once we're here, SPINNING has stopped, we should signal that we're done
          THR_DEBUG_MAIN ("SPINNING stopped, signaling back to parent process\n");
          g_cond_signal (thread->cond);
          // now we can wait for PAUSED
          continue;
        }
      case GST_STATE_PLAYING:
        // wait to be set to PAUSED
        THR_DEBUG_MAIN ("thread in %s state, waiting for %s\n",
                        gst_element_statename(GST_STATE_PLAYING),
                        gst_element_statename(GST_STATE_PAUSED));
        g_cond_wait (thread->cond,thread->lock);

        // been signaled, we need to state transition now and signal back
        gst_thread_update_state (thread);
        g_cond_signal (thread->cond);
        // now we decide what to do next
        // there's only PAUSED, we we just wait for it
        continue;
      case GST_STATE_NULL:
        THR_DEBUG_MAIN ("thread in %s state, preparing to die\n",
                        gst_element_statename(GST_STATE_NULL));
        GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
        break;
      default:
	g_assert_not_reached ();
        break;
    }
  }
  // since we don't unlock at the end of the while loop, do it here
  g_mutex_unlock (thread->lock);

  GST_INFO (GST_CAT_THREAD, "gstthread: thread \"%s\" is stopped",
		  GST_ELEMENT_NAME (thread));
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
  GST_DEBUG (GST_CAT_THREAD,"gstthread: restore\n");

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, self);
}
#endif // GST_DISABLE_LOADSAVE
