/* Async Jitter Queue based on g_async_queue
 *
 * Farsight Voice+Video library
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
 *   @author: Philippe Khalaf <philippe.khalaf@collabora.co.uk>.
 */

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/. 
 */

#ifndef __ASYNCJITTERQUEUE_H__
#define __ASYNCJITTERQUEUE_H__

#include <glib.h>
#include <glib/gthread.h>

G_BEGIN_DECLS

typedef struct _AsyncJitterQueue     AsyncJitterQueue;

/* Asyncronous Queues, can be used to communicate between threads
 */

/* Get a new AsyncJitterQueue with the ref_count 1 */
AsyncJitterQueue*  async_jitter_queue_new                (void);

/* Lock and unlock a AsyncJitterQueue. All functions lock the queue for
 * themselves, but in certain cirumstances you want to hold the lock longer,
 * thus you lock the queue, call the *_unlocked functions and unlock it again.
 */
void          async_jitter_queue_lock               (AsyncJitterQueue *queue);
void          async_jitter_queue_unlock             (AsyncJitterQueue *queue);

/* Ref and unref the AsyncJitterQueue. */
AsyncJitterQueue*  async_jitter_queue_ref           (AsyncJitterQueue *queue);
void          async_jitter_queue_unref              (AsyncJitterQueue *queue);
#ifndef G_DISABLE_DEPRECATED
/* You don't have to hold the lock for calling *_ref and *_unref anymore. */
void          async_jitter_queue_ref_unlocked       (AsyncJitterQueue *queue);
void          async_jitter_queue_unref_and_unlock   (AsyncJitterQueue *queue);
#endif /* !G_DISABLE_DEPRECATED */

void          async_jitter_queue_set_low_threshold  (AsyncJitterQueue *queue,
                                                gfloat threshold);
void          async_jitter_queue_set_high_threshold (AsyncJitterQueue *queue,
                                                gfloat threshold);

void          async_jitter_queue_set_max_queue_length (AsyncJitterQueue *queue,
                                                guint32 max_length);

/* Push data into the async queue. Must not be NULL. */
void          async_jitter_queue_push               (AsyncJitterQueue *queue,
                                                gpointer     data);
void          async_jitter_queue_push_unlocked      (AsyncJitterQueue *queue,
                                                gpointer     data);
gboolean      async_jitter_queue_push_sorted        (AsyncJitterQueue *queue,
                                                gpointer          data,
                                                GCompareDataFunc  func,
                                                gpointer          user_data);

void          async_jitter_queue_insert_after_unlocked(AsyncJitterQueue *queue,
                                                GList *sibling,
                                                gpointer data);

gboolean      async_jitter_queue_push_sorted_unlocked(AsyncJitterQueue *queue,
                                                gpointer          data,
                                                GCompareDataFunc  func,
                                                gpointer          user_data);

/* Pop data from the async queue. When no data is there, the thread is blocked
 * until data arrives. */
gpointer      async_jitter_queue_pop                (AsyncJitterQueue *queue);
gpointer      async_jitter_queue_pop_unlocked       (AsyncJitterQueue *queue);

/* Try to pop data. NULL is returned in case of empty queue. */
gpointer      async_jitter_queue_try_pop            (AsyncJitterQueue *queue);
gpointer      async_jitter_queue_try_pop_unlocked   (AsyncJitterQueue *queue);

/* Wait for data until at maximum until end_time is reached. NULL is returned
 * in case of empty queue. */
gpointer      async_jitter_queue_timed_pop          (AsyncJitterQueue *queue,
                                                GTimeVal    *end_time);
gpointer      async_jitter_queue_timed_pop_unlocked (AsyncJitterQueue *queue,
                                                GTimeVal    *end_time);

/* Return the length of the queue. Negative values mean that threads
 * are waiting, positve values mean that there are entries in the
 * queue. Actually this function returns the length of the queue minus
 * the number of waiting threads, async_jitter_queue_length == 0 could also
 * mean 'n' entries in the queue and 'n' thread waiting. Such can
 * happen due to locking of the queue or due to scheduling. */
gint          async_jitter_queue_length             (AsyncJitterQueue *queue);
gint          async_jitter_queue_length_unlocked    (AsyncJitterQueue *queue);

void          async_jitter_queue_set_flushing_unlocked   (AsyncJitterQueue* queue, 
                                                          GFunc free_func, gpointer user_data);
void          async_jitter_queue_unset_flushing_unlocked (AsyncJitterQueue* queue);
void          async_jitter_queue_set_blocking_unlocked   (AsyncJitterQueue* queue,
                                                          gboolean blocking);
guint32
async_jitter_queue_length_ts_units_unlocked (AsyncJitterQueue *queue);

G_END_DECLS

#endif /* __ASYNCJITTERQUEUE_H__ */

