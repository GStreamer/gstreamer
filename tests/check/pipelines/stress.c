/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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


#include <gst/check/gstcheck.h>

static int playing = 1;
static int quit = 0;

static gboolean
change_state_timeout (gpointer data)
{
  GstElement *pipeline = (GstElement *) data;

  if (quit)
    return FALSE;

  if (playing) {
    playing = 0;
    gst_element_set_state (pipeline, GST_STATE_NULL);
  } else {
    playing = 1;
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
  }

  return TRUE;
}

static gboolean
quit_timeout (gpointer data)
{
  quit = 1;
  return FALSE;
}

GST_START_TEST (test_stress_preroll)
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline;

  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  pipeline = gst_element_factory_make ("pipeline", NULL);

  g_return_if_fail (fakesrc && fakesink && pipeline);

  g_object_set (G_OBJECT (fakesink), "preroll-queue-len", 4, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_timeout_add (500, &change_state_timeout, pipeline);
  g_timeout_add (10000, &quit_timeout, NULL);

  while (!quit) {
    g_main_context_iteration (NULL, TRUE);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_stress)
{
  GstElement *fakesrc, *fakesink, *pipeline;
  gint i;

  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  pipeline = gst_element_factory_make ("pipeline", NULL);

  g_return_if_fail (fakesrc && fakesink && pipeline);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  i = 100;
  while (i--) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
stress_suite (void)
{
  Suite *s = suite_create ("stress");
  TCase *tc_chain = tcase_create ("linear");

  /* Completely disable timeout for this test */
  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_stress);
  tcase_add_test (tc_chain, test_stress_preroll);

  return s;
}

GST_CHECK_MAIN (stress);
