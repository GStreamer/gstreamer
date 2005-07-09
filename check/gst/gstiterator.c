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


#include "../gstcheck.h"


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

  iter = gst_iterator_new_list (m, &cookie, &l, NULL, NULL, NULL);

  g_return_if_fail (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS) {
      g_return_if_fail (res == GST_ITERATOR_OK);
      g_return_if_fail (GPOINTER_TO_INT (item) == i);
      i++;
      continue;
    } else {
      g_return_if_fail (res == GST_ITERATOR_DONE);
      break;
    }
  }

  gst_iterator_free (iter);
}

GST_END_TEST
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

  iter = gst_iterator_new_list (m, &cookie, &l, NULL, NULL, NULL);

  g_return_if_fail (iter != NULL);

  while (1) {
    res = gst_iterator_next (iter, &item);
    if (i < NUM_ELEMENTS / 2) {
      g_return_if_fail (res == GST_ITERATOR_OK);
      g_return_if_fail (GPOINTER_TO_INT (item) == i);
      i++;
      continue;
    } else if (!hacked_list) {
      /* here's where we test resync */
      g_return_if_fail (res == GST_ITERATOR_OK);
      l = g_list_prepend (l, GINT_TO_POINTER (-1));
      cookie++;
      hacked_list = TRUE;
      continue;
    } else {
      g_return_if_fail (res == GST_ITERATOR_RESYNC);
      gst_iterator_resync (iter);
      res = gst_iterator_next (iter, &item);
      g_return_if_fail (res == GST_ITERATOR_OK);
      g_return_if_fail (GPOINTER_TO_INT (item) == -1);
      break;
    }
  }

  gst_iterator_free (iter);
}
GST_END_TEST static gboolean
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
  iter = gst_iterator_new_list (m, &cookie, &l, NULL, NULL, NULL);
  g_return_if_fail (iter != NULL);

  expected = 0;
  for (i = 0; i < NUM_ELEMENTS; i++)
    expected += i;

  g_value_init (&ret, G_TYPE_INT);
  g_value_set_int (&ret, 0);

  res = gst_iterator_fold (iter, add_fold_func, &ret, NULL);

  g_return_if_fail (res == GST_ITERATOR_DONE);
  g_return_if_fail (g_value_get_int (&ret) == expected);
}
GST_END_TEST Suite *
gstiterator_suite (void)
{
  Suite *s = suite_create ("GstIterator");
  TCase *tc_chain = tcase_create ("correctness");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_manual_iteration);
  tcase_add_test (tc_chain, test_resync);
  tcase_add_test (tc_chain, test_fold);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstiterator_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
