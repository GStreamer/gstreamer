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

gboolean have_eos = FALSE;

GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

gboolean
event_func (GstPad * pad, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    have_eos = TRUE;
    gst_event_unref (event);
    return TRUE;
  }

  gst_event_unref (event);
  return FALSE;
}

GstElement *
setup_filesrc ()
{
  GstElement *filesrc;

  GST_DEBUG ("setup_filesrc");
  filesrc = gst_check_setup_element ("filesrc");
  mysinkpad = gst_check_setup_sink_pad (filesrc, &sinktemplate, NULL);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);
  return filesrc;
}

void
cleanup_filesrc (GstElement * filesrc)
{
  gst_check_teardown_sink_pad (filesrc);
  gst_check_teardown_element (filesrc);
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

Suite *
filesrc_suite (void)
{
  Suite *s = suite_create ("filesrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_seeking);
  tcase_add_test (tc_chain, test_coverage);

  return s;
}

GST_CHECK_MAIN (filesrc);
