/* GStreamer
 * Copyright (C) 2001 Erik Walthinsen <omega@temple-baptist.com>
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

#include "adapter.h"
#include <string.h>

/* default size for the assembled data buffer */
#define DEFAULT_SIZE 16

GST_DEBUG_CATEGORY_STATIC (gst_adapter_debug);
#define GST_CAT_DEFAULT gst_adapter_debug

#define _do_init(thing) \
  GST_DEBUG_CATEGORY_INIT (gst_adapter_debug, "GstAdapter", 0, "object to splice and merge buffers to dewsired size")
GST_BOILERPLATE_FULL (GstAdapter, gst_adapter, GObject, G_TYPE_OBJECT, _do_init)

     static void gst_adapter_dispose (GObject * object);
     static void gst_adapter_finalize (GObject * object);

     static void gst_adapter_base_init (gpointer g_class)
{
}

static void
gst_adapter_class_init (GstAdapterClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);

  object->dispose = gst_adapter_dispose;
  object->finalize = gst_adapter_finalize;
}

static void
gst_adapter_init (GstAdapter * adapter)
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
 * Creates a new #GstAdapter.
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
 * @adapter: the #GstAdapter to clear
 *
 * Removes all buffers from the @adapter.
 */
void
gst_adapter_clear (GstAdapter * adapter)
{
  g_return_if_fail (GST_IS_ADAPTER (adapter));

  g_slist_foreach (adapter->buflist, (GFunc) gst_data_unref, NULL);
  g_slist_free (adapter->buflist);
  adapter->buflist = NULL;
  adapter->size = 0;
  adapter->skip = 0;
  adapter->assembled_len = 0;
}

/**
 * gst_adapter_push:
 * @adapter: a #GstAdapter
 * @buf: the #GstBuffer to queue into the adapter
 *
 * Adds the data from @buf to the data stored inside @adapter and takes 
 * ownership of the buffer.
 */
void
gst_adapter_push (GstAdapter * adapter, GstBuffer * buf)
{
  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (GST_IS_BUFFER (buf));

  adapter->size += GST_BUFFER_SIZE (buf);
  adapter->buflist = g_slist_append (adapter->buflist, buf);
}

/**
 * gst_adapter_peek:
 * @adapter: a #GstAdapter
 * @size: number of bytes to peek
 *
 * Gets the first @size bytes stored in the @adapter. If this many bytes are
 * not available, it returns NULL. The returned pointer is valid until the next
 * function is called on the adapter.
 *
 * Returns: a pointer to the first @size bytes of data or NULL
 */
const guint8 *
gst_adapter_peek (GstAdapter * adapter, guint size)
{
  GstBuffer *cur;
  GSList *cur_list;
  guint copied;

  g_return_val_if_fail (GST_IS_ADAPTER (adapter), NULL);
  g_return_val_if_fail (size > 0, NULL);

  if (size > adapter->size)
    return NULL;

  if (adapter->assembled_len >= size)
    return adapter->assembled_data;

  cur = adapter->buflist->data;
  if (GST_BUFFER_SIZE (cur) >= size + adapter->skip)
    return GST_BUFFER_DATA (cur) + adapter->skip;

  if (adapter->assembled_size < size) {
    adapter->assembled_size = (size / DEFAULT_SIZE + 1) * DEFAULT_SIZE;
    GST_DEBUG_OBJECT (adapter, "setting size of internal buffer to %u\n",
        adapter->assembled_size);
    adapter->assembled_data =
        g_realloc (adapter->assembled_data, adapter->assembled_size);
  }
  adapter->assembled_len = size;
  copied = GST_BUFFER_SIZE (cur) - adapter->skip;
  memcpy (adapter->assembled_data, GST_BUFFER_DATA (cur) + adapter->skip,
      copied);
  cur_list = g_slist_next (adapter->buflist);
  while (copied < size) {
    g_assert (cur_list);
    cur = cur_list->data;
    cur_list = g_slist_next (cur_list);
    memcpy (adapter->assembled_data + copied, GST_BUFFER_DATA (cur),
        MIN (GST_BUFFER_SIZE (cur), size - copied));
    copied = MIN (size, copied + GST_BUFFER_SIZE (cur));
  }

  return adapter->assembled_data;
}

/**
 * gst_adapter_flush:
 * @adapter: a #GstAdapter
 * @flush: number of bytes to flush
 *
 * Flushes the first @flush bytes of the @adapter.
 */
void
gst_adapter_flush (GstAdapter * adapter, guint flush)
{
  GstBuffer *cur;

  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (flush > 0);
  g_return_if_fail (flush <= adapter->size);

  GST_LOG_OBJECT (adapter, "flushing %u bytes\n", flush);
  adapter->size -= flush;
  adapter->assembled_len = 0;
  while (flush > 0) {
    cur = adapter->buflist->data;
    if (GST_BUFFER_SIZE (cur) <= flush + adapter->skip) {
      /* can skip whole buffer */
      flush -= GST_BUFFER_SIZE (cur) - adapter->skip;
      adapter->skip = 0;
      adapter->buflist = g_slist_remove (adapter->buflist, cur);
      gst_data_unref (GST_DATA (cur));
    } else {
      adapter->skip += flush;
      break;
    }
  }
}

/**
 * gst_adapter_available:
 * @adapter: a #GstAdapter
 *
 * Gets the maximum amount of bytes available, that is it returns the maximum 
 * value that can be supplied to gst_adapter_peek() without that function 
 * returning NULL.
 *
 * Returns: amount of bytes available in @adapter
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
 * Gets the maximum amount of bytes available without the need to do expensive
 * operations (like copying the data into a temporary buffer).
 *
 * Returns: amount of bytes available in @adapter without expensive operations
 */
guint
gst_adapter_available_fast (GstAdapter * adapter)
{
  g_return_val_if_fail (GST_IS_ADAPTER (adapter), 0);

  if (!adapter->buflist)
    return 0;
  if (adapter->assembled_len)
    return adapter->assembled_len;
  g_assert (GST_BUFFER_SIZE (adapter->buflist->data) > adapter->skip);
  return GST_BUFFER_SIZE (adapter->buflist->data) - adapter->skip;
}
