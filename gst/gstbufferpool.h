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


#define GST_BUFFER_POOL(buf) \
  ((GstBufferPool *)(buf))

typedef struct _GstBufferPool GstBufferPool;

typedef GstBuffer*	(*GstBufferPoolCreateFunction) 	(GstBufferPool *pool, gpointer user_data);
typedef void 		(*GstBufferPoolDestroyFunction) (GstBufferPool *pool, GstBuffer *buffer, gpointer user_data);

struct _GstBufferPool {
  /* will be called when a new buffer is to be created */
  GstBufferPoolCreateFunction new_buffer;
  /* user data to pass with the new_buffer function */
  gpointer new_user_data;

  gpointer destroy_user_data;
  GstBufferPoolDestroyFunction destroy_buffer;
};

void _gst_buffer_pool_initialize (void);

/* creating a new buffer pool from scratch */
GstBufferPool*		gst_buffer_pool_new			(void);

/* creating a buffer from the pool */
GstBuffer*		gst_buffer_pool_new_buffer		(GstBufferPool *pool);
void 			gst_buffer_pool_destroy_buffer		(GstBufferPool *pool, GstBuffer *buffer);

/* setting create and destroy functions */
void 			gst_buffer_pool_set_create_function	(GstBufferPool *pool, 
								 GstBufferPoolCreateFunction create, 
								 gpointer user_data);
void 			gst_buffer_pool_set_destroy_function	(GstBufferPool *pool, 
								 GstBufferPoolDestroyFunction destroy, 
								 gpointer user_data);

/* destroying the buffer pool */
void 			gst_buffer_pool_destroy			(GstBufferPool *pool);

/* a default buffer pool implementation */
GstBufferPool* gst_buffer_pool_get_default (guint buffer_size, guint pool_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BUFFER_POOL_H__ */
