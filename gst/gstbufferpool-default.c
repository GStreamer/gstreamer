/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbufferpool-default.c: Private implementation of the default bufferpool
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
#include "gstinfo.h"
#include "gstmemchunk.h"

/* methods prefixed with underscores to avoid namespace collisions with
 * gstbuffer.c */

static GstBuffer*	_gst_buffer_pool_default_buffer_new	(GstBufferPool *pool,
                                                                 guint64 offset, guint size,
                                                                 gpointer user_data);
static void		_gst_buffer_pool_default_buffer_free	(GstBufferPool *pool,
                                                                 GstBuffer *buffer,
                                                                 gpointer user_data);
static void		_gst_buffer_pool_default_free		(GstData *pool);


typedef struct _GstBufferPoolDefault GstBufferPoolDefault;

struct _GstBufferPoolDefault {
  GstMemChunk *mem_chunk;
  guint size;
};


static GMutex *_default_pool_lock = NULL;
static GHashTable *_default_pools = NULL;


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
 * Returns: an instance of #GstBufferPool
 */
GstBufferPool*
gst_buffer_pool_get_default (guint buffer_size, guint pool_size)
{
  GstBufferPool *pool;
  GstMemChunk *data_chunk;
  guint real_buffer_size;
  GstBufferPoolDefault *def;
  
  if (!_default_pool_lock) {
    _default_pool_lock = g_mutex_new ();
    _default_pools = g_hash_table_new (NULL, NULL);
  }

  /* round up to the nearest 32 bytes for cache-line and other efficiencies */
  real_buffer_size = (((buffer_size-1) / 32) + 1) * 32;
  
  /* check for an existing GstBufferPool with the same real_buffer_size */
  /* (we won't worry about the pool_size) */
  g_mutex_lock (_default_pool_lock);
  pool = (GstBufferPool*)g_hash_table_lookup(_default_pools,GINT_TO_POINTER(real_buffer_size));
  g_mutex_unlock (_default_pool_lock);

  if (pool != NULL){
    gst_buffer_pool_ref (pool);
    return pool;
  }
  
  data_chunk = gst_mem_chunk_new ("GstBufferPoolDefault", real_buffer_size, 
                                  real_buffer_size * pool_size, G_ALLOC_AND_FREE);
    
  def = g_new0 (GstBufferPoolDefault, 1);
  def->size = buffer_size;
  def->mem_chunk = data_chunk;

  pool = gst_buffer_pool_new (_gst_buffer_pool_default_free,
                              NULL, /* pool copy */
                              _gst_buffer_pool_default_buffer_new,
                              NULL, /* buffer copy */
                              _gst_buffer_pool_default_buffer_free,
                              def);
  
  g_mutex_lock (_default_pool_lock);
  g_hash_table_insert (_default_pools, GINT_TO_POINTER (real_buffer_size), pool);
  g_mutex_unlock (_default_pool_lock);
  
  GST_CAT_DEBUG (GST_CAT_BUFFER,"new default buffer pool %p bytes:%d size:%d",
             pool, real_buffer_size, pool_size);
  
  return pool;
}

static GstBuffer* 
_gst_buffer_pool_default_buffer_new (GstBufferPool *pool, guint64 offset,
                                     guint size, gpointer user_data)
{
  GstBuffer *buffer;
  GstBufferPoolDefault *def = (GstBufferPoolDefault*) user_data;
  GstMemChunk *data_chunk = def->mem_chunk;
  
  buffer = gst_buffer_new ();
  GST_CAT_INFO (GST_CAT_BUFFER, "creating new buffer %p from pool %p", buffer, pool);
  
  GST_BUFFER_DATA (buffer) = gst_mem_chunk_alloc (data_chunk);
  
  GST_BUFFER_SIZE (buffer)    = def->size;
  GST_BUFFER_MAXSIZE (buffer) = def->size;
  
  return buffer;
}

static void
_gst_buffer_pool_default_buffer_free (GstBufferPool *pool, GstBuffer *buffer, gpointer user_data)
{
  GstBufferPoolDefault *def = (GstBufferPoolDefault*)user_data;
  GstMemChunk *data_chunk = def->mem_chunk;
  gpointer data = GST_BUFFER_DATA (buffer);
  
  gst_mem_chunk_free (data_chunk, data);

  GST_BUFFER_DATA (buffer) = NULL;

  gst_buffer_default_free (buffer);
}

static void
_gst_buffer_pool_default_free (GstData *data) 
{
  GstBufferPool *pool = (GstBufferPool*) data;
  GstBufferPoolDefault *def = (GstBufferPoolDefault*) pool->user_data;
  GstMemChunk *data_chunk = def->mem_chunk;
  guint real_buffer_size;
  
  real_buffer_size = (((def->size-1) / 32) + 1) * 32;

  GST_CAT_DEBUG (GST_CAT_BUFFER,"destroying default buffer pool %p bytes:%d size:%d",
             pool, real_buffer_size, def->size);
  
  g_mutex_lock (_default_pool_lock);
  g_hash_table_remove (_default_pools, GINT_TO_POINTER (real_buffer_size));
  g_mutex_unlock (_default_pool_lock);
  
  /* this is broken right now, FIXME
     gst_mem_chunk_destroy (data_chunk); */
  
  g_free (data_chunk);
  g_free (def);

  gst_buffer_pool_default_free (pool);
}
