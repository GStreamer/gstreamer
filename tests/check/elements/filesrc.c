/* GStreamer
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gst/check/gstcheck.h>

static gboolean have_eos = FALSE;
static GCond *eos_cond;
static GMutex *event_mutex;

static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
event_func (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;

  g_mutex_lock (event_mutex);
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    have_eos = TRUE;
    GST_DEBUG ("signal EOS");
    g_cond_broadcast (eos_cond);
  }
  g_mutex_unlock (event_mutex);

  gst_event_unref (event);

  return res;
}

static void
wait_eos (void)
{
  g_mutex_lock (event_mutex);
  GST_DEBUG ("waiting for EOS");
  while (!have_eos) {
    g_cond_wait (eos_cond, event_mutex);
  }
  GST_DEBUG ("received EOS");
  g_mutex_unlock (event_mutex);
}

static GstElement *
setup_filesrc (void)
{
  GstElement *filesrc;

  GST_DEBUG ("setup_filesrc");
  filesrc = gst_check_setup_element ("filesrc");
  mysinkpad = gst_check_setup_sink_pad (filesrc, &sinktemplate, NULL);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);

  eos_cond = g_cond_new ();
  event_mutex = g_mutex_new ();

  return filesrc;
}

static void
cleanup_filesrc (GstElement * filesrc)
{
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (filesrc);
  gst_check_teardown_element (filesrc);

  g_cond_free (eos_cond);
  g_mutex_free (event_mutex);
}

GST_START_TEST (test_seeking)
{
  GstElement *src;
  GstQuery *seeking_query;
  gboolean seekable;

#ifndef TESTFILE
#error TESTFILE not defined
#endif
  src = setup_filesrc ();

  g_object_set (G_OBJECT (src), "location", TESTFILE, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  /* Test that filesrc is seekable with a file fd */
  fail_unless ((seeking_query = gst_query_new_seeking (GST_FORMAT_BYTES))
      != NULL);
  fail_unless (gst_element_query (src, seeking_query) == TRUE);
  gst_query_parse_seeking (seeking_query, NULL, &seekable, NULL, NULL);
  fail_unless (seekable == TRUE);
  gst_query_unref (seeking_query);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_filesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_reverse)
{
  GstElement *src;

#ifndef TESTFILE
#error TESTFILE not defined
#endif
  src = setup_filesrc ();

  g_object_set (G_OBJECT (src), "location", TESTFILE, NULL);
  /* we're going to perform the seek in ready */
  fail_unless (gst_element_set_state (src,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* reverse seek from end to start */
  gst_element_seek (src, -1.0, GST_FORMAT_BYTES, 0, GST_SEEK_TYPE_SET, 100,
      GST_SEEK_TYPE_SET, -1);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  /* wait for EOS */
  wait_eos ();

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_filesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_pull)
{
  GstElement *src;
  GstQuery *seeking_query;
  gboolean res, seekable;
  gint64 start, stop;
  GstPad *pad;
  GstFlowReturn ret;
  GstBuffer *buffer1, *buffer2;

  src = setup_filesrc ();

  g_object_set (G_OBJECT (src), "location", TESTFILE, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* get the source pad */
  pad = gst_element_get_static_pad (src, "src");
  fail_unless (pad != NULL);

  /* activate the pad in pull mode */
  res = gst_pad_activate_pull (pad, TRUE);
  fail_unless (res == TRUE);

  /* not start playing */
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  /* Test that filesrc is seekable with a file fd */
  fail_unless ((seeking_query = gst_query_new_seeking (GST_FORMAT_BYTES))
      != NULL);
  fail_unless (gst_element_query (src, seeking_query) == TRUE);

  /* get the seeking capabilities */
  gst_query_parse_seeking (seeking_query, NULL, &seekable, &start, &stop);
  fail_unless (seekable == TRUE);
  fail_unless (start == 0);
  fail_unless (start != -1);
  gst_query_unref (seeking_query);

  /* do some pulls */
  ret = gst_pad_get_range (pad, 0, 100, &buffer1);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer1 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer1) == 100);

  ret = gst_pad_get_range (pad, 0, 50, &buffer2);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer2 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer2) == 50);

  /* this should be the same */
  fail_unless (memcmp (GST_BUFFER_DATA (buffer1), GST_BUFFER_DATA (buffer2),
          50) == 0);

  gst_buffer_unref (buffer2);

  /* read next 50 bytes */
  ret = gst_pad_get_range (pad, 50, 50, &buffer2);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer2 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer2) == 50);

  /* compare with previously read data */
  fail_unless (memcmp (GST_BUFFER_DATA (buffer1) + 50,
          GST_BUFFER_DATA (buffer2), 50) == 0);

  gst_buffer_unref (buffer1);
  gst_buffer_unref (buffer2);

  /* read 10 bytes at end-10 should give exactly 10 bytes */
  ret = gst_pad_get_range (pad, stop - 10, 10, &buffer1);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer1 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer1) == 10);
  gst_buffer_unref (buffer1);

  /* read 20 bytes at end-10 should give exactly 10 bytes */
  ret = gst_pad_get_range (pad, stop - 10, 20, &buffer1);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer1 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer1) == 10);
  gst_buffer_unref (buffer1);

  /* read 0 bytes at end-1 should return 0 bytes */
  ret = gst_pad_get_range (pad, stop - 1, 0, &buffer1);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer1 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer1) == 0);
  gst_buffer_unref (buffer1);

  /* read 10 bytes at end-1 should return 1 byte */
  ret = gst_pad_get_range (pad, stop - 1, 10, &buffer1);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer1 != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer1) == 1);
  gst_buffer_unref (buffer1);

  /* read 0 bytes at end should EOS */
  ret = gst_pad_get_range (pad, stop, 0, &buffer1);
  fail_unless (ret == GST_FLOW_UNEXPECTED);

  /* read 10 bytes before end should EOS */
  ret = gst_pad_get_range (pad, stop, 10, &buffer1);
  fail_unless (ret == GST_FLOW_UNEXPECTED);

  /* read 0 bytes after end should EOS */
  ret = gst_pad_get_range (pad, stop + 10, 0, &buffer1);
  fail_unless (ret == GST_FLOW_UNEXPECTED);

  /* read 10 bytes after end should EOS too */
  ret = gst_pad_get_range (pad, stop + 10, 10, &buffer1);
  fail_unless (ret == GST_FLOW_UNEXPECTED);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  gst_object_unref (pad);
  cleanup_filesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_coverage)
{
  GstElement *src;
  gchar *location;
  GstBus *bus;
  GstMessage *message;

  src = setup_filesrc ();
  bus = gst_bus_new ();

  gst_element_set_bus (src, bus);

  g_object_set (G_OBJECT (src), "location", "/i/do/not/exist", NULL);
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_unless_equals_string (location, "/i/do/not/exist");
  g_free (location);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE,
      "could set to playing with wrong location");

  /* a state change and an error */
  fail_if ((message = gst_bus_pop (bus)) == NULL);
  gst_message_unref (message);
  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, RESOURCE, NOT_FOUND);
  gst_message_unref (message);

  g_object_set (G_OBJECT (src), "location", NULL, NULL);
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_if (location);

  /* cleanup */
  gst_element_set_bus (src, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_filesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_uri_interface)
{
  GstElement *src;
  gchar *location;
  GstBus *bus;

  src = setup_filesrc ();
  bus = gst_bus_new ();

  gst_element_set_bus (src, bus);

  g_object_set (G_OBJECT (src), "location", "/i/do/not/exist", NULL);
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_unless_equals_string (location, "/i/do/not/exist");
  g_free (location);

  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (src));
  fail_unless_equals_string (location, "file:///i/do/not/exist");

  /* should accept file:///foo/bar URIs */
  fail_unless (gst_uri_handler_set_uri (GST_URI_HANDLER (src),
          "file:///foo/bar"));
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (src));
  fail_unless_equals_string (location, "file:///foo/bar");
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/bar");
  g_free (location);

  /* should accept file://localhost/foo/bar URIs */
  fail_unless (gst_uri_handler_set_uri (GST_URI_HANDLER (src),
          "file://localhost/foo/baz"));
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (src));
  fail_unless_equals_string (location, "file:///foo/baz");
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/baz");
  g_free (location);

  /* should escape non-uri characters for the URI but not for the location */
  g_object_set (G_OBJECT (src), "location", "/foo/b?r", NULL);
  g_object_get (G_OBJECT (src), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/b?r");
  g_free (location);
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (src));
  fail_unless_equals_string (location, "file:///foo/b%3Fr");

  /* should fail with other hostnames */
  fail_if (gst_uri_handler_set_uri (GST_URI_HANDLER (src),
          "file://hostname/foo/foo"));

  /* cleanup */
  gst_element_set_bus (src, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_filesrc (src);
}

GST_END_TEST;

static void
check_uri_for_uri (GstElement * e, const gchar * in_uri, const gchar * uri)
{
  GstQuery *query;
  gchar *query_uri = NULL;

  gst_uri_handler_set_uri (GST_URI_HANDLER (e), in_uri);

  query = gst_query_new_uri ();
  fail_unless (gst_element_query (e, query));
  gst_query_parse_uri (query, &query_uri);
  gst_query_unref (query);

  if (uri != NULL) {
    fail_unless_equals_string (query_uri, uri);
  } else {
    gchar *fn;

    fail_unless (gst_uri_is_valid (query_uri));
    fn = g_filename_from_uri (query_uri, NULL, NULL);
    fail_unless (g_path_is_absolute (fn));
    fail_unless (fn != NULL);
    g_free (fn);
  }

  g_free (query_uri);
}

static void
check_uri_for_location (GstElement * e, const gchar * location,
    const gchar * uri)
{
  GstQuery *query;
  gchar *query_uri = NULL;

  g_object_set (e, "location", location, NULL);
  query = gst_query_new_uri ();
  fail_unless (gst_element_query (e, query));
  gst_query_parse_uri (query, &query_uri);
  gst_query_unref (query);

  if (uri != NULL) {
    fail_unless_equals_string (query_uri, uri);
  } else {
    gchar *fn;

    fail_unless (gst_uri_is_valid (query_uri));
    fn = g_filename_from_uri (query_uri, NULL, NULL);
    fail_unless (g_path_is_absolute (fn));
    fail_unless (fn != NULL);
    g_free (fn);
  }

  g_free (query_uri);
}

GST_START_TEST (test_uri_query)
{
  GstElement *src;

  src = setup_filesrc ();

#ifdef G_OS_UNIX
  {
    GST_INFO ("*nix");
    check_uri_for_location (src, "/i/do/not/exist", "file:///i/do/not/exist");
    check_uri_for_location (src, "/i/do/not/../exist", "file:///i/do/exist");
    check_uri_for_location (src, "/i/do/not/.././exist", "file:///i/do/exist");
    check_uri_for_location (src, "/i/./do/not/../exist", "file:///i/do/exist");
    check_uri_for_location (src, "/i/do/./not/../exist", "file:///i/do/exist");
    check_uri_for_location (src, "/i/do/not/./../exist", "file:///i/do/exist");
    check_uri_for_location (src, "/i/./do/./././././exist",
        "file:///i/do/exist");
    check_uri_for_location (src, "/i/do/not/../../exist", "file:///i/exist");
    check_uri_for_location (src, "/i/../not/../exist", "file:///exist");
    /* hard to test relative URIs, just make sure it returns an URI of sorts */
    check_uri_for_location (src, "foo", NULL);
    check_uri_for_location (src, "foo/../bar", NULL);
    check_uri_for_location (src, "./foo", NULL);
    check_uri_for_location (src, "../foo", NULL);
    check_uri_for_location (src, "foo/./bar", NULL);
    /* make sure non-ASCII characters are escaped properly (U+00F6 here) */
    check_uri_for_location (src, "/i/./d\303\266/not/../exist",
        "file:///i/d%C3%B6/exist");
    /* let's see what happens if we set a malformed URI with ISO-8859-1 chars,
     * i.e. one that the input characters haven't been escaped properly. We
     * should get back a properly escaped URI */
    check_uri_for_uri (src, "file:///M\366t\366r", "file:///M%F6t%F6r");
  }
#endif

  cleanup_filesrc (src);
}

GST_END_TEST;

static Suite *
filesrc_suite (void)
{
  Suite *s = suite_create ("filesrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_seeking);
  tcase_add_test (tc_chain, test_reverse);
  tcase_add_test (tc_chain, test_pull);
  tcase_add_test (tc_chain, test_coverage);
  tcase_add_test (tc_chain, test_uri_interface);
  tcase_add_test (tc_chain, test_uri_query);

  return s;
}

GST_CHECK_MAIN (filesrc);
