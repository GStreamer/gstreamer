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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
  GMutex *m;
  GstIterator *iter;
  GstIteratorResult res;
  gpointer item;
  gint i = 0;

  l = make_list_of_ints (NUM_ELEMENTS);
  m = g_mutex_new ();

  iter = gst_iterator_new_list (G_TYPE_INT, m, &cookie, &l, NULL, NULL, NULL);

  fail_unless (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS) {
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (item) == i);
      i++;
      continue;
    } else {
      fail_unless (res == GST_ITERATOR_DONE);
      break;
    }
  }

  /* clean up */
  gst_iterator_free (iter);
  g_mutex_free (m);
  g_list_free (l);
}

GST_END_TEST;

GST_START_TEST (test_resync)
{
  GList *l;
  guint32 cookie = 0;
  GMutex *m;
  GstIterator *iter;
  GstIteratorResult res;
  gpointer item;
  gint i = 0;
  gboolean hacked_list = FALSE;

  l = make_list_of_ints (NUM_ELEMENTS);
  m = g_mutex_new ();

  iter = gst_iterator_new_list (G_TYPE_INT, m, &cookie, &l, NULL, NULL, NULL);

  fail_unless (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS / 2) {
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (item) == i);
      i++;
      continue;
    } else if (!hacked_list) {
      /* here's where we test resync */
      fail_unless (res == GST_ITERATOR_OK);
      l = g_list_prepend (l, GINT_TO_POINTER (-1));
      cookie++;
      hacked_list = TRUE;
      continue;
    } else {
      fail_unless (res == GST_ITERATOR_RESYNC);
      gst_iterator_resync (iter);
      res = gst_iterator_next (iter, &item);
      fail_unless (res == GST_ITERATOR_OK);
      fail_unless (GPOINTER_TO_INT (item) == -1);
      break;
    }
  }

  /* clean up */
  gst_iterator_free (iter);
  g_mutex_free (m);
  g_list_free (l);
}

GST_END_TEST;

static gboolean
add_fold_func (gpointer item, GValue * ret, gpointer user_data)
{
  g_value_set_int (ret, g_value_get_int (ret) + GPOINTER_TO_INT (item));
  return TRUE;
}

GST_START_TEST (test_fold)
{
  GList *l;
  guint32 cookie = 0;
  GMutex *m;
  GstIterator *iter;
  GstIteratorResult res;
  gint i, expected;
  GValue ret = { 0, };

  l = make_list_of_ints (NUM_ELEMENTS);
  m = g_mutex_new ();
  iter = gst_iterator_new_list (G_TYPE_INT, m, &cookie, &l, NULL, NULL, NULL);
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
  g_mutex_free (m);
  g_list_free (l);
}

GST_END_TEST;

GST_START_TEST (test_single)
{
  GstIterator *it;
  GstStructure *s = gst_structure_new ("test", NULL);
  GstStructure *i;

  it = gst_iterator_new_single (GST_TYPE_STRUCTURE, s,
      (GstCopyFunction) gst_structure_copy, (GFreeFunc) gst_structure_free);

  fail_unless (gst_iterator_next (it, (gpointer) & i) == GST_ITERATOR_OK);
  fail_unless (strcmp (gst_structure_get_name (s),
          gst_structure_get_name (i)) == 0);
  gst_structure_free (i);
  i = NULL;
  fail_unless (gst_iterator_next (it, (gpointer) & i) == GST_ITERATOR_DONE);
  fail_unless (i == NULL);

  gst_iterator_free (it);
  gst_structure_free (s);

  it = gst_iterator_new_single (GST_TYPE_STRUCTURE, NULL,
      (GstCopyFunction) gst_structure_copy, (GFreeFunc) gst_structure_free);

  fail_unless (gst_iterator_next (it, (gpointer) & i) == GST_ITERATOR_DONE);
  fail_unless (i == NULL);

  gst_iterator_free (it);
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
  return s;
}

GST_CHECK_MAIN (gst_iterator);
