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

/* Taken from the GNet unit test and extended with other URIs:
 * https://git.gnome.org/browse/archive/gnet/plain/tests/check/gnet/gneturi.c
 */
struct QueryValue
{
  const gchar *key;
  const gchar *value;
};

struct URITest
{
  const gchar *str;
  struct
  {
    const gchar *scheme;
    const gchar *userinfo;
    const gchar *host;
    gint port;
    const gchar *path;
    /* needs to be updated if more than 10 */
    struct QueryValue query[10];
    const gchar *fragment;
  } uri;
};

static const struct URITest tests[] = {
  /* VALID URIS.  PARSING AND PRINTING OF THESE SHOULD NOT CHANGE */

  /* scheme/path */
  {"scheme:",
      {"scheme", NULL, NULL, GST_URI_NO_PORT, NULL, {{NULL, NULL}}, NULL}},

  {"scheme:path",
      {"scheme", NULL, NULL, GST_URI_NO_PORT, "path", {{NULL, NULL}}, NULL}},

  {"path",
      {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{NULL, NULL}}, NULL}},

  {"/path",
      {NULL, NULL, NULL, GST_URI_NO_PORT, "/path", {{NULL, NULL}}, NULL}},

  /* hostname/port */
  {"scheme://hostname/path",
        {"scheme", NULL, "hostname", GST_URI_NO_PORT, "/path", {{NULL, NULL}},
          NULL}},

  {"scheme://hostname:123/path",
      {"scheme", NULL, "hostname", 123, "/path", {{NULL, NULL}}, NULL}},

  /* ipv6 hostname/port */
  {"scheme://[01:23:45:67:89:ab:cd:ef]/path",
        {"scheme", NULL, "01:23:45:67:89:ab:cd:ef", GST_URI_NO_PORT, "/path",
          {{NULL, NULL}}, NULL}},

  {"scheme://[01:23:45:67:89:ab:cd:ef]:123/path",
        {"scheme", NULL, "01:23:45:67:89:ab:cd:ef", 123, "/path", {{NULL,
                  NULL}}, NULL}},

  /* query/fragment */
  {"path?query",
        {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{"query", NULL}, {NULL,
                  NULL}}, NULL}},
  {"path?query=value",
        {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{"query", "value"}, {NULL,
                  NULL}}, NULL}},

  {"path?query#fragment",
        {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{"query", NULL}, {NULL,
                  NULL}}, "fragment"}},

  {"path?query=value#fragment",
        {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{"query", "value"}, {NULL,
                  NULL}}, "fragment"}},

  {"scheme:path?query#fragment",
        {"scheme", NULL, NULL, GST_URI_NO_PORT, "path", {{"query", NULL}, {NULL,
                  NULL}}, "fragment"}},

  /* full */
  {"scheme://hostname:123/path?query#fragment",
        {"scheme", NULL, "hostname", 123, "/path", {{"query", NULL}, {NULL,
                  NULL}}, "fragment"}},

  {"scheme://hostname:123/path?query=value#fragment",
        {"scheme", NULL, "hostname", 123, "/path", {{"query", "value"}, {NULL,
                  NULL}}, "fragment"}},

  {"scheme://hostname:123?query",
        {"scheme", NULL, "hostname", 123, NULL, {{"query", NULL}, {NULL,
                  NULL}}, NULL}},

  {"scheme://hostname:123?query=value",
        {"scheme", NULL, "hostname", 123, NULL, {{"query", "value"}, {NULL,
                  NULL}}, NULL}},

  {"scheme://hostname:123?query#fragment",
        {"scheme", NULL, "hostname", 123, NULL, {{"query", NULL}, {NULL,
                  NULL}}, "fragment"}},

  {"scheme://hostname:123?query=value#fragment",
        {"scheme", NULL, "hostname", 123, NULL, {{"query", "value"}, {NULL,
                  NULL}}, "fragment"}},

  /* user/pass */
  {"scheme://userinfo@hostname",
        {"scheme", "userinfo", "hostname", GST_URI_NO_PORT, NULL, {{NULL,
                  NULL}}, NULL}},

  {"scheme://userinfo@hostname:123/path?query#fragment",
        {"scheme", "userinfo", "hostname", 123, "/path", {{"query", NULL},
              {NULL, NULL}}, "fragment"}},

  {"scheme://user:pass@hostname",
        {"scheme", "user:pass", "hostname", GST_URI_NO_PORT, NULL, {{NULL,
                  NULL}}, NULL}},

  {"scheme://user:pass@hostname:123/path?query#fragment",
        {"scheme", "user:pass", "hostname", 123, "/path", {{"query", NULL},
              {NULL, NULL}}, "fragment"}},

  /* FUNNY URIS.  PARSING AND PRINTING OF THESE MAY CHANGE */

  {"scheme:hostname:123/path?query#fragment",
        {"scheme", NULL, NULL, GST_URI_NO_PORT, "hostname:123/path", {{"query",
                  NULL}, {NULL, NULL}}, "fragment"}},

  {"scheme://:pass@hostname:123/path?query#fragment",
        {"scheme", ":pass", "hostname", 123, "/path", {{"query", NULL}, {NULL,
                  NULL}}, "fragment"}},

  /* Skip initial white space */
  {" \f\n\r\t\vscheme:",
      {"scheme", NULL, NULL, GST_URI_NO_PORT, NULL, {{NULL, NULL}}, NULL}},

  {" \f\n\r\t\vpath",
      {NULL, NULL, NULL, GST_URI_NO_PORT, "path", {{NULL, NULL}}, NULL}},

  /* file URI */
  {"file://host/home/joe/foo.txt",
        {"file", NULL, "host", GST_URI_NO_PORT, "/home/joe/foo.txt", {{NULL,
                  NULL}}, NULL}},
  {"file:///home/joe/foo.txt",
        {"file", NULL, NULL, GST_URI_NO_PORT, "/home/joe/foo.txt", {{NULL,
                  NULL}}, NULL}},
};

static const gchar *unparsable_uri_tests[] = {
  /* Path not started correctly */
  "scheme://hostname:123path?query#fragment",

  /* Brackets that don't close */
  "scheme://[01:23:45:67:89:ab:cd:ef:123/path",

  /* IPv6 hostname without brackets */
  "scheme://01:23:45:67:89:ab:cd:ef:123/path",
};

GST_START_TEST (test_url_parsing)
{
  GstUri *uri;
  GList *list;
  gchar *tmp_str;
  guint i, j;

  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    GST_DEBUG ("Testing URI '%s'", tests[i].str);

    uri = gst_uri_from_string (tests[i].str);
    fail_unless (uri != NULL);
    fail_unless_equals_string (gst_uri_get_scheme (uri), tests[i].uri.scheme);
    fail_unless_equals_string (gst_uri_get_userinfo (uri),
        tests[i].uri.userinfo);
    fail_unless_equals_string (gst_uri_get_host (uri), tests[i].uri.host);
    fail_unless_equals_int (gst_uri_get_port (uri), tests[i].uri.port);
    tmp_str = gst_uri_get_path (uri);
    fail_unless_equals_string (tmp_str, tests[i].uri.path);
    g_free (tmp_str);

    for (j = 0; j < 10; j++) {
      if (!tests[i].uri.query[j].key)
        break;

      if (tests[i].uri.query[j].value) {
        fail_unless_equals_string (gst_uri_get_query_value (uri,
                tests[i].uri.query[j].key), tests[i].uri.query[j].value);
      } else {
        fail_unless (gst_uri_query_has_key (uri, tests[i].uri.query[j].key));
      }
    }
    list = gst_uri_get_query_keys (uri);
    fail_unless_equals_int (j, g_list_length (list));
    g_list_free (list);
    gst_uri_unref (uri);
  }

  for (i = 0; i < G_N_ELEMENTS (unparsable_uri_tests); i++) {
    GST_DEBUG ("Testing unparsable URI '%s'", unparsable_uri_tests[i]);

    uri = gst_uri_from_string (unparsable_uri_tests[i]);
    fail_unless (uri == NULL);
  }
}

GST_END_TEST;

static const struct URITest url_presenting_tests[] = {
  /* check all URI elements present */
  {.uri = {"scheme", "user:pass", "host", 1234, "/path/to/dir",
          {{"query", NULL}, {"key", "value"}}, "fragment"},
      .str =
      "scheme://user:pass@host:1234/path/to/dir?query&key=value#fragment"},

  /* IPv6 literal should render in square brackets */
  {.uri = {"scheme", "user:pass", "12:34:56:78:9a:bc:de:f0", 1234,
          "/path/to/dir", {{"query", "value"}}, "fragment"},
      .str =
      "scheme://user:pass@[12:34:56:78:9a:bc:de:f0]:1234/path/to/dir?query=value#fragment"},
};

GST_START_TEST (test_url_presenting)
{
  GstUri *uri;
  gchar *result;
  guint i, j;

  for (i = 0; i < G_N_ELEMENTS (url_presenting_tests); i++) {
    uri = gst_uri_new (url_presenting_tests[i].uri.scheme,
        url_presenting_tests[i].uri.userinfo,
        url_presenting_tests[i].uri.host,
        url_presenting_tests[i].uri.port,
        url_presenting_tests[i].uri.path,
        NULL, url_presenting_tests[i].uri.fragment);
    fail_unless (uri != NULL);
    for (j = 0; j < 10; j++) {
      if (!url_presenting_tests[i].uri.query[j].key)
        break;

      fail_unless (gst_uri_set_query_value (uri,
              url_presenting_tests[i].uri.query[j].key,
              url_presenting_tests[i].uri.query[j].value));
    }

    result = gst_uri_to_string (uri);
    fail_unless_equals_string (result, url_presenting_tests[i].str);
    g_free (result);
    gst_uri_unref (uri);
  }
}

GST_END_TEST;

GST_START_TEST (test_url_normalization)
{
  GstUri *url;
  gchar *tmp_str;

  url =
      gst_uri_from_string
      ("ScHeMe://User:P%61ss@HOST.%63om:1234/path/./from/../to%7d/item%2dobj?qu%65ry=something#fr%61gment");
  fail_unless (gst_uri_normalize (url));
  fail_unless_equals_string (gst_uri_get_scheme (url), "scheme");
  fail_unless_equals_string (gst_uri_get_userinfo (url), "User:Pass");
  fail_unless_equals_string (gst_uri_get_host (url), "host.com");
  tmp_str = gst_uri_get_path (url);
  fail_unless_equals_string (tmp_str, "/path/to}/item-obj");
  g_free (tmp_str);
  fail_unless (gst_uri_query_has_key (url, "query"));
  fail_unless_equals_string (gst_uri_get_query_value (url, "query"),
      "something");
  fail_unless_equals_string (gst_uri_get_fragment (url), "fragment");
  gst_uri_unref (url);
}

GST_END_TEST;

GST_START_TEST (test_url_joining)
{
  GstUri *base, *rel, *joined;
  gchar *l;

  base =
      gst_uri_from_string
      ("http://example.com/path/to/dir/filename.html#fragment");

  /* test change of fragment only */
  rel = gst_uri_from_string ("#new_frag");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/filename.html#new_frag");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test addition of new query string */
  rel = gst_uri_from_string ("?key=val");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/filename.html?key=val");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test new base filename */
  rel = gst_uri_from_string ("new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test relative file same directory */
  rel = gst_uri_from_string ("./new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test relative file parent directory */
  rel = gst_uri_from_string ("../new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/path/to/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test relative file grandparent directory */
  rel = gst_uri_from_string ("../../new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/path/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test relative file root directory */
  rel = gst_uri_from_string ("../../../new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test relative file beyond root directory */
  rel = gst_uri_from_string ("../../../../new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test add subdirectory */
  rel = gst_uri_from_string ("subdir/new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/subdir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test change directory */
  rel = gst_uri_from_string ("../subdir/new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/subdir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  gst_uri_unref (base);

  /* change base for path ending in directory */
  base = gst_uri_from_string ("http://example.com/path/to/dir/");

  /* test adding file to directory */
  rel = gst_uri_from_string ("new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test adding file to directory using relative path */
  rel = gst_uri_from_string ("./new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l,
      "http://example.com/path/to/dir/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test filename in parent directory */
  rel = gst_uri_from_string ("../new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/path/to/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test path ending in '../' */
  rel = gst_uri_from_string ("one/two/../");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/path/to/dir/one/");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test path ending in '..' Result should be the same as when ending in '../' */
  rel = gst_uri_from_string ("one/two/..");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "http://example.com/path/to/dir/one/");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  /* test replace with absolute */
  rel = gst_uri_from_string ("https://ssl.example.com/new_filename.xml");
  joined = gst_uri_join (base, rel);
  l = gst_uri_to_string (joined);
  fail_unless_equals_string (l, "https://ssl.example.com/new_filename.xml");
  g_free (l);
  gst_uri_unref (joined);
  gst_uri_unref (rel);

  gst_uri_unref (base);
}

GST_END_TEST;

GST_START_TEST (test_url_equality)
{
  GstUri *url1, *url2;

  url1 =
      gst_uri_from_string
      ("ScHeMe://User:Pass@HOST.com:1234/path/./from/../to%7d/item%2dobj?query=something#fragment");

  /* equal */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (gst_uri_equal (url1, url2));
  fail_unless (gst_uri_equal (url2, url1));
  gst_uri_unref (url2);

  /* different fragment */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/item-obj?query=something#different-fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different query */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/item-obj?query=different-something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different path */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/different-item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different port */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:4321/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different host */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@different-host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different userinfo */
  url2 =
      gst_uri_from_string
      ("scheme://Different-User:Pass@host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different scheme */
  url2 =
      gst_uri_from_string
      ("different+scheme://User:Pass@host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no scheme) */
  url2 =
      gst_uri_from_string
      ("//User:Pass@host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no userinfo) */
  url2 =
      gst_uri_from_string
      ("scheme://host.com:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no host) */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@:1234/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no port) */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com/path/to%7D/item-obj?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no path) */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234?query=something#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no query) */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/item-obj#fragment");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* different (no fragment) */
  url2 =
      gst_uri_from_string
      ("scheme://User:Pass@host.com:1234/path/to%7D/item-obj?query=something");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  /* compare two NULL uris */
  fail_unless (gst_uri_equal (NULL, NULL));

  /* compare same object */
  fail_unless (gst_uri_equal (url1, url1));

  /* compare one NULL and one non-NULL uri */
  fail_unless (!gst_uri_equal (url1, NULL));
  fail_unless (!gst_uri_equal (NULL, url1));

  gst_uri_unref (url1);
}

GST_END_TEST;

GST_START_TEST (test_url_constructors)
{
  GstUri *url1, *url2;
  gchar *tmp_str;
  GHashTable *tmp_table;

  url1 =
      gst_uri_new ("scheme", "userinfo", "hostname", 1234, "/path/to/file",
      "query", "fragment");
  fail_unless_equals_string (gst_uri_get_scheme (url1), "scheme");
  fail_unless_equals_string (gst_uri_get_userinfo (url1), "userinfo");
  fail_unless_equals_string (gst_uri_get_host (url1), "hostname");
  fail_unless (gst_uri_get_port (url1) == 1234);
  tmp_str = gst_uri_get_path (url1);
  fail_unless_equals_string (tmp_str, "/path/to/file");
  g_free (tmp_str);
  tmp_table = gst_uri_get_query_table (url1);
  fail_unless (g_hash_table_size (tmp_table) == 1);
  fail_unless (g_hash_table_contains (tmp_table, "query"));
  fail_unless (g_hash_table_lookup (tmp_table, "query") == NULL);
  g_hash_table_unref (tmp_table);
  fail_unless_equals_string (gst_uri_get_fragment (url1), "fragment");
  tmp_str = gst_uri_to_string (url1);
  fail_unless_equals_string (tmp_str,
      "scheme://userinfo@hostname:1234/path/to/file?query#fragment");
  g_free (tmp_str);

  url2 =
      gst_uri_new_with_base (url1, NULL, NULL, NULL, GST_URI_NO_PORT,
      "new_file", NULL, NULL);
  fail_unless_equals_string (gst_uri_get_scheme (url2), "scheme");
  fail_unless_equals_string (gst_uri_get_userinfo (url2), "userinfo");
  fail_unless_equals_string (gst_uri_get_host (url2), "hostname");
  fail_unless (gst_uri_get_port (url2) == 1234);
  tmp_str = gst_uri_get_path (url2);
  fail_unless_equals_string (tmp_str, "/path/to/new_file");
  g_free (tmp_str);
  fail_unless (gst_uri_get_query_table (url2) == NULL);
  fail_unless (gst_uri_get_fragment (url2) == NULL);
  tmp_str = gst_uri_to_string (url2);
  fail_unless_equals_string (tmp_str,
      "scheme://userinfo@hostname:1234/path/to/new_file");
  g_free (tmp_str);
  gst_uri_unref (url2);

  url2 = gst_uri_from_string_with_base (url1, "/a/new/path/to/file");
  fail_unless_equals_string (gst_uri_get_scheme (url2), "scheme");
  fail_unless_equals_string (gst_uri_get_userinfo (url2), "userinfo");
  fail_unless_equals_string (gst_uri_get_host (url2), "hostname");
  fail_unless (gst_uri_get_port (url2) == 1234);
  tmp_str = gst_uri_get_path (url2);
  fail_unless_equals_string (tmp_str, "/a/new/path/to/file");
  g_free (tmp_str);
  fail_unless (gst_uri_get_query_table (url2) == NULL);
  fail_unless (gst_uri_get_fragment (url2) == NULL);
  tmp_str = gst_uri_to_string (url2);
  fail_unless_equals_string (tmp_str,
      "scheme://userinfo@hostname:1234/a/new/path/to/file");
  g_free (tmp_str);
  gst_uri_unref (url2);

  url2 = gst_uri_from_string_with_base (url1, "http://foobar.com/bla");
  fail_unless_equals_string (gst_uri_get_scheme (url2), "http");
  fail_unless_equals_string (gst_uri_get_host (url2), "foobar.com");
  fail_unless (gst_uri_get_port (url2) == 0);
  tmp_str = gst_uri_get_path (url2);
  fail_unless_equals_string (tmp_str, "/bla");
  g_free (tmp_str);
  fail_unless (gst_uri_get_query_table (url2) == NULL);
  fail_unless (gst_uri_get_fragment (url2) == NULL);
  tmp_str = gst_uri_to_string (url2);
  fail_unless_equals_string (tmp_str, "http://foobar.com/bla");
  g_free (tmp_str);
  gst_uri_unref (url2);

  url2 = gst_uri_copy (url1);
  fail_unless (gst_uri_equal (url1, url2));
  gst_uri_set_query_value (url2, "key", "value");
  fail_unless (!gst_uri_equal (url1, url2));
  gst_uri_unref (url2);

  gst_uri_unref (url1);
}

GST_END_TEST;

GST_START_TEST (test_url_get_set)
{
  GstUri *url;
  gchar *tmp_str;
  GList *tmp_list;

  url = gst_uri_from_string ("scheme://hostname/path/to/file?query#fragment");

  fail_unless (gst_uri_set_scheme (url, "new+scheme"));
  fail_unless_equals_string (gst_uri_get_scheme (url), "new+scheme");
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "new+scheme://hostname/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (gst_uri_set_scheme (url, NULL));
  fail_unless (gst_uri_get_scheme (url) == NULL);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str, "//hostname/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_scheme (NULL, "fail"));
  fail_unless (gst_uri_set_scheme (NULL, NULL));

  fail_unless (gst_uri_set_userinfo (url, "username:password"));
  fail_unless_equals_string (gst_uri_get_userinfo (url), "username:password");
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//username:password@hostname/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (gst_uri_set_userinfo (url, NULL));
  fail_unless (gst_uri_get_userinfo (url) == NULL);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str, "//hostname/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_userinfo (NULL, "fail"));
  fail_unless (gst_uri_set_userinfo (NULL, NULL));

  fail_unless (gst_uri_set_host (url, NULL));
  fail_unless (gst_uri_get_host (url) == NULL);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str, "/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (gst_uri_set_host (url, "example.com"));
  fail_unless_equals_string (gst_uri_get_host (url), "example.com");
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_host (NULL, "fail"));
  fail_unless (gst_uri_set_host (NULL, NULL));

  fail_unless (gst_uri_set_port (url, 12345));
  fail_unless (gst_uri_get_port (url) == 12345);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com:12345/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (gst_uri_set_port (url, GST_URI_NO_PORT));
  fail_unless (gst_uri_get_port (url) == GST_URI_NO_PORT);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_port (NULL, 1234));
  fail_unless (gst_uri_set_port (NULL, GST_URI_NO_PORT));

  fail_unless (gst_uri_append_path_segment (url, "here"));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/here?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_append_path_segment (NULL, "fail"));
  fail_unless (gst_uri_append_path_segment (NULL, NULL));

  fail_unless (gst_uri_append_path (url, "../there"));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/here/../there?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_append_path (NULL, "fail"));
  fail_unless (gst_uri_append_path (NULL, NULL));

  gst_uri_normalize (url);

  tmp_list = gst_uri_get_path_segments (url);
  fail_unless (tmp_list != NULL);
  tmp_list = g_list_append (tmp_list, g_strdup ("segment"));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there?query#fragment");
  g_free (tmp_str);
  fail_unless (gst_uri_set_path_segments (url, tmp_list));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query#fragment");
  g_free (tmp_str);

  tmp_list = g_list_append (NULL, g_strdup ("test"));
  fail_unless (!gst_uri_set_path_segments (NULL, tmp_list));
  fail_unless (gst_uri_set_path_segments (NULL, NULL));

  fail_unless (gst_uri_set_query_value (url, "key", "value"));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query&key=value#fragment");
  g_free (tmp_str);

  fail_unless (gst_uri_set_query_value (url, "key", NULL));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query&key#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_query_value (NULL, "key", "value"));

  fail_unless (gst_uri_remove_query_key (url, "key"));
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query#fragment");
  g_free (tmp_str);

  fail_unless (!gst_uri_remove_query_key (url, "key"));
  fail_unless (!gst_uri_remove_query_key (NULL, "key"));

  fail_unless (gst_uri_set_fragment (url, NULL));
  fail_unless (gst_uri_get_fragment (url) == NULL);
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query");
  g_free (tmp_str);

  fail_unless (gst_uri_set_fragment (url, "tag"));
  fail_unless_equals_string (gst_uri_get_fragment (url), "tag");
  tmp_str = gst_uri_to_string (url);
  fail_unless_equals_string (tmp_str,
      "//example.com/path/to/file/there/segment?query#tag");
  g_free (tmp_str);

  fail_unless (!gst_uri_set_fragment (NULL, "can't set if no URI"));
  fail_unless (gst_uri_set_fragment (NULL, NULL));

  gst_uri_unref (url);
}

GST_END_TEST;

GST_START_TEST (test_url_get_media_fragment_table)
{
  GstUri *url;
  gchar *val;
  GHashTable *table;

  /* Examples at https://www.w3.org/TR/media-frags/#processing-media-fragment-uri */

  /* TEST "t=1" */
  url = gst_uri_from_string ("http://foo/var/file#t=1");
  table = gst_uri_get_media_fragment_table (url);
  fail_unless (table);
  fail_unless (g_hash_table_size (table) == 1);
  fail_unless (g_hash_table_lookup_extended (table, "t", NULL,
          (gpointer) & val));
  fail_unless_equals_string ("1", val);
  g_hash_table_unref (table);
  gst_uri_unref (url);

  /* NOTE: Media Fragments URI 1.0 (W3C) is saying that
   * "Multiple occurrences of the same dimension: only the last valid occurrence
   *  of a dimension (e.g. t=10 in #t=2&t=10) is interpreted and all previous
   *  occurrences (valid or invalid) SHOULD be ignored by the user agent"
   */
  /* TEST "t=1&t=2" */
  url = gst_uri_from_string ("http://foo/var/file#t=1&t=2");
  table = gst_uri_get_media_fragment_table (url);
  fail_unless (table);
  fail_unless (g_hash_table_size (table) == 1);
  fail_unless (g_hash_table_lookup_extended (table, "t", NULL,
          (gpointer) & val));
  fail_unless_equals_string ("2", val);
  g_hash_table_unref (table);
  gst_uri_unref (url);

  /* TEST "a=b=c" */
  url = gst_uri_from_string ("http://foo/var/file#a=b=c");
  table = gst_uri_get_media_fragment_table (url);
  fail_unless (table);
  fail_unless (g_hash_table_size (table) == 1);
  fail_unless (g_hash_table_lookup_extended (table, "a", NULL,
          (gpointer) & val));
  fail_unless_equals_string ("b=c", val);
  g_hash_table_unref (table);
  gst_uri_unref (url);

  /* TEST "a&b=c" */
  url = gst_uri_from_string ("http://foo/var/file#a&b=c");
  table = gst_uri_get_media_fragment_table (url);
  fail_unless (table);
  fail_unless (g_hash_table_size (table) == 2);
  fail_unless (g_hash_table_lookup_extended (table, "a", NULL,
          (gpointer) & val));
  fail_unless (val == NULL);
  fail_unless (g_hash_table_lookup_extended (table, "b", NULL,
          (gpointer) & val));
  fail_unless_equals_string ("c", val);
  g_hash_table_unref (table);
  gst_uri_unref (url);

  /* TEST "%74=%6ept%3A%310" */
  url = gst_uri_from_string ("http://foo/var/file#%74=%6ept%3A%310");
  table = gst_uri_get_media_fragment_table (url);
  fail_unless (table);
  fail_unless (g_hash_table_size (table) == 1);
  fail_unless (g_hash_table_lookup_extended (table, "t", NULL,
          (gpointer) & val));
  fail_unless_equals_string ("npt:10", val);
  g_hash_table_unref (table);
  gst_uri_unref (url);
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
  tcase_add_test (tc_chain, test_url_parsing);
  tcase_add_test (tc_chain, test_url_presenting);
  tcase_add_test (tc_chain, test_url_normalization);
  tcase_add_test (tc_chain, test_url_joining);
  tcase_add_test (tc_chain, test_url_equality);
  tcase_add_test (tc_chain, test_url_constructors);
  tcase_add_test (tc_chain, test_url_get_set);
  tcase_add_test (tc_chain, test_url_get_media_fragment_table);

  return s;
}

GST_CHECK_MAIN (gst_uri);
