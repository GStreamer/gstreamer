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
#include <gst/gstbufferpool.h>



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

  pool = g_malloc (sizeof(GstBufferPool));
  DEBUG("BUF: allocating new buffer pool %p\n", pool);

  pool->new_buffer = NULL;
  pool->destroy_buffer = NULL;
  
  return pool;
}

/**
 * gst_buffer_pool_set_create_function:
 * @pool: the pool to set the create function for
 * @create: the create function
 * @user_data: any user data to be passed in the create function
 *
 * Sets the function that will be called when a buffer is created 
 * from this pool.
 */
void 
gst_buffer_pool_set_create_function (GstBufferPool *pool, 
		                     GstBufferPoolCreateFunction create, 
				     gpointer user_data) 
{
  g_return_if_fail (pool != NULL);

  pool->new_buffer = create;
  pool->new_user_data = user_data;
}

/**
 * gst_buffer_pool_set_destroy_function:
 * @pool: the pool to set the destroy function for
 * @destroy: the destroy function
 * @user_data: any user data to be passed in the create function
 *
 * Sets the function that will be called when a buffer is destroyed 
 * from this pool.
 */
void 
gst_buffer_pool_set_destroy_function (GstBufferPool *pool, 
		                      GstBufferPoolDestroyFunction destroy, 
				      gpointer user_data) 
{
  g_return_if_fail (pool != NULL);

  pool->destroy_buffer = destroy;
  pool->destroy_user_data = user_data;
}

/**
 * gst_buffer_pool_destroy:
 * @pool: the pool to destroy
 *
 * frees the memory for this bufferpool
 */
void 
gst_buffer_pool_destroy (GstBufferPool *pool) 
{
  g_return_if_fail (pool != NULL);

  g_free(pool);
}

/**
 * gst_buffer_pool_new_buffer:
 * @pool: the pool to create the buffer from
 *
 * uses the given pool to create a new buffer.
 *
 * Returns: The new buffer
 */
GstBuffer*
gst_buffer_pool_new_buffer (GstBufferPool *pool) 
{
  GstBuffer *buffer;

  g_return_val_if_fail (pool != NULL, NULL);

  buffer = pool->new_buffer (pool, pool->new_user_data);
  buffer->pool = pool;

  return buffer;
}

/**
 * gst_buffer_pool_destroy_buffer:
 * @pool: the pool to return the buffer to
 * @buffer: the buffer to return to the pool
 *
 * Gives a buffer back to the given pool.
 */
void 
gst_buffer_pool_destroy_buffer (GstBufferPool *pool, 
		                GstBuffer *buffer) 
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (buffer != NULL);

  pool->destroy_buffer (pool, buffer, pool->new_user_data);
}
