/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstbufferstore.c: keep an easily accessible list of all buffers
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "gstbufferstore.h"
#include <gst/gstutils.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_buffer_store_debug);
#define GST_CAT_DEFAULT gst_buffer_store_debug

enum {
  CLEARED,
  BUFFER_ADDED,
  LAST_SIGNAL
};
enum {
  ARG_0
};


static void	gst_buffer_store_dispose	(GObject *		object);

static gboolean	gst_buffer_store_add_buffer_func (GstBufferStore *	store, 
						 GstBuffer *		buffer);
static void	gst_buffer_store_cleared_func	(GstBufferStore *	store);
  
static guint gst_buffer_store_signals[LAST_SIGNAL] = { 0 };

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_buffer_store_debug, "GstBufferStore", 0, "buffer store helper");

GST_BOILERPLATE_FULL (GstBufferStore, gst_buffer_store, GObject, G_TYPE_OBJECT, _do_init);


G_GNUC_UNUSED static void
debug_buffers (GstBufferStore *store)
{
  GList *walk = store->buffers;
  
  g_printerr ("BUFFERS in store:\n");
  while (walk) {
    g_print ("%15"G_GUINT64_FORMAT" - %7u\n", GST_BUFFER_OFFSET (walk->data), GST_BUFFER_SIZE (walk->data));
    walk = g_list_next (walk);
  }
  g_printerr ("\n");
}
static gboolean
continue_accu (GSignalInvocationHint *ihint, GValue *return_accu, 
	       const GValue *handler_return, gpointer data)
{
  gboolean do_continue = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, do_continue);

  return do_continue;
}
static void
gst_buffer_store_base_init (gpointer g_class)
{
}
static void
gst_buffer_store_class_init (GstBufferStoreClass *store_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (store_class);

  gobject_class->dispose = gst_buffer_store_dispose;
  
  gst_buffer_store_signals[CLEARED] = g_signal_new ("cleared", 
	  G_TYPE_FROM_CLASS (store_class), G_SIGNAL_RUN_LAST,
          G_STRUCT_OFFSET (GstBufferStoreClass, cleared), NULL, NULL,
          gst_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_buffer_store_signals[BUFFER_ADDED] = g_signal_new ("buffer-added", 
	  G_TYPE_FROM_CLASS (store_class), G_SIGNAL_RUN_LAST,
          G_STRUCT_OFFSET (GstBufferStoreClass, buffer_added), continue_accu, NULL,
          gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1, GST_TYPE_BUFFER);

  store_class->cleared = gst_buffer_store_cleared_func;
  store_class->buffer_added = gst_buffer_store_add_buffer_func;
}
static void
gst_buffer_store_init (GstBufferStore *store)
{
  store->buffers = NULL;
}
static void
gst_buffer_store_dispose (GObject *object)
{
  GstBufferStore *store = GST_BUFFER_STORE (object);

  gst_buffer_store_clear (store);

  parent_class->dispose (object);
}
static gboolean
gst_buffer_store_add_buffer_func (GstBufferStore *store, GstBuffer *buffer)
{
  g_assert (buffer != NULL);
  
  if (!GST_BUFFER_OFFSET_IS_VALID (buffer) &&
      store->buffers &&
      GST_BUFFER_OFFSET_IS_VALID (store->buffers->data)) {
    /* we assumed valid offsets, but suddenly they are not anymore */
    GST_DEBUG_OBJECT (store, "attempting to add buffer %p with invalid offset to store with valid offset, abort",
	    buffer);
    return FALSE;
  } else if (!store->buffers || !GST_BUFFER_OFFSET_IS_VALID (store->buffers->data)) {
    /* the starting buffer had an invalid offset, in that case we assume continuous buffers */
    GST_LOG_OBJECT (store, "adding buffer %p with invalid offset and size %u",
	    buffer, GST_BUFFER_SIZE (buffer));
    gst_data_ref (GST_DATA (buffer));
    store->buffers = g_list_append (store->buffers, buffer);
    return TRUE;
  } else {
    /* both list and buffer have valid offsets, we can really go wild */
    GList *walk, *current_list = NULL;
    GstBuffer *current;
    
    g_assert (GST_BUFFER_OFFSET_IS_VALID (buffer));
    GST_LOG_OBJECT (store, "attempting to add buffer %p with offset %"G_GUINT64_FORMAT" and size %u",
	    buffer, GST_BUFFER_OFFSET (buffer), GST_BUFFER_SIZE (buffer));
    /* we keep a sorted list of non-overlapping buffers */
    walk = store->buffers;
    while (walk) {
      current = GST_BUFFER (walk->data);
      current_list = walk;
      walk = g_list_next (walk);
      if (GST_BUFFER_OFFSET (current) < GST_BUFFER_OFFSET (buffer)) {
	continue;
      } else if (GST_BUFFER_OFFSET (current) == GST_BUFFER_OFFSET (buffer)) {
	guint needed_size;
	if (walk) {
	  needed_size = MIN (GST_BUFFER_SIZE (buffer), 
		  GST_BUFFER_OFFSET (walk->data) - GST_BUFFER_OFFSET (current));
	} else {
	  needed_size = GST_BUFFER_SIZE (buffer);
	}
	if (needed_size <= GST_BUFFER_SIZE (current)) {
	  buffer = NULL;
	  break;
	} else {
	  if (needed_size < GST_BUFFER_SIZE (buffer)) {
	    /* need to create subbuffer to not have overlapping data */
	    GstBuffer *sub = gst_buffer_create_sub (buffer, 0, needed_size);
	    g_assert (sub);
	    buffer = sub;
	  } else {
	    gst_data_ref (GST_DATA (buffer));
	  }
	  /* replace current buffer with new one */
	  GST_INFO_OBJECT (store, "replacing buffer %p with buffer %p with offset %"G_GINT64_FORMAT" and size %u", 
			   current_list->data, buffer, GST_BUFFER_OFFSET (buffer), GST_BUFFER_SIZE (buffer));
	  gst_data_unref (GST_DATA (current_list->data));
	  current_list->data = buffer;
	  buffer = NULL;
	  break;
	}
      } else if (GST_BUFFER_OFFSET (current) > GST_BUFFER_OFFSET (buffer)) {
	GList *previous = g_list_previous (current_list);
	guint64 start_offset = previous ? 
		GST_BUFFER_OFFSET (previous->data) + GST_BUFFER_SIZE (previous->data) : 0;

	if (start_offset == GST_BUFFER_OFFSET (current)) {
	  buffer = NULL;
	  break;
	} else {
	  /* we have data to insert */
	  if (start_offset > GST_BUFFER_OFFSET (buffer) ||
	      GST_BUFFER_OFFSET (buffer) + GST_BUFFER_SIZE (buffer) > GST_BUFFER_OFFSET (current)) {
	    GstBuffer *sub;

	    /* need a subbuffer */
	    start_offset = GST_BUFFER_OFFSET (buffer) > start_offset ? 0 : 
			   start_offset - GST_BUFFER_OFFSET (buffer);
	    sub = gst_buffer_create_sub (buffer, start_offset,
		    MIN (GST_BUFFER_SIZE (buffer), GST_BUFFER_OFFSET (current) - start_offset - GST_BUFFER_OFFSET (buffer)));
	    g_assert (sub);
	    GST_BUFFER_OFFSET (sub) = start_offset + GST_BUFFER_OFFSET (buffer);
	    buffer = sub;
	  } else {
	    gst_data_ref (GST_DATA (buffer));
	  }
	  GST_INFO_OBJECT (store, "adding buffer %p with offset %"G_GINT64_FORMAT" and size %u", 
			   buffer, GST_BUFFER_OFFSET (buffer), GST_BUFFER_SIZE (buffer));
	  store->buffers = g_list_insert_before (store->buffers, current_list, buffer);
	  buffer = NULL;
	  break;
	}
      }
    }
    if (buffer) {
      gst_data_ref (GST_DATA (buffer));
      GST_INFO_OBJECT (store, "adding buffer %p with offset %"G_GINT64_FORMAT" and size %u", 
		       buffer, GST_BUFFER_OFFSET (buffer), GST_BUFFER_SIZE (buffer));
      if (current_list) {
	g_list_append (current_list, buffer);
      } else {
	g_assert (store->buffers == NULL);
	store->buffers = g_list_prepend (NULL, buffer);
      }
    }
    return TRUE;
  }
}
static void
gst_buffer_store_cleared_func (GstBufferStore *store)
{
  g_list_foreach (store->buffers, (GFunc) gst_data_unref, NULL);
  g_list_free (store->buffers);
  store->buffers = NULL;
}
/**
 * gst_buffer_store_new:
 *
 * Creates a new bufferstore.
 *
 * Returns: the new bufferstore.
 */
GstBufferStore *
gst_buffer_store_new (void)
{
  return GST_BUFFER_STORE (g_object_new (GST_TYPE_BUFFER_STORE, NULL));
}
/**
 * gst_buffer_store_clear:
 * @store: a bufferstore
 *
 * Clears the buffer store. All buffers are removed and the buffer store
 * behaves like it was just created.
 */
/* FIXME: call this function _reset ? */
void
gst_buffer_store_clear (GstBufferStore *store)
{
  g_return_if_fail (GST_IS_BUFFER_STORE (store));
  
  g_signal_emit (store, gst_buffer_store_signals [CLEARED], 0, NULL);
}
/**
 * gst_buffer_store_add_buffer:
 * @store: a bufferstore
 * @buffer: the buffer to add
 *
 * Adds a buffer to the buffer store. 
 *
 * Returns: TRUE, if the buffer was added, FALSE if an error occured.
 */
gboolean
gst_buffer_store_add_buffer (GstBufferStore *store, GstBuffer *buffer)
{
  gboolean ret;
  
  g_return_val_if_fail (GST_IS_BUFFER_STORE (store), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  if (store->buffers &&
      GST_BUFFER_OFFSET_IS_VALID (store->buffers->data) &&
      !GST_BUFFER_OFFSET_IS_VALID (buffer))
    return FALSE;
  
  g_signal_emit (store, gst_buffer_store_signals [BUFFER_ADDED], 0, buffer, &ret);
  
  return ret;
}
/**
 * gst_buffer_store_get_buffer:
 * @store: a bufferstore
 * @offset: starting offset of returned buffer
 * @size: size of returned buffer
 *
 * Returns a buffer that corresponds to the given area of data. If part of the
 * data is not available inside the store, NULL is returned. You have to unref
 * the buffer after use.
 *
 * Returns: a buffer with the requested data or NULL if the data was not 
 *          available.
 */
GstBuffer *
gst_buffer_store_get_buffer (GstBufferStore *store, guint64 offset, guint size)
{
  GstBuffer *current;
  GList *walk;
  guint8 *data;
  guint tmp;
  gboolean have_offset;
  guint64 cur_offset = 0;
  GstBuffer *ret = NULL;

  g_return_val_if_fail (GST_IS_BUFFER_STORE (store), NULL);

  walk = store->buffers;
  if (!walk)
    return NULL;
  if (GST_BUFFER_OFFSET_IS_VALID (walk->data)) {
    have_offset = TRUE;
  } else {
    have_offset = FALSE;
  }
  while (walk) {
    current = GST_BUFFER (walk->data);
    if (have_offset) {
      cur_offset = GST_BUFFER_OFFSET (current);
    }
    walk = g_list_next (walk);
    if (cur_offset > offset) {
      /* #include <windows.h>
         do_nothing_loop (); */
    } else if (cur_offset == offset &&
	GST_BUFFER_SIZE (current) == size) {
      GST_LOG_OBJECT (store, "found matching buffer %p for offset %"G_GUINT64_FORMAT" and size %u",
		      current, offset, size);
      ret = current;
      gst_data_ref (GST_DATA (ret));
      GST_LOG_OBJECT (store, "refcount %d",
		      GST_DATA_REFCOUNT_VALUE(ret));
      break;
    } else if (cur_offset + GST_BUFFER_SIZE (current) > offset) {
      if (cur_offset + GST_BUFFER_SIZE (current) >= offset + size) {
	ret = gst_buffer_create_sub (current, offset - cur_offset, size);
	GST_LOG_OBJECT (store, "created subbuffer %p from buffer %p for offset %llu and size %u",
			ret, current,  offset, size);
	break;
      }
      /* uh, the requested data spans some buffers */
      ret = gst_buffer_new_and_alloc (size);
      GST_LOG_OBJECT (store, "created buffer %p for offset %"G_GUINT64_FORMAT
		      " and size %u, will fill with data now",
		      ret, offset, size);
      data = GST_BUFFER_DATA (ret);
      tmp = GST_BUFFER_SIZE (current) - offset + cur_offset;
      memcpy (data, GST_BUFFER_DATA (current) + offset - cur_offset, tmp);
      data += tmp;
      size -= tmp;
      while (size) {
	if (walk == NULL || 
	    (have_offset && 
	     GST_BUFFER_OFFSET (current) + GST_BUFFER_SIZE (current) != GST_BUFFER_OFFSET (walk->data))) {
	  GST_DEBUG_OBJECT (store, "not all data for offset %"G_GUINT64_FORMAT" and remaining size %u available, aborting",
			    offset, size);
	  gst_data_unref (GST_DATA (ret));
	  ret = NULL;
	  goto out;
	}
	current = GST_BUFFER (walk->data);
	walk = g_list_next (walk);
	tmp = MIN (GST_BUFFER_SIZE (current), size);
	memcpy (data, GST_BUFFER_DATA (current), tmp);
	data += tmp;
	size -= tmp;
      }
    }
    if (!have_offset) {
      cur_offset += GST_BUFFER_SIZE (current);
    }
  }
out:
  
  return ret;
}
/**
 * gst_buffer_store_get_size:
 * @store: a bufferstore
 * @offset: desired offset
 *
 * Calculates the number of bytes available starting from offset. This allows
 * to query a buffer with the returned size.
 *
 * Returns: the number of continuous bytes in the bufferstore starting at
 *          offset.
 */
guint
gst_buffer_store_get_size (GstBufferStore *store, guint64 offset)
{
  GList *walk;
  gboolean have_offset;
  gboolean counting = FALSE;
  guint64 cur_offset = 0;
  GstBuffer *current = NULL;
  guint ret = 0;

  g_return_val_if_fail (GST_IS_BUFFER_STORE (store), 0);

  walk = store->buffers;
  if (!walk)
    return 0;
  if (GST_BUFFER_OFFSET_IS_VALID (walk->data)) {
    have_offset = TRUE;
  } else {
    have_offset = FALSE;
  }
  while (walk) {
    if (have_offset && counting && 
	cur_offset + GST_BUFFER_SIZE (current) !=  GST_BUFFER_OFFSET (walk->data)) {
      break;
    }
    current = GST_BUFFER (walk->data);
    if (have_offset) {
      cur_offset = GST_BUFFER_OFFSET (current);
    }
    walk = g_list_next (walk);
    if (counting) {
      ret += GST_BUFFER_SIZE (current);
    } else {
      if (cur_offset > offset)
	return 0;
      if (cur_offset + GST_BUFFER_SIZE (current) > offset) {
	/* we have at least some bytes */
	ret = cur_offset + GST_BUFFER_SIZE (current) - offset;
	counting = TRUE;
      }
    }
    if (!have_offset) {
      cur_offset += GST_BUFFER_SIZE (current);
    }
  }
  
  return ret;
}
