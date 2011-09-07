/* GStreamer unit test for the filesink element
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <glib.h>
#include <glib/gstdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>             /* for close() */
#endif

#include <gst/check/gstcheck.h>

static GstPad *mysrcpad;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElement *
setup_filesink (void)
{
  GstElement *filesink;

  GST_DEBUG ("setup_filesink");
  filesink = gst_check_setup_element ("filesink");
  mysrcpad = gst_check_setup_src_pad (filesink, &srctemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  return filesink;
}

static void
cleanup_filesink (GstElement * filesink)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_check_teardown_src_pad (filesink);
  gst_check_teardown_element (filesink);
}

#if 0
/* this queries via the element vfunc, which is currently not implemented */
#define CHECK_QUERY_POSITION(filesink,format,position)                  \
    G_STMT_START {                                                       \
      GstFormat fmt = format;                                            \
      gint64 pos;                                                        \
      fail_unless (gst_element_query_position (filesink, &fmt, &pos));   \
      fail_unless_equals_int (pos, position);                            \
    } G_STMT_END
#else
#define CHECK_QUERY_POSITION(filesink,format,position)                   \
    G_STMT_START {                                                       \
      GstFormat fmt = format;                                            \
      GstPad *pad;                                                       \
      gint64 pos;                                                        \
      pad = gst_element_get_static_pad (filesink, "sink");               \
      fail_unless (gst_pad_query_position (pad, &fmt, &pos));            \
      fail_unless_equals_int (pos, position);                            \
      gst_object_unref (pad);                                            \
    } G_STMT_END
#endif

#define PUSH_BYTES(num_bytes)                                             \
    G_STMT_START {                                                        \
      GstBuffer *buf = gst_buffer_new_and_alloc(num_bytes);               \
      GRand *rand = g_rand_new_with_seed (num_bytes);                     \
      guint i;                                                            \
      for (i = 0; i < num_bytes; ++i)                                     \
        GST_BUFFER_DATA(buf)[i] = (g_rand_int (rand) >> 24) & 0xff;       \
      fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK); \
      g_rand_free (rand);                                                 \
    } G_STMT_END

/* TODO: we don't check that the data is actually written to the right
 * position after a seek */
GST_START_TEST (test_seeking)
{
  const gchar *tmpdir;
  GstElement *filesink;
  gchar *tmp_fn;
  gint fd;

  tmpdir = g_get_tmp_dir ();
  if (tmpdir == NULL)
    return;

  /* this is just silly, but gcc warns if we try to use tpmnam() */
  tmp_fn = g_build_filename (tmpdir, "gstreamer-filesink-test-XXXXXX", NULL);
  fd = g_mkstemp (tmp_fn);
  if (fd < 0) {
    GST_ERROR ("can't create temp file %s: %s", tmp_fn, g_strerror (errno));
    g_free (tmp_fn);
    return;
  }
  /* don't want the file, just a filename (hence silly, see above) */
  close (fd);
  g_remove (tmp_fn);

  filesink = setup_filesink ();

  GST_LOG ("using temp file '%s'", tmp_fn);
  g_object_set (filesink, "location", tmp_fn, NULL);

  fail_unless_equals_int (gst_element_set_state (filesink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

#if 0
  /* Test that filesink is seekable with a file fd */
  /* filesink doesn't implement seekable query at the moment */
  GstQuery *seeking_query;
  gboolean seekable;

  fail_unless ((seeking_query = gst_query_new_seeking (GST_FORMAT_BYTES))
      != NULL);
  fail_unless (gst_element_query (filesink, seeking_query) == TRUE);
  gst_query_parse_seeking (seeking_query, NULL, &seekable, NULL, NULL);
  fail_unless (seekable == TRUE);
  gst_query_unref (seeking_query);
#endif

  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0)));

  CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 0);

  /* push buffer with size 0 and NULL data */
  PUSH_BYTES (0);
  CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 0);

  PUSH_BYTES (1);
  CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 1);

  PUSH_BYTES (99);
  CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 100);

  PUSH_BYTES (8800);
  CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 8900);

  if (gst_pad_push_event (mysrcpad,
          gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES, 8800, -1,
              0))) {
    GST_LOG ("seek ok");
    /* make sure that new position is reported immediately */
    CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 8800);
    PUSH_BYTES (1);
    CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 8801);
    PUSH_BYTES (9256);
    CHECK_QUERY_POSITION (filesink, GST_FORMAT_BYTES, 18057);
  } else {
    GST_INFO ("seeking not supported for tempfile?!");
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (gst_element_set_state (filesink, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  /* cleanup */
  cleanup_filesink (filesink);

  /* check that we wrote data to the right position after the seek */
  {
    gchar *data = NULL;
    gsize len;

    fail_unless (g_file_get_contents (tmp_fn, &data, &len, NULL),
        "Failed to read in newly-created file '%s'", tmp_fn);
    fail_unless_equals_int (len, 18057);
    {
      /* we wrote 9256 bytes at position 8801 */
      GRand *rand = g_rand_new_with_seed (9256);
      guint i;

      for (i = 0; i < 9256; ++i) {
        guint8 byte_written = *(((guint8 *) data) + 8801 + i);

        fail_unless_equals_int (byte_written, g_rand_int (rand) >> 24);
      }
      g_rand_free (rand);
    }
    g_free (data);
  }

  /* remove file */
  g_remove (tmp_fn);
  g_free (tmp_fn);
}

GST_END_TEST;

GST_START_TEST (test_coverage)
{
  GstElement *filesink;
  gchar *location;
  GstBus *bus;
  GstMessage *message;

  filesink = setup_filesink ();
  bus = gst_bus_new ();

  gst_element_set_bus (filesink, bus);

  g_object_set (filesink, "location", "/i/do/not/exist", NULL);
  g_object_get (filesink, "location", &location, NULL);
  fail_unless_equals_string (location, "/i/do/not/exist");
  g_free (location);

  fail_unless_equals_int (gst_element_set_state (filesink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_FAILURE);

  /* a state change and an error */
  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, RESOURCE, OPEN_WRITE);
  gst_message_unref (message);

  g_object_set (filesink, "location", NULL, NULL);
  g_object_get (filesink, "location", &location, NULL);
  fail_if (location);

  /* cleanup */
  gst_element_set_bus (filesink, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_filesink (filesink);
}

GST_END_TEST;

GST_START_TEST (test_uri_interface)
{
  GstElement *filesink;
  gchar *location;
  GstBus *bus;

  filesink = setup_filesink ();
  bus = gst_bus_new ();

  gst_element_set_bus (filesink, bus);

  g_object_set (G_OBJECT (filesink), "location", "/i/do/not/exist", NULL);
  g_object_get (G_OBJECT (filesink), "location", &location, NULL);
  fail_unless_equals_string (location, "/i/do/not/exist");
  g_free (location);

  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (filesink));
  fail_unless_equals_string (location, "file:///i/do/not/exist");

  /* should accept file:///foo/bar URIs */
  fail_unless (gst_uri_handler_set_uri (GST_URI_HANDLER (filesink),
          "file:///foo/bar"));
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (filesink));
  fail_unless_equals_string (location, "file:///foo/bar");
  g_object_get (G_OBJECT (filesink), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/bar");
  g_free (location);

  /* should accept file://localhost/foo/bar URIs */
  fail_unless (gst_uri_handler_set_uri (GST_URI_HANDLER (filesink),
          "file://localhost/foo/baz"));
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (filesink));
  fail_unless_equals_string (location, "file:///foo/baz");
  g_object_get (G_OBJECT (filesink), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/baz");
  g_free (location);

  /* should escape non-uri characters for the URI but not for the location */
  g_object_set (G_OBJECT (filesink), "location", "/foo/b?r", NULL);
  g_object_get (G_OBJECT (filesink), "location", &location, NULL);
  fail_unless_equals_string (location, "/foo/b?r");
  g_free (location);
  location = (gchar *) gst_uri_handler_get_uri (GST_URI_HANDLER (filesink));
  fail_unless_equals_string (location, "file:///foo/b%3Fr");

  /* should fail with other hostnames */
  fail_if (gst_uri_handler_set_uri (GST_URI_HANDLER (filesink),
          "file://hostname/foo/foo"));

  /* cleanup */
  gst_element_set_bus (filesink, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_filesink (filesink);
}

GST_END_TEST;

static Suite *
filesink_suite (void)
{
  Suite *s = suite_create ("filesink");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_coverage);
  tcase_add_test (tc_chain, test_uri_interface);
  tcase_add_test (tc_chain, test_seeking);

  return s;
}

GST_CHECK_MAIN (filesink);
