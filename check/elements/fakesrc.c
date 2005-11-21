/* GStreamer
 *
 * unit test for fakesrc
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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
setup_fakesrc ()
{
  GstElement *fakesrc;

  GST_DEBUG ("setup_fakesrc");
  fakesrc = gst_check_setup_element ("fakesrc");
  mysinkpad = gst_check_setup_sink_pad (fakesrc, &sinktemplate, NULL);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);
  return fakesrc;
}

void
cleanup_fakesrc (GstElement * fakesrc)
{
  gst_check_teardown_sink_pad (fakesrc);
  gst_check_teardown_element (fakesrc);
}

GST_START_TEST (test_num_buffers)
{
  GstElement *src;

  src = setup_fakesrc ();
  g_object_set (G_OBJECT (src), "num-buffers", 3, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 3);
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_empty)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 1, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (GST_BUFFER_SIZE (buf) == 0);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_fixed)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (GST_BUFFER_SIZE (buf) == 8192);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_random)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 3, NULL);
  g_object_set (G_OBJECT (src), "sizemin", 4096, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_if (GST_BUFFER_SIZE (buf) > 8192);
    fail_if (GST_BUFFER_SIZE (buf) < 4096);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_no_preroll)
{
  GstElement *src;
  GstStateChangeReturn ret;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);

  ret = gst_element_set_state (src, GST_STATE_PAUSED);

  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "error going to paused the first time");

  ret = gst_element_set_state (src, GST_STATE_PAUSED);

  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "error going to paused the second time");

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

Suite *
fakesrc_suite (void)
{
  Suite *s = suite_create ("fakesrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_sizetype_empty);
  tcase_add_test (tc_chain, test_sizetype_fixed);
  tcase_add_test (tc_chain, test_sizetype_random);
  tcase_add_test (tc_chain, test_no_preroll);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = fakesrc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
