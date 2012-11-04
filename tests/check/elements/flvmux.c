/* GStreamer unit tests for flvmux
 *
 * Copyright (C) 2009 Tim-Philipp Müller  <tim centricular net>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/check/gstcheck.h>

#include <gst/gst.h>

static GstBusSyncReply
error_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error (msg, &err, &dbg);
    g_error ("ERROR: %s\n%s\n", err->message, dbg);
  }

  return GST_BUS_PASS;
}

static void
handoff_cb (GstElement * element, GstBuffer * buf, GstPad * pad,
    gint * p_counter)
{
  *p_counter += 1;
  GST_LOG ("counter = %d", *p_counter);
}

static void
mux_pcm_audio (guint num_buffers, guint repeat)
{
  GstElement *src, *sink, *flvmux, *conv, *pipeline;
  GstPad *sinkpad, *srcpad;
  gint counter;

  GST_LOG ("num_buffers = %u", num_buffers);

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL, "Failed to create pipeline!");

  /* kids, don't use a sync handler for this at home, really; we do because
   * we just want to abort and nothing else */
  gst_bus_set_sync_handler (GST_ELEMENT_BUS (pipeline), error_cb, NULL, NULL);

  src = gst_element_factory_make ("audiotestsrc", "audiotestsrc");
  fail_unless (src != NULL, "Failed to create 'audiotestsrc' element!");

  g_object_set (src, "num-buffers", num_buffers, NULL);

  conv = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (conv != NULL, "Failed to create 'audioconvert' element!");

  flvmux = gst_element_factory_make ("flvmux", "flvmux");
  fail_unless (flvmux != NULL, "Failed to create 'flvmux' element!");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Failed to create 'fakesink' element!");

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (handoff_cb), &counter);

  gst_bin_add_many (GST_BIN (pipeline), src, conv, flvmux, sink, NULL);

  fail_unless (gst_element_link (src, conv));
  fail_unless (gst_element_link (flvmux, sink));

  /* now link the elements */
  sinkpad = gst_element_get_request_pad (flvmux, "audio");
  fail_unless (sinkpad != NULL, "Could not get audio request pad");

  srcpad = gst_element_get_static_pad (conv, "src");
  fail_unless (srcpad != NULL, "Could not get audioconvert's source pad");

  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad), GST_PAD_LINK_OK);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  do {
    GstStateChangeReturn state_ret;
    GstMessage *msg;

    GST_LOG ("repeat=%d", repeat);

    counter = 0;

    state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    if (state_ret == GST_STATE_CHANGE_ASYNC) {
      GST_LOG ("waiting for pipeline to reach PAUSED state");
      state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
      fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
    }

    GST_LOG ("PAUSED, let's do the rest of it");

    state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_EOS, -1);
    fail_unless (msg != NULL, "Expected EOS message on bus!");

    GST_LOG ("EOS");
    gst_message_unref (msg);

    /* should have some output */
    fail_unless (counter > 2);

    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
        GST_STATE_CHANGE_SUCCESS);

    /* repeat = test re-usability */
    --repeat;
  } while (repeat > 0);

  gst_object_unref (pipeline);
}

GST_START_TEST (test_index_writing)
{
  /* note: there's a magic 128 value in flvmux when doing index writing */
  if ((__i__ % 33) == 1)
    mux_pcm_audio (__i__, 2);
}

GST_END_TEST;

static Suite *
flvmux_suite (void)
{
  Suite *s = suite_create ("flvmux");
  TCase *tc_chain = tcase_create ("general");
  gint loop = 499;

  suite_add_tcase (s, tc_chain);

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    loop = 140;
  }
#endif

  tcase_add_loop_test (tc_chain, test_index_writing, 1, loop);

  return s;
}

GST_CHECK_MAIN (flvmux)
