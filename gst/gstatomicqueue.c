/* GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
 *               2011 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstatomicqueue.c:
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

#include <string.h>
#include <gst/gst.h>
#include "gstatomicqueue.h"

/* By default the queue uses 2 * sizeof(gpointer) * clp2 (max_items) of
 * memory. clp2(x) is the next power of two >= than x.
 *
 * The queue can operate in low memory mode, in which it consumes almost
 * half the memory at the expense of extra overhead in the readers. This
 * is disabled by default because even without LOW_MEM mode, the memory
 * consumption is still lower than a plain GList.
 */
#undef LOW_MEM

typedef struct _GstAQueueMem GstAQueueMem;

struct _GstAQueueMem
{
  gint size;
  gpointer *array;
  volatile gint head;
  volatile gint tail;
  GstAQueueMem *next;
  GstAQueueMem *free;
};

static guint
clp2 (guint n)
{
  guint res = 1;

  while (res < n)
    res <<= 1;

  return res;
}

static GstAQueueMem *
new_queue_mem (guint size, gint pos)
{
  GstAQueueMem *mem;

  mem = g_new (GstAQueueMem, 1);

  /* we keep the size as a mask for performance */
  mem->size = clp2 (MAX (size, 16)) - 1;
  mem->array = g_new0 (gpointer, mem->size);
  mem->head = pos;
  mem->tail = pos;
  mem->next = NULL;
  mem->free = NULL;

  return mem;
}

static void
free_queue_mem (GstAQueueMem * mem)
{
  g_free (mem->array);
  g_free (mem);
}

struct _GstAtomicQueue
{
#ifdef LOW_MEM
  gint num_readers;
#endif
  GstAQueueMem *head_mem;
  GstAQueueMem *tail_mem;
  GstAQueueMem *free_list;
};

static void
add_to_free_list (GstAtomicQueue * queue, GstAQueueMem * mem)
{
  do {
    mem->free = g_atomic_pointer_get (&queue->free_list);
  } while (!g_atomic_pointer_compare_and_exchange ((gpointer *) &
          queue->free_list, mem->free, mem));
}

static void
clear_free_list (GstAtomicQueue * queue)
{
  GstAQueueMem *free_list;

  /* take the free list and replace with NULL */
  do {
    free_list = g_atomic_pointer_get (&queue->free_list);
    if (free_list == NULL)
      return;
  } while (!g_atomic_pointer_compare_and_exchange ((gpointer *) &
          queue->free_list, free_list, NULL));

  while (free_list) {
    GstAQueueMem *next = free_list->free;

    free_queue_mem (free_list);

    free_list = next;
  }
}

GstAtomicQueue *
gst_atomic_queue_new (guint initial_size)
{
  GstAtomicQueue *queue;

  queue = g_new (GstAtomicQueue, 1);

#ifdef LOW_MEM
  queue->num_readers = 0;
#endif
  queue->head_mem = queue->tail_mem = new_queue_mem (initial_size, 0);
  queue->free_list = NULL;

  return queue;
}

void
gst_atomic_queue_free (GstAtomicQueue * queue)
{
  free_queue_mem (queue->head_mem);
  if (queue->head_mem != queue->tail_mem)
    free_queue_mem (queue->tail_mem);
  clear_free_list (queue);
  g_free (queue);
}

gpointer
gst_atomic_queue_peek (GstAtomicQueue * queue)
{
  GstAQueueMem *head_mem;
  gint head, tail, size;

  while (TRUE) {
    GstAQueueMem *next;

    head_mem = g_atomic_pointer_get (&queue->head_mem);

    head = g_atomic_int_get (&head_mem->head);
    tail = g_atomic_int_get (&head_mem->tail);
    size = head_mem->size;

    /* when we are not empty, we can continue */
    if (G_LIKELY (head != tail))
      break;

    /* else array empty, try to take next */
    next = g_atomic_pointer_get (&head_mem->next);
    if (next == NULL)
      return NULL;

    /* now we try to move the next array as the head memory. If we fail to do that,
     * some other reader managed to do it first and we retry */
    if (!g_atomic_pointer_compare_and_exchange ((gpointer *) &
            queue->head_mem, head_mem, next))
      continue;

    /* when we managed to swing the head pointer the old head is now
     * useless and we add it to the freelist. We can't free the memory yet
     * because we first need to make sure no reader is accessing it anymore. */
    add_to_free_list (queue, head_mem);
  }

  return head_mem->array[head & size];
}

gpointer
gst_atomic_queue_pop (GstAtomicQueue * queue)
{
  gpointer ret;
  GstAQueueMem *head_mem;
  gint head, tail, size;

#ifdef LOW_MEM
  g_atomic_int_inc (&queue->num_readers);
#endif

  do {
    while (TRUE) {
      GstAQueueMem *next;

      head_mem = g_atomic_pointer_get (&queue->head_mem);

      head = g_atomic_int_get (&head_mem->head);
      tail = g_atomic_int_get (&head_mem->tail);
      size = head_mem->size;

      /* when we are not empty, we can continue */
      if (G_LIKELY (head != tail))
        break;

      /* else array empty, try to take next */
      next = g_atomic_pointer_get (&head_mem->next);
      if (next == NULL)
        return NULL;

      /* now we try to move the next array as the head memory. If we fail to do that,
       * some other reader managed to do it first and we retry */
      if (!g_atomic_pointer_compare_and_exchange ((gpointer *) &
              queue->head_mem, head_mem, next))
        continue;

      /* when we managed to swing the head pointer the old head is now
       * useless and we add it to the freelist. We can't free the memory yet
       * because we first need to make sure no reader is accessing it anymore. */
      add_to_free_list (queue, head_mem);
    }

    ret = head_mem->array[head & size];
  } while (!g_atomic_int_compare_and_exchange (&head_mem->head, head,
          head + 1));

#ifdef LOW_MEM
  /* decrement number of readers, when we reach 0 readers we can be sure that
   * none is accessing the memory in the free list and we can try to clean up */
  if (g_atomic_int_dec_and_test (&queue->num_readers))
    clear_free_list (queue);
#endif

  return ret;
}

void
gst_atomic_queue_push (GstAtomicQueue * queue, gpointer data)
{
  GstAQueueMem *tail_mem;
  gint head, tail, size;

  do {
    while (TRUE) {
      GstAQueueMem *mem;

      tail_mem = g_atomic_pointer_get (&queue->tail_mem);
      head = g_atomic_int_get (&tail_mem->head);
      tail = g_atomic_int_get (&tail_mem->tail);
      size = tail_mem->size;

      /* we're not full, continue */
      if (tail - head < size)
        break;

      /* else we need to grow the array, we store a mask so we have to add 1 */
      mem = new_queue_mem ((size << 1) + 1, tail);

      /* try to make our new array visible to other writers */
      if (!g_atomic_pointer_compare_and_exchange ((gpointer *) &
              queue->tail_mem, tail_mem, mem)) {
        /* we tried to swap the new writer array but something changed. This is
         * because some other writer beat us to it, we free our memory and try
         * again */
        free_queue_mem (mem);
        continue;
      }
      /* make sure that readers can find our new array as well. The one who
       * manages to swap the pointer is the only one who can set the next
       * pointer to the new array */
      g_atomic_pointer_set (&tail_mem->next, mem);
    }
  } while (!g_atomic_int_compare_and_exchange (&tail_mem->tail, tail,
          tail + 1));

  tail_mem->array[tail & size] = data;
}

guint
gst_atomic_queue_length (GstAtomicQueue * queue)
{
  GstAQueueMem *head_mem, *tail_mem;
  gint head, tail;

#ifdef LOW_MEM
  g_atomic_int_inc (&queue->num_readers);
#endif

  head_mem = g_atomic_pointer_get (&queue->head_mem);
  head = g_atomic_int_get (&head_mem->head);

  tail_mem = g_atomic_pointer_get (&queue->tail_mem);
  tail = g_atomic_int_get (&tail_mem->tail);

#ifdef LOW_MEM
  if (g_atomic_int_dec_and_test (&queue->num_readers))
    clear_free_list (queue);
#endif

  return tail - head;
}
