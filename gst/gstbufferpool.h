/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbufferpool.h: Header for buffer-pool management
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

#ifndef __GST_BUFFER_POOL_H__
#define __GST_BUFFER_POOL_H__

#include <gst/gstbuffer.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_BUFFER_POOL(pool) \
  ((GstBufferPool *)(pool))
#define GST_BUFFER_POOL_LOCK(pool)	(g_mutex_lock(GST_BUFFER_POOL(pool)->lock))
#define GST_BUFFER_POOL_UNLOCK(pool)	(g_mutex_unlock(GST_BUFFER_POOL(pool)->lock))

typedef struct _GstBufferPool GstBufferPool;

typedef GstBuffer*	(*GstBufferPoolBufferCreateFunction)  (GstBufferPool *pool, guint64 location, gint size, gpointer user_data);
typedef void 		(*GstBufferPoolBufferDestroyFunction) (GstBufferPool *pool, GstBuffer *buffer, gpointer user_data);
typedef void 		(*GstBufferPoolPoolDestroyHook)       (GstBufferPool *pool, gpointer user_data);

struct _GstBufferPool {
  /* locking */
  GMutex *lock;

  /* refcounting */
#ifdef HAVE_ATOMIC_H
  atomic_t refcount;
#define GST_BUFFER_POOL_REFCOUNT(pool)	(atomic_read(&(GST_BUFFER_POOL((pool))->refcount)))
#else
  int refcount;
#define GST_BUFFER_POOL_REFCOUNT(pool)	(GST_BUFFER_POOL(pool)->refcount)
#endif

  /* will be called when a new buffer is to be created */
  GstBufferPoolBufferCreateFunction new_buffer;
  /* user data to pass with the new_buffer function */
  gpointer new_buffer_user_data;

  GstBufferPoolBufferDestroyFunction destroy_buffer;
  gpointer destroy_buffer_user_data;

  GstBufferPoolPoolDestroyHook destroy_pool_hook;
  gpointer destroy_pool_user_data;
  
  gpointer private_data;
};

void _gst_buffer_pool_initialize (void);

/* creating a new buffer pool from scratch */
GstBufferPool*		gst_buffer_pool_new			(void);

/* refcounting */
void 		gst_buffer_pool_ref			(GstBufferPool *pool);
void 		gst_buffer_pool_ref_by_count		(GstBufferPool *pool, int count);
void 		gst_buffer_pool_unref			(GstBufferPool *buffer);

/* setting create and destroy functions */
void 		gst_buffer_pool_set_buffer_create_function	(GstBufferPool *pool, 
                                                                 GstBufferPoolBufferCreateFunction create, 
                                                                 gpointer user_data);
void 		gst_buffer_pool_set_buffer_destroy_function	(GstBufferPool *pool, 
                                                                 GstBufferPoolBufferDestroyFunction destroy, 
                                                                 gpointer user_data);
void 		gst_buffer_pool_set_pool_destroy_hook		(GstBufferPool *pool, 
                                                                 GstBufferPoolPoolDestroyHook destroy, 
                                                                 gpointer user_data);

/* destroying the buffer pool */
void 		gst_buffer_pool_destroy			(GstBufferPool *pool);

/* a default buffer pool implementation */
GstBufferPool* gst_buffer_pool_get_default (GstBufferPool *oldpool, guint buffer_size, guint pool_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BUFFER_POOL_H__ */
