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


static GMemChunk *_gst_buffer_chunk;
static GMutex *_gst_buffer_chunk_lock;

void 
_gst_buffer_initialize (void) 
{
  int buffersize = sizeof(GstBuffer);

  // round up to the nearest 32 bytes for cache-line and other efficiencies
  buffersize = (((buffersize-1) / 32) + 1) * 32;

  _gst_buffer_chunk = g_mem_chunk_new ("GstBuffer", buffersize,
    buffersize * 32, G_ALLOC_AND_FREE);

  _gst_buffer_chunk_lock = g_mutex_new ();
}

/**
 * gst_buffer_new:
 *
 * Create a new buffer.
 *
 * Returns: new buffer
 */
GstBuffer*
gst_buffer_new(void) 
{
  GstBuffer *buffer;

  g_mutex_lock (_gst_buffer_chunk_lock);
  buffer = g_mem_chunk_alloc (_gst_buffer_chunk);
  g_mutex_unlock (_gst_buffer_chunk_lock);
  GST_INFO (GST_CAT_BUFFER,"creating new buffer %p",buffer);

  buffer->lock = g_mutex_new ();
#ifdef HAVE_ATOMIC_H
  atomic_set (&buffer->refcount, 1);
#else
  buffer->refcount = 1;
#endif
  buffer->flags = 0;
  buffer->data = NULL;
  buffer->size = 0;
  buffer->maxsize = 0;
  buffer->offset = -1;
  buffer->timestamp = 0;
  buffer->parent = NULL;
  buffer->pool = NULL;
  buffer->free = NULL;
  buffer->copy = NULL;
  
  return buffer;
}

/**
 * gst_buffer_new_from_pool:
 * @pool: the buffer pool to use
 *
 * Create a new buffer using the specified bufferpool.
 *
 * Returns: new buffer
 */
GstBuffer*
gst_buffer_new_from_pool (GstBufferPool *pool, guint64 location, gint size)
{
  GstBuffer *buffer;

  g_return_val_if_fail (pool != NULL, NULL);
  g_return_val_if_fail (pool->buffer_new != NULL, NULL);
  
  buffer = pool->buffer_new (pool, location, size, pool->user_data);
  buffer->pool = pool;
  buffer->free = pool->buffer_free;
  buffer->copy = pool->buffer_copy;
  buffer->pool_private = pool->user_data;
  
  return buffer;
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
gst_buffer_create_sub (GstBuffer *parent,
		       guint32 offset,
		       guint32 size) 
{
  GstBuffer *buffer;

  g_return_val_if_fail (parent != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail ((offset+size) <= parent->size, NULL);

  g_mutex_lock (_gst_buffer_chunk_lock);
  buffer = g_mem_chunk_alloc (_gst_buffer_chunk);
  g_mutex_unlock (_gst_buffer_chunk_lock);
  GST_INFO (GST_CAT_BUFFER,"creating new subbuffer %p from parent %p", buffer, parent);

  buffer->lock = g_mutex_new ();
#ifdef HAVE_ATOMIC_H
  atomic_set (&buffer->refcount, 1);
#else
  buffer->refcount = 1;
#endif

  // copy flags and type from parent, for lack of better
  buffer->flags = parent->flags;

  // set the data pointer, size, offset, and maxsize
  buffer->data = parent->data + offset;
  buffer->size = size;
  buffer->maxsize = parent->size - offset;

  // deal with bogus/unknown offsets
  if (parent->offset != -1)
    buffer->offset = parent->offset + offset;
  else
    buffer->offset = -1;

  // again, for lack of better, copy parent's timestamp
  buffer->timestamp = parent->timestamp;
  buffer->maxage = parent->maxage;

  // if the parent buffer is a subbuffer itself, use its parent, a real buffer
  if (parent->parent != NULL)
    parent = parent->parent;

  // set parentage and reference the parent
  buffer->parent = parent;
  gst_buffer_ref (parent);

  buffer->pool = NULL;
  // return the new subbuffer
  return buffer;
}


// FIXME FIXME: how does this overlap with the newly-added gst_buffer_span() ???
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
gst_buffer_append (GstBuffer *buffer, 
		   GstBuffer *append) 
{
  guint size;
  GstBuffer *newbuf;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (append != NULL, NULL);
  g_return_val_if_fail (buffer->pool == NULL, NULL);

  GST_INFO (GST_CAT_BUFFER,"appending buffers %p and %p",buffer,append);

  GST_BUFFER_LOCK (buffer);
  // the buffer is not used by anyone else
  if (GST_BUFFER_REFCOUNT (buffer) == 1 && buffer->parent == NULL 
	  && !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE)) {
    // save the old size
    size = buffer->size;
    buffer->size += append->size;
    buffer->data = g_realloc (buffer->data, buffer->size);
    memcpy(buffer->data + size, append->data, append->size);
    GST_BUFFER_UNLOCK (buffer);
  }
  // the buffer is used, create a new one 
  else {
    newbuf = gst_buffer_new ();
    newbuf->size = buffer->size+append->size;
    newbuf->data = g_malloc (newbuf->size);
    memcpy (newbuf->data, buffer->data, buffer->size);
    memcpy (newbuf->data+buffer->size, append->data, append->size);
    GST_BUFFER_UNLOCK (buffer);
    gst_buffer_unref (buffer);
    buffer = newbuf;
  }
  return buffer;
}

/**
 * gst_buffer_destroy:
 * @buffer: the GstBuffer to destroy
 *
 * destroy the buffer
 */
void gst_buffer_destroy (GstBuffer *buffer) 
{

  g_return_if_fail (buffer != NULL);
  
  GST_INFO (GST_CAT_BUFFER,"freeing %sbuffer %p", (buffer->parent?"sub":""),buffer);
  
  // free the data only if there is some, DONTFREE isn't set, and not sub
  if (GST_BUFFER_DATA (buffer) &&
      !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE) &&
      (buffer->parent == NULL)) {
    // if there's a free function, use it
    if (buffer->free != NULL) {
      (buffer->free)(buffer);
    } else {
      g_free (GST_BUFFER_DATA (buffer));
    }
  }

  // unreference the parent if there is one
  if (buffer->parent != NULL)
    gst_buffer_unref (buffer->parent);

  g_mutex_free (buffer->lock);
  //g_print("freed mutex\n");

  // remove it entirely from memory
  g_mutex_lock (_gst_buffer_chunk_lock);
  g_mem_chunk_free (_gst_buffer_chunk,buffer);
  g_mutex_unlock (_gst_buffer_chunk_lock);
}

/**
 * gst_buffer_ref:
 * @buffer: the GstBuffer to reference
 *
 * Increment the refcount of this buffer.
 */
void 
gst_buffer_ref (GstBuffer *buffer) 
{
  g_return_if_fail (buffer != NULL);

  GST_DEBUG (0,"referencing buffer %p\n", buffer);

#ifdef HAVE_ATOMIC_H
  //g_return_if_fail(atomic_read(&(buffer->refcount)) > 0);
  atomic_inc (&(buffer->refcount));
#else
  g_return_if_fail (buffer->refcount > 0);
  GST_BUFFER_LOCK (buffer);
  buffer->refcount++;
  GST_BUFFER_UNLOCK (buffer);
#endif
}

/**
 * gst_buffer_ref_by_count:
 * @buffer: the GstBuffer to reference
 * @count: a number
 *
 * Increment the refcount of this buffer by the given number.
 */
void 
gst_buffer_ref_by_count (GstBuffer *buffer, int count) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (count > 0);

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(buffer->refcount)) > 0);
  atomic_add (count, &(buffer->refcount));
#else
  g_return_if_fail (buffer->refcount > 0);
  GST_BUFFER_LOCK (buffer);
  buffer->refcount += count;
  GST_BUFFER_UNLOCK (buffer);
#endif
}

/**
 * gst_buffer_unref:
 * @buffer: the GstBuffer to unref
 *
 * Decrement the refcount of this buffer. If the refcount is
 * zero, the buffer will be destroyed.
 */
void 
gst_buffer_unref (GstBuffer *buffer) 
{
  gint zero;

  g_return_if_fail (buffer != NULL);

  GST_DEBUG (0,"unreferencing buffer %p\n", buffer);

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(buffer->refcount)) > 0);
  zero = atomic_dec_and_test (&(buffer->refcount));
#else
  g_return_if_fail (buffer->refcount > 0);
  GST_BUFFER_LOCK (buffer);
  buffer->refcount--;
  zero = (buffer->refcount == 0);
  GST_BUFFER_UNLOCK (buffer);
#endif

  /* if we ended up with the refcount at zero, destroy the buffer */
  if (zero) {
    gst_buffer_destroy (buffer);
  }
}

/**
 * gst_buffer_copy:
 * @buffer: the orignal GstBuffer to make a copy of
 *
 * Make a full copy of the give buffer, data and all.
 *
 * Returns: new buffer
 */
GstBuffer *
gst_buffer_copy (GstBuffer *buffer)
{
  GstBuffer *newbuf;

  // allocate a new buffer
  newbuf = gst_buffer_new();

  // if a copy function exists, use it, else copy the bytes
  if (buffer->copy != NULL) {
    (buffer->copy)(buffer,newbuf);
  } else {
    // copy the absolute size
    newbuf->size = buffer->size;
    // allocate space for the copy
    newbuf->data = (guchar *)g_malloc (buffer->size);
    // copy the data straight across
    memcpy(newbuf->data,buffer->data,buffer->size);
    // the new maxsize is the same as the size, since we just malloc'd it
    newbuf->maxsize = newbuf->size;
  }
  newbuf->offset = buffer->offset;
  newbuf->timestamp = buffer->timestamp;
  newbuf->maxage = buffer->maxage;

  // since we just created a new buffer, so we have no ties to old stuff
  newbuf->parent = NULL;
  newbuf->pool = NULL;

  return newbuf;
}


GstBuffer *
gst_buffer_span (GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len)
{
  GstBuffer *newbuf;

  // make sure buf1 has a lower address than buf2
  if (buf1->data > buf2->data) {
    GstBuffer *tmp = buf1;
    buf1 = buf2;
    buf2 = tmp;
  }

  // if the two buffers have the same parent and are adjacent
  if ((buf1->parent == buf2->parent) &&
      ((buf1->data + buf1->size) == buf2->data)) {
    // we simply create a subbuffer of the common parent
    return gst_buffer_create_sub(buf1->parent, buf1->data - (buf1->parent->data) + offset, len);
  }

  // otherwise we simply have to brute-force copy the buffers
  newbuf = gst_buffer_new();

  // put in new size
  newbuf->size = len;
  // allocate space for the copy
  newbuf->data = (guchar *)g_malloc(len);
  // copy the first buffer's data across
  memcpy(newbuf->data, buf1->data + offset, buf1->size - offset);
  // copy the second buffer's data across
  memcpy(newbuf->data + offset, buf2->data, len - (buf1->size - offset));

  if (newbuf->offset != -1)
    newbuf->offset = buf1->offset + offset;
  newbuf->timestamp = buf1->timestamp;
  if (buf2->maxage > buf1->maxage) newbuf->maxage = buf2->maxage;
  else newbuf->maxage = buf1->maxage;

  newbuf->parent = NULL;
  newbuf->pool = NULL;

  return newbuf;
}


GstBuffer *
gst_buffer_merge (GstBuffer *buf1, GstBuffer *buf2)
{
  // we're just a specific case of the more general gst_buffer_span()
  return gst_buffer_span(buf1, 0, buf2, buf1->size + buf2->size);
}
