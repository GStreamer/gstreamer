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

#include <gst/gst.h>
#include <gst/gstthread.h>

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


static void gst_thread_class_init(GstThreadClass *klass);
static void gst_thread_init(GstThread *thread);

static void gst_thread_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_thread_get_arg(GtkObject *object,GtkArg *arg,guint id);
static GstElementStateReturn gst_thread_change_state(GstElement *element);

static xmlNodePtr gst_thread_save_thyself(GstElement *element,xmlNodePtr parent);

static void gst_thread_prepare(GstThread *thread);
static void gst_thread_signal_thread(GstThread *thread);


static GstBin *parent_class = NULL;
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
    thread_type = gtk_type_unique(gst_bin_get_type(),&thread_info);
  }
  return thread_type;
}

static void
gst_thread_class_init(GstThreadClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(gst_bin_get_type());

  gtk_object_add_arg_type("GstThread::create_thread", GTK_TYPE_BOOL,
                          GTK_ARG_READWRITE, ARG_CREATE_THREAD);

  gstelement_class->change_state = gst_thread_change_state;
  gstelement_class->save_thyself = gst_thread_save_thyself;

  gtkobject_class->set_arg = gst_thread_set_arg;
  gtkobject_class->get_arg = gst_thread_get_arg;
}

static void gst_thread_init(GstThread *thread) {
  GST_FLAG_SET(thread,GST_THREAD_CREATE);

//  thread->entries = NULL;
//  thread->numentries = 0;

  thread->lock = g_mutex_new();
  thread->cond = g_cond_new();
}

static void gst_thread_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_THREAD(object));

  switch(id) {
    case ARG_CREATE_THREAD:
      if (GTK_VALUE_BOOL(*arg)) {
        gst_info("gstthread: turning ON the creation of the thread\n");
        GST_FLAG_SET(object,GST_THREAD_CREATE);
        gst_info("gstthread: flags are 0x%08x\n",GST_FLAGS(object));
      } else {
        gst_info("gstthread: turning OFF the creation of the thread\n");
        GST_FLAG_UNSET(object,GST_THREAD_CREATE);
        gst_info("gstthread: flags are 0x%08x\n",GST_FLAGS(object));
      }
      break;
    default:
      break;
  }
}

static void gst_thread_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_THREAD(object));

  switch(id) {
    case ARG_CREATE_THREAD:
      GTK_VALUE_BOOL(*arg) = GST_FLAG_IS_SET(object,GST_THREAD_CREATE);
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
GstElement *gst_thread_new(guchar *name) {
  GstThread *thread;

  thread = gtk_type_new(gst_thread_get_type());
  gst_element_set_name(GST_ELEMENT(thread),name);
  return GST_ELEMENT(thread);
}


#ifdef OLD_STUFF
static void gst_thread_prepare(GstThread *thread) {
  GList *elements;
  GstElement *element;
  GList *pads;
  GstPad *pad, *peer;
  GstElement *outside;

  thread->numentries = 0;

  /* first we need to find all the entry points into the thread */
  elements = GST_BIN(thread)->children;
  while (elements) {
    element = GST_ELEMENT(elements->data);
    if (GST_IS_SRC(element)) {
      gst_info("gstthread: element \"%s\" is a source entry point for the thread\n",
               gst_element_get_name(GST_ELEMENT(element)));
      thread->entries = g_list_prepend(thread->entries,element);
      thread->numentries++;
    } else {
      /* go through the list of pads to see if there's a Connection */
      pads = gst_element_get_pad_list(element);
      while (pads) {
        pad = GST_PAD(pads->data);
        /* we only worry about sink pads */
        if (gst_pad_get_direction(pad) == GST_PAD_SINK) {
          /* get the pad's peer */
          peer = gst_pad_get_peer(pad);
          if (!peer) break;
          /* get the parent of the peer of the pad */
          outside = GST_ELEMENT(gst_pad_get_parent(peer));
          if (!outside) break;
          /* if it's a connection and it's not ours... */
          if (GST_IS_CONNECTION(outside) &&
              (gst_object_get_parent(GST_OBJECT(outside)) != GST_OBJECT(thread))) {
            gst_info("gstthread: element \"%s\" is the external source Connection \
for internal element \"%s\"\n",
                    gst_element_get_name(GST_ELEMENT(outside)),
                    gst_element_get_name(GST_ELEMENT(element)));
            thread->entries = g_list_prepend(thread->entries,outside);
            thread->numentries++;
          }
        }
        pads = g_list_next(pads);
      }
    }
    elements = g_list_next(elements);
  }
  gst_info("gstthread: have %d entries into thread\n",thread->numentries);
}
#endif


static GstElementStateReturn gst_thread_change_state(GstElement *element) {
  GstThread *thread;
  gboolean stateset = TRUE;

/*
  g_return_val_if_fail(GST_IS_THREAD(element), FALSE);
  thread = GST_THREAD(element);

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    stateset = GST_ELEMENT_CLASS(parent_class)->change_state(element,state);

  switch (state) {
    case GST_STATE_READY:
      if (!stateset) return FALSE;
      // we want to prepare our internal state for doing the iterations
      gst_info("preparing thread \"%s\" for iterations:\n",
               gst_element_get_name(GST_ELEMENT(element)));
//      gst_thread_prepare(thread);
      gst_bin_create_plan(GST_BIN(thread));
//      if (thread->numentries == 0)
//        return FALSE;
      // set the state to idle
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      // create the thread if that's what we're supposed to do
      gst_info("flags are 0x%08x\n",GST_FLAGS(thread));
      if (GST_FLAG_IS_SET(thread,GST_THREAD_CREATE)) {
        gst_info("gstthread: starting thread \"%s\"\n",
                 gst_element_get_name(GST_ELEMENT(element)));
        pthread_create(&thread->thread_id,NULL,
                       gst_thread_main_loop,thread);
      } else {
        gst_info("gstthread: NOT starting thread \"%s\"\n",
                gst_element_get_name(GST_ELEMENT(element)));
      }
      return TRUE;
      break;
#if OLDSTATE
    case ~GST_STATE_RUNNING:
      // stop, reap, and join the thread
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      GST_FLAG_SET(thread,GST_THREAD_STATE_REAPING);
      gst_thread_signal_thread(thread);
      pthread_join(thread->thread_id,0);
      // tear down the internal state
      gst_info("tearing down thread's iteration state\n");
      // FIXME do stuff
      break;
#endif
    case GST_STATE_PLAYING:
      if (!stateset) return FALSE;
      gst_info("gstthread: starting thread \"%s\"\n",
              gst_element_get_name(GST_ELEMENT(element)));
      GST_FLAG_SET(thread,GST_THREAD_STATE_SPINNING);
      gst_thread_signal_thread(thread);
      return TRUE;
      break;  
    case ~GST_STATE_PLAYING:
      gst_info("gstthread: stopping thread \"%s\"\n",
              gst_element_get_name(GST_ELEMENT(element)));
      GST_FLAG_UNSET(thread,GST_THREAD_STATE_SPINNING);
      gst_thread_signal_thread(thread);
      break;
    default:
      break;
  }
*/

  return stateset;
}

/**
 * gst_thread_main_loop:
 * @arg: the thread to start
 *
 * The main loop of the thread. The thread will iterate
 * while the state is GST_THREAD_STATE_SPINNING
 */
void *gst_thread_main_loop(void *arg) {
  GstThread *thread = GST_THREAD(arg);

  gst_info("gstthread: thread \"%s\" is running with PID %d\n",
		  gst_element_get_name(GST_ELEMENT(thread)), getpid());

  while(!GST_FLAG_IS_SET(thread,GST_THREAD_STATE_REAPING)) {
    if (GST_FLAG_IS_SET(thread,GST_THREAD_STATE_SPINNING))
      gst_bin_iterate(GST_BIN(thread));
    else {
      g_mutex_lock(thread->lock);
      g_cond_wait(thread->cond,thread->lock);
      g_mutex_unlock(thread->lock);
    }
  }

  GST_FLAG_UNSET(thread,GST_THREAD_STATE_REAPING);
  //pthread_join(thread->thread_id,0);

  gst_info("gstthread: thread \"%s\" is stopped\n",
		  gst_element_get_name(GST_ELEMENT(thread)));
  return NULL;
}

#ifdef OLD_STUFF
/**
 * gst_thread_iterate:
 * @thread: the thread to iterate
 *
 * do one iteration
 */
void gst_thread_iterate(GstThread *thread) {
  GList *entries;
  GstElement *entry;

  g_return_if_fail(thread != NULL);
  g_return_if_fail(GST_IS_THREAD(thread));
//  g_return_if_fail(GST_FLAG_IS_SET(thread,GST_STATE_RUNNING));
  g_return_if_fail(thread->numentries > 0);

  entries = thread->entries;

  DEBUG("gstthread: %s: thread iterate\n", gst_element_get_name(GST_ELEMENT(thread)));

  while (entries) {
    entry = GST_ELEMENT(entries->data);
    if (GST_IS_SRC(entry))
      gst_src_push(GST_SRC(entry));
    else if (GST_IS_CONNECTION(entry))
      gst_connection_push(GST_CONNECTION(entry));
    else
      g_assert_not_reached();
    entries = g_list_next(entries);
  }
  DEBUG("gstthread: %s: thread iterate done\n", gst_element_get_name(GST_ELEMENT(thread)));
  //g_print(",");
}
#endif

static void gst_thread_signal_thread(GstThread *thread) {
  g_mutex_lock(thread->lock);
  g_cond_signal(thread->cond);
  g_mutex_unlock(thread->lock);
}

static xmlNodePtr gst_thread_save_thyself(GstElement *element,xmlNodePtr parent) {
  xmlNewChild(parent,NULL,"type","thread");

  if (GST_ELEMENT_CLASS(parent_class)->save_thyself)
    GST_ELEMENT_CLASS(parent_class)->save_thyself(element,parent);
	return NULL;
}
