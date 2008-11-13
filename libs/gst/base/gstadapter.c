/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *               2005 Wim Taymans <wim@fluendo.com>
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
 * SECTION:gstadapter
 * @short_description: adapts incoming data on a sink pad into chunks of N bytes
 *
 * This class is for elements that receive buffers in an undesired size.
 * While for example raw video contains one image per buffer, the same is not
 * true for a lot of other formats, especially those that come directly from
 * a file. So if you have undefined buffer sizes and require a specific size,
 * this object is for you.
 *
 * An adapter is created with gst_adapter_new(). It can be freed again with
 * g_object_unref().
 *
 * The theory of operation is like this: All buffers received are put
 * into the adapter using gst_adapter_push() and the data is then read back
 * in chunks of the desired size using gst_adapter_peek(). After the data is
 * processed, it is freed using gst_adapter_flush().
 *
 * For example, a sink pad's chain function that needs to pass data to a library
 * in 512-byte chunks could be implemented like this:
 * <programlisting>
 * static GstFlowReturn
 * sink_pad_chain (GstPad *pad, GstBuffer *buffer)
 * {
 *   MyElement *this;
 *   GstAdapter *adapter;
 *   GstFlowReturn ret = GST_FLOW_OK;
 *   
 *   // will give the element an extra ref; remember to drop it 
 *   this = MY_ELEMENT (gst_pad_get_parent (pad));
 *   adapter = this->adapter;
 *   
 *   // put buffer into adapter
 *   gst_adapter_push (adapter, buffer);
 *   // while we can read out 512 bytes, process them
 *   while (gst_adapter_available (adapter) >= 512 && ret == GST_FLOW_OK) {
 *     // use flowreturn as an error value
 *     ret = my_library_foo (gst_adapter_peek (adapter, 512));
 *     gst_adapter_flush (adapter, 512);
 *   }
 *   
 *   gst_object_unref (this);
 *   return ret;
 * }
 * </programlisting>
 * For another example, a simple element inside GStreamer that uses GstAdapter
 * is the libvisual element.
 *
 * An element using GstAdapter in its sink pad chain function should ensure that
 * when the FLUSH_STOP event is received, that any queued data is cleared using
 * gst_adapter_clear(). Data should also be cleared or processed on EOS and
 * when changing state from #GST_STATE_PAUSED to #GST_STATE_READY.
 *
 * Also check the GST_BUFFER_FLAG_DISCONT flag on the buffer. Some elements might
 * need to clear the adapter after a discontinuity.
 *
 * A last thing to note is that while GstAdapter is pretty optimized,
 * merging buffers still might be an operation that requires a memcpy()
 * operation, and this operation is not the fastest. Because of this, some
 * functions like gst_adapter_available_fast() are provided to help speed up
 * such cases should you want to.
 *
 * GstAdapter is not MT safe. All operations on an adapter must be serialized by
 * the caller. This is not normally a problem, however, as the normal use case
 * of GstAdapter is inside one pad's chain function, in which case access is
 * serialized via the pad's STREAM_LOCK.
 *
 * Note that gst_adapter_push() takes ownership of the buffer passed. Use 
 * gst_buffer_ref() before pushing it into the adapter if you still want to
 * access the buffer later. The adapter will never modify the data in the
 * buffer pushed in it.
 *
 * Last reviewed on 2006-04-04 (0.10.6).
 */


#include "gstadapter.h"
#include <string.h>

/* default size for the assembled data buffer */
#define DEFAULT_SIZE 16

GST_DEBUG_CATEGORY_STATIC (gst_adapter_debug);
#define GST_CAT_DEFAULT gst_adapter_debug

#define _do_init(thing) \
  GST_DEBUG_CATEGORY_INIT (gst_adapter_debug, "adapter", 0, "object to splice and merge buffers to desired size")
GST_BOILERPLATE_FULL (GstAdapter, gst_adapter, GObject, G_TYPE_OBJECT,
    _do_init);

static void gst_adapter_dispose (GObject * object);
static void gst_adapter_finalize (GObject * object);

static void
gst_adapter_base_init (gpointer g_class)
{
  /* nop */
}

static void
gst_adapter_class_init (GstAdapterClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);

  object->dispose = gst_adapter_dispose;
  object->finalize = gst_adapter_finalize;
}

static void
gst_adapter_init (GstAdapter * adapter, GstAdapterClass * g_class)
{
  adapter->assembled_data = g_malloc (DEFAULT_SIZE);
  adapter->assembled_size = DEFAULT_SIZE;
}

static void
gst_adapter_dispose (GObject * object)
{
  GstAdapter *adapter = GST_ADAPTER (object);

  gst_adapter_clear (adapter);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_adapter_finalize (GObject * object)
{
  GstAdapter *adapter = GST_ADAPTER (object);

  g_free (adapter->assembled_data);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

/**
 * gst_adapter_new:
 *
 * Creates a new #GstAdapter. Free with g_object_unref().
 *
 * Returns: a new #GstAdapter
 */
GstAdapter *
gst_adapter_new (void)
{
  return g_object_new (GST_TYPE_ADAPTER, NULL);
}

/**
 * gst_adapter_clear:
 * @adapter: a #GstAdapter
 *
 * Removes all buffers from @adapter.
 */
void
gst_adapter_clear (GstAdapter * adapter)
{
  g_return_if_fail (GST_IS_ADAPTER (adapter));

  g_slist_foreach (adapter->buflist, (GFunc) gst_mini_object_unref, NULL);
  g_slist_free (adapter->buflist);
  adapter->buflist = NULL;
  adapter->buflist_end = NULL;
  adapter->size = 0;
  adapter->skip = 0;
  adapter->assembled_len = 0;
}

/**
 * gst_adapter_push:
 * @adapter: a #GstAdapter
 * @buf: a #GstBuffer to add to queue in the adapter
 *
 * Adds the data from @buf to the data stored inside @adapter and takes
 * ownership of the buffer.
 */
void
gst_adapter_push (GstAdapter * adapter, GstBuffer * buf)
{
  guint size;

  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (GST_IS_BUFFER (buf));

  size = GST_BUFFER_SIZE (buf);

  adapter->size += size;

  /* Note: merging buffers at this point is premature. */
  if (G_UNLIKELY (adapter->buflist == NULL)) {
    GST_LOG_OBJECT (adapter, "pushing first %u bytes", size);
    adapter->buflist = adapter->buflist_end = g_slist_append (NULL, buf);
  } else {
    /* Otherwise append to the end, and advance our end pointer */
    GST_LOG_OBJECT (adapter, "pushing %u bytes at end, size now %u", size,
        adapter->size);
    adapter->buflist_end = g_slist_append (adapter->buflist_end, buf);
    adapter->buflist_end = g_slist_next (adapter->buflist_end);
  }
}

/* Internal function that copies data into the given buffer, size must be 
 * bigger than the first buffer */
static void
gst_adapter_peek_into (GstAdapter * adapter, guint8 * data, guint size)
{
  GstBuffer *cur;
  GSList *cur_list;
  guint copied, to_copy;

  /* The first buffer might be partly consumed, so need to handle
   * 'skipped' bytes. */
  cur = adapter->buflist->data;
  copied = to_copy = MIN (GST_BUFFER_SIZE (cur) - adapter->skip, size);
  memcpy (data, GST_BUFFER_DATA (cur) + adapter->skip, copied);
  data += copied;

  cur_list = g_slist_next (adapter->buflist);
  while (copied < size) {
    g_assert (cur_list);
    cur = cur_list->data;
    cur_list = g_slist_next (cur_list);
    to_copy = MIN (GST_BUFFER_SIZE (cur), size - copied);
    memcpy (data, GST_BUFFER_DATA (cur), to_copy);
    data += to_copy;
    copied += to_copy;
  }
}

/* Internal method only. Tries to merge buffers at the head of the queue
 * to form a single larger buffer of size 'size'. Only merges buffers that
 * where 'gst_buffer_is_span_fast' returns TRUE.
 *
 * Returns TRUE if it managed to merge anything.
 */
static gboolean
gst_adapter_try_to_merge_up (GstAdapter * adapter, guint size)
{
  GstBuffer *cur, *head;
  GSList *g;

  g = adapter->buflist;
  if (g == NULL)
    return FALSE;

  head = g->data;
  g = g_slist_next (g);

  /* How large do we want our head buffer? The requested size, plus whatever's
   * been skipped already */
  size += adapter->skip;

  while (g != NULL && GST_BUFFER_SIZE (head) < size) {
    cur = g->data;
    if (!gst_buffer_is_span_fast (head, cur))
      return TRUE;

    /* Merge the head buffer and the next in line */
    GST_LOG_OBJECT (adapter,
        "Merging buffers of size %u & %u in search of target %u",
        GST_BUFFER_SIZE (head), GST_BUFFER_SIZE (cur), size);

    head = gst_buffer_join (head, cur);

    /* Delete the front list item, and store our new buffer in the 2nd list 
     * item */
    adapter->buflist = g_slist_delete_link (adapter->buflist, adapter->buflist);
    g->data = head;
  }

  return FALSE;
}

/**
 * gst_adapter_peek:
 * @adapter: a #GstAdapter
 * @size: the number of bytes to peek
 *
 * Gets the first @size bytes stored in the @adapter. The returned pointer is
 * valid until the next function is called on the adapter.
 *
 * Note that setting the returned pointer as the data of a #GstBuffer is
 * incorrect for general-purpose plugins. The reason is that if a downstream
 * element stores the buffer so that it has access to it outside of the bounds
 * of its chain function, the buffer will have an invalid data pointer after
 * your element flushes the bytes. In that case you should use
 * gst_adapter_take(), which returns a freshly-allocated buffer that you can set
 * as #GstBuffer malloc_data or the potentially more performant 
 * gst_adapter_take_buffer().
 *
 * Returns #NULL if @size bytes are not available.
 *
 * Returns: a pointer to the first @size bytes of data, or NULL.
 */
const guint8 *
gst_adapter_peek (GstAdapter * adapter, guint size)
{
  GstBuffer *cur;

  g_return_val_if_fail (GST_IS_ADAPTER (adapter), NULL);
  g_return_val_if_fail (size > 0, NULL);

  /* we don't have enough data, return NULL. This is unlikely
   * as one usually does an _available() first instead of peeking a
   * random size. */
  if (G_UNLIKELY (size > adapter->size))
    return NULL;

  /* we have enough assembled data, return it */
  if (adapter->assembled_len >= size)
    return adapter->assembled_data;

  /* our head buffer has enough data left, return it */
  cur = adapter->buflist->data;
  if (GST_BUFFER_SIZE (cur) >= size + adapter->skip)
    return GST_BUFFER_DATA (cur) + adapter->skip;

  /* We may be able to efficiently merge buffers in our pool to 
   * gather a big enough chunk to return it from the head buffer directly */
  if (gst_adapter_try_to_merge_up (adapter, size)) {
    /* Merged something! Check if there's enough avail now */
    cur = adapter->buflist->data;
    if (GST_BUFFER_SIZE (cur) >= size + adapter->skip)
      return GST_BUFFER_DATA (cur) + adapter->skip;
  }

  /* Gonna need to copy stuff out */
  if (adapter->assembled_size < size) {
    adapter->assembled_size = (size / DEFAULT_SIZE + 1) * DEFAULT_SIZE;
    GST_DEBUG_OBJECT (adapter, "setting size of internal buffer to %u",
        adapter->assembled_size);
    g_free (adapter->assembled_data);
    adapter->assembled_data = g_malloc (adapter->assembled_size);
  }
  adapter->assembled_len = size;

  gst_adapter_peek_into (adapter, adapter->assembled_data, size);

  return adapter->assembled_data;
}

/**
 * gst_adapter_copy:
 * @adapter: a #GstAdapter
 * @dest: the memory where to copy to
 * @offset: the bytes offset in the adapter to start from
 * @size: the number of bytes to copy
 *
 * Copies @size bytes of data starting at @offset out of the buffers
 * contained in @GstAdapter into an array @dest provided by the caller.
 *
 * The array @dest should be large enough to contain @size bytes.
 * The user should check that the adapter has (@offset + @size) bytes
 * available before calling this function.
 *
 * Since: 0.10.12
 */
void
gst_adapter_copy (GstAdapter * adapter, guint8 * dest, guint offset, guint size)
{
  GSList *g;
  int skip;

  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (size > 0);

  /* we don't have enough data, return. This is unlikely
   * as one usually does an _available() first instead of copying a
   * random size. */
  if (G_UNLIKELY (offset + size > adapter->size))
    return;

  skip = adapter->skip;
  for (g = adapter->buflist; g && size > 0; g = g_slist_next (g)) {
    GstBuffer *buf;

    buf = g->data;
    if (offset < GST_BUFFER_SIZE (buf) - skip) {
      int n;

      n = MIN (GST_BUFFER_SIZE (buf) - skip - offset, size);
      memcpy (dest, GST_BUFFER_DATA (buf) + skip + offset, n);

      dest += n;
      offset = 0;
      size -= n;
    } else {
      offset -= GST_BUFFER_SIZE (buf) - skip;
    }
    skip = 0;
  }
}

/**
 * gst_adapter_flush:
 * @adapter: a #GstAdapter
 * @flush: the number of bytes to flush
 *
 * Flushes the first @flush bytes in the @adapter. The caller must ensure that
 * at least this many bytes are available.
 *
 * See also: gst_adapter_peek().
 */
void
gst_adapter_flush (GstAdapter * adapter, guint flush)
{
  GstBuffer *cur;

  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (flush <= adapter->size);

  GST_LOG_OBJECT (adapter, "flushing %u bytes", flush);
  adapter->size -= flush;
  adapter->assembled_len = 0;
  while (flush > 0) {
    cur = adapter->buflist->data;
    if (GST_BUFFER_SIZE (cur) <= flush + adapter->skip) {
      /* can skip whole buffer */
      flush -= GST_BUFFER_SIZE (cur) - adapter->skip;
      adapter->skip = 0;
      adapter->buflist =
          g_slist_delete_link (adapter->buflist, adapter->buflist);
      if (G_UNLIKELY (adapter->buflist == NULL))
        adapter->buflist_end = NULL;
      gst_buffer_unref (cur);
    } else {
      adapter->skip += flush;
      break;
    }
  }
}

/**
 * gst_adapter_take:
 * @adapter: a #GstAdapter
 * @nbytes: the number of bytes to take
 *
 * Returns a freshly allocated buffer containing the first @nbytes bytes of the
 * @adapter. The returned bytes will be flushed from the adapter.
 *
 * Caller owns returned value. g_free after usage.
 *
 * Returns: oven-fresh hot data, or #NULL if @nbytes bytes are not available
 */
guint8 *
gst_adapter_take (GstAdapter * adapter, guint nbytes)
{
  guint8 *data;

  g_return_val_if_fail (GST_IS_ADAPTER (adapter), NULL);
  g_return_val_if_fail (nbytes > 0, NULL);

  /* we don't have enough data, return NULL. This is unlikely
   * as one usually does an _available() first instead of peeking a
   * random size. */
  if (G_UNLIKELY (nbytes > adapter->size))
    return NULL;

  data = g_malloc (nbytes);

  /* we have enough assembled data, copy from there */
  if (adapter->assembled_len >= nbytes) {
    GST_LOG_OBJECT (adapter, "taking %u bytes already assembled", nbytes);
    memcpy (data, adapter->assembled_data, nbytes);
  } else {
    GST_LOG_OBJECT (adapter, "taking %u bytes by collection", nbytes);
    gst_adapter_peek_into (adapter, data, nbytes);
  }

  gst_adapter_flush (adapter, nbytes);

  return data;
}

/**
 * gst_adapter_take_buffer:
 * @adapter: a #GstAdapter
 * @nbytes: the number of bytes to take
 *
 * Returns a #GstBuffer containing the first @nbytes bytes of the
 * @adapter. The returned bytes will be flushed from the adapter.
 * This function is potentially more performant than gst_adapter_take() 
 * since it can reuse the memory in pushed buffers by subbuffering
 * or merging.
 *
 * Caller owns returned value. gst_buffer_unref() after usage.
 *
 * Since: 0.10.6
 *
 * Returns: a #GstBuffer containing the first @nbytes of the adapter, 
 * or #NULL if @nbytes bytes are not available
 */
GstBuffer *
gst_adapter_take_buffer (GstAdapter * adapter, guint nbytes)
{
  GstBuffer *buffer;
  GstBuffer *cur;

  g_return_val_if_fail (GST_IS_ADAPTER (adapter), NULL);
  g_return_val_if_fail (nbytes > 0, NULL);

  GST_LOG_OBJECT (adapter, "taking buffer of %u bytes", nbytes);

  /* we don't have enough data, return NULL. This is unlikely
   * as one usually does an _available() first instead of grabbing a
   * random size. */
  if (G_UNLIKELY (nbytes > adapter->size))
    return NULL;

  /* our head buffer has enough data left, return it */
  cur = adapter->buflist->data;
  if (GST_BUFFER_SIZE (cur) >= nbytes + adapter->skip) {
    GST_LOG_OBJECT (adapter, "providing buffer of %d bytes via sub-buffer",
        nbytes);
    buffer = gst_buffer_create_sub (cur, adapter->skip, nbytes);

    gst_adapter_flush (adapter, nbytes);

    return buffer;
  }

  if (gst_adapter_try_to_merge_up (adapter, nbytes)) {
    /* Merged something, let's try again for sub-buffering */
    cur = adapter->buflist->data;
    if (GST_BUFFER_SIZE (cur) >= nbytes + adapter->skip) {
      GST_LOG_OBJECT (adapter, "providing buffer of %d bytes via sub-buffer",
          nbytes);
      buffer = gst_buffer_create_sub (cur, adapter->skip, nbytes);

      gst_adapter_flush (adapter, nbytes);

      return buffer;
    }
  }

  buffer = gst_buffer_new_and_alloc (nbytes);

  /* we have enough assembled data, copy from there */
  if (adapter->assembled_len >= nbytes) {
    GST_LOG_OBJECT (adapter, "taking %u bytes already assembled", nbytes);
    memcpy (GST_BUFFER_DATA (buffer), adapter->assembled_data, nbytes);
  } else {
    GST_LOG_OBJECT (adapter, "taking %u bytes by collection", nbytes);
    gst_adapter_peek_into (adapter, GST_BUFFER_DATA (buffer), nbytes);
  }

  gst_adapter_flush (adapter, nbytes);

  return buffer;
}

/**
 * gst_adapter_available:
 * @adapter: a #GstAdapter
 *
 * Gets the maximum amount of bytes available, that is it returns the maximum
 * value that can be supplied to gst_adapter_peek() without that function
 * returning NULL.
 *
 * Returns: number of bytes available in @adapter
 */
guint
gst_adapter_available (GstAdapter * adapter)
{
  g_return_val_if_fail (GST_IS_ADAPTER (adapter), 0);

  return adapter->size;
}

/**
 * gst_adapter_available_fast:
 * @adapter: a #GstAdapter
 *
 * Gets the maximum number of bytes that are immediately available without
 * requiring any expensive operations (like copying the data into a 
 * temporary buffer).
 *
 * Returns: number of bytes that are available in @adapter without expensive 
 * operations
 */
guint
gst_adapter_available_fast (GstAdapter * adapter)
{
  GstBuffer *first;
  guint size;

  g_return_val_if_fail (GST_IS_ADAPTER (adapter), 0);

  /* no buffers, we have no data */
  if (!adapter->buflist)
    return 0;

  /* some stuff we already assembled */
  if (adapter->assembled_len)
    return adapter->assembled_len;

  /* take the first buffer and its size */
  first = GST_BUFFER_CAST (adapter->buflist->data);
  size = GST_BUFFER_SIZE (first);

  /* we cannot have skipped more than the first buffer */
  g_assert (size > adapter->skip);

  /* we can quickly get the data of the first buffer */
  return size - adapter->skip;
}
