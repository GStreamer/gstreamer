/* GStreamer unit tests for GstURI
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

GST_START_TEST (test_protocol_case)
{
  GstElement *element;
  GError *err = NULL;

  element = gst_element_make_from_uri (GST_URI_SRC, "file:///foo/bar", NULL,
      &err);

  /* no element? probably no registry, bail out */
  if (element == NULL && err->code == GST_URI_ERROR_UNSUPPORTED_PROTOCOL) {
    g_error_free (err);
    return;
  }

  gst_object_unref (element);
  element = gst_element_make_from_uri (GST_URI_SRC, "FILE:///foo/bar", NULL,
      NULL);
  fail_unless (element != NULL,
      "Got source for 'file://' URI but not for 'FILE://' URI");
  gst_object_unref (element);
}

GST_END_TEST;

GST_START_TEST (test_uri_get_location)
{
  gchar *l;

  /* URI with no location should return empty string */
  l = gst_uri_get_location ("dvd://");
  fail_unless (l != NULL);
  fail_unless_equals_string (l, "");
  g_free (l);

  /* URI with hostname */
  l = gst_uri_get_location ("smb://supercomputer/path/to/file");
  fail_unless (l != NULL);
  fail_unless_equals_string (l, "supercomputer/path/to/file");
  g_free (l);

  /* URI */
  l = gst_uri_get_location ("file:///path/to/file");
  fail_unless (l != NULL);
  fail_unless_equals_string (l, "/path/to/file");
  g_free (l);

  /* unescaping */
  l = gst_uri_get_location ("file:///path/to/some%20file");
  fail_unless (l != NULL);
  fail_unless_equals_string (l, "/path/to/some file");
  g_free (l);
}

GST_END_TEST;

#ifdef G_OS_WIN32

GST_START_TEST (test_win32_uri)
{
  gchar *uri, *l;

  uri = g_strdup ("file:///c:/my%20music/foo.ogg");
  l = gst_uri_get_location (uri);
  fail_unless (l != NULL);
  /* fail_unless_equals_string will screw up here in the failure case
   * because the string constant will be appended to the printf format
   * message string and contains a '%', that's why we use fail_unless here */
  fail_unless (g_str_equal (l, "c:/my music/foo.ogg"),
      "wrong location '%s' returned for URI '%s'", l, uri);
  g_free (l);
  g_free (uri);

  /* make sure the other variant with two slashes before the C: (which was
   * needed before because of a bug in _get_location()) still works */
  uri = g_strdup ("file://c:/my%20music/foo.ogg");
  l = gst_uri_get_location (uri);
  fail_unless (l != NULL);
  /* fail_unless_equals_string will screw up here in the failure case
   * because the string constant will be appended to the printf format
   * message string and contains a '%', that's why we use fail_unless here */
  fail_unless (g_str_equal (l, "c:/my music/foo.ogg"),
      "wrong location '%s' returned for URI '%s'", l, uri);
  g_free (l);
  g_free (uri);
}

GST_END_TEST;

#endif /* G_OS_WIN32 */

GST_START_TEST (test_uri_misc)
{
  /* require at least two characters for the protocol */
  fail_if (gst_uri_is_valid ("B:\\foo.txt"));
  fail_if (gst_uri_is_valid ("B:/foo.txt"));
  fail_if (gst_uri_is_valid ("B://foo.txt"));
  fail_if (gst_uri_is_valid ("B:foo.txt"));

  fail_unless (gst_uri_is_valid ("fd://0"));
  fail_unless (gst_uri_is_valid ("AB:\\foo.txt"));
  fail_unless (gst_uri_is_valid ("AB:/foo.txt"));
  fail_unless (gst_uri_is_valid ("AB://foo.txt"));
  fail_unless (gst_uri_is_valid ("AB:foo.txt"));

  fail_unless (gst_uri_is_valid ("ABC:/foo.txt"));
  fail_unless (gst_uri_is_valid ("ABC://foo.txt"));
  fail_unless (gst_uri_is_valid ("ABC:foo.txt"));

  fail_unless (gst_uri_is_valid ("ABCD:/foo.txt"));
  fail_unless (gst_uri_is_valid ("ABCD://foo.txt"));
  fail_unless (gst_uri_is_valid ("ABCD:foo.txt"));
}

GST_END_TEST;

GST_START_TEST (test_element_make_from_uri)
{
  GstElement *element;
  GError *err = NULL;

  element = gst_element_make_from_uri (GST_URI_SRC, "foo://", NULL, NULL);
  fail_unless (element == NULL);

  element = gst_element_make_from_uri (GST_URI_SRC, "foo://", NULL, &err);
  fail_unless (element == NULL);
  fail_unless (err != NULL);
  fail_unless (err->code == GST_URI_ERROR_UNSUPPORTED_PROTOCOL);
  g_error_free (err);
  err = NULL;

  if (gst_registry_check_feature_version (gst_registry_get (), "filesrc",
          GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO)) {
    element = gst_element_make_from_uri (GST_URI_SRC, "file://host/foo", NULL,
        &err);
    fail_unless (element == NULL);
    fail_unless (err != NULL);
    fail_unless (err->code == GST_URI_ERROR_BAD_URI);
    g_error_free (err);
    err = NULL;
  }
}

GST_END_TEST;

static Suite *
gst_uri_suite (void)
{
  Suite *s = suite_create ("GstURI");
  TCase *tc_chain = tcase_create ("uri");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_protocol_case);
  tcase_add_test (tc_chain, test_uri_get_location);
  tcase_add_test (tc_chain, test_uri_misc);
  tcase_add_test (tc_chain, test_element_make_from_uri);
#ifdef G_OS_WIN32
  tcase_add_test (tc_chain, test_win32_uri);
#endif

  return s;
}

GST_CHECK_MAIN (gst_uri);
