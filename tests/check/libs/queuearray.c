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
compare_pointer_value (gconstpointer a, gconstpointer b)
{
  return (int) ((guintptr) a - (guintptr) b);
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
        idx = gst_queue_array_find (array, compare_pointer_value,
            GUINT_TO_POINTER (i));
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

  return s;
}


GST_CHECK_MAIN (gst_queue_array);
