/* GStreamer unit test for queue
 *
 * Copyright (C) 2007 Tim-Philipp Müller  <tim centricular net>
 * Copyright (C) 2009 Mark Nauwelaerts  <mnauw users sourceforge net>
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

#include <gst/gst.h>

static GstPadProbeReturn
modify_caps (GstObject * pad, GstPadProbeInfo * info, gpointer data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstElement *filter = GST_ELEMENT (data);
  GstCaps *caps;

  fail_unless (event != NULL);
  fail_unless (GST_IS_EVENT (event));

  if (GST_EVENT_TYPE (event) != GST_EVENT_EOS)
    return GST_PAD_PROBE_OK;

  /* trigger caps negotiation error */
  caps = gst_caps_new_empty_simple ("video/x-raw");
  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_queue)
{
  GstStateChangeReturn state_ret;
  GstMessage *msg;
  GstElement *pipeline, *filter, *queue;
  GstBus *bus;
  GstPad *pad;
  guint probe;
  gchar *pipe_desc =
      g_strdup_printf ("fakesrc num-buffers=1 ! video/x-raw ! "
      "queue min-threshold-buffers=2 name=queue ! "
      "capsfilter name=nasty ! fakesink");

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);
  g_free (pipe_desc);

  filter = gst_bin_get_by_name (GST_BIN (pipeline), "nasty");
  fail_unless (filter != NULL);

  /* queue waits for all data and EOS to arrive */
  /* then probe forces downstream element to return negotiation error */
  queue = gst_bin_get_by_name (GST_BIN (pipeline), "queue");
  fail_unless (queue != NULL);
  pad = gst_element_get_static_pad (queue, "sink");
  fail_unless (pad != NULL);
  probe =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) modify_caps, filter, NULL);

  bus = gst_element_get_bus (pipeline);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS, 5 * GST_SECOND);
  fail_unless (msg != NULL, "timeout waiting for error or eos message");

  gst_message_unref (msg);
  gst_object_unref (bus);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_pad_remove_probe (pad, probe);
  gst_object_unref (queue);
  gst_object_unref (pad);
  gst_object_unref (filter);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
queue_suite (void)
{
  Suite *s = suite_create ("queue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_queue);

  return s;
}

GST_CHECK_MAIN (queue)
