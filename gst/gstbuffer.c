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

/**
 * SECTION:gstbuffer
 * @short_description: Data-passing buffer type, supporting sub-buffers.
 * @see_also: #GstPad, #GstMiniObject
 *
 * Buffers are the basic unit of data transfer in GStreamer.  The #GstBuffer
 * type provides all the state necessary to define a region of memory as part
 * of a stream.  Sub-buffers are also supported, allowing a smaller region of a
 * buffer to become its own buffer, with mechanisms in place to ensure that
 * neither memory space goes away prematurely.
 *
 * Buffers are usually created with gst_buffer_new(). After a buffer has been
 * created one will typically allocate memory for it and set the size of the
 * buffer data.  The following example creates a buffer that can hold a given
 * video frame with a given width, height and bits per plane.
 * <example>
 * <title>Creating a buffer for a video frame</title>
 *   <programlisting>
 *   GstBuffer *buffer;
 *   gint size, width, height, bpp;
 *   ...
 *   size = width * height * bpp;
 *   buffer = gst_buffer_new ();
 *   GST_BUFFER_SIZE (buffer) = size;
 *   GST_BUFFER_MALLOCDATA (buffer) = g_malloc (size);
 *   GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);
 *   ...
 *   </programlisting>
 * </example>
 *
 * Alternatively, use gst_buffer_new_and_alloc()
 * to create a buffer with preallocated data of a given size.
 *
 * The data pointed to by the buffer can be retrieved with the GST_BUFFER_DATA()
 * macro. The size of the data can be found with GST_BUFFER_SIZE(). For buffers
 * of size 0, the data pointer is undefined (usually NULL) and should never be used.
 *
 * If an element knows what pad you will push the buffer out on, it should use
 * gst_pad_alloc_buffer() instead to create a buffer.  This allows downstream
 * elements to provide special buffers to write in, like hardware buffers.
 *
 * A buffer has a pointer to a #GstCaps describing the media type of the data
 * in the buffer. Attach caps to the buffer with gst_buffer_set_caps(); this
 * is typically done before pushing out a buffer using gst_pad_push() so that
 * the downstream element knows the type of the buffer.
 *
 * A buffer will usually have a timestamp, and a duration, but neither of these
 * are guaranteed (they may be set to #GST_CLOCK_TIME_NONE). Whenever a
 * meaningful value can be given for these, they should be set. The timestamp
 * and duration are measured in nanoseconds (they are #GstClockTime values).
 *
 * A buffer can also have one or both of a start and an end offset. These are
 * media-type specific. For video buffers, the start offset will generally be
 * the frame number. For audio buffers, it will be the number of samples
 * produced so far. For compressed data, it could be the byte offset in a
 * source or destination file. Likewise, the end offset will be the offset of
 * the end of the buffer. These can only be meaningfully interpreted if you
 * know the media type of the buffer (the #GstCaps set on it). Either or both
 * can be set to #GST_BUFFER_OFFSET_NONE.
 *
 * gst_buffer_ref() is used to increase the refcount of a buffer. This must be
 * done when you want to keep a handle to the buffer after pushing it to the
 * next element.
 *
 * To efficiently create a smaller buffer out of an existing one, you can
 * use gst_buffer_create_sub().
 *
 * If a plug-in wants to modify the buffer data in-place, it should first obtain
 * a buffer that is safe to modify by using gst_buffer_make_writable().  This
 * function is optimized so that a copy will only be made when it is necessary.
 *
 * A plugin that only wishes to modify the metadata of a buffer, such as the
 * offset, timestamp or caps, should use gst_buffer_make_metadata_writable(),
 * which will create a subbuffer of the original buffer to ensure the caller
 * has sole ownership, and not copy the buffer data.
 *
 * Several flags of the buffer can be set and unset with the
 * GST_BUFFER_FLAG_SET() and GST_BUFFER_FLAG_UNSET() macros. Use
 * GST_BUFFER_FLAG_IS_SET() to test if a certain #GstBufferFlag is set.
 *
 * Buffers can be efficiently merged into a larger buffer with
 * gst_buffer_merge() and gst_buffer_span() if the gst_buffer_is_span_fast()
 * function returns TRUE.
 *
 * An element should either unref the buffer or push it out on a src pad
 * using gst_pad_push() (see #GstPad).
 *
 * Buffers are usually freed by unreffing them with gst_buffer_unref(). When
 * the refcount drops to 0, any data pointed to by GST_BUFFER_MALLOCDATA() will
 * also be freed.
 *
 * Last reviewed on August 11th, 2006 (0.10.10)
 */
#include "gst_private.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "gstbuffer.h"
#include "gstbufferpool.h"
#include "gstinfo.h"
#include "gstutils.h"
#include "gstminiobject.h"
#include "gstversion.h"

GType _gst_buffer_type = 0;

typedef struct _GstMetaItem GstMetaItem;

struct _GstMetaItem
{
  GstMetaItem *next;
  GstMeta meta;
};

#define ITEM_SIZE(info) ((info)->size + sizeof (GstMetaItem))

/* buffer alignment in bytes
 * an alignment of 8 would be the same as malloc() guarantees
 */
#ifdef HAVE_POSIX_MEMALIGN
#if defined(BUFFER_ALIGNMENT_MALLOC)
static size_t _gst_buffer_data_alignment = 8;
#elif defined(BUFFER_ALIGNMENT_PAGESIZE)
static size_t _gst_buffer_data_alignment = 0;
#elif defined(BUFFER_ALIGNMENT)
static size_t _gst_buffer_data_alignment = BUFFER_ALIGNMENT;
#else
#error "No buffer alignment configured"
#endif
#endif /* HAVE_POSIX_MEMALIGN */

void
_gst_buffer_initialize (void)
{
  if (G_LIKELY (_gst_buffer_type == 0)) {
    _gst_buffer_type = gst_mini_object_register ("GstBuffer");
#ifdef HAVE_GETPAGESIZE
#ifdef BUFFER_ALIGNMENT_PAGESIZE
    _gst_buffer_data_alignment = getpagesize ();
#endif
#endif
  }
}

/**
 * gst_buffer_copy_into:
 * @dest: a destination #GstBuffer
 * @src: a source #GstBuffer
 * @flags: flags indicating what metadata fields should be copied.
 * @offset: offset to copy from
 * @size: total size to copy
 *
 * Copies the information from @src into @dest.
 *
 * @flags indicate which fields will be copied.
 */
void
gst_buffer_copy_into (GstBuffer * dest, GstBuffer * src,
    GstBufferCopyFlags flags, gsize offset, gsize size)
{
  GstMetaItem *walk;
  gsize bufsize;

  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);

  /* nothing to copy if the buffers are the same */
  if (G_UNLIKELY (dest == src))
    return;

  bufsize = gst_buffer_get_size (src);
  if (size == -1)
    size = bufsize - offset;
  g_return_if_fail (bufsize >= offset + size);

#if GST_VERSION_NANO == 1
  /* we enable this extra debugging in git versions only for now */
  g_warn_if_fail (gst_buffer_is_writable (dest));
#endif

  GST_CAT_LOG (GST_CAT_BUFFER, "copy %p to %p", src, dest);

  if (flags & GST_BUFFER_COPY_FLAGS) {
    guint mask;

    /* copy relevant flags */
    mask = GST_BUFFER_FLAG_PREROLL | GST_BUFFER_FLAG_IN_CAPS |
        GST_BUFFER_FLAG_DELTA_UNIT | GST_BUFFER_FLAG_DISCONT |
        GST_BUFFER_FLAG_GAP | GST_BUFFER_FLAG_MEDIA1 |
        GST_BUFFER_FLAG_MEDIA2 | GST_BUFFER_FLAG_MEDIA3;
    GST_MINI_OBJECT_FLAGS (dest) |= GST_MINI_OBJECT_FLAGS (src) & mask;
  }

  if (flags & GST_BUFFER_COPY_TIMESTAMPS) {
    if (offset == 0) {
      GST_BUFFER_TIMESTAMP (dest) = GST_BUFFER_TIMESTAMP (src);
      GST_BUFFER_OFFSET (dest) = GST_BUFFER_OFFSET (src);
      if (size == gst_buffer_get_size (src)) {
        GST_BUFFER_DURATION (dest) = GST_BUFFER_DURATION (src);
        GST_BUFFER_OFFSET_END (dest) = GST_BUFFER_OFFSET_END (src);
      }
    } else {
      GST_BUFFER_TIMESTAMP (dest) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (dest) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_OFFSET (dest) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END (dest) = GST_BUFFER_OFFSET_NONE;
    }
  }

  if (flags & GST_BUFFER_COPY_CAPS) {
    gst_caps_replace (&GST_BUFFER_CAPS (dest), GST_BUFFER_CAPS (src));
  }

  if (flags & GST_BUFFER_COPY_MEMORY) {
    GPtrArray *sarr = (GPtrArray *) src->memory;
    GPtrArray *darr = (GPtrArray *) dest->memory;
    guint i, len;

    len = sarr->len;

    for (i = 0; i < len; i++) {
      GstMemory *mem, *dmem;
      gsize msize;

      mem = g_ptr_array_index (sarr, i);
      msize = gst_memory_get_sizes (mem, NULL);

      if (i + 1 == len) {
        /* last chunk */
        dmem = gst_memory_sub (mem, offset, size);
      } else if (offset) {
        if (msize > offset) {
          dmem = gst_memory_sub (mem, offset, msize - offset);
          offset = 0;
        } else {
          offset -= msize;
          dmem = NULL;
        }
      } else
        dmem = gst_memory_ref (mem);

      if (dmem)
        g_ptr_array_add (darr, dmem);
    }
  }

  for (walk = src->priv; walk; walk = walk->next) {
    GstMeta *meta = &walk->meta;
    const GstMetaInfo *info = meta->info;

    if (info->copy_func)
      info->copy_func (dest, meta, (GstBuffer *) src, offset, size);
  }
}

static GstBuffer *
_gst_buffer_copy (GstBuffer * buffer)
{
  GstBuffer *copy;

  g_return_val_if_fail (buffer != NULL, NULL);

  /* create a fresh new buffer */
  copy = gst_buffer_new ();

  /* we simply copy everything from our parent */
  gst_buffer_copy_into (copy, buffer, GST_BUFFER_COPY_ALL, 0, -1);

  return copy;
}

/* the default dispose function revives the buffer and returns it to the
 * pool when there is a pool */
static void
_gst_buffer_dispose (GstBuffer * buffer)
{
  GstBufferPool *pool;

  if ((pool = buffer->pool) != NULL) {
    /* keep the buffer alive */
    gst_buffer_ref (buffer);
    /* return the buffer to the pool */
    GST_CAT_LOG (GST_CAT_BUFFER, "release %p to pool %p", buffer, pool);
    gst_buffer_pool_release_buffer (pool, buffer);
  }
}

static void
_gst_buffer_free (GstBuffer * buffer)
{
  GstMetaItem *walk, *next;

  g_return_if_fail (buffer != NULL);

  GST_CAT_LOG (GST_CAT_BUFFER, "finalize %p", buffer);

  gst_caps_replace (&GST_BUFFER_CAPS (buffer), NULL);

  /* free metadata */
  for (walk = buffer->priv; walk; walk = next) {
    GstMeta *meta = &walk->meta;
    const GstMetaInfo *info = meta->info;

    /* call free_func if any */
    if (info->free_func)
      info->free_func (meta, buffer);

    next = walk->next;
    /* and free the slice */
    g_slice_free1 (ITEM_SIZE (info), walk);
  }

  /* free our data, unrefs the memory too */
  g_ptr_array_free (buffer->memory, TRUE);

  g_slice_free1 (GST_MINI_OBJECT_SIZE (buffer), buffer);
}

static void
gst_buffer_init (GstBuffer * buffer, gsize size)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (buffer), _gst_buffer_type, size);

  buffer->mini_object.copy = (GstMiniObjectCopyFunction) _gst_buffer_copy;
  buffer->mini_object.dispose =
      (GstMiniObjectDisposeFunction) _gst_buffer_dispose;
  buffer->mini_object.free = (GstMiniObjectFreeFunction) _gst_buffer_free;

  GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;

  /* FIXME, do more efficient with array in the buffer memory itself */
  buffer->memory =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_memory_unref);
}

/**
 * gst_buffer_new:
 *
 * Creates a newly allocated buffer without any data.
 *
 * MT safe.
 *
 * Returns: (transfer full): the new #GstBuffer.
 */
GstBuffer *
gst_buffer_new (void)
{
  GstBuffer *newbuf;

  newbuf = g_slice_new0 (GstBuffer);
  GST_CAT_LOG (GST_CAT_BUFFER, "new %p", newbuf);

  gst_buffer_init (newbuf, sizeof (GstBuffer));

  return newbuf;
}

/**
 * gst_buffer_new_and_alloc:
 * @size: the size in bytes of the new buffer's data.
 *
 * Creates a newly allocated buffer with data of the given size.
 * The buffer memory is not cleared. If the requested amount of
 * memory can't be allocated, the program will abort. Use
 * gst_buffer_try_new_and_alloc() if you want to handle this case
 * gracefully or have gotten the size to allocate from an untrusted
 * source such as a media stream.
 *
 * Note that when @size == 0, the buffer data pointer will be NULL.
 *
 * MT safe.
 *
 * Returns: (transfer full): the new #GstBuffer.
 */
GstBuffer *
gst_buffer_new_and_alloc (guint size)
{
  GstBuffer *newbuf;

  newbuf = gst_buffer_new ();

  if (size > 0) {
    gst_buffer_take_memory (newbuf, gst_memory_new_alloc (size,
            _gst_buffer_data_alignment));
  }

  GST_CAT_LOG (GST_CAT_BUFFER, "new %p of size %d", newbuf, size);

  return newbuf;
}

/**
 * gst_buffer_try_new_and_alloc:
 * @size: the size in bytes of the new buffer's data.
 *
 * Tries to create a newly allocated buffer with data of the given size. If
 * the requested amount of memory can't be allocated, NULL will be returned.
 * The buffer memory is not cleared.
 *
 * Note that when @size == 0, the buffer will not have memory associated with it.
 *
 * MT safe.
 *
 * Returns: (transfer full): a new #GstBuffer, or NULL if the memory couldn't
 *     be allocated.
 */
GstBuffer *
gst_buffer_try_new_and_alloc (guint size)
{
  GstBuffer *newbuf;
  GstMemory *mem;

  if (size > 0) {
    mem = gst_memory_new_alloc (size, _gst_buffer_data_alignment);
    if (G_UNLIKELY (mem == NULL))
      goto no_memory;
  } else {
    mem = NULL;
  }

  newbuf = gst_buffer_new ();

  if (mem != NULL)
    gst_buffer_take_memory (newbuf, mem);

  GST_CAT_LOG (GST_CAT_BUFFER, "new %p of size %d", newbuf, size);

  return newbuf;

  /* ERRORS */
no_memory:
  {
    GST_CAT_WARNING (GST_CAT_BUFFER, "failed to allocate %d bytes", size);
    return NULL;
  }
}

/**
 * gst_buffer_n_memory:
 * @buffer: a #GstBuffer.
 *
 * Get the amount of memory blocks that this buffer has.
 *
 * Returns: (transfer full): the amount of memory block in this buffer.
 */
guint
gst_buffer_n_memory (GstBuffer * buffer)
{
  GPtrArray *arr;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  arr = (GPtrArray *) buffer->memory;

  return arr->len;
}

/**
 * gst_buffer_take_memory:
 * @buffer: a #GstBuffer.
 * @mem: a #GstMemory.
 *
 * Add the memory block @mem to @buffer. This function takes ownership of @mem
 * and thus doesn't increase its refcount.
 */
void
gst_buffer_take_memory (GstBuffer * buffer, GstMemory * mem)
{
  GPtrArray *arr;

  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (gst_buffer_is_writable (buffer));
  g_return_if_fail (mem != NULL);

  arr = (GPtrArray *) buffer->memory;
  g_ptr_array_add (arr, mem);
}

/**
 * gst_buffer_peek_memory:
 * @buffer: a #GstBuffer.
 * @idx: an index
 *
 * Get the memory block in @buffer at @idx. This function does not return a
 * refcount to the memory block. The memory block stays valid for as long as the
 * caller has a valid reference to @buffer.
 *
 * Returns: a #GstMemory at @idx.
 */
GstMemory *
gst_buffer_peek_memory (GstBuffer * buffer, guint idx)
{
  GstMemory *mem;
  GPtrArray *arr;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  arr = (GPtrArray *) buffer->memory;
  g_return_val_if_fail (idx < arr->len, NULL);

  mem = g_ptr_array_index (arr, idx);

  return mem;
}

/**
 * gst_buffer_remove_memory:
 * @buffer: a #GstBuffer.
 * @idx: an index
 *
 * Remove the memory block in @buffer at @idx.
 */
void
gst_buffer_remove_memory (GstBuffer * buffer, guint idx)
{
  GPtrArray *arr;

  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (gst_buffer_is_writable (buffer));
  arr = (GPtrArray *) buffer->memory;
  g_return_if_fail (idx < arr->len);

  g_ptr_array_remove_index (arr, idx);
}

/**
 * gst_buffer_get_size:
 * @buffer: a #GstBuffer.
 *
 * Get the total size of all memory blocks in @buffer.
 *
 * Returns: the total size of the memory in @buffer.
 */
gsize
gst_buffer_get_size (GstBuffer * buffer)
{
  GPtrArray *arr = (GPtrArray *) buffer->memory;
  guint i, size, len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  len = arr->len;

  size = 0;
  for (i = 0; i < len; i++) {
    size += gst_memory_get_sizes (g_ptr_array_index (arr, i), NULL);
  }
  return size;
}

/**
 * gst_buffer_set_size:
 * @buffer: a #GstBuffer.
 * @size: the new size
 *
 * Set the total size of the buffer
 */
void
gst_buffer_set_size (GstBuffer * buffer, gsize size)
{
  /* FIXME */
  g_warning ("gst_buffer_set_size not imlpemented");
}

/**
 * gst_buffer_map:
 * @buffer: a #GstBuffer.
 * @size: a location for the size
 * @maxsize: a location for the max size
 * @flags: flags for the mapping
 *
 * This function return a pointer to the memory in @buffer. @flags describe the
 * desired access of the memory. When @flags is #GST_MAP_WRITE, @buffer should
 * be writable (as returned from gst_buffer_is_writable()).
 *
 * @size and @maxsize will contain the current valid number of bytes in the
 * returned memory area and the total maximum mount of bytes available in the
 * returned memory area respectively.
 *
 * When @buffer is writable but the memory isn't, a writable copy will
 * automatically be created and returned. The readonly copy of the buffer memory
 * will then also be replaced with this writable copy.
 *
 * When the buffer contains multiple memory blocks, the returned pointer will be
 * a concatenation of the memory blocks.
 *
 * Returns: a pointer to the memory for the buffer.
 */
gpointer
gst_buffer_map (GstBuffer * buffer, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  GPtrArray *arr;
  guint len;
  gpointer data;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  arr = (GPtrArray *) buffer->memory;
  len = arr->len;

  if (G_UNLIKELY ((flags & GST_MAP_WRITE) && !gst_buffer_is_writable (buffer)))
    goto not_writable;

  if (G_LIKELY (len == 1)) {
    GstMemory *mem;

    mem = g_ptr_array_index (arr, 0);

    if (flags & GST_MAP_WRITE) {
      if (G_UNLIKELY (!GST_MEMORY_IS_WRITABLE (mem))) {
        GstMemory *copy;

        /* replace with a writable copy */
        copy = gst_memory_copy (mem, 0, gst_memory_get_sizes (mem, NULL));
        g_ptr_array_index (arr, 0) = copy;
        gst_memory_unref (mem);
        mem = copy;
      }
    }

    data = gst_memory_map (mem, size, maxsize, flags);
  } else {
    data = NULL;
  }
  return data;

  /* ERROR */
not_writable:
  {
    g_return_val_if_fail (gst_buffer_is_writable (buffer), NULL);
    return NULL;
  }
}

/**
 * gst_buffer_unmap:
 * @buffer: a #GstBuffer.
 * @data: the previously mapped data
 * @size: the size of @data
 *
 * Release the memory previously mapped with gst_buffer_map().
 *
 * Returns: #TRUE on success. #FALSE can be returned when the new size is larger
 * than the maxsize of the memory.
 */
gboolean
gst_buffer_unmap (GstBuffer * buffer, gpointer data, gsize size)
{
  GPtrArray *arr;
  gboolean result;
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  arr = (GPtrArray *) buffer->memory;
  len = arr->len;

  if (G_LIKELY (len == 1)) {
    GstMemory *mem = g_ptr_array_index (arr, 0);

    result = gst_memory_unmap (mem, data, size);
  } else {
    result = FALSE;
  }
  return result;
}

/**
 * gst_buffer_fill:
 * @buffer: a #GstBuffer.
 * @offset: the offset to fill
 * @src: the source address
 * @size: the size to fill
 *
 * Copy @size bytes fro @src to @buffer at @offset.
 */
void
gst_buffer_fill (GstBuffer * buffer, gsize offset, gconstpointer src,
    gsize size)
{
  GPtrArray *arr = (GPtrArray *) buffer->memory;
  gsize i, len;
  const guint8 *ptr = src;

  len = arr->len;

  for (i = 0; i < len && size > 0; i++) {
    guint8 *data;
    gsize ssize, tocopy;
    GstMemory *mem;

    mem = g_ptr_array_index (arr, i);

    data = gst_memory_map (mem, &ssize, NULL, GST_MAP_WRITE);
    if (ssize > offset) {
      /* we have enough */
      tocopy = MIN (ssize - offset, size);
      memcpy (data + offset, ptr, tocopy);
      size -= tocopy;
      ptr += tocopy;
      offset = 0;
    } else {
      /* offset past buffer, skip */
      offset -= ssize;
    }
    gst_memory_unmap (mem, data, ssize);
  }
}

/**
 * gst_buffer_extract:
 * @buffer: a #GstBuffer.
 * @offset: the offset to extract
 * @dest: the destination address
 * @size: the size to extract
 *
 * Copy @size bytes starting from @offset in @buffer to @dest.
 */
void
gst_buffer_extract (GstBuffer * buffer, gsize offset, gpointer dest, gsize size)
{
  GPtrArray *arr = (GPtrArray *) buffer->memory;
  gsize i, len;
  guint8 *ptr = dest;

  len = arr->len;

  for (i = 0; i < len && size > 0; i++) {
    guint8 *data;
    gsize ssize, tocopy;
    GstMemory *mem;

    mem = g_ptr_array_index (arr, i);

    data = gst_memory_map (mem, &ssize, NULL, GST_MAP_READ);
    if (ssize > offset) {
      /* we have enough */
      tocopy = MIN (ssize - offset, size);
      memcpy (ptr, data + offset, tocopy);
      size -= tocopy;
      ptr += tocopy;
      offset = 0;
    } else {
      /* offset past buffer, skip */
      offset -= ssize;
    }
    gst_memory_unmap (mem, data, ssize);
  }
}

/**
 * gst_buffer_get_caps:
 * @buffer: a #GstBuffer.
 *
 * Gets the media type of the buffer. This can be NULL if there
 * is no media type attached to this buffer.
 *
 * Returns: (transfer full): a reference to the #GstCaps. unref after usage.
 * Returns NULL if there were no caps on this buffer.
 */
/* this is not made atomic because if the buffer were reffed from multiple
 * threads, it would have a refcount > 2 and thus be immutable.
 */
GstCaps *
gst_buffer_get_caps (GstBuffer * buffer)
{
  GstCaps *ret;

  g_return_val_if_fail (buffer != NULL, NULL);

  ret = GST_BUFFER_CAPS (buffer);

  if (ret)
    gst_caps_ref (ret);

  return ret;
}

/**
 * gst_buffer_set_caps:
 * @buffer: a #GstBuffer.
 * @caps: (transfer none): a #GstCaps.
 *
 * Sets the media type on the buffer. The refcount of the caps will
 * be increased and any previous caps on the buffer will be
 * unreffed.
 */
/* this is not made atomic because if the buffer were reffed from multiple
 * threads, it would have a refcount > 2 and thus be immutable.
 */
void
gst_buffer_set_caps (GstBuffer * buffer, GstCaps * caps)
{
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (caps == NULL || GST_CAPS_IS_SIMPLE (caps));

#if GST_VERSION_NANO == 1
  /* we enable this extra debugging in git versions only for now */
  g_warn_if_fail (gst_buffer_is_writable (buffer));
  /* FIXME: would be nice to also check if caps are fixed here, but expensive */
#endif

  gst_caps_replace (&GST_BUFFER_CAPS (buffer), caps);
}

/**
 * gst_buffer_create_sub:
 * @parent: a #GstBuffer.
 * @offset: the offset into parent #GstBuffer at which the new sub-buffer 
 *          begins.
 * @size: the size of the new #GstBuffer sub-buffer, in bytes.
 *
 * Creates a sub-buffer from @parent at @offset and @size.
 * This sub-buffer uses the actual memory space of the parent buffer.
 * This function will copy the offset and timestamp fields when the
 * offset is 0. If not, they will be set to #GST_CLOCK_TIME_NONE and 
 * #GST_BUFFER_OFFSET_NONE.
 * If @offset equals 0 and @size equals the total size of @buffer, the
 * duration and offset end fields are also copied. If not they will be set
 * to #GST_CLOCK_TIME_NONE and #GST_BUFFER_OFFSET_NONE.
 *
 * MT safe.
 *
 * Returns: (transfer full): the new #GstBuffer or NULL if the arguments were
 *     invalid.
 */
GstBuffer *
gst_buffer_create_sub (GstBuffer * buffer, gsize offset, gsize size)
{
  GstBuffer *subbuffer;
  gsize bufsize;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (buffer->mini_object.refcount > 0, NULL);

  bufsize = gst_buffer_get_size (buffer);
  g_return_val_if_fail (bufsize >= offset + size, NULL);

  /* create the new buffer */
  subbuffer = gst_buffer_new ();

  GST_CAT_LOG (GST_CAT_BUFFER, "new subbuffer %p of %p", subbuffer, buffer);

  gst_buffer_copy_into (subbuffer, buffer, GST_BUFFER_COPY_ALL, offset, size);

  return subbuffer;
}

/**
 * gst_buffer_is_span_fast:
 * @buf1: the first #GstBuffer.
 * @buf2: the second #GstBuffer.
 *
 * Determines whether a gst_buffer_span() can be done without copying
 * the contents, that is, whether the data areas are contiguous sub-buffers of
 * the same buffer.
 *
 * MT safe.
 * Returns: TRUE if the buffers are contiguous,
 * FALSE if a copy would be required.
 */
gboolean
gst_buffer_is_span_fast (GstBuffer * buf1, GstBuffer * buf2)
{
  GPtrArray *arr1, *arr2;

  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, FALSE);
  g_return_val_if_fail (buf1->mini_object.refcount > 0, FALSE);
  g_return_val_if_fail (buf2->mini_object.refcount > 0, FALSE);

  arr1 = (GPtrArray *) buf1->memory;
  arr2 = (GPtrArray *) buf2->memory;

  return gst_memory_is_span ((GstMemory **) arr1->pdata, arr1->len,
      (GstMemory **) arr2->pdata, arr2->len, NULL, NULL);
}

/**
 * gst_buffer_span:
 * @buf1: the first source #GstBuffer to merge.
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
 * MT safe.
 *
 * Returns: (transfer full): the new #GstBuffer that spans the two source
 *     buffers, or NULL if the arguments are invalid.
 */
GstBuffer *
gst_buffer_span (GstBuffer * buf1, gsize offset, GstBuffer * buf2, gsize len)
{
  GstBuffer *newbuf;
  GPtrArray *arr1, *arr2;
  GstMemory *mem;

  g_return_val_if_fail (buf1 != NULL && buf2 != NULL, NULL);
  g_return_val_if_fail (buf1->mini_object.refcount > 0, NULL);
  g_return_val_if_fail (buf2->mini_object.refcount > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);
  g_return_val_if_fail (len <= gst_buffer_get_size (buf1) +
      gst_buffer_get_size (buf2) - offset, NULL);

  newbuf = gst_buffer_new ();

  arr1 = (GPtrArray *) buf1->memory;
  arr2 = (GPtrArray *) buf2->memory;

  mem = gst_memory_span ((GstMemory **) arr1->pdata, arr1->len, offset,
      (GstMemory **) arr2->pdata, arr2->len, len);
  gst_buffer_take_memory (newbuf, mem);

#if 0
  /* if the offset is 0, the new buffer has the same timestamp as buf1 */
  if (offset == 0) {
    GST_BUFFER_OFFSET (newbuf) = GST_BUFFER_OFFSET (buf1);
    GST_BUFFER_TIMESTAMP (newbuf) = GST_BUFFER_TIMESTAMP (buf1);

    /* if we completely merged the two buffers (appended), we can
     * calculate the duration too. Also make sure we's not messing with
     * invalid DURATIONS */
    if (buf1->size + buf2->size == len) {
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
  }
#endif

  return newbuf;
}

/**
 * gst_buffer_get_meta:
 * @buffer: a #GstBuffer
 * @info: a #GstMetaInfo
 *
 * Get the metadata for the api in @info on buffer. When there is no such
 * metadata, NULL is returned.
 *
 * Note that the result metadata might not be of the implementation @info.
 *
 * Returns: the metadata for the api in @info on @buffer.
 */
GstMeta *
gst_buffer_get_meta (GstBuffer * buffer, const GstMetaInfo * info)
{
  GstMetaItem *item;
  GstMeta *result = NULL;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);

  /* find GstMeta of the requested API */
  for (item = buffer->priv; item; item = item->next) {
    GstMeta *meta = &item->meta;
    if (meta->info->api == info->api) {
      result = meta;
      break;
    }
  }
  return result;
}

/**
 * gst_buffer_add_meta:
 * @buffer: a #GstBuffer
 * @info: a #GstMetaInfo
 * @params: params for @info
 *
 * Add metadata for @info to @buffer using the parameters in @params.
 *
 * Returns: the metadata for the api in @info on @buffer.
 */
GstMeta *
gst_buffer_add_meta (GstBuffer * buffer, const GstMetaInfo * info,
    gpointer params)
{
  GstMetaItem *item;
  GstMeta *result = NULL;
  gsize size;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);

  /* create a new slice */
  GST_CAT_DEBUG (GST_CAT_BUFFER, "alloc metadata of size %" G_GSIZE_FORMAT,
      info->size);

  size = ITEM_SIZE (info);
  item = g_slice_alloc (size);
  result = &item->meta;
  result->info = info;

  /* call the init_func when needed */
  if (info->init_func)
    if (!info->init_func (result, params, buffer))
      goto init_failed;

  /* and add to the list of metadata */
  item->next = buffer->priv;
  buffer->priv = item;

  return result;

init_failed:
  {
    g_slice_free1 (size, item);
    return NULL;
  }
}

/**
 * gst_buffer_remove_meta:
 * @buffer: a #GstBuffer
 * @info: a #GstMetaInfo
 *
 * Remove the metadata for @info on @buffer.
 *
 * Returns: %TRUE if the metadata existed and was removed, %FALSE if no such
 * metadata was on @buffer.
 */
gboolean
gst_buffer_remove_meta (GstBuffer * buffer, GstMeta * meta)
{
  GstMetaItem *walk, *prev;

  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);

  /* find the metadata and delete */
  prev = buffer->priv;
  for (walk = prev; walk; walk = walk->next) {
    GstMeta *m = &walk->meta;
    if (m == meta) {
      const GstMetaInfo *info = meta->info;

      /* remove from list */
      if (buffer->priv == walk)
        buffer->priv = walk->next;
      else
        prev->next = walk->next;
      /* call free_func if any */
      if (info->free_func)
        info->free_func (m, buffer);

      /* and free the slice */
      g_slice_free1 (ITEM_SIZE (info), walk);
      break;
    }
    prev = walk;
  }
  return walk != NULL;
}

/**
 * gst_buffer_iterate_meta:
 * @buffer: a #GstBuffer
 * @state: an opaque state pointer
 *
 * Retrieve the next #GstMeta after @current. If @state points
 * to %NULL, the first metadata is returned.
 *
 * @state will be updated with an opage state pointer 
 *
 * Returns: The next #GstMeta or %NULL when there are no more items.
 */
GstMeta *
gst_buffer_iterate_meta (GstBuffer * buffer, gpointer * state)
{
  GstMetaItem **meta;

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (state != NULL, NULL);

  meta = (GstMetaItem **) state;
  if (*meta == NULL)
    /* state NULL, move to first item */
    *meta = buffer->priv;
  else
    /* state !NULL, move to next item in list */
    *meta = (*meta)->next;

  if (*meta)
    return &(*meta)->meta;
  else
    return NULL;
}
