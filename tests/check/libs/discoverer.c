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

  dc = gst_discoverer_new (GST_SECOND, &err);
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


static Suite *
discoverer_suite (void)
{
  Suite *s = suite_create ("discoverer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_disco_init);
  tcase_add_test (tc_chain, test_disco_sync);
  return s;
}

GST_CHECK_MAIN (discoverer);
