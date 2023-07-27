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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstqueuearray.h>

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

  fail_unless_equals_int (gst_queue_array_get_length (array), 5);

  /* pull 5 values out */
  for (i = 0; i < 5; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);


  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));

  fail_unless_equals_int (gst_queue_array_get_length (array), 11);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_multiple)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push 11 values in */
  for (i = 0; i < 11; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* With 11 values, it should have grown once (15) */
  fail_unless_equals_int (gst_queue_array_get_length (array), 11);

  for (i = 11; i < 20; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* With 20 total values, it should have grown another time (3 * 15) / 2 = 22) */
  fail_unless_equals_int (gst_queue_array_get_length (array), 20);
  /* It did grow beyond initial size */

  /* pull the 20 values out */
  for (i = 0; i < 20; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_middle)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push/pull 5 values to end up in the middle */
  for (i = 0; i < 5; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));
  fail_unless_equals_int (gst_queue_array_get_length (array), 11);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_end)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push/pull 9 values to end up at the last position */
  for (i = 0; i < 9; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* If we add one value, it will grow */
  gst_queue_array_push_tail (array, GINT_TO_POINTER (10));
  fail_unless_equals_int (gst_queue_array_get_length (array), 11);

  /* pull the 11 values out */
  for (i = 0; i < 11; i++) {
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);
  gst_queue_array_free (array);
}

GST_END_TEST;

static int
compare_pointer_value (guintptr a, guintptr b)
{
  return (int) (a - b);
}

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
  fail_unless_equals_int (gst_queue_array_get_length (array), count);

  while (gst_queue_array_get_length (array) > 0) {
    for (i = 0; i < NUM_QA_ELEMENTS; i++) {
      gpointer dropped;

      if (g_random_boolean () && g_random_boolean () && in_array[i]) {
        idx = gst_queue_array_find (array,
            (GCompareFunc) compare_pointer_value, GUINT_TO_POINTER (i));
        dropped = gst_queue_array_drop_element (array, idx);
        fail_unless_equals_int (i, GPOINTER_TO_INT (dropped));
        in_array[i] = FALSE;
      }
    }

    for (j = 0, count = 0; j < NUM_QA_ELEMENTS; j++)
      count += in_array[j] ? 1 : 0;
    fail_unless_equals_int (gst_queue_array_get_length (array), count);
  }

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_grow_from_prealloc1)
{
  GstQueueArray *array;

  array = gst_queue_array_new (1);
  gst_queue_array_push_tail (array, NULL);
  gst_queue_array_push_tail (array, NULL);
  gst_queue_array_free (array);
}

GST_END_TEST;
GST_START_TEST (test_array_peek_nth)
{
  GstQueueArray *array;
  guint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* push 10 values in */
  for (i = 0; i < 10; i++)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  for (i = 0; i < 10; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_peek_nth (array,
                i)), i);

  gst_queue_array_pop_head (array);

  for (i = 0; i < 9; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_peek_nth (array,
                i)), i + 1);

  gst_queue_array_free (array);
}

GST_END_TEST;


GST_START_TEST (test_array_peek_pop_tail)
{
  const guint array_sizes[] = { 0, 1, 2, 5 };
  guint s;

  for (s = 0; s < G_N_ELEMENTS (array_sizes); ++s) {
    GstQueueArray *array;

    GST_INFO ("Testing with initial size %u", array_sizes[s]);

    array = gst_queue_array_new (array_sizes[s]);
    fail_unless_equals_int (gst_queue_array_get_length (array), 0);

    fail_unless (gst_queue_array_peek_tail (array) == NULL);
    fail_unless (gst_queue_array_pop_tail (array) == NULL);

    gst_queue_array_push_tail (array, GINT_TO_POINTER (42));
    fail_unless_equals_int (gst_queue_array_get_length (array), 1);
    fail_unless (gst_queue_array_peek_tail (array) == GINT_TO_POINTER (42));
    fail_unless (gst_queue_array_peek_head (array) == GINT_TO_POINTER (42));
    fail_unless_equals_int (gst_queue_array_get_length (array), 1);
    fail_unless (gst_queue_array_pop_tail (array) == GINT_TO_POINTER (42));
    fail_unless_equals_int (gst_queue_array_get_length (array), 0);

    gst_queue_array_push_tail (array, GINT_TO_POINTER (42));
    fail_unless_equals_int (gst_queue_array_get_length (array), 1);
    fail_unless (gst_queue_array_pop_head (array) == GINT_TO_POINTER (42));
    fail_unless_equals_int (gst_queue_array_get_length (array), 0);
    fail_unless (gst_queue_array_peek_tail (array) == NULL);
    fail_unless (gst_queue_array_pop_tail (array) == NULL);

    gst_queue_array_push_tail (array, GINT_TO_POINTER (43));
    gst_queue_array_push_tail (array, GINT_TO_POINTER (44));

    fail_unless_equals_int (gst_queue_array_get_length (array), 2);
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_peek_head (array)),
        43);
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_peek_tail (array)),
        44);
    fail_unless_equals_int (gst_queue_array_get_length (array), 2);
    fail_unless (gst_queue_array_pop_tail (array) == GINT_TO_POINTER (44));
    fail_unless_equals_int (gst_queue_array_get_length (array), 1);
    fail_unless (gst_queue_array_peek_head (array) == GINT_TO_POINTER (43));
    fail_unless (gst_queue_array_peek_tail (array) == GINT_TO_POINTER (43));
    fail_unless_equals_int (gst_queue_array_get_length (array), 1);

    gst_queue_array_free (array);
  }
}

GST_END_TEST;

GST_START_TEST (test_array_push_sorted)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2)
    gst_queue_array_push_sorted (array, GINT_TO_POINTER (i),
        (GCompareDataFunc) compare_pointer_value, NULL);

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_push_sorted_wrapped)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* Push and pull 4 values to offset head/tail.
   * Pushing +1's the tail and popping +1's the head, so the push after this will
   * store data at [4] internally, and further 10 pushes will cause the array
   * to wrap around. */
  for (i = 0; i < 4; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2)
    gst_queue_array_push_sorted (array, GINT_TO_POINTER (i),
        (GCompareDataFunc) compare_pointer_value, NULL);

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);

  gst_queue_array_free (array);
}

GST_END_TEST;

typedef struct
{
  gint value;
} CompareTestStruct;

static int
compare_struct_value (CompareTestStruct * a, CompareTestStruct * b)
{
  return a->value - b->value;
}

GST_START_TEST (test_array_push_sorted_struct)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new_for_struct (sizeof (CompareTestStruct), 10);

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_tail_struct (array, &s);
  }

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_sorted_struct (array, &s,
        (GCompareDataFunc) compare_struct_value, NULL);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++) {
    CompareTestStruct *s = gst_queue_array_pop_head_struct (array);
    fail_unless_equals_int (s->value, i);
  }

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_push_sorted_struct_wrapped)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new_for_struct (sizeof (CompareTestStruct), 10);

  /* Push and pull 4 values to offset head/tail.
   * Pushing +1's the tail and popping +1's the head, so the push after this will
   * store data at [4] internally, and further 10 pushes will cause the array
   * to wrap around. */
  for (i = 0; i < 4; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_tail_struct (array, &s);
  }

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_sorted_struct (array, &s,
        (GCompareDataFunc) compare_struct_value, NULL);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++) {
    CompareTestStruct *s = gst_queue_array_pop_head_struct (array);
    fail_unless_equals_int (s->value, i);
  }

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_sort)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Sort the array */
  gst_queue_array_sort (array, (GCompareDataFunc) compare_pointer_value, NULL);

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_sort_struct)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new_for_struct (sizeof (CompareTestStruct), 10);

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_tail_struct (array, &s);
  }

  /* Now try to push even values, in reverse order because why not */
  for (i = 8; i >= 0; i -= 2) {
    CompareTestStruct s = { i };
    gst_queue_array_push_tail_struct (array, &s);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Sort the array */
  gst_queue_array_sort (array, (GCompareDataFunc) compare_struct_value, NULL);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++) {
    CompareTestStruct *s = gst_queue_array_pop_head_struct (array);
    fail_unless_equals_int (s->value, i);
  }

  gst_queue_array_free (array);
}

GST_END_TEST;

GST_START_TEST (test_array_sort_wrapped)
{
  GstQueueArray *array;
  gint i;

  /* Create an array of initial size 10 */
  array = gst_queue_array_new (10);

  /* Push and pull 4 values to offset head/tail */
  for (i = 0; i < 4; i++) {
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);
  }

  fail_unless_equals_int (gst_queue_array_get_length (array), 0);

  /* Fill it with odd values */
  for (i = 1; i < 10; i += 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  /* Now try to push even values, in reverse order because why not
   * At this point the array should've wrapped around (head > tail) */
  for (i = 8; i >= 0; i -= 2)
    gst_queue_array_push_tail (array, GINT_TO_POINTER (i));

  fail_unless_equals_int (gst_queue_array_get_length (array), 10);

  /* Sort the array */
  gst_queue_array_sort (array, (GCompareDataFunc) compare_pointer_value, NULL);

  /* Check that the array is now 0-9 in correct order */
  for (i = 0; i < 10; i++)
    fail_unless_equals_int (GPOINTER_TO_INT (gst_queue_array_pop_head (array)),
        i);

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
  tcase_add_test (tc_chain, test_array_drop2);
  tcase_add_test (tc_chain, test_array_grow_from_prealloc1);
  tcase_add_test (tc_chain, test_array_peek_pop_tail);
  tcase_add_test (tc_chain, test_array_peek_nth);
  tcase_add_test (tc_chain, test_array_push_sorted);
  tcase_add_test (tc_chain, test_array_push_sorted_wrapped);
  tcase_add_test (tc_chain, test_array_push_sorted_struct);
  tcase_add_test (tc_chain, test_array_push_sorted_struct_wrapped);
  tcase_add_test (tc_chain, test_array_sort);
  tcase_add_test (tc_chain, test_array_sort_struct);
  tcase_add_test (tc_chain, test_array_sort_wrapped);

  return s;
}


GST_CHECK_MAIN (gst_queue_array);
