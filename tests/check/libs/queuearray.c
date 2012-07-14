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

  return s;
}


GST_CHECK_MAIN (gst_queue_array);
