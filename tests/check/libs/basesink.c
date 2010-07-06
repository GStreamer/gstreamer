/* GStreamer
 *
 * Copyright (C) 2010 Alessandro Decina <alessandro.decina@collabora.co.uk>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbasesink.h>

GST_START_TEST (basesink_last_buffer_enabled)
{
  GstElement *src, *sink, *pipeline;
  GstBus *bus;
  GstMessage *msg;
  GstBuffer *last_buffer;

  pipeline = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  fail_unless (gst_bin_add (GST_BIN (pipeline), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink) == TRUE);
  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipeline);

  /* try with enable-last-buffer set to TRUE */
  g_object_set (src, "num-buffers", 1, NULL);
  fail_unless (gst_element_set_state (pipeline, GST_STATE_PLAYING)
      != GST_STATE_CHANGE_FAILURE);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* last-buffer should be != NULL */
  fail_unless (gst_base_sink_is_last_buffer_enabled (GST_BASE_SINK (sink))
      == TRUE);
  g_object_get (sink, "last-buffer", &last_buffer, NULL);
  fail_unless (last_buffer != NULL);
  gst_buffer_unref (last_buffer);

  /* set enable-last-buffer to FALSE now, this should set last-buffer to NULL */
  g_object_set (sink, "enable-last-buffer", FALSE, NULL);
  fail_unless (gst_base_sink_is_last_buffer_enabled (GST_BASE_SINK (sink))
      == FALSE);
  g_object_get (sink, "last-buffer", &last_buffer, NULL);
  fail_unless (last_buffer == NULL);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_INFO ("stopped");

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (basesink_last_buffer_disabled)
{
  GstElement *src, *sink, *pipeline;
  GstBus *bus;
  GstMessage *msg;
  GstBuffer *last_buffer;

  pipeline = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  fail_unless (gst_bin_add (GST_BIN (pipeline), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink) == TRUE);
  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipeline);

  /* set enable-last-buffer to FALSE */
  g_object_set (src, "num-buffers", 1, NULL);
  gst_base_sink_set_last_buffer_enabled (GST_BASE_SINK (sink), FALSE);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* last-buffer should be NULL */
  g_object_get (sink, "last-buffer", &last_buffer, NULL);
  fail_unless (last_buffer == NULL);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_INFO ("stopped");

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gst_basesrc_suite (void)
{
  Suite *s = suite_create ("GstBaseSink");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, basesink_last_buffer_enabled);
  tcase_add_test (tc, basesink_last_buffer_disabled);

  return s;
}

GST_CHECK_MAIN (gst_basesrc);
