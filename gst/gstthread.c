/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstthread.h"
#include "gstdebug.h"

GstElementDetails gst_thread_details = {
  "Threaded container",
  "Bin",
  "Container that creates/manages a thread",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Thread signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CREATE_THREAD,
};


static void 			gst_thread_class_init		(GstThreadClass *klass);
static void 			gst_thread_init			(GstThread *thread);

static void 			gst_thread_set_arg		(GtkObject *object,GtkArg *arg,guint id);
static void 			gst_thread_get_arg		(GtkObject *object,GtkArg *arg,guint id);

static GstElementStateReturn 	gst_thread_change_state		(GstElement *element);

static xmlNodePtr 		gst_thread_save_thyself		(GstElement *element,xmlNodePtr parent);
static void 			gst_thread_restore_thyself	(GstElement *element,xmlNodePtr parent, 
								 GHashTable *elements);

static void 			gst_thread_signal_thread	(GstThread *thread);
static void 			gst_thread_create_plan_dummy	(GstBin *bin);

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

  gtkobject_class = 	(GtkObjectClass*)klass;
  gstobject_class = 	(GstObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gstbin_class = 	(GstBinClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_BIN);

  gtk_object_add_arg_type ("GstThread::create_thread", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_CREATE_THREAD);

  gstelement_class->change_state = 	gst_thread_change_state;
  gstelement_class->save_thyself = 	gst_thread_save_thyself;
  gstelement_class->restore_thyself = 	gst_thread_restore_thyself;

  gstbin_class->create_plan = gst_thread_create_plan_dummy;

  gtkobject_class->set_arg = gst_thread_set_arg;
  gtkobject_class->get_arg = gst_thread_get_arg;

}

static void 
gst_thread_init (GstThread *thread) 
{
  // we're a manager by default
  GST_FLAG_SET (thread, GST_BIN_FLAG_MANAGER);

  // default is to create a thread
  GST_FLAG_SET (thread, GST_THREAD_CREATE);
  GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);

  thread->lock = g_mutex_new();
  thread->cond = g_cond_new();
}

static void 
gst_thread_create_plan_dummy (GstBin *bin) 
{
  g_return_if_fail (GST_IS_THREAD (bin));

  if (!GST_FLAG_IS_SET (GST_THREAD (bin), GST_THREAD_STATE_SPINNING)) 
    gst_info("gstthread: create plan delayed until thread starts\n");
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
        gst_info("gstthread: turning ON the creation of the thread\n");
        GST_FLAG_SET (object, GST_THREAD_CREATE);
        gst_info("gstthread: flags are 0x%08x\n", GST_FLAGS (object));
      } else {
        gst_info("gstthread: turning OFF the creation of the thread\n");
        GST_FLAG_UNSET (object, GST_THREAD_CREATE);
        gst_info("gstthread: flags are 0x%08x\n", GST_FLAGS (object));
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
 * Create a new thrad with the given name
 *
 * Returns; The new thread
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
  gint pending, transition;

  g_return_val_if_fail (GST_IS_THREAD(element), FALSE);
  DEBUG_ENTER("(\"%s\")",gst_element_get_name(element));

  thread = GST_THREAD (element);

  gst_info("gstthread: thread \"%s\" change state %d\n",
               gst_element_get_name (GST_ELEMENT (element)), 
	       GST_STATE_PENDING (element));

  pending = GST_STATE_PENDING (element);
  transition = GST_STATE_TRANSITION (element);

  if (pending == GST_STATE (element)) return GST_STATE_SUCCESS;

  GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    stateset = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  gst_info("gstthread: stateset %d %d %d %02x\n", GST_STATE (element), stateset, 
		  GST_STATE_PENDING (element), GST_STATE_TRANSITION (element));

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if (!stateset) return FALSE;
      // we want to prepare our internal state for doing the iterations
      gst_info("gstthread: preparing thread \"%s\" for iterations:\n",
               gst_element_get_name (GST_ELEMENT (element)));
      
      // set the state to idle
      GST_FLAG_UNSET (thread, GST_THREAD_STATE_SPINNING);
      // create the thread if that's what we're supposed to do
      gst_info("gstthread: flags are 0x%08x\n", GST_FLAGS (thread));
      
      if (GST_FLAG_IS_SET (thread, GST_THREAD_CREATE)) {
        gst_info("gstthread: starting thread \"%s\"\n",
                 gst_element_get_name (GST_ELEMENT (element)));
	
        pthread_create (&thread->thread_id, NULL,
                        gst_thread_main_loop, thread);
      } else {
        gst_info("gstthread: NOT starting thread \"%s\"\n",
                gst_element_get_name (GST_ELEMENT (element)));
      }
      return GST_STATE_ASYNC;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_READY_TO_PLAYING:
      if (!stateset) return FALSE;
      gst_info("gstthread: starting thread \"%s\"\n",
              gst_element_get_name (GST_ELEMENT (element)));
      
      GST_FLAG_SET (thread, GST_THREAD_STATE_SPINNING);
      gst_thread_signal_thread (thread);
      break;  
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_info("gstthread: pausing thread \"%s\"\n",
              gst_element_get_name (GST_ELEMENT (element)));
      
      //GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      gst_thread_signal_thread (thread);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_info("gstthread: stopping thread \"%s\"\n",
              gst_element_get_name (GST_ELEMENT (element)));
      
      GST_FLAG_SET (thread, GST_THREAD_STATE_REAPING);
      gst_thread_signal_thread (thread);
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

  gst_info("gstthread: thread \"%s\" is running with PID %d\n",
		  gst_element_get_name (GST_ELEMENT (thread)), getpid ());

  if (GST_BIN_CLASS (parent_class)->create_plan)
    GST_BIN_CLASS (parent_class)->create_plan (GST_BIN (thread));

  while (!GST_FLAG_IS_SET (thread, GST_THREAD_STATE_REAPING)) {
    if (GST_FLAG_IS_SET (thread, GST_THREAD_STATE_SPINNING))
      gst_bin_iterate (GST_BIN (thread));
    else {
      g_mutex_lock (thread->lock);
      g_cond_wait (thread->cond, thread->lock);
      g_mutex_unlock (thread->lock);
    }
  }

  GST_FLAG_UNSET (thread, GST_THREAD_STATE_REAPING);
  pthread_join (thread->thread_id, 0);

  gst_info("gstthread: thread \"%s\" is stopped\n",
		  gst_element_get_name (GST_ELEMENT (thread)));
  return NULL;
}

static void 
gst_thread_signal_thread (GstThread *thread) 
{
  g_mutex_lock (thread->lock);
  g_cond_signal (thread->cond);
  g_mutex_unlock (thread->lock);
}

static void 
gst_thread_restore_thyself (GstElement *element,
		            xmlNodePtr parent, 
			    GHashTable *elements) 
{
  g_print("gstthread: restore\n");

  if (GST_ELEMENT_CLASS (parent_class)->restore_thyself)
    GST_ELEMENT_CLASS (parent_class)->restore_thyself (element,parent, elements);
}

static xmlNodePtr 
gst_thread_save_thyself (GstElement *element,
		         xmlNodePtr parent) 
{
  if (GST_ELEMENT_CLASS (parent_class)->save_thyself)
    GST_ELEMENT_CLASS (parent_class)->save_thyself (element,parent);
  return NULL;
}
