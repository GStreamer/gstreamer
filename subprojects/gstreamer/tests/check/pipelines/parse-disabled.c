/* GStreamer unit test for disabled gst-parse
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
 * *
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
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/gstconfig.h>

GST_START_TEST (test_parse_launch_errors)
{
  GstElement *pipe;
  GError *err;
  const gchar *arr[] = { "fakesrc", "fakesink", NULL };

  err = NULL;
  pipe = gst_parse_launch ("fakesrc ! fakesink", &err);
  fail_unless (err != NULL, "expected an error, but did not get one");
  fail_unless (pipe == NULL, "got pipeline, but expected NULL");
  fail_unless (err->domain == GST_CORE_ERROR);
  fail_unless (err->code == GST_CORE_ERROR_DISABLED);
  g_error_free (err);

  err = NULL;
  pipe = gst_parse_bin_from_description ("fakesrc ! fakesink", TRUE, &err);
  fail_unless (err != NULL, "expected an error, but did not get one");
  fail_unless (pipe == NULL, "got pipeline, but expected NULL");
  fail_unless (err->domain == GST_CORE_ERROR);
  fail_unless (err->code == GST_CORE_ERROR_DISABLED);
  g_error_free (err);

  err = NULL;
  pipe = gst_parse_launchv (arr, &err);
  fail_unless (err != NULL, "expected an error, but did not get one");
  fail_unless (pipe == NULL, "got pipeline, but expected NULL");
  fail_unless (err->domain == GST_CORE_ERROR);
  fail_unless (err->code == GST_CORE_ERROR_DISABLED);
  g_error_free (err);
}

GST_END_TEST;

static Suite *
parsedisabled_suite (void)
{
  Suite *s = suite_create ("Parse Launch (Disabled Mode)");
  TCase *tc_chain = tcase_create ("parselaunchdisabled");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_launch_errors);
  return s;
}

GST_CHECK_MAIN (parsedisabled);
