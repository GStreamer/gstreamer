/* GStreamer
 *
 * unit test for GstQueueArray
 *
 * Copyright (C) <2009> Edward Hervey <bilboed@bilboed.com>
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
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include "../../../plugins/elements/gstqueuearray.h"
#include "../../../plugins/elements/gstqueuearray.c"

/* Simplest test
 * Initial size : 10
 * Add 10, Remove 10
 */
GST_START_TEST (test_array_1)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push 5 values in */
  for (i = 0; i < 5; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (array->length, 5);

  /* pull 5 values out */
  for (i = 0; i < 5; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (array->length, 0);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (array->length, 10);
  /* It did not grow beyond initial size */
  fail_unless_equals_int (array->size, 10);
  /* The head is still at the beginning */
  fail_unless_equals_int (array->head, 0);
  /* The tail wrapped around to the head */
  fail_unless_equals_int (array->tail, 0);


  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));

  fail_unless_equals_int (array->length, 11);
  /* It did grow beyond initial size */
  fail_unless_equals_int (array->size, 15);
  /* The head remains the same */
  fail_unless_equals_int (array->head, 0);
  /* The tail was brought to position 11 */
  fail_unless_equals_int (array->tail, 11);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (array->length, 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_multiple)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  /* push 11 values in */
  for (i = 0; i < 11; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* With 11 values, it should have grown once (15) */
  fail_unless_equals_int (array->length, 11);
  fail_unless_equals_int (array->size, 15);

  for (i = 11; i < 20; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* With 20 total values, it should have grown another time (3 * 15) / 2 = 22) */
  fail_unless_equals_int (array->length, 20);
  /* It did grow beyond initial size */
  fail_unless_equals_int (array->size, 22);

  /* pull the 20 values out */
  for (i = 0; i < 20; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (array->length, 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_middle)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  /* push/pull 5 values to end up in the middle */
  for (i = 0; i < 5; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (array->length, 10);
  /* It did not grow beyond initial size */
  fail_unless_equals_int (array->size, 10);

  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));
  fail_unless_equals_int (array->length, 11);
  /* It did grow beyond initial size */
  fail_unless_equals_int (array->size, 15);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (array->length, 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_end)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  /* push/pull 9 values to end up at the last position */
  for (i = 0; i < 9; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (array->length, 10);
  /* It did not grow beyond initial size */
  fail_unless_equals_int (array->size, 10);

  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));
  fail_unless_equals_int (array->length, 11);
  /* It did grow beyond initial size */
  fail_unless_equals_int (array->size, 15);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (array->length, 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

static int
compare_pointer_value (gconstpointer a, gconstpointer b)
{
  return (int) ((guintptr) a - (guintptr) b);
}

GST_START_TEST (test_array_find)
{
  GstQueueArray *array;
  guint i;
  guint index;

  guint random_initial = g_random_int_range (10, 100);
  guint value_to_find = 5;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  while (random_initial--) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (g_random_int ()));
    gst_queue_array_pop_head (array);
  }

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (array->length, 10);
  fail_unless_equals_int (array->size, 10);

  index =
      gst_queue_array_find (array, compare_pointer_value,
      GINT_TO_POINTER (value_to_find));
  fail_if (index == -1);
  fail_unless_equals_int (value_to_find, GPOINTER_TO_INT (array->array[index]));

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_pop_head (array);

  index =
      gst_queue_array_find (array, compare_pointer_value,
      GINT_TO_POINTER (value_to_find));
  fail_unless (index == -1);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_drop)
{
  GstQueueArray *array;
  guint i;
  guint index;
  guint index_2;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);
  fail_unless_equals_int (array->size, 10);

  for (i = 0; i < 5; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless (array->length == 5);

  /* Naive case remove head */
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (0));
  fail_if (index == -1);
  gst_queue_array_drop_element (array, index);
  fail_unless (array->length == 4);
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (0));
  fail_unless (index == -1);

  /* Naive case remove tail */
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (4));
  fail_if (index == -1);
  gst_queue_array_drop_element (array, index);
  fail_unless (array->length == 3);
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (4));
  fail_unless (index == -1);

  /* Remove in middle of non-wrapped */
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (2));
  index_2 =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (3));
  fail_if (index == -1);
  fail_if (index_2 == -1);
  gst_queue_array_drop_element (array, index);
  fail_unless (array->length == 2);
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (2));
  fail_unless (index == -1);
  index_2 =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (3));
  fail_if (index_2 == -1);

  /* Remove the rest */
  while (array->length)
    gst_queue_array_pop_head (array);

  /* Add until wrapping */
  for (i = 0; i < 9; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless (array->head > array->tail);

  /* Remove from between head and array end */
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (1));
  fail_if (index == -1);
  fail_unless (index > array->head);
  index_2 = array->head;
  gst_queue_array_drop_element (array, index);
  fail_unless (array->length == 8);
  fail_if (array->head == index_2);
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (1));
  fail_unless (index == -1);

  /* Remove from between head and array end */
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (8));
  fail_if (index == -1);
  fail_unless (index < array->tail);
  index_2 = array->tail;
  gst_queue_array_drop_element (array, index);
  fail_unless (array->length == 7);
  fail_if (array->tail == index_2);
  index =
      gst_queue_array_find (array, compare_pointer_value, GINT_TO_POINTER (8));
  fail_unless (index == -1);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_drop2)
{
#define NUM_QA_ELEMENTS 674
  gboolean in_array[NUM_QA_ELEMENTS] = { FALSE, };
  GstQueueArray *array;
  guint i, j, count, idx;

  array = gst_queue_array_new (10);

  for (i = 0; i < NUM_QA_ELEMENTS; i++) {
    gpointer element = GUINT_TO_POINTER (i);

    if (g_random_boolean ()) {
      gst_queue_array_push_tail (array, element);
      in_array[i] = TRUE;
    }
  }

  for (j = 0, count = 0; j < NUM_QA_ELEMENTS; j++)
    count += in_array[j] ? 1 : 0;
  fail_unless_equals_int (array->length, count);

  while (array->length > 0) {
    for (i = 0; i < NUM_QA_ELEMENTS; i++) {
      if (g_random_boolean () && g_random_boolean () && in_array[i]) {
        idx = gst_queue_array_find (array, compare_pointer_value,
            GUINT_TO_POINTER (i));
        gst_queue_array_drop_element (array, idx);
        in_array[i] = FALSE;
      }
    }

    for (j = 0, count = 0; j < NUM_QA_ELEMENTS; j++)
      count += in_array[j] ? 1 : 0;
    fail_unless_equals_int (array->length, count);
  }

  gst_queue_array_free (array);
}

GST_END_TEST;

static Suite *
gst_queue_array_suite (void)
{
  Suite *s = suite_create ("GstQueueArray");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_array_1);
  tcase_add_test (tc_chain, test_array_grow);
  tcase_add_test (tc_chain, test_array_grow_multiple);
  tcase_add_test (tc_chain, test_array_grow_middle);
  tcase_add_test (tc_chain, test_array_grow_end);
  tcase_add_test (tc_chain, test_array_find);
  tcase_add_test (tc_chain, test_array_drop);
  tcase_add_test (tc_chain, test_array_drop2);

  return s;
}


GST_CHECK_MAIN (gst_queue_array);
