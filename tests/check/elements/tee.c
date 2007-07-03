/* GStreamer
 *
 * unit test for tee
 *
 * Copyright (C) <2007> Wim Taymans <wim dot taymans at gmail dot com>
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

static gint count1;
static gint count2;

static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
}

/* construct fakesrc num-buffers=3 ! tee name=t ! queue ! fakesink t. ! queue !
 * fakesink. Each fakesink should exactly receive 3 buffers.
 */
GST_START_TEST (test_num_buffers)
{
  GstElement *pipeline;
  GstElement *f1, *f2;
  gchar *desc;
  GstBus *bus;
  GstMessage *msg;

  desc = "fakesrc num-buffers=3 ! tee name=t ! queue ! fakesink name=f1 "
      "t. ! queue ! fakesink name=f2";
  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  f1 = gst_bin_get_by_name (GST_BIN (pipeline), "f1");
  fail_if (f1 == NULL);
  f2 = gst_bin_get_by_name (GST_BIN (pipeline), "f2");
  fail_if (f2 == NULL);

  count1 = 0;
  count2 = 0;

  g_object_set (G_OBJECT (f1), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (f1), "handoff", (GCallback) handoff, &count1);
  g_object_set (G_OBJECT (f2), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (f2), "handoff", (GCallback) handoff, &count2);

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_if (count1 != 3);
  fail_if (count2 != 3);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (f1);
  gst_object_unref (f2);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* we use fakesrc ! tee ! fakesink and then randomly request/release and link
 * some pads from tee. This should happily run without any errors. */
GST_START_TEST (test_stress)
{
  GstElement *pipeline;
  GstElement *tee;
  gchar *desc;
  GstBus *bus;
  GstMessage *msg;
  gint i;

  desc = "fakesrc num-buffers=100000 ! tee name=t ! queue ! fakesink";
  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  tee = gst_bin_get_by_name (GST_BIN (pipeline), "t");
  fail_if (tee == NULL);

  /* bring the pipeline to PLAYING, then start switching */
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  for (i = 0; i < 50000; i++) {
    GstPad *pad;

    pad = gst_element_get_request_pad (tee, "src%d");
    gst_element_release_request_pad (tee, pad);
    gst_object_unref (pad);
  }

  /* now wait for completion or error */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (tee);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

Suite *
tee_suite (void)
{
  Suite *s = suite_create ("tee");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_stress);

  return s;
}

GST_CHECK_MAIN (tee);
