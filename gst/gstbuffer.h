/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbuffer.h: Header for GstBuffer object
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


#ifndef __GST_BUFFER_H__
#define __GST_BUFFER_H__

/*
 * Define this to add file:line info to each GstBuffer showing
 * the location in the source code where the buffer was created.
 * 
 * #define GST_BUFFER_WHERE
 *
 * Then in gdb, you can `call gst_buffer_print_live()' to get a list
 * of allocated GstBuffers and also the file:line where they were
 * allocated.
 */

#include <gst/gstdata.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstBuffer GstBuffer;
typedef struct _GstBufferPool GstBufferPool;

/*
#define GST_BUFFER(buf)		 	(((GstData*)(buf))->type == GST_BUFFER ? (GstBuffer *)(buf) : NULL)
*/
#define GST_BUFFER(buf)		 	((GstBuffer *)(buf))
#define GST_IS_BUFFER(buf)		(((GstData*)(buf))->type == GST_DATA_BUFFER)
  
#ifdef HAVE_ATOMIC_H
#  define GST_BUFFER_REFCOUNT(buf)	(atomic_read(&(GST_DATA(buf)->refcount)))
#else
#  define GST_BUFFER_REFCOUNT(buf)	(GST_DATA(buf)->refcount)
#endif

#define GST_BUFFER_DATA(buf)		(GST_BUFFER(buf)->data)
#define GST_BUFFER_SIZE(buf)		(GST_BUFFER(buf)->size)
#define GST_BUFFER_OFFSET(buf)		(GST_DATA(buf)->offset[GST_OFFSET_BYTES])
#define GST_BUFFER_MAXSIZE(buf)		(GST_BUFFER(buf)->maxsize)
#define GST_BUFFER_TIMESTAMP(buf)	(GST_DATA(buf)->offset[GST_OFFSET_TIME])
#define GST_BUFFER_MAXAGE(buf)		(GST_BUFFER(buf)->maxage)
#define GST_BUFFER_BUFFERPOOL(buf)	(GST_BUFFER(buf)->pool)
#define GST_BUFFER_PARENT(buf)		(GST_BUFFER(buf)->parent)
#define GST_BUFFER_POOL_PRIVATE(buf)	(GST_BUFFER(buf)->pool_private)
#define GST_BUFFER_COPY_FUNC(buf)	(GST_BUFFER(buf)->copy)
#define GST_BUFFER_FREE_FUNC(buf)	(GST_BUFFER(buf)->free)


#define GST_BUFFER_LOCK(buf)	(g_mutex_lock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_TRYLOCK(buf)	(g_mutex_trylock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_UNLOCK(buf)	(g_mutex_unlock(GST_BUFFER(buf)->lock))

enum {
  GST_BUFFER_ORIGINAL = GST_DATA_FLAG_LAST,
  GST_BUFFER_FLAG_LAST,
};

/* bufferpools */
#define GST_BUFFER_POOL(pool) ((GstBufferPool *)(pool))

typedef GstBuffer *	(*GstBufferPoolBufferNewFunction)	(GstBufferPool *pool, guint size);
typedef GstBuffer * 	(*GstBufferPoolBufferCopyFunction)	(GstBufferPool *pool, const GstBuffer *buffer, guint offset, guint size);


struct _GstBuffer {
  GstData 		data_type;

  /* pointer to data and its size */
  gpointer 		data;
  guint 		size;

  /* this is a pointer to the buffer pool (if any) */
  GstBufferPool *	pool;
  gpointer 		pool_private;
};

struct _GstBufferPool {
  /* easiest way to get refcounting */
  GstData 				data;

  GstBufferPoolBufferNewFunction 	buffer_new;
  GstBufferPoolBufferCopyFunction	buffer_copy;
  GstDataFreeFunction			buffer_dispose;
  GstDataFreeFunction			buffer_free;

  gpointer				user_data;
};


/* initialization */
void		_gst_buffer_initialize		(void);

/* allocation */
GstBuffer *	gst_buffer_alloc 		(void);
void 		gst_buffer_free 		(GstBuffer *buffer);

/* inheritance */
void		gst_buffer_init			(GstBuffer *buffer, GstBufferPool *pool);
void		gst_buffer_dispose 		(GstData *data);

/* creating a new buffer from scratch */
GstBuffer*	gst_buffer_new			(GstBufferPool *pool, guint size);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint offset, guint size);

/* copy buffer */
#define		gst_buffer_copy(buffer)		gst_buffer_copy_part_from_pool (buffer->pool, buffer, 0, buffer->size);
GstBuffer*	gst_buffer_copy_part_from_pool	(GstBufferPool *pool, const GstBuffer *buffer, guint offset, guint size);

/* merge, span, or append two buffers, intelligently */
GstBuffer*	gst_buffer_merge		(GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span			(GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len);
GstBuffer*	gst_buffer_append		(GstBuffer *buffer, GstBuffer *append);
gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);


/* creating a new buffer pool from scratch */
GstBufferPool *		gst_buffer_pool_new			(void);

/* setting create and destroy functions */
void 		gst_buffer_pool_set_buffer_new_function	        (GstBufferPool *pool, 
                                                                 GstBufferPoolBufferNewFunction create);
void 		gst_buffer_pool_set_buffer_copy_function	(GstBufferPool *pool, 
                                                                 GstBufferPoolBufferCopyFunction copy); 
void 		gst_buffer_pool_set_buffer_dispose_function	(GstBufferPool *pool, 
                                                                 GstDataFreeFunction dispose); 
void 		gst_buffer_pool_set_buffer_free_function	(GstBufferPool *pool, 
                                                                 GstDataFreeFunction free); 
void 		gst_buffer_pool_set_user_data			(GstBufferPool *pool, 
                                                                 gpointer user_data);
gpointer	gst_buffer_pool_get_user_data			(GstBufferPool *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BUFFER_H__ */
