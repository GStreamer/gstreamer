/* GStreamer
 *
 * unit test for mimic
 *
 * Copyright 2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2009 Nokia Corp.
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

static GMainLoop *loop;

static void
eos_message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GST_DEBUG ("Received eos");
  g_main_loop_quit (loop);
}

GST_START_TEST (test_mimic_pipeline)
{
  GstElement *pipeline;
  GError *error = NULL;
  GstBus *bus;
  const gchar *bin_str = "videotestsrc num-buffers=10 ! mimenc ! "
      "mimdec ! fakesink";

  pipeline = gst_parse_launch (bin_str, &error);
  fail_unless (pipeline != NULL, "Error parsing pipeline: %s", bin_str,
      error ? error->message : "(invalid error)");

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", (GCallback) eos_message_cb, NULL);
  gst_object_unref (bus);

  fail_unless (gst_element_set_state (pipeline, GST_STATE_PLAYING)
      != GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
mimic_suite (void)
{
  Suite *s = suite_create ("mimic");
  TCase *tc_chain;

  tc_chain = tcase_create ("mimic_pipeline");
  tcase_add_test (tc_chain, test_mimic_pipeline);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (mimic)
