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

static void 			gst_thread_real_destroy 	(GtkObject *gtk_object);

static void			gst_thread_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void			gst_thread_get_arg		(GtkObject *object, GtkArg *arg, guint id);

static GstElementStateReturn	gst_thread_change_state		(GstElement *element);

static xmlNodePtr		gst_thread_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_thread_restore_thyself	(GstObject *object, xmlNodePtr self);

static void			gst_thread_signal_thread	(GstThread *thread, gboolean spinning);

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

  gtkobject_class->destroy =		gst_thread_real_destroy;

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
gst_thread_real_destroy (GtkObject *gtk_object)
{
  GstThread *thread = GST_THREAD (gtk_object);

  GST_DEBUG (GST_CAT_REFCOUNTING,"destroy()\n");

  g_mutex_free (thread->lock);
  g_cond_free (thread->cond);

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    GTK_OBJECT_CLASS (parent_class)->destroy (gtk_object);

  gst_object_destroy (GST_OBJECT (GST_ELEMENT_SCHED (thread)));
  gst_object_unref (GST_OBJECT (GST_ELEMENT_SCHED (thread)));
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
//        GST_DEBUG (GST_CAT_THREAD,"flags are 0x%08x\n", GST_FLAGS (object));
      } else {
        GST_INFO (GST_CAT_THREAD,"gstthread: turning OFF the creation of the thread");
        GST_FLAG_UNSET (object, GST_THREAD_CREATE);
//        GST_DEBUG (GST_CAT_THREAD,"gstthread: flags are 0x%08x\n", GST_FLAGS (object));
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
gst_thread_new (const guchar *name)
{
  return gst_elementfactory_make ("thread", name);
}


#define THR_INFO(format,args...) \
  GST_INFO(GST_CAT_THREAD, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )
#define THR_DEBUG(format,args...) \
  GST_DEBUG(GST_CAT_THREAD, "sync(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->pid) , ## args )

#define THR_INFO_MAIN(format,args...) \
  GST_INFO(GST_CAT_THREAD, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
  GST_DEBUG_THREAD_ARGS(thread->ppid) , ## args )
#define THR_DEBUG_MAIN(format,args...) \
  GST_DEBUG(GST_CAT_THREAD, "sync-main(" GST_DEBUG_THREAD_FORMAT "): " format , \
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

  THR_INFO("thread \"%s\" changing state from %s to %s",
           GST_ELEMENT_NAME (GST_ELEMENT (element)),
           gst_element_statename(GST_STATE (element)),
           gst_element_statename(GST_STATE_PENDING (element)));

  //GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      // we want to prepare our internal state for doing the iterations
      GST_INFO (GST_CAT_THREAD, "preparing thread \"%s\" for iterations:",
               GST_ELEMENT_NAME (GST_ELEMENT (element)));

      // set the state to idle
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      // create the thread if that's what we're supposed to do

      if (GST_FLAG_IS_SET (thread, GST_THREAD_CREATE)) {
        GST_DEBUG (GST_CAT_THREAD, "creating thread \"%s\"\n",
                   GST_ELEMENT_NAME (GST_ELEMENT (element)));

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
      THR_INFO("starting thread \"%s\"",GST_ELEMENT_NAME(element));

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
      THR_INFO("pausing thread \"%s\"",GST_ELEMENT_NAME(element));


      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to paused\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
        GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
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
            g_cond_signal((GST_QUEUE(e)->emptycond));
            g_cond_signal((GST_QUEUE(e)->fullcond));
          }
          else
          {
            GList *pads = GST_ELEMENT_PADS(e);
            while (pads)
            {
              GstPad *p = GST_PAD(pads->data);
              pads = g_list_next(pads);
              if (GST_IS_REAL_PAD(p) &&
                  GST_ELEMENT_SCHED(e) != GST_ELEMENT_SCHED(GST_ELEMENT(GST_PAD_PARENT(GST_PAD_PEER(p)))))
              {
                THR_DEBUG("  element \"%s\" has pad cross sched boundary\n",GST_ELEMENT_NAME(e));
                // FIXME i assume this signals our own (current) thread so don't need to lock
                // FIXME however, this *may* go to yet another thread for which we need locks
                // FIXME i'm too tired to deal with this now 
                g_cond_signal(GST_QUEUE(GST_ELEMENT(GST_PAD_PARENT(GST_PAD_PEER(p))))->emptycond);
                g_cond_signal(GST_QUEUE(GST_ELEMENT(GST_PAD_PARENT(GST_PAD_PEER(p))))->fullcond);

              }
            }
          }
        }
        THR_INFO("telling thread to pause");
        gst_thread_signal_thread(thread,FALSE);
      }
      break;
    case GST_STATE_PLAYING_TO_READY:
      if (pthread_equal(self, thread->thread_id))
      {
        //FIXME this should not happen
        g_assert(!pthread_equal(self, thread->thread_id));
        GST_DEBUG(GST_CAT_THREAD,"no sync(" GST_DEBUG_THREAD_FORMAT "): setting own thread's state to ready (paused)\n",
                  GST_DEBUG_THREAD_ARGS(thread->pid));
        GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      }
      else
      {
        THR_DEBUG("telling thread to pause (ready)\n");
        g_mutex_lock(thread->lock);
        gst_thread_signal_thread(thread,FALSE);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      THR_INFO("stopping thread \"%s\"",GST_ELEMENT_NAME(element));

      GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
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
//        g_mutex_lock(thread->lock);
//        gst_thread_signal_thread(thread,FALSE);
        pthread_join(thread->thread_id,NULL);
      }

      GST_FLAG_UNSET(thread,GST_THREAD_STATE_REAPING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_STARTED);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_ELEMENT_CHANGED);

      if (GST_ELEMENT_CLASS (parent_class)->change_state)
        stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

      break;
    default:
      break;
  }

  return stateset;
}

static void gst_thread_update_state (GstThread *thread)
{
  // check for state change
  if (GST_STATE_PENDING(thread) != GST_STATE_NONE_PENDING) {
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
  THR_INFO_MAIN("thread \"%s\" is running",GST_ELEMENT_NAME (GST_ELEMENT (thread)));

  // first we need to change the state of all the children
  THR_DEBUG_MAIN("thread started, setting children's state\n");
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT(thread));

  // construct the plan and signal back
  THR_DEBUG_MAIN("creating plan for thread\n");
  if (GST_BIN_CLASS (parent_class)->schedule)
    GST_BIN_CLASS (parent_class)->schedule (GST_BIN (thread));

  THR_DEBUG_MAIN("indicating spinup\n");
  g_mutex_lock (thread->lock);
  g_cond_signal (thread->cond);
  // don't unlock the mutex because we hold it into the top of the while loop
  THR_DEBUG_MAIN("thread has indicated spinup to parent process\n");

  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    // start out by waiting for a state change into spinning
    THR_DEBUG_MAIN("waiting at top of while for signal from parent process\n");
    g_cond_wait (thread->cond,thread->lock);
    THR_DEBUG_MAIN("parent thread has signaled back at top of while\n");
    // now is a good time to change the state of the children and the thread itself
    gst_thread_update_state (thread);
    THR_DEBUG_MAIN("doe changing state, signaling back to parent process\n");
    g_cond_signal (thread->cond);
    g_mutex_unlock (thread->lock);
    THR_DEBUG_MAIN("done syncing with parent process at top of while\n");

    while (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING)) {
      if (!gst_bin_iterate (GST_BIN (thread))) {
	GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
        THR_DEBUG_MAIN("removed spinning state due to failed iteration!\n");
      }
    }
    THR_DEBUG_MAIN("waiting at bottom of while for signal from parent process\n");
    g_mutex_lock (thread->lock);
    THR_DEBUG_MAIN("signaling that the thread is out of the SPINNING loop\n");
    g_cond_signal (thread->cond);
    g_cond_wait (thread->cond, thread->lock);
    THR_DEBUG_MAIN("parent process has signaled at bottom of while\n");
    // now change the children's and thread's state
    gst_thread_update_state (thread);
    THR_DEBUG_MAIN("done changing state, signaling back to parent process\n");
    g_cond_signal (thread->cond);
    // don't release the mutex, we hold that into the top of the loop
    THR_DEBUG_MAIN("done syncing with parent process at bottom of while\n");
  }

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

  if (!spinning) {
    THR_DEBUG("waiting for spindown\n");
    g_cond_wait (thread->cond, thread->lock);
  }
  THR_DEBUG("signaling\n");
  g_cond_signal (thread->cond);
  THR_DEBUG("waiting for ack\n");
  g_cond_wait (thread->cond,thread->lock);

  THR_DEBUG("unlocking\n");
  g_mutex_unlock(thread->lock);
  THR_DEBUG("unlocked\n");
}


static void
gst_thread_restore_thyself (GstObject *object,
		            xmlNodePtr self)
{
  GST_DEBUG (GST_CAT_THREAD,"gstthread: restore\n");

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
