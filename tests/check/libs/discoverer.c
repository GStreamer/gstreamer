/* GStreamer unit tests for discoverer
 *
 * Copyright (C) 2011 Stefan Kost <ensonic@users.sf.net>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/pbutils/pbutils.h>

#include <stdio.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>


GST_START_TEST (test_disco_init)
{
  GError *err = NULL;
  GstDiscoverer *dc;

  dc = gst_discoverer_new (GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  g_object_unref (dc);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri;

  /* high timeout, in case we're running under valgrind */
  dc = gst_discoverer_new (5 * GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  /* GST_TEST_FILE comes from makefile CFLAGS */
  GST_INFO ("discovering file '%s'", GST_TEST_FILE);
  uri = g_filename_to_uri (GST_TEST_FILE, NULL, &err);
  fail_unless (err == NULL);
  GST_INFO ("discovering uri '%s'", uri);

  info = gst_discoverer_discover_uri (dc, uri, &err);
  result = gst_discoverer_info_get_result (info);
  GST_INFO ("result: %d", result);
  gst_discoverer_info_unref (info);
  g_free (uri);

  if (err) {
    /* we won't have the codec for the jpeg */
    g_error_free (err);
  }

  g_object_unref (dc);
}

GST_END_TEST;

static void
test_disco_sync_reuse (const gchar * test_fn, guint num, GstClockTime timeout)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri, *path;
  int i;

  dc = gst_discoverer_new (timeout, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  /* GST_TEST_FILE comes from makefile CFLAGS */
  path = g_build_filename (GST_TEST_FILES_PATH, test_fn, NULL);
  uri = gst_filename_to_uri (path, &err);
  g_free (path);
  fail_unless (err == NULL);

  for (i = 0; i < num; ++i) {
    GST_INFO ("[%02d] discovering uri '%s'", i, uri);
    info = gst_discoverer_discover_uri (dc, uri, &err);
    if (info) {
      result = gst_discoverer_info_get_result (info);
      GST_INFO ("result: %d", result);
      gst_discoverer_info_unref (info);
    }
    /* in case we don't have some of the elements needed */
    if (err) {
      g_error_free (err);
      err = NULL;
    }
  }
  g_free (uri);

  g_object_unref (dc);
}

GST_START_TEST (test_disco_sync_reuse_ogg)
{
  test_disco_sync_reuse ("theora-vorbis.ogg", 2, 10 * GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync_reuse_mp3)
{
  /* this will cause errors because -base doesn't do mp3 parsing or decoding */
  test_disco_sync_reuse ("test.mp3", 3, 10 * GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync_reuse_timeout)
{
  /* set minimum timeout to test that, esp. leakage under valgrind */
  /* FIXME: should really be even shorter */
  test_disco_sync_reuse ("theora-vorbis.ogg", 2, GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_missing_plugins)
{
  const gchar *files[] = { "test.mkv", "test.mp3", "partialframe.mjpeg" };
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri, *path;
  int i;

  for (i = 0; i < G_N_ELEMENTS (files); ++i) {
    dc = gst_discoverer_new (5 * GST_SECOND, &err);
    fail_unless (dc != NULL);
    fail_unless (err == NULL);

    /* GST_TEST_FILE comes from makefile CFLAGS */
    path = g_build_filename (GST_TEST_FILES_PATH, files[i], NULL);
    uri = gst_filename_to_uri (path, &err);
    g_free (path);
    fail_unless (err == NULL);

    GST_INFO ("discovering uri '%s'", uri);
    info = gst_discoverer_discover_uri (dc, uri, &err);
    fail_unless (info != NULL);
    fail_unless (err != NULL);
    result = gst_discoverer_info_get_result (info);
    GST_INFO ("result: %d, error message: %s", result, err->message);
    fail_unless_equals_int (result, GST_DISCOVERER_MISSING_PLUGINS);
    GST_INFO ("misc: %" GST_PTR_FORMAT, gst_discoverer_info_get_misc (info));

    gst_discoverer_info_unref (info);
    g_error_free (err);
    err = NULL;
    g_free (uri);
    g_object_unref (dc);
  }
}

GST_END_TEST;

static Suite *
discoverer_suite (void)
{
  Suite *s = suite_create ("discoverer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_disco_init);
  tcase_add_test (tc_chain, test_disco_sync);
  tcase_add_test (tc_chain, test_disco_sync_reuse_ogg);
  tcase_add_test (tc_chain, test_disco_sync_reuse_mp3);
  tcase_add_test (tc_chain, test_disco_sync_reuse_timeout);
  tcase_add_test (tc_chain, test_disco_missing_plugins);
  return s;
}

GST_CHECK_MAIN (discoverer);
