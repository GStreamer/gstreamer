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
  buffer->offset = 0;
  buffer->timestamp = 0;
//  buffer->metas = NULL;
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
gst_buffer_new_from_pool (GstBufferPool *pool)
{
  return gst_buffer_pool_new_buffer (pool);
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
  buffer->offset = parent->offset + offset;
  buffer->maxsize = parent->size - offset;

  // again, for lack of better, copy parent's timestamp
  buffer->timestamp = parent->timestamp;
  buffer->maxage = parent->maxage;

  // no metas, this is sane I think
//  buffer->metas = NULL;

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
//  GSList *metas;

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

/* DEPRACATED!!!
  // unreference any metadata attached to this buffer
  metas = buffer->metas;
  while (metas) {
    gst_meta_unref ((GstMeta *)(metas->data));
    metas = g_slist_next (metas);
  }
  g_slist_free (buffer->metas);
*/

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
    // if it came from a pool, give it back
    if (buffer->pool != NULL) {
      gst_buffer_pool_destroy_buffer (buffer->pool, buffer);
      return;
    }
    else {
      gst_buffer_destroy (buffer);
    }
  }
}

/**
 * gst_buffer_add_meta:
 * @buffer: the GstBuffer to add the metadata to
 * @meta: the metadata to add to this buffer
 *
 * Add the meta data to the buffer.
 * DEPRACATED!!!
 */
/* DEPRACATED!!!
void 
gst_buffer_add_meta (GstBuffer *buffer, GstMeta *meta) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (meta != NULL);

  gst_meta_ref (meta);
  buffer->metas = g_slist_append (buffer->metas,meta);
}
*/

/**
 * gst_buffer_get_metas:
 * @buffer: the GstBuffer to get the metadata from
 *
 * Get the metadatas from the buffer.
 * DEPRACATED!!!
 *
 * Returns: a GSList of metadata
 */
/* DEPRACATED!!!
GSList*
gst_buffer_get_metas (GstBuffer *buffer) 
{
  g_return_val_if_fail (buffer != NULL, NULL);

  return buffer->metas;
}
*/

/**
 * gst_buffer_get_first_meta:
 * @buffer: the GstBuffer to get the metadata from
 *
 * Get the first metadata from the buffer.
 * DEPRACATED!!!
 *
 * Returns: the first metadata from the buffer
 */
/* DEPRACATED!!!
GstMeta*
gst_buffer_get_first_meta (GstBuffer *buffer) 
{
  g_return_val_if_fail (buffer != NULL, NULL);

  if (buffer->metas == NULL)
    return NULL;
  return GST_META (buffer->metas->data);
}
*/

/**
 * gst_buffer_remove_meta:
 * @buffer: the GstBuffer to remove the metadata from
 * @meta: the metadata to remove
 *
 * Remove the given metadata from the buffer.
 * DEPRACATED!!!
 */
/* DEPRACATED!!!
void 
gst_buffer_remove_meta (GstBuffer *buffer, GstMeta *meta) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (meta != NULL);

  buffer->metas = g_slist_remove (buffer->metas, meta);
  gst_meta_unref (meta);
}
*/



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
