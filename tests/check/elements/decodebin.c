/* GStreamer unit tests for decodebin
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <unistd.h>

static const gchar dummytext[] =
    "Quick Brown Fox Jumps over a Lazy Frog Quick Brown "
    "Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick "
    "Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog "
    "Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy "
    "Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a "
    "Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps "
    "over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox "
    "jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown "
    "Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick "
    "Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog "
    "Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy "
    "Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a "
    "Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps "
    "over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox ";

static void
src_need_data_cb (GstElement * src, guint size, gpointer data)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  buf = gst_buffer_new ();
  gst_buffer_take_memory (buf, 0,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          (gpointer) dummytext, NULL, sizeof (dummytext), 0,
          sizeof (dummytext)));

  GST_BUFFER_OFFSET (buf) = 0;

  g_signal_emit_by_name (src, "push-buffer", buf, &ret);

  fail_unless (ret == GST_FLOW_OK);
}

static void
decodebin_pad_added_cb (GstElement * decodebin, GstPad * pad,
    gboolean last, gboolean * p_flag)
{
  /* we should not be reached */
  fail_unless (decodebin == NULL, "pad-added should not be emitted");
}

/* make sure that decodebin errors out instead of creating a new decoded pad
 * if the entire stream is a plain text file */
GST_START_TEST (test_text_plain_streams)
{
  GstElement *pipe, *src, *decodebin;
  GstMessage *msg;

  pipe = gst_pipeline_new (NULL);
  fail_unless (pipe != NULL, "failed to create pipeline");

  src = gst_element_factory_make ("appsrc", "src");
  fail_unless (src != NULL, "Failed to create appsrc element");

  g_object_set (src, "emit-signals", TRUE, NULL);
  g_object_set (src, "num-buffers", 1, NULL);
  g_signal_connect (src, "need-data", G_CALLBACK (src_need_data_cb), NULL);

  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  fail_unless (decodebin != NULL, "Failed to create decodebin element");

  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (decodebin_pad_added_cb), NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src));
  fail_unless (gst_bin_add (GST_BIN (pipe), decodebin));
  fail_unless (gst_element_link (src, decodebin), "can't link src<->decodebin");

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* it's push-based, so should be async */
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);

  /* it should error out at some point */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static void
pad_added_plug_fakesink_cb (GstElement * decodebin, GstPad * srcpad,
    gboolean last, GstElement * pipeline)
{
  GstElement *sink;
  GstPad *sinkpad;

  GST_LOG ("Linking fakesink");

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (sink != NULL, "Failed to create fakesink element");

  gst_bin_add (GST_BIN (pipeline), sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad), GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);

  gst_element_set_state (sink, GST_STATE_PLAYING);
}

GST_START_TEST (test_reuse_without_decoders)
{
  GstElement *pipe, *src, *decodebin, *sink;

  pipe = gst_pipeline_new (NULL);
  fail_unless (pipe != NULL, "failed to create pipeline");

  src = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (src != NULL, "Failed to create audiotestsrc element");

  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  fail_unless (decodebin != NULL, "Failed to create decodebin element");

  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (pad_added_plug_fakesink_cb), pipe);

  fail_unless (gst_bin_add (GST_BIN (pipe), src));
  fail_unless (gst_bin_add (GST_BIN (pipe), decodebin));
  fail_unless (gst_element_link (src, decodebin), "can't link src<->decodebin");

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* it's push-based, so should be async */
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);

  /* wait for state change to complete */
  fail_unless_equals_int (gst_element_get_state (pipe, NULL, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);

  /* there shouldn't be any errors */
  fail_if (gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, 0) != NULL);

  /* reset */
  gst_element_set_state (pipe, GST_STATE_READY);

  sink = gst_bin_get_by_name (GST_BIN (pipe), "sink");
  gst_bin_remove (GST_BIN (pipe), sink);
  gst_element_set_state (sink, GST_STATE_NULL);
  gst_object_unref (sink);

  GST_LOG ("second try");

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* it's push-based, so should be async */
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);

  /* wait for state change to complete */
  fail_unless_equals_int (gst_element_get_state (pipe, NULL, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);

  /* there shouldn't be any errors */
  fail_if (gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, 0) != NULL);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static Suite *
decodebin_suite (void)
{
  Suite *s = suite_create ("decodebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_text_plain_streams);
  tcase_add_test (tc_chain, test_reuse_without_decoders);

  return s;
}

GST_CHECK_MAIN (decodebin);
