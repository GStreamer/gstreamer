/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbuffer.c: Buffer operations
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

/* this file makes too much noise for most debugging sessions */
#define GST_DEBUG_FORCE_DISABLE
#include "gst_private.h"

#include "gstdata_private.h"
#include "gstbuffer.h"
#include "gstmemchunk.h"
#include "gstlog.h"
#include "gstbufferpool-default.h"

GType _gst_buffer_type;
GType _gst_buffer_pool_type;

static gint _gst_buffer_live;
static gint _gst_buffer_pool_live;

static GstMemChunk *chunk;

void
_gst_buffer_initialize (void)
{
  _gst_buffer_type = g_boxed_type_register_static ("GstBuffer",
		                                   (GBoxedCopyFunc) gst_data_ref,
						   (GBoxedFreeFunc) gst_data_unref);

  _gst_buffer_pool_type = g_boxed_type_register_static ("GstBufferPool",
		                                   (GBoxedCopyFunc) gst_data_ref,
						   (GBoxedFreeFunc) gst_data_unref);

  _gst_buffer_live = 0;
  _gst_buffer_pool_live = 0;

  chunk = gst_mem_chunk_new ("GstBufferChunk", sizeof (GstBuffer), sizeof (GstBuffer) * 200, 0);

  GST_INFO (GST_CAT_BUFFER, "Buffers are initialized now");
}

/**
 * gst_buffer_print_stats:
 *
 * Logs statistics about live buffers (using g_log).
 */
void
gst_buffer_print_stats (void)
{
  g_log (g_log_domain_gstreamer, G_LOG_LEVEL_INFO,
                  "%d live buffer(s)", _gst_buffer_live);
  g_log (g_log_domain_gstreamer, G_LOG_LEVEL_INFO,
                  "%d live bufferpool(s)", _gst_buffer_pool_live);
}

static void
_gst_buffer_free_to_pool (GstBuffer *buffer)
{
  GstBufferPool *pool = buffer->pool;

  pool->buffer_free (pool, buffer, pool->user_data);

  gst_data_unref (GST_DATA (pool));
}

static void
_gst_buffer_sub_free (GstBuffer *buffer)
{
  gst_data_unref (GST_DATA (buffer->pool_private));

  GST_BUFFER_DATA (buffer) = NULL;
  GST_BUFFER_SIZE (buffer) = 0;

  _GST_DATA_DISPOSE (GST_DATA (buffer));
  
  gst_mem_chunk_free (chunk, GST_DATA (buffer));
  _gst_buffer_live--;
}

/**
 * gst_buffer_default_free:
 * @buffer: a #GstBuffer to free
 *
 * Free the memory associated with the buffer including the buffer data,
 * unless the GST_BUFFER_DONTFREE flags was set or the buffer data is NULL.
 * This function is used by bufferpools.
 */
void
gst_buffer_default_free (GstBuffer *buffer)
{
  /* free our data */
  if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE) && GST_BUFFER_DATA (buffer)) 
    g_free (GST_BUFFER_DATA (buffer));

  /* set to safe values */
  GST_BUFFER_DATA (buffer) = NULL;
  GST_BUFFER_SIZE (buffer) = 0;

  _GST_DATA_DISPOSE (GST_DATA (buffer));

  gst_mem_chunk_free (chunk, GST_DATA (buffer));
  _gst_buffer_live--;
}

static GstBuffer*
_gst_buffer_copy_from_pool (GstBuffer *buffer)
{
  return buffer->pool->buffer_copy (buffer->pool, buffer, buffer->pool->user_data);
}

/**
 * gst_buffer_default_copy:
 * @buffer: a #GstBuffer to make a copy of
 *
 * Make a full newly allocated copy of the given buffer, data and all.
 * This function is used by bufferpools.
 *
 * Returns: new #GstBuffer
 */
GstBuffer*
gst_buffer_default_copy (GstBuffer *buffer)
{
  GstBuffer *copy;

  /* create a fresh new buffer */
  copy = gst_buffer_new ();

  /* we simply copy everything from our parent */
  GST_BUFFER_DATA (copy) 	= g_memdup (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  GST_BUFFER_SIZE (copy)  	= GST_BUFFER_SIZE (buffer);
  GST_BUFFER_MAXSIZE (copy) 	= GST_BUFFER_MAXSIZE (buffer);
  GST_BUFFER_TIMESTAMP (copy) 	= GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (copy) 	= GST_BUFFER_OFFSET (buffer);

  return copy;
}

/**
 * gst_buffer_new:
 *
 * Creates a newly allocated buffer without any data.
 *
 * Returns: new #GstBuffer
 */
GstBuffer*
gst_buffer_new (void)
{
  GstBuffer *new;
  
  new = gst_mem_chunk_alloc0 (chunk);
  _gst_buffer_live++;

  _GST_DATA_INIT (GST_DATA (new), 
		  _gst_buffer_type,
		  0,
		  (GstDataFreeFunction) gst_buffer_default_free,
		  (GstDataCopyFunction) gst_buffer_default_copy);

  GST_BUFFER_BUFFERPOOL (new) = NULL;
  GST_BUFFER_POOL_PRIVATE (new) = NULL;

  return new;
}

/**
 * gst_buffer_new_and_alloc:
 * @size: the size of the buffer and the memory to allocate
 *
 * Creates a newly allocated buffer with data of the given size.
 *
 * Returns: new #GstBuffer
 */
GstBuffer*
gst_buffer_new_and_alloc (guint size)
{
  GstBuffer *new;

  new = gst_buffer_new ();

  GST_BUFFER_DATA (new) = g_malloc (size);
  GST_BUFFER_SIZE (new) = size;

  return new;
}

/**
 * gst_buffer_new_from_pool:
 * @pool: the buffer pool to use
 * @offset: the offset of the new buffer
 * @size: the size of the new buffer
 *
 * Creates a newly allocated buffer using the specified bufferpool, offset and size.
 *
 * Returns: new #GstBuffer
 */
GstBuffer*
gst_buffer_new_from_pool (GstBufferPool *pool, guint64 offset, guint size)
{
  GstBuffer *buffer;
  
  g_return_val_if_fail (pool != NULL, NULL);

  buffer = pool->buffer_new (pool, offset, size, pool->user_data);
  if (!buffer)
    return NULL;

  GST_BUFFER_BUFFERPOOL (buffer) = pool;
  gst_data_ref (GST_DATA (pool));

  /* override the buffer refcount functions with those from the pool (if any) */
  if (pool->buffer_free)
    GST_DATA (buffer)->free = (GstDataFreeFunction)_gst_buffer_free_to_pool;
  if (pool->buffer_copy)
    GST_DATA (buffer)->copy = (GstDataCopyFunction)_gst_buffer_copy_from_pool;

  return buffer;
}

/**
 * gst_buffer_create_sub:
 * @parent: parent #GstBuffer
 * @offset: offset into parent #GstBuffer
 * @size: size of new sub-buffer
 *
 * Creates a sub-buffer from the parent at a given offset.
 * This sub-buffer uses the actual memory space of the parent buffer.
 *
 * Returns: a new #GstBuffer
 */
GstBuffer*
gst_buffer_create_sub (GstBuffer *parent, guint offset, guint size)
{
  GstBuffer *buffer;
  gpointer buffer_data;
  guint64 parent_offset;
	      
  g_return_val_if_fail (parent != NULL, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (parent) > 0, NULL);
  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail (parent->size >= offset + size, NULL);

  /* remember the data for the new buffer */
  buffer_data = parent->data + offset;
  parent_offset = GST_BUFFER_OFFSET (parent);
  /* make sure we're child not child from a child buffer */
  while (GST_BUFFER_FLAG_IS_SET (parent, GST_BUFFER_SUBBUFFER)) {
    parent = GST_BUFFER (parent->pool_private);
  }
  /* ref the real parent */
  gst_data_ref (GST_DATA (parent));
  /* make sure nobody overwrites data in the parent */
  if (!GST_DATA_IS_READONLY (parent))
    GST_DATA_FLAG_SET(parent, GST_DATA_READONLY);

  /* create the new buffer */
  buffer = gst_mem_chunk_alloc0 (chunk);
  _gst_buffer_live++;

  /* make sure nobody overwrites data in the new buffer by setting the READONLY flag */
  _GST_DATA_INIT (GST_DATA (buffer), 
		  _gst_buffer_type,
		  GST_DATA_FLAG_SHIFT (GST_BUFFER_SUBBUFFER) |
		  GST_DATA_FLAG_SHIFT (GST_DATA_READONLY),
		  (GstDataFreeFunction) _gst_buffer_sub_free,
		  (GstDataCopyFunction) gst_buffer_default_copy);

  GST_BUFFER_OFFSET (buffer) = parent_offset + offset;
  GST_BUFFER_TIMESTAMP (buffer) = -1;
  GST_BUFFER_BUFFERPOOL (buffer) = NULL;
  GST_BUFFER_POOL_PRIVATE (buffer) = parent;

  /* set the right values in the child */
  buffer->data = buffer_data;
  buffer->size = size;

  return buffer;
}


/**
 * gst_buffer_merge:
 * @buf1: first source #GstBuffer to merge
 * @buf2: second source #GstBuffer to merge
 *
 * Create a new buffer that is the concatenation of the two source
 * buffers.  The original source buffers will not be modified or
 * unref'd.
 *
 * Internally is nothing more than a specialized gst_buffer_span(),
 * so the same optimizations can occur.
 *
 * Returns: a new #GstBuffer that's the concatenation of the source buffers
 */
GstBuffer*
gst_buffer_merge (GstBuffer *buf1, GstBuffer *buf2)
{
  GstBuffer *result;
  /* we're just a specific case of the more general gst_buffer_span() */
  result = gst_buffer_span (buf1, 0, buf2, buf1->size + buf2->size);

  return result;
}

/**
 * gst_buffer_is_span_fast:
 * @buf1: first source buffer
 * @buf2: second source buffer
 *
 * Determines whether a gst_buffer_span() is free (as in free beer), 
 * or requires a memcpy. 
 *
 * Returns: TRUE if the buffers are contiguous, FALSE if a copy would be required.
 */
gboolean
gst_buffer_is_span_fast (GstBuffer *buf1, GstBuffer *buf2)
{
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf1) > 0, FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf2) > 0, FALSE);

  /* it's only fast if we have subbuffers of the same parent */
  return ((GST_BUFFER_FLAG_IS_SET (buf1, GST_BUFFER_SUBBUFFER)) &&
          (GST_BUFFER_FLAG_IS_SET (buf2, GST_BUFFER_SUBBUFFER)) &&
 	  (buf1->pool_private == buf2->pool_private) &&
          ((buf1->data + buf1->size) == buf2->data));
}

/**
 * gst_buffer_span:
 * @buf1: first source #GstBuffer to merge
 * @offset: offset in first buffer to start new buffer
 * @buf2: second source #GstBuffer to merge
 * @len: length of new buffer
 *
 * Creates a new buffer that consists of part of buf1 and buf2.
 * Logically, buf1 and buf2 are concatenated into a single larger
 * buffer, and a new buffer is created at the given offset inside
 * this space, with a given length.
 *
 * If the two source buffers are children of the same larger buffer,
 * and are contiguous, the new buffer will be a child of the shared
 * parent, and thus no copying is necessary. you can use 
 * gst_buffer_is_span_fast() to determine if a memcpy will be needed.
 *
 * Returns: a new #GstBuffer that spans the two source buffers
 */
GstBuffer*
gst_buffer_span (GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len)
{
  GstBuffer *newbuf;

  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf1) > 0, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf2) > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);

  /* if the two buffers have the same parent and are adjacent */
  if (gst_buffer_is_span_fast (buf1, buf2)) {
    GstBuffer *parent = GST_BUFFER (buf1->pool_private);
    /* we simply create a subbuffer of the common parent */
    newbuf = gst_buffer_create_sub (parent, buf1->data - parent->data + offset, len);
  }
  else {
    GST_DEBUG (GST_CAT_BUFFER,"slow path taken while spanning buffers %p and %p", buffer, append);
    /* otherwise we simply have to brute-force copy the buffers */
    newbuf = gst_buffer_new_and_alloc (len);

    /* copy relevant stuff from data struct of buffer1 */
    GST_BUFFER_OFFSET (newbuf) = GST_BUFFER_OFFSET (buf1) + offset;

    /* copy the first buffer's data across */
    memcpy (newbuf->data, buf1->data + offset, buf1->size - offset);
    /* copy the second buffer's data across */
    memcpy (newbuf->data + (buf1->size - offset), buf2->data, len - (buf1->size - offset));
  }
  /* if the offset is 0, the new buffer has the same timestamp as buf1 */
  if (offset == 0)
    GST_BUFFER_TIMESTAMP (newbuf) = GST_BUFFER_TIMESTAMP (buf1);

  return newbuf;
}

void
gst_buffer_pool_default_free (GstBufferPool *pool)
{
  _GST_DATA_DISPOSE (GST_DATA (pool));
  g_free (pool);
  _gst_buffer_pool_live--;
}

/** 
 * gst_buffer_pool_new:
 * @free: The function to free the bufferpool
 * @copy: The function to copy the bufferpool
 * @buffer_new: the function to create a new buffer from this pool
 * @buffer_copy: the function to copy a buffer from this pool
 * @buffer_free: the function to free a buffer in this pool
 * @user_data: user data passed to buffer_* functions
 *
 * Create a new bufferpool with the given functions.
 *
 * Returns: a new GstBufferPool or NULL on error.
 */
GstBufferPool*	
gst_buffer_pool_new (GstDataFreeFunction free,
		     GstDataCopyFunction copy,
                     GstBufferPoolBufferNewFunction buffer_new,
                     GstBufferPoolBufferCopyFunction buffer_copy,
                     GstBufferPoolBufferFreeFunction buffer_free,
		     gpointer user_data)
{
  GstBufferPool *pool;

  /* we need at least a buffer_new function */
  g_return_val_if_fail (buffer_new != NULL, NULL);

  pool = g_new0 (GstBufferPool, 1);
  _gst_buffer_pool_live++;

  GST_DEBUG (GST_CAT_BUFFER, "allocating new buffer pool %p\n", pool);
        
  /* init data struct */
  _GST_DATA_INIT (GST_DATA (pool), 
		  _gst_buffer_pool_type,
		  0,
		  (free ? free : (GstDataFreeFunction) gst_buffer_pool_default_free),
		  copy);
	    
  /* set functions */
  pool->buffer_new = buffer_new;
  pool->buffer_copy = buffer_copy;
  pool->buffer_free = buffer_free;
  pool->user_data = user_data;
		    
  return pool;
}

/**
 * gst_buffer_pool_is_active:
 * @pool: the pool to query
 *
 * Query if the geven bufferpool is active.
 *
 * Returns: TRUE if the pool is active.
 */
gboolean
gst_buffer_pool_is_active (GstBufferPool *pool)
{
  return pool->active;
}

/**
 * gst_buffer_pool_set_active:
 * @pool: the pool to activate
 * @active: new status of the pool
 *
 * Set the given pool to the active or inactive state depending on the
 * activate parameter
 */
void
gst_buffer_pool_set_active (GstBufferPool *pool, gboolean active)
{
  pool->active = active;
}

/**
 * gst_buffer_pool_set_user_data:
 * @pool: the pool set the user data for
 * @user_data: the user_data to set
 *
 * Set the given user data to the bufferpool
 */
void
gst_buffer_pool_set_user_data (GstBufferPool *pool, gpointer user_data)
{
  pool->user_data = user_data;
}

/**
 * gst_buffer_pool_get_user_data:
 * @pool: the pool get the user data for
 *
 * Get the user data of the bufferpool
 * 
 * Returns: the user data associated with this bufferpool
 */
gpointer
gst_buffer_pool_get_user_data (GstBufferPool *pool)
{
  return pool->user_data;
}

