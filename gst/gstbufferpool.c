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

static GMutex *_default_pool_lock;
static GHashTable *_default_pools;

static GstBuffer* gst_buffer_pool_default_buffer_new (GstBufferPool *pool, gint64 location, gint size, gpointer user_data);
static void gst_buffer_pool_default_buffer_free (GstBuffer *buffer);
static void gst_buffer_pool_default_destroy_hook (GstBufferPool *pool, gpointer user_data);

typedef struct _GstBufferPoolDefault GstBufferPoolDefault;

struct _GstBufferPoolDefault {
  GMemChunk *mem_chunk;
  guint size;
};

void 
_gst_buffer_pool_initialize (void) 
{
  _default_pools = g_hash_table_new(NULL,NULL);
  _default_pool_lock = g_mutex_new ();
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
  
  /* all hooks and user data set to NULL or 0 by g_new0 */
  
  pool->lock = g_mutex_new ();
#ifdef HAVE_ATOMIC_H
  atomic_set (&pool->refcount, 1);
#else
  pool->refcount = 1;
#endif
  
  return pool;
}

/**
 * gst_buffer_pool_ref:
 * @pool: the GstBufferPool to reference
 *
 * Increment the refcount of this buffer pool.
 */
void 
gst_buffer_pool_ref (GstBufferPool *pool) 
{
  g_return_if_fail (pool != NULL);

  GST_DEBUG(GST_CAT_BUFFER,"referencing buffer pool %p from %d\n", pool, GST_BUFFER_POOL_REFCOUNT(pool));
  
#ifdef HAVE_ATOMIC_H
  atomic_inc (&(pool->refcount));
#else
  g_return_if_fail (pool->refcount > 0);
  GST_BUFFER_POOL_LOCK (pool);
  pool->refcount++;
  GST_BUFFER_POOL_UNLOCK (pool);
#endif
}

/**
 * gst_buffer_pool_ref_by_count:
 * @pool: the GstBufferPool to reference
 * @count: a number
 *
 * Increment the refcount of this buffer pool by the given number.
 */
void 
gst_buffer_pool_ref_by_count (GstBufferPool *pool, int count) 
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (count > 0);

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(pool->refcount)) > 0);
  atomic_add (count, &(pool->refcount));
#else
  g_return_if_fail (pool->refcount > 0);
  GST_BUFFER_POOL_LOCK (pool);
  pool->refcount += count;
  GST_BUFFER_POOL_UNLOCK (pool);
#endif
}

/**
 * gst_buffer_pool_unref:
 * @pool: the GstBufferPool to unref
 *
 * Decrement the refcount of this buffer pool. If the refcount is
 * zero and the pool is a default implementation, 
 * the buffer pool will be destroyed.
 */
void 
gst_buffer_pool_unref (GstBufferPool *pool) 
{
  gint zero;

  g_return_if_fail (pool != NULL);

  GST_DEBUG(GST_CAT_BUFFER, "unreferencing buffer pool %p from %d\n", pool, GST_BUFFER_POOL_REFCOUNT(pool));

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(pool->refcount)) > 0);
  zero = atomic_dec_and_test (&(pool->refcount));
#else
  g_return_if_fail (pool->refcount > 0);
  GST_BUFFER_POOL_LOCK (pool);
  pool->refcount--;
  zero = (pool->refcount == 0);
  GST_BUFFER_POOL_UNLOCK (pool);
#endif

  /* if we ended up with the refcount at zero, destroy the buffer pool*/
  if (zero) {
    gst_buffer_pool_destroy (pool);
  }
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
  
  pool->buffer_new = create;
}

/**
 * gst_buffer_pool_set_buffer_free_function:
 * @pool: the pool to set the buffer free function for
 * @destroy: the free function
 *
 * Sets the function that will be called when a buffer is freed
 * from this pool.
 */
void 
gst_buffer_pool_set_buffer_free_function (GstBufferPool *pool, 
                                          GstBufferFreeFunc destroy)
{
  g_return_if_fail (pool != NULL);
  
  pool->buffer_free = destroy;
}

/**
 * gst_buffer_pool_set_buffer_copy_function:
 * @pool: the pool to set the buffer copy function for
 * @destroy: the copy function
 *
 * Sets the function that will be called when a buffer is copied.
 *
 * You may use the default GstBuffer implementation (gst_buffer_copy).
 */
void 
gst_buffer_pool_set_buffer_copy_function (GstBufferPool *pool, 
                                          GstBufferCopyFunc copy)
{
  g_return_if_fail (pool != NULL);
  
  pool->buffer_copy = copy;
}

/**
 * gst_buffer_pool_set_pool_destroy_hook:
 * @pool: the pool to set the destroy hook for
 * @destroy: the destroy function
 *
 * Sets the function that will be called before a bufferpool is destroyed.
 * You can take care of you private_data here.
 */
void 
gst_buffer_pool_set_destroy_hook (GstBufferPool *pool, 
                                  GstBufferPoolDestroyHook destroy)
{
  g_return_if_fail (pool != NULL);
  
  pool->destroy_hook = destroy;
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
gst_buffer_pool_set_user_data (GstBufferPool *pool, 
                               gpointer user_data)
{
  g_return_if_fail (pool != NULL);

  pool->user_data = user_data;
}

/**
 * gst_buffer_pool_get_user_data:
 * @pool: the pool to get the user data from
 * @user_data: any user data to be passed to the create/destroy buffer functions
 * and the destroy hook
 *
 * gets user data
 */
gpointer
gst_buffer_pool_get_user_data (GstBufferPool *pool, 
                               gpointer user_data)
{
  g_return_val_if_fail (pool != NULL, NULL);

  return pool->user_data;
}

/**
 * gst_buffer_pool_destroy:
 * @pool: the pool to destroy
 *
 * Frees the memory for this bufferpool, calls the destroy hook.
 */
void 
gst_buffer_pool_destroy (GstBufferPool *pool) 
{
  g_return_if_fail (pool != NULL);
  
  if (pool->destroy_hook)
    pool->destroy_hook (pool, pool->user_data);
  
  g_free(pool);
}

//
// This is so we don't get messed up by GST_BUFFER_WHERE.
//
static GstBuffer *
_pool_gst_buffer_copy (GstBuffer *buffer)
{ return gst_buffer_copy (buffer); }

/**
 * gst_buffer_pool_get_default:
 * @buffer_size: the number of bytes this buffer will store
 * @pool_size: the default number of buffers to be preallocated
 *
 * Returns an instance of a buffer pool using the default
 * implementation.  If a buffer pool instance with the same buffer_size
 * already exists this will be returned, otherwise a new instance will
 * be created.
 * 
 * Returns: an instance of GstBufferPool
 */
GstBufferPool*
gst_buffer_pool_get_default (guint buffer_size, guint pool_size)
{
  GstBufferPool *pool;
  GMemChunk *data_chunk;
  guint real_buffer_size;
  GstBufferPoolDefault *def;
  
  // round up to the nearest 32 bytes for cache-line and other efficiencies
  real_buffer_size = (((buffer_size-1) / 32) + 1) * 32;
  
  // check for an existing GstBufferPool with the same real_buffer_size
  // (we won't worry about the pool_size)
  g_mutex_lock (_default_pool_lock);
  pool = (GstBufferPool*)g_hash_table_lookup(_default_pools,GINT_TO_POINTER(real_buffer_size));
  g_mutex_unlock (_default_pool_lock);

  if (pool != NULL){
    gst_buffer_pool_ref(pool);
    return pool;
  }
  
  data_chunk = g_mem_chunk_new ("GstBufferPoolDefault", real_buffer_size, 
    real_buffer_size * pool_size, G_ALLOC_AND_FREE);
    
  pool = gst_buffer_pool_new();
  gst_buffer_pool_set_buffer_new_function (pool, gst_buffer_pool_default_buffer_new);
  gst_buffer_pool_set_buffer_free_function (pool, gst_buffer_pool_default_buffer_free);
  gst_buffer_pool_set_buffer_copy_function (pool, _pool_gst_buffer_copy);
  gst_buffer_pool_set_destroy_hook (pool, gst_buffer_pool_default_destroy_hook);
  
  def = g_new0 (GstBufferPoolDefault, 1);
  def->size = buffer_size;
  def->mem_chunk = data_chunk;
  pool->user_data = def;
  
  g_mutex_lock (_default_pool_lock);
  g_hash_table_insert(_default_pools,GINT_TO_POINTER(real_buffer_size),pool);
  g_mutex_unlock (_default_pool_lock);
  
  GST_DEBUG(GST_CAT_BUFFER,"new buffer pool %p bytes:%d size:%d\n", pool, real_buffer_size, pool_size);
  
  return pool;
}

static GstBuffer* 
gst_buffer_pool_default_buffer_new (GstBufferPool *pool, gint64 location /*unused*/,
                                    gint size /*unused*/, gpointer user_data)
{
  GstBuffer *buffer;
  GstBufferPoolDefault *def = (GstBufferPoolDefault*) user_data;
  GMemChunk *data_chunk = def->mem_chunk;
  
  gst_buffer_pool_ref(pool);
  buffer = gst_buffer_new();
  GST_INFO (GST_CAT_BUFFER,"creating new buffer %p from pool %p",buffer,pool);
  
  g_mutex_lock (pool->lock);
  GST_BUFFER_DATA(buffer) = g_mem_chunk_alloc(data_chunk);
  g_mutex_unlock (pool->lock);
  
  GST_BUFFER_SIZE(buffer)    = def->size;
  GST_BUFFER_MAXSIZE(buffer) = def->size;
  
  return buffer;
}

static void
gst_buffer_pool_default_buffer_free (GstBuffer *buffer)
{
  GstBufferPool *pool = buffer->pool;
  GstBufferPoolDefault *def = (GstBufferPoolDefault*) pool->user_data;
  GMemChunk *data_chunk = def->mem_chunk;
  gpointer data = GST_BUFFER_DATA(buffer);
  
  g_mutex_lock (pool->lock);
  g_mem_chunk_free (data_chunk,data);
  g_mutex_unlock (pool->lock);

  buffer->pool = NULL;
  gst_buffer_pool_unref(pool);
}

static void
gst_buffer_pool_default_destroy_hook (GstBufferPool *pool, gpointer user_data) 
{
  GstBufferPoolDefault *def = (GstBufferPoolDefault*) user_data;
  GMemChunk *data_chunk = def->mem_chunk;
  
  GST_DEBUG(GST_CAT_BUFFER,"destroying default buffer pool %p\n", pool);
  
  g_mutex_free (pool->lock);
  g_mem_chunk_reset(data_chunk);
  g_free(data_chunk);
  g_free(def);
}
