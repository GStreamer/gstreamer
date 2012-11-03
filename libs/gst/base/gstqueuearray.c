/* GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
 *
 * gstqueuearray.c:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gstqueuearray
 * @short_description: Array based queue object
 *
 * #GstQueueArray is an object that provides standard queue functionality
 * based on an array instead of linked lists. This reduces the overhead
 * caused by memory managment by a large factor.
 */


#include <string.h>
#include <gst/gst.h>
#include "gstqueuearray.h"

struct _GstQueueArray
{
  /* < private > */
  gpointer *array;
  guint size;
  guint head;
  guint tail;
  guint length;
};

/**
 * gst_queue_array_new:
 * @initial_size: Initial size of the new queue
 *
 * Allocates a new #GstQueueArray object with an initial
 * queue size of @initial_size.
 *
 * Returns: a new #GstQueueArray object
 *
 * Since: 1.2.0
 */
GstQueueArray *
gst_queue_array_new (guint initial_size)
{
  GstQueueArray *array;

  array = g_slice_new (GstQueueArray);
  array->size = initial_size;
  array->array = g_new0 (gpointer, initial_size);
  array->head = 0;
  array->tail = 0;
  array->length = 0;
  return array;
}


/**
 * gst_queue_array_free:
 * @array: a #GstQueueArray object
 *
 * Frees queue @array and all memory associated to it.
 *
 * Since: 1.2.0
 */
void
gst_queue_array_free (GstQueueArray * array)
{
  g_free (array->array);
  g_slice_free (GstQueueArray, array);
}

/**
 * gst_queue_array_pop_head:
 * @array: a #GstQueueArray object
 *
 * Returns and head of the queue @array and removes
 * it from the queue.
 *
 * Returns: The head of the queue
 *
 * Since: 1.2.0
 */
gpointer
gst_queue_array_pop_head (GstQueueArray * array)
{
  gpointer ret;

  /* empty array */
  if (G_UNLIKELY (array->length == 0))
    return NULL;
  ret = array->array[array->head];
  array->head++;
  array->head %= array->size;
  array->length--;
  return ret;
}

/**
 * gst_queue_array_pop_head:
 * @array: a #GstQueueArray object
 *
 * Returns and head of the queue @array and does not
 * remove it from the queue.
 *
 * Returns: The head of the queue
 *
 * Since: 1.2.0
 */
gpointer
gst_queue_array_peek_head (GstQueueArray * array)
{
  /* empty array */
  if (G_UNLIKELY (array->length == 0))
    return NULL;
  return array->array[array->head];
}

/**
 * gst_queue_array_push_tail:
 * @array: a #GstQueueArray object
 * @data: object to push
 *
 * Pushes @data to the tail of the queue @array.
 *
 * Since: 1.2.0
 */
void
gst_queue_array_push_tail (GstQueueArray * array, gpointer data)
{
  /* Check if we need to make room */
  if (G_UNLIKELY (array->length == array->size)) {
    /* newsize is 50% bigger */
    guint newsize = (3 * array->size) / 2;

    /* copy over data */
    if (array->tail != 0) {
      gpointer *array2 = g_new0 (gpointer, newsize);
      guint t1 = array->head;
      guint t2 = array->size - array->head;

      /* [0-----TAIL][HEAD------SIZE]
       *
       * We want to end up with
       * [HEAD------------------TAIL][----FREEDATA------NEWSIZE]
       *
       * 1) move [HEAD-----SIZE] part to beginning of new array
       * 2) move [0-------TAIL] part new array, after previous part
       */

      memcpy (array2, &array->array[array->head], t2 * sizeof (gpointer));
      memcpy (&array2[t2], array->array, t1 * sizeof (gpointer));

      g_free (array->array);
      array->array = array2;
      array->head = 0;
    } else {
      /* Fast path, we just need to grow the array */
      array->array = g_renew (gpointer, array->array, newsize);
    }
    array->tail = array->size;
    array->size = newsize;
  }

  array->array[array->tail] = data;
  array->tail++;
  array->tail %= array->size;
  array->length++;
}

/**
 * gst_queue_array_is_empty:
 * @array: a #GstQueueArray object
 *
 * Checks if the queue @array is empty.
 *
 * Returns: %TRUE if the queue @array is empty
 *
 * Since: 1.2.0
 */
gboolean
gst_queue_array_is_empty (GstQueueArray * array)
{
  return (array->length == 0);
}

/**
 * gst_queue_array_drop_element:
 * @array: a #GstQueueArray object
 * @idx: index to drop
 *
 * Drops the queue element at position @idx from queue @array.
 *
 * Returns: the dropped element
 *
 * Since: 1.2.0
 */
gpointer
gst_queue_array_drop_element (GstQueueArray * array, guint idx)
{
  gpointer element;

  if (idx == array->head) {
    /* just move the head */
    element = array->array[idx];
    array->head++;
    array->head %= array->size;
    return element;
  }
  if (idx == array->tail - 1) {
    /* just move the tail */
    element = array->array[idx];
    array->tail = (array->tail - 1 + array->size) % array->size;
    return element;
  }
  /* drop the element #idx... and readjust the array */
  if (array->head < array->tail) {
    /* Make sure it's within the boundaries */
    g_assert (array->head < idx && idx <= array->tail);
    element = array->array[idx];
    /* ends not wrapped */
    /* move head-idx to head+1 */
    memcpy (&array->array[array->head + 1],
        &array->array[array->head], (idx - array->head) * sizeof (gpointer));
    array->tail--;
  } else {
    /* ends are wrapped */
    if (idx < array->tail) {
      element = array->array[idx];
      /* move idx-tail backwards one */
      memcpy (&array->array[idx - 1],
          &array->array[idx], (array->tail - idx) * sizeof (gpointer));
      array->tail--;
    } else if (idx >= array->head) {
      element = array->array[idx];
      /* move head-idx forwards one */
      memcpy (&array->array[array->head],
          &array->array[array->head + 1],
          (idx - array->head) * sizeof (gpointer));
      array->head++;
    } else {
      g_assert_not_reached ();
      element = NULL;
    }
  }
  return element;
}

/**
 * gst_queue_array_find:
 * @array: a #GstQueueArray object
 * @func: comparison function
 * @data: data for comparison function
 *
 * Finds an element in the queue @array by comparing every element
 * with @func and returning the index of the found element.
 *
 * Returns: Index of the found element or -1 if nothing was found.
 *
 * Since: 1.2.0
 */
guint
gst_queue_array_find (GstQueueArray * array, GCompareFunc func, gpointer data)
{
  guint i;

  /* Scan from head to tail */
  for (i = array->head; i < array->length; i = (i + 1) % array->size)
    if (func (array->array[i], data) == 0)
      return i;
  return -1;
}

/**
 * gst_queue_array_get_length:
 * @array: a #GstQueueArray object
 *
 * Returns the length of the queue @array
 *
 * Returns: the length of the queue @array.
 *
 * Since: 1.2.0
 */
guint
gst_queue_array_get_length (GstQueueArray * array)
{
  return array->length;
}
