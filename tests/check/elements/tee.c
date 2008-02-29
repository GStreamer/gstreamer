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
#define NUM_SUBSTREAMS 15
#define NUM_BUFFERS 3
  GstElement *pipeline, *src, *tee;
  GstElement *queues[NUM_SUBSTREAMS];
  GstElement *sinks[NUM_SUBSTREAMS];
  GstPad *req_pads[NUM_SUBSTREAMS];
  guint counts[NUM_SUBSTREAMS];
  GstBus *bus;
  GstMessage *msg;
  gint i;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);
  tee = gst_check_setup_element ("tee");
  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), tee));
  fail_unless (gst_element_link (src, tee));

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    GstPad *qpad;
    gchar name[32];

    counts[i] = 0;

    queues[i] = gst_check_setup_element ("queue");
    g_snprintf (name, 32, "queue%d", i);
    gst_object_set_name (GST_OBJECT (queues[i]), name);
    fail_unless (gst_bin_add (GST_BIN (pipeline), queues[i]));

    sinks[i] = gst_check_setup_element ("fakesink");
    g_snprintf (name, 32, "sink%d", i);
    gst_object_set_name (GST_OBJECT (sinks[i]), name);
    fail_unless (gst_bin_add (GST_BIN (pipeline), sinks[i]));
    fail_unless (gst_element_link (queues[i], sinks[i]));
    g_object_set (sinks[i], "signal-handoffs", TRUE, NULL);
    g_signal_connect (sinks[i], "handoff", (GCallback) handoff, &counts[i]);

    req_pads[i] = gst_element_get_request_pad (tee, "src%d");
    fail_unless (req_pads[i] != NULL);

    qpad = gst_element_get_pad (queues[i], "sink");
    fail_unless_equals_int (gst_pad_link (req_pads[i], qpad), GST_PAD_LINK_OK);
    gst_object_unref (qpad);
  }

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    fail_unless_equals_int (counts[i], NUM_BUFFERS);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    gst_element_release_request_pad (tee, req_pads[i]);
    gst_object_unref (req_pads[i]);
  }
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

  /* Pump 1000 buffers (10 bytes each) per second through tee for 5 secs */
  desc = "fakesrc datarate=10000 sizemin=10 sizemax=10 num-buffers=5000 ! "
      "video/x-raw-rgb,framerate=25/1 ! tee name=t ! "
      "queue max-size-buffers=2 ! fakesink sync=true";

  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  tee = gst_bin_get_by_name (GST_BIN (pipeline), "t");
  fail_if (tee == NULL);

  /* bring the pipeline to PLAYING, then start switching */
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* Wait for the pipeline to hit playing so that parse_launch can do the
   * initial link, otherwise we perform linking from multiple threads and cause
   * trouble */
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  for (i = 0; i < 50000; i++) {
    GstPad *pad;

    pad = gst_element_get_request_pad (tee, "src%d");
    gst_element_release_request_pad (tee, pad);
    gst_object_unref (pad);

    if ((msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, 0)))
      break;
  }

  /* now wait for completion or error */
  if (msg == NULL)
    msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (tee);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
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
