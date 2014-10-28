/* GStreamer
 *
 * unit test for fdsrc
 *
 * Copyright (C) <2005> Jan Schmidt <thaytan at mad dot scientist dot com>
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <gst/check/gstcheck.h>

static gboolean have_eos = FALSE;

static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    have_eos = TRUE;
  }

  gst_event_unref (event);
  return TRUE;
}

static GstElement *
setup_fdsrc (void)
{
  GstElement *fdsrc;

  GST_DEBUG ("setup_fdsrc");
  fdsrc = gst_check_setup_element ("fdsrc");
  mysinkpad = gst_check_setup_sink_pad (fdsrc, &sinktemplate);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);
  return fdsrc;
}

static void
cleanup_fdsrc (GstElement * fdsrc)
{
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (fdsrc);
  gst_check_teardown_element (fdsrc);
}

GST_START_TEST (test_num_buffers)
{
  GstElement *src;
  gint pipe_fd[2];
  gchar data[4096];

#ifndef G_OS_WIN32
  fail_if (pipe (pipe_fd) < 0);
#else
  fail_if (_pipe (pipe_fd, 2048, _O_BINARY) < 0);
#endif

  src = setup_fdsrc ();
  g_object_set (G_OBJECT (src), "num-buffers", 3, NULL);
  g_object_set (G_OBJECT (src), "fd", pipe_fd[0], NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

#if defined (G_OS_UNIX) && defined (O_NONBLOCK)
  {
    int flags;

    flags = fcntl (pipe_fd[1], F_GETFL, 0);
    fcntl (pipe_fd[1], F_SETFL, flags | O_NONBLOCK);
  }
#endif

  memset (data, 0, 4096);
  while (!have_eos) {
    int ret = write (pipe_fd[1], data, 4096);
    fail_if (ret < 0 && errno != EAGAIN);
    g_usleep (100);
  }

  fail_unless (g_list_length (buffers) == 3);
  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fdsrc (src);
  close (pipe_fd[0]);
  close (pipe_fd[1]);
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
}

GST_END_TEST;

GST_START_TEST (test_nonseeking)
{
  GstElement *src;
  GstQuery *seeking_query;
  gint pipe_fd[2];
  gchar data[4096];
  gboolean seekable;

#ifndef G_OS_WIN32
  fail_if (pipe (pipe_fd) < 0);
#else
  fail_if (_pipe (pipe_fd, 2048, _O_BINARY) < 0);
#endif

  src = setup_fdsrc ();
  g_object_set (G_OBJECT (src), "num-buffers", 3, NULL);
  g_object_set (G_OBJECT (src), "fd", pipe_fd[0], NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  memset (data, 0, 4096);
  fail_if (write (pipe_fd[1], data, 4096) < 0);

  /* Test that fdsrc is non-seekable with a pipe */
  fail_unless ((seeking_query = gst_query_new_seeking (GST_FORMAT_BYTES))
      != NULL);
  fail_unless (gst_element_query (src, seeking_query) == TRUE);
  gst_query_parse_seeking (seeking_query, NULL, &seekable, NULL, NULL);
  fail_unless (seekable == FALSE);
  gst_query_unref (seeking_query);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fdsrc (src);
  close (pipe_fd[0]);
  close (pipe_fd[1]);
}

GST_END_TEST;

GST_START_TEST (test_seeking)
{
  GstElement *src;
  gint in_fd;
  GstQuery *seeking_query;
  gboolean seekable;

#ifndef TESTFILE
#error TESTFILE not defined
#endif
  fail_if ((in_fd = open (TESTFILE, O_RDONLY)) < 0);
  src = setup_fdsrc ();

  g_object_set (G_OBJECT (src), "fd", in_fd, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  /* Test that fdsrc is seekable with a file fd */
  fail_unless ((seeking_query = gst_query_new_seeking (GST_FORMAT_BYTES))
      != NULL);
  fail_unless (gst_element_query (src, seeking_query) == TRUE);
  gst_query_parse_seeking (seeking_query, NULL, &seekable, NULL, NULL);
  fail_unless (seekable == TRUE);
  gst_query_unref (seeking_query);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fdsrc (src);
  close (in_fd);
}

GST_END_TEST;

static Suite *
fdsrc_suite (void)
{
  Suite *s = suite_create ("fdsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_nonseeking);
  tcase_add_test (tc_chain, test_seeking);

  return s;
}

GST_CHECK_MAIN (fdsrc);
