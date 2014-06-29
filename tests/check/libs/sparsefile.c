/* GStreamer
 *
 * unit test for cachefile helper
 *
 * Copyright (C) 2014 Wim Taymans  <wtaymans@redhat.com>
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

#include <glib/gstdio.h>

#include <gst/check/gstcheck.h>

/* not public API for now */
#include "../../../plugins/elements/gstsparsefile.c"

static void
expect_range_before (GstSparseFile * file, gsize offset, gsize start,
    gsize stop)
{
  gsize tstart, tstop;

  fail_unless (gst_sparse_file_get_range_before (file, offset, &tstart,
          &tstop) == TRUE);
  fail_unless (tstart == start);
  fail_unless (tstop == stop);
}

static void
expect_range_after (GstSparseFile * file, gsize offset, gsize start, gsize stop)
{
  gsize tstart, tstop;

  fail_unless (gst_sparse_file_get_range_after (file, offset, &tstart,
          &tstop) == TRUE);
  fail_unless (tstart == start);
  fail_unless (tstop == stop);
}

static gboolean
expect_write (GstSparseFile * file, gsize offset, gsize count, gsize result,
    gsize avail)
{
  GError *error = NULL;
  gchar buffer[200] = { 0, };
  gsize res, a;

  res = gst_sparse_file_write (file, offset, buffer, count, &a, &error);
  if (res != result)
    return FALSE;
  if (res == 0) {
    if (error == NULL)
      return FALSE;
    g_clear_error (&error);
  } else if (a != avail)
    return FALSE;
  return TRUE;
}

static gboolean
expect_read (GstSparseFile * file, gsize offset, gsize count, gsize result,
    gsize avail)
{
  GError *error = NULL;
  gchar buffer[200];
  gsize res, a;

  res = gst_sparse_file_read (file, offset, buffer, count, &a, &error);
  if (res != result)
    return FALSE;
  if (res == 0) {
    if (error == NULL)
      return FALSE;
    g_clear_error (&error);
  } else if (a != avail)
    return FALSE;
  return TRUE;
}

GST_START_TEST (test_write_read)
{
  GstSparseFile *file;
  gint fd;
  gchar *name;
  gsize start, stop;

  name = g_strdup ("cachefile-testXXXXXX");
  fd = g_mkstemp (name);
  fail_if (fd == -1);

  file = gst_sparse_file_new ();
  fail_unless (file != NULL);
  fail_unless (gst_sparse_file_set_fd (file, fd));
  fail_unless (gst_sparse_file_n_ranges (file) == 0);

  /* should fail, we didn't write anything yet */
  fail_unless (expect_read (file, 0, 100, 0, 0));

  /* no ranges, searching for a range should fail */
  fail_unless (gst_sparse_file_n_ranges (file) == 0);
  fail_unless (gst_sparse_file_get_range_before (file, 0, &start,
          &stop) == FALSE);
  fail_unless (gst_sparse_file_get_range_before (file, 10, &start,
          &stop) == FALSE);
  fail_unless (gst_sparse_file_get_range_after (file, 0, &start,
          &stop) == FALSE);
  fail_unless (gst_sparse_file_get_range_after (file, 10, &start,
          &stop) == FALSE);

  /* now write some data */
  fail_unless (expect_write (file, 0, 100, 100, 0));

  /* we have 1 range now */
  fail_unless (gst_sparse_file_n_ranges (file) == 1);
  expect_range_before (file, 0, 0, 100);
  expect_range_after (file, 0, 0, 100);
  expect_range_before (file, 100, 0, 100);
  expect_range_before (file, 50, 0, 100);
  expect_range_before (file, 200, 0, 100);
  fail_unless (gst_sparse_file_get_range_after (file, 100, &start,
          &stop) == FALSE);
  expect_range_after (file, 50, 0, 100);

  /* we can read all data now */
  fail_unless (expect_read (file, 0, 100, 100, 0));
  /* we can read less */
  fail_unless (expect_read (file, 0, 50, 50, 50));
  /* but we can't read more than what is written */
  fail_unless (expect_read (file, 0, 101, 0, 0));

  g_unlink (name);
  gst_sparse_file_free (file);
  g_free (name);
}

GST_END_TEST;

GST_START_TEST (test_write_merge)
{
  GstSparseFile *file;
  gint fd;
  gchar *name;
  gsize start, stop;

  name = g_strdup ("cachefile-testXXXXXX");
  fd = g_mkstemp (name);
  fail_if (fd == -1);

  file = gst_sparse_file_new ();
  gst_sparse_file_set_fd (file, fd);

  /* write something at offset 0 */
  fail_unless (expect_write (file, 0, 100, 100, 0));
  /* we have 1 range now */
  fail_unless (gst_sparse_file_n_ranges (file) == 1);
  expect_range_before (file, 110, 0, 100);
  expect_range_after (file, 50, 0, 100);
  fail_unless (gst_sparse_file_get_range_after (file, 100, &start,
          &stop) == FALSE);

  /* read should fail */
  fail_unless (expect_read (file, 50, 150, 0, 0));

  /* write something at offset 150 */
  fail_unless (expect_write (file, 150, 100, 100, 0));
  /* we have 2 ranges now */
  fail_unless (gst_sparse_file_n_ranges (file) == 2);
  expect_range_before (file, 110, 0, 100);
  expect_range_after (file, 50, 0, 100);
  expect_range_after (file, 100, 150, 250);
  expect_range_before (file, 150, 150, 250);

  /* read should still fail */
  fail_unless (expect_read (file, 50, 150, 0, 0));

  /* fill the hole */
  fail_unless (expect_write (file, 100, 50, 50, 100));
  /* we have 1 range now */
  fail_unless (gst_sparse_file_n_ranges (file) == 1);
  expect_range_before (file, 110, 0, 250);
  expect_range_after (file, 50, 0, 250);
  expect_range_after (file, 100, 0, 250);
  expect_range_before (file, 150, 0, 250);
  fail_unless (gst_sparse_file_get_range_after (file, 250, &start,
          &stop) == FALSE);

  /* read work */
  fail_unless (expect_read (file, 50, 150, 150, 50));

  g_unlink (name);
  gst_sparse_file_free (file);
  g_free (name);
}

GST_END_TEST;

static Suite *
gst_cachefile_suite (void)
{
  Suite *s = suite_create ("cachefile");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_write_read);
  tcase_add_test (tc_chain, test_write_merge);

  return s;
}

GST_CHECK_MAIN (gst_cachefile);
