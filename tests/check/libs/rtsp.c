/* GStreamer unit tests for the RTSP support library
 *
 * Copyright (C) 2010 Andy Wingo <wingo@oblong.com>
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

#include <gst/check/gstcheck.h>

#include <gst/rtsp/gstrtspurl.h>
#include <string.h>

GST_START_TEST (test_rtsp_url_basic)
{
  GstRTSPUrl *url = NULL;
  GstRTSPResult res;

  res = gst_rtsp_url_parse ("rtsp://localhost/foo/bar", &url);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (url != NULL);
  fail_unless (url->transports & GST_RTSP_LOWER_TRANS_TCP);
  fail_unless (url->transports & GST_RTSP_LOWER_TRANS_UDP);
  fail_unless (url->transports & GST_RTSP_LOWER_TRANS_UDP_MCAST);
  fail_unless (url->family == GST_RTSP_FAM_INET);
  fail_unless (!url->user);
  fail_unless (!url->passwd);
  fail_unless (!strcmp (url->host, "localhost"));
  /* fail_unless (url->port == GST_RTSP_DEFAULT_PORT); */
  fail_unless (!strcmp (url->abspath, "/foo/bar"));
  fail_unless (!url->query);

  gst_rtsp_url_free (url);
}

GST_END_TEST;

GST_START_TEST (test_rtsp_url_components_1)
{
  GstRTSPUrl *url = NULL;
  GstRTSPResult res;
  gchar **comps = NULL;

  res = gst_rtsp_url_parse ("rtsp://localhost/foo/bar", &url);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (url != NULL);

  comps = gst_rtsp_url_decode_path_components (url);
  fail_unless (comps != NULL);
  fail_unless (g_strv_length (comps) == 3);
  fail_unless (!strcmp (comps[0], ""));
  fail_unless (!strcmp (comps[1], "foo"));
  fail_unless (!strcmp (comps[2], "bar"));

  g_strfreev (comps);
  gst_rtsp_url_free (url);
}

GST_END_TEST;

GST_START_TEST (test_rtsp_url_components_2)
{
  GstRTSPUrl *url = NULL;
  GstRTSPResult res;
  gchar **comps = NULL;

  res = gst_rtsp_url_parse ("rtsp://localhost/foo%2Fbar/qux%20baz", &url);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (url != NULL);

  comps = gst_rtsp_url_decode_path_components (url);
  fail_unless (comps != NULL);
  fail_unless (g_strv_length (comps) == 3);
  fail_unless (!strcmp (comps[0], ""));
  fail_unless (!strcmp (comps[1], "foo/bar"));
  fail_unless (!strcmp (comps[2], "qux baz"));

  g_strfreev (comps);
  gst_rtsp_url_free (url);
}

GST_END_TEST;

GST_START_TEST (test_rtsp_url_components_3)
{
  GstRTSPUrl *url = NULL;
  GstRTSPResult res;
  gchar **comps = NULL;

  res = gst_rtsp_url_parse ("rtsp://localhost/foo%00bar/qux%20baz", &url);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (url != NULL);

  comps = gst_rtsp_url_decode_path_components (url);
  fail_unless (comps != NULL);
  fail_unless (g_strv_length (comps) == 3);
  fail_unless (!strcmp (comps[0], ""));
  fail_unless (!strcmp (comps[1], "foo%00bar"));
  fail_unless (!strcmp (comps[2], "qux baz"));

  g_strfreev (comps);
  gst_rtsp_url_free (url);
}

GST_END_TEST;

static Suite *
rtsp_suite (void)
{
  Suite *s = suite_create ("rtsp support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtsp_url_basic);
  tcase_add_test (tc_chain, test_rtsp_url_components_1);
  tcase_add_test (tc_chain, test_rtsp_url_components_2);
  tcase_add_test (tc_chain, test_rtsp_url_components_3);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = rtsp_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
