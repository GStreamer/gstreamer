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

#include "gstbuffer.h"
#include "gstobject.h"

/* memchunks are slower if you have to lock them */
/* static GMemChunk *_buffer_chunk = NULL;*/
static GstBufferPool *_default_pool = NULL;
static GstBufferPool *_sub_buffer_pool = NULL;

static GstBuffer * 	gst_buffer_pool_default_buffer_new	(GstBufferPool *pool, 
								 guint size);
static GstBuffer * 	gst_buffer_pool_default_buffer_copy	(GstBufferPool *from_pool,
								 const GstBuffer *buffer, 
								 guint offset, 
								 guint size);
static void 		gst_buffer_pool_default_buffer_dispose	(GstData *buffer);

static GstBuffer * 	gst_buffer_pool_sub_buffer_new		(GstBufferPool *pool, 
								 guint size);
static GstBuffer * 	gst_buffer_pool_sub_buffer_copy		(GstBufferPool *from_pool,
								 const GstBuffer *buffer, 
								 guint offset, 
								 guint size);
static void 		gst_buffer_pool_sub_buffer_dispose	(GstData *buffer);

void
_gst_buffer_initialize (void)
{
  /*gint buffersize = sizeof (GstBuffer);
  if (_buffer_chunk == NULL)*/
  if (_default_pool == NULL)
  {
    /* create the default buffer chunk */
    /*_buffer_chunk = g_mem_chunk_new ("GstBufferChunk", buffersize, buffersize * 128, G_ALLOC_AND_FREE);*/
    /* create the default pool that uses g_malloc/free */
    _default_pool = gst_buffer_pool_new ();
    /* create the pool for subbuffers */
    _sub_buffer_pool = gst_buffer_pool_new ();
    gst_buffer_pool_set_buffer_new_function (_sub_buffer_pool, gst_buffer_pool_sub_buffer_new);
    gst_buffer_pool_set_buffer_copy_function (_sub_buffer_pool, gst_buffer_pool_sub_buffer_copy);
    gst_buffer_pool_set_buffer_dispose_function (_sub_buffer_pool, gst_buffer_pool_sub_buffer_dispose);
    GST_INFO (GST_CAT_BUFFER, "Buffers are initialized now");
  }
}
/**
 * gst_buffer_alloc:
 *
 * Allocate space for a new buffer. Works the same way
 * as g_malloc.
 * Buffers allocated with gst_buffer_alloc must be freed
 * with gst_buffer_free.
 * 
 * Returns: A new buffer or NULL on failure
 */
GstBuffer *
gst_buffer_alloc (void)
{
  /* return g_mem_chunk_alloc (_buffer_chunk);*/
  return g_new (GstBuffer, 1);
}
/**
 * gst_buffer_free:
 * @buffer: The buffer to free.
 * 
 * Frees a buffer that wa previously allocated with gst_buffer_alloc.
 */
void
gst_buffer_free (GstBuffer *buffer)
{
  /* g_mem_chunk_free (_buffer_chunk, buffer);*/
  g_free (buffer);
}
/**
 * gst_buffer_init:
 * @buffer: The buffer to be initialized
 * @pool: The bufferpool to initialize from
 * 
 * Initializes a buffer from a given bufferpool.
 * This function is a convenience function to be used in buffer_new 
 * routines of bufferpools.
 */
void
gst_buffer_init	(GstBuffer *buffer, GstBufferPool *pool)
{
  GST_DEBUG (GST_CAT_BUFFER, "initializing new buffer %p", buffer);
  gst_data_ref (GST_DATA (pool));

  gst_data_init (GST_DATA (buffer));
  GST_DATA (buffer)->type = GST_DATA_BUFFER;
  GST_DATA (buffer)->dispose = pool->buffer_dispose;
  GST_DATA (buffer)->free = pool->buffer_free;
  
  buffer->data = NULL;
  buffer->size = 0;
  buffer->pool = pool;
  buffer->pool_private = NULL;
}
/**
 * gst_buffer_dispose:
 * @buffer: The buffer to be disposed
 * 
 * Disposes a buffer.
 * This function is a convenience function to be used in buffer_dispose 
 * routines of bufferpools.
 */
void
gst_buffer_dispose (GstData *buffer)
{
  gst_data_unref (GST_DATA (GST_BUFFER (buffer)->pool));
  gst_data_dispose (buffer);
}
/**
 * gst_buffer_new:
 * @pool: bufferpool to create the buffer from
 * @size: size of the data, 0 if no data initialization
 *
 * Create a new buffer from the given bufferpool. If the bufferpool
 * is set to NULL, the default pool is used.
 *
 * Returns: new buffer or NULL, if buffer couldn't be created.
 */
GstBuffer*
gst_buffer_new (GstBufferPool *pool, guint size)
{
  if (pool == NULL)
  {
    GstBufferPool *def = _default_pool;
    return def->buffer_new (def, size);
  } else {
    return pool->buffer_new (pool, size);
  }
}
/**
 * gst_buffer_create_sub:
 * @parent: parent buffer
 * @offset: offset into parent buffer
 * @size: size of new subbuffer
 *
 * Creates a sub-buffer from the parent at a given offset.
 *
 * Returns: new buffer
 */
GstBuffer*
gst_buffer_create_sub (GstBuffer *parent, guint offset, guint size) 
{
  GstBuffer *buffer;
  gpointer buffer_data;
  
  g_return_val_if_fail (parent != NULL, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(parent) > 0, NULL);
  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail (parent->size >= offset + size, NULL);
  /* remember the data for the new buffer */
  buffer_data = parent->data + offset;
  /* make sure we're child not child from a child buffer */
  while (parent->pool == _sub_buffer_pool)
  {
    parent = GST_BUFFER (parent->pool_private);
  }
  /* ref the real parent */
  gst_data_ref (GST_DATA (parent));
  /* make sure nobody overwrites data in the parent */
  if (GST_DATA_IS_WRITABLE (parent))
    GST_DATA_FLAG_SET(parent, GST_DATA_READONLY);
  /* create the new buffer */
  buffer = gst_buffer_new (_sub_buffer_pool, 0);
  /* make sure nobody overwrites data in the new buffer */
  GST_DATA_FLAG_SET(buffer, GST_DATA_READONLY);
  /* set the right values in the child */
  buffer->pool_private = parent;
  buffer->data = buffer_data;
  buffer->size = size;

  return buffer;
}


/**
 * gst_buffer_append:
 * @buffer: a buffer
 * @append: the buffer to append
 *
 * Creates a new buffer by appending the data of append to the
 * existing data of buffer.
 *
 * Returns: new buffer
 */
GstBuffer*
gst_buffer_append (GstBuffer *buffer, GstBuffer *append) 
{
  GstBuffer *newbuf;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (append != NULL, NULL);
  g_return_val_if_fail (buffer->pool == NULL, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(buffer) > 0, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(append) > 0, NULL);

  GST_INFO (GST_CAT_BUFFER,"appending buffers %p and %p",buffer,append);

  newbuf = gst_buffer_merge (buffer, append);
  gst_data_unref (GST_DATA (buffer));
  
  return newbuf;
}
/**
 * gst_buffer_copy_part_from_pool:
 * @pool: the GstBufferPool to create the buffer from or NULL to use the default
 * @buffer: the orignal GstBuffer to make a copy of
 * @offset: the offset into the buffer
 * @offset: the offset into the buffer
 *
 * Make a full copy of the given buffer with the given part of the data. Use the
 * given bufferpool to create the buffer.
 *
 * Returns: new buffer
 */
GstBuffer *
gst_buffer_copy_part_from_pool (GstBufferPool *pool, const GstBuffer *buffer, guint offset, guint size)
{
  GstBufferPool *use_pool;
  
  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT (buffer) > 0, NULL);
  g_return_val_if_fail (buffer->size >= offset + size, NULL);
  
  use_pool = pool ? pool : _default_pool;
  return use_pool->buffer_copy (use_pool, buffer, offset, size);
}
/**
 * gst_buffer_is_span_fast:
 * @buf1: first source buffer
 * @buf2: second source buffer
 *
 * Determines whether a gst_buffer_span is free, or requires a memcpy. 
 *
 * Returns: TRUE if the buffers are contiguous, FALSE if a copy would be required.
 */
gboolean
gst_buffer_is_span_fast (GstBuffer *buf1, GstBuffer *buf2)
{
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(buf1) > 0, FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(buf2) > 0, FALSE);

  /* it's only fast if we have subbuffers of the same parent */
  return ((buf1->pool == _sub_buffer_pool) &&
	  (buf2->pool == _sub_buffer_pool) &&
	  (buf1->pool_private == buf2->pool_private) &&
          ((buf1->data + buf1->size) == buf2->data));
}
/**
 * gst_buffer_span:
 * @buf1: first source buffer to merge
 * @offset: offset in first buffer to start new buffer
 * @buf2: second source buffer to merge
 * @len: length of new buffer
 *
 * Create a new buffer that consists of part of buf1 and buf2.
 * Logically, buf1 and buf2 are concatenated into a single larger
 * buffer, and a new buffer is created at the given offset inside
 * this space, with a given length.
 *
 * If the two source buffers are children of the same larger buffer,
 * and are contiguous, the new buffer will be a child of the shared
 * parent, and thus no copying is necessary.
 *
 * Returns: new buffer that spans the two source buffers
 */
/* FIXME need to think about CoW and such... */
GstBuffer *
gst_buffer_span (GstBuffer *buf1, guint offset, GstBuffer *buf2, guint len)
{
  GstBuffer *newbuf;

  g_return_val_if_fail (GST_BUFFER_REFCOUNT(buf1) > 0, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT(buf2) > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);

  /* if the two buffers have the same parent and are adjacent */
  if (gst_buffer_is_span_fast(buf1,buf2)) {
    GstBuffer *parent = GST_BUFFER (buf1->pool_private);
    /* we simply create a subbuffer of the common parent */
    newbuf = gst_buffer_create_sub (parent, buf1->data - parent->data + offset, len);
  }
  else {
    GST_DEBUG (GST_CAT_BUFFER,"slow path taken while spanning buffers %p and %p", buffer, append);
    /* otherwise we simply have to brute-force copy the buffers */
    newbuf = gst_buffer_new (buf1->pool, len);

    /* copy relevant stuff from data struct of buffer1 */
    gst_data_copy (GST_DATA (newbuf), GST_DATA (buf1));
    /* copy the first buffer's data across */
    memcpy (newbuf->data, buf1->data + offset, buf1->size - offset);
    /* copy the second buffer's data across */
    memcpy (newbuf->data + (buf1->size - offset), buf2->data, len - (buf1->size - offset));
  }

  return newbuf;
}


/**
 * gst_buffer_merge:
 * @buf1: first source buffer to merge
 * @buf2: second source buffer to merge
 *
 * Create a new buffer that is the concatenation of the two source
 * buffers.  The original source buffers will not be modified or
 * unref'd.
 *
 * Internally is nothing more than a specialized gst_buffer_span,
 * so the same optimizations can occur.
 *
 * Returns: new buffer that's the concatenation of the source buffers
 */
GstBuffer *
gst_buffer_merge (GstBuffer *buf1, GstBuffer *buf2)
{
  guint i;
  GstBuffer *result;
  /* we're just a specific case of the more general gst_buffer_span() */
  result = gst_buffer_span (buf1, 0, buf2, buf1->size + buf2->size);

  /* but we can include offset info */
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    GST_DATA (result)->offset[i] = GST_DATA (buf1)->offset[i];
  }

  return result;
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
 * Your function does not need to check the given parameters, they will
 * be checked before calling your function.
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
static GstBuffer *
gst_buffer_pool_default_buffer_copy (GstBufferPool *from_pool, const GstBuffer *buffer, guint offset, guint size)
{
  GstBuffer *newbuf;
  
  /* allocate a new buffer with the right size */
  newbuf = gst_buffer_new (from_pool, size);
  /* copy all relevant data stuff */
  gst_data_copy (GST_DATA (newbuf), GST_DATA (buffer));
  /* copy the data straight across */
  memcpy (newbuf->data, buffer->data + offset, size);

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
static GstBuffer *
gst_buffer_pool_sub_buffer_new (GstBufferPool *pool, guint size)
{
  GstBuffer *buffer;
  
  g_return_val_if_fail((buffer = gst_buffer_alloc()), NULL);
  
  GST_DEBUG (GST_CAT_BUFFER, "creating new subbuffer %p", buffer, pool);
  
  gst_buffer_init (buffer, pool);

  GST_BUFFER_DATA(buffer) = NULL;
  GST_BUFFER_SIZE(buffer) = 0;
  
  return buffer;
}
static GstBuffer *
gst_buffer_pool_sub_buffer_copy	(GstBufferPool *from_pool, const GstBuffer *buffer, guint offset, guint size)
{
  GstBuffer *parent = GST_BUFFER (buffer->pool_private);
  guint new_offset = buffer->data - parent->data;
  GstBufferPool *use_pool = from_pool == _sub_buffer_pool ? NULL : from_pool;
  return parent->pool->buffer_copy (use_pool, parent, new_offset, size);
}
static void
gst_buffer_pool_sub_buffer_dispose (GstData *buffer)
{
  GstBuffer *buf = GST_BUFFER (buffer);
  
  buf->data = NULL;
  buf->size = 0;
  gst_data_unref (GST_DATA (buf->pool_private));
  buf->pool_private = NULL; 
  gst_buffer_dispose (buffer);
}
