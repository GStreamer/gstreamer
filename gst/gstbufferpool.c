/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbufferpool.c: Buffer-pool operations
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


#include "gst_private.h"

#include "gstbuffer.h"
/* debugging and error checking */
#include "gstlog.h"
#include "gstinfo.h"

static GstBufferPool *_default_pool = NULL;

static GstBuffer * 	gst_buffer_pool_default_buffer_new	(GstBufferPool *pool, 
								 guint size);
static GstBuffer * 	gst_buffer_pool_default_buffer_copy	(const GstBuffer *buffer);
static void 		gst_buffer_pool_default_buffer_dispose	(GstData *buffer);

void 
_gst_buffer_pool_initialize (void) 
{
  if (_default_pool == NULL)
  {
    _default_pool = gst_buffer_pool_new ();
    GST_INFO (GST_CAT_BUFFER, "Buffer pools are initialized now");
  }
}
/**
 * gst_buffer_pool_default:
 *
 * Get the default buffer pool.
 *
 * Returns: the default buffer pool
 */
GstBufferPool *
gst_buffer_pool_default (void)
{
  return _default_pool;
}
/**
 * gst_buffer_pool_new:
 *
 * Create a new buffer pool.
 *
 * Returns: new buffer pool
 */
GstBufferPool*
gst_buffer_pool_new (void)
{
  GstBufferPool *pool;

  pool = g_new0 (GstBufferPool, 1);
  GST_DEBUG (GST_CAT_BUFFER,"allocating new buffer pool %p\n", pool);
  
  /* init data struct */
  gst_data_init (GST_DATA (pool));
  GST_DATA_TYPE (pool) = GST_DATA_BUFFERPOOL;
  
  /* set functions */
  pool->buffer_new = gst_buffer_pool_default_buffer_new;
  pool->buffer_copy = gst_buffer_pool_default_buffer_copy;
  pool->buffer_dispose = gst_buffer_pool_default_buffer_dispose;
  pool->buffer_free = (GstDataFreeFunction) gst_buffer_free;
  
  return pool;
}
/**
 * gst_buffer_pool_set_buffer_new_function:
 * @pool: the pool to set the buffer create function for
 * @create: the create function
 *
 * Sets the function that will be called when a buffer is created 
 * from this pool.
 */
void 
gst_buffer_pool_set_buffer_new_function (GstBufferPool *pool, 
                                         GstBufferPoolBufferNewFunction create)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (create != NULL);
  
  pool->buffer_new = create;
}
/**
 * gst_buffer_pool_set_buffer_dispose_function:
 * @pool: the pool to set the buffer dispose function for
 * @free: the dispose function
 *
 * Sets the function that will be called when a buffer should be disposed.
 */
void 
gst_buffer_pool_set_buffer_dispose_function (GstBufferPool *pool, 
                                             GstDataFreeFunction dispose)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (dispose != NULL);

  pool->buffer_dispose = dispose;
}
/**
 * gst_buffer_pool_set_buffer_free_function:
 * @pool: the pool to set the buffer free function for
 * @free: the free function
 *
 * Sets the function that will be called when a buffer should be freed.
 */
void 
gst_buffer_pool_set_buffer_free_function (GstBufferPool *pool, 
                                          GstDataFreeFunction free)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (free != NULL);

  pool->buffer_free = free;
}
/**
 * gst_buffer_pool_set_buffer_copy_function:
 * @pool: the pool to set the buffer copy function for
 * @copy: the copy function
 *
 * Sets the function that will be called when a buffer is copied.
 */
void 
gst_buffer_pool_set_buffer_copy_function (GstBufferPool *pool, GstBufferPoolBufferCopyFunction copy)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (copy != NULL);
  
  pool->buffer_copy = copy;
}
/**
 * gst_buffer_pool_set_user_data:
 * @pool: the pool to set the user data for
 * @user_data: any user data to be passed to the create/destroy buffer functions
 * and the destroy hook
 *
 * You can put your private data here.
 */
void 
gst_buffer_pool_set_user_data (GstBufferPool *pool, gpointer user_data)
{
  g_return_if_fail (pool != NULL);

  pool->user_data = user_data;
}

/**
 * gst_buffer_pool_get_user_data:
 * @pool: the pool to get the user data from
 *
 * gets user data
 *
 * Returns: The user data of this bufferpool
 */
gpointer
gst_buffer_pool_get_user_data (GstBufferPool *pool)
{
  g_return_val_if_fail (pool != NULL, NULL);

  return pool->user_data;
}

static GstBuffer* 
gst_buffer_pool_default_buffer_new (GstBufferPool *pool, guint size)
{
  GstBuffer *buffer;
  
  g_return_val_if_fail((buffer = gst_buffer_alloc()), NULL);
  
  GST_INFO (GST_CAT_BUFFER,"creating new buffer %p from pool %p", buffer, pool);

  gst_buffer_init (buffer, pool);

  GST_BUFFER_DATA(buffer) = size > 0 ? g_malloc (size) : NULL;
  GST_BUFFER_SIZE(buffer) = GST_BUFFER_DATA(buffer) ? size : 0;
  
  return buffer;
}
GstBuffer *
gst_buffer_pool_default_buffer_copy (const GstBuffer *buffer)
{
  GstBuffer *newbuf;
  
  /* allocate a new buffer with the right size */
  newbuf = gst_buffer_new (buffer->pool, buffer->size);
  /* copy all relevant data stuff */
  gst_data_copy (GST_DATA (newbuf), GST_DATA (buffer));
  /* copy the data straight across */
  memcpy (newbuf->data, buffer->data, buffer->size);

  return newbuf;
}
static void
gst_buffer_pool_default_buffer_dispose(GstData *buffer)
{
  GstBuffer *buf = GST_BUFFER (buffer);
  
  gst_buffer_dispose (buffer);
  g_free (buf->data);
  buf->data = NULL;
  buf->size = 0;
}
