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

#include "gst_private.h"

#include "gstatomic_impl.h"
#include "gstdata_private.h"
#include "gstbuffer.h"
#include "gstmemchunk.h"
#include "gstinfo.h"

GType _gst_buffer_type;
GType _gst_buffer_pool_type;

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE  */
#include "gsttrace.h"

static GstAllocTrace *_gst_buffer_trace;
#endif

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

#ifndef GST_DISABLE_TRACE
  _gst_buffer_trace = gst_alloc_trace_register (GST_BUFFER_TRACE_NAME);
#endif

  chunk = gst_mem_chunk_new ("GstBufferChunk", sizeof (GstBuffer), 
                             sizeof (GstBuffer) * 200, 0);

  GST_CAT_INFO (GST_CAT_BUFFER, "Buffers are initialized now");
}

GType
gst_buffer_get_type (void)
{
  return _gst_buffer_type;
}

static void
_gst_buffer_sub_free (GstBuffer *buffer)
{
  gst_data_unref (GST_DATA (buffer->pool_private));

  GST_BUFFER_DATA (buffer) = NULL;
  GST_BUFFER_SIZE (buffer) = 0;

  _GST_DATA_DISPOSE (GST_DATA (buffer));
  
  gst_mem_chunk_free (chunk, GST_DATA (buffer));
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_buffer_trace, buffer);
#endif
}

/**
 * gst_buffer_default_free:
 * @buffer: a #GstBuffer to free.
 *
 * Frees the memory associated with the buffer including the buffer data,
 * unless the GST_BUFFER_DONTFREE flags was set or the buffer data is NULL.
 * This function is used by buffer pools.
 */
void
gst_buffer_default_free (GstBuffer *buffer)
{
  g_return_if_fail (buffer != NULL);

  /* free our data */
  if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE) && GST_BUFFER_DATA (buffer)) 
    g_free (GST_BUFFER_DATA (buffer));

  /* set to safe values */
  GST_BUFFER_DATA (buffer) = NULL;
  GST_BUFFER_SIZE (buffer) = 0;

  _GST_DATA_DISPOSE (GST_DATA (buffer));

  gst_mem_chunk_free (chunk, GST_DATA (buffer));
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_buffer_trace, buffer);
#endif
}

/**
 * gst_buffer_stamp:
 * @dest: buffer to stamp
 * @src: buffer to stamp from
 *
 * Copies additional information (timestamps and offsets) from one buffer to
 * the other.
 */
void
gst_buffer_stamp (GstBuffer *dest, const GstBuffer *src)
{
  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);
  
  GST_BUFFER_TIMESTAMP (dest) 	 = GST_BUFFER_TIMESTAMP (src);
  GST_BUFFER_DURATION (dest) 	 = GST_BUFFER_DURATION (src);
  GST_BUFFER_OFFSET (dest) 	 = GST_BUFFER_OFFSET (src);
  GST_BUFFER_OFFSET_END (dest) 	 = GST_BUFFER_OFFSET_END (src);
}
/**
 * gst_buffer_default_copy:
 * @buffer: a #GstBuffer to make a copy of.
 *
 * Make a full newly allocated copy of the given buffer, data and all.
 * This function is used by buffer pools.
 *
 * Returns: the new #GstBuffer.
 */
GstBuffer*
gst_buffer_default_copy (GstBuffer *buffer)
{
  GstBuffer *copy;

  g_return_val_if_fail (buffer != NULL, NULL);

  /* create a fresh new buffer */
  copy = gst_mem_chunk_alloc (chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_buffer_trace, copy);
#endif

  _GST_DATA_INIT (GST_DATA (copy), 
		  _gst_buffer_type,
		  0,
		  (GstDataFreeFunction) gst_buffer_default_free,
		  (GstDataCopyFunction) gst_buffer_default_copy);

  /* we simply copy everything from our parent */
  GST_BUFFER_DATA (copy) 	 = g_memdup (GST_BUFFER_DATA (buffer), 
                                             GST_BUFFER_SIZE (buffer));
  GST_BUFFER_SIZE (copy)  	 = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_MAXSIZE (copy) 	 = GST_BUFFER_SIZE (buffer);

  gst_buffer_stamp (copy, buffer);
  GST_BUFFER_BUFFERPOOL (copy)   = NULL;
  GST_BUFFER_POOL_PRIVATE (copy) = NULL;

  return copy;
}

/**
 * gst_buffer_new:
 *
 * Creates a newly allocated buffer without any data.
 *
 * Returns: the new #GstBuffer.
 */
GstBuffer*
gst_buffer_new (void)
{
  GstBuffer *newbuf;
  
  newbuf = gst_mem_chunk_alloc (chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_buffer_trace, newbuf);
#endif

  GST_CAT_LOG (GST_CAT_BUFFER, "new %p", newbuf);

  _GST_DATA_INIT (GST_DATA (newbuf), 
		  _gst_buffer_type,
		  0,
		  (GstDataFreeFunction) gst_buffer_default_free,
		  (GstDataCopyFunction) gst_buffer_default_copy);

  GST_BUFFER_DATA (newbuf)         = NULL;
  GST_BUFFER_SIZE (newbuf)         = 0;
  GST_BUFFER_MAXSIZE (newbuf)      = GST_BUFFER_MAXSIZE_NONE;
  GST_BUFFER_TIMESTAMP (newbuf)    = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (newbuf)     = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (newbuf)       = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (newbuf)   = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_BUFFERPOOL (newbuf)   = NULL;
  GST_BUFFER_POOL_PRIVATE (newbuf) = NULL;

  return newbuf;
}

/**
 * gst_buffer_new_and_alloc:
 * @size: the size of the new buffer's data.
 *
 * Creates a newly allocated buffer with data of the given size.
 *
 * Returns: the new #GstBuffer.
 */
GstBuffer*
gst_buffer_new_and_alloc (guint size)
{
  GstBuffer *newbuf;

  newbuf = gst_buffer_new ();

  GST_BUFFER_DATA (newbuf)    = g_malloc (size);
  GST_BUFFER_SIZE (newbuf)    = size;
  GST_BUFFER_MAXSIZE (newbuf) = size;

  return newbuf;
}

/**
 * gst_buffer_create_sub:
 * @parent: a parent #GstBuffer to create a subbuffer from.
 * @offset: the offset into parent #GstBuffer.
 * @size: the size of the new #GstBuffer sub-buffer (with size > 0).
 *
 * Creates a sub-buffer from the parent at a given offset.
 * This sub-buffer uses the actual memory space of the parent buffer.
 * This function will copy the offset and timestamp field when the 
 * offset is 0, else they are set to _NONE.
 * The duration field of the new buffer are set to GST_CLOCK_TIME_NONE.
 *
 * Returns: the new #GstBuffer, or NULL if there was an error.
 */
GstBuffer*
gst_buffer_create_sub (GstBuffer *parent, guint offset, guint size)
{
  GstBuffer *buffer;
  gpointer buffer_data;
	      
  g_return_val_if_fail (parent != NULL, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (parent) > 0, NULL);
  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail (parent->size >= offset + size, NULL);

  /* remember the data for the new buffer */
  buffer_data = parent->data + offset;
  /* make sure we're child not child from a child buffer */
  while (GST_BUFFER_FLAG_IS_SET (parent, GST_BUFFER_SUBBUFFER)) {
    parent = GST_BUFFER (parent->pool_private);
  }
  /* ref the real parent */
  gst_data_ref (GST_DATA (parent));

  /* create the new buffer */
  buffer = gst_mem_chunk_alloc (chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_buffer_trace, buffer);
#endif

  GST_CAT_LOG (GST_CAT_BUFFER, "new subbuffer %p", buffer);

  /* make sure nobody overwrites data in the new buffer 
   * by setting the READONLY flag */
  _GST_DATA_INIT (GST_DATA (buffer), 
		  _gst_buffer_type,
		  GST_DATA_FLAG_SHIFT (GST_BUFFER_SUBBUFFER) |
		  GST_DATA_FLAG_SHIFT (GST_DATA_READONLY),
		  (GstDataFreeFunction) _gst_buffer_sub_free,
		  (GstDataCopyFunction) gst_buffer_default_copy);

  /* set the right values in the child */
  GST_BUFFER_DATA (buffer)         = buffer_data;
  GST_BUFFER_SIZE (buffer)         = size;
  GST_BUFFER_MAXSIZE (buffer)      = size;
  GST_BUFFER_BUFFERPOOL (buffer)   = NULL;
  GST_BUFFER_POOL_PRIVATE (buffer) = parent;
  /* we can copy the timestamp and offset if the new buffer starts at
   * offset 0 */
  if (offset == 0) {
    GST_BUFFER_TIMESTAMP (buffer)    = GST_BUFFER_TIMESTAMP (parent);
    GST_BUFFER_OFFSET (buffer)       = GST_BUFFER_OFFSET (parent);
  }
  else {
    GST_BUFFER_TIMESTAMP (buffer)    = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET (buffer)       = GST_BUFFER_OFFSET_NONE;
  }

  GST_BUFFER_DURATION (buffer)     = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET_END (buffer)   = GST_BUFFER_OFFSET_NONE;
  /* make sure nobody overwrites data as it would overwrite in the parent.
   * data in parent cannot be overwritten because we hold a ref */
  GST_DATA_FLAG_SET (parent, GST_DATA_READONLY);

  return buffer;
}


/**
 * gst_buffer_merge:
 * @buf1: a first source #GstBuffer to merge.
 * @buf2: the second source #GstBuffer to merge.
 *
 * Create a new buffer that is the concatenation of the two source
 * buffers.  The original source buffers will not be modified or
 * unref'd.
 *
 * Internally is nothing more than a specialized gst_buffer_span(),
 * so the same optimizations can occur.
 *
 * Returns: the new #GstBuffer that's the concatenation of the source buffers.
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
 * @buf1: a first source #GstBuffer.
 * @buf2: the second source #GstBuffer.
 *
 * Determines whether a gst_buffer_span() is free (as in free beer), 
 * or requires a memcpy. 
 *
 * Returns: TRUE if the buffers are contiguous, 
 * FALSE if a copy would be required.
 */
gboolean
gst_buffer_is_span_fast (GstBuffer *buf1, GstBuffer *buf2)
{
  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, FALSE);
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
 * @buf1: a first source #GstBuffer to merge.
 * @offset: the offset in the first buffer from where the new
 * buffer should start.
 * @buf2: the second source #GstBuffer to merge.
 * @len: the total length of the new buffer.
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
 * Returns: the new #GstBuffer that spans the two source buffers.
 */
GstBuffer*
gst_buffer_span (GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len)
{
  GstBuffer *newbuf;

  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf1) > 0, NULL);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buf2) > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);
  g_return_val_if_fail (len <= buf1->size + buf2->size - offset, NULL);

  /* if the two buffers have the same parent and are adjacent */
  if (gst_buffer_is_span_fast (buf1, buf2)) {
    GstBuffer *parent = GST_BUFFER (buf1->pool_private);
    /* we simply create a subbuffer of the common parent */
    newbuf = gst_buffer_create_sub (parent, 
	                            buf1->data - parent->data + offset, len);
  }
  else {
    GST_CAT_DEBUG (GST_CAT_BUFFER, "slow path taken while spanning buffers %p and %p", 
	       buf1, buf2);
    /* otherwise we simply have to brute-force copy the buffers */
    newbuf = gst_buffer_new_and_alloc (len);

    /* copy the first buffer's data across */
    memcpy (newbuf->data, buf1->data + offset, buf1->size - offset);
    /* copy the second buffer's data across */
    memcpy (newbuf->data + (buf1->size - offset), buf2->data, 
	    len - (buf1->size - offset));
    /* if the offset is 0, the new buffer has the same timestamp as buf1 */
    if (offset == 0) {
      GST_BUFFER_OFFSET (newbuf) = GST_BUFFER_OFFSET (buf1);
      GST_BUFFER_TIMESTAMP (newbuf) = GST_BUFFER_TIMESTAMP (buf1);
    }
  }
  /* if we completely merged the two buffers (appended), we can
   * calculate the duration too. Also make sure we's not messing with
   * invalid DURATIONS */
  if (offset == 0 && buf1->size + buf2->size == len) {
    if (GST_BUFFER_DURATION_IS_VALID (buf1) &&
	GST_BUFFER_DURATION_IS_VALID (buf2)) {
      /* add duration */
      GST_BUFFER_DURATION (newbuf) = GST_BUFFER_DURATION (buf1) + 
				    GST_BUFFER_DURATION (buf2);
    }
    if (GST_BUFFER_OFFSET_END_IS_VALID (buf2)) {
      /* add offset_end */
      GST_BUFFER_OFFSET_END (newbuf) = GST_BUFFER_OFFSET_END (buf2);
    }
  }

  return newbuf;
}

GType
gst_buffer_pool_get_type (void)
{
  return _gst_buffer_pool_type;
}

