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


#include <gstqueue.h>


GstElementDetails gst_queue_details = {
  "Queue",
  "Connection",
  "Simple data queue",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Queue signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LEVEL,
  ARG_MAX_LEVEL,
};


static void gst_queue_class_init(GstQueueClass *klass);
static void gst_queue_init(GstQueue *queue);
static void gst_queue_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_queue_get_arg(GtkObject *object,GtkArg *arg,guint id);

void gst_queue_push(GstConnection *connection);
void gst_queue_chain(GstPad *pad,GstBuffer *buf);

static GstConnectionClass *parent_class = NULL;
//static guint gst_queue_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_queue_get_type(void) {
  static GtkType queue_type = 0;

  if (!queue_type) {
    static const GtkTypeInfo queue_info = {
      "GstQueue",
      sizeof(GstQueue),
      sizeof(GstQueueClass),
      (GtkClassInitFunc)gst_queue_class_init,
      (GtkObjectInitFunc)gst_queue_init,
      (GtkArgSetFunc)gst_queue_set_arg,
      (GtkArgGetFunc)gst_queue_get_arg,
      (GtkClassInitFunc)NULL,
    };
    queue_type = gtk_type_unique(GST_TYPE_CONNECTION,&queue_info);
  }
  return queue_type;
}

static void gst_queue_class_init(GstQueueClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstConnectionClass *gstconnection_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstconnection_class = (GstConnectionClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_CONNECTION);

  gtk_object_add_arg_type("GstQueue::level", GTK_TYPE_INT,
                          GTK_ARG_READABLE, ARG_LEVEL);
  gtk_object_add_arg_type("GstQueue::max_level", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_MAX_LEVEL);

  gstconnection_class->push = gst_queue_push;

  gtkobject_class->set_arg = gst_queue_set_arg;  
  gtkobject_class->get_arg = gst_queue_get_arg;
}

static void gst_queue_init(GstQueue *queue) {
  queue->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(queue),queue->sinkpad);
  gst_pad_set_chain_function(queue->sinkpad,gst_queue_chain);
  queue->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(queue),queue->srcpad);

  queue->queue = NULL;
  queue->tail = NULL;
  queue->level_buffers = 0;
  queue->max_buffers = 10;
  queue->level_bytes = 0;
  queue->size_buffers = 0;
  queue->size_bytes = 0;

  queue->emptylock = g_mutex_new();
  queue->emptycond = g_cond_new();

  queue->fulllock = g_mutex_new();
  queue->fullcond = g_cond_new();
}

GstElement *gst_queue_new(gchar *name) {
  GstElement *queue = GST_ELEMENT(gtk_type_new(GST_TYPE_QUEUE));
  gst_element_set_name(GST_ELEMENT(queue),name);
  return queue;
}

void gst_queue_chain(GstPad *pad,GstBuffer *buf) {
  GstQueue *queue;
  gboolean tosignal = FALSE;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  queue = GST_QUEUE(pad->parent);

  /* we have to lock the queue since we span threads */
  
  GST_LOCK(queue);
  //g_print("queue: chain %d\n", queue->level_buffers);

  if (queue->level_buffers >= queue->max_buffers) {
	  //g_print("queue: waiting %d\n", queue->level_buffers);
    GST_UNLOCK(queue);
    while (queue->level_buffers >= queue->max_buffers) {
      g_mutex_lock(queue->fulllock);
//      g_print("0");
      g_cond_wait(queue->fullcond,queue->fulllock);
      g_mutex_unlock(queue->fulllock);
    }
    GST_LOCK(queue);
	  //g_print("queue: waiting done %d\n", queue->level_buffers);
  }
  

  /* put the buffer on the head of the list */
  /* if the queue is NULL, start a new list and make this the tail */
  if (!queue->queue) {
    queue->queue = g_list_prepend(queue->queue,buf);
//    queue->tail = queue->queue;
  /* otherwise append to the end of the list */
  } else {
//    queue->tail = g_list_append(queue->tail,buf);
//    queue->tail = g_list_next(queue->tail);
    queue->queue = g_list_append(queue->queue,buf);
  }
//  g_print("+");

  /* if we were empty, but aren't any more, signal a condition */
  tosignal = (queue->level_buffers++ == 0);

  /* we can unlock now */
	//g_print("queue: chain %d end\n", queue->level_buffers);
  GST_UNLOCK(queue);

  if (tosignal) {
    g_mutex_lock(queue->emptylock);
    g_cond_signal(queue->emptycond);
    g_mutex_unlock(queue->emptylock);
//    g_print(">");
  }
}

void gst_queue_push(GstConnection *connection) {
  GstQueue *queue = GST_QUEUE(connection);
  GstBuffer *buf = NULL;
  GList *front;
  gboolean tosignal = FALSE;

  /* have to lock for thread-safety */
  GST_LOCK(queue);
	//g_print("queue: push %d\n", queue->level_buffers);

  if (!queue->level_buffers) {
    GST_UNLOCK(queue);
    while (!queue->level_buffers) {
      g_mutex_lock(queue->emptylock);
//      g_print("0");
      g_cond_wait(queue->emptycond,queue->emptylock);
      g_mutex_unlock(queue->emptylock);
    }
    GST_LOCK(queue);
  }

  front = queue->queue;
  buf = (GstBuffer *)(front->data);
  queue->queue = g_list_remove_link(queue->queue,front);
	//g_print("queue: pushing %d\n", queue->level_buffers);
  gst_pad_push(queue->srcpad,buf);
	//g_print("queue: pushing %d done\n", queue->level_buffers);
  g_list_free(front);
  queue->level_buffers--;

  tosignal = queue->level_buffers < queue->max_buffers;
	//g_print("queue: push end %d\n", queue->level_buffers);
  GST_UNLOCK(queue);

  if (tosignal) {
    g_mutex_lock(queue->fulllock);
    g_cond_signal(queue->fullcond);
    g_mutex_unlock(queue->fulllock);
  }
  

  /* unlock now */
}


static void gst_queue_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_QUEUE(object));
  queue = GST_QUEUE(object);

  switch(id) {
    case ARG_MAX_LEVEL:
      queue->max_buffers = GTK_VALUE_INT(*arg);
      break;
    default:
      break;
  }
}

static void gst_queue_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstQueue *queue;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_QUEUE(object));
  queue = GST_QUEUE(object);

  switch (id) {
    case ARG_LEVEL:
      GTK_VALUE_INT(*arg) = queue->level_buffers;
      break;
    case ARG_MAX_LEVEL:
      GTK_VALUE_INT(*arg) = queue->max_buffers;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}
