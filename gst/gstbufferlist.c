/* GStreamer
 * Copyright (C) 2009 Axis Communications <dev-gstreamer at axis dot com>
 * @author Jonas Holmberg <jonas dot holmberg at axis dot com>
 *
 * gstbufferlist.c: Buffer list
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
 * SECTION:gstbufferlist
 * @short_description: Lists of buffers for data-passing
 * @see_also: #GstPad, #GstMiniObject
 *
 * Buffer lists are an object containing a list of buffers.
 *
 * Buffer lists are created with gst_buffer_list_new() and filled with data
 * using a gst_buffer_list_take().
 *
 */
#include "gst_private.h"

#include "gstbuffer.h"
#include "gstbufferlist.h"

#define GST_CAT_DEFAULT GST_CAT_BUFFER_LIST

/**
 * GstBufferList:
 * @mini_object: the parent structure
 *
 * Opaque list of grouped buffers.
 *
 * Since: 0.10.24
 */
struct _GstBufferList
{
  GstMiniObject mini_object;

  GArray *array;
};

GType _gst_buffer_list_type = 0;

void
_gst_buffer_list_initialize (void)
{
  if (G_LIKELY (_gst_buffer_list_type == 0)) {
    _gst_buffer_list_type = gst_mini_object_register ("GstBufferList");
  }
}

static GstBufferList *
_gst_buffer_list_copy (GstBufferList * list)
{
  GstBufferList *copy;
  guint i, len;

  len = list->array->len;
  copy = gst_buffer_list_sized_new (len);

  /* add and ref all buffers in the array */
  for (i = 0; i < len; i++) {
    GstBuffer *buf = g_array_index (list->array, GstBuffer *, i);
    buf = gst_buffer_ref (buf);
    g_array_append_val (copy->array, buf);
  }
  return copy;
}

static void
_gst_buffer_list_free (GstBufferList * list)
{
  guint i, len;
  GST_LOG ("free %p", list);

  /* unrefs all buffers too */
  len = list->array->len;
  for (i = 0; i < len; i++)
    gst_buffer_unref (g_array_index (list->array, GstBuffer *, i));
  g_array_free (list->array, TRUE);

  g_slice_free1 (GST_MINI_OBJECT_SIZE (list), list);
}

static void
gst_buffer_list_init (GstBufferList * list, gsize size, guint asize)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (list), _gst_buffer_list_type,
      size);

  list->mini_object.copy = (GstMiniObjectCopyFunction) _gst_buffer_list_copy;
  list->mini_object.free = (GstMiniObjectFreeFunction) _gst_buffer_list_free;

  list->array = g_array_sized_new (FALSE, FALSE, sizeof (GstBuffer *), asize);

  GST_LOG ("init %p", list);
}

/**
 * gst_buffer_list_sized_new:
 * @size: an initial reserved size
 *
 * Creates a new, empty #GstBufferList. The caller is responsible for unreffing
 * the returned #GstBufferList. The list will have @size space preallocated so
 * that memory reallocations can be avoided.
 *
 * Free-function: gst_buffer_list_unref
 *
 * Returns: (transfer full): the new #GstBufferList. gst_buffer_list_unref()
 *     after usage.
 *
 * Since: 0.10.24
 */
GstBufferList *
gst_buffer_list_sized_new (guint size)
{
  GstBufferList *list;

  list = g_slice_new0 (GstBufferList);

  GST_LOG ("new %p", list);

  gst_buffer_list_init (list, sizeof (GstBufferList), size);

  return list;
}

/**
 * gst_buffer_list_new:
 *
 * Creates a new, empty #GstBufferList. The caller is responsible for unreffing
 * the returned #GstBufferList.
 *
 * Free-function: gst_buffer_list_unref
 *
 * Returns: (transfer full): the new #GstBufferList. gst_buffer_list_unref()
 *     after usage.
 *
 * Since: 0.10.24
 */
GstBufferList *
gst_buffer_list_new (void)
{
  return gst_buffer_list_sized_new (8);
}

/**
 * gst_buffer_list_len:
 * @list: a #GstBufferList
 *
 * Returns the number of buffers in @list.
 *
 * Returns: the number of buffers in the buffer list
 *
 * Since: 0.10.24
 */
guint
gst_buffer_list_len (GstBufferList * list)
{
  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), 0);

  return list->array->len;
}

/**
 * gst_buffer_list_foreach:
 * @list: a #GstBufferList
 * @func: (scope call): a #GstBufferListFunc to call
 * @user_data: (closure): user data passed to @func
 *
 * Call @func with @data for each buffer in @list.
 *
 * @func can modify the passed buffer pointer or its contents. The return value
 * of @func define if this function returns or if the remaining buffers in a
 * group should be skipped.
 *
 * Since: 0.10.24
 */
void
gst_buffer_list_foreach (GstBufferList * list, GstBufferListFunc func,
    gpointer user_data)
{
  guint i, len;

  g_return_if_fail (GST_IS_BUFFER_LIST (list));
  g_return_if_fail (func != NULL);

  len = list->array->len;
  for (i = 0; i < len;) {
    GstBuffer *buf, *buf_ret;
    gboolean ret;

    buf = buf_ret = g_array_index (list->array, GstBuffer *, i);
    ret = func (&buf_ret, i, user_data);

    /* Check if the function changed the buffer */
    if (buf != buf_ret) {
      if (buf_ret == NULL) {
        g_array_remove_index (list->array, i);
      } else {
        g_array_index (list->array, GstBuffer *, i) = buf_ret;
      }
    }

    if (!ret)
      break;

    /* If the buffer was not removed by func go to the next buffer */
    if (buf_ret != NULL)
      i++;
  }
}

/**
 * gst_buffer_list_get:
 * @list: a #GstBufferList
 * @idx: the index
 *
 * Get the buffer at @idx.
 *
 * Returns: (transfer none): the buffer at @idx in @group or NULL when there
 *     is no buffer. The buffer remains valid as long as @list is valid.
 *
 * Since: 0.10.24
 */
GstBuffer *
gst_buffer_list_get (GstBufferList * list, guint idx)
{
  GstBuffer *buf;

  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), NULL);
  g_return_val_if_fail (idx < list->array->len, NULL);

  buf = g_array_index (list->array, GstBuffer *, idx);

  return buf;
}

/**
 * gst_buffer_list_insert:
 * @list: a #GstBufferList
 * @idx: the index
 * @buffer: a #GstBuffer
 *
 * Insert @buffer at @idx in @list. Other buffers are moved to make room for
 * this new buffer.
 *
 * A -1 value for @idx will append the buffer at the end.
 */
void
gst_buffer_list_insert (GstBufferList * list, guint idx, GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_BUFFER_LIST (list));
  g_return_if_fail (buffer != NULL);

  if (idx == -1)
    g_array_append_val (list->array, buffer);
  else {
    g_return_if_fail (idx < list->array->len);
    g_array_insert_val (list->array, idx, buffer);
  }
}

void
gst_buffer_list_remove (GstBufferList * list, guint idx, guint length)
{
  g_return_if_fail (GST_IS_BUFFER_LIST (list));
  g_return_if_fail (idx < list->array->len);

  g_array_remove_range (list->array, idx, length);
}
