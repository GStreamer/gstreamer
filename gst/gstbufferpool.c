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

static GMutex *default_pool_lock;
static GHashTable *_default_pools;

static GstBuffer* gst_buffer_pool_default_create (GstBufferPool *pool, gpointer user_data);
static void gst_buffer_pool_default_destroy (GstBufferPool *pool, GstBuffer *buffer, gpointer user_data);

void 
_gst_buffer_pool_initialize (void) 
{
  _default_pools = g_hash_table_new(NULL,NULL);
  default_pool_lock = g_mutex_new ();
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

  pool = g_malloc (sizeof(GstBufferPool));
  GST_DEBUG (0,"BUF: allocating new buffer pool %p\n", pool);

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
 * Frees the memory for this bufferpool.
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
 * Uses the given pool to create a new buffer.
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

GstBufferPool*
gst_buffer_pool_get_default (guint buffer_size, guint pool_size)
{
  GstBufferPool *pool;
  GMemChunk *data_chunk;
  guint real_buffer_size;
  
  // check for an existing GstBufferPool with the same buffer_size
  // (we won't worry about the pool_size)
  if ((pool = (GstBufferPool*)g_hash_table_lookup(_default_pools,GINT_TO_POINTER(buffer_size)))){
    return pool;
  }
  
  // g_print("new buffer pool bytes:%d size:%d\n", buffer_size, pool_size);
  
  // round up to the nearest 32 bytes for cache-line and other efficiencies
  real_buffer_size = ((buffer_size-1 / 32) + 1) * 32;

  data_chunk = g_mem_chunk_new ("GstBufferPoolDefault", real_buffer_size, 
    real_buffer_size * pool_size, G_ALLOC_AND_FREE);
    
  pool = gst_buffer_pool_new();
  gst_buffer_pool_set_create_function	(pool, gst_buffer_pool_default_create, data_chunk);
  gst_buffer_pool_set_destroy_function	(pool, gst_buffer_pool_default_destroy, data_chunk);

  g_hash_table_insert(_default_pools,GINT_TO_POINTER(buffer_size),pool);
  return pool;
}

static GstBuffer* 
gst_buffer_pool_default_create (GstBufferPool *pool, gpointer user_data)
{
  GMemChunk *data_chunk = (GMemChunk*)user_data;
  GstBuffer *buffer;
  
  buffer = gst_buffer_new();
  GST_BUFFER_FLAG_SET(buffer,GST_BUFFER_DONTFREE);
  buffer->pool = pool;

  g_mutex_lock (default_pool_lock);
  GST_BUFFER_DATA(buffer) = g_mem_chunk_alloc(data_chunk);
  g_mutex_unlock (default_pool_lock);
    
  return buffer;
}

static void
gst_buffer_pool_default_destroy (GstBufferPool *pool, GstBuffer *buffer, gpointer user_data)
{
  GMemChunk *data_chunk = (GMemChunk*)user_data;
  gpointer data = GST_BUFFER_DATA(buffer);

  g_mutex_lock (default_pool_lock);
  g_mem_chunk_free (data_chunk,data);
  g_mutex_unlock (default_pool_lock);

  buffer->pool = NULL;
  gst_buffer_destroy (buffer);
  
}
