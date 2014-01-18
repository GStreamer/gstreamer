/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * gstiterator.c: Unit test for iterators
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


#include <gst/check/gstcheck.h>


static GList *
make_list_of_ints (gint n)
{
  GList *ret = NULL;
  gint i;

  for (i = 0; i < n; i++)
    ret = g_list_prepend (ret, GINT_TO_POINTER (i));

  return g_list_reverse (ret);
}

#define NUM_ELEMENTS 10

GST_START_TEST (test_manual_iteration)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter;
  GstIteratorResult res;
  GValue item = { 0, };
  gint i = 0;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);

  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);

  fail_unless (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS) {
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (g_value_get_pointer (&item)) == i);
      g_value_reset (&item);
      i++;
      continue;
    } else {
      fail_unless (res == GST_ITERATOR_DONE);
      break;
    }
  }
  /* clean up */
  g_value_unset (&item);
  gst_iterator_free (iter);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

GST_START_TEST (test_resync)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter;
  GstIteratorResult res;
  GValue item = { 0, };
  gint i = 0;
  gboolean hacked_list = FALSE;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);

  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);

  fail_unless (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS / 2) {
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (g_value_get_pointer (&item)) == i);
      g_value_reset (&item);
      i++;
      continue;
    } else if (!hacked_list) {
      /* here's where we test resync */
      fail_unless (res == GST_ITERATOR_OK);
      g_value_reset (&item);
      l = g_list_prepend (l, GINT_TO_POINTER (-1));
      cookie++;
      hacked_list = TRUE;
      continue;
    } else {
      fail_unless (res == GST_ITERATOR_RESYNC);
      gst_iterator_resync (iter);
      res = gst_iterator_next (iter, &item);
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (g_value_get_pointer (&item)) == -1);
      g_value_reset (&item);
      break;
    }
  }

  /* clean up */
  g_value_unset (&item);
  gst_iterator_free (iter);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

static gboolean
add_fold_func (const GValue * item, GValue * ret, gpointer user_data)
{
  g_value_set_int (ret,
      g_value_get_int (ret) + GPOINTER_TO_INT (g_value_get_pointer (item)));
  return TRUE;
}

GST_START_TEST (test_fold)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter;
  GstIteratorResult res;
  gint i, expected;
  GValue ret = { 0, };

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);
  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);
  fail_unless (iter != NULL);

  expected = 0;
  for (i = 0; i < NUM_ELEMENTS; i++)
    expected += i;

  g_value_init (&ret, G_TYPE_INT);
  g_value_set_int (&ret, 0);

  res = gst_iterator_fold (iter, add_fold_func, &ret, NULL);

  fail_unless (res == GST_ITERATOR_DONE);
  fail_unless (g_value_get_int (&ret) == expected);

  /* clean up */
  gst_iterator_free (iter);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

GST_START_TEST (test_single)
{
  GstIterator *it;
  GstStructure *s = gst_structure_new_empty ("test");
  GValue v = { 0, };
  GstStructure *i;

  g_value_init (&v, GST_TYPE_STRUCTURE);
  g_value_set_boxed (&v, s);
  it = gst_iterator_new_single (GST_TYPE_STRUCTURE, &v);
  g_value_reset (&v);

  fail_unless (gst_iterator_next (it, &v) == GST_ITERATOR_OK);
  i = g_value_get_boxed (&v);
  fail_unless (strcmp (gst_structure_get_name (s),
          gst_structure_get_name (i)) == 0);
  i = NULL;
  g_value_reset (&v);

  fail_unless (gst_iterator_next (it, &v) == GST_ITERATOR_DONE);
  fail_unless (g_value_get_boxed (&v) == NULL);

  gst_iterator_free (it);
  gst_structure_free (s);

  it = gst_iterator_new_single (GST_TYPE_STRUCTURE, NULL);

  fail_unless (gst_iterator_next (it, &v) == GST_ITERATOR_DONE);
  fail_unless (g_value_get_boxed (&v) == NULL);

  g_value_reset (&v);

  gst_iterator_free (it);
}

GST_END_TEST;

static gint
filter2_cb (gconstpointer a, gconstpointer b)
{
  const GValue *va = a;
  gint ia;

  ia = GPOINTER_TO_INT (g_value_get_pointer (va));

  return ia % 2;
}

GST_START_TEST (test_filter)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter, *filter;
  GstIteratorResult res;
  GValue item = { 0, };
  gint expected = 0, value;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);
  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);
  fail_unless (iter != NULL);

  filter = gst_iterator_filter (iter, filter2_cb, NULL);

  while (1) {
    res = gst_iterator_next (filter, &item);
    if (res == GST_ITERATOR_DONE)
      break;
    fail_unless (res == GST_ITERATOR_OK);
    value = GPOINTER_TO_INT (g_value_get_pointer (&item));
    fail_unless_equals_int (value, expected);
    expected += 2;
  }

  /* clean up */
  g_value_unset (&item);
  gst_iterator_free (filter);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

static gint
filter2_lock_cb (gconstpointer a, gconstpointer b)
{
  const GValue *va = a;
  const GValue *vb = b;
  gint ia;
  GMutex *m;

  ia = GPOINTER_TO_INT (g_value_get_pointer (va));

  m = g_value_get_pointer (vb);
  g_mutex_lock (m);
  g_mutex_unlock (m);

  return ia % 2;
}

GST_START_TEST (test_filter_locking)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter, *filter;
  GstIteratorResult res;
  GValue item = { 0, };
  GValue user_data = { 0, };
  gint expected = 0, value;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);
  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);
  fail_unless (iter != NULL);

  g_value_init (&user_data, G_TYPE_POINTER);
  g_value_set_pointer (&user_data, &m);

  filter = gst_iterator_filter (iter, filter2_lock_cb, &user_data);

  while (1) {
    res = gst_iterator_next (filter, &item);
    if (res == GST_ITERATOR_DONE)
      break;
    fail_unless (res == GST_ITERATOR_OK);
    value = GPOINTER_TO_INT (g_value_get_pointer (&item));
    fail_unless_equals_int (value, expected);
    expected += 2;
  }

  /* clean up */
  g_value_unset (&item);
  g_value_unset (&user_data);
  gst_iterator_free (filter);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

static gint
filter4_cb (gconstpointer a, gconstpointer b)
{
  const GValue *va = a;
  gint ia;

  ia = GPOINTER_TO_INT (g_value_get_pointer (va));

  return ia % 4;
}

GST_START_TEST (test_filter_of_filter)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter, *filter, *filter2;
  GstIteratorResult res;
  GValue item = { 0, };
  gint expected = 0, value;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);
  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);
  fail_unless (iter != NULL);

  filter = gst_iterator_filter (iter, filter2_cb, NULL);
  filter2 = gst_iterator_filter (filter, filter4_cb, NULL);

  while (1) {
    res = gst_iterator_next (filter2, &item);
    if (res == GST_ITERATOR_DONE)
      break;
    fail_unless (res == GST_ITERATOR_OK);
    value = GPOINTER_TO_INT (g_value_get_pointer (&item));
    fail_unless_equals_int (value, expected);
    expected += 4;
  }

  /* clean up */
  g_value_unset (&item);
  gst_iterator_free (filter2);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

static gint
filter4_lock_cb (gconstpointer a, gconstpointer b)
{
  const GValue *va = a;
  const GValue *vb = b;
  gint ia;
  GMutex *m;

  ia = GPOINTER_TO_INT (g_value_get_pointer (va));

  m = g_value_get_pointer (vb);
  g_mutex_lock (m);
  g_mutex_unlock (m);

  return ia % 4;
}

GST_START_TEST (test_filter_of_filter_locking)
{
  GList *l;
  guint32 cookie = 0;
  GMutex m;
  GstIterator *iter, *filter, *filter2;
  GstIteratorResult res;
  GValue item = { 0, };
  GValue user_data = { 0, };
  gint expected = 0, value;

  l = make_list_of_ints (NUM_ELEMENTS);
  g_mutex_init (&m);
  iter = gst_iterator_new_list (G_TYPE_POINTER, &m, &cookie, &l, NULL, NULL);
  fail_unless (iter != NULL);

  g_value_init (&user_data, G_TYPE_POINTER);
  g_value_set_pointer (&user_data, &m);

  filter = gst_iterator_filter (iter, filter2_lock_cb, &user_data);
  filter2 = gst_iterator_filter (filter, filter4_lock_cb, &user_data);

  while (1) {
    res = gst_iterator_next (filter2, &item);
    if (res == GST_ITERATOR_DONE)
      break;
    fail_unless (res == GST_ITERATOR_OK);
    value = GPOINTER_TO_INT (g_value_get_pointer (&item));
    fail_unless_equals_int (value, expected);
    expected += 4;
  }

  /* clean up */
  g_value_unset (&item);
  g_value_unset (&user_data);
  gst_iterator_free (filter2);
  g_mutex_clear (&m);
  g_list_free (l);
}

GST_END_TEST;

static Suite *
gst_iterator_suite (void)
{
  Suite *s = suite_create ("GstIterator");
  TCase *tc_chain = tcase_create ("correctness");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_manual_iteration);
  tcase_add_test (tc_chain, test_resync);
  tcase_add_test (tc_chain, test_fold);
  tcase_add_test (tc_chain, test_single);
  tcase_add_test (tc_chain, test_filter);
  tcase_add_test (tc_chain, test_filter_locking);
  tcase_add_test (tc_chain, test_filter_of_filter);
  tcase_add_test (tc_chain, test_filter_of_filter_locking);
  return s;
}

GST_CHECK_MAIN (gst_iterator);
