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


#define GST_BUFFER_POOL(pool) ((GstBufferPool *)(pool))

typedef struct _GstBufferPool GstBufferPool;

typedef GstBuffer *	(*GstBufferPoolBufferNewFunction)	(GstBufferPool *pool, guint size);
typedef GstBuffer * 	(*GstBufferPoolBufferCopyFunction)	(const GstBuffer *buffer);

struct _GstBufferPool {
  /* easiest way to get refcounting */
  GstData 				data;

  GstBufferPoolBufferNewFunction 	buffer_new;
  GstBufferPoolBufferCopyFunction	buffer_copy;
  GstDataFreeFunction			buffer_dispose;
  GstDataFreeFunction			buffer_free;

  gpointer				user_data;
};

void 			_gst_buffer_pool_initialize 		(void);

/* creating a new buffer pool from scratch */
GstBufferPool *		gst_buffer_pool_new			(void);

/* getting the default bufferpool */
GstBufferPool *		gst_buffer_pool_default			(void);


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


#endif /* __GST_BUFFER_POOL_H__ */
