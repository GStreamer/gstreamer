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


/* this file makes too much noise for most debugging sessions */
#define GST_DEBUG_FORCE_DISABLE
#include <gst/gst.h>
#include <gst/gstbuffer.h>


GMemChunk *_gst_buffer_chunk;

void 
_gst_buffer_initialize (void) 
{
  _gst_buffer_chunk = g_mem_chunk_new ("GstBuffer", sizeof(GstBuffer),
    sizeof(GstBuffer) * 16, G_ALLOC_AND_FREE);
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

  buffer = g_mem_chunk_alloc (_gst_buffer_chunk);
  DEBUG("allocating new buffer %p\n",buffer);

//  g_print("allocating new mutex\n");
  buffer->lock = g_mutex_new ();
#ifdef HAVE_ATOMIC_H
  atomic_set (&buffer->refcount, 1);
#else
  buffer->refcount = 1;
#endif
  buffer->flags = 0;
  buffer->type = 0;
  buffer->data = NULL;
  buffer->size = 0;
  buffer->maxsize = 0;
  buffer->offset = 0;
  buffer->timestamp = 0;
  buffer->metas = NULL;
  buffer->parent = NULL;
  buffer->pool = NULL;
  
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

  buffer = g_mem_chunk_alloc (_gst_buffer_chunk);
  DEBUG("allocating new subbuffer %p, parent %p\n", buffer, parent);

  buffer->lock = g_mutex_new ();
#ifdef HAVE_ATOMIC_H
  atomic_set (&buffer->refcount, 1);
#else
  buffer->refcount = 1;
#endif

  // copy flags and type from parent, for lack of better
  buffer->flags = parent->flags;
  buffer->type = parent->type;

  // set the data pointer, size, offset, and maxsize
  buffer->data = parent->data + offset;
  buffer->size = size;
  buffer->offset = parent->offset + offset;
  buffer->maxsize = parent->size - offset;

  // again, for lack of better, copy parent's timestamp
  buffer->timestamp = parent->timestamp;

  // no metas, this is sane I think
  buffer->metas = NULL;

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

  GST_BUFFER_LOCK (buffer);
  // the buffer is not used by anyone else
  if (GST_BUFFER_REFCOUNT (buffer) == 1 && buffer->parent == NULL) {
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
  GSList *metas;

  g_return_if_fail (buffer != NULL);

  if (buffer->parent != NULL) {
    DEBUG("freeing subbuffer %p\n", buffer);
  }
  else {
    DEBUG("freeing buffer %p\n", buffer);
  }

  // free the data only if there is some, DONTFREE isn't set, and not sub
  if (GST_BUFFER_DATA (buffer) &&
      !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE) &&
      (buffer->parent == NULL)) {
    g_free (GST_BUFFER_DATA (buffer));
//    g_print("freed data in buffer\n");
  }

  // unreference any metadata attached to this buffer
  metas = buffer->metas;
  while (metas) {
    gst_meta_unref ((GstMeta *)(metas->data));
    metas = g_slist_next (metas);
  }
  g_slist_free (buffer->metas);

  // unreference the parent if there is one
  if (buffer->parent != NULL)
    gst_buffer_unref (buffer->parent);

  g_mutex_free (buffer->lock);
  //g_print("freed mutex\n");

  // remove it entirely from memory
  g_mem_chunk_free (_gst_buffer_chunk,buffer);
}

/**
 * gst_buffer_ref:
 * @buffer: the GstBuffer to reference
 *
 * increment the refcount of this buffer
 */
void 
gst_buffer_ref (GstBuffer *buffer) 
{
  g_return_if_fail (buffer != NULL);

  DEBUG("referencing buffer %p\n", buffer);

#ifdef HAVE_ATOMIC_H
  //g_return_if_fail(atomic_read(&(buffer->refcount)) > 0);
  atomic_inc (&(buffer->refcount))
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
 * increment the refcount of this buffer with count
 */
void 
gst_buffer_ref_by_count (GstBuffer *buffer, int count) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (count > 0);

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(buffer->refcount)) > 0);
  atomic_add (count, &(buffer->refcount))
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
 * decrement the refcount of this buffer. If the refcount is
 * zero, the buffer will be destroyed.
 */
void 
gst_buffer_unref (GstBuffer *buffer) 
{
  gint zero;

  g_return_if_fail (buffer != NULL);

  DEBUG("unreferencing buffer %p\n", buffer);

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(buffer->refcount)) > 0);
  zero = atomic_dec_and_test (&(buffer->refcount))
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
 * add the meta data to the buffer
 */
void 
gst_buffer_add_meta (GstBuffer *buffer, GstMeta *meta) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (meta != NULL);

  gst_meta_ref (meta);
  buffer->metas = g_slist_append (buffer->metas,meta);
}

/**
 * gst_buffer_get_metas:
 * @buffer: the GstBuffer to get the metadata from
 *
 * get the metadatas from the buffer
 *
 * Returns: a GSList of metadata
 */
GSList*
gst_buffer_get_metas (GstBuffer *buffer) 
{
  g_return_val_if_fail (buffer != NULL, NULL);

  return buffer->metas;
}

/**
 * gst_buffer_get_first_meta:
 * @buffer: the GstBuffer to get the metadata from
 *
 * get the first metadata from the buffer
 *
 * Returns: the first metadata from the buffer
 */
GstMeta*
gst_buffer_get_first_meta (GstBuffer *buffer) 
{
  g_return_val_if_fail (buffer != NULL, NULL);

  if (buffer->metas == NULL)
    return NULL;
  return GST_META (buffer->metas->data);
}

/**
 * gst_buffer_remove_meta:
 * @buffer: the GstBuffer to remove the metadata from
 * @meta: the metadata to remove
 *
 * remove the given metadata from the buffer
 */
void 
gst_buffer_remove_meta (GstBuffer *buffer, GstMeta *meta) 
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (meta != NULL);

  buffer->metas = g_slist_remove (buffer->metas, meta);
  gst_meta_unref (meta);
}
